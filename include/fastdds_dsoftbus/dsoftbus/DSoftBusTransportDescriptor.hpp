#ifndef FASTDDS_DSOFTBUS_DSOFTBUS_TRANSPORT_DESCRIPTOR_HPP
#define FASTDDS_DSOFTBUS_DSOFTBUS_TRANSPORT_DESCRIPTOR_HPP

#include <fastdds_dsoftbus/dsoftbus/RuntimeSendPath.hpp>
#include <fastdds_dsoftbus/dsoftbus/FastddsApi.hpp>
#include <fastdds_dsoftbus/dsoftbus/V1Address.hpp>

namespace fastdds_dsoftbus::dsoftbus {
struct V1PeerDeployment
{
    v1::DeviceIdentity identity;
    std::uint32_t slot = 0;
    std::string network_id;
    std::string local_initiator_name;
    std::vector<std::string> expected_remote_initiator_names;
    v1::NativeNameScope native_name_scope;
};
struct V1Deployment
{
    v1::DeviceIdentity identity;
    std::uint32_t slot = 0;
    v1::Profile profile = v1::Profile::BytesNative;
    std::string callback_namespace;
    std::vector<V1PeerDeployment> peers;
    v1::NativeNameScope native_name_scope;
};
struct DSoftBusTransportDescriptor final : eprosima::fastdds::rtps::TransportDescriptorInterface
{
    explicit DSoftBusTransportDescriptor(std::uint32_t max_record_bytes);
    DSoftBusTransportDescriptor(const DSoftBusTransportDescriptor&) = default;
    DSoftBusTransportDescriptor& operator=(const DSoftBusTransportDescriptor&) = default;
    DirectoryConfiguration directory;
    std::vector<EgressSelection> egress_selections;
    std::vector<Locator> default_metatraffic_unicast;
    std::vector<Locator> default_user_unicast;
    SendQueueLimits send_limits;
    v1::Profile profile = v1::Profile::BytesNative;
    v1::NativeNameScope native_name_scope;
    // Atomic pre-publication setup. Inputs are explicit deployment identities;
    // this operation does not acquire permissions or authenticate the digest.
    DirectoryIssue ConfigureV1(const V1Deployment&);
    DirectoryIssue ValidateV1() const;
    eprosima::fastdds::rtps::TransportInterface* create_transport() const override;
    std::uint32_t min_send_buffer_size() const override;
};
} // namespace fastdds_dsoftbus::dsoftbus
#endif
