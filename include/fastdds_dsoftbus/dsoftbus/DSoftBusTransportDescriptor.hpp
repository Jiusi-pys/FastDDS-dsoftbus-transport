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
    // Receive admission for this runtime's local input, in records and bytes. A record
    // that arrives while the input is full is dropped, like UDP. 0 keeps the default
    // (32 records / 256 KiB). Only the Participant that starts a runtime sets it.
    std::size_t receive_records = 0;
    std::size_t receive_bytes = 0;
    // Participants one shared runtime admits at once (1..4). The Participant that starts
    // the runtime sets it; joiners cannot change it. Default 2.
    std::size_t max_participants = 2;
    // Domain-circuit runtimes only, with expected_domain_id set: bind the view to its
    // domain when its first input opens instead of on its first initial peer. A
    // Participant without initial peers (Discovery Server) needs this. Default off: the
    // view stays unconfirmed (Await) until an initial peer confirms the domain.
    bool bind_declared_domain_at_open = false;
    // Fast DDS calls shutdown() before it sends the Participant dispose, so closing there loses the dispose and peers
    // wait out the lease. With this set shutdown() does nothing and destruction closes, as on UDP. Default off: shutdown()
    // closes, which callers that drive the Transport directly rely on.
    bool close_at_destruction = false;
    v1::Profile profile = v1::Profile::BytesNative;
    v1::NativeNameScope native_name_scope;
    // Absent preserves the legacy unscoped Transport. Stock assembly opts in
    // from the validated IdentityBinding primitive, never a locator inversion.
    std::optional<std::uint32_t> expected_domain_id;
    // Atomic pre-publication setup. Inputs are explicit deployment identities;
    // this operation does not acquire permissions or authenticate the digest.
    DirectoryIssue ConfigureV1(const V1Deployment&);
    DirectoryIssue ValidateV1() const;
    eprosima::fastdds::rtps::TransportInterface* create_transport() const override;
    std::uint32_t min_send_buffer_size() const override;
};
} // namespace fastdds_dsoftbus::dsoftbus
#endif
