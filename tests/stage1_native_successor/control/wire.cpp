#include "wire.hpp"
#include <cstring>
#include <stdexcept>
#include <limits>
namespace stage1_control {
namespace {
bool text(const char* s,size_t cap,size_t required=0)noexcept{
    const auto* end=static_cast<const char*>(std::memchr(s,0,cap));if(!end||end==s)return false;const auto n=static_cast<size_t>(end-s);if(required&&n!=required)return false;
    for(size_t i=0;i<n;++i){const auto c=static_cast<unsigned char>(s[i]);if(c<33||c>126)return false;if(required==64&&!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;}return true;
}
struct Writer {
    std::vector<uint8_t> b;
    void u(uint64_t v,unsigned n){for(unsigned i=n;i>0;--i)b.push_back(static_cast<uint8_t>(v>>(8*(i-1))));}
    void raw(const void* p,size_t n){auto q=static_cast<const uint8_t*>(p);b.insert(b.end(),q,q+n);}
    void str(const char* s){const auto n=std::strlen(s);u(n,2);raw(s,n);}
    void envelope(const NS_Envelope& e){u(e.run,8);u(e.self_token,8);u(e.side,4);u(e.domain,4);u(e.pid,4);str(e.board);str(e.executable_sha);str(e.deployment_sha);str(e.name_space);}
};
struct Reader {
    const uint8_t* p;size_t n,at=0;
    uint64_t u(unsigned count){if(count>n-at)throw Error::Invalid;uint64_t v=0;for(unsigned i=0;i<count;++i)v=(v<<8)|p[at++];return v;}
    void raw(void* out,size_t count){if(count>n-at)throw Error::Invalid;std::memcpy(out,p+at,count);at+=count;}
    void str(char* out,size_t cap){auto len=u(2);if(!len||len>=cap||len>n-at)throw Error::Bound;raw(out,static_cast<size_t>(len));out[len]=0;for(size_t i=0;i<len;++i)if(static_cast<unsigned char>(out[i])<33||static_cast<unsigned char>(out[i])>126)throw Error::Invalid;}
    NS_Envelope envelope(){NS_Envelope e{};e.run=u(8);e.self_token=u(8);e.side=static_cast<uint32_t>(u(4));e.domain=static_cast<uint32_t>(u(4));e.pid=static_cast<uint32_t>(u(4));str(e.board,sizeof e.board);str(e.executable_sha,sizeof e.executable_sha);str(e.deployment_sha,sizeof e.deployment_sha);str(e.name_space,sizeof e.name_space);return e;}
};
int32_t signed32(uint32_t v)noexcept{if(v<=INT32_MAX)return static_cast<int32_t>(v);return -1-static_cast<int32_t>(~v);}
bool direction_kind(Kind k,Direction d)noexcept{return (d==Direction::Endpoint&&(k==Kind::Event||k==Kind::Abort))||(d==Direction::Coordinator&&(k==Kind::Release||k==Kind::Abort));}
}
bool valid_envelope(const NS_Envelope& e)noexcept{
    static const char* boards[]={"3e01ff55454d202020104033bf453b00","3e01ff55454d202020104433991c3b00"};
    return e.run&&e.self_token&&e.pid&&e.side<2&&text(e.board,sizeof e.board)&&!std::strcmp(e.board,boards[e.side])&&text(e.executable_sha,sizeof e.executable_sha,64)&&text(e.deployment_sha,sizeof e.deployment_sha,64)&&text(e.name_space,sizeof e.name_space);
}
bool same_envelope(const NS_Envelope& a,const NS_Envelope& b)noexcept{return valid_envelope(a)&&valid_envelope(b)&&a.run==b.run&&a.self_token==b.self_token&&a.side==b.side&&a.domain==b.domain&&a.pid==b.pid&&!std::strcmp(a.board,b.board)&&!std::strcmp(a.executable_sha,b.executable_sha)&&!std::strcmp(a.deployment_sha,b.deployment_sha)&&!std::strcmp(a.name_space,b.name_space);}
std::vector<uint8_t> encode(const Frame& f){
    if(!valid_envelope(f.envelope)||f.nonce!=f.envelope.run||!f.sequence||f.sequence>frame_count_limit||!direction_kind(f.kind,f.direction))throw std::invalid_argument("control frame identity/direction");
    Writer body;body.envelope(f.envelope);
    if(f.kind==Kind::Event){const auto& e=f.event;if(e.header.version!=NS_RUN_VERSION||e.header.size!=sizeof(e)||e.header.scope!=NS_SCOPE||e.header.reserved||!same_envelope(e.envelope,f.envelope)||e.kind<NS_ROOT_LISTENER||e.kind>NS_TEARDOWN||e.sequence<0)throw std::invalid_argument("control event");
        body.u(e.kind,4);body.u(e.ordinal,4);body.u(static_cast<uint64_t>(e.sequence),8);body.raw(e.writer_guid,16);body.raw(e.payload,32);body.u(static_cast<uint32_t>(e.socket),4);body.u(static_cast<uint32_t>(e.listen_rc),4);body.u(e.flags,4);body.u(e.writer_matches,4);body.u(e.reader_matches,4);
    }else if(f.kind==Kind::Release){if(f.release<1||f.release>4)throw std::invalid_argument("control release");body.u(f.release,4);}
    else body.u(static_cast<uint32_t>(f.error),4);
    if(body.b.size()>body_limit)throw std::length_error("control body bound");
    Writer w;const uint8_t magic[]={0x53,0x31,0x43,0x46};w.raw(magic,4);w.u(1,2);w.u(static_cast<uint8_t>(f.kind),1);w.u(static_cast<uint8_t>(f.direction),1);w.u(body.b.size(),4);w.u(f.sequence,4);w.u(f.nonce,8);w.raw(body.b.data(),body.b.size());return w.b;
}
Error header_size(const uint8_t* p,size_t n,uint32_t& body)noexcept{
    if(!p||n!=header_bytes)return Error::Invalid;
    try{Reader r{p,n};if(r.u(4)!=0x53314346u||r.u(2)!=1)return Error::Invalid;auto k=static_cast<Kind>(r.u(1));auto d=static_cast<Direction>(r.u(1));if(!direction_kind(k,d))return Error::Invalid;body=static_cast<uint32_t>(r.u(4));auto seq=r.u(4);if(!body||body>body_limit||!seq||seq>frame_count_limit||!r.u(8))return Error::Bound;return Error::None;}catch(...){return Error::Invalid;}
}
Error decode(const uint8_t* p,size_t n,Frame& out)noexcept{
    if(!p||n<header_bytes||n>frame_limit)return Error::Bound;uint32_t body=0;auto h=header_size(p,header_bytes,body);if(h!=Error::None)return h;if(n!=header_bytes+body)return Error::Invalid;
    try{Reader hdr{p,header_bytes};(void)hdr.u(4);(void)hdr.u(2);Frame f;f.kind=static_cast<Kind>(hdr.u(1));f.direction=static_cast<Direction>(hdr.u(1));(void)hdr.u(4);
        f.sequence=static_cast<uint32_t>(hdr.u(4));f.nonce=hdr.u(8);Reader r{p+header_bytes,body};f.envelope=r.envelope();if(!valid_envelope(f.envelope)||f.envelope.run!=f.nonce)return Error::Envelope;
        if(f.kind==Kind::Event){auto& e=f.event;e.header={NS_RUN_VERSION,sizeof(e),NS_SCOPE,0};e.envelope=f.envelope;e.kind=static_cast<uint32_t>(r.u(4));e.ordinal=static_cast<uint32_t>(r.u(4));auto seq=r.u(8);if(seq>static_cast<uint64_t>(INT64_MAX)||e.kind<1||e.kind>7)return Error::Invalid;e.sequence=static_cast<int64_t>(seq);r.raw(e.writer_guid,16);r.raw(e.payload,32);e.socket=signed32(static_cast<uint32_t>(r.u(4)));e.listen_rc=signed32(static_cast<uint32_t>(r.u(4)));e.flags=static_cast<uint32_t>(r.u(4));e.writer_matches=static_cast<uint32_t>(r.u(4));e.reader_matches=static_cast<uint32_t>(r.u(4));}
        else if(f.kind==Kind::Release){f.release=static_cast<uint32_t>(r.u(4));if(f.release<1||f.release>4)return Error::Invalid;}
        else f.error=signed32(static_cast<uint32_t>(r.u(4)));
        if(r.at!=r.n)return Error::Invalid;out=f;return Error::None;
    }catch(Error e){return e;}catch(...){return Error::Invalid;}
}
bool codec_selftest(){
    // Independent fixed header vector: S1CF/v1/EVENT/endpoint/body=1,seq=1,
    // nonce0102030405060708. Body bound/header is checked without native calls.
    const uint8_t golden[]={0x53,0x31,0x43,0x46,0,1,1,1,0,0,0,1,0,0,0,1,1,2,3,4,5,6,7,8};uint32_t n=0;
    if(header_size(golden,sizeof golden,n)!=Error::None||n!=1)return false;
    auto broken=std::array<uint8_t,24>{};std::memcpy(broken.data(),golden,24);broken[5]=2;if(header_size(broken.data(),24,n)==Error::None)return false;
    return true;
}
}
