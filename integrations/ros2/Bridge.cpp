#include "HostSocket.hpp"
#include "BridgeOwner.hpp"
#include "target/NativeObservation.hpp"
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
    for(auto& e:m)if(e.first!="bridge_library"&&e.first!="fastdds_module"&&e.first!="fastcdr_module"&&e.first!="backend"&&e.first!="run"&&e.first!="side"&&e.first!="port"&&e.first!="peer_port"&&e.first!="domain"&&e.first!="forward")throw std::runtime_error("unknown key");
    return m;
}
unsigned integer(const std::map<std::string,std::string>& m,const char* k,unsigned max) {
    const auto& s=m.at(k);if(!std::regex_match(s,std::regex("[0-9]+")))throw std::runtime_error("integer syntax");
    auto n=std::stoul(s);if(n>max)throw std::runtime_error("integer range");return static_cast<unsigned>(n);
}
Locator logical(unsigned side) {
    Locator l{};l.kind=LocatorCodec::kind;l.port=LocatorCodec::port;l.address[0]=0x31;l.address[15]=static_cast<unsigned char>(side+1);return l;
}
using Owner = fastdds_dsoftbus::ros2_integration::BridgeOwner;
}
extern "C" __declspec(dllexport) unsigned FastddsDsoftbusHookVersion(){return 21405;}
extern "C" __declspec(dllexport) int FastddsDsoftbusCreate(const char* path,unsigned domain,void* raw,void** out) {
    *out=nullptr;
    try {
        auto m=read(path);
        if(m.at("backend")!="HOST_OPAQUE_BYTES_TEST_ONLY")throw std::runtime_error("backend unavailable; Host-only binary");
        if(!std::regex_match(m.at("run"),std::regex("[0-9a-f]{16}")))throw std::runtime_error("run identity");
        unsigned side=integer(m,"side",1),port=integer(m,"port",65535),peerport=integer(m,"peer_port",65535);
        if(!port||!peerport||port==peerport||integer(m,"domain",232)!=domain)throw std::runtime_error("domain/ports");
        bool forward=integer(m,"forward",1)!=0;
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
        fastdds_dsoftbus::ros2_target_observation::Begin(owner->run,side);
        fastdds_dsoftbus::ros2_target_observation::SetSourceForHostInjection();
        owner->diagnostic_control=[] {(void)fastdds_dsoftbus::ros2_target_observation::ExportPending();};
        fastdds_dsoftbus::ros2_host::ConfigureLink({side,static_cast<std::uint16_t>(port),static_cast<std::uint16_t>(peerport),owner->run,"host.ros.test",name(side,"listener"),name(1-side,"listener"),name(1-side,"outgoing"),forward});
        phase0::PublicSocketConfiguration b;
        b.identity={name(side,"outgoing"),{910+side*4},{1010+side*4}};
        b.listener_identity={name(side,"listener"),{911+side*4},{1011+side*4}};
        b.package_name="host.ros.test";b.peer_network_id=side?"host.net.A":"host.net.B";b.peer_name=name(1-side,"listener");
        b.max_message_size=65536;b.bind_qos={{0,80},{1,2000}};b.listen_qos=b.bind_qos;
        DSoftBusTransportDescriptor config(65536);
        config.directory.locals={{logical(side),prefix+".ns",b.listener_identity.service_name}};
        config.directory.routes={{{logical(side),logical(1-side)}, {b.peer_network_id,b.peer_name,b.identity.service_name,{name(1-side,"outgoing")},RecordDataType::Bytes},1}};
        config.default_metatraffic_unicast={logical(side)};config.default_user_unicast={logical(side)};
        owner->runtime=std::make_unique<experimental::Runtime>(b);
        if(!owner->runtime->SetPreConnectGate([]{return fastdds_dsoftbus::ros2_host::WaitPeerListener(15000);}))throw std::runtime_error("gate installation");
        owner->descriptor=owner->runtime->MakeDescriptor(config);if(!owner->descriptor)throw std::runtime_error("descriptor issuance");
        auto& qos=*static_cast<fd::DomainParticipantQos*>(raw);
        qos.transport().use_builtin_transports=false;qos.transport().user_transports.clear();qos.transport().user_transports.push_back(owner->descriptor);
        qos.wire_protocol().builtin.initialPeersList.clear();qos.wire_protocol().builtin.initialPeersList.push_back(logical(1-side));
        qos.wire_protocol().builtin.metatrafficMulticastLocatorList.clear();
        qos.wire_protocol().builtin.metatrafficUnicastLocatorList.clear();
        qos.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(logical(side));
        qos.wire_protocol().default_unicast_locator_list.clear();
        qos.wire_protocol().default_unicast_locator_list.push_back(logical(side));
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
    std::fprintf(stderr,"FDDST_ROS_CLOSED run=%s side=%u pump_joined=1 runtime_closed=1 local_only=1\n",p->run.c_str(),p->side);std::fflush(stderr);delete p;return failed?2:0;
}
extern "C" __declspec(dllexport) int FastddsDsoftbusCaptureStartup(fastdds_dsoftbus::ros2_diagnostics::Snapshot* output) noexcept {
    return output&&fastdds_dsoftbus::ros2_target_observation::Capture(*output)?0:1;
}
