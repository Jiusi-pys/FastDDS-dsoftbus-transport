#include "TransportBootstrap.hpp"
#include "V1NativeNameTestSupport.hpp"
#include "FakePublicSocketLink.hpp"
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/rtps/transport/TransportReceiverInterface.h>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace fastdds_dsoftbus::dsoftbus;
using namespace fastdds_dsoftbus::stock_transport;

namespace {
// The peer exercises the public Transport input lifecycle only. It does not
// implement a DDS/ROS2 peer or supply protocol acknowledgements.
class PeerReceiver final : public eprosima::fastdds::rtps::TransportReceiverInterface {
public:
    void OnDataReceived(const eprosima::fastrtps::rtps::octet*, std::uint32_t,
            const eprosima::fastdds::rtps::Locator&, const eprosima::fastdds::rtps::Locator&) override {}
};

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
v1::DeviceIdentity identity(unsigned side) {
    return {"stock-host-runtime", 151, side ? "synthetic-B" : "synthetic-A"};
}
std::string listener(unsigned side) {
    std::string result;
    check(v1::AddressCodec::EncodeScopedServiceName(
        {identity(side).name_space, 151, 17 + side, v1::Profile::BytesNative},
        {v1::NativeNameContract::PackageScopedV1, "host.public.dds", "host.public.dds.v1"}, result),
        "listener name encoding");
    return result;
}
phase0::PublicSocketConfiguration configuration(unsigned side) {
    phase0::PublicSocketConfiguration value;
    value.identity = {side ? "host.public.B.outgoing" : "host.public.A.outgoing",
        {910 + side * 4}, {1010 + side * 4}};
    value.listener_identity = {listener(side), {911 + side * 4}, {1011 + side * 4}};
    value.package_name = "host.public.dds";
    value.peer_name = listener(1 - side);
    value.peer_network_id = side ? "model.net.A" : "model.net.B";
    value.expected_peer_initiator_name = side ? "host.public.A.outgoing" : "host.public.B.outgoing";
    value.bind_qos = {{0, 80}, {1, 2000}};
    value.listen_qos = value.bind_qos;
    value.max_message_size = 8192;
    return value;
}
DSoftBusTransportDescriptor configuration_descriptor(unsigned side) {
    const auto config = configuration(side);
    DSoftBusTransportDescriptor value(8192);
    V1Deployment input{identity(side), 17 + side, v1::Profile::BytesNative, "stock.host",
        {{identity(1 - side), 18 - side, config.peer_network_id,
            config.identity.service_name, {config.expected_peer_initiator_name}}}};
    check(value.ConfigureV1(ScopedHostDeployment(input)).code == DirectoryError::None,
        "transport directory validation");
    return value;
}

void invalid() {
    auto& factory = *eprosima::fastdds::dds::DomainParticipantFactory::get_instance();
    eprosima::fastdds::dds::DomainParticipantQos before;
    check(factory.get_default_participant_qos(before) == 0, "read initial defaults");
    StockTransportBootstrap bootstrap(factory);
    check(bootstrap.Install(151, {}, {}, {}) == BootstrapResult::InvalidConfiguration,
        "null descriptor rejected");
    check(factory.get_default_participant_qos() == before, "invalid install changed defaults");
    check(bootstrap.Release() == BootstrapResult::InvalidConfiguration, "uninstalled release accepted");
}

void lifecycle(bool changed) {
    auto& factory = *eprosima::fastdds::dds::DomainParticipantFactory::get_instance();
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(), false);
    public_socket_fake::SetListenerNames({listener(0), listener(1)});
    auto a_config = configuration_descriptor(0), b_config = configuration_descriptor(1);
    experimental::Runtime a(configuration(0)), b(configuration(1));
    std::mutex gate;
    std::condition_variable ready;
    unsigned arrived = 0;
    auto peer_ready = [&] {
        std::unique_lock<std::mutex> lock(gate);
        ++arrived;
        ready.notify_all();
        return ready.wait_for(lock, std::chrono::seconds(3), [&] { return arrived == 2; });
    };
    check(a.SetPreConnectGate(peer_ready) && b.SetPreConnectGate(peer_ready), "install listener barrier");
    auto a_descriptor = a.MakeDescriptor(a_config), b_descriptor = b.MakeDescriptor(b_config);
    check(a_descriptor && b_descriptor, "runtime descriptors");
    StockTransportBootstrap bootstrap(factory);
    check(bootstrap.Install(151, a_descriptor, {a_config.directory.locals.front().local_locator},
        {a_config.directory.routes.front().key.remote_locator}) == BootstrapResult::Ok, "public default install");
    const auto configured = factory.get_default_participant_qos();
    check(!configured.transport().use_builtin_transports &&
        configured.transport().user_transports.size() == 1 &&
        configured.transport().user_transports.front() == a_descriptor, "selected default transport");
    check(factory.load_profiles() == 0 && factory.get_default_participant_qos() == configured,
        "stock RMW's repeated load_profiles would replace selected defaults");

    PeerReceiver receiver;
    std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> peer(b_descriptor->create_transport());
    check(bool(peer), "peer transport factory");
    bool peer_started = false;
    std::thread other([&] {
        peer_started = peer->init() && peer->OpenInputChannel(
            b_config.directory.locals.front().local_locator, &receiver, 8192);
    });
    // Actual published Factory API. Its PARTICIPANT_QOS_DEFAULT must select the
    // configured transport without patching/intercepting create_participant.
    auto* participant = factory.create_participant(151, eprosima::fastdds::dds::PARTICIPANT_QOS_DEFAULT);
    other.join();
    check(participant && peer_started, "actual default Participant/Transport creation");
    check(bootstrap.Release() == BootstrapResult::ParticipantsActive,
        "release while selected Participant live");
    const auto before = public_socket_fake::Observe();
    std::cout << "FAKE_BEFORE_DELETE live=" << before.live << " binds="
        << before.binds[0] << ',' << before.binds[1] << '\n';
    check(before.live == 6 && before.binds[0] == 1 && before.binds[1] == 1,
        "fake native pair was not actually created");
    // No Runtime::Close, after_delete observer, bootstrap release or manual
    // Transport shutdown precedes the public factory deletion below.
    const auto deletion = factory.delete_participant(participant);
    check(deletion == 0, "original public deletion failed");
    const auto deleted = public_socket_fake::Observe();
    check(deleted.shutdowns[0] > before.shutdowns[0] && deleted.live < before.live,
        "Participant deletion did not naturally close its transport sockets");
    check(factory.get_default_participant_qos().transport().user_transports.front() == a_descriptor,
        "factory default did not retain descriptor through native shutdown");
    if (changed) {
        auto external = configured;
        external.name("external-configuration");
        check(factory.set_default_participant_qos(external) == 0, "external public configuration");
        check(bootstrap.Release() == BootstrapResult::DefaultsChanged &&
            factory.get_default_participant_qos() == external, "release clobbered external configuration");
        check(factory.set_default_participant_qos(configured) == 0, "restore test's selected defaults");
    }
    check(bootstrap.Release() == BootstrapResult::Ok, "public defaults release");
    peer.reset();
    const auto closed = public_socket_fake::Observe();
    check(closed.live == 0 && closed.queued == 0 && closed.bytes == 0 && closed.invalid == 0,
        "native Fake Socket residue after natural Transport destruction");
    // Post-observation idempotent cleanup is kept separate from the proof above.
    check(a.Close() && b.Close(), "idempotent runtime cleanup");
    std::cout << "PUBLIC_FACTORY_DELETE=0 NATURAL_TRANSPORT_SHUTDOWN=1 DEFAULT_DESCRIPTOR_RETAINED=1 "
        "SOCKET_BOUNDARY=HOST_FAKE DEVICE_TESTED=0\n";
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "one case required");
        const std::string name = argv[1];
        if (name == "invalid") invalid();
        else if (name == "lifecycle" || name == "defaults-changed") lifecycle(name == "defaults-changed");
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
