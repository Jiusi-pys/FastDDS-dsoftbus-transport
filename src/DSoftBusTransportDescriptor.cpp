#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransportDescriptor.hpp>
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
#include <cstdio>
#include <limits>
#include <algorithm>

namespace fastdds_dsoftbus::dsoftbus {
DSoftBusTransportDescriptor::DSoftBusTransportDescriptor(std::uint32_t bound)
    : TransportDescriptorInterface(bound, 1),
      directory{std::vector<LocalBinding>(std::size_t{0}), std::vector<Route>(std::size_t{0})},
      egress_selections(std::size_t{0}), default_metatraffic_unicast(std::size_t{0}), default_user_unicast(std::size_t{0})
{ send_limits.max_record_bytes = bound; }
DirectoryIssue DSoftBusTransportDescriptor::ConfigureV1(const V1Deployment& deployment)
{
    // This runtime currently transports Bytes records only. Legal RAW codec
    // profiles remain structural values; they are not a native support claim.
    if (deployment.profile != v1::Profile::BytesNative) return {DirectoryError::IncompatibleProfile};
    if (!v1::AddressCodec::ValidNativeScope(deployment.native_name_scope)) return {DirectoryError::InvalidString};
    DSoftBusTransportDescriptor prepared(*this);
    prepared.directory.locals.clear();
    prepared.directory.routes.clear();
    prepared.directory.identities.clear();
    prepared.egress_selections.clear();
    prepared.profile = deployment.profile;
    prepared.native_name_scope = deployment.native_name_scope;
    const auto encode = [&](const v1::DeviceIdentity& identity, std::uint32_t slot,
            Locator& locator, std::string& name) -> DirectoryIssue {
        EndpointId key{};
        if (!v1::AddressCodec::DeriveKey(identity, key)) return {DirectoryError::InvalidIdentity};
        if (!v1::AddressCodec::EncodeLocator({key, slot, deployment.profile}, locator) ||
                !v1::AddressCodec::EncodeScopedServiceName({identity.name_space, identity.domain, slot, deployment.profile}, deployment.native_name_scope, name))
            return {DirectoryError::InvalidLocator};
        prepared.directory.identities.push_back({key, identity.name_space, identity.domain, identity.udid});
        return {};
    };
    Locator local; std::string listener;
    auto issue = encode(deployment.identity, deployment.slot, local, listener);
    if (issue.code != DirectoryError::None) return issue;
    prepared.directory.locals.push_back({local, deployment.callback_namespace, listener});
    for (const auto& peer : deployment.peers) {
        if (!v1::AddressCodec::SameNativeScope(peer.native_name_scope,deployment.native_name_scope)) return {DirectoryError::InvalidString};
        if (peer.identity.name_space != deployment.identity.name_space || peer.identity.domain != deployment.identity.domain)
            return {DirectoryError::InvalidIdentity};
        Locator remote; std::string remote_listener;
        issue = encode(peer.identity, peer.slot, remote, remote_listener);
        if (issue.code != DirectoryError::None) return issue;
        prepared.directory.routes.push_back({{local, remote}, {peer.network_id, remote_listener,
            peer.local_initiator_name, peer.expected_remote_initiator_names, RecordDataType::Bytes}, 1});
    }
    prepared.default_metatraffic_unicast = {local};
    prepared.default_user_unicast = {local};
    issue = prepared.ValidateV1();
    if (issue.code != DirectoryError::None) return issue;
    directory.locals.swap(prepared.directory.locals);
    directory.routes.swap(prepared.directory.routes);
    directory.identities.swap(prepared.directory.identities);
    egress_selections.swap(prepared.egress_selections);
    default_metatraffic_unicast.swap(prepared.default_metatraffic_unicast);
    default_user_unicast.swap(prepared.default_user_unicast);
    profile = prepared.profile;
    native_name_scope.authorized_package.swap(prepared.native_name_scope.authorized_package);
    native_name_scope.deployment_prefix.swap(prepared.native_name_scope.deployment_prefix);
    native_name_scope.contract=prepared.native_name_scope.contract;
    return {};
}
DirectoryIssue DSoftBusTransportDescriptor::ValidateV1() const
{
    if (directory.identities.empty()) return {DirectoryError::MissingIdentity};
    if (!v1::AddressCodec::ValidNativeScope(native_name_scope)) return {DirectoryError::InvalidString};
    if (profile != v1::Profile::BytesNative) return {DirectoryError::IncompatibleProfile};
    const auto built = EndpointDirectory::Build(directory);
    if (!built.value) return built.issue;
    // One activity corresponds to one listener and one complete advertisement.
    if (directory.locals.size() != 1 || default_metatraffic_unicast.size() != 1 ||
            default_user_unicast != default_metatraffic_unicast ||
            default_user_unicast.front() != directory.locals.front().local_locator)
        return {DirectoryError::InvalidLocator};
    const auto canonical_name = [&](const Locator& locator, const std::string& name) {
        v1::EndpointAddress address;
        if (!v1::AddressCodec::DecodeLocator(locator, address) || address.profile != profile) return false;
        const auto identity = std::find_if(directory.identities.begin(), directory.identities.end(),
            [&](const DirectoryConfiguration::IdentityBinding& value) { return value.key == address.device_key; });
        if (identity == directory.identities.end()) return false;
        std::string expected;
        return v1::AddressCodec::EncodeScopedServiceName({identity->name_space, identity->domain, address.slot, profile}, native_name_scope, expected) && expected == name;
    };
    if (!canonical_name(directory.locals.front().local_locator, directory.locals.front().local_listen_name))
        return {DirectoryError::InvalidIdentity};
    EndpointId local_key{};
    LocatorCodec::Decode(directory.locals.front().local_locator, local_key);
    const auto local_identity = std::find_if(directory.identities.begin(), directory.identities.end(),
        [&](const DirectoryConfiguration::IdentityBinding& value) { return value.key == local_key; });
    for (const auto& identity : directory.identities)
        if (identity.name_space != local_identity->name_space || identity.domain != local_identity->domain)
            return {DirectoryError::InvalidIdentity};
    for (const auto& route : directory.routes) {
        if (!canonical_name(route.key.remote_locator, route.fields.remote_listen_name)) return {DirectoryError::InvalidIdentity};
        if (route.fields.local_initiator_name==directory.locals.front().local_listen_name ||
                std::find(route.fields.expected_remote_initiator_names.begin(),route.fields.expected_remote_initiator_names.end(),route.fields.remote_listen_name)!=route.fields.expected_remote_initiator_names.end())return {DirectoryError::InvalidString};
    }
    return {};
}
eprosima::fastdds::rtps::TransportInterface* DSoftBusTransportDescriptor::create_transport() const
{
    try { return new DSoftBusTransport(*this); }
    catch (const std::bad_alloc&) {
        std::fputs("Fast DDS DSoftBus factory: AllocationFailure\n", stderr); std::fflush(stderr); return nullptr;
    }
}
std::uint32_t DSoftBusTransportDescriptor::min_send_buffer_size() const
{
    return send_limits.max_record_bytes <= (std::numeric_limits<std::uint32_t>::max)()
            ? static_cast<std::uint32_t>(send_limits.max_record_bytes) : 0;
}
} // namespace fastdds_dsoftbus::dsoftbus
