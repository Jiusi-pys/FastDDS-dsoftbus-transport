#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace fastdds_dsoftbus { namespace ros2_diagnostics {
constexpr std::size_t startup_capacity=256;
enum class Source:unsigned {Unknown,RealTarget,HostInjected};
enum class ValueType:unsigned {NotObserved,SocketHandle,SdkReturn,VoidReturn,LocalStatus,CallbackStatus,RedactedText};
struct Event {
    std::uint64_t seq=0,entry=UINT64_MAX,length=0;
    std::int64_t value=0;
    std::int32_t socket=-1,role=-1;
    ValueType type=ValueType::NotObserved;
    bool entered=false,returned=false,cleanup=false,truncated=false,redacted=false;
    char operation[48]{};
};
struct Snapshot {
    unsigned schema=1,side=0;Source source=Source::Unknown;
    bool initialized=false,first_present=false;
    char run[17]{};
    std::uint64_t last_seq=0,overflow=0,omitted=0,text_truncated=0,output_errors=0;
    std::size_t count=0;Event events[startup_capacity]{};Event first{};
    std::uint64_t entered[5]{},returned[5]{};
};
static_assert(std::is_trivially_copyable<Snapshot>::value,"Diagnostic ABI must copy values only");
using Writer=bool(*)(const char*,std::size_t,void*);
inline bool Stdout(const char* text,std::size_t length,void*) {return std::fwrite(text,1,length,stdout)==length;}
inline const char* Type(ValueType t) {
    switch(t){case ValueType::SocketHandle:return "SOCKET_HANDLE";case ValueType::SdkReturn:return "SDK_RC";
    case ValueType::VoidReturn:return "VOID_RETURN_NO_RC";case ValueType::LocalStatus:return "LOCAL_STATUS";
    case ValueType::CallbackStatus:return "CALLBACK_STATUS";case ValueType::RedactedText:return "REDACTED_TEXT";default:return "NOT_OBSERVED";}
}
// Formatting/output is a caller-owned control operation, after every capture lock.
inline bool Write(const Snapshot& s,Writer writer,void* context,std::uint64_t failure_ref=0) noexcept {
    if(!writer)return false;
    try {
        char line[768];bool ok=true;
        const char* source=s.source==Source::RealTarget?"REAL_TARGET":s.source==Source::HostInjected?"HOST_FAKE_INJECTION":"UNKNOWN";
        auto event=[&](const Event& e,bool reserved){
            char operation_index[24];
            if(!std::strcmp(e.operation,"BackendReject_BackendStatus_result")||!std::strcmp(e.operation,"SdkException_BackendStatus_result"))std::snprintf(operation_index,sizeof(operation_index),"%llu",static_cast<unsigned long long>(e.length));else std::strcpy(operation_index,"NOT_APPLICABLE");
            char value[40];if(e.type==ValueType::NotObserved)std::strcpy(value,"NOT_OBSERVED");else if(e.type==ValueType::VoidReturn||e.type==ValueType::RedactedText)std::strcpy(value,"NOT_APPLICABLE");else std::snprintf(value,sizeof(value),"%lld",static_cast<long long>(e.value));
            const auto n=std::snprintf(line,sizeof(line),"ROS_STARTUP_EVENT schema=1 run=%s side=%c observation_seq=%llu operation=%s operation_index=%s detail_value=%llu value_type=%s value=%s sdk_entered=%d sdk_return_observed=%d local_result_observed=%d callback_observed=%d socket=%d entry=%llu role=%d source=%s cleanup=%d truncated=%d redacted=%d reserved_first=%d failure_ref=%llu\n",s.initialized?s.run:"UNKNOWN",s.side?'B':'A',static_cast<unsigned long long>(e.seq),e.operation,operation_index,static_cast<unsigned long long>(e.length),Type(e.type),value,int(e.entered),int(e.returned&&(e.type==ValueType::SocketHandle||e.type==ValueType::SdkReturn||e.type==ValueType::VoidReturn)),int(e.returned&&e.type==ValueType::LocalStatus),int(e.type==ValueType::CallbackStatus),e.socket,static_cast<unsigned long long>(e.entry),e.role,source,int(e.cleanup),int(e.truncated),int(e.redacted),int(reserved),static_cast<unsigned long long>(failure_ref));
            if(n<=0||static_cast<std::size_t>(n)>=sizeof(line)||!writer(line,static_cast<std::size_t>(n),context))ok=false;
        };
        // Export the protected first error before optional detail, including if
        // a later output write fails or the consumer temporarily stops draining.
        if(s.first_present)event(s.first,true);
        for(std::size_t i=0;i<s.count&&i<startup_capacity;++i)if(!s.first_present||s.events[i].seq!=s.first.seq)event(s.events[i],false);
        const char* operations[]={"Socket","Listen","Bind","SendBytes","Shutdown"};
        for(unsigned i=0;i<5;++i){
            const auto n=std::snprintf(line,sizeof(line),"ROS_STARTUP_OPERATION schema=1 run=%s side=%c operation=%s entered=%llu returned=%llu state=%s return_semantics=%s source=%s\n",s.initialized?s.run:"UNKNOWN",s.side?'B':'A',operations[i],static_cast<unsigned long long>(s.entered[i]),static_cast<unsigned long long>(s.returned[i]),!s.entered[i]?"NOT_CALLED":s.entered[i]!=s.returned[i]?"RETURN_NOT_OBSERVED":"RETURN_OBSERVED",i==4?"VOID_NO_RC":i==0?"HANDLE_OR_ERROR":"SDK_RC",source);
            if(n<=0||static_cast<std::size_t>(n)>=sizeof(line)||!writer(line,static_cast<std::size_t>(n),context))ok=false;
        }
        const auto n=std::snprintf(line,sizeof(line),"ROS_STARTUP_CAPTURE schema=1 run=%s side=%c source=%s stored=%zu limit=%zu overflow=%llu omitted=%llu text_truncated=%llu output_errors=%llu first_observation_seq=%llu failure_ref=%llu completeness=%s sdk_epoch=NOT_PROVEN\n",s.initialized?s.run:"UNKNOWN",s.side?'B':'A',source,s.count,startup_capacity,static_cast<unsigned long long>(s.overflow),static_cast<unsigned long long>(s.omitted),static_cast<unsigned long long>(s.text_truncated),static_cast<unsigned long long>(s.output_errors),static_cast<unsigned long long>(s.first_present?s.first.seq:0),static_cast<unsigned long long>(failure_ref),s.initialized&&!s.overflow&&!s.output_errors?"CAPTURED":"INCOMPLETE");
        return n>0&&static_cast<std::size_t>(n)<sizeof(line)&&writer(line,static_cast<std::size_t>(n),context)&&ok;
    }catch(...){return false;}
}
}
}
