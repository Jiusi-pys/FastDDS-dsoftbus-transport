#include "parent_protocol.hpp"
#include <cstring>
#include <stdexcept>
namespace stage1_control {
namespace {
void need(bool b){if(!b)throw std::invalid_argument("owned parent bootstrap binding");}
void word(uint8_t* p,uint64_t v,unsigned n){while(n){p[--n]=static_cast<uint8_t>(v);v>>=8;}}
uint64_t number(const uint8_t* p,unsigned n){uint64_t v=0;while(n--)v=(v<<8)|*p++;return v;}
bool hash(const std::string& v){return v.size()==64&&v.find_first_not_of("0123456789abcdef")==std::string::npos;}
}
void validate_os_fd_set(int gate,const std::array<int,4>& data){
    need(gate>=3);for(size_t i=0;i<data.size();++i){need(data[i]>=3&&data[i]!=gate);for(size_t j=0;j<i;++j)need(data[i]!=data[j]);}
}
std::array<uint8_t,parent_boot_bytes> encode_parent_boot(const ParentBoot& b){
    need(b.parent&&b.child&&b.parent!=b.child&&b.side<2&&b.run&&b.birth&&b.budget_ms&&b.budget_ms<=5000&&hash(b.manifest_sha));std::array<uint8_t,parent_boot_bytes> out{};
    std::memcpy(out.data(),"NP01",4);word(out.data()+4,1,2);word(out.data()+6,parent_boot_bytes,2);word(out.data()+8,b.parent,4);word(out.data()+12,b.child,4);word(out.data()+16,b.side,4);word(out.data()+24,b.run,8);word(out.data()+32,b.budget_ms,4);word(out.data()+36,b.birth,8);std::memcpy(out.data()+44,b.manifest_sha.data(),64);return out;
}
ParentBoot decode_parent_boot(const uint8_t* p,size_t n,uint32_t child,uint32_t parent,uint64_t run){
    need(p&&n==parent_boot_bytes&&!std::memcmp(p,"NP01",4)&&number(p+4,2)==1&&number(p+6,2)==parent_boot_bytes);for(size_t i=20;i<24;++i)need(!p[i]);for(size_t i=108;i<n;++i)need(!p[i]);
    ParentBoot b{static_cast<uint32_t>(number(p+8,4)),static_cast<uint32_t>(number(p+12,4)),static_cast<uint32_t>(number(p+16,4)),static_cast<uint32_t>(number(p+32,4)),number(p+24,8),number(p+36,8),std::string(reinterpret_cast<const char*>(p+44),64)};
    (void)encode_parent_boot(b);need(b.child==child&&b.parent==parent&&b.run==run);return b;
}
}
