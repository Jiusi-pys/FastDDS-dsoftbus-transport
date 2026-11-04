#include "../../integrations/ros2/RepeatedProcessRounds.hpp"
#include <iostream>
#include <stdexcept>
using namespace fastdds_dsoftbus::ros2_integration;
static unsigned checks=0;
static void Check(bool v){++checks;if(!v)throw std::runtime_error("round ledger assertion");}
template<class F>void Reject(F f){bool rejected=false;try{f();}catch(const std::runtime_error&){rejected=true;}Check(rejected);}
static fastdds_dsoftbus::dsoftbus::phase0::AdapterObservation SettledView(const ProcessRestartObservation& old){
    using namespace fastdds_dsoftbus::dsoftbus;using namespace phase0;
    AdapterObservation v;v.phase=AdapterPhase::Published;v.outgoing_stopped=true;
    v.listener.socket=10;v.listener.owner_incarnation=100;
    v.retired_outgoing.owned_id=old.dial;v.retired_outgoing.incarnation=old.dial_owner;
    v.retired_accepted.owned_id=old.accepted;v.retired_accepted.incarnation=old.accepted_owner;
    v.retired_outgoing.close_intent=v.retired_accepted.close_intent=true;
    v.retired_outgoing.on_shutdown_seen=v.retired_accepted.on_shutdown_seen=true;
    return v;
}
int main(){try{
    const ProcessRestartObservation initial{11,12,101,102}, first{21,22,201,202}, second{31,32,301,302};
    RepeatedProcessRounds r;
    Check(!r.Consume(1));Reject([&]{r.Original();});Reject([&]{r.Arm(2,initial,10,100);});
    r.Arm(1,initial,10,100);
    auto view=SettledView(initial);Check(r.CanReplaceOutgoing(view));
    view.accepted_phase=fastdds_dsoftbus::dsoftbus::phase0::AcceptedPhase::Active;
    view.accepted.socket=first.accepted;view.accepted.owner_incarnation=first.accepted_owner;
    Check(r.CanReplaceOutgoing(view));
    {auto bad=view;bad.accepted.socket=initial.dial;Check(!r.CanReplaceOutgoing(bad));}
    {auto bad=view;bad.retired_outgoing.callback_inflight=1;Check(!r.CanReplaceOutgoing(bad));}
    {auto bad=view;bad.retired_accepted.receive_inflight=1;Check(!r.CanReplaceOutgoing(bad));}
    {auto bad=view;bad.retired_accepted.entered_context_lost=true;Check(!r.CanReplaceOutgoing(bad));}
    {auto bad=view;bad.phase=fastdds_dsoftbus::dsoftbus::phase0::AdapterPhase::Closing;Check(!r.CanReplaceOutgoing(bad));}
    {auto bad=view;bad.provider_stopped=true;Check(!r.CanReplaceOutgoing(bad));}
    {auto bad=view;bad.listener.socket=9;Check(!r.CanReplaceOutgoing(bad));}
    {auto bad=view;bad.listener.owner_incarnation=99;Check(!r.CanReplaceOutgoing(bad));}
    Reject([&]{r.Commit(first);});Reject([&]{r.Arm(1,initial,10,100);});
    Reject([&]{r.Arm(2,first,10,100);});Check(!r.Consume(2));Check(r.Consume(1));Check(!r.Consume(1));
    Check(!r.CanReplaceOutgoing(view));
    for(auto bad:{ProcessRestartObservation{11,22,201,202},ProcessRestartObservation{21,12,201,202},
            ProcessRestartObservation{21,22,101,202},ProcessRestartObservation{21,22,201,102},
            ProcessRestartObservation{10,22,201,202},ProcessRestartObservation{21,21,201,202},
            ProcessRestartObservation{21,22,100,202},ProcessRestartObservation{21,22,201,201}})
        Reject([&]{r.Commit(bad);});
    r.Commit(first);Check(r.Active());Check(!r.Consume(1));Reject([&]{r.Commit(first);});
    Reject([&]{r.Arm(2,first,9,100);});Reject([&]{r.Arm(2,first,10,99);});
    Reject([&]{r.Arm(2,initial,10,100);});r.Arm(2,first,10,100);
    view=SettledView(first);Check(r.CanReplaceOutgoing(view));
    view.accepted_phase=fastdds_dsoftbus::dsoftbus::phase0::AcceptedPhase::Active;
    view.accepted.socket=second.accepted;view.accepted.owner_incarnation=second.accepted_owner;
    Check(r.CanReplaceOutgoing(view));
    for(auto old:{initial,first}){
        auto bad=view;bad.accepted.socket=old.accepted;Check(!r.CanReplaceOutgoing(bad));
        bad=view;bad.accepted.owner_incarnation=old.dial_owner;Check(!r.CanReplaceOutgoing(bad));
    }
    Check(!r.Consume(1));Check(r.Consume(2));Check(!r.Consume(2));
    for(auto old:{initial,first}){
        auto bad=second;bad.dial=old.dial;Reject([&]{r.Commit(bad);});
        bad=second;bad.accepted=old.accepted;Reject([&]{r.Commit(bad);});
        bad=second;bad.dial_owner=old.accepted_owner;Reject([&]{r.Commit(bad);});
    }
    r.Commit(second);Check(r.Active()&&r.Round()==2&&r.Consumed());
    Reject([&]{r.Arm(3,second,10,100);});Check(!r.Consume(2));Check(!r.Consume(3));
    std::cout<<"PASS HOST_PURE_LEDGER checks="<<checks<<" SDK_NOT_EXERCISED\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
