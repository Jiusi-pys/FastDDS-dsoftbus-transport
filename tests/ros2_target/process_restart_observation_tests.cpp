#include "../../integrations/ros2/ProcessRestartObservation.hpp"
#include <iostream>
#include <stdexcept>
using namespace fastdds_dsoftbus;
using namespace dsoftbus;
using namespace dsoftbus::phase0;
using ros2_integration::ProcessRestartObservation;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static NativeOwnerObservation Retired(int id,std::uint64_t incarnation){
    NativeOwnerObservation out;out.status=NativeStatus::None;out.owned_id=id;
    out.incarnation=incarnation;out.close_intent=true;out.on_shutdown_seen=true;return out;
}
int main(){try{
    const ProcessRestartObservation original{11,12,101,102};
    AdapterObservation ready;ready.retired_outgoing=Retired(11,101);ready.retired_accepted=Retired(12,102);
    Check(original.OldBothReady(ready),"normal dual remote shutdown permits observation");
    Check(!original.NewBothActive(ready),"empty accepted is not replacement success");
    auto active=ready;active.accepted_phase=AcceptedPhase::Active;
    active.binding.socket=21;active.binding.owner_incarnation=201;
    active.accepted.socket=22;active.accepted.owner_incarnation=202;
    Check(original.NewBothActive(active),"two fresh active lanes qualify");
    Check(!original.OldBothReady(active),"active accepted must not release new peer");
    unsigned negative=0;
    // Every old-lane hazard must withhold readiness AND replacement evidence.
    for(bool accepted:{false,true})for(unsigned hazard=0;hazard<9;++hazard){
        auto bad_ready=ready,bad_active=active;
        for(auto* sample:{&bad_ready,&bad_active}){
            auto& old=accepted?sample->retired_accepted:sample->retired_outgoing;
            switch(hazard){
                case 0:old.incarnation++;break;
                case 1:old.owned_id=33;break;
                case 2:old.close_intent=false;break;
                case 3:old.on_shutdown_seen=false;break;
                case 4:old.callback_inflight=1;break;
                case 5:old.receive_inflight=1;break;
                case 6:old.entered_context_lost=true;break;
                case 7:old.failure=NativeStatus::ProviderContractFault;break;
                case 8:old.provider_fault=NativeProviderFault::ExceptionUnwind;break;
            }
        }
        Check(!original.OldBothReady(bad_ready),"unsafe old owner emitted readiness");
        Check(!original.NewBothActive(bad_active),"unsafe old owner emitted replacement");++negative;
    }
    for(unsigned hazard=0;hazard<10;++hazard){
        auto bad=active;
        switch(hazard){
            case 0:bad.binding.socket=11;break;
            case 1:bad.binding.socket=12;break;
            case 2:bad.accepted.socket=11;break;
            case 3:bad.accepted.socket=12;break;
            case 4:bad.accepted.socket=bad.binding.socket;break;
            case 5:bad.binding.owner_incarnation=102;break;
            case 6:bad.accepted.owner_incarnation=101;break;
            case 7:bad.accepted.owner_incarnation=bad.binding.owner_incarnation;break;
            case 8:bad.outgoing_stopped=true;break;
            case 9:bad.accepted_phase=AcceptedPhase::Preparing;break;
        }
        Check(!original.NewBothActive(bad),"old/colliding/unfinished replacement accepted");++negative;
    }
    for(auto* sample:{&ready,&active}){
        auto bad=*sample;bad.listener_stopped=true;
        Check(!original.OldBothReady(bad)&&!original.NewBothActive(bad),"closed listener accepted");
        bad=*sample;bad.provider_status=BackendStatus::InvalidState;
        Check(!original.OldBothReady(bad)&&!original.NewBothActive(bad),"failed provider accepted");negative+=2;
    }
    auto returned=ready;returned.retired_outgoing.on_shutdown_seen=false;
    returned.retired_outgoing.shutdown_returned=true;
    Check(original.OldBothReady(returned),"local void Shutdown return observation supported without SDK drain claim");
    std::cout<<"PASS process observation: positive dual-lane boundaries and "<<negative<<" negative snapshots; model only, native_calls=0\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
