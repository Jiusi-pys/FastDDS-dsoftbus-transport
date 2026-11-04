#pragma once
#include "run_contract.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>

namespace stage1_run {
using Clock=std::chrono::steady_clock;
inline bool bounded(const char* s,size_t n){return s&&s[0]&&std::memchr(s,0,n);}
inline bool hex(const char* s,size_t n){if(!s||std::strlen(s)!=n)return false;for(size_t i=0;i<n;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;return true;}
inline bool valid(const NS_Envelope& e){
    static const char* boards[]={"3e01ff55454d202020104033bf453b00","3e01ff55454d202020104433991c3b00"};
    return e.run&&e.self_token&&e.side<2&&e.pid&&bounded(e.board,sizeof e.board)&&
        !std::strcmp(e.board,boards[e.side])&&bounded(e.executable_sha,sizeof e.executable_sha)&&hex(e.executable_sha,64)&&
        bounded(e.deployment_sha,sizeof e.deployment_sha)&&hex(e.deployment_sha,64)&&bounded(e.name_space,sizeof e.name_space);
}
inline bool same(const NS_Envelope& a,const NS_Envelope& b){return a.run==b.run&&a.self_token==b.self_token&&a.side==b.side&&a.domain==b.domain&&a.pid==b.pid&&
    !std::strcmp(a.board,b.board)&&!std::strcmp(a.executable_sha,b.executable_sha)&&!std::strcmp(a.deployment_sha,b.deployment_sha)&&!std::strcmp(a.name_space,b.name_space);}
inline std::array<uint8_t,32> payload(uint64_t run,uint32_t side,uint32_t ordinal){std::array<uint8_t,32> p{};for(unsigned i=0;i<p.size();++i)p[i]=uint8_t((run>>(8*(i%8)))^side^ordinal^i);return p;}
inline bool nonzero(const uint8_t* p,size_t n){for(size_t i=0;i<n;++i)if(p[i])return true;return false;}
inline bool event_header(const NS_RunEvent& e){return e.header.version==NS_RUN_VERSION&&e.header.size==sizeof(e)&&e.header.scope==NS_SCOPE&&!e.header.reserved;}

// Progress notifications are not delivery evidence. Snapshot BEFORE inspecting
// DDS status/draining samples; callbacks only increment + notify, never Close.
class Progress {
    std::mutex gate_;std::condition_variable cv_;uint64_t generation_=0;bool stopped_=false;
public:
    uint64_t snapshot(){std::lock_guard<std::mutex> l(gate_);return generation_;}
    void notify(){std::lock_guard<std::mutex> l(gate_);++generation_;cv_.notify_all();}
    void cancel(){std::lock_guard<std::mutex> l(gate_);stopped_=true;cv_.notify_all();}
    bool wait(uint64_t seen,Clock::time_point end){std::unique_lock<std::mutex> l(gate_);return cv_.wait_until(l,end,[&]{return stopped_||seen!=generation_;})&&!stopped_;}
};

// Bounded reusable controller: exact two admitted endpoints, three samples each.
// Native events must be produced by the real consumer; fixtures are explicitly
// distinguished. This class cannot authenticate an external producer or SDK.
class Window {
    struct Sample {bool present=false;NS_RunEvent value{};};
    struct Side {bool root=false,matched=false,ack=false,done=false,torn=false;uint8_t guid[16]{};std::array<Sample,3> writes{},takes{};};
    std::array<NS_Envelope,2> ids_;std::array<Side,2> sides_{};
    Clock::time_point end_;bool fixture_,cancelled_=false;const char* error_=nullptr;
    mutable std::mutex gate_;std::condition_variable cv_;
    bool fail(const char* why){if(!error_)error_=why;cancelled_=true;cv_.notify_all();return false;}
    bool all(const std::array<Sample,3>& x)const{return x[0].present&&x[1].present&&x[2].present;}
    bool correlate(unsigned side)const{
        const auto& tx=sides_[side].writes;const auto& rx=sides_[1-side].takes;
        if(!all(tx)||!all(rx))return false;
        for(unsigned i=0;i<3;++i)if(tx[i].value.sequence!=rx[i].value.sequence||std::memcmp(tx[i].value.writer_guid,rx[i].value.writer_guid,16)||std::memcmp(tx[i].value.payload,rx[i].value.payload,32))return false;
        return true;
    }
    bool released(uint32_t phase)const{
        if(phase==NS_CONNECT_RELEASE)return sides_[0].root&&sides_[1].root;
        if(phase==NS_SEND_RELEASE)return sides_[0].matched&&sides_[1].matched;
        if(phase==NS_DELETE_RELEASE)return sides_[0].done&&sides_[1].done&&correlate(0)&&correlate(1);
        if(phase==NS_DATA_CHECK_RELEASE)return sides_[0].ack&&sides_[1].ack&&correlate(0)&&correlate(1);
        return false;
    }
public:
    Window(NS_Envelope a,NS_Envelope b,Clock::time_point end,bool host_fixture):ids_{{a,b}},end_(end),fixture_(host_fixture){
        if(!valid(a)||!valid(b)||a.side!=0||b.side!=1||a.run!=b.run||a.domain!=b.domain||std::strcmp(a.name_space,b.name_space))fail("ENDPOINT_BINDING");
    }
    bool emit(const NS_RunEvent& e){
        std::lock_guard<std::mutex> l(gate_);
        if(cancelled_)return false;if(Clock::now()>=end_)return fail("DEADLINE");
        if(!event_header(e)||!valid(e.envelope)||!same(e.envelope,ids_[e.envelope.side]))return fail("ENVELOPE");
        auto& s=sides_[e.envelope.side];
        if(e.kind==NS_ROOT_LISTENER){if(s.root||e.socket<0||e.listen_rc||e.flags!=static_cast<uint32_t>(fixture_?NS_HOST_FIXTURE_LISTEN:NS_REAL_ROOT_LISTEN_PUBLISHED))return fail("ROOT_WITNESS");s.root=true;}
        else if(e.kind==NS_MATCHED){if(!released(NS_CONNECT_RELEASE)||s.matched||e.writer_matches!=1||e.reader_matches!=1||!nonzero(e.writer_guid,16))return fail("MATCH_ORDER");s.matched=true;std::memcpy(s.guid,e.writer_guid,16);}
        else if(e.kind==NS_WRITE||e.kind==NS_TAKE){
            if(!released(NS_SEND_RELEASE)||s.done||e.ordinal<1||e.ordinal>3||e.sequence<=0||!nonzero(e.writer_guid,16))return fail("SAMPLE_ORDER");
            const unsigned producer=e.kind==NS_WRITE?e.envelope.side:1-e.envelope.side;
            const auto p=payload(e.envelope.run,producer,e.ordinal);
            if(std::memcmp(p.data(),e.payload,p.size())||std::memcmp(sides_[producer].guid,e.writer_guid,16))return fail("SAMPLE_IDENTITY");
            auto& rows=e.kind==NS_WRITE?s.writes:s.takes;
            for(const auto& row:rows)if(row.present&&row.value.sequence==e.sequence)return fail("DUPLICATE_SEQUENCE");
            auto& row=rows[e.ordinal-1];if(row.present)return fail("DUPLICATE_ORDINAL");row={true,e};
        }else if(e.kind==NS_ACK){if(!all(s.writes)||!all(s.takes)||s.ack||s.done)return fail("ACK_ORDER");s.ack=true;}
        else if(e.kind==NS_DATA_DONE){if(!s.ack||s.done||!all(s.writes)||!all(s.takes)||!correlate(0)||!correlate(1))return fail("DATA_CORRELATION");s.done=true;}
        else if(e.kind==NS_TEARDOWN){if(!released(NS_DELETE_RELEASE)||s.torn)return fail("EARLY_DELETE");s.torn=true;}
        else return fail("UNKNOWN_EVENT");
        cv_.notify_all();return true;
    }
    bool can_release(uint32_t p){std::lock_guard<std::mutex> l(gate_);return !cancelled_&&Clock::now()<end_&&released(p);}
    bool wait(uint32_t p){std::unique_lock<std::mutex> l(gate_);if(p<1||p>4)return fail("UNKNOWN_RELEASE");if(!cv_.wait_until(l,end_,[&]{return cancelled_||released(p);}))return fail("DEADLINE");return !cancelled_&&Clock::now()<end_;}
    void cancel(){std::lock_guard<std::mutex> l(gate_);fail("CANCELLED");}
    const char* error()const{std::lock_guard<std::mutex> l(gate_);return error_;}
};
}
