#pragma once
#include "pair_record.hpp"
#include "../run_protocol.hpp"
#include "../control/wire.hpp"
#include <deque>
#include <algorithm>
#include <memory>
#include <utility>
namespace stage1_goal {
// Evidence/release coordinator only. No DDS payload transport, token API,
// device operation or authentication of a producer is implemented by this type.
class PairCoordinator {
    struct Side {std::vector<uint8_t> prefix;std::deque<stage1_control::Frame> pending;uint32_t next=1,events=0;bool torn=false;};
    std::array<NS_Envelope,2> ids_;std::array<Side,2> sides_;
    stage1_run::Window window_;stage1_run::Clock::time_point end_;
    unsigned phase_=0;bool waiting_delivery_=false,failed_=false;
    const char* error_=nullptr;
    static constexpr uint32_t releases_[4]={NS_CONNECT_RELEASE,NS_SEND_RELEASE,NS_DATA_CHECK_RELEASE,NS_DELETE_RELEASE};
    bool fail(const char* why){if(!failed_)error_=why;failed_=true;window_.cancel();return false;}
    bool current(){return !failed_&&(stage1_run::Clock::now()<end_||fail("CONTROLLER_DEADLINE"));}
    bool allowed(uint32_t k)const{return phase_==0?k==NS_ROOT_LISTENER:phase_==1?k==NS_MATCHED:phase_==2?(k==NS_WRITE||k==NS_TAKE||k==NS_ACK):phase_==3?k==NS_DATA_DONE:phase_==4?k==NS_TEARDOWN:false;}
    bool drain(){
        if(!current())return false;
        for(auto& s:sides_)while(!s.pending.empty()){
            if(waiting_delivery_||!allowed(s.pending.front().event.kind))return fail("EVENT_BEFORE_RELEASE_OR_ORDER");
            const auto e=s.pending.front().event;
            if(!window_.emit(e))return fail(window_.error()?window_.error():"WINDOW_REJECTED");
            s.pending.pop_front();++s.events;if(e.kind==NS_TEARDOWN)s.torn=true;
        }
        if(phase_<4&&window_.can_release(releases_[phase_]))waiting_delivery_=true;
        if(phase_==4&&(sides_[0].events>11||sides_[1].events>11))return fail("EXTRA_EVENT");
        return true;
    }
public:
    PairCoordinator(NS_Envelope a,NS_Envelope b,stage1_run::Clock::time_point end,bool marked_fixture=false):ids_{{a,b}},window_(a,b,end,marked_fixture),end_(end){if(window_.error())fail(window_.error());}
    // Each input is the whole atomically published cumulative snapshot. A rename
    // changes inode normally; the transport must validate its own source/paths.
    bool ingest(unsigned side,const std::vector<uint8_t>& bytes){
        if(!current()||side>1)return fail("CONTROLLER_SIDE");auto& s=sides_[side];
        if(bytes.size()>stage1_control::byte_limit||bytes.size()<s.prefix.size()||!std::equal(s.prefix.begin(),s.prefix.end(),bytes.begin()))return fail("JOURNAL_PREFIX_OR_BOUND");
        size_t at=s.prefix.size();while(at<bytes.size()){
            if(bytes.size()-at<stage1_control::header_bytes)return fail("JOURNAL_PARTIAL_HEADER");uint32_t n=0;
            if(stage1_control::header_size(bytes.data()+at,stage1_control::header_bytes,n)!=stage1_control::Error::None||n>bytes.size()-at-stage1_control::header_bytes)return fail("JOURNAL_PARTIAL_FRAME");
            stage1_control::Frame f;const size_t total=stage1_control::header_bytes+n;
            if(stage1_control::decode(bytes.data()+at,total,f)!=stage1_control::Error::None||f.kind!=stage1_control::Kind::Event||f.direction!=stage1_control::Direction::Endpoint||f.sequence!=s.next||f.nonce!=ids_[side].run||!stage1_control::same_envelope(f.envelope,ids_[side]))return fail("JOURNAL_IDENTITY_SEQUENCE");
            ++s.next;if(s.next>stage1_control::frame_count_limit+1)return fail("JOURNAL_FRAME_BOUND");s.pending.push_back(f);at+=total;
        }
        s.prefix=bytes;return drain();
    }
    bool has_release(){return current()&&waiting_delivery_;}
    unsigned delivery_sequence()const{return phase_+1;}
    std::array<std::vector<uint8_t>,2> release_frames(){
        if(!has_release())throw std::logic_error("NO_RELEASE_READY");std::array<std::vector<uint8_t>,2> out;
        for(unsigned s=0;s<2;++s){stage1_control::Frame f;f.kind=stage1_control::Kind::Release;f.direction=stage1_control::Direction::Coordinator;f.sequence=phase_+1;f.nonce=ids_[s].run;f.envelope=ids_[s];f.release=releases_[phase_];out[s]=stage1_control::encode(f);}return out;
    }
    // Caller must have validated BOTH exact release-file deliveries. This is an
    // ordering receipt, not OS permission/human consent/SDK-ready attestation.
    bool confirm_both_delivered(unsigned sequence){if(!has_release()||sequence!=phase_+1)return fail("DELIVERY_RECEIPT");waiting_delivery_=false;++phase_;return drain();}
    void cancel(){fail("CONTROLLER_CANCELLED");}
    bool complete(){return current()&&phase_==4&&sides_[0].torn&&sides_[1].torn&&sides_[0].events==11&&sides_[1].events==11;}
    const char* error()const{return error_;}
};
}
