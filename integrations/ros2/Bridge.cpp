#include "HostSocket.hpp"
#include "HostProfile.hpp"
#include "HostProfileGuard.hpp"
#include "HostIoObservation.hpp"
#include "HostReceiveObservation.hpp"
#include "HostCallbackObservation.hpp"
#include "BridgeOwner.hpp"
#include "Ros214Version.hpp"
#include "ShutdownCoordination.hpp"
#include "BoundedNativeControl.hpp"
#include "PayloadResourceConfig.hpp"
#include "target/NativeObservation.hpp"
extern "C" __declspec(dllimport) void FastddsDsoftbusFactoryFlushTeardownPending() noexcept;
#define NOMINMAX
#include <windows.h>
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastrtps/attributes/LibrarySettingsAttributes.h>
#include <fastrtps/xmlparser/XMLProfileManager.h>
#include <atomic>
#include <fstream>
#include <map>
#include <thread>
#include <stdexcept>
#include <regex>
#include <cstdio>
#include <filesystem>
#include <cstring>

namespace {
using namespace fastdds_dsoftbus::dsoftbus;
namespace fd=eprosima::fastdds::dds;
std::map<std::string,std::string> read(const char* path) {
    std::ifstream f(path);if(!f)throw std::runtime_error("configuration missing");
    std::map<std::string,std::string> m;std::string line;
    while(std::getline(f,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        auto p=line.find('=');if(p==std::string::npos)throw std::runtime_error("configuration syntax");
        if(!m.emplace(line.substr(0,p),line.substr(p+1)).second)throw std::runtime_error("duplicate key");
    }
    for(auto& e:m)if(e.first!="host_close_incomplete"&&e.first!="host_fragment_control"&&e.first!="host_fragment_observation"&&e.first!="host_send_terminal_control"&&e.first!="bridge_library"&&e.first!="fastdds_module"&&e.first!="fastcdr_module"&&e.first!="backend"&&e.first!="run"&&e.first!="side"&&e.first!="port"&&e.first!="peer_port"&&e.first!="domain"&&e.first!="forward"&&e.first!="host_close_error"&&e.first!="runtime_close_release"&&e.first!="locator_version"&&e.first!="native_name_contract"&&e.first!="service_prefix"&&e.first!="authorized_package"&&e.first!="host_reaccept"&&e.first!="reaccept_control"&&e.first!="route_lifecycle"&&e.first!="owned_normal_reaccept"&&e.first!="native_reaccept_order"&&e.first!="host_io_observation"&&e.first!="host_receive_observation")throw std::runtime_error("unknown key");
    (void)fastdds_dsoftbus::payload_resource::parse(m);
    return m;
}
unsigned integer(const std::map<std::string,std::string>& m,const char* k,unsigned max) {
    const auto& s=m.at(k);if(!std::regex_match(s,std::regex("[0-9]+")))throw std::runtime_error("integer syntax");
    auto n=std::stoul(s);if(n>max)throw std::runtime_error("integer range");return static_cast<unsigned>(n);
}
class HostReceiveObserver final : public ProcessReceiveObserver {
public:
    void OnReceive(ReceiveStage,const ReceiveDiagnostic&,const std::uint8_t*,std::size_t) noexcept override {}
    void OnDecision(const ReceiveDecision& original) noexcept override {
        // Original value-only capture has already left every ownership gate.
        // This observer performs no lookup, payload decode, I/O or Runtime call.
        using Item=fastdds_dsoftbus::ros2_target_observation::ReceiveDecisionSnapshot::Item;
        Item value;const auto& admission=original.bridge;
        value.layer=static_cast<int>(original.layer);value.branch=static_cast<int>(original.branch);
        value.branch_known=original.branch!=ReceiveDecisionBranch::Unknown;
        value.rejected=value.branch_known&&original.branch!=ReceiveDecisionBranch::BridgeAccepted&&original.branch!=ReceiveDecisionBranch::Forwarded;
        value.sequence=original.sequence;value.record_incarnation=original.record_incarnation;value.correlation=original.correlation;
        value.socket=original.socket;value.role=original.role;value.phase=original.phase;value.bytes=original.size;
        value.bridge_called=original.bridge_called;value.refused_receive=original.refused_receive;value.refused_receive_total=original.refused_receive_total;
        value.status_known=original.status_known;value.status=original.status;
        value.stage=static_cast<int>(admission.stage);value.reason=static_cast<int>(admission.reason);
        value.result_known=admission.result_known;value.result=admission.result;
        value.native_status_known=admission.native_status_known;value.native_status=static_cast<int>(admission.native_status);
        value.native_known=admission.native_known;value.native_slot=admission.native_slot;value.native_incarnation=admission.native_incarnation;
        value.native_generation=admission.native_generation;value.native_route_version=admission.native_route_version;
        value.application_known=admission.application_known;value.application_incarnation=admission.application_incarnation;value.logical_token=admission.logical_token;
        value.receiver_known=admission.receiver_known;value.receiver_incarnation=admission.receiver_incarnation;
        value.capacity_known=admission.capacity_known;value.lifecycle_known=admission.lifecycle_known;
        value.application_phase=admission.application_phase;value.receiver_phase=admission.receiver_phase;value.stopped=admission.stopped;
        value.record_limit_known=admission.record_limit_known;value.record_limit=admission.record_limit;
        value.records_used=admission.records_used;value.records_limit=admission.records_limit;
        value.bytes_used=admission.bytes_used;value.bytes_limit=admission.bytes_limit;
        value.local_records_used=admission.local_records_used;value.local_records_limit=admission.local_records_limit;
        value.local_bytes_used=admission.local_bytes_used;value.local_bytes_limit=admission.local_bytes_limit;
        value.copying_used=admission.copying_used;value.copying_limit=admission.copying_limit;
        value.application_copying=admission.application_copying;value.application_copying_limit=admission.application_copying_limit;
        std::strncpy(value.stage_name,RxAdmissionStageName(admission.stage),sizeof(value.stage_name)-1);
        std::strncpy(value.reason_name,RxAdmissionReasonName(admission.reason),sizeof(value.reason_name)-1);
        fastdds_dsoftbus::ros2_target_observation::ObserveReceiveDecision(value);
    }
};
static_assert(static_cast<std::size_t>(ReceiveDecisionBranch::Count)<=16,"receive branch ledger capacity");
static_assert(static_cast<std::size_t>(RxAdmissionReason::Count)<=64,"receive reason ledger capacity");
struct Owner:fastdds_dsoftbus::ros2_integration::BridgeOwner {
    bool host_close_error=false,host_close_incomplete=false;
    bool host_fragment_observation=false,host_fragment_observation_flushed=false;
    std::string send_refusal_control;
    bool send_refusal_exercised=false; // sole pump thread, until control-side join
    static void EndOriginalSend(void* context,std::int32_t original_socket,std::chrono::steady_clock::time_point) noexcept {
        auto& owner=*static_cast<Owner*>(context);
        if(owner.send_refusal_exercised)return;
        try {
            std::ifstream control(owner.send_refusal_control);std::string value;
            if(!std::getline(control,value)||value!="REFUSE")return;
            owner.send_refusal_exercised=true;
            const bool observed=fastdds_dsoftbus::ros2_host::EndOriginalSendForHostTest(original_socket);
            std::fprintf(stderr,"FDDST_HOST_SEND_TERMINAL_BARRIER original_socket=%d terminal_observed=%d source=HOST_ORIGINAL_CONNECTION\n",original_socket,int(observed));
            if(!observed)owner.failed=true;
        }catch(...){owner.failed=true;}
    }
};
}
extern "C" __declspec(dllexport) unsigned FastddsDsoftbusHookVersion(){return 21405;}
extern "C" __declspec(dllexport) unsigned FastddsDsoftbusHookSecurity(){return HAVE_SECURITY;}
extern "C" __declspec(dllexport) int FastddsDsoftbusCreate(const char* path,unsigned domain,void* raw,void** out) {
    *out=nullptr;
    try {
        // Plain C only: reject a foreign public C++ layout before inspecting qos.
        auto module=GetModuleHandleA("fastrtps-2.14.dll");
        auto mode=reinterpret_cast<unsigned(*)()>(GetProcAddress(module,"FastddsDsoftbusFactorySecurityMode"));
        if((mode&&mode()!=HAVE_SECURITY)||(!mode&&HAVE_SECURITY))throw std::runtime_error("security ABI mode mismatch");
#if FASTDDS_DSOFTBUS_HOST_HISTORICAL_PROFILE
        // Qualify the actual exporting module before Owner allocation or any
        // access to the factory-owned C++ QoS object. Keep the factory's own
        // independent pre-Create security check intact.
        fastdds_dsoftbus::ros2_host_profile::ValidateHistoricalFactory(
            GetModuleHandleA("fastrtps-2.14.dll"),
            std::filesystem::u8path(fastdds_dsoftbus::ros2_host_profile::factory_path), HAVE_SECURITY);
        fastdds_dsoftbus::ros2_host_profile::RequireModulePath(
            GetModuleHandleA("fastcdr-2.2.dll"),
            std::filesystem::u8path(fastdds_dsoftbus::ros2_host_profile::cdr_path));
#endif
        auto m=read(path);
        if(m.at("locator_version")!="V1"||m.at("native_name_contract")!="PACKAGE_SCOPED_V1"||m.at("authorized_package")!="host.ros.test")throw std::runtime_error("explicit V1 configuration required");
        if(m.at("backend")!="HOST_OPAQUE_BYTES_TEST_ONLY")throw std::runtime_error("backend unavailable; Host-only binary");
        if(!std::regex_match(m.at("run"),std::regex("[0-9a-f]{16}")))throw std::runtime_error("run identity");
        unsigned side=integer(m,"side",1),port=integer(m,"port",65535),peerport=integer(m,"peer_port",65535);
        if(!port||!peerport||port==peerport||integer(m,"domain",232)!=domain)throw std::runtime_error("domain/ports");
        bool forward=integer(m,"forward",1)!=0;
        const bool fragment_observation=m.count("host_fragment_observation")&&integer(m,"host_fragment_observation",1)!=0;
        const bool reaccept=m.count("host_reaccept")&&integer(m,"host_reaccept",1)!=0;
        const bool owned_normal=m.count("owned_normal_reaccept")&&integer(m,"owned_normal_reaccept",1)!=0;
        const bool observe_io=fastdds_dsoftbus::ros2_host::io_observation::ConfigurationEnabled(m);
        const bool observe_receive=fastdds_dsoftbus::ros2_host_receive::ConfigurationEnabled(m);
        const bool routes=m.count("route_lifecycle")&&integer(m,"route_lifecycle",1)!=0;
        const auto fragment_control=m.count("host_fragment_control")?m.at("host_fragment_control"):std::string{};
        if(m.count("host_fragment_control")&&(side!=1||!forward||fragment_control.empty()||!std::filesystem::path(fragment_control).is_absolute()||reaccept||routes))throw std::runtime_error("fragment loss control requires explicit B-only absolute Host negative profile");
        if(!fragment_control.empty())std::fputs("FDDST_HOST_FRAGMENT_CONTROL enabled=1 mode=NEGATIVE_DROP_LAST profile=TEST_ONLY_BOUNDED diagnostic_cost=PER_CALL_RTPS_PARSE_ONLY_WHEN_OPTED_IN\n",stderr);
        if(routes&&!owned_normal)throw std::runtime_error("route model requires original retained-entity scene");
        if(owned_normal&&(!reaccept||!m.count("native_reaccept_order")||(m.at("native_reaccept_order")!="outgoing-first"&&m.at("native_reaccept_order")!="accepted-first")))throw std::runtime_error("explicit owned normal Host model required");
        if(reaccept&&(!m.count("reaccept_control")||!std::filesystem::path(m.at("reaccept_control")).is_absolute()))throw std::runtime_error("Host reaccept control must be explicit absolute path");
        if(GetModuleHandleA("fastdds-3.6.dll")||GetModuleHandleA("fastcdr-2.3.dll"))throw std::runtime_error("mixed Fast DDS/Fast CDR ABI");
        for(const auto* dll:{"fastrtps-2.14.dll","fastcdr-2.2.dll"}) {
            char loaded[MAX_PATH]{};auto handle=GetModuleHandleA(dll);
            if(!handle||!GetModuleFileNameA(handle,loaded,MAX_PATH))throw std::runtime_error("declared dependency not loaded");
            const auto& expected=m.at(std::strcmp(dll,"fastrtps-2.14.dll")==0?"fastdds_module":"fastcdr_module");
            if(!std::filesystem::path(expected).is_absolute()||!std::filesystem::is_regular_file(expected)||!std::filesystem::equivalent(expected,loaded))throw std::runtime_error("dependency source mismatch");
            std::fprintf(stderr,"FDDST_LOADED module=%s path=%s\n",dll,loaded);
        }
        const auto prefix="host.ros."+m.at("run");
        auto name=[&](unsigned s,const char* role){return prefix+"."+std::to_string(s)+"."+role;};
        auto* owner=new Owner;*out=owner;owner->run=m.at("run");owner->side=side;
        owner->host_fragment_observation=fragment_observation;
        if(fragment_observation)std::fputs("FDDST_HOST_FRAGMENT_OBSERVATION enabled=1 profile=TEST_ONLY_BOUNDED scope=STRUCTURAL_APP_OBSERVER export=CONTROL_AFTER_JOIN diagnostic_cost=BOUNDED_CLOSE_IO\n",stderr);
        // Explicit Host injection only. Execute the actual resource-complete
        // Runtime close, then report its existing execution-error outcome.
        if(m.count("host_close_error")&&integer(m,"host_close_error",1)){
            owner->host_close_error=true;std::fputs("FDDST_HOST_TEARDOWN_INJECTION source=HOST_FAKE cause=RESOURCE_COMPLETE_ERROR\n",stderr);
        }
        // Explicit Host injection only, and only for the close-contract counterexample. It
        // forces the "close could not be certified complete" verdict that the huge-rejection
        // scene reaches on its own about once in 36 Host runs, so the abort path below it is
        // exercisable on demand instead of by waiting for that race. It does not touch the
        // runtime, the pool, the adapter or any success criterion: the real close still runs
        // and its real result is still observed; only the reported verdict is pinned.
        if(m.count("host_close_incomplete")&&integer(m,"host_close_incomplete",1)){
            owner->host_close_incomplete=true;std::fputs("FDDST_HOST_TEARDOWN_INJECTION source=HOST_FAKE cause=RUNTIME_CLOSE_INCOMPLETE\n",stderr);
        }
        fastdds_dsoftbus::ros2_target_observation::Begin(owner->run,side);
        fastdds_dsoftbus::ros2_target_observation::SetSourceForHostInjection();
        owner->diagnostic_control=[] {(void)fastdds_dsoftbus::ros2_target_observation::ExportPending();FastddsDsoftbusFactoryFlushTeardownPending();};
        // 65535, not 65536. Two constraints bound this value, and only one of them is about sizes
        // being large enough. (1) Upper bound: a participant with DDS Security caps the receiver
        // buffer it will offer at uint16 max, because the secure ReceiverResource needs an
        // auxiliary decrypt buffer (RTPSParticipantImpl::createReceiverResources), so
        // capacity = min(65536, 65535) = 65535 while the transport refuses a receiver buffer
        // smaller than its descriptor's maximum. 65536 could therefore never be opened by a secure
        // participant. (2) Consistency: init() requires maxMessageSize <= the effective no-fragment
        // limit; 65500 is Fast DDS's default limit that this line overrides below, not a lower
        // bound to clear.
        DSoftBusTransportDescriptor config(65535);
        const auto identity=[&](unsigned index){return v1::DeviceIdentity{"host-ros",domain,index?"synthetic-host-B":"synthetic-host-A"};};
        const auto issue=config.ConfigureV1([&]{V1Deployment input{identity(side),17+side,v1::Profile::BytesNative,prefix+".ns",
            {{identity(1-side),18-side,side?"host.net.A":"host.net.B",name(side,"outgoing"),{name(1-side,"outgoing")}}}};input.native_name_scope={v1::NativeNameContract::PackageScopedV1,m.at("authorized_package"),m.at("service_prefix")};for(auto& peer:input.peers)peer.native_name_scope=input.native_name_scope;return input;}());
        if(issue.code!=DirectoryError::None)throw std::runtime_error("V1 deployment configuration rejected");
        const auto local=config.directory.locals.front().local_locator;
        const auto remote=config.directory.routes.front().key.remote_locator;
        const auto& local_name=config.directory.locals.front().local_listen_name;
        const auto& peer_name=config.directory.routes.front().fields.remote_listen_name;
        fastdds_dsoftbus::ros2_host::ConfigureLink({side,static_cast<std::uint16_t>(port),static_cast<std::uint16_t>(peerport),owner->run,"host.ros.test",local_name,peer_name,name(1-side,"outgoing"),forward,reaccept,reaccept?m.at("reaccept_control"):std::string{},routes,identity(1-side).udid,fragment_control,observe_io});
        phase0::PublicSocketConfiguration b;
        b.identity={name(side,"outgoing"),{910+side*4},{1010+side*4}};
        b.listener_identity={local_name,{911+side*4},{1011+side*4}};
        b.package_name=m.at("authorized_package");b.expected_peer_initiator_name=name(1-side,"outgoing");b.peer_network_id=side?"host.net.A":"host.net.B";b.peer_name=peer_name;
        b.max_message_size=65536;b.bind_qos={{0,80},{1,2000}};b.listen_qos=b.bind_qos;
        b.bounded_reaccept_host=reaccept;
        b.route_lifecycle=routes;
        if(!host_rx_observation::Configure(observe_receive))throw std::runtime_error("Host receive observation configuration changed within process");
        std::shared_ptr<ProcessReceiveObserver> receive_observer;
        if(observe_receive)receive_observer=std::make_shared<fastdds_dsoftbus::ros2_host_receive::Observer>();
        owner->runtime=std::make_unique<experimental::Runtime>(b,std::move(receive_observer));
        if(m.count("host_send_terminal_control")) {
            owner->send_refusal_control=m.at("host_send_terminal_control");
            if(!std::filesystem::path(owner->send_refusal_control).is_absolute())throw std::runtime_error("absolute Host send control required");
            if(!phase0::RuntimeProbeAccess::BeforeSendForHostTest(*owner->runtime,&Owner::EndOriginalSend,owner))
                throw std::runtime_error("Host send scheduling installation");
        }
        if(owned_normal){
            auto control=std::make_shared<fastdds_dsoftbus::ros2_integration::BoundedNativeControl>(*owner,domain,m.at("native_reaccept_order")=="outgoing-first",m.at("reaccept_control"),true,routes);
            if(routes)owner->node_diagnostic_close=[control]{if(!control->ExportNodeDiagnostics())throw std::runtime_error("incomplete node diagnostic capture");};
            if(!phase0::RuntimeProbeAccess::BoundedReplacementGate(*owner->runtime,[control]{return control->PermitReplacement();}))throw std::runtime_error("model replacement gate installation");
            owner->before_pump=[control,routes]{try{if(routes)fastdds_dsoftbus::ros2_host::NodeControlStep();return control->Step();}catch(const std::exception& error){control->Refuse(error.what());return true;}catch(...){control->Refuse();return true;}};
        }else if(reaccept){
            struct Observation {std::optional<std::int32_t> outgoing,accepted;bool terminal=false,settled=false,replaced=false;};
            auto observed=std::make_shared<Observation>();
            owner->before_pump=[owner,observed]{
                const bool permitted=fastdds_dsoftbus::ros2_host::ReacceptControlStep();
                const auto current=owner->runtime->ObserveSocket();
                if(!observed->outgoing&&current.outgoing_socket&&current.accepted_socket){observed->outgoing=current.outgoing_socket;observed->accepted=current.accepted_socket;}
                if(observed->outgoing){
                    const bool terminal=owner->side==0?current.outgoing_stopped:current.accepted_stopped;
                    if(terminal&&!observed->terminal){observed->terminal=true;fastdds_dsoftbus::ros2_host::ReacceptControlReceipt("TERMINAL");}
                    if(owner->side==1&&observed->terminal&&!current.accepted_socket&&!observed->settled){observed->settled=true;fastdds_dsoftbus::ros2_host::ReacceptControlReceipt("SETTLED");}
                    const auto id=owner->side==0?current.outgoing_socket:current.accepted_socket;
                    const auto old=owner->side==0?observed->outgoing:observed->accepted;
                    if(observed->terminal&&id&&id!=old&&!terminal&&!observed->replaced){
                        observed->replaced=true;fastdds_dsoftbus::ros2_host::ReacceptControlReceipt("REPLACED");
                        std::fprintf(stderr,"HOST_REACCEPT_REPLACED run=%s side=%u old_id=%d new_id=%d source=HOST_PUBLIC_C_API sdk_epoch=NOT_PROVEN\n",owner->run.c_str(),owner->side,*old,*id);std::fflush(stderr);
                    }
                }
                return permitted;
            };
        }
        if(!owner->runtime->SetPreConnectGate([]{return fastdds_dsoftbus::ros2_host::WaitPeerListener(15000);}))throw std::runtime_error("gate installation");
        if(m.count("runtime_close_release")) {
            if(!std::filesystem::path(m.at("runtime_close_release")).is_absolute())throw std::runtime_error("absolute close control required");
            fastdds_dsoftbus::ros2_integration::InstallShutdownCoordination(*owner,owner,m.at("runtime_close_release"));
        }
        owner->descriptor=owner->runtime->MakeDescriptor(config);if(!owner->descriptor)throw std::runtime_error("descriptor issuance");
        auto& qos=*static_cast<fd::DomainParticipantQos*>(raw);
        // The descriptor's maxMessageSize (65535) overrides Fast DDS's default no-fragment limit (65500). Like the
        // built-in UDP/TCP transports, init() accepts that only when the participant states the larger limit.
        qos.transport().max_msg_size_no_frag=config.maxMessageSize;
        qos.transport().use_builtin_transports=false;qos.transport().user_transports.clear();qos.transport().user_transports.push_back(owner->descriptor);
        qos.wire_protocol().builtin.initialPeersList.clear();qos.wire_protocol().builtin.initialPeersList.push_back(remote);
        qos.wire_protocol().builtin.metatrafficMulticastLocatorList.clear();
        qos.wire_protocol().builtin.metatrafficUnicastLocatorList.clear();
        qos.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(local);
        qos.wire_protocol().default_unicast_locator_list.clear();
        qos.wire_protocol().default_unicast_locator_list.push_back(local);
        qos.wire_protocol().default_multicast_locator_list.clear();
        eprosima::fastrtps::LibrarySettingsAttributes settings;settings.intraprocess_delivery=eprosima::fastrtps::INTRAPROCESS_OFF;
        eprosima::fastrtps::xmlparser::XMLProfileManager::library_settings(settings);
        if(eprosima::fastrtps::xmlparser::XMLProfileManager::library_settings().intraprocess_delivery!=eprosima::fastrtps::INTRAPROCESS_OFF)throw std::runtime_error("intraprocess disable");
        owner->StartPump();
        std::fprintf(stderr,"FDDST_ROS_PRE_PARTICIPANT run=%s side=%u builtin=0 intraprocess=0 mode=HOST_OPAQUE_BYTES_TEST_ONLY\n",owner->run.c_str(),side);std::fflush(stderr);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FDDST_REQUIRED_ERROR %s\n",e.what());std::fflush(stderr);return 1;}
}
extern "C" __declspec(dllexport) int FastddsDsoftbusClose(void* value) {
    auto* p=static_cast<Owner*>(value);if(!p)return 1;
    const auto export_fragments=[p] {
        // The sole supported control-side close owner calls this after the pump
        // and Host link joins, before any Runtime/descriptor/module release.
        // Flush snapshots the existing fixed store under its leaf observation
        // mutex and emits outside it. This bounded opt-in export has I/O cost;
        // neither its scope nor its success is an SDK physical-drain claim.
        if(!p->host_fragment_observation||p->host_fragment_observation_flushed)return;
        p->host_fragment_observation_flushed=true;
        bool flush_return=false,exception=false;
        try{flush_return=fastdds_dsoftbus::ros2_target_observation::Flush();}catch(...){exception=true;}
        const int output_flush=std::fflush(stdout);
        const bool output_error=output_flush!=0||std::ferror(stdout)!=0;
        const bool complete=flush_return&&!exception&&!output_error;
        const int marker=std::fprintf(stderr,"FDDST_HOST_FRAGMENT_CAPTURE run=%s side=%c flush_return=%d exception=%d output_error=%d complete=%d profile=TEST_ONLY_BOUNDED scope=STRUCTURAL_APP_OBSERVER sdk_epoch=NOT_PROVEN\n",p->run.c_str(),p->side?'B':'A',int(flush_return),int(exception),int(output_error),int(complete));
        const int marker_flush=std::fflush(stderr);
        if(marker<0||marker_flush!=0)std::fputs("FDDST_HOST_FRAGMENT_CAPTURE_MARKER_OUTPUT_ERROR\n",stderr);
        // Diagnostic incompleteness never changes the original provider/SDK
        // status, business verdict or ownership obligations of this close.
    };
    const int local_close=p->CloseLocal();
    const int close_result=(local_close==0&&p->host_close_incomplete)?1:local_close;
    if(close_result==1) {
        // Bounded read-only diagnostic: name the provider-side event that left the close
        // incomplete. Observation only; close semantics and the failure verdict are unchanged.
        const auto io=fastdds_dsoftbus::ros2_host::ObserveLink();const auto native=p->runtime->ObserveSocket();
        std::fprintf(stderr,"FDDST_HOST_IO run=%s side=%u sends=%llu ingress=%llu send_errors=%llu fake_sdk=%d rejected_callbacks=%llu\n",p->run.c_str(),p->side,static_cast<unsigned long long>(io.sends),static_cast<unsigned long long>(io.ingress),static_cast<unsigned long long>(io.send_errors),int(native.fake_sdk),static_cast<unsigned long long>(native.rejected_callbacks));
        std::fprintf(stderr,"FDDST_CLOSE_INCOMPLETE_DETAIL result=%d first_exception=%d first_api_error_op=%d first_api_error_code=%d exceptions=%llu rejected_callbacks=%llu fake_sdk=%d\n",
            close_result,
            native.first_exception?int(*native.first_exception):-1,
            native.first_api_error?int(native.first_api_error->operation):-1,
            native.first_api_error?native.first_api_error->code:-1,
            static_cast<unsigned long long>(native.exceptions),
            static_cast<unsigned long long>(native.rejected_callbacks),int(native.fake_sdk));std::fflush(stderr);
        // The Host link seam must be joined on this path too. Its accept/read threads are
        // namespace-scope std::thread objects in HostSocket.cpp that the CRT destroys at
        // DLL_PROCESS_DETACH, and a still-joinable one calls std::terminate() there -- which
        // killed the process with 0xC0000409 *after* the incomplete close was already
        // reported, turning a reported failure into an unreportable abort. Joining here is a
        // teardown-accounting fix only: the return value, the verdict and the diagnostics
        // above are unchanged, and the retained product owner (return 1) is untouched.
        fastdds_dsoftbus::ros2_host::StopLink();
        if(!fastdds_dsoftbus::ros2_target_observation::ExportAdmissions())std::fputs("FDDST_ADMISSION_CAPTURE_INCOMPLETE\n",stderr);
        export_fragments();
        std::fprintf(stderr,"FDDST_HOST_LINK_JOINED complete=1 owner_released=0\n");
        std::fputs("FDDST_RUNTIME_CLOSE_INCOMPLETE\n",stderr);std::fflush(stderr);return 1;}
    fastdds_dsoftbus::ros2_host::StopLink();
    if(!fastdds_dsoftbus::ros2_target_observation::ExportAdmissions())std::fputs("FDDST_ADMISSION_CAPTURE_INCOMPLETE\n",stderr);
    export_fragments();
    if(!fastdds_dsoftbus::ros2_host::io_observation::Export(p->run.c_str(),p->side))
        std::fputs("FDDST_HOST_IO_CAPTURE_INCOMPLETE\n",stderr);
    if(!fastdds_dsoftbus::ros2_host_receive::Export(p->run.c_str(),p->side,GetCurrentProcessId()))
        std::fputs("FDDST_HOST_RX_CAPTURE_INCOMPLETE\n",stderr);
    const auto io=fastdds_dsoftbus::ros2_host::ObserveLink();const auto native=p->runtime->ObserveSocket();
    if(fastdds_dsoftbus::dsoftbus::host_rx_observation::Enabled()&&
        !fastdds_dsoftbus::ros2_host_callback::ExportCoverage(p->run.c_str(),p->side,GetCurrentProcessId(),io.ingress,phase0::RuntimeProbeAccess::ObserveConnection(*p->runtime).adapter))
        std::fputs("FDDST_HOST_RX_CAPTURE_INCOMPLETE\n",stderr);
    const auto final_native=phase0::RuntimeProbeAccess::ObserveConnection(*p->runtime).native;
    const auto& send_call=native.operations[static_cast<std::size_t>(phase0::PublicSocketOperation::Send)];
    std::fprintf(stderr,"FDDST_HOST_CLEANUP threads_joined=%d retained_owner_metadata=%llu retained_id_metadata=%llu callbacks=%llu receives=%llu send_entered=%llu attempts=%llu calls=%llu returns=%llu\n",
        int(io.threads_joined),static_cast<unsigned long long>(final_native.owners),static_cast<unsigned long long>(final_native.owned_ids),
        static_cast<unsigned long long>(final_native.callbacks),static_cast<unsigned long long>(final_native.receives),
        static_cast<unsigned long long>(final_native.send.entered),static_cast<unsigned long long>(send_call.attempts),
        static_cast<unsigned long long>(send_call.calls),static_cast<unsigned long long>(send_call.returns));
    std::fprintf(stderr,"FDDST_HOST_IO run=%s side=%u sends=%llu ingress=%llu send_errors=%llu fake_sdk=%d rejected_callbacks=%llu\n",p->run.c_str(),p->side,static_cast<unsigned long long>(io.sends),static_cast<unsigned long long>(io.ingress),static_cast<unsigned long long>(io.send_errors),int(native.fake_sdk),static_cast<unsigned long long>(native.rejected_callbacks));
    const bool failed=p->failed.load();if(failed)std::fputs("FDDST_PUMP_ERROR_OBSERVED\n",stderr);
    p->descriptor.reset();p->runtime.reset();
    const bool injected=p->host_close_error;
    std::fprintf(stderr,"FDDST_ROS_CLOSED run=%s side=%u pump_joined=1 runtime_closed=1 local_only=1\n",p->run.c_str(),p->side);std::fflush(stderr);delete p;return failed||injected?2:0;
}
extern "C" __declspec(dllexport) int FastddsDsoftbusCaptureStartup(fastdds_dsoftbus::ros2_diagnostics::Snapshot* output) noexcept {
    return output&&fastdds_dsoftbus::ros2_target_observation::Capture(*output)?0:1;
}
extern "C" __declspec(dllexport) int FastddsDsoftbusCaptureNativePrefix(fastdds_dsoftbus::ros2_diagnostics::NativePrefix* output) noexcept {
    return output&&fastdds_dsoftbus::ros2_target_observation::CapturePrefix(*output)?0:1;
}
