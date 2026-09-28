#ifndef MDDS_SHARED_SEND_CONTROL_HPP
#define MDDS_SHARED_SEND_CONTROL_HPP
#include <mdds/dsoftbus/HostProviderLoop.hpp>
#include <array>
#include <limits>
#include <mutex>
namespace mdds::dsoftbus::shared_probe {
// Control evidence only: no resource references, routing or cancellation powers.
struct Retirement {
    ReceiverIdentity receiver{};
    unsigned cycle=0,life=0,group=0;
    bool final=false;
};
class SendControl {
#ifdef MDDS_HOST_BACKEND_TESTING
    friend struct SendControlTestAccess;
#endif
    struct Slot { Retirement value;bool used=false,complete=false;std::uint64_t cutoff=0; };
    std::mutex gate_;
    std::array<Slot,2> slots_{};
    std::uint64_t started_=0,finished_=0;
    bool good_=true;
    void Reclaim(){for(auto& s:slots_)if(s.used&&s.complete&&finished_>=s.cutoff)s={};}
public:
    std::uint64_t Begin(){std::lock_guard<std::mutex> lock(gate_);Reclaim();
        if(!good_||started_!=finished_||started_==std::numeric_limits<std::uint64_t>::max()){good_=false;return 0;}return ++started_;}
    bool Finish(std::uint64_t sequence){std::lock_guard<std::mutex> lock(gate_);
        if(!good_||!sequence||sequence!=started_||finished_+1!=sequence){good_=false;return false;}
        finished_=sequence;Reclaim();return true;}
    bool Register(const Retirement& value){std::lock_guard<std::mutex> lock(gate_);Reclaim();
        if(!good_||value.receiver==ReceiverIdentity{}||!value.cycle||!value.life||value.group>1||value.group==1&&!value.final){good_=false;return false;}
        for(auto& s:slots_)if(s.used&&s.value.receiver==value.receiver){
            // A failed public deletion may be retried in cleanup, but cannot
            // change its generation or recycle a pending classification.
            if(!s.complete||s.value.cycle!=value.cycle||s.value.life!=value.life||s.value.group!=value.group){good_=false;return false;}
            s.complete=false;return true;}
        for(auto& s:slots_)if(!s.used){s.value=value;s.used=true;return true;}good_=false;return false;}
    bool Complete(const ReceiverIdentity& identity){std::lock_guard<std::mutex> lock(gate_);
        for(auto& s:slots_)if(good_&&s.used&&!s.complete&&s.value.receiver==identity){s.complete=true;s.cutoff=started_;Reclaim();return true;}good_=false;return false;}
    bool Find(std::uint64_t sequence,const ReceiverIdentity& identity,Retirement& value){std::lock_guard<std::mutex> lock(gate_);
        if(!good_||sequence!=started_||sequence<=finished_)return false;
        for(const auto& s:slots_)if(s.used&&s.value.receiver==identity){value=s.value;return true;}return false;}
    bool Good(){std::lock_guard<std::mutex> lock(gate_);return good_;}
};
struct RetirementCompletion {
    SendControl& control;ReceiverIdentity receiver;
    ~RetirementCompletion(){control.Complete(receiver);}
};
inline bool RetiredBeforeEntry(const NativeSendRunResult& result,const host_test::HostSendObservation& selected,
        const host_test::HostLoopObservation& runtime,const Retirement& retirement) {
    return result.status==NativeStatus::Revoked&&result.outcome==NativeSendOutcome::NotEntered&&!result.actual_return_code&&
        selected.selected&&!(selected.receiver==ReceiverIdentity{})&&selected.receiver==retirement.receiver&&
        selected.view_closing&&selected.registration_status==NativeStatus::None&&selected.admission_closed&&
        retirement.cycle&&retirement.life&&retirement.group<=1&&(retirement.group==0||retirement.final)&&
        runtime.shared_runtime&&runtime.started&&runtime.live_views&&!runtime.sealed&&!runtime.closed&&
        runtime.adapter.phase==phase0::AdapterPhase::Published&&!runtime.adapter.outgoing_stopped&&
        runtime.adapter.binding.setup_complete&&!runtime.adapter.binding.owner_closed;
}
}
#endif
