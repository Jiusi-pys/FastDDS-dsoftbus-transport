#pragma once
// Same authoritative required configuration; TEST_ONLY never approves production.
#include <cstdint>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <limits>
#include <sstream>
namespace fastdds_dsoftbus { namespace payload_resource {
inline bool config_key(const std::string& key) {
    return key=="payload_guard_profile"||key=="payload_max_sample_bytes"||key=="payload_pool_budget_bytes"||key=="payload_max_allocations";
}
struct Config {bool enabled=false;std::uint32_t m=0,n=0;std::uint64_t b=0,identity=0;std::string content;};
inline std::uint64_t number(const std::string& value,std::uint64_t ceiling) {
    if(value.empty())throw std::invalid_argument("missing payload limit");
    std::uint64_t n=0;
    for(char c:value){if(c<'0'||c>'9'||n>(ceiling-std::uint64_t(c-'0'))/10)throw std::invalid_argument("payload limit syntax/overflow");n=n*10+std::uint64_t(c-'0');}
    if(!n)throw std::invalid_argument("zero payload limit");return n;
}
inline Config parse(const std::map<std::string,std::string>& values) {
    Config c;unsigned present=0;for(const auto& row:values)if(config_key(row.first))++present;
    if(!present)return c;
    if(present!=4||values.at("payload_guard_profile")!="TEST_ONLY_BOUNDED")throw std::invalid_argument("payload profile/NEEDS_PRODUCT_VALUE");
    c.enabled=true;c.m=static_cast<std::uint32_t>(number(values.at("payload_max_sample_bytes"),UINT32_MAX-UINT16_MAX));
    c.b=number(values.at("payload_pool_budget_bytes"),UINT64_MAX);c.n=static_cast<std::uint32_t>(number(values.at("payload_max_allocations"),UINT32_MAX));
    if(c.m<4||c.b<c.m)throw std::invalid_argument("payload M/B contract");return c;
}
inline Config read(const char* path) {
    std::ifstream file(path,std::ios::binary);if(!file)throw std::invalid_argument("payload configuration missing");
    std::string raw(65537,'\0');file.read(&raw[0],raw.size());
    if(file.bad()||file.gcount()>65536)throw std::invalid_argument("payload configuration read/size limit");
    raw.resize(static_cast<std::size_t>(file.gcount()));std::istringstream input(raw);
    std::map<std::string,std::string> values;std::string line,content;std::uint64_t identity=1469598103934665603ULL;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        content+=line+'\n';
        for(unsigned char c:line){identity^=c;identity*=1099511628211ULL;}identity^='\n';identity*=1099511628211ULL;
        const auto equal=line.find('=');if(equal==std::string::npos||!values.emplace(line.substr(0,equal),line.substr(equal+1)).second)throw std::invalid_argument("payload config syntax/duplicate");
    }
    auto c=parse(values);c.identity=identity;c.content=std::move(content);return c; // Diagnostic hash, not trust attestation.
}
} }
