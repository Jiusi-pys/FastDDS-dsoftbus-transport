#include "NativeObservation.hpp"
#include "../../../probes/PublicSocketRtpsObservation.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {
constexpr std::size_t capacity = 8192;
struct Frame { std::uint64_t observation; bool sending; std::int32_t socket;
    std::array<std::uint8_t,16> writer; std::int32_t high; std::uint32_t low; };
std::array<Frame,capacity> frames;
std::size_t count=0;
std::mutex mutex;
std::string run;
unsigned side=0;
std::atomic<std::uint64_t> observations{0},send_enter{0},send_return{0},send_error{0},ingress{0},overflow{0},malformed{0};
std::mutex startup_mutex;
fastdds_dsoftbus::ros2_diagnostics::Snapshot startup;
std::atomic<std::uint64_t> capture_faults{0},exported_failure{0},output_errors{0};
bool cleanup_started=false;
bool suffix(const char* text,const char* ending) {
    const auto n=std::strlen(text),m=std::strlen(ending);return n>=m&&!std::strcmp(text+n-m,ending);
}
void capture(const char* name,std::uint64_t entry,int role,std::int32_t socket,std::int64_t value,std::uint64_t length) noexcept {
    using namespace fastdds_dsoftbus::ros2_diagnostics;
    try {
        std::lock_guard<std::mutex> lock(startup_mutex);
        if(!name){++startup.omitted;return;}
        Event e;e.seq=++startup.last_seq;e.entry=entry;e.role=role;e.socket=socket;e.length=length;e.value=value;
        std::size_t n=0;while(n<sizeof(e.operation)-1&&name[n]){const char ch=name[n];e.operation[n]=(ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'?ch:'_';++n;}
        e.operation[n]='\0';e.truncated=name[n]!='\0';
        const bool sdk=!std::strncmp(name,"Socket_",7)||!std::strncmp(name,"Listen_",7)||!std::strncmp(name,"Bind_",5)||!std::strncmp(name,"SendBytes_",10)||!std::strncmp(name,"Shutdown_",9);
        int operation=-1;
        if(!std::strncmp(name,"Socket_",7))operation=0;else if(!std::strncmp(name,"Listen_",7))operation=1;
        else if(!std::strncmp(name,"Bind_",5))operation=2;else if(!std::strncmp(name,"SendBytes_",10))operation=3;else if(!std::strncmp(name,"Shutdown_",9))operation=4;
        bool failed=false;
        if(sdk&&suffix(name,"_enter")){e.entered=true;if(operation>=0)++startup.entered[operation];}
        else if(sdk&&suffix(name,"_return")){
            e.entered=true;e.returned=true;if(operation>=0)++startup.returned[operation];
            if(!std::strcmp(name,"Shutdown_void_return"))e.type=ValueType::VoidReturn;
            else if(operation==0&&value>=0)e.type=ValueType::SocketHandle;
            else {e.type=ValueType::SdkReturn;failed=operation==0?value<0:value!=0;if(operation==0)e.socket=-1;}
        }else if(!std::strncmp(name,"On",2)){e.type=ValueType::CallbackStatus;e.returned=true;failed=!std::strcmp(name,"OnError");}
        else {e.type=ValueType::LocalStatus;e.returned=true;failed=suffix(name,"_result")&&value!=0;}
        if(!std::strcmp(name,"LocalCleanup_enter"))cleanup_started=true;
        e.cleanup=cleanup_started;
        // A diagnosis is not automatically the application's primary failure.
        if(failed&&!e.cleanup&&!startup.first_present){startup.first=e;startup.first_present=true;}
        const bool ordinary_data=!std::strcmp(name,"OnBytes")||!std::strncmp(name,"SendBytes_",10);
        if(ordinary_data&&!failed){++startup.omitted;return;}
        if(startup.count<startup_capacity)startup.events[startup.count++]=e;else ++startup.overflow;
    }catch(...){++capture_faults;}
}
}
namespace fastdds_dsoftbus::dsoftbus::runtime_probe {
void TraceEvent(const char* event,std::uint64_t entry,int role,std::int32_t socket,std::int64_t value,std::uint64_t length) noexcept {
    if(!event){++capture_faults;return;}
    if(!std::strcmp(event,"SendBytes_enter"))++send_enter;
    else if(!std::strcmp(event,"SendBytes_return")){++send_return;if(value!=0)++send_error;}
    capture(event,entry,role,socket,value,length);
}
void TraceText(const char* key,const char* borrowed) noexcept {
    // The interface supplies a valid borrowed C string. Do not retain pointers,
    // peer identities or arbitrary text; bounded scanning records only truncation.
    try {
        std::lock_guard<std::mutex> lock(startup_mutex);ros2_diagnostics::Event e;
        e.seq=++startup.last_seq;e.type=ros2_diagnostics::ValueType::RedactedText;e.redacted=true;e.returned=true;e.cleanup=cleanup_started;
        std::strcpy(e.operation,"TraceText_redacted");
        std::size_t n=0;if(borrowed){while(n<64&&borrowed[n])++n;e.truncated=n==64&&borrowed[n]!='\0';}else e.truncated=false;
        e.length=n;if(e.truncated)++startup.text_truncated;
        if(!key||!borrowed){++startup.omitted;std::strcpy(e.operation,"TraceText_unknown");e.type=ros2_diagnostics::ValueType::NotObserved;e.returned=false;}
        if(startup.count<ros2_diagnostics::startup_capacity)startup.events[startup.count++]=e;else ++startup.overflow;
    }catch(...){++capture_faults;}
}
}
namespace fastdds_dsoftbus::dsoftbus::dds_experiment {
std::uint64_t SocketFrame(const char* event,std::int32_t socket,const std::uint8_t* data,std::size_t size) noexcept {
    const auto observation=++observations;
    const bool sending=!std::strcmp(event,"SDK_SEND");
    if(!sending)++ingress;
    const auto decoded=fastdds_dsoftbus::experiment::ObserveRtps(data,size);
    if(!decoded.valid){++malformed;return observation;}
    // Bounded structural metadata only. Never store payload or alter/forward it.
    std::lock_guard<std::mutex> lock(mutex);
    for(const auto& item:decoded.data) {
        if(count==capacity){++overflow;continue;}
        frames[count++]={observation,sending,socket,item.writer_guid,item.sequence_hi,item.sequence_lo};
    }
    return observation;
}
}
namespace fastdds_dsoftbus::ros2_target_observation {
void Begin(const std::string& identifier,unsigned board) {
    std::lock_guard<std::mutex> lock(mutex);run=identifier;side=board;
    std::lock_guard<std::mutex> startup_lock(startup_mutex);
    startup={};startup.side=board;startup.source=ros2_diagnostics::Source::RealTarget;
    startup.initialized=identifier.size()==16&&board<2&&identifier.find_first_not_of("0123456789abcdef")==std::string::npos;
    if(startup.initialized)std::memcpy(startup.run,identifier.data(),16);
    cleanup_started=false;capture_faults=0;exported_failure=0;output_errors=0;
}
#ifdef FASTDDS_DSOFTBUS_OBSERVATION_HOST_TEST
void SetSourceForHostInjection() noexcept {try{std::lock_guard<std::mutex> lock(startup_mutex);startup.source=ros2_diagnostics::Source::HostInjected;}catch(...){++capture_faults;}}
#endif
bool Capture(ros2_diagnostics::Snapshot& output) noexcept {
    try{std::unique_lock<std::mutex> lock(startup_mutex,std::try_to_lock);if(!lock.owns_lock())return false;output=startup;output.overflow+=capture_faults.load();output.output_errors=output_errors.load();return output.initialized;}catch(...){++capture_faults;return false;}
}
bool ExportPending(ros2_diagnostics::Writer writer,void* context) noexcept {
    ros2_diagnostics::Snapshot snapshot;
    if(!Capture(snapshot)||!snapshot.first_present)return true;
    auto previous=exported_failure.load();if(previous>=snapshot.first.seq)return output_errors.load()==0;
    if(!exported_failure.compare_exchange_strong(previous,snapshot.first.seq))return true;
    if(!ros2_diagnostics::Write(snapshot,writer,context)||(writer==ros2_diagnostics::Stdout&&std::fflush(stdout))){++output_errors;return false;}
    return true;
}
bool Flush() {
    // Snapshot under the leaf observation mutex, release before any I/O. A late
    // SDK callback is not declared physically drained by this local snapshot.
    std::array<Frame,capacity> snapshot;
    std::size_t stored;
    {std::lock_guard<std::mutex> lock(mutex);stored=count;std::copy_n(frames.begin(),stored,snapshot.begin());}
    for(std::size_t i=0;i<stored;++i) {
        char guid[33]{};for(std::size_t j=0;j<16;++j)std::snprintf(guid+2*j,3,"%02x",snapshot[i].writer[j]);
        std::fprintf(stdout,"ROS_NATIVE_DATA run=%s side=%c observation=%llu direction=%s socket=%d writer=%s sequence_hi=%d sequence_lo=%u scope=STRUCTURAL_APP_OBSERVER\n",
            run.c_str(),side?'B':'A',static_cast<unsigned long long>(snapshot[i].observation),snapshot[i].sending?"SDK_SEND":"SDK_INGRESS",snapshot[i].socket,guid,snapshot[i].high,snapshot[i].low);
    }
    std::fprintf(stdout,"ROS_NATIVE_SUMMARY run=%s side=%c send_enter=%llu send_return=%llu send_error=%llu ingress=%llu detailed_stored=%zu metadata_limit=%zu overflow=%llu malformed=%llu sdk_epoch=NOT_PROVEN\n",
        run.c_str(),side?'B':'A',static_cast<unsigned long long>(send_enter.load()),static_cast<unsigned long long>(send_return.load()),static_cast<unsigned long long>(send_error.load()),static_cast<unsigned long long>(ingress.load()),stored,capacity,static_cast<unsigned long long>(overflow.load()),static_cast<unsigned long long>(malformed.load()));
    std::fflush(stdout);
    ros2_diagnostics::Snapshot startup_copy;
    const bool startup_ok=Capture(startup_copy)&&ros2_diagnostics::Write(startup_copy,ros2_diagnostics::Stdout,nullptr);
    return startup_ok&&!startup_copy.overflow&&!startup_copy.output_errors&&overflow.load()==0&&malformed.load()==0&&send_error.load()==0&&send_enter.load()==send_return.load();
}
}
