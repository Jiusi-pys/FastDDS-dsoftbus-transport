#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <iostream>

namespace host_dds {
inline std::string Hex(const std::uint8_t* p,std::size_t n){
    const char* digits="0123456789abcdef";std::string out;out.reserve(n*2);
    for(std::size_t i=0;i<n;++i){out+=digits[p[i]>>4];out+=digits[p[i]&15];}return out;
}
inline std::uint64_t Hash(const std::uint8_t* p,std::size_t n){
    std::uint64_t h=14695981039346656037ull;for(std::size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;
}
struct ProtocolItem {std::string kind,writer;std::uint64_t sn=0;};
// Observation only: never called by routing or response generation.
inline std::vector<ProtocolItem> Parse(const std::uint8_t* p,std::size_t n){
    std::vector<ProtocolItem> out;if(!p||n<20||std::memcmp(p,"RTPS",4))return out;
    std::string prefix=Hex(p+8,12);
    for(std::size_t pos=20;pos+4<=n;){
        const auto id=p[pos],flags=p[pos+1];const bool le=(flags&1)!=0;
        auto u32=[&](std::size_t at){std::uint32_t v=0;for(int j=0;j<4;++j)v|=std::uint32_t(p[at+j])<<(8*(le?j:3-j));return v;};
        const std::size_t len=le?(p[pos+2]|(std::size_t(p[pos+3])<<8)):((std::size_t(p[pos+2])<<8)|p[pos+3]);
        const auto body=pos+4;
        const bool empty_special=id==0x01||(id==0x09&&(flags&2));
        const auto end=len?body+len:(empty_special?body:n);if(end>n)return {};
        if(id==0x0c&&end-body>=20)prefix=Hex(p+body+8,12);
        if(id==0x15&&end-body>=20){
            const auto entity=Hex(p+body+8,4);
            std::string kind=entity=="000100c2"?"SPDP":(entity=="000003c2"||entity=="000004c2")?"SEDP":"DATA";
            out.push_back({kind,prefix+entity,(std::uint64_t(u32(body+12))<<32)|u32(body+16)});
        } else if(id==0x06||id==0x07)out.push_back({id==0x06?"ACKNACK":"HEARTBEAT","",0});
        pos=end;
    }return out;
}
struct Event {
    std::string stage,kind,writer,detail;int side=0;std::uint64_t sn=0,hash=0,correlation=0,app=0,receiver=0;
    std::size_t length=0;
};
class Trace {
public:
    Trace(){events_.reserve(8192);}
    void Add(Event e) noexcept{try{std::lock_guard<std::mutex> l(gate_);if(events_.size()==8192){overflow_=true;return;}events_.push_back(std::move(e));}catch(...){std::lock_guard<std::mutex> l(gate_);overflow_=true;}}
    void Frame(const char* stage,int side,const std::uint8_t* p,std::size_t n,std::uint64_t c=0,std::uint64_t a=0,std::uint64_t r=0)noexcept{
        try{if(!p||!n)return;const auto h=Hash(p,n);for(const auto& x:Parse(p,n))Add({stage,x.kind,x.writer,"",side,x.sn,h,c,a,r,n});}catch(...){Add({"TRACE_ERROR","","","allocation",side});}
    }
    bool Has(const std::string& stage,int side,const std::string& kind,const std::string& writer="",std::uint64_t sn=0)const{
        std::lock_guard<std::mutex> l(gate_);for(const auto&e:events_)if(e.stage==stage&&e.side==side&&e.kind==kind&&(writer.empty()||e.writer==writer)&&(!sn||e.sn==sn))return true;return false;
    }
    bool Good()const{std::lock_guard<std::mutex> l(gate_);if(overflow_)return false;for(const auto&e:events_)if(e.stage=="TRACE_ERROR"||e.stage=="PUMP_ERROR")return false;return true;}
    std::string FirstPumpFailure()const{std::lock_guard<std::mutex> l(gate_);for(const auto&e:events_)if(e.stage=="PUMP_ERROR")return e.detail;return {};}
    void Dump(std::uint64_t run)const{
        std::lock_guard<std::mutex> l(gate_);std::size_t ordinal=0;
        for(const auto&e:events_)std::cout<<"HOST_MODEL run="<<run<<" event="<<++ordinal<<" side="<<e.side<<" stage="<<e.stage<<" kind="<<e.kind<<" writer="<<e.writer<<" sn="<<e.sn<<" length="<<e.length<<" hash="<<e.hash<<" correlation="<<e.correlation<<" app="<<e.app<<" receiver="<<e.receiver<<" detail="<<e.detail<<'\n';
        std::cout<<"TRACE_SUMMARY count="<<events_.size()<<" limit=8192 overflow="<<overflow_<<'\n';
    }
private:mutable std::mutex gate_;std::vector<Event> events_;bool overflow_=false;
};
struct SampleReceipt {std::string reader,writer;std::uint64_t run=0,sn=0;unsigned ordinal=0;bool payload=false,taken=false;};
inline bool CurrentMatch(const std::set<std::string>& actual,const std::string& expected){return actual==std::set<std::string>{expected};}
inline bool LedgerValid(const std::vector<SampleReceipt>& rows,const std::string& reader,const std::string& writer,std::uint64_t run,unsigned first,unsigned last){
    if(rows.size()!=last-first+1)return false;std::set<unsigned> ordinals;std::set<std::uint64_t> seq;
    for(const auto&r:rows)if(r.reader!=reader||r.writer!=writer||r.run!=run||!r.taken||!r.payload||!r.sn||r.ordinal<first||r.ordinal>last||!ordinals.insert(r.ordinal).second||!seq.insert(r.sn).second)return false;
    return true;
}
}
