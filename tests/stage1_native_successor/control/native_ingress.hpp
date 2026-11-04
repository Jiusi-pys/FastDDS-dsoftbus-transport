#pragma once
#include "deployment.hpp"
#include "restoration.hpp"
namespace stage1_control {
// Exact decimal descriptor arguments; never a selector/approval interface.
int native_fd_number(const char*);
size_t native_file_size(int64_t,size_t,bool regular);
// Pure native-purpose binding validation, shared with the Host contract test.
NS_RunRequest native_bound_request(const Deployment&,unsigned,uint32_t,uint32_t,
    const std::string&,const std::string&,Clock::time_point);
// Preparation only. Real business loader/identity route remains disabled.
int native_fd_prepare(int,char**);
// Native parent and OS-only child share existing bounded snapshot primitives.
int native_snapshot(int,size_t,const std::string&,Clock::time_point);
int native_open_input(const std::string&);
uint64_t native_parent_start(uint32_t);
bool native_sole_main();
std::string native_digest(const std::string&);
}
