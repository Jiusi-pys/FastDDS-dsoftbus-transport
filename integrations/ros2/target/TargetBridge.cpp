#include "../BridgeOwner.hpp"
#include "../ShutdownCoordination.hpp"
extern "C" void FastddsDsoftbusFactoryFlushTeardownPending() noexcept;
#include "../PlatformModule.hpp"
#include "NativeObservation.hpp"
#include "../BoundedNativeControl.hpp"
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastrtps/attributes/LibrarySettingsAttributes.h>
#include <fastrtps/xmlparser/XMLProfileManager.h>
#include <link.h>
#include <unistd.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <chrono>
#include <tinyxml2.h>
static_assert(TIXML2_MAJOR_VERSION == 6, "Target requires the pinned TinyXML2 ABI");

namespace {
using namespace fastdds_dsoftbus::dsoftbus;
using Owner = fastdds_dsoftbus::ros2_integration::BridgeOwner;
using Values = std::map<std::string, std::string>;
Values read(const char* path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("configuration missing");
    const std::set<std::string> keys{"bridge_library", "fastdds_module", "fastcdr_module", "softbus_module",
        "backend", "run", "side", "domain", "authorized_package", "peer_network_id", "service_prefix", "connect_release", "locator_version", "namespace", "local_udid", "peer_udid", "service_slot", "peer_slot", "profile", "native_name_contract", "local_initiator_name", "peer_initiator_name"};
    Values values;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto equals = line.find('=');
        if (equals == std::string::npos || (!keys.count(line.substr(0, equals))&&line.substr(0,equals)!="runtime_close_release"&&line.substr(0,equals)!="native_reaccept_order"&&line.substr(0,equals)!="native_reaccept_control"&&line.substr(0,equals)!="route_lifecycle"&&line.substr(0,equals)!="process_restart_observation"&&line.substr(0,equals)!="process_restart_cycles") ||
                !values.emplace(line.substr(0, equals), line.substr(equals + 1)).second)
            throw std::runtime_error("configuration syntax/duplicate/unknown key");
    }
    for (const auto& key : keys) if (!values.count(key) || values.at(key).empty())
        throw std::runtime_error("required configuration field missing");
    return values;
}
unsigned integer(const Values& v, const char* key, unsigned limit) {
    const auto& text = v.at(key);
    if (!std::regex_match(text, std::regex("[0-9]+"))) throw std::runtime_error("integer syntax");
    const auto value = std::stoul(text);
    if (value > limit) throw std::runtime_error("integer range");
    return static_cast<unsigned>(value);
}
struct Origins { const Values* config; bool fastdds=false, fastcdr=false, softbus=false, mixed=false; };
int loaded(dl_phdr_info* info, size_t, void* context) {
    auto& origins = *static_cast<Origins*>(context);
    const std::string path = info->dlpi_name ? info->dlpi_name : "";
    if (path.empty()) return 0;
    const auto name = std::filesystem::path(path).filename().string();
    if (name.find("libfastdds.so") == 0) origins.mixed = true;
    for (const auto& item : {std::make_pair("libfastrtps.so", "fastdds_module"),
            std::make_pair("libfastcdr.so", "fastcdr_module"),
            std::make_pair("libsoftbus_client", "softbus_module")}) {
        if (name.find(item.first) != 0) continue;
        std::error_code error;
        const bool same = std::filesystem::equivalent(path, origins.config->at(item.second), error);
        if (error || !same) { origins.mixed = true; continue; }
        if (std::string(item.second) == "fastdds_module") origins.fastdds = true;
        else if (std::string(item.second) == "fastcdr_module") origins.fastcdr = true;
        else origins.softbus = true;
        std::fprintf(stderr, "FDDST_LOADED module=%s path=%s\n", item.first, path.c_str());
    }
    return 0;
}
bool await_connect(Owner* owner, const std::string& path) {
    const auto native = owner->runtime->ObserveSocket();
    if (!native.listener_configured || !native.listener_socket || native.fake_sdk) return false;
    std::fprintf(stdout, "ROS_LISTENER_READY run=%s side=%c pid=%ld listener=%d fake_sdk=0\n",
            owner->run.c_str(), owner->side ? 'B' : 'A', static_cast<long>(getpid()), *native.listener_socket);
    std::fprintf(stdout,"ROS_STAGE run=%s side=%c pid=%ld stage=LISTENER_READY\n",owner->run.c_str(),owner->side?'B':'A',static_cast<long>(getpid()));
    std::fflush(stdout);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < end) {
        std::ifstream input(path); std::string line;
        if (std::getline(input, line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            const auto expected = "ROS_RELEASE run=" + owner->run + " side=" + (owner->side ? "B" : "A") + " stage=LISTENER_READY";
            if (line != expected) return false;
            std::fprintf(stdout, "ROS_CONNECT_ALLOWED run=%s side=%c pid=%ld\n", owner->run.c_str(),
                    owner->side ? 'B' : 'A', static_cast<long>(getpid()));
            std::fflush(stdout); return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false; // Control deadline is not an SDK invocation cancellation.
}
}

extern "C" FASTDDS_DSOFTBUS_FACTORY_EXPORT unsigned FastddsDsoftbusHookVersion() { return 21405; }
extern "C" FASTDDS_DSOFTBUS_FACTORY_EXPORT int FastddsDsoftbusCreate(
        const char* path, unsigned domain, void* raw, void** output) {
    *output = nullptr;
    try {
        const auto values = read(path);
        if(values.at("locator_version")!="V1" || values.at("profile")!="BYTES_NATIVE" || values.at("native_name_contract")!="PACKAGE_SCOPED_V1")
            throw std::runtime_error("explicit supported V1 profile required");
        DSoftBusTransportDescriptor config(65536);
        const auto slot=integer(values,"service_slot",v1::AddressCodec::max_slot);
        const auto peer_slot=integer(values,"peer_slot",v1::AddressCodec::max_slot);
        const v1::DeviceIdentity local_identity{values.at("namespace"),domain,values.at("local_udid")};
        const v1::DeviceIdentity peer_identity{values.at("namespace"),domain,values.at("peer_udid")};
        if(!v1::AddressCodec::ValidIdentity(local_identity)||!v1::AddressCodec::ValidIdentity(peer_identity))
            throw std::runtime_error("invalid deployment identity");
        const bool bounded_native=values.at("backend")=="OHOS_PUBLIC_SOCKET_BOUNDED_REACCEPT_EXPERIMENTAL";
        const bool routes=values.count("route_lifecycle")&&integer(values,"route_lifecycle",1)!=0;
        if(routes&&!bounded_native)throw std::runtime_error("route lifecycle requires explicit bounded experiment");
        if(!bounded_native&&values.at("backend")!="OHOS_PUBLIC_SOCKET_EXPERIMENTAL")throw std::runtime_error("target backend selection");
#ifndef FASTDDS_DSOFTBUS_BOUNDED_NATIVE_REACCEPT_EXPERIMENT
        if(bounded_native)throw std::runtime_error("native reaccept experiment not compiled");
#endif
        if(bounded_native){
            if(!values.count("native_reaccept_order")||!values.count("native_reaccept_control")||
                (values.at("native_reaccept_order")!="outgoing-first"&&values.at("native_reaccept_order")!="accepted-first"))throw std::runtime_error("explicit finite native reaccept contract required");
        }else if(values.count("native_reaccept_order")||values.count("native_reaccept_control"))throw std::runtime_error("native experiment fields require native selection");
        if (!std::regex_match(values.at("run"), std::regex("[0-9a-f]{16}")) ||
                !std::regex_match(values.at("peer_network_id"), std::regex("[0-9a-f]{64}")))
            throw std::runtime_error("run/peer identity");
        const auto side = integer(values, "side", 1);
        const bool process_observation=values.count("process_restart_observation")&&integer(values,"process_restart_observation",1)!=0;
        if(values.count("process_restart_observation")&&(!bounded_native||routes||side))
            throw std::runtime_error("process observation requires non-route bounded A");
        const unsigned restart_cycles=values.count("process_restart_cycles")?integer(values,"process_restart_cycles",2):1;
        if(values.count("process_restart_cycles")&&
                (restart_cycles!=2||!process_observation||!bounded_native||routes||side))
            throw std::runtime_error("two process restarts require explicit non-route bounded A observation");
        if (integer(values, "domain", 232) != domain) throw std::runtime_error("domain mismatch");
        for (const auto* key : {"fastdds_module", "fastcdr_module", "softbus_module", "connect_release"})
            if (!std::filesystem::path(values.at(key)).is_absolute()) throw std::runtime_error("absolute input required");
        Origins origins{&values}; dl_iterate_phdr(loaded, &origins);
        if (origins.mixed || !origins.fastdds || !origins.fastcdr || !origins.softbus)
            throw std::runtime_error("loaded dependency origin mismatch");
        auto* owner = new Owner; *output = owner; owner->run = values.at("run"); owner->side = side;
        fastdds_dsoftbus::ros2_target_observation::Begin(owner->run,side);
        owner->diagnostic_control=[] { (void)fastdds_dsoftbus::ros2_target_observation::ExportPending(); FastddsDsoftbusFactoryFlushTeardownPending(); };
        const auto prefix = values.at("service_prefix");
        const auto issue=config.ConfigureV1([&]{V1Deployment input{local_identity,slot,v1::Profile::BytesNative,prefix+".ns",
            {{peer_identity,peer_slot,values.at("peer_network_id"),values.at("local_initiator_name"),{values.at("peer_initiator_name")}}}};input.native_name_scope={v1::NativeNameContract::PackageScopedV1,values.at("authorized_package"),prefix};for(auto& peer:input.peers)peer.native_name_scope=input.native_name_scope;return input;}());
        if(issue.code!=DirectoryError::None)throw std::runtime_error("V1 deployment configuration rejected");
        const auto local=config.directory.locals.front().local_locator;
        const auto remote=config.directory.routes.front().key.remote_locator;
        phase0::PublicSocketConfiguration backend;
        backend.identity = {values.at("local_initiator_name"),{910+side*4},{1010+side*4}};
        backend.listener_identity = {config.directory.locals.front().local_listen_name,{911+side*4},{1011+side*4}};
        backend.package_name = values.at("authorized_package");backend.expected_peer_initiator_name=values.at("peer_initiator_name"); backend.peer_network_id = values.at("peer_network_id");
        backend.peer_name = config.directory.routes.front().fields.remote_listen_name; backend.max_message_size = 65536;
        backend.bind_qos = {{0,80},{1,2000}}; backend.listen_qos = backend.bind_qos;
        backend.bounded_native_reaccept_experiment=bounded_native;backend.route_lifecycle=routes;
        backend.bounded_native_reset_limit=restart_cycles;
        owner->runtime = std::make_unique<experimental::Runtime>(backend);
        if(bounded_native){
            const auto expected="/data/local/tmp/mdds_dds_"+owner->run+"_"+(side?"B":"A")+"/control.NATIVE_REACCEPT";
            if(values.at("native_reaccept_control")!=expected)throw std::runtime_error("native control window path");
            auto control=std::make_shared<fastdds_dsoftbus::ros2_integration::BoundedNativeControl>(*owner,domain,values.at("native_reaccept_order")=="outgoing-first",expected,false,routes,process_observation,restart_cycles);
            if(routes)owner->node_diagnostic_close=[control]{control->ExportNodeDiagnostics();};
            if(!phase0::RuntimeProbeAccess::BoundedReplacementGate(*owner->runtime,[control]{return control->PermitReplacement();}))throw std::runtime_error("native replacement gate installation");
            owner->before_pump=[control]{try{return control->Step();}catch(const std::exception& error){control->Refuse(error.what());return true;}catch(...){control->Refuse();return true;}};
        }
        if (!owner->runtime->SetPreConnectGate([owner, release=values.at("connect_release")] { return await_connect(owner,release); }))
            throw std::runtime_error("gate installation failed");
        if(values.count("runtime_close_release")) {
            const auto expected="/data/local/tmp/mdds_dds_"+owner->run+"_"+(side?"B":"A")+"/control.RUNTIME_CLOSE";
            if(values.at("runtime_close_release")!=expected)throw std::runtime_error("close control window path");
            fastdds_dsoftbus::ros2_integration::InstallShutdownCoordination(*owner,owner,expected);
        }
        owner->descriptor = owner->runtime->MakeDescriptor(config);
        if (!owner->descriptor) throw std::runtime_error("descriptor issuance failed");
        auto& qos = *static_cast<eprosima::fastdds::dds::DomainParticipantQos*>(raw);
        // The descriptor's maxMessageSize (65536) is above Fast DDS's default no-fragment ceiling (65500). Like the
        // built-in UDP/TCP transports, init() accepts that only when the participant states the larger limit.
        qos.transport().max_msg_size_no_frag = config.maxMessageSize;
        qos.transport().use_builtin_transports = false; qos.transport().user_transports.clear();
        qos.transport().user_transports.push_back(owner->descriptor);
        qos.wire_protocol().builtin.initialPeersList.clear(); qos.wire_protocol().builtin.initialPeersList.push_back(remote);
        qos.wire_protocol().builtin.metatrafficMulticastLocatorList.clear();
        qos.wire_protocol().builtin.metatrafficUnicastLocatorList.clear();
        qos.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(local);
        qos.wire_protocol().default_unicast_locator_list.clear(); qos.wire_protocol().default_unicast_locator_list.push_back(local);
        qos.wire_protocol().default_multicast_locator_list.clear();
        eprosima::fastrtps::LibrarySettingsAttributes settings;
        settings.intraprocess_delivery = eprosima::fastrtps::INTRAPROCESS_OFF;
        eprosima::fastrtps::xmlparser::XMLProfileManager::library_settings(settings);
        owner->StartPump();
        std::fprintf(stderr,"FDDST_ROS_PRE_PARTICIPANT run=%s side=%u builtin=0 intraprocess=0 mode=OHOS_PUBLIC_SOCKET_EXPERIMENTAL\n",owner->run.c_str(),side);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FDDST_REQUIRED_ERROR %s\n",error.what()); return 1;
    }
}
extern "C" FASTDDS_DSOFTBUS_FACTORY_EXPORT void FastddsDsoftbusAfterPublicDelete(void* value) {
    auto* owner = static_cast<Owner*>(value); if (!owner) return;
    // Called by the patched factory only after the original public deletion
    // path actually disabled/deleted/removed the Participant successfully.
    owner->public_delete_completed=true;
    std::fprintf(stdout,"ROS_DDS_DELETED run=%s side=%c pid=%ld dds_component_result=0 api_return=NOT_OBSERVED scope=FACTORY_POST_DELETE\n",
        owner->run.c_str(),owner->side?'B':'A',static_cast<long>(getpid()));std::fflush(stdout);
}
extern "C" FASTDDS_DSOFTBUS_FACTORY_EXPORT int FastddsDsoftbusCaptureStartup(
        fastdds_dsoftbus::ros2_diagnostics::Snapshot* output) noexcept {
    return output&&fastdds_dsoftbus::ros2_target_observation::Capture(*output)?0:1;
}
extern "C" FASTDDS_DSOFTBUS_FACTORY_EXPORT int FastddsDsoftbusClose(void* value) {
    // The normal final summary remains separate from pre-delete value prefixes.
    auto* owner = static_cast<Owner*>(value); if (!owner) return 1;
    const auto result = owner->CloseLocal();
    if (result == 1) { std::fputs("FDDST_RUNTIME_CLOSE_INCOMPLETE\n",stderr); return 1; }
    const auto native = owner->runtime->ObserveSocket();
    const bool observation_complete = fastdds_dsoftbus::ros2_target_observation::Flush();
    std::fprintf(stderr,"FDDST_REAL_SOCKET run=%s side=%u fake_sdk=%d rejected_callbacks=%llu\n",owner->run.c_str(),owner->side,
        int(native.fake_sdk),static_cast<unsigned long long>(native.rejected_callbacks));
    owner->descriptor.reset(); owner->runtime.reset();
    std::fprintf(stderr,"FDDST_ROS_CLOSED run=%s side=%u pump_joined=1 runtime_closed=1 local_only=1\n",owner->run.c_str(),owner->side);
    if(result==0&&observation_complete&&owner->public_delete_completed){std::fprintf(stdout,"ROS_STAGE run=%s side=%c pid=%ld stage=DDS_DELETED\n",owner->run.c_str(),owner->side?'B':'A',static_cast<long>(getpid()));std::fflush(stdout);}
    std::fflush(stderr); delete owner; return observation_complete ? result : 2;
}
extern "C" FASTDDS_DSOFTBUS_FACTORY_EXPORT int FastddsDsoftbusCaptureNativePrefix(
        fastdds_dsoftbus::ros2_diagnostics::NativePrefix* output) noexcept {
    return output&&fastdds_dsoftbus::ros2_target_observation::CapturePrefix(*output)?0:1;
}
