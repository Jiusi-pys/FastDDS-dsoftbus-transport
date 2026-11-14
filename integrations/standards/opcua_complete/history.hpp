#pragma once
#include "record_dds.hpp"
#include "../dds_ua_server/ua_type.hpp"
#include <deque>
namespace d4 {
struct Sample{Record record;UA_DateTime stamp=0;Clock::time_point received;bool valid=true;UA_DateTime server_stamp=0;bool extra_data=false;};
struct HistoryEntry{Sample latest;std::deque<Sample> values;};
class Retention{
    unsigned capacity_,retention_;
public:
    std::map<unsigned,HistoryEntry> entries;
    Retention(unsigned capacity,unsigned retention):capacity_(capacity),retention_(retention){check(capacity>=1&&capacity<=16&&retention>=100&&retention<=60000,"D4 retention bounds");}
    void expire(Clock::time_point now){for(auto& item:entries){auto& q=item.second.values;while(!q.empty()&&now-q.front().received>=std::chrono::milliseconds(retention_))q.pop_front();}}
    void insert(Sample sample){
        check(entries.count(sample.record.id())||entries.size()<4,"D4 instance capacity");expire(sample.received);auto& entry=entries[sample.record.id()];entry.latest=sample;entry.values.push_back(std::move(sample));if(entry.values.size()>capacity_)entry.values.pop_front();
    }
    std::vector<Sample> select(unsigned key,UA_DateTime start,UA_DateTime end,Clock::time_point now){
        expire(now);std::vector<Sample> result;auto it=entries.find(key);if(it==entries.end())return result;
        bool reverse=(start&&end&&start>end)||(!start&&end);auto low=start,high=end;
        if(start&&end&&start>end){low=end;high=start;}
        for(const auto& item:it->second.values)if((!low||item.stamp>=low)&&(!high||item.stamp<=high))result.push_back(item);
        std::stable_sort(result.begin(),result.end(),[](const Sample& a,const Sample& b){return a.stamp<b.stamp;});
        std::vector<Sample> newest;
        for(auto& sample:result){
            if(!newest.empty()&&newest.back().stamp==sample.stamp){newest.back()=std::move(sample);newest.back().extra_data=true;}
            else newest.push_back(std::move(sample));
        }
        if(reverse)std::reverse(newest.begin(),newest.end());return newest;
    }
};
enum class Field{Value,Id,Sequence,Label,Payload};
struct HistoryNode{unsigned key;Field field;};
inline UA_StatusCode data_value(const Sample& sample,Field field,const d2::RecordType& type,UA_DataValue& out,UA_TimestampsToReturn stamps){
    UA_UInt32 id=sample.record.id(),seq=sample.record.sequence_number();
    UA_ByteString label{sample.record.label().size(),reinterpret_cast<UA_Byte*>(const_cast<char*>(sample.record.label().data()))};
    UA_ByteString payload{sample.record.payload().size(),sample.record.payload().empty()?static_cast<UA_Byte*>(UA_EMPTY_ARRAY_SENTINEL):const_cast<UA_Byte*>(sample.record.payload().data())};
    d2::UaRecord record{id,seq,label,payload};const void* value=&record;const UA_DataType* target=&type.type;
    if(field==Field::Id){value=&id;target=&UA_TYPES[UA_TYPES_UINT32];}
    if(field==Field::Sequence){value=&seq;target=&UA_TYPES[UA_TYPES_UINT32];}
    if(field==Field::Label){value=&label;target=&UA_TYPES[UA_TYPES_BYTESTRING];}
    if(field==Field::Payload){value=&payload;target=&UA_TYPES[UA_TYPES_BYTESTRING];}
    auto code=UA_Variant_setScalarCopy(&out.value,value,target);if(code!=UA_STATUSCODE_GOOD)return code;out.hasValue=true;out.hasStatus=true;out.status=sample.valid?UA_STATUSCODE_GOOD:UA_STATUSCODE_BADOUTOFSERVICE;
    // OPC UA StatusCode HistorianBits.ExtraData is bit3 within DataValue info.
    if(sample.extra_data)out.status|=UA_STATUSCODE_INFOTYPE_DATAVALUE|UA_StatusCode(0x08);
    if(stamps==UA_TIMESTAMPSTORETURN_SOURCE||stamps==UA_TIMESTAMPSTORETURN_BOTH){out.hasSourceTimestamp=true;out.sourceTimestamp=sample.stamp;}
    if(stamps==UA_TIMESTAMPSTORETURN_SERVER||stamps==UA_TIMESTAMPSTORETURN_BOTH){out.hasServerTimestamp=true;out.serverTimestamp=sample.server_stamp;}
    return UA_STATUSCODE_GOOD;
}
class HistoryGateway{
    Config c_;Retention retention_;d2::RecordType type_;UA_Server* server_=nullptr;UA_UInt16 ns_=0;
    std::map<std::string,HistoryNode> nodes_;
    std::map<unsigned,fd::InstanceHandle_t> handles_;
    struct Cursor{std::string session,node;std::vector<Sample> values;size_t offset=0;unsigned page=0;Clock::time_point expires;UA_TimestampsToReturn stamps=UA_TIMESTAMPSTORETURN_SOURCE;};
    std::map<std::string,Cursor> cursors_;uint64_t next_=0;bool failed_=false;
    static std::string node_text(const UA_NodeId* id){
        UA_String text=UA_STRING_NULL;good(UA_NodeId_print(id,&text));std::string result(reinterpret_cast<char*>(text.data),text.length);UA_String_clear(&text);return result;
    }
    std::string token(){check(next_!=UINT64_MAX,"D4 token exhaustion");return std::to_string(++next_);}
    void expire(){auto now=Clock::now();retention_.expire(now);for(auto it=cursors_.begin();it!=cursors_.end();)if(now>=it->second.expires)it=cursors_.erase(it);else ++it;}
    static void read_raw(UA_Server*,void* context,const UA_NodeId* session,void*,const UA_RequestHeader*,const UA_ReadRawModifiedDetails* details,
                         UA_TimestampsToReturn stamps,UA_Boolean release,size_t count,const UA_HistoryReadValueId* ids,UA_HistoryReadResponse* response,UA_HistoryData* const * const history) noexcept{
        auto& self=*static_cast<HistoryGateway*>(context);
        try{self.expire();if(static_cast<unsigned>(stamps)>static_cast<unsigned>(UA_TIMESTAMPSTORETURN_BOTH)){response->responseHeader.serviceResult=UA_STATUSCODE_BADTIMESTAMPSTORETURNINVALID;return;}
        if(count>8){response->responseHeader.serviceResult=UA_STATUSCODE_BADTOOMANYOPERATIONS;return;}for(size_t n=0;n<count;++n){
            auto& result=response->results[n];auto name=node_text(&ids[n].nodeId);auto found=self.nodes_.find(name);
            if(found==self.nodes_.end()){result.statusCode=UA_STATUSCODE_BADNODEIDUNKNOWN;continue;}
            if(ids[n].indexRange.length){result.statusCode=UA_STATUSCODE_BADINDEXRANGEINVALID;continue;}
            std::string sid=node_text(session),old=ids[n].continuationPoint.length?std::string(reinterpret_cast<char*>(ids[n].continuationPoint.data),ids[n].continuationPoint.length):std::string();Cursor cursor;
            if(!old.empty()){
                auto it=self.cursors_.find(old);if(it==self.cursors_.end()||it->second.session!=sid||it->second.node!=name){result.statusCode=UA_STATUSCODE_BADCONTINUATIONPOINTINVALID;continue;}
                cursor=std::move(it->second);self.cursors_.erase(it);if(release){result.statusCode=UA_STATUSCODE_GOOD;continue;}
            }else{
                if(release){result.statusCode=UA_STATUSCODE_BADCONTINUATIONPOINTINVALID;continue;}
                if(unsigned(details->startTime!=0)+unsigned(details->endTime!=0)+unsigned(details->numValuesPerNode!=0)<2){result.statusCode=UA_STATUSCODE_BADINVALIDARGUMENT;continue;}
                if(details->returnBounds||details->isReadModified){result.statusCode=UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED;continue;}
                cursor.session=sid;cursor.node=name;cursor.stamps=stamps;cursor.values=self.retention_.select(found->second.key,details->startTime,details->endTime,Clock::now());
                cursor.page=details->numValuesPerNode?std::min<unsigned>(details->numValuesPerNode,16):16;cursor.expires=Clock::now()+std::chrono::milliseconds(self.c_.retention_ms);
                for(const auto& sample:cursor.values)cursor.expires=std::min(cursor.expires,sample.received+std::chrono::milliseconds(self.c_.retention_ms));
            }
            size_t amount=std::min<size_t>(cursor.page,cursor.values.size()-cursor.offset);
            if(!amount){result.statusCode=UA_STATUSCODE_GOODNODATA;continue;}
            bool more=cursor.offset+amount<cursor.values.size();
            if(more&&self.cursors_.size()>=8){result.statusCode=UA_STATUSCODE_BADNOCONTINUATIONPOINTS;continue;}
            auto* data=history[n];data->dataValues=static_cast<UA_DataValue*>(UA_Array_new(amount,&UA_TYPES[UA_TYPES_DATAVALUE]));check(data->dataValues,"D4 history data allocation");data->dataValuesSize=amount;
            for(size_t i=0;i<amount;++i)good(data_value(cursor.values[cursor.offset+i],found->second.field,self.type_,data->dataValues[i],cursor.stamps));
            cursor.offset+=amount;
            if(more){auto key=self.token();result.continuationPoint=UA_STRING_ALLOC(key.c_str());check(result.continuationPoint.data,"D4 continuation allocation");self.cursors_.emplace(std::move(key),std::move(cursor));}
            result.statusCode=UA_STATUSCODE_GOOD;
        }}catch(...){self.failed_=true;response->responseHeader.serviceResult=UA_STATUSCODE_BADINTERNALERROR;}
    }
    void create_nodes(unsigned key){
        const std::pair<const char*,Field> fields[]={{"Value",Field::Value},{"id",Field::Id},{"sequence_number",Field::Sequence},{"label",Field::Label},{"payload",Field::Payload}};
        for(const auto& item:fields){
            std::string name="Instance_"+std::to_string(key)+"_"+item.first;UA_NodeId id=UA_NODEID_STRING(ns_,const_cast<char*>(name.c_str()));UA_VariableAttributes a=UA_VariableAttributes_default;
            a.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>(name.c_str()));a.historizing=true;a.accessLevel=UA_ACCESSLEVELMASK_READ|UA_ACCESSLEVELMASK_HISTORYREAD;
            a.dataType=item.second==Field::Value?type_.type.typeId:item.second==Field::Label||item.second==Field::Payload?UA_TYPES[UA_TYPES_BYTESTRING].typeId:UA_TYPES[UA_TYPES_UINT32].typeId;
            UA_DataValue initial;UA_DataValue_init(&initial);good(data_value(retention_.entries.at(key).latest,item.second,type_,initial,UA_TIMESTAMPSTORETURN_BOTH));a.value=initial.value;
            auto code=UA_Server_addVariableNode(server_,id,UA_NODEID_NUMERIC(0,UA_NS0ID_OBJECTSFOLDER),UA_NODEID_NUMERIC(0,UA_NS0ID_ORGANIZES),UA_QUALIFIEDNAME(ns_,const_cast<char*>(name.c_str())),UA_NODEID_NUMERIC(0,UA_NS0ID_BASEDATAVARIABLETYPE),a,nullptr,nullptr);UA_DataValue_clear(&initial);good(code);
            nodes_.emplace(node_text(&id),HistoryNode{key,item.second});
        }
    }
    void update(const Sample& sample){
        for(const auto& item:nodes_)if(item.second.key==sample.record.id()){
            auto at=item.first.find(";s=");check(at!=std::string::npos,"D4 node format");auto value=item.first.substr(at+3);UA_DataValue data;UA_DataValue_init(&data);
            auto code=data_value(sample,item.second.field,type_,data,UA_TIMESTAMPSTORETURN_BOTH);
            if(code==UA_STATUSCODE_GOOD)code=UA_Server_writeDataValue(server_,UA_NODEID_STRING(ns_,const_cast<char*>(value.c_str())),data);UA_DataValue_clear(&data);good(code);
        }
    }
    void model(){
        ns_=UA_Server_addNamespace(server_,"urn:fastdds:standards:d4:history");check(ns_>0,"D4 namespace");type_.init(ns_);UA_Server_getConfig(server_)->customDataTypes=&type_.array;
        UA_DataTypeAttributes a=UA_DataTypeAttributes_default;a.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>("BoundedRecord"));
        good(UA_Server_addDataTypeNode(server_,type_.type.typeId,UA_NODEID_NUMERIC(0,UA_NS0ID_STRUCTURE),UA_NODEID_NUMERIC(0,UA_NS0ID_HASSUBTYPE),UA_QUALIFIEDNAME(ns_,const_cast<char*>("BoundedRecord")),a,nullptr,nullptr));
        UA_ObjectAttributes o=UA_ObjectAttributes_default;o.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>("Default Binary"));
        good(UA_Server_addObjectNode(server_,type_.type.binaryEncodingId,UA_NODEID_NULL,UA_NODEID_NULL,UA_QUALIFIEDNAME(0,const_cast<char*>("Default Binary")),UA_NODEID_NUMERIC(0,UA_NS0ID_DATATYPEENCODINGTYPE),o,nullptr,nullptr));
        good(UA_Server_addReference(server_,type_.type.typeId,UA_NODEID_NUMERIC(0,UA_NS0ID_HASENCODING),UA_EXPANDEDNODEID_NUMERIC(ns_,3002),true));
        auto* c=UA_Server_getConfig(server_);c->historyDatabase.context=this;c->historyDatabase.readRaw=read_raw;
        UA_Boolean access=true;UA_Variant capability;UA_Variant_init(&capability);UA_Variant_setScalar(&capability,&access,&UA_TYPES[UA_TYPES_BOOLEAN]);
        good(UA_Server_writeValue(server_,UA_NODEID_NUMERIC(0,UA_NS0ID_HISTORYSERVERCAPABILITIES_ACCESSHISTORYDATACAPABILITY),capability));
        UA_UInt32 maximum=16;UA_Variant_setScalar(&capability,&maximum,&UA_TYPES[UA_TYPES_UINT32]);
        good(UA_Server_writeValue(server_,UA_NODEID_NUMERIC(0,UA_NS0ID_HISTORYSERVERCAPABILITIES_MAXRETURNDATAVALUES),capability));
    }
public:
    explicit HistoryGateway(Config c):c_(std::move(c)),retention_(c_.capacity,c_.retention_ms){}
    int run(){
        std::unique_ptr<RecordDds> dds;bool started=false;unsigned received=0;
        try{
            dds=std::make_unique<RecordDds>();dds->open(c_);server_=UA_Server_new();check(server_,"D4 server");auto* config=UA_Server_getConfig(server_);good(UA_ServerConfig_setMinimal(config,static_cast<UA_UInt16>(c_.port),nullptr));
            config->tcpMaxMsgSize=65536;config->tcpMaxChunks=16;config->maxNodesPerRead=16;
            UA_Array_delete(config->serverUrls,config->serverUrlsSize,&UA_TYPES[UA_TYPES_STRING]);config->serverUrls=static_cast<UA_String*>(UA_Array_new(1,&UA_TYPES[UA_TYPES_STRING]));check(config->serverUrls,"D4 URL allocation");config->serverUrlsSize=1;
            config->serverUrls[0]=UA_STRING_ALLOC(("opc.tcp://127.0.0.1:"+std::to_string(c_.port)).c_str());check(config->serverUrls[0].data,"D4 URL");model();good(UA_Server_run_startup(server_));started=true;
            std::cout<<"D4_HISTORY_READY namespace="<<ns_<<std::endl;auto end=Clock::now()+std::chrono::milliseconds(c_.run_ms);
            while(!fs::exists(c_.stop)&&!failed_){
                check(Clock::now()<end,"D4 history deadline");expire();
                for(unsigned budget=0;budget<16;++budget){Record value;fd::SampleInfo info;auto code=dds->reader->take_next_sample(&value,&info);if(code==D4_DDS_EMPTY)break;check(code==D4_DDS_OK,"D4 native take");
                    if(!info.valid_data){auto old=std::find_if(handles_.begin(),handles_.end(),[&](const auto& item){return item.second==info.instance_handle;});check(old!=handles_.end(),"D4 lifecycle without observed key");value=retention_.entries.at(old->first).latest.record;}
                    check(value.label().size()<=64&&value.payload().size()<=256&&value.label().find('\0')==std::string::npos,"D4 sample bounds");
                    bool first=retention_.entries.find(value.id())==retention_.entries.end();Sample sample{value,(static_cast<UA_Int64>(info.source_timestamp.seconds())+11644473600LL)*10000000LL+info.source_timestamp.nanosec()/100,Clock::now(),bool(info.valid_data),UA_DateTime_now()};
                    if(info.valid_data)handles_[value.id()]=info.instance_handle;
                    retention_.insert(sample);if(first)create_nodes(value.id());update(sample);++received;
                    std::cout<<"D4_HISTORY_SAMPLE key="<<value.id()<<" sequence="<<value.sequence_number()<<" timestamp="<<sample.stamp<<std::endl;
                }
                UA_Server_run_iterate(server_,false);std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }catch(const std::exception& e){std::cerr<<"D4_HISTORY_FAIL "<<e.what()<<std::endl;failed_=true;}
        if(server_){if(started&&UA_Server_run_shutdown(server_)!=UA_STATUSCODE_GOOD)failed_=true;UA_Server_delete(server_);server_=nullptr;}
        if(dds&&!dds->close())failed_=true;
        std::cout<<"D4_HISTORY_CLOSED exit="<<(failed_?3:0)<<" owners="<<(dds&&dds->owns())<<" received="<<received<<std::endl;return failed_?3:0;
    }
};
}
