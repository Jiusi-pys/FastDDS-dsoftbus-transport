#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace fastdds_dsoftbus::dsoftbus::phase0 {
// Native control-plane identity only. These bytes are never added to RTPS data.
// Preserve the entire authorized base, use a fixed-width canonical DID, and
// reject overlength rather than truncate/hash into another service identity.
inline bool EncodeDomainCircuitName(std::string_view base, std::uint32_t domain, std::string& out) {
    constexpr std::string_view marker=".dds1d";
    if(base.empty()||base.size()>255-marker.size()-8||base.find('\0')!=std::string_view::npos)return false;
    std::string prepared(base);prepared+=marker;
    constexpr char digits[]="0123456789abcdef";
    for(unsigned shift=32;shift;shift-=4)prepared+=digits[(domain>>(shift-4))&15];
    out.swap(prepared);return true;
}
inline bool DecodeDomainCircuitName(std::string_view name, std::string_view& base, std::uint32_t& domain) noexcept {
    constexpr std::string_view marker=".dds1d";
    if(name.size()<=marker.size()+8||name.size()>255||name.find('\0')!=std::string_view::npos)return false;
    const auto pos=name.size()-marker.size()-8;
    if(name.substr(pos,marker.size())!=marker)return false;
    std::uint32_t value=0;
    for(char c:name.substr(pos+marker.size())) {
        const unsigned digit=c>='0'&&c<='9'?unsigned(c-'0'):c>='a'&&c<='f'?unsigned(c-'a'+10):16;
        if(digit==16)return false;
        value=(value<<4)|digit;
    }
    base=name.substr(0,pos);domain=value;return true;
}
}
