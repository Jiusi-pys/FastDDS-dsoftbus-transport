#include "TransportBootstrap.hpp"

namespace fastdds_dsoftbus::stock_transport {
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
using eprosima::fastrtps::types::ReturnCode_t;
static const ReturnCode_t kOk = ReturnCode_t::RETCODE_OK;
#else
// Fast DDS 3.x: ReturnCode_t is an integral alias with namespace-level constants.
using eprosima::fastdds::dds::ReturnCode_t;
static constexpr ReturnCode_t kOk = eprosima::fastdds::dds::RETCODE_OK;
#endif

StockTransportBootstrap::StockTransportBootstrap(
        eprosima::fastdds::dds::DomainParticipantFactory& factory) : factory_(factory) {}

BootstrapResult StockTransportBootstrap::Install(
        eprosima::fastdds::dds::DomainId_t domain,
        std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> descriptor,
        const std::vector<fastdds_dsoftbus::binding::Locator_t>& local,
        const std::vector<fastdds_dsoftbus::binding::Locator_t>& peers)
{
    if (installed_) return BootstrapResult::AlreadyInstalled;
    if (!descriptor || descriptor->maxMessageSize == 0 || local.empty() || peers.empty())
        return BootstrapResult::InvalidConfiguration;
    if (!factory_.lookup_participants(domain).empty()) return BootstrapResult::ParticipantsActive;
    // rmw_fastrtps calls this same public once-only loader before reading the
    // factory default. Loading first prevents later XML initialization from
    // overwriting this explicit transport selection.
    if (factory_.load_profiles() != kOk ||
            factory_.get_default_participant_qos(previous_) != kOk)
        return BootstrapResult::FactoryError;
    selected_ = previous_;
    auto& transport = selected_.transport();
    transport.use_builtin_transports = false;
    transport.user_transports = {std::move(descriptor)};
    transport.max_msg_size_no_frag = transport.user_transports.front()->maxMessageSize;
    auto& wire = selected_.wire_protocol();
    wire.builtin.initialPeersList.clear();
    wire.builtin.metatrafficMulticastLocatorList.clear();
    wire.builtin.metatrafficUnicastLocatorList.clear();
    wire.default_multicast_locator_list.clear();
    wire.default_unicast_locator_list.clear();
    for (const auto& value : peers) wire.builtin.initialPeersList.push_back(value);
    for (const auto& value : local) {
        wire.builtin.metatrafficUnicastLocatorList.push_back(value);
        wire.default_unicast_locator_list.push_back(value);
    }
    if (factory_.set_default_participant_qos(selected_) != kOk)
        return BootstrapResult::FactoryError;
    domain_ = domain;
    installed_ = true;
    return BootstrapResult::Ok;
}

BootstrapResult StockTransportBootstrap::Release()
{
    if (!installed_) return BootstrapResult::InvalidConfiguration;
    if (!factory_.lookup_participants(domain_).empty()) return BootstrapResult::ParticipantsActive;
    eprosima::fastdds::dds::DomainParticipantQos current;
    if (factory_.get_default_participant_qos(current) != kOk)
        return BootstrapResult::FactoryError;
    if (!(current == selected_)) return BootstrapResult::DefaultsChanged;
    if (factory_.set_default_participant_qos(previous_) != kOk)
        return BootstrapResult::FactoryError;
    installed_ = false;
    selected_ = previous_;
    return BootstrapResult::Ok;
}
}
