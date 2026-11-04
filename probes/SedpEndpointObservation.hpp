#pragma once
#include "PublicSocketRtpsObservation.hpp"
#include <algorithm>
namespace fastdds_dsoftbus::experiment {
struct SedpEndpoint {std::array<std::uint8_t,16> guid{};std::array<char,256> topic{};bool reader=false;};
struct SedpEndpoints {std::array<SedpEndpoint,32> values{};std::size_t count=0;bool valid=true;};
inline SedpEndpoints ObserveSedpEndpoints(const std::uint8_t* bytes,std::size_t size,const RtpsObservation& packet) noexcept {
    SedpEndpoints result;
    if(!bytes||!packet.valid){result.valid=false;return result;}
    for(const auto& data:packet.data){
        const auto& id=data.writer_guid;
        if(id[12]||id[13]||(id[14]!=3&&id[14]!=4)||id[15]!=0xc2)continue;
        const auto offset=data.submessage_offset;
        if(offset>size||size-offset<4){result.valid=false;break;}
        const auto body=offset+4;
        if(data.submessage_body_bytes>size-body||data.submessage_body_bytes<20){result.valid=false;break;}
        const auto end=body+data.submessage_body_bytes;
        const bool little=(bytes[offset+1]&1)!=0;
        const auto u16=[&](std::size_t p,bool le){return std::uint16_t(le?unsigned(bytes[p])|(unsigned(bytes[p+1])<<8):(unsigned(bytes[p])<<8)|unsigned(bytes[p+1]));};
        const auto inline_offset=u16(body+2,little);
        if(inline_offset<16||(bytes[offset+1]&12)==12){result.valid=false;break;}
        auto cursor=body+4+inline_offset;if(cursor>end){result.valid=false;break;}
        if(bytes[offset+1]&2){bool sentinel=false;while(end-cursor>=4){const auto pid=u16(cursor,little),n=u16(cursor+2,little);cursor+=4;if(pid==1){if(n==0)sentinel=true;break;}const auto aligned=(std::size_t(n)+3)&~std::size_t(3);if(aligned>end-cursor)break;cursor+=aligned;}if(!sentinel){result.valid=false;break;}}
        if(!(bytes[offset+1]&4))continue; // disposed/key-only data is not an endpoint advertisement
        if(end-cursor<4||bytes[cursor]!=0||(bytes[cursor+1]!=2&&bytes[cursor+1]!=3)){result.valid=false;break;}
        const bool cdr_little=bytes[cursor+1]==3;cursor+=4;
        SedpEndpoint item;bool guid=false,topic=false,sentinel=false;
        while(end-cursor>=4){const auto pid=u16(cursor,cdr_little),n=u16(cursor+2,cdr_little);cursor+=4;const auto aligned=(std::size_t(n)+3)&~std::size_t(3);
            if(pid==1){if(n==0)sentinel=true;else result.valid=false;break;}if(aligned>end-cursor){result.valid=false;break;}
            if(pid==0x005a){if(n!=16||guid){result.valid=false;break;}std::copy_n(bytes+cursor,16,item.guid.begin());guid=true;}
            if(pid==0x0005){if(n<5||topic){result.valid=false;break;}std::uint32_t length=0;for(unsigned j=0;j<4;++j)length|=std::uint32_t(bytes[cursor+j])<<(cdr_little?j*8:(3-j)*8);
                if(length<2||length>256||length>n-4||bytes[cursor+4+length-1]!=0){result.valid=false;break;}
                for(std::size_t j=0;j+1<length;++j){const auto ch=bytes[cursor+4+j];if(ch<33||ch>126){result.valid=false;break;}item.topic[j]=char(ch);}topic=true;}
            cursor+=aligned;
        }
        if(!result.valid||!guid||!topic||!sentinel){result.valid=false;break;}
        if(end-cursor>3||!std::all_of(bytes+cursor,bytes+end,[](auto value){return value==0;})){result.valid=false;break;}
        item.reader=id[14]==4;
        const auto kind=item.guid[15];if(item.reader?(kind!=4&&kind!=7):(kind!=2&&kind!=3)){result.valid=false;break;}
        if(result.count==result.values.size()){result.valid=false;break;}result.values[result.count++]=item;
    }
    return result;
}
}
