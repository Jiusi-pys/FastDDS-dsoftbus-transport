#pragma once
#include <open62541/server.h>
#include <cstddef>
namespace d2 {
struct UaRecord{UA_UInt32 id,sequence;UA_ByteString label,payload;};
class RecordType{
    UA_DataTypeMember members_[4]{};
public:
    UA_DataType type{};
    UA_DataTypeArray array{nullptr,1,&type,false};
    void init(UA_UInt16 ns){
        members_[0]={UA_TYPENAME("id") &UA_TYPES[UA_TYPES_UINT32],0,false,false};
        members_[1]={UA_TYPENAME("sequence_number") &UA_TYPES[UA_TYPES_UINT32],offsetof(UaRecord,sequence)-sizeof(UA_UInt32),false,false};
        members_[2]={UA_TYPENAME("label") &UA_TYPES[UA_TYPES_BYTESTRING],offsetof(UaRecord,label)-offsetof(UaRecord,sequence)-sizeof(UA_UInt32),false,false};
        members_[3]={UA_TYPENAME("payload") &UA_TYPES[UA_TYPES_BYTESTRING],offsetof(UaRecord,payload)-offsetof(UaRecord,label)-sizeof(UA_ByteString),false,false};
        type={UA_TYPENAME("BoundedRecord") UA_NODEID_NUMERIC(ns,3001),UA_NODEID_NUMERIC(ns,3002),sizeof(UaRecord),UA_DATATYPEKIND_STRUCTURE,false,false,4,members_};
    }
};
}
