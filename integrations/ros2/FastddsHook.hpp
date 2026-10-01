#pragma once
// Injected only into the declared isolated Fast DDS 2.14.5 build. No ROS headers.
#include "PlatformModule.hpp"
#include "StartupDiagnostics.hpp"
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <atomic>

namespace fastdds_dsoftbus_hook {
using Create = int (*)(const char*, unsigned, void*, void**);
using Close = int (*)(void*);
using Version = unsigned (*)();
using PublicDelete = void (*)(void*);
using Capture = int (*)(fastdds_dsoftbus::ros2_diagnostics::Snapshot*);
struct Lease { fastdds_dsoftbus_hook_platform::Module module=nullptr; void* owner=nullptr; Close close=nullptr; PublicDelete public_delete=nullptr; Capture capture=nullptr; std::mutex module_copy; std::atomic<unsigned> close_state{0}; int terminal_result=1; };
inline std::mutex& gate() { static auto* g=new std::mutex; return *g; }
inline std::unordered_map<void*,std::shared_ptr<Lease>>& leases() { static auto* m=new std::unordered_map<void*,std::shared_ptr<Lease>>; return *m; }
inline bool& reserved() { static auto* b=new bool(false); return *b; }
// OHOS SDK callback text/slot lifetime is not bounded by local Runtime Close.
// The existing one-lifecycle reservation bounds this process-lifetime reference.
inline fastdds_dsoftbus_hook_platform::Module& retired_callback_module() {
    static fastdds_dsoftbus_hook_platform::Module module=nullptr; return module;
}
// One required Participant lifecycle is reserved per process. This slot does
// not allocate when an exception occurs after the plugin has acquired owners.
inline std::shared_ptr<Lease>& unpublished_failure() { static std::shared_ptr<Lease> value; return value; }
struct PendingLeaseGuard {
    std::shared_ptr<Lease> value;
    explicit PendingLeaseGuard(const std::shared_ptr<Lease>& p) noexcept : value(p) {}
    void disarm() noexcept { value.reset(); }
    ~PendingLeaseGuard() noexcept {
        if(!value)return;
        // Partial DDS construction may still own the descriptor: do not Close.
        std::lock_guard<std::mutex> lock(gate());
        unpublished_failure()=value;
        std::fputs("FDDST_UNPUBLISHED_OWNER_RETAINED_INCOMPLETE\n",stderr);
    }
};
inline bool absolute_file(const std::string& p) {
    return fastdds_dsoftbus_hook_platform::absolute_file(p);
}
inline std::shared_ptr<Lease> prepare(unsigned domain, void* qos, bool& ok) {
    ok=true; const char* path=std::getenv("FASTDDS_DSOFTBUS_REQUIRED_CONFIG");
    if(!path)return {};
    ok=false;
    if(!absolute_file(path)){std::fputs("FDDST_REQUIRED_ABSOLUTE_CONFIG_MISSING\n",stderr);return {};}
    { std::lock_guard<std::mutex> l(gate()); if(reserved())return {}; reserved()=true; }
    std::ifstream input(path,std::ios::binary); std::string key,library,line;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.rfind("bridge_library=",0)==0)library=line.substr(15);
    }
    auto p=std::make_shared<Lease>();
    if(absolute_file(library))p->module=fastdds_dsoftbus_hook_platform::load(library.c_str());
    auto version=reinterpret_cast<Version>(fastdds_dsoftbus_hook_platform::symbol(p->module,"FastddsDsoftbusHookVersion"));
    auto create=reinterpret_cast<Create>(fastdds_dsoftbus_hook_platform::symbol(p->module,"FastddsDsoftbusCreate"));
    p->close=reinterpret_cast<Close>(fastdds_dsoftbus_hook_platform::symbol(p->module,"FastddsDsoftbusClose"));
    p->public_delete=reinterpret_cast<PublicDelete>(fastdds_dsoftbus_hook_platform::symbol(p->module,"FastddsDsoftbusAfterPublicDelete"));
    p->capture=reinterpret_cast<Capture>(fastdds_dsoftbus_hook_platform::symbol(p->module,"FastddsDsoftbusCaptureStartup"));
    if(version&&version()==21405&&create&&p->close&&create(path,domain,qos,&p->owner)==0) {
        ok=true; return p;
    }
    std::fputs("FDDST_REQUIRED_CREATE_FAILED\n",stderr); std::fflush(stderr);
    // A failed plugin must retain any uncertain owner; never turn that into
    // successful normal cleanup or unload code that can still be referenced.
    if(!p->owner) { if(p->module)fastdds_dsoftbus_hook_platform::unload(p->module); std::lock_guard<std::mutex> l(gate()); reserved()=false; }
    else { std::lock_guard<std::mutex> l(gate()); unpublished_failure()=p; }
    return {};
}
inline void attach(void* part,const std::shared_ptr<Lease>& p) {
    if(!p)return; std::lock_guard<std::mutex> l(gate()); leases().emplace(part,p);
}
enum class CloseCause { FailedConstruction, PublicDelete };
inline bool release_pending(const std::shared_ptr<Lease>& p, CloseCause cause=CloseCause::FailedConstruction) {
    if(!p)return true;
    unsigned active=0;
    if(!p->close_state.compare_exchange_strong(active,1))return active==2&&p->terminal_result==0;
    // Notify only the exclusive closer. A competing deletion must not call
    // into an owner which the winning closer may already be releasing.
    if(cause==CloseCause::PublicDelete&&p->public_delete)p->public_delete(p->owner);
    p->terminal_result=p->close(p->owner);
    // ABI result 2 means resources were closed but runtime execution failed.
    // Result 1 means uncertain resources/owner must remain retained.
    if(p->terminal_result!=0&&p->terminal_result!=2) { p->close_state=3;std::fputs("FDDST_CLOSE_INCOMPLETE\n",stderr);return false; }
    p->owner=nullptr;
    p->close_state=2;
    // Captures read only a process-lifetime POD cache, never owner. Close and
    // its output need no copy lock. Pin only unloading against an in-flight copy.
    {
    std::lock_guard<std::mutex> module_lifetime(p->module_copy);
#if defined(__OHOS__)
    { std::lock_guard<std::mutex> lock(gate()); retired_callback_module()=p->module; }
#else
    fastdds_dsoftbus_hook_platform::unload(p->module);
#endif
    p->module=nullptr;
    }
#if defined(__OHOS__)
    std::fputs("FDDST_MODULE_RETAINED_UNTIL_PROCESS_EXIT sdk_callback_drain=NOT_PROVEN\n",stderr);
#endif
    // This Host profile permits one Participant lifecycle per process. Its
    // existing process-global native entries are not a hot-reuse guarantee.
    if(p->terminal_result==2)std::fputs("FDDST_CLOSE_RESOURCE_COMPLETE_WITH_RUNTIME_ERROR\n",stderr);
    return p->terminal_result==0;
}
inline int capture_startup(fastdds_dsoftbus::ros2_diagnostics::Snapshot* output) noexcept {
    if(!output)return 1;
    try {
        std::shared_ptr<Lease> lease;
        {std::unique_lock<std::mutex> lock(gate(),std::try_to_lock);if(!lock.owns_lock())return 1;if(!leases().empty())lease=leases().begin()->second;else lease=unpublished_failure();}
        if(!lease)return 1;
        std::unique_lock<std::mutex> pin(lease->module_copy,std::try_to_lock);
        if(!pin.owns_lock()||lease->close_state.load()!=0||!lease->module||!lease->capture)return 1;
        // Copy only the independent fixed diagnostic cache. No owner access,
        // Runtime/SDK calls, formatting or I/O while pinning module code.
        return lease->capture(output);
    }catch(...){return 1;}
}
inline bool after_delete(void* part) {
    std::shared_ptr<Lease> p;
    {std::lock_guard<std::mutex> l(gate());auto it=leases().find(part);if(it==leases().end())return true;p=it->second;}
    const bool clean=release_pending(p,CloseCause::PublicDelete);
    if(p->close_state.load()==2){std::lock_guard<std::mutex> l(gate());leases().erase(part);}
    return clean;
}
inline void fail_pending(const std::shared_ptr<Lease>& p) {
    if(!p)return;
    release_pending(p);
    if(p->close_state.load()!=2){std::lock_guard<std::mutex> l(gate());unpublished_failure()=p;}
}
inline void destructor_notice(void* part) {
    bool retained=false;{std::lock_guard<std::mutex> l(gate());retained=leases().count(part)!=0;}
    if(retained)std::fputs("FDDST_FACTORY_DESTRUCTOR_INCOMPLETE_NO_NORMAL_CLOSE\n",stderr);
    // Normal context deletion is required. Do not start/join a plugin pump in
    // the DLL/factory teardown path, where the Windows loader lock may be held.
}
}

extern "C" FASTDDS_DSOFTBUS_FACTORY_EXPORT unsigned FastddsDsoftbusFactoryHookAbi(){return 21405;}
extern "C" FASTDDS_DSOFTBUS_FACTORY_EXPORT int FastddsDsoftbusFactoryCaptureStartup(fastdds_dsoftbus::ros2_diagnostics::Snapshot* output) noexcept{return fastdds_dsoftbus_hook::capture_startup(output);}
