#include "PublicSocketRuntimeProbe.hpp"
#include "PublicSocketRuntimeTrace.hpp"
#include <mdds/dsoftbus/RuntimeProbeAccess.hpp>
#include <mdds/dsoftbus/PublicSocketBackend.hpp>
#include <mdds/dsoftbus/NativeSendExecution.hpp>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <stdexcept>

namespace {thread_local const std::function<bool(const char*)>* control_gate=nullptr;}
namespace mdds::dsoftbus::phase0 {
bool RuntimeProbeBeforeConnect() noexcept {try{return control_gate&&(*control_gate)("LISTENER_READY");}catch(...){return false;}}
}
namespace mdds::dsoftbus::runtime_probe {
using namespace phase0;using namespace std::chrono_literals;namespace rtps=eprosima::fastdds::rtps;
void ObserveTokenDiagnostic(std::string& first,const char* phase,std::uint64_t expected,std::uint64_t actual,int rc){
    const std::string name=phase;
    const bool failed=(name=="original_token"&&expected==0)||((name=="select_token"||name=="restore_token")&&(rc!=0||expected!=actual))||name=="initialization_or_record_exception";
    if(failed&&first.empty())first=name;
}
const char* RestorationLabel(unsigned set_calls,bool restored)noexcept{return set_calls==0?"NOT_NEEDED":restored?"PASS":"FAIL";}
bool ValidConfig(const ProbeConfig& c) noexcept {
    if(c.side<0||c.side>1||c.run.size()!=16||c.peer_network.empty()||c.peer_network.size()>64||c.package.empty()||c.package.size()>64)return false;
    for(char x:c.run)if(!((x>='0'&&x<='9')||(x>='a'&&x<='f')))return false;
    for(const auto* p:{&c.peer_network,&c.package,&c.service_prefix})for(char x:*p)if(!((x>='0'&&x<='9')||(x>='a'&&x<='z')||(x>='A'&&x<='Z')||x=='.'||x=='_'||x=='-'))return false;
    return c.service_prefix.size()<=64;
}
Record MakeRecord(const std::string& run,int side,unsigned sequence){
    if(run.size()!=16||side<0||side>1||sequence<1||sequence>5)throw std::invalid_argument("record identity");
    Record p{};for(unsigned i=0;i<p.size();++i)p[i]=static_cast<std::uint8_t>((i*37+sequence*11+side*71)%251);
    std::memcpy(p.data(),"MDP1",4);std::memcpy(p.data()+4,run.data(),16);p[20]=static_cast<std::uint8_t>(side);p[21]=static_cast<std::uint8_t>(sequence);return p;
}
bool RecordLedger::Accept(const void* data,std::size_t n) noexcept {
    std::lock_guard<std::mutex> lock(gate_);
    if(run_.size()!=16||side_<0||side_>1||!data||n!=256){++errors_;return false;}
    const auto* p=static_cast<const std::uint8_t*>(data);const unsigned sequence=p[21];
    if(std::memcmp(p,"MDP1",4)||std::memcmp(p+4,run_.data(),16)||p[20]!=1-side_||sequence<1||sequence>5){++errors_;return false;}
    const auto expected=MakeRecord(run_,1-side_,sequence);
    if(std::memcmp(p,expected.data(),256)||seen_[sequence-1]){++errors_;return false;}
    seen_[sequence-1]=true;++count_;return true;
}
unsigned RecordLedger::Count()const noexcept{std::lock_guard<std::mutex> lock(gate_);return count_;}
unsigned RecordLedger::Errors()const noexcept{std::lock_guard<std::mutex> lock(gate_);return errors_;}
int ProbeLoadCheck() noexcept {std::puts("PUBLIC_SOCKET_PROBE mode=load-check explicit_socket_calls=0 scope=MAIN_ONLY_NOT_DSO_CONSTRUCTOR_PROOF");return 0;}
namespace {
Locator Local(int side){EndpointId id{};id[0]=10;id[14]=127;id[15]=static_cast<std::uint8_t>(2+side);Locator l;if(!LocatorCodec::Encode(id,l))throw std::runtime_error("locator");return l;}
struct Receiver:InputChannels::Receiver {
    RecordLedger ledger;explicit Receiver(const ProbeConfig& c):ledger(c.run,c.side){}
    void OnDataReceived(const rtps::octet* p,std::uint32_t n,const Locator&,const Locator&)override {
        const bool ok=ledger.Accept(p,n);
        if(!ok)TraceEvent("RECEIVER_INVALID",0,0,0,0,n);
        std::printf("PROBE_RECEIVER valid=%d direction=%u sequence=%u length=%u\n",ok,p&&n==256?p[20]:99,p&&n==256?p[21]:0,n);std::fflush(stdout);
    }
};
void Snapshot(const char* stage,ProviderAdapter& adapter,PublicSocketBackend& backend){
    const auto a=adapter.Observe();const auto b=backend.Observe();
    std::printf("PROBE_OWNERS stage=%s outgoing=%d listener=%d accepted=%d accepted_phase=%d record=%llu owner=%llu admitted=%zu rejected=%zu sdk_identity=UNPROVEN\n",stage,b.outgoing_socket.value_or(-1),b.listener_socket.value_or(-1),b.accepted_socket.value_or(-1),int(a.accepted_phase),(unsigned long long)a.accepted.record_incarnation,(unsigned long long)a.accepted.owner_incarnation,a.accepted.admitted,a.accepted_rejected);
    for(std::size_t i=0;i<b.operations.size();++i){const auto&o=b.operations[i];std::printf("PROBE_CALL_TOTAL stage=%s operation=%zu attempts=%llu calls=%llu returns=%llu rc_present=%d rc=%d handle_present=%d handle=%d\n",stage,i,(unsigned long long)o.attempts,(unsigned long long)o.calls,(unsigned long long)o.returns,o.last_call&&o.last_call->provider_return.has_value(),o.last_call?o.last_call->provider_return.value_or(-999):-999,int(o.handle.has_value()),o.handle.value_or(-1));}
    std::fflush(stdout);
}
}
ProbeResult RunProbe(const ProbeConfig& c,const std::function<bool(const char*)>& gate,const std::function<void()>& pump) {
    ProbeResult result;if(!ValidConfig(c)){result.first_failure="INVALID_CONFIGURATION";return result;}
    Receiver receiver(c);std::shared_ptr<PublicSocketBackend> backend;
    std::unique_ptr<NativeOwnerTable> native;std::unique_ptr<ApplicationAttachments> attachments;
    std::unique_ptr<NativeSendExecution> execution;EgressRegistration registration;ProviderAdapterDomain domain;
    std::unique_ptr<ProviderAdapter> adapter;rtps::SendResourceList senders;
    std::array<Record,5> payloads;std::atomic<bool> stop{false};std::thread rx;
    auto fail=[&](const char* why){if(result.first_failure.empty())result.first_failure=why;};
    try {
        control_gate=&gate;const auto prefix=c.service_prefix.empty()?"com.kaihong.mdds.p"+c.run:c.service_prefix;
        auto name=[&](int side,const char* role){return prefix+(side?".B.":".A.")+role;};
        DirectoryConfiguration directory_config;directory_config.locals={{Local(c.side),name(c.side,"namespace"),name(c.side,"listener")}};
        directory_config.routes={{{Local(c.side),Local(1-c.side)},{c.peer_network,name(1-c.side,"listener"),name(c.side,"outgoing"),{name(1-c.side,"outgoing")},RecordDataType::Bytes},1}};
        NativeTableConfiguration config;config.directory=directory_config;config.directions={RouteDirection::Both};config.dial_only_egress=true;config.service_pkg_name=c.package;
        auto built=NativeOwnerTable::Build(config);if(!built)throw std::runtime_error("OWNER_CONFIG");native=std::move(*built.value);
        AttachmentLimits limits;limits.max_record_bytes=256;attachments=std::make_unique<ApplicationAttachments>(std::vector<Locator>{Local(c.side)},limits);
        execution=std::make_unique<NativeSendExecution>(*native);auto reg=execution->CreateRuntime(config,{},SendQueueLimits{32,256*1024,128,1024*1024,256});if(!reg)throw std::runtime_error("SEND_REGISTRATION");registration=std::move(*reg.value);
        PublicSocketConfiguration bc;bc.identity={name(c.side,"outgoing"),{std::uint64_t(100+2*c.side)},{1}};bc.listener_identity={name(c.side,"listener"),{std::uint64_t(101+2*c.side)},{2}};
        bc.package_name=c.package;bc.peer_name=name(1-c.side,"listener");bc.peer_network_id=c.peer_network;bc.max_message_size=256;bc.bind_qos={{0,80},{1,2000}};bc.listen_qos=bc.bind_qos;
        backend=std::make_shared<PublicSocketBackend>(bc);result.kind_is_real=backend->Kind()==BackendKind::DSoftBusProvider;
        BindingRequest request;request.config=config;request.local=Local(c.side);request.route=directory_config.routes[0].key;request.has_route=true;request.role=RecordRole::Client;request.service_name=bc.identity.service_name;request.receiver=&receiver;
        auto directory=EndpointDirectory::Build(directory_config);if(!directory.value)throw std::runtime_error("DIRECTORY");request.directory=*directory.value;
        result.default_rejected=ProviderAdapter::Build(domain,ServiceRecordTable::Process(),*native,*attachments,backend,request).status==BackendStatus::NativeBackendUnavailable;
        if(!result.default_rejected||!result.kind_is_real)throw std::runtime_error("DEFAULT_ISOLATION");
        auto a=RuntimeProbeAccess::Build(domain,ServiceRecordTable::Process(),*native,*attachments,backend,request);if(!a.adapter)throw std::runtime_error("PROBE_BUILD");adapter=std::move(a.adapter);
        if(adapter->Initialize()!=BackendStatus::None||adapter->Publish()!=BackendStatus::None)throw std::runtime_error("INITIALIZE_PUBLISH");
        auto send=RuntimeProbeAccess::SendBridge(*adapter,*native);if(!send)throw std::runtime_error("SEND_BRIDGE");
        execution->Attach(registration,senders);if(senders.empty())throw std::runtime_error("SENDER_ATTACH");
        rx=std::thread([&]{while(!stop.load()){if(pump)pump();adapter->DispatchOne();std::this_thread::sleep_for(1ms);}});
        // Bind return is not proof that the opposite initiation's OnBind has run.
        // Observe this original association, then tell control we are data-ready.
        TraceEvent("WAIT_ACCEPTED",0,0,0,0,0);
        const auto accepted_deadline=std::chrono::steady_clock::now()+15s;
        for(;;){const auto phase=adapter->Observe().accepted_phase;
            if(phase==AcceptedPhase::Active)break;
            if(phase==AcceptedPhase::Failed||phase==AcceptedPhase::Stopping||phase==AcceptedPhase::Closed||std::chrono::steady_clock::now()>=accepted_deadline)throw std::runtime_error("ACCEPTED_OBSERVATION_FAILED");
            std::this_thread::sleep_for(1ms);
        }
        if(!gate("READY"))throw std::runtime_error("READY_GATE");
        if(adapter->Observe().accepted_phase!=AcceptedPhase::Active)throw std::runtime_error("ACCEPTED_NOT_ACTIVE");
        Snapshot("READY",*adapter,*backend);
        for(unsigned i=0;i<5;++i){payloads[i]=MakeRecord(c.run,c.side,i+1);std::vector<rtps::NetworkBuffer> buffers{{payloads[i].data(),256}};std::vector<Locator> targets{Local(1-c.side)};rtps::Locators begin(targets.begin()),end(targets.end());
            if(!senders.front()->send(buffers,256,&begin,&end,std::chrono::steady_clock::now()+5s,0))throw std::runtime_error("SEND_ADMISSION");
            auto work=execution->Take(registration);if(!work)throw std::runtime_error("SEND_TAKE");auto invocation=execution->TryAdmit(*work.value,send);if(!invocation)throw std::runtime_error("SEND_LEASE");
            const auto sent=invocation.value->Run();std::printf("PROBE_SEND side=%d sequence=%u outcome=%d returned=%d code=%d\n",c.side,i+1,int(sent.outcome),int(sent.actual_return_code.has_value()),sent.actual_return_code.value_or(-999));std::fflush(stdout);
            if(!sent.actual_return_code||*sent.actual_return_code!=0)throw std::runtime_error("SEND_RESULT");++result.sent;
        }
        const auto end=std::chrono::steady_clock::now()+15s;while(receiver.ledger.Count()!=5&&receiver.ledger.Errors()==0&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(1ms);
        if(receiver.ledger.Count()!=5||receiver.ledger.Errors()!=0)throw std::runtime_error("RECEIVER_LEDGER");
        if(!gate("DELIVERED"))throw std::runtime_error("DELIVERED_GATE");
    }catch(const std::exception& e){fail(e.what());}catch(...){fail("LOCAL_EXCEPTION");}
    control_gate=nullptr;
    // No detached thread or timed-out invocation release. Synchronous Run has returned here.
    if(execution&&registration){auto ticket=execution->BeginRevoke(registration);if(ticket)ticket.value->Progress();}
    stop=true;if(rx.joinable())rx.join();
    if(adapter){Snapshot("PRE_CLOSE",*adapter,*backend);auto close=adapter->Close();Snapshot("POST_CLOSE",*adapter,*backend);auto usage=attachments->SnapshotUsage();result.drained=close.adapter_drained&&usage.records==0&&usage.bytes==0;if(close.status!=BackendStatus::None||close.cleanup_status!=BackendStatus::None||!result.drained)fail("CLEANUP");}
    if(TraceOverflow())fail("TRACE_OVERFLOW");
    result.received=receiver.ledger.Count();result.invalid=receiver.ledger.Errors();
    if(result.invalid)fail("RECEIVER_LEDGER_FINAL");
    result.success=result.first_failure.empty()&&result.drained&&result.received==5&&result.sent==5&&result.invalid==0;
    std::printf("PROBE_RESULT side=%d success=%d sent=%u received=%u invalid=%u drained=%d first_failure=%s sdk_drain=UNPROVEN\n",c.side,result.success,result.sent,result.received,result.invalid,result.drained,result.first_failure.c_str());std::fflush(stdout);return result;
}
}
