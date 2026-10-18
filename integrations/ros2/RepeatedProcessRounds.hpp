#pragma once
#include "ProcessRestartObservation.hpp"
#include <array>
#include <stdexcept>

namespace fastdds_dsoftbus::ros2_integration {
// Two-round observation/control ledger. The caller serializes all access.
// It neither retires native ownership nor grants callback epoch authority.
class RepeatedProcessRounds final {
    std::array<ProcessRestartObservation,3> history_{};
    unsigned round_=0;
    bool consumed_=false, active_=false;
    int listener_=-1;
    std::uint64_t listener_owner_=0;
    static bool Same(const ProcessRestartObservation& a,const ProcessRestartObservation& b) {
        return a.dial==b.dial&&a.accepted==b.accepted&&a.dial_owner==b.dial_owner&&a.accepted_owner==b.accepted_owner;
    }
    void Fresh(const ProcessRestartObservation& value,unsigned prior) const {
        if(value.dial<0||value.accepted<0||value.dial==value.accepted||value.dial==listener_||value.accepted==listener_||
                !value.dial_owner||!value.accepted_owner||value.dial_owner==value.accepted_owner||
                value.dial_owner==listener_owner_||value.accepted_owner==listener_owner_)throw std::runtime_error("invalid repeated lane identity");
        for(unsigned i=0;i<prior;++i){const auto& old=history_[i];
            for(auto id:{value.dial,value.accepted})if(id==old.dial||id==old.accepted)throw std::runtime_error("historical repeated socket ID");
            for(auto id:{value.dial_owner,value.accepted_owner})if(id==old.dial_owner||id==old.accepted_owner)throw std::runtime_error("historical repeated owner");
        }
    }
public:
    unsigned Round() const noexcept{return round_;}
    bool Active() const noexcept{return active_;}
    bool Consumed() const noexcept{return consumed_;}
    const ProcessRestartObservation& Original()const {
        if(!round_)throw std::runtime_error("unarmed repeated round");return history_[round_-1];
    }
    bool CanReplaceOutgoing(const dsoftbus::phase0::AdapterObservation& view)const noexcept {
        using namespace dsoftbus::phase0;
        if(!round_||consumed_||active_||view.phase!=AdapterPhase::Published||view.provider_stopped||
                view.provider_status!=BackendStatus::None||view.listener_stopped||!view.outgoing_stopped||
                view.listener.socket!=listener_||view.listener.owner_incarnation!=listener_owner_)return false;
        const auto& old=history_[round_-1];
        if(!ProcessRestartObservation::Settled(view.retired_outgoing,old.dial,old.dial_owner)||
                !ProcessRestartObservation::Settled(view.retired_accepted,old.accepted,old.accepted_owner))return false;
        // OLD_BOTH_READY must already have been observed by the coordinator.
        // A peer can publish its fresh accepted lane before our next pump; that
        // does not invalidate the recorded retirement of the previous lane.
        if(view.accepted_phase==AcceptedPhase::Empty)return true;
        const auto id=view.accepted.socket;const auto owner=view.accepted.owner_incarnation;
        if(view.accepted_phase!=AcceptedPhase::Active||id<0||!owner||id==listener_||owner==listener_owner_)return false;
        for(unsigned i=0;i<round_;++i){const auto& prior=history_[i];
            if(id==prior.dial||id==prior.accepted||owner==prior.dial_owner||owner==prior.accepted_owner)return false;
        }
        return true;
    }
    void Arm(unsigned next,const ProcessRestartObservation& current,int listener,std::uint64_t listener_owner) {
        if(next<1||next>2||next!=round_+1||(round_&&!active_)||listener<0||!listener_owner)
            throw std::runtime_error("repeated round order");
        if(!round_){listener_=listener;listener_owner_=listener_owner;Fresh(current,0);history_[0]=current;}
        else if(listener!=listener_||listener_owner!=listener_owner_||!Same(current,history_[round_]))
            throw std::runtime_error("repeated anchor changed");
        round_=next;consumed_=false;active_=false;
    }
    bool Consume(unsigned round) {
        if(!round_||round!=round_||consumed_||active_)return false;
        consumed_=true;return true; // closes synchronously, even if native work later fails
    }
    void Commit(const ProcessRestartObservation& current) {
        if(!round_||!consumed_||active_)throw std::runtime_error("unpermitted repeated activation");
        Fresh(current,round_);history_[round_]=current;active_=true;
    }
};
}
