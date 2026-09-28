#include "PublicSocketRuntimeTrace.hpp"
#include <array>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <new>
#include <atomic>
namespace mdds::dsoftbus::runtime_probe {
namespace {
std::atomic<unsigned> observation_cycle{0};
#ifdef MDDS_PUBLIC_SOCKET_BOUNDED_REGRESSION
constexpr std::size_t trace_limit=16384;
#else
constexpr std::size_t trace_limit=1024;
#endif
struct Event {const char* name=nullptr;std::uint64_t tick=0,slot=0,length=0;std::int64_t value=0;std::int32_t socket=0,role=0;char text[256]{};};
struct Store {std::mutex gate;std::array<Event,trace_limit> events{};std::size_t count=0,overflow=0,total=0,omitted=0;};
Store& Storage(){alignas(Store) static unsigned char backing[sizeof(Store)];static Store* s=::new(backing) Store;return *s;} // process-lifetime, never reused/destroyed
}
void SetObservationCycle(unsigned cycle)noexcept{observation_cycle.store(cycle);}
unsigned ObservationCycle()noexcept{return observation_cycle.load();}
bool DetailedObservation()noexcept{return ObservationCycle()<=1;}
void TraceEvent(const char* name,std::uint64_t slot,int role,std::int32_t socket,std::int64_t value,std::uint64_t length) noexcept {
    const bool detail=DetailedObservation();
    auto& s=Storage();std::lock_guard<std::mutex> lock(s.gate);++s.total;
    const bool per_packet=!std::strcmp(name,"OnBytes")||!std::strcmp(name,"SendBytes_enter")||(!std::strcmp(name,"SendBytes_return")&&value==0);
    if(!detail&&per_packet){++s.omitted;return;}
    if(s.count==s.events.size()){++s.overflow;return;}
    s.events[s.count++]={name,static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()),slot,length,value,socket,role};
}
std::size_t TraceCount()noexcept{auto& s=Storage();std::lock_guard<std::mutex> lock(s.gate);return s.count;}
void TraceText(const char* name,const char* value)noexcept{
    auto& s=Storage();std::lock_guard<std::mutex> lock(s.gate);++s.total;if(s.count==s.events.size()){++s.overflow;return;}
    auto& e=s.events[s.count++];e.name=name;e.tick=static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());std::strncpy(e.text,value,255);e.text[255]=0;
}
std::size_t TraceOverflow()noexcept{auto& s=Storage();std::lock_guard<std::mutex> lock(s.gate);return s.overflow;}
bool TraceHas(const char* name)noexcept{auto& s=Storage();std::lock_guard<std::mutex> lock(s.gate);for(std::size_t i=0;i<s.count;++i)if(!std::strcmp(s.events[i].name,name))return true;return false;}
void DumpTrace()noexcept{
    std::size_t count=0,overflow=0,total=0,omitted=0;
    {auto& s=Storage();std::lock_guard<std::mutex> lock(s.gate);count=s.count;overflow=s.overflow;total=s.total;omitted=s.omitted;}
    for(std::size_t i=0;i<count;++i){Event e;{auto& s=Storage();std::lock_guard<std::mutex> lock(s.gate);e=s.events[i];}std::printf("PROBE_API event=%zu ticks=%llu name=%s entry=%llu role=%d socket=%d value=%lld length=%llu text_hex=",i+1,(unsigned long long)e.tick,e.name,(unsigned long long)e.slot,e.role,e.socket,(long long)e.value,(unsigned long long)e.length);for(std::size_t j=0;j<sizeof(e.text)&&e.text[j];++j)std::printf("%02x",static_cast<unsigned char>(e.text[j]));std::putchar('\n');}
    std::printf("PROBE_TRACE count=%zu capacity=%zu overflow=%zu storage_bytes=%zu total=%zu omitted=%zu\n",count,trace_limit,overflow,sizeof(Store),total,omitted);std::fflush(stdout);
}
}
