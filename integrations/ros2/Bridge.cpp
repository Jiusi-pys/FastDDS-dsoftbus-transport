#include "HostSocket.hpp"
#include "BridgeOwner.hpp"
#include "ShutdownCoordination.hpp"
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
    for(auto& e:m)if(e.first!="bridge_library"&&e.first!="fastdds_module"&&e.first!="fastcdr_module"&&e.first!="backend"&&e.first!="run"&&e.first!="side"&&e.first!="port"&&e.first!="peer_port"&&e.first!="domain"&&e.first!="forward"&&e.first!="host_close_error"&&e.first!="runtime_close_release"&&e.first!="locator_version"&&e.first!="native_name_contract"&&e.first!="service_prefix"&&e.first!="authorized_package"&&e.first!="host_reaccept"&&e.first!="reaccept_control")throw std::runtime_error("unknown key");
    return m;
}
unsigned integer(const std::map<std::string,std::string>& m,const char* k,unsigned max) {
    const auto& s=m.at(k);if(!std::regex_match(s,std::regex("[0-9]+")))throw std::runtime_error("integer syntax");
    auto n=std::stoul(s);if(n>max)throw std::runtime_error("integer range");return static_cast<unsigned>(n);
}
struct Owner:fastdds_dsoftbus::ros2_integration::BridgeOwner {bool host_close_error=false;};
}
extern "C" __declspec(dllexport) unsigned FastddsDsoftbusHookVersion(){return 21405;}
extern "C" __declspec(dllexport) int FastddsDsoftbusCreate(const char* path,unsigned domain,void* raw,void** out) {
    *out=nullptr;
    try {
        auto m=read(path);
        if(m.at("locator_version")!="V1"||m.at("native_name_contract")!="PACKAGE_SCOPED_V1"||m.at("authorized_package")!="host.ros.test")throw std::runtime_error("explicit V1 configuration required");
        if(m.at("backend")!="HOST_OPAQUE_BYTES_TEST_ONLY")throw std::runtime_error("backend unavailable; Host-only binary");
        if(!std::regex_match(m.at("run"),std::regex("[0-9a-f]{16}")))throw std::runtime_error("run identity");
        unsigned side=integer(m,"side",1),port=integer(m,"port",65535),peerport=integer(m,"peer_port",65535);
        if(!port||!peerport||port==peerport||integer(m,"domain",232)!=domain)throw std::runtime_error("domain/ports");
        bool forward=integer(m,"forward",1)!=0;
        const bool reaccept=m.count("host_reaccept")&&integer(m,"host_reaccept",1)!=0;
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
        // Explicit Host injection only. Execute the actual resource-complete
        // Runtime close, then report its existing execution-error outcome.
        if(m.count("host_close_error")&&integer(m,"host_close_error",1)){
            owner->host_close_error=true;std::fputs("FDDST_HOST_TEARDOWN_INJECTION source=HOST_FAKE cause=RESOURCE_COMPLETE_ERROR\n",stderr);
        }
        fastdds_dsoftbus::ros2_target_observation::Begin(owner->run,side);
        fastdds_dsoftbus::ros2_target_observation::SetSourceForHostInjection();
        owner->diagnostic_control=[] {(void)fastdds_dsoftbus::ros2_target_observation::ExportPending();FastddsDsoftbusFactoryFlushTeardownPending();};
        DSoftBusTransportDescriptor config(65536);
        const auto identity=[&](unsigned index){return v1::DeviceIdentity{"host-ros",domain,index?"synthetic-host-B":"synthetic-host-A"};};
        const auto issue=config.ConfigureV1([&]{V1Deployment input{identity(side),17+side,v1::Profile::BytesNative,prefix+".ns",
            {{identity(1-side),18-side,side?"host.net.A":"host.net.B",name(side,"outgoing"),{name(1-side,"outgoing")}}}};input.native_name_scope={v1::NativeNameContract::PackageScopedV1,m.at("authorized_package"),m.at("service_prefix")};for(auto& peer:input.peers)peer.native_name_scope=input.native_name_scope;return input;}());
        if(issue.code!=DirectoryError::None)throw std::runtime_error("V1 deployment configuration rejected");
        const auto local=config.directory.locals.front().local_locator;
        const auto remote=config.directory.routes.front().key.remote_locator;
        const auto& local_name=config.directory.locals.front().local_listen_name;
        const auto& peer_name=config.directory.routes.front().fields.remote_listen_name;
        fastdds_dsoftbus::ros2_host::ConfigureLink({side,static_cast<std::uint16_t>(port),static_cast<std::uint16_t>(peerport),owner->run,"host.ros.test",local_name,peer_name,name(1-side,"outgoing"),forward,reaccept,reaccept?m.at("reaccept_control"):std::string{}});
        phase0::PublicSocketConfiguration b;
        b.identity={name(side,"outgoing"),{910+side*4},{1010+side*4}};
        b.listener_identity={local_name,{911+side*4},{1011+side*4}};
        b.package_name=m.at("authorized_package");b.expected_peer_initiator_name=name(1-side,"outgoing");b.peer_network_id=side?"host.net.A":"host.net.B";b.peer_name=peer_name;
        b.max_message_size=65536;b.bind_qos={{0,80},{1,2000}};b.listen_qos=b.bind_qos;
        b.bounded_reaccept_host=reaccept;
        owner->runtime=std::make_unique<experimental::Runtime>(b);
        if(reaccept){
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
        // The descriptor's maxMessageSize (65536) is above Fast DDS's default no-fragment ceiling (65500). Like the
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
    const int close_result=p->CloseLocal();
    if(close_result==1) {std::fputs("FDDST_RUNTIME_CLOSE_INCOMPLETE\n",stderr);return 1;}
    fastdds_dsoftbus::ros2_host::StopLink();
    const auto io=fastdds_dsoftbus::ros2_host::ObserveLink();const auto native=p->runtime->ObserveSocket();
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
