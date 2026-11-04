#pragma once
// Read-only bounded SPDP parameter observation. No payload retained or modified.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace fastdds_dsoftbus::experiment {
struct SpdpLocatorObservation {
    std::array<std::uint8_t,16> writer{},address{};
    std::uint32_t sequence_high=0,sequence_low=0,kind=0,port=0;
    std::uint16_t pid=0,serialized_bytes=0;
    std::size_t data_submessage_offset=0;
};
struct SpdpLocatorBatch {
    bool valid=false,unsupported=false,overflow=false;
    std::size_t count=0;
    std::array<SpdpLocatorObservation,32> values{};
};
inline SpdpLocatorBatch ObserveSpdpLocators(const void* pointer,std::size_t size) noexcept {
    SpdpLocatorBatch out;
    if(!pointer||size<20)return out;
    const auto* b=static_cast<const std::uint8_t*>(pointer);
    if(std::memcmp(b,"RTPS",4)||b[4]!=2)return out;
    std::array<std::uint8_t,12> prefix{};std::memcpy(prefix.data(),b+8,12);
    const auto u16=[&](std::size_t p,bool le) {return std::uint16_t(le?unsigned(b[p])|(unsigned(b[p+1])<<8):(unsigned(b[p])<<8)|unsigned(b[p+1]));};
    const auto u32=[&](std::size_t p,bool le) {std::uint32_t v=0;for(unsigned j=0;j<4;++j)v|=std::uint32_t(b[p+j])<<(8*(le?j:3-j));return v;};
    for(std::size_t offset=20;offset<size;) {
        if(size-offset<4)return {};
        const auto id=b[offset],flags=b[offset+1];const bool le=(flags&1)!=0;
        const auto length=u16(offset+2,le);const auto body=offset+4;
        if(length>size-body)return {};
        const auto end=length?body+length:((id==1||id==9)?body:size);
        if(id==0x0c) {if(end-body<20)return {};std::memcpy(prefix.data(),b+body+8,12);}
        if(id==0x15||id==0x16) {
            if(end-body<20)return {};
            const bool spdp=b[body+8]==0&&b[body+9]==1&&b[body+10]==0&&b[body+11]==0xc2;
            if(spdp&&id==0x16){out.unsupported=true;return out;}
            if(id==0x15&&(flags&12)==12)return {};
            if(spdp&&(flags&4)) {
                const auto inline_offset=u16(body+2,le);
                if(inline_offset<16||inline_offset>end-body-4)return {};
                auto p=body+4+inline_offset;
                if(flags&2) {
                    bool sentinel=false;
                    while(p<end) {
                        if(end-p<4)return {};
                        const auto pid=u16(p,le),n=u16(p+2,le);p+=4;
                        if(pid==1){if(n!=0)return {};sentinel=true;break;}
                        const auto padded=(std::size_t(n)+3)&~std::size_t(3);
                        if(padded>end-p)return {};p+=padded;
                    }
                    if(!sentinel)return {};
                }
                if(end-p<4)return {};
                const auto encoding=(unsigned(b[p])<<8)|b[p+1];
                if((encoding!=2&&encoding!=3)||b[p+2]||b[p+3]){out.unsupported=true;return out;}
                const bool payload_le=encoding==3;p+=4;
                bool sentinel=false;
                while(p<end) {
                    if(end-p<4)return {};
                    const auto pid=u16(p,payload_le),n=u16(p+2,payload_le);p+=4;
                    if(pid==1){if(n!=0)return {};sentinel=true;break;}
                    const auto padded=(std::size_t(n)+3)&~std::size_t(3);
                    if(padded>end-p)return {};
                    if(pid==0x31||pid==0x32) {
                        if(n!=24)return {};
                        if(out.count==out.values.size()){out.overflow=true;return out;}
                        auto& value=out.values[out.count++];value.pid=pid;value.serialized_bytes=n;
                        value.data_submessage_offset=offset;
                        std::memcpy(value.writer.data(),prefix.data(),12);std::memcpy(value.writer.data()+12,b+body+8,4);
                        value.sequence_high=u32(body+12,le);value.sequence_low=u32(body+16,le);
                        value.kind=u32(p,payload_le);value.port=u32(p+4,payload_le);std::memcpy(value.address.data(),b+p+8,16);
                    }
                    p+=padded;
                }
                if(!sentinel)return {};
                // Only zero RTPS alignment padding may follow this parameter list.
                if(end-p>3)return {};for(;p<end;++p)if(b[p])return {};
            }
        }
        offset=end;
    }
    out.valid=true;return out;
}
}
