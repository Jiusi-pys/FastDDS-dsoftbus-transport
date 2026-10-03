// Linux component test of the actual NativeObservation translation unit.
// This is not a ROS/SDK process. No ended observation object is accessed on RED.
#include "../../integrations/ros2/target/NativeObservation.hpp"
#include "../../probes/PublicSocketRuntimeTrace.hpp"
#include "../../probes/PublicSocketDdsTrace.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

namespace obs = fastdds_dsoftbus::ros2_target_observation;
namespace diag = fastdds_dsoftbus::ros2_diagnostics;
extern "C" int __real___cxa_atexit(void (*)(void*), void*, void*);
extern "C" char __ehdr_start;
extern "C" void* __dso_handle;
namespace {
struct Registration { void (*callback)(void*); void* object; };
Registration registrations[32]{};
unsigned registered = 0, destroyed = 0;
bool main_started = false, registration_overflow = false;
std::atomic<unsigned> phase{0}, polls{0}, post_polls{0};
std::atomic<bool> paused{false}, worker_ok{true};
std::thread* worker = nullptr;
std::atomic<unsigned> allocations{0};

void observed_destructor(void* raw) {
    const auto* entry = static_cast<const Registration*>(raw);
    entry->callback(entry->object);
    ++destroyed;
}
bool wait_for(bool (*predicate)()) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate() && std::chrono::steady_clock::now() < end)
        std::this_thread::yield();
    return predicate();
}
void pause_before_finalization() {
    phase = 1;
    if (!wait_for([] { return paused.load(); })) worker_ok = false;
}
void final_check(void*) {
    if (!worker) return;
    // All executable-owned ordinary static destructors registered after the
    // early check have now run. Never probe known-ended objects in the RED.
    const bool state_survived = registered == 0 && destroyed == 0 && !registration_overflow;
    if (state_survived && worker_ok.load()) {
        phase = 2;
        if (!wait_for([] { return post_polls.load() >= 16; })) worker_ok = false;
    }
    phase = 3;
    worker->join();
    delete worker;
    worker = nullptr;
    const bool pass = state_survived && worker_ok.load() && post_polls.load() >= 16;
    std::printf("OBSERVER_EXIT_LIFETIME result=%s executable_destructors=%u destroyed=%u before=%u after=%u worker_joined=1 scope=HOST_OBSERVER_COMPONENT\n",
            pass ? "PASS" : "FAIL", registered, destroyed, polls.load(), post_polls.load());
    std::fflush(stdout);
}
__attribute__((constructor(101))) void register_last_check() {
    // Bypass our instrumentation for the test's own early final callback.
    __real___cxa_atexit(final_check, nullptr, __dso_handle);
}
}
void* operator new(std::size_t size) {
    ++allocations;
    if(auto* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete(void* value,std::size_t) noexcept {std::free(value);}
extern "C" int __wrap___cxa_atexit(void (*callback)(void*), void* object, void* dso) {
    Dl_info origin{};
    if (!main_started && dladdr(__builtin_return_address(0), &origin) &&
            origin.dli_fbase == static_cast<void*>(&__ehdr_start)) {
        if (registered == 32) {
            registration_overflow = true;
        } else {
            auto* entry = &registrations[registered++];
            entry->callback = callback;
            entry->object = object;
            return __real___cxa_atexit(observed_destructor, entry, dso);
        }
    }
    return __real___cxa_atexit(callback, object, dso);
}
int main(int argc,char** argv) {
    main_started = true;
    const char* mode=argc==2?argv[1]:"lifetime";
    if(std::strcmp(mode,"lifetime")&&std::strcmp(mode,"prebegin")&&
            std::strcmp(mode,"invalid")&&std::strcmp(mode,"duplicate")&&std::strcmp(mode,"concurrent")&&std::strcmp(mode,"prefix"))return 2;
    if(!std::strcmp(mode,"prebegin")) {
        diag::Snapshot empty;
        diag::NativePrefix prefix;
        const auto before=allocations.load();
        fastdds_dsoftbus::dsoftbus::runtime_probe::TraceEvent("Socket_enter",1,0,-1,0,0);
        if(obs::Capture(empty)||obs::CapturePrefix(prefix)||obs::ExportPending()||allocations.load()!=before)return 3;
    }
    if(!std::strcmp(mode,"invalid")) {
        bool rejected=false;try{obs::Begin("not-a-run",0);}catch(const std::invalid_argument&){rejected=true;}
        diag::Snapshot empty;if(!rejected||obs::Capture(empty))return 4;
    }
    if(!std::strcmp(mode,"concurrent")) {
        std::atomic<unsigned> ready{0},accepted{0},rejected{0};std::atomic<bool> release{false};
        std::vector<std::thread> attempts;
        for(unsigned i=0;i<8;++i)attempts.emplace_back([&]{++ready;while(!release.load())std::this_thread::yield();
            try{obs::Begin("1234567890abcdef",0);++accepted;}catch(const std::logic_error&){++rejected;}});
        while(ready.load()!=8)std::this_thread::yield();release=true;
        for(auto& thread:attempts)thread.join();
        if(accepted!=1||rejected!=7)return 5;
    }else obs::Begin("1234567890abcdef", 0);
    obs::SetSourceForHostInjection();
    diag::NativePrefix prefix;
    const auto prefix_allocations=allocations.load();
    if(!obs::CapturePrefix(prefix)||!prefix.valid||prefix.stored!=0||prefix.total_stored!=0||prefix.side!=0||
            std::strcmp(prefix.run,"1234567890abcdef")||allocations.load()!=prefix_allocations)return 8;
    if(!std::strcmp(mode,"prefix")) {
        std::uint8_t packet[44]{};std::memcpy(packet,"RTPS",4);packet[4]=2;
        packet[20]=0x15;packet[21]=1;packet[22]=20;packet[35]=2;
        for(unsigned i=1;i<=200;++i){packet[40]=static_cast<std::uint8_t>(i);
            fastdds_dsoftbus::dsoftbus::dds_experiment::SocketFrame(i%2?"SDK_SEND":"SDK_INGRESS",7,packet,sizeof(packet));}
        const auto before_copy=allocations.load();
        if(!obs::CapturePrefix(prefix)||prefix.stored!=128||prefix.total_stored!=200||prefix.first_index!=72||
                prefix.frames[0].low!=73||prefix.frames[127].low!=200||prefix.frames[0].socket!=7||
                !prefix.frames[0].sending||prefix.frames[127].sending||allocations.load()!=before_copy)return 9;
        std::printf("OBSERVER_PREFIX_BOUND result=PASS total=200 stored=128 first=72 scope=HOST_INJECTED_STRUCTURAL_BYTES_NOT_SDK\n");
    }
    fastdds_dsoftbus::dsoftbus::runtime_probe::TraceEvent("Socket_enter", 1, 0, -1, 0, 0);
    if(!std::strcmp(mode,"duplicate")) {
        fastdds_dsoftbus::dsoftbus::runtime_probe::TraceEvent("Socket_return",1,0,-1,-777,0);
        bool rejected=false;try{obs::Begin("fedcba0987654321",1);}catch(const std::logic_error&){rejected=true;}
        diag::Snapshot preserved;
        if(!rejected||!obs::Capture(preserved)||!preserved.first_present||preserved.first.value!=-777||
                std::strcmp(preserved.run,"1234567890abcdef"))return 6;
    }
    if(!std::strcmp(mode,"prebegin")) {diag::Snapshot s;if(!obs::Capture(s)||s.overflow!=1)return 7;}
    worker = new std::thread([] {
        for (;;) {
            const unsigned current = phase.load();
            if (current == 3) return;
            if (current == 1) { paused = true; std::this_thread::yield(); continue; }
            diag::Snapshot snapshot;
            diag::NativePrefix retained_prefix;
            if (!obs::Capture(snapshot) || !snapshot.initialized || snapshot.side != 0 ||
                    std::strcmp(snapshot.run, "1234567890abcdef") || !obs::ExportPending()||
                    !obs::CapturePrefix(retained_prefix)||!retained_prefix.valid||
                    std::strcmp(retained_prefix.run,"1234567890abcdef"))
                worker_ok = false;
            if (current == 2) ++post_polls; else ++polls;
        }
    });
    if (!wait_for([] { return polls.load() >= 16; })) worker_ok = false;
    if (std::atexit(pause_before_finalization) != 0) return 2;
    // The parent harness judges the final marker as well as process exit. RED
    // returns naturally after joining; it never forces exit to manufacture PASS.
    return 0;
}
