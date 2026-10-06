#pragma once
// Value-only diagnostic cache in the selected Fast DDS module. No owner/SDK calls.
#include "StartupDiagnostics.hpp"
#include "ResidualDiagnostics.hpp"
#include <mutex>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif
namespace fastdds_dsoftbus { namespace ros2_teardown {
constexpr std::size_t capacity=256;
struct Event {
    std::uint64_t seq=0,call=0,actor=0,object=0;
    std::int64_t value=0;
    bool returned=false;
    char operation[48]{},branch[48]{},type[32]{};
};
struct Snapshot {
    char run[17]{}; unsigned side=0; long pid=0;
    bool initialized=false,first_present=false;
    std::uint64_t seq=0,overflow=0,output_errors=0;
    std::size_t count=0;
    Event events[capacity]{},first{};
};
static_assert(std::is_trivially_copyable<Snapshot>::value,"Teardown values must not own module resources");
struct Store {std::mutex lock;Snapshot snapshot,export_buffer;std::thread::id control;std::uint64_t exported=0;bool first_exported=false,exporting=false;};
// One bounded process-lifetime metadata cache, just like the factory's gate and
// lease table. It owns no Participant, Runtime, payload or native resources.
// A retained failure pump must never encounter a statically destroyed mutex.
inline Store& store(){static Store* value=new Store;return *value;}
inline long pid(){
#if defined(_WIN32)
    return _getpid();
#else
    return getpid();
#endif
}
inline void Begin(const std::string& run,unsigned side) noexcept {
    if(run.size()!=16||side>1)return;
    for(char c:run)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return;
    try {auto& s=store();std::lock_guard<std::mutex> l(s.lock);
        if(s.snapshot.initialized)return;
        std::memcpy(s.snapshot.run,run.c_str(),17);s.snapshot.side=side;s.snapshot.pid=pid();s.snapshot.initialized=true;
        if(!InitializeResiduals())++s.snapshot.output_errors;
    }catch(...){}
}
inline std::uint64_t Record(const char* op,const void* actor,const void* object,const char* type,
        bool returned,std::int64_t value,std::uint64_t call,const char* branch) noexcept {
    try {auto& s=store();std::lock_guard<std::mutex> l(s.lock);auto& v=s.snapshot;if(!v.initialized)return 0;
        if(!op||!type||!branch)return 0;
        Event e;e.seq=++v.seq;e.call=call?call:e.seq;e.actor=reinterpret_cast<std::uintptr_t>(actor);
        e.object=reinterpret_cast<std::uintptr_t>(object);e.returned=returned;e.value=value;
        std::snprintf(e.operation,sizeof(e.operation),"%s",op);std::snprintf(e.type,sizeof(e.type),"%s",type);
        std::snprintf(e.branch,sizeof(e.branch),"%s",branch);
        if(std::strlen(op)>=sizeof(e.operation)||std::strlen(type)>=sizeof(e.type)||std::strlen(branch)>=sizeof(e.branch))++v.overflow;
        if(returned&&value!=0&&!v.first_present){v.first_present=true;v.first=e;}
        if(v.count<capacity)v.events[v.count++]=e;else ++v.overflow;
        return e.call;
    }catch(...){return 0;}
}
inline bool Capture(Snapshot* out) noexcept {
    if(!out)return false;
    try {auto& s=store();std::lock_guard<std::mutex> l(s.lock);*out=s.snapshot;return out->initialized;}catch(...){return false;}
}
// Only a caller explicitly declaring a legal control checkpoint can output.
// Record is callable under DDS locks, but never formats output or enters SDK.
inline void ControlThread() noexcept {try{auto& s=store();std::lock_guard<std::mutex> l(s.lock);s.control=std::this_thread::get_id();}catch(...){}}
inline bool IsControlThread() noexcept {try{auto& s=store();std::lock_guard<std::mutex> l(s.lock);return s.control==std::this_thread::get_id();}catch(...){return false;}}
inline bool Write(const Snapshot& v,const char* checkpoint,ros2_diagnostics::Writer writer,void* ctx,
        std::uint64_t after=0,bool first=true) noexcept {
    if(!writer||!v.initialized)return false;
    bool ok=true;char text[640];
    auto event=[&](const Event& e,bool reserved){
        char value[32];if(e.returned)std::snprintf(value,sizeof(value),"%lld",static_cast<long long>(e.value));else std::strcpy(value,"NOT_OBSERVED");
        const int n=std::snprintf(text,sizeof(text),"ROS_TEARDOWN_EVENT schema=1 run=%s side=%c pid=%ld seq=%llu call=%llu operation=%s actor=%llu object=%llu entered=1 returned=%d value_type=%s value=%s branch=%s reserved_first=%d failure_ref=%llu\n",
            v.run,v.side?'B':'A',v.pid,static_cast<unsigned long long>(e.seq),static_cast<unsigned long long>(e.call),e.operation,
            static_cast<unsigned long long>(e.actor),static_cast<unsigned long long>(e.object),int(e.returned),e.type,value,e.branch,int(reserved),static_cast<unsigned long long>(v.first_present?v.first.seq:0));
        if(n<=0||static_cast<std::size_t>(n)>=sizeof(text)||!writer(text,static_cast<std::size_t>(n),ctx))ok=false;
    };
    try {
        if(first&&v.first_present)event(v.first,true);
        for(std::size_t i=0;i<v.count&&i<capacity;++i)if(v.events[i].seq>after&&(!first||!v.first_present||v.events[i].seq!=v.first.seq))event(v.events[i],false);
        const int n=std::snprintf(text,sizeof(text),"ROS_TEARDOWN_CAPTURE schema=1 run=%s side=%c pid=%ld checkpoint=%s stored=%zu limit=%zu overflow=%llu output_errors=%llu first_seq=%llu completeness=%s object_identity=PROCESS_LOCAL_TOKEN sdk_quiescence=NOT_PROVEN\n",
            v.run,v.side?'B':'A',v.pid,checkpoint,v.count,capacity,static_cast<unsigned long long>(v.overflow),static_cast<unsigned long long>(v.output_errors),static_cast<unsigned long long>(v.first_present?v.first.seq:0),!v.overflow&&!v.output_errors?"CAPTURED":"INCOMPLETE");
        return n>0&&static_cast<std::size_t>(n)<sizeof(text)&&writer(text,static_cast<std::size_t>(n),ctx)&&ok;
    }catch(...){return false;}
}
using Flusher=int(*)(std::FILE*);
inline void FlushControl(const char* checkpoint,bool known_pump_control=false,Flusher flusher=std::fflush) noexcept {
    if(!known_pump_control&&!IsControlThread())return;
    std::uint64_t after=0;bool first=false;
    try {auto& s=store();{std::lock_guard<std::mutex> l(s.lock);if(s.exporting)return;after=s.exported;first=!s.first_exported&&s.snapshot.first_present;
        if(!s.snapshot.initialized||(known_pump_control&&!first))return;s.exporting=true;s.export_buffer=s.snapshot;}
        // Fixed module-owned export buffer avoids a large stack object on the
        // pump thread. Its exporting flag prevents concurrent overwrite.
        const bool events_written=Write(s.export_buffer,checkpoint,ros2_diagnostics::Stdout,nullptr,after,first);
        const bool residuals_written=WriteResiduals(s.export_buffer.run,s.export_buffer.side,s.export_buffer.pid,ros2_diagnostics::Stdout,nullptr);
        const bool written=events_written&&residuals_written;
        const bool flushed=flusher&&flusher(stdout)==0;
        const bool ok=written&&flushed;
        {std::lock_guard<std::mutex> l(s.lock);if(ok){s.exported=s.export_buffer.seq;if(first)s.first_exported=true;}else ++s.snapshot.output_errors;s.exporting=false;}
        if(!ok)std::fputs("ROS_TEARDOWN_CAPTURE_FAILURE reason=OUTPUT_FAILURE\n",stderr);
    }catch(...){try{auto& s=store();std::lock_guard<std::mutex> l(s.lock);s.exporting=false;++s.snapshot.output_errors;}catch(...){}std::fputs("ROS_TEARDOWN_CAPTURE_FAILURE reason=CAPTURE_EXCEPTION\n",stderr);}
}
struct Call {
    const char* operation;const void* actor;const void* object;std::uint64_t seq;bool outermost;
    Call(const char* op,const void* a,const void* o,bool outer=false) noexcept:operation(op),actor(a),object(o),seq(Record(op,a,o,"DDS_RETURN_CODE",false,0,0,"CALL_ENTERED")),outermost(outer){}
    template<class R> R Return(R r,const char* branch="UNKNOWN_INTERNAL_BRANCH",const char* type="DDS_RETURN_CODE") noexcept {Record(operation,actor,object,type,true,r(),seq,branch);return r;}
    // Construct this guard before any existing DDS lock. Locks are released
    // before its destructor. Object tokens are values; never dereference them.
    ~Call() noexcept {if(outermost)FlushControl("DELETE_RETURN");}
};
}}
