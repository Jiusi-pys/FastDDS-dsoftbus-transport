#pragma once
#include <cstddef>
#include <cstdint>
namespace stage1_entry {
enum Field {Format,Intent,Run,Side,Board,Own,Caller,Target,Issued,Until,Frontend,FrontendSha,Bridge,BridgeSha,Consumer,ConsumerSha,Package,NameSpace,Prefix,Domain,Count};
// Private same-executable POD; never passed across the SDK/platform C++ boundary.
struct Record {char value[Count][1024]{};std::uint64_t caller=0,target=0,issued=0,until=0,domain=0;};
bool parse_admission(const char*,std::size_t,std::uint64_t,Record&,const char*&);
bool verify_admission(const char* record_path,const char* side,const char* image,Record&,char digest[65],const char*&);
bool admission_current(const Record&,const char*&);
bool admission_selftest();
constexpr bool execution_enabled(){return false;}
}
