#pragma once
#include "native_ingress.hpp"
#include <array>
namespace stage1_control {
constexpr size_t parent_boot_bytes=128;
struct ParentBoot {uint32_t parent=0,child=0,side=0,budget_ms=0;uint64_t run=0,birth=0;std::string manifest_sha;};
std::array<uint8_t,parent_boot_bytes> encode_parent_boot(const ParentBoot&);
ParentBoot decode_parent_boot(const uint8_t*,size_t,uint32_t,uint32_t,uint64_t);
void validate_os_fd_set(int,const std::array<int,4>&);
int native_os_child(int,char**);
int native_parent(int,char**);
}
