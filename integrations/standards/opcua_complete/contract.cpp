#include "services.hpp"
#include "history.hpp"
int main(){try{
    using namespace d4;check(service(1)&&service(12)&&!service(0)&&!service(13),"D4 service table");
    UA_ReadRequest req;UA_ReadRequest_init(&req);req.maxAge=25;auto bytes=encode(&req,&UA_TYPES[UA_TYPES_READREQUEST]);UaValue decoded(&UA_TYPES[UA_TYPES_READREQUEST]);good(decode(bytes,decoded.data,&UA_TYPES[UA_TYPES_READREQUEST]));
    bytes.push_back(0);check(decode(bytes,decoded.data,&UA_TYPES[UA_TYPES_READREQUEST])!=UA_STATUSCODE_GOOD,"D4 trailing bytes");
    Retention history(2,100);auto now=Clock::time_point{};
    for(unsigned n=1;n<=3;++n){Record record;record.id(7);record.sequence_number(n);history.insert({record,UA_DateTime(n),now});}
    auto values=history.select(7,0,0,now);check(values.size()==2&&values[0].record.sequence_number()==2&&values[1].record.sequence_number()==3,"D4 retention count");
    auto reverse=history.select(7,3,2,now);check(reverse.size()==2&&reverse.front().stamp==3,"D4 reverse range");
    check(history.select(7,0,0,now+std::chrono::milliseconds(100)).empty(),"D4 exact expiry boundary");
    check(history.entries.at(7).latest.record.sequence_number()==3,"D4 current survives historical expiry");
    Retention duplicate(4,100);Record first;first.id(9);first.sequence_number(1);duplicate.insert({first,42,now,true,500});
    first.sequence_number(2);duplicate.insert({first,42,now,true,600});auto unique=duplicate.select(9,0,0,now);
    check(unique.size()==1&&unique[0].record.sequence_number()==2&&unique[0].extra_data,"D4 newest raw timestamp/ExtraData");
    d2::RecordType type;type.init(2);UA_DataValue value;UA_DataValue_init(&value);good(data_value(unique[0],Field::Payload,type,value,UA_TIMESTAMPSTORETURN_BOTH));
    auto* payload=static_cast<UA_ByteString*>(value.value.data);check(payload&&payload->length==0&&payload->data==UA_EMPTY_ARRAY_SENTINEL,"D4 empty ByteString representation");
    check(value.serverTimestamp==600&&value.sourceTimestamp==42&&(value.status&0x408)==0x408,"D4 preserved receipt time and historian flag");UA_DataValue_clear(&value);
    std::cout<<"D4_CONTRACT PASS"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<"D4_CONTRACT FAIL "<<e.what()<<std::endl;return 1;}}
