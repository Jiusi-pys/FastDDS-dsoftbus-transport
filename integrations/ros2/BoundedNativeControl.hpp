#pragma once
// Finite run/phase coordinator over the existing owner/control path. No SDK
// symbols, owner-table mutations, payload transport or extra threads live here.
#include "BridgeOwner.hpp"
#include "target/NativeObservation.hpp"
#include <fastdds_dsoftbus/dsoftbus/RuntimeProbeAccess.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstdio>
#include <stdexcept>
#if !defined(_WIN32)
#include <sys/time.h>
#endif

namespace fastdds_dsoftbus::ros2_integration {
class BoundedNativeControl final {
    using Role=dsoftbus::RecordRole;
    using Probe=dsoftbus::phase0::RuntimeProbeAccess;
    BridgeOwner& owner;
    const unsigned domain;
    const bool outgoing_first,host_model,route_model;
    const std::string path;
    bool armed=false,requested=false,ended=false,settled=false,ready=false,published=false;
    int old_id=-1,listener=-1;
    std::size_t client_entry=0,listener_entry=0;
    std::uint64_t old_owner=0,sequence=0,last_wall=0;
    std::uint64_t initial_shutdown_calls=0,initial_shutdown_returns=0;
    std::uint64_t initial_negotiations=0;
    std::mutex clock_gate;
    std::chrono::steady_clock::time_point next_poll{};
    std::atomic<bool> refused{false};
    static std::uint64_t WallTime(){
#if defined(_WIN32)
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(ms<=0)throw std::runtime_error("wall clock failure");return static_cast<std::uint64_t>(ms);
#else
        timeval value{};if(gettimeofday(&value,nullptr)!=0||value.tv_sec<=0)throw std::runtime_error("wall clock failure");
        return static_cast<std::uint64_t>(value.tv_sec)*1000+value.tv_usec/1000;
#endif
    }
    bool Control(const char* phase)const {
        std::ifstream input(path+"."+phase,std::ios::binary);if(!input)return false;
        char bytes[256]{};if(!input.getline(bytes,sizeof(bytes))||input.peek()!=std::char_traits<char>::eof())throw std::runtime_error("bounded control shape");
        std::string row(bytes);if(!row.empty()&&row.back()=='\r')row.pop_back();
        if(row!="NATIVE_CONTROL run="+owner.run+" side="+(owner.side?"B":"A")+" phase="+phase)throw std::runtime_error("bounded control identity");
        return true;
    }
    template<class Guid>static std::string Hex(const Guid& guid){
        std::ostringstream text;text<<std::hex<<std::setfill('0');
        for(auto b:guid.guidPrefix.value)text<<std::setw(2)<<unsigned(b);
        for(auto b:guid.entityId.value)text<<std::setw(2)<<unsigned(b);
        return text.str();
    }
    void Endpoints(const char* point){
        // Metadata comes from SDK_SEND RTPS bytes. Never dereference a DDS
        // object concurrently with main-thread teardown.
        ros2_target_observation::V1LocatorSnapshot locators;
        if(!ros2_target_observation::CaptureV1(locators))throw std::runtime_error("participant wire identity unavailable");
        std::array<std::uint8_t,16> participant{};bool found=false;
        for(std::size_t i=0;i<locators.advert_count;++i)if(locators.adverts[i].sending){
            const auto& writer=locators.adverts[i].value.writer;
            if(!found){std::copy_n(writer.begin(),12,participant.begin());participant[14]=1;participant[15]=0xc1;found=true;}
            else if(!std::equal(writer.begin(),writer.begin()+12,participant.begin()))throw std::runtime_error("multiple Participant wire identities");
        }
        if(!found)throw std::runtime_error("local SPDP identity unavailable");
        char guid[33]{};for(unsigned i=0;i<16;++i)std::snprintf(guid+2*i,3,"%02x",participant[i]);
        std::fprintf(stdout,"NATIVE_ENTITY_ANCHOR run=%s side=%c point=%s runtime=%llu participant=WIRE_GUID guid=%s identity_source=SDK_SEND_SPDP_PREFIX\n",owner.run.c_str(),owner.side?'B':'A',point,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(owner.runtime.get())),guid);
        ros2_target_observation::EndpointSnapshot endpoints;
        if(!ros2_target_observation::CaptureEndpoints(endpoints)||!endpoints.complete||!endpoints.count)throw std::runtime_error("wire endpoint identity incomplete");
        for(std::size_t i=0;i<endpoints.count;++i){const auto& value=endpoints.values[i];char guid[33]{};for(unsigned j=0;j<16;++j)std::snprintf(guid+2*j,3,"%02x",value.guid[j]);
            std::fprintf(stdout,"NATIVE_ENDPOINT_ANCHOR run=%s side=%c point=%s role=%s guid=%s topic=%s identity_source=SDK_SEND_SEDP\n",owner.run.c_str(),owner.side?'B':'A',point,value.reader?"reader":"writer",guid,value.topic.data());}
        std::fflush(stdout);
    }
    void Emit(const char* stage,int fresh=-1,std::uint64_t incarnation=0,const dsoftbus::NativeOwnerObservation& old={}){
        const auto sdk=owner.runtime->ObserveSocket();
        const auto& close=sdk.operations[std::size_t(dsoftbus::phase0::PublicSocketOperation::Shutdown)];
        std::fprintf(stdout,"NATIVE_REACCEPT_EVENT schema=1 run=%s side=%c seq=%llu stage=%s source=%s listener=%d listener_entry=%zu client_entry=%zu old_id=%d new_id=%d old_owner=%llu new_owner=%llu shutdown_entered=%d shutdown_returned=%d on_shutdown_seen=%d callbacks=%zu receivers=%zu sdk_shutdown_calls=%llu sdk_shutdown_returns=%llu wall_ms=%llu sdk_epoch=NOT_PROVEN sdk_drain=NOT_PROVEN outgoing_id=%d accepted_id=%d\n",
            owner.run.c_str(),owner.side?'B':'A',static_cast<unsigned long long>(++sequence),stage,host_model?"HOST_OPAQUE_PUBLIC_API_SHAPE":"SDK_PUBLIC_SOCKET",
            listener,listener_entry,client_entry,old_id,fresh,static_cast<unsigned long long>(old_owner),static_cast<unsigned long long>(incarnation),int(old.shutdown_entered),int(old.shutdown_returned),int(old.on_shutdown_seen),old.callback_inflight,old.receive_inflight,
            static_cast<unsigned long long>(close.calls-initial_shutdown_calls),static_cast<unsigned long long>(close.returns-initial_shutdown_returns),static_cast<unsigned long long>(last_wall),sdk.outgoing_socket.value_or(-1),sdk.accepted_socket.value_or(-1));std::fflush(stdout);
    }
public:
    BoundedNativeControl(BridgeOwner& value,unsigned d,bool first,std::string control,bool model=false,bool routes=false)
        :owner(value),domain(d),outgoing_first(first),host_model(model),route_model(routes),path(std::move(control)){}
    void CheckClock(){std::lock_guard<std::mutex> lock(clock_gate);const auto wall=WallTime();if(last_wall&&wall<last_wall)throw std::runtime_error("wall clock rollback");last_wall=wall;}
    bool PermitReplacement(){
        if(refused.load()||!armed)return false;
        try {CheckClock();return Control("REPLACE");}catch(...){Refuse();throw;}
    }
    const char* FailureStatus()const {
        const auto sdk=owner.runtime->ObserveSocket();const auto value=Probe::ObserveConnection(*owner.runtime);
        return published||sdk.operations[std::size_t(dsoftbus::phase0::PublicSocketOperation::Socket)].calls>2||value.adapter.accepted_activation_count>1||
            (armed&&value.adapter.accepted_id&&*value.adapter.accepted_id!=old_id)?"REACCEPT_ATTEMPT_FAILED":"REACCEPT_NOT_EXERCISED";
    }
    void Refuse() noexcept {
        if(refused.exchange(true))return;
        Probe::SealBoundedAdmission(*owner.runtime);owner.failed=true;
        std::fprintf(stdout,"NATIVE_REACCEPT_REFUSAL run=%s side=%c reason=ADMISSION_OR_CONTROL_REFUSED status=%s\n",owner.run.c_str(),owner.side?'B':'A',FailureStatus());std::fflush(stdout);
    }
    bool Step(){
        if(refused.load())return true;
        const auto now=std::chrono::steady_clock::now();if(now<next_poll)return true;next_poll=now+std::chrono::milliseconds(2);
        CheckClock();
        auto sdk=owner.runtime->ObserveSocket();auto view=Probe::ObserveConnection(*owner.runtime);
        if(sdk.fake_sdk!=host_model)throw std::runtime_error("native/model source mismatch");
        const auto& lane=owner.side?view.adapter.accepted:view.adapter.binding;
        if(!armed&&Control("ARM")){
            if(!sdk.listener_socket||!sdk.outgoing_socket||!sdk.accepted_socket||lane.socket<0||!lane.owner_incarnation||view.adapter.accepted_phase!=dsoftbus::phase0::AcceptedPhase::Active)throw std::runtime_error("original connection anchor incomplete");
            listener=*sdk.listener_socket;client_entry=sdk.entry_index;listener_entry=sdk.listener_entry_index;old_id=lane.socket;old_owner=lane.owner_incarnation;
            initial_negotiations=sdk.negotiation_callbacks;
            const auto& close=sdk.operations[std::size_t(dsoftbus::phase0::PublicSocketOperation::Shutdown)];initial_shutdown_calls=close.calls;initial_shutdown_returns=close.returns;
            Endpoints("BEFORE");armed=true;Emit("ARMED");
        }
        if(!armed)return true;
        if(!sdk.listener_socket||*sdk.listener_socket!=listener||sdk.listener_stopped||sdk.listener_entry_index!=listener_entry||sdk.entry_index!=client_entry)throw std::runtime_error("listener/entry continuity failed");
        const bool origin=outgoing_first?owner.side==0:owner.side==1;
        if(origin&&!ended&&Control("END")){
            if(!requested){requested=true;Emit("ADMISSION_STOP_REQUESTED");}
            const auto result=Probe::RequestNormalEnd(*owner.runtime,owner.side?Role::Server:Role::Client);
            if(result!=dsoftbus::phase0::BackendStatus::None&&result!=dsoftbus::phase0::BackendStatus::NoWork)throw std::runtime_error("original controlled close refused");
            if(result==dsoftbus::phase0::BackendStatus::None){
                sdk=owner.runtime->ObserveSocket();const auto& close=sdk.operations[std::size_t(dsoftbus::phase0::PublicSocketOperation::Shutdown)];
                if(close.calls-initial_shutdown_calls!=1||close.returns-initial_shutdown_returns!=1)throw std::runtime_error("local close has no unique actual SDK entry/void return");
                ended=true;Emit("LOCAL_CLOSE_RETURNED");
            }
        }
        const auto& old=owner.side?view.adapter.retired_accepted:view.adapter.retired_outgoing;
        if(!settled&&old.incarnation==old_owner&&old.owned_id==old_id&&old.close_intent&&(old.shutdown_returned||old.on_shutdown_seen)&&!old.callback_inflight&&!old.receive_inflight&&!old.entered_context_lost){settled=true;Emit("OLD_OWNER_SETTLED",-1,0,old);}
        if((owner.side||route_model)&&settled&&!ready&&view.adapter.accepted_phase==dsoftbus::phase0::AcceptedPhase::Empty){ready=true;Emit("PEER_READY",-1,0,old);}
        if(settled&&!published&&lane.socket>=0&&lane.socket!=old_id&&lane.owner_incarnation&&lane.owner_incarnation!=old_owner&&
            (owner.side?view.adapter.accepted_phase==dsoftbus::phase0::AcceptedPhase::Active:!view.adapter.outgoing_stopped)){
            Endpoints("AFTER");published=true;Emit("OWNER_PUBLISHED",lane.socket,lane.owner_incarnation,old);
        }
        if(view.adapter.provider_status!=dsoftbus::phase0::BackendStatus::None||view.adapter.accepted_phase==dsoftbus::phase0::AcceptedPhase::Failed)throw std::runtime_error("native reaccept admission failed");
        return true;
    }
};
}
