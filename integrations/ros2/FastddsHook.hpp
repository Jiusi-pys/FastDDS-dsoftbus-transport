#pragma once
// Injected only into the declared isolated Fast DDS 2.14.5 build. No ROS headers.
#include <windows.h>
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
struct Lease { HMODULE module=nullptr; void* owner=nullptr; Close close=nullptr; std::atomic<unsigned> close_state{0}; int terminal_result=1; };
inline std::mutex& gate() { static auto* g=new std::mutex; return *g; }
inline auto& leases() { static auto* m=new std::unordered_map<void*,std::shared_ptr<Lease>>; return *m; }
inline bool& reserved() { static auto* b=new bool(false); return *b; }
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
    const bool rooted=(p.size()>2&&p[1]==':'&&(p[2]=='\\'||p[2]=='/')) ||
            (p.size()>1&&p[0]=='\\'&&p[1]=='\\');
    const auto attributes=GetFileAttributesA(p.c_str());
    return rooted&&attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
inline std::shared_ptr<Lease> prepare(unsigned domain, void* qos, bool& ok) {
    ok=true; const char* path=std::getenv("FASTDDS_DSOFTBUS_REQUIRED_CONFIG");
    if(!path)return {};
    ok=false;
    if(!absolute_file(path)){std::fputs("FDDST_REQUIRED_ABSOLUTE_CONFIG_MISSING\n",stderr);return {};}
    { std::lock_guard<std::mutex> l(gate()); if(reserved())return {}; reserved()=true; }
    std::ifstream input(path); std::string key,library,line;
    while(std::getline(input,line)) if(line.rfind("bridge_library=",0)==0)library=line.substr(15);
    auto p=std::make_shared<Lease>();
    if(absolute_file(library))p->module=LoadLibraryA(library.c_str());
    auto version=p->module?reinterpret_cast<Version>(GetProcAddress(p->module,"FastddsDsoftbusHookVersion")):nullptr;
    auto create=p->module?reinterpret_cast<Create>(GetProcAddress(p->module,"FastddsDsoftbusCreate")):nullptr;
    p->close=p->module?reinterpret_cast<Close>(GetProcAddress(p->module,"FastddsDsoftbusClose")):nullptr;
    if(version&&version()==21405&&create&&p->close&&create(path,domain,qos,&p->owner)==0) {
        ok=true; return p;
    }
    std::fputs("FDDST_REQUIRED_CREATE_FAILED\n",stderr); std::fflush(stderr);
    // A failed plugin must retain any uncertain owner; never turn that into
    // successful normal cleanup or unload code that can still be referenced.
    if(!p->owner) { if(p->module)FreeLibrary(p->module); std::lock_guard<std::mutex> l(gate()); reserved()=false; }
    else { std::lock_guard<std::mutex> l(gate()); unpublished_failure()=p; }
    return {};
}
inline void attach(void* part,const std::shared_ptr<Lease>& p) {
    if(!p)return; std::lock_guard<std::mutex> l(gate()); leases().emplace(part,p);
}
inline bool release_pending(const std::shared_ptr<Lease>& p) {
    if(!p)return true;
    unsigned active=0;
    if(!p->close_state.compare_exchange_strong(active,1))return active==2&&p->terminal_result==0;
    p->terminal_result=p->close(p->owner);
    // ABI result 2 means resources were closed but runtime execution failed.
    // Result 1 means uncertain resources/owner must remain retained.
    if(p->terminal_result!=0&&p->terminal_result!=2) { p->close_state=3;std::fputs("FDDST_CLOSE_INCOMPLETE\n",stderr);return false; }
    p->owner=nullptr;
    p->close_state=2;
    FreeLibrary(p->module);
    // This Host profile permits one Participant lifecycle per process. Its
    // existing process-global native entries are not a hot-reuse guarantee.
    if(p->terminal_result==2)std::fputs("FDDST_CLOSE_RESOURCE_COMPLETE_WITH_RUNTIME_ERROR\n",stderr);
    return p->terminal_result==0;
}
inline bool after_delete(void* part) {
    std::shared_ptr<Lease> p;
    {std::lock_guard<std::mutex> l(gate());auto it=leases().find(part);if(it==leases().end())return true;p=it->second;}
    const bool clean=release_pending(p);
    if(p->close_state.load()==2){std::lock_guard<std::mutex> l(gate());leases().erase(part);}
    return clean;
}
inline void fail_pending(const std::shared_ptr<Lease>& p) {
    if(!p)return;
    release_pending(p);
    if(p->close_state.load()!=2){std::lock_guard<std::mutex> l(gate());unpublished_failure()=p;}
}
inline void destructor_notice(void* part) {
    std::lock_guard<std::mutex> l(gate());
    if(leases().count(part))std::fputs("FDDST_FACTORY_DESTRUCTOR_INCOMPLETE_NO_NORMAL_CLOSE\n",stderr);
    // Normal context deletion is required. Do not start/join a plugin pump in
    // the DLL/factory teardown path, where the Windows loader lock may be held.
}
}

extern "C" __declspec(dllexport) unsigned FastddsDsoftbusFactoryHookAbi(){return 21405;}
