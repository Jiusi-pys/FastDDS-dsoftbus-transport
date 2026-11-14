#include "dds.hpp"
#include "profile.hpp"
#include "ua_type.hpp"
#include <open62541/server_config_default.h>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <deque>
#include <iostream>
#include <thread>

namespace d2 {
class DdsUaGateway {
    Config config_;Dds dds_;UA_Server* server_=nullptr;UA_UInt16 ns_=0;RecordType ua_type_;
    bool started_=false,failed_=false;uint64_t received_=0,invalid_=0,writes_=0;
    struct Entry{Record record;bool has_value=false,valid=false,registered=false;UA_UInt32 state=0;UA_DateTime stamp=0;fd::InstanceHandle_t handle,received_handle;};
    std::map<uint32_t,Entry> entries_;
    enum Field{Id,Sequence,Label,Payload,Value,Valid,State,Timestamp};
    struct Context{DdsUaGateway* gateway;uint32_t id;Field field;};std::deque<Context> contexts_;
    static void good(UA_StatusCode code){require(code==UA_STATUSCODE_GOOD,"UA model operation");}
    UA_NodeId numeric(unsigned id)const{return UA_NODEID_NUMERIC(ns_,id);}
    UA_NodeId string_id(const std::string& id)const{return UA_NODEID_STRING(ns_,const_cast<char*>(id.c_str()));}
    static UA_QualifiedName browse(UA_UInt16 ns,const std::string& name){return UA_QUALIFIEDNAME(ns,const_cast<char*>(name.c_str()));}
    void object(unsigned id,unsigned parent,const char* name,unsigned type=UA_NS0ID_BASEOBJECTTYPE){
        auto a=UA_ObjectAttributes_default;a.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>(name));
        good(UA_Server_addObjectNode(server_,numeric(id),parent?numeric(parent):UA_NODEID_NUMERIC(0,UA_NS0ID_OBJECTSFOLDER),UA_NODEID_NUMERIC(0,UA_NS0ID_HASCOMPONENT),browse(ns_,name),UA_NODEID_NUMERIC(0,type),a,nullptr,nullptr));
    }
    void property(const UA_NodeId& parent,const std::string& id,const char* name,const void* value,const UA_DataType* type){
        auto a=UA_VariableAttributes_default;a.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>(name));a.dataType=type->typeId;a.valueRank=UA_VALUERANK_SCALAR;
        good(UA_Variant_setScalarCopy(&a.value,value,type));auto result=UA_Server_addVariableNode(server_,string_id(id),parent,UA_NODEID_NUMERIC(0,UA_NS0ID_HASPROPERTY),browse(ns_,name),UA_NODEID_NUMERIC(0,UA_NS0ID_PROPERTYTYPE),a,nullptr,nullptr);
        UA_Variant_clear(&a.value);good(result);
    }
    Entry& ensure(uint32_t id){
        auto found=entries_.find(id);if(found!=entries_.end())return found->second;
        require(entries_.size()<config_.capacity,"instance capacity exceeded");
        auto& entry=entries_[id];entry.record.id(id);
        auto base="Instance_"+std::to_string(id);auto a=UA_ObjectAttributes_default;a.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>(base.c_str()));
        good(UA_Server_addObjectNode(server_,string_id(base),numeric(130),UA_NODEID_NUMERIC(0,UA_NS0ID_HASCOMPONENT),browse(ns_,base),numeric(4001),a,nullptr,nullptr));
        const char* names[]={"id","sequence_number","label","payload","Value","ValidData","InstanceState","SourceTimestamp"};
        const UA_DataType* types[]={&UA_TYPES[UA_TYPES_UINT32],&UA_TYPES[UA_TYPES_UINT32],&UA_TYPES[UA_TYPES_BYTESTRING],&UA_TYPES[UA_TYPES_BYTESTRING],&ua_type_.type,&UA_TYPES[UA_TYPES_BOOLEAN],&UA_TYPES[UA_TYPES_UINT32],&UA_TYPES[UA_TYPES_DATETIME]};
        for(unsigned i=0;i<8;++i){
            contexts_.push_back({this,id,static_cast<Field>(i)});auto attr=UA_VariableAttributes_default;
            attr.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>(names[i]));attr.dataType=types[i]->typeId;attr.valueRank=UA_VALUERANK_SCALAR;
            attr.accessLevel=UA_ACCESSLEVELMASK_READ|(i==Value?UA_ACCESSLEVELMASK_WRITE:0);
            UA_DataSource source{read,i==Value?write:nullptr};
            good(UA_Server_addDataSourceVariableNode(server_,string_id(base+"/"+names[i]),string_id(base),UA_NODEID_NUMERIC(0,UA_NS0ID_HASCOMPONENT),browse(ns_,names[i]),UA_NODEID_NUMERIC(0,UA_NS0ID_BASEDATAVARIABLETYPE),attr,source,&contexts_.back(),nullptr));
        }
        return entry;
    }
    static UA_StatusCode read(UA_Server*,const UA_NodeId*,void*,const UA_NodeId*,void* context,UA_Boolean timestamp,const UA_NumericRange* range,UA_DataValue* out) noexcept {
        auto& c=*static_cast<Context*>(context);auto& g=*c.gateway;
        try{
            if(range){out->hasStatus=true;out->status=UA_STATUSCODE_BADINDEXRANGEINVALID;return UA_STATUSCODE_GOOD;}
            auto& e=g.entries_.at(c.id);const void* data=nullptr;const UA_DataType* type=nullptr;
            UA_UInt32 id=c.id,seq=e.record.sequence_number();UA_Boolean valid=e.valid;
            UA_ByteString label{e.record.label().size(),reinterpret_cast<UA_Byte*>(const_cast<char*>(e.record.label().data()))};
            UA_ByteString payload{e.record.payload().size(),const_cast<UA_Byte*>(e.record.payload().data())};UaRecord record{id,seq,label,payload};
            switch(c.field){case Id:data=&id;type=&UA_TYPES[UA_TYPES_UINT32];break;case Sequence:data=&seq;type=&UA_TYPES[UA_TYPES_UINT32];break;
                case Label:data=&label;type=&UA_TYPES[UA_TYPES_BYTESTRING];break;case Payload:data=&payload;type=&UA_TYPES[UA_TYPES_BYTESTRING];break;
                case Value:data=&record;type=&g.ua_type_.type;break;case Valid:data=&valid;type=&UA_TYPES[UA_TYPES_BOOLEAN];break;
                case State:data=&e.state;type=&UA_TYPES[UA_TYPES_UINT32];break;case Timestamp:data=&e.stamp;type=&UA_TYPES[UA_TYPES_DATETIME];break;}
            bool value_field=c.field==Sequence||c.field==Label||c.field==Payload||c.field==Value;
            out->hasStatus=true;out->status=value_field?(!e.has_value?UA_STATUSCODE_BADWAITINGFORINITIALDATA:!e.valid?UA_STATUSCODE_BADOUTOFSERVICE:UA_STATUSCODE_GOOD):UA_STATUSCODE_GOOD;
            // Keep a typed placeholder before the first sample; its bad quality forbids treating it as DDS data.
            {auto code=UA_Variant_setScalarCopy(&out->value,data,type);if(code!=UA_STATUSCODE_GOOD){out->status=code;return UA_STATUSCODE_GOOD;}out->hasValue=true;}
            if(timestamp&&e.has_value){out->hasSourceTimestamp=true;out->sourceTimestamp=e.stamp;}
            return UA_STATUSCODE_GOOD;
        }catch(...){g.failed_=true;out->hasStatus=true;out->status=UA_STATUSCODE_BADINTERNALERROR;return UA_STATUSCODE_GOOD;}
    }
    bool acknowledge(const char* phase,uint32_t key){
        fd::PublicationMatchedStatus matched;auto match_code=dds_.writer->get_publication_matched_status(matched);
        std::cout<<"D2_ACK_BEGIN phase="<<phase<<" key="<<key<<" matched="<<matched.current_count<<" status_ok="<<(match_code==DDS_OK)<<std::endl;
        if(match_code!=DDS_OK)return false;
        auto code=dds_.writer->wait_for_acknowledgments({5,0});std::cout<<"D2_ACK ok="<<(code==DDS_OK)<<" code=";
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        std::cout<<code();
#else
        std::cout<<static_cast<unsigned>(code);
#endif
        std::cout<<" phase="<<phase<<" key="<<key<<std::endl;return code==DDS_OK;
    }
    UA_StatusCode publish(uint32_t id,uint32_t seq,const UA_ByteString& label,const UA_ByteString& payload){
        auto it=entries_.find(id);if(it==entries_.end())return UA_STATUSCODE_BADNODEIDUNKNOWN;if(!it->second.registered)return UA_STATUSCODE_BADINVALIDSTATE;
        if(label.length>64||payload.length>256)return UA_STATUSCODE_BADOUTOFRANGE;
        if((label.length&&!label.data)||(payload.length&&!payload.data))return UA_STATUSCODE_BADINVALIDARGUMENT;
        if(label.length&&std::memchr(label.data,0,label.length))return UA_STATUSCODE_BADINVALIDARGUMENT;
        Record r;r.id(id);r.sequence_number(seq);if(label.length)r.label(std::string(reinterpret_cast<const char*>(label.data),label.length));
        if(payload.length)r.payload(std::vector<uint8_t>(payload.data,payload.data+payload.length));
        if(!dds_.write(r,it->second.handle)){failed_=true;return UA_STATUSCODE_BADINTERNALERROR;}++writes_;return UA_STATUSCODE_GOOD;
    }
    static UA_StatusCode write(UA_Server*,const UA_NodeId*,void*,const UA_NodeId*,void* context,const UA_NumericRange* range,const UA_DataValue* value) noexcept {
        auto& c=*static_cast<Context*>(context);auto& g=*c.gateway;
        try{
            if(range)return UA_STATUSCODE_BADINDEXRANGEINVALID;
            if(!value->hasValue||!UA_Variant_hasScalarType(&value->value,&g.ua_type_.type))return UA_STATUSCODE_BADTYPEMISMATCH;
            const auto& r=*static_cast<const UaRecord*>(value->value.data);if(r.id!=c.id)return UA_STATUSCODE_BADINVALIDARGUMENT;
            return g.publish(r.id,r.sequence,r.label,r.payload);
        }catch(...){g.failed_=true;return UA_STATUSCODE_BADINTERNALERROR;}
    }
    static UA_StatusCode method(UA_Server*,const UA_NodeId*,void*,const UA_NodeId* method,void* context,const UA_NodeId* object,void*,size_t count,const UA_Variant* in,size_t,UA_Variant*) noexcept {
        auto& g=*static_cast<DdsUaGateway*>(context);
        try{
            auto topic=g.numeric(130);if(!UA_NodeId_equal(object,&topic))return UA_STATUSCODE_BADNODEIDINVALID;
            auto op=method->identifier.numeric;if(count!=(op==204?4u:1u))return UA_STATUSCODE_BADINVALIDARGUMENT;
            if(!UA_Variant_hasScalarType(&in[0],&UA_TYPES[UA_TYPES_UINT32]))return UA_STATUSCODE_BADTYPEMISMATCH;
            auto id=*static_cast<const UA_UInt32*>(in[0].data);
            if(op==204){
                if(!UA_Variant_hasScalarType(&in[1],&UA_TYPES[UA_TYPES_UINT32])||!UA_Variant_hasScalarType(&in[2],&UA_TYPES[UA_TYPES_BYTESTRING])||!UA_Variant_hasScalarType(&in[3],&UA_TYPES[UA_TYPES_BYTESTRING]))return UA_STATUSCODE_BADTYPEMISMATCH;
                return g.publish(id,*static_cast<const UA_UInt32*>(in[1].data),*static_cast<const UA_ByteString*>(in[2].data),*static_cast<const UA_ByteString*>(in[3].data));
            }
            auto it=g.entries_.find(id);
            if(op==201){
                if(it!=g.entries_.end()&&it->second.registered)return UA_STATUSCODE_BADENTRYEXISTS;
                if(it==g.entries_.end()&&g.entries_.size()>=g.config_.capacity)return UA_STATUSCODE_BADRESOURCEUNAVAILABLE;
                auto& e=g.ensure(id);Record key;key.id(id);auto handle=g.dds_.writer->register_instance(&key);
                if(!handle.isDefined()){g.failed_=true;return UA_STATUSCODE_BADINTERNALERROR;}e.handle=handle;e.registered=true;return UA_STATUSCODE_GOOD;
            }
            if(it==g.entries_.end()||!it->second.registered)return UA_STATUSCODE_BADINVALIDSTATE;
            Record key;key.id(id);auto code=op==202?g.dds_.writer->dispose(&key,it->second.handle):g.dds_.writer->unregister_instance(&key,it->second.handle);
            if(code!=DDS_OK){g.failed_=true;return UA_STATUSCODE_BADINTERNALERROR;}
            if(op==203){
                it->second.registered=false;
                // The qualified SDKs require this public ACK fence before another key reuses the writer.
                if(!g.acknowledge("unregister",id)){g.failed_=true;return UA_STATUSCODE_BADTIMEOUT;}
            }
            return UA_STATUSCODE_GOOD;
        }catch(...){g.failed_=true;return UA_STATUSCODE_BADINTERNALERROR;}
    }
    void model(){
        ns_=UA_Server_addNamespace(server_,"urn:fastdds:standards:d2");require(ns_>0,"UA namespace");ua_type_.init(ns_);UA_Server_getConfig(server_)->customDataTypes=&ua_type_.array;
        auto dtype=UA_DataTypeAttributes_default;dtype.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>("BoundedRecord"));
        good(UA_Server_addDataTypeNode(server_,numeric(3001),UA_NODEID_NUMERIC(0,UA_NS0ID_STRUCTURE),UA_NODEID_NUMERIC(0,UA_NS0ID_HASSUBTYPE),browse(ns_,"BoundedRecord"),dtype,nullptr,nullptr));
        auto encoding=UA_ObjectAttributes_default;encoding.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>("Default Binary"));
        good(UA_Server_addObjectNode(server_,numeric(3002),UA_NODEID_NULL,UA_NODEID_NULL,browse(0,"Default Binary"),UA_NODEID_NUMERIC(0,UA_NS0ID_DATATYPEENCODINGTYPE),encoding,nullptr,nullptr));
        good(UA_Server_addReference(server_,numeric(3001),UA_NODEID_NUMERIC(0,UA_NS0ID_HASENCODING),UA_EXPANDEDNODEID_NUMERIC(ns_,3002),true));
        auto inst=UA_ObjectTypeAttributes_default;inst.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>("InstanceType"));
        good(UA_Server_addObjectTypeNode(server_,numeric(4001),UA_NODEID_NUMERIC(0,UA_NS0ID_BASEOBJECTTYPE),UA_NODEID_NUMERIC(0,UA_NS0ID_HASSUBTYPE),browse(ns_,"InstanceType"),inst,nullptr,nullptr));
        object(100,0,"DDS");object(110,100,"Types");object(120,100,("Domain_"+std::to_string(config_.domain)).c_str());object(130,120,config_.topic.c_str());
        good(UA_Server_addReference(server_,numeric(110),UA_NODEID_NUMERIC(0,UA_NS0ID_ORGANIZES),UA_EXPANDEDNODEID_NUMERIC(ns_,3001),true));
        UA_UInt32 domain=config_.domain;property(numeric(120),"DomainId","DomainId",&domain,&UA_TYPES[UA_TYPES_UINT32]);
        UA_String name=UA_STRING(const_cast<char*>("Standards::BoundedRecord"));property(numeric(130),"RegisteredTypeName","RegisteredTypeName",&name,&UA_TYPES[UA_TYPES_STRING]);
        UA_String fields=UA_STRING(const_cast<char*>("id:UInt32:key;sequence_number:UInt32;label:ByteString<=64;payload:ByteString<=256"));property(numeric(3001),"Fields","Fields",&fields,&UA_TYPES[UA_TYPES_STRING]);
        const char* names[]={"RegisterInstance","DisposeInstance","UnregisterInstance","WriteSample"};
        for(unsigned i=0;i<4;++i){
            UA_Argument args[4]{};const char* argnames[]={"id","sequence_number","label","payload"};
            for(unsigned j=0;j<4;++j){args[j].name=UA_STRING(const_cast<char*>(argnames[j]));args[j].dataType=UA_TYPES[j<2?UA_TYPES_UINT32:UA_TYPES_BYTESTRING].typeId;args[j].valueRank=UA_VALUERANK_SCALAR;}
            auto a=UA_MethodAttributes_default;a.displayName=UA_LOCALIZEDTEXT(const_cast<char*>("en"),const_cast<char*>(names[i]));a.executable=true;a.userExecutable=true;
            good(UA_Server_addMethodNode(server_,numeric(201+i),numeric(130),UA_NODEID_NUMERIC(0,UA_NS0ID_HASCOMPONENT),browse(ns_,names[i]),a,method,i==3?4:1,args,0,nullptr,this,nullptr));
        }
    }
    void poll(){
        for(unsigned budget=0;budget<16;++budget){
            Record record;fd::SampleInfo info;auto code=dds_.reader->take_next_sample(&record,&info);if(code==DDS_NO_DATA)return;require(code==DDS_OK,"DDS take");
            if(!info.valid_data){
                auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& item){
                    const auto& e=item.second;
                    return (e.has_value&&e.received_handle==info.instance_handle)||(e.handle.isDefined()&&e.handle==info.instance_handle);
                });
                require(found!=entries_.end(),"DDS lifecycle without observed key");record.id(found->first);++invalid_;
            }
            else require(record.label().size()<=64&&record.payload().size()<=256&&record.label().find('\0')==std::string::npos,"DDS record bounds");
            auto& e=ensure(record.id());if(info.valid_data){e.record=record;e.received_handle=info.instance_handle;e.has_value=true;++received_;}
            e.valid=info.valid_data;e.state=static_cast<UA_UInt32>(info.instance_state);
            e.stamp=(static_cast<UA_Int64>(info.source_timestamp.seconds())+11644473600LL)*10000000LL+info.source_timestamp.nanosec()/100;
            std::cout<<"D2_DDS key="<<record.id()<<" valid="<<info.valid_data<<" state="<<e.state<<std::endl;
        }
    }
public:
    explicit DdsUaGateway(Config config):config_(std::move(config)){}
    int run(){
        try{
            dds_.open(config_.domain,config_.topic,config_.rep);server_=UA_Server_new();require(server_,"UA allocation");auto* c=UA_Server_getConfig(server_);good(UA_ServerConfig_setMinimal(c,static_cast<UA_UInt16>(config_.port),nullptr));
            UA_Array_delete(c->serverUrls,c->serverUrlsSize,&UA_TYPES[UA_TYPES_STRING]);c->serverUrls=nullptr;c->serverUrlsSize=0;
            c->serverUrls=static_cast<UA_String*>(UA_Array_new(1,&UA_TYPES[UA_TYPES_STRING]));require(c->serverUrls,"UA URL allocation");c->serverUrlsSize=1;c->serverUrls[0]=UA_STRING_ALLOC(("opc.tcp://127.0.0.1:"+std::to_string(config_.port)).c_str());require(c->serverUrls[0].data,"UA URL copy");
            c->publishingIntervalLimits.min=50;c->samplingIntervalLimits.min=10;model();good(UA_Server_run_startup(server_));started_=true;
            std::cout<<"D2_READY namespace="<<ns_<<" port="<<config_.port<<std::endl;
            auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(config_.run);
            while(!std::filesystem::exists(config_.stop)&&!failed_){require(std::chrono::steady_clock::now()<deadline,"D2 run deadline");poll();UA_Server_run_iterate(server_,false);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        }catch(const std::exception& e){std::cerr<<"D2_FAIL "<<e.what()<<std::endl;failed_=true;}catch(...){failed_=true;}
        if(server_){if(started_&&UA_Server_run_shutdown(server_)!=UA_STATUSCODE_GOOD)failed_=true;UA_Server_delete(server_);server_=nullptr;}
        if(writes_&&!acknowledge("shutdown",0))failed_=true;
        if(!dds_.close())failed_=true;
        std::cout<<"D2_RESULT status="<<(failed_?"FAIL":"PASS")<<" received="<<received_<<" lifecycle="<<invalid_<<" writes="<<writes_<<" instances="<<entries_.size()<<" ua_owners="<<(server_?1:0)<<" dds_owners="<<(dds_.participant?1:0)<<std::endl;
        return failed_?3:0;
    }
};
}
int main(int argc,char** argv){try{if(argc!=2)throw std::runtime_error("usage: dds_ua_gateway CONFIG");d2::DdsUaGateway gateway(d2::load(argv[1]));return gateway.run();}catch(const std::exception& e){std::cerr<<"D2_CONFIG_FAIL "<<e.what()<<std::endl;return 2;}}
