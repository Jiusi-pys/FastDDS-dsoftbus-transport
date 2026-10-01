#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
extern "C" void FastddsDsoftbusFailureSnapshot(std::uint64_t) noexcept;
namespace ros_failure_protocol {
inline std::atomic<std::uint64_t>& sequence(){static std::atomic<std::uint64_t> s{0};return s;}
inline std::uint64_t& primary(){static std::uint64_t value=0;return value;}
inline void Report(const std::string& run,const std::string& side,long pid,const char* stage,const char* reason,bool cleanup=false) noexcept {
    if(!cleanup&&primary())return;
    const auto seq=++sequence();const auto reference=cleanup?primary():0;
    if(!cleanup)primary()=seq;
    // Fixed safe stage/reason identifiers; never serialize exception text,
    // payloads, configuration paths, peer identities or token values.
    std::fprintf(stderr,"\n%s schema=1 run=%s side=%s pid=%ld seq=%llu stage=%s reason=%s failure_ref=%llu\n",cleanup?"ROS_CLEANUP_FAILURE":"ROS_FAILURE",run.c_str(),side.c_str(),pid,static_cast<unsigned long long>(seq),stage,reason,static_cast<unsigned long long>(reference));
    std::fflush(stderr);
    FastddsDsoftbusFailureSnapshot(primary());
}
[[noreturn]] inline void Fail(const std::string& run,const std::string& side,long pid,const char* stage,const char* reason){Report(run,side,pid,stage,reason);throw std::runtime_error(reason);}
}
