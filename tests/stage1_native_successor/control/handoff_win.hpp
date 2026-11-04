#pragma once
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "deployment.hpp"
#include <vector>
#include <cstring>
#include <stdexcept>
namespace stage1_control::handoff {
#ifdef STAGE1_BOUND_FIXTURE_SHA
inline constexpr const char qualified_fixture_sha[]=STAGE1_BOUND_FIXTURE_SHA;
#else
inline constexpr const char qualified_fixture_sha[]="759a4950944ee4d6ccce998418ee07268e2078194ff2ca1e670acae075c8d57c";
#endif
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    Handle()=default;explicit Handle(HANDLE h):value(h){}~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(Handle&& h)noexcept:value(h.release()){}Handle& operator=(Handle&& h)noexcept{if(this!=&h){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=h.release();}return *this;}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    HANDLE release()noexcept{HANDLE h=value;value=INVALID_HANDLE_VALUE;return h;}
};
struct FileId {uint32_t volume=0,high=0,low=0;bool operator==(const FileId& b)const{return volume==b.volume&&high==b.high&&low==b.low;}};
class LockedFile {
    struct State;std::unique_ptr<State> state_;
public:
    explicit LockedFile(const std::string& absolute);
    static std::unique_ptr<LockedFile> adopt_readonly(HANDLE& known_owned);
    ~LockedFile();LockedFile(const LockedFile&)=delete;LockedFile& operator=(const LockedFile&)=delete;
    HANDLE handle()const;FileId id()const;const std::string& path()const;
    std::vector<uint8_t> read(size_t maximum,Clock::time_point)const;
    std::string sha(size_t maximum,Clock::time_point)const;
};
std::string sha_bytes(const void*,size_t);
std::string current_image();
uint64_t creation_time(HANDLE);
bool sole_main_thread();
void diagnostic_threads(const char* checkpoint); // own process metadata only, no module/getter/business
class Module {
    struct State;std::shared_ptr<State> state_;std::shared_ptr<void> image_;std::unique_ptr<std::shared_ptr<State>> pin_;
public:
    Module(const std::string&,const std::string& sha,Clock::time_point);
    ~Module();bool release_fixture();
    std::shared_ptr<void> keepalive()const{return image_;}
    HMODULE handle()const{return static_cast<HMODULE>(image_.get());}
    template<class F>F function(const char* name)const{F f{};auto raw=GetProcAddress(handle(),name);static_assert(sizeof(f)==sizeof(raw),"Win function pointer ABI");std::memcpy(&f,&raw,sizeof(f));if(!f)throw std::runtime_error("module export missing");return f;}
    static bool owns(void*,const void*);
};
void write_manifest_new(const std::string& path,const std::string& bytes);
}
#endif
