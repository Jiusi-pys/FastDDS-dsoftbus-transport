#pragma once
#include "profile.hpp"
#include <open62541/types.h>
#include <open62541/types_generated.h>
#include <cstring>
namespace d1 {
inline Failure map_value(const Item& item,const UA_DataValue& data,b4_record& out) noexcept {
    if(item.label.size()>64 || item.label.find('\0')!=std::string::npos) return Failure::type;
    if(data.hasStatus && data.status != UA_STATUSCODE_GOOD) return Failure::quality;
    if(!data.hasValue || !UA_Variant_hasScalarType(&data.value,&UA_TYPES[UA_TYPES_UINT32]) || data.value.arrayDimensionsSize) return Failure::type;
    b4_record record{}; record.id=item.key; record.sequence_number=*static_cast<const UA_UInt32*>(data.value.data);
    record.label_size=item.label.size(); std::memcpy(record.label,item.label.data(),item.label.size());
    out=record; return Failure::none;
}
}
