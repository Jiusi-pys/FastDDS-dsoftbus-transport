#include <mdds/dsoftbus/EndpointDirectory.hpp>

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace mdds::dsoftbus {
namespace {

bool DirectoryBytesValid(const std::string& bytes, std::size_t maximum)
{
    return !bytes.empty() && bytes.size() <= maximum && bytes.find('\0') == std::string::npos;
}

bool DirectoryEndpointValid(const EndpointId& endpoint)
{
    if (endpoint[0] == 0xff) return false;
    // Besides all-zero, reserve IPv6 ::1 because Fast DDS's generic isLocal
    // fallback and external-locator heuristic also inspect private kinds.
    const bool prefix_zero = std::all_of(endpoint.begin(), endpoint.end() - 1,
            [](std::uint8_t byte) { return byte == 0; });
    return !(prefix_zero && endpoint.back() <= 1);
}

bool DirectorySameKey(const RouteKey& a, const RouteKey& b)
{
    // Locator's equality compares kind, port and all address bytes. Do not use
    // its whole-struct ordering or an IP address comparator for logical IDs.
    return a.local_locator == b.local_locator && a.remote_locator == b.remote_locator;
}

bool DirectorySameObserved(const ObservedPeerKey& a, const ObservedPeerKey& b)
{
    return a.callback_namespace == b.callback_namespace &&
           a.remote_network_id == b.remote_network_id &&
           a.actual_remote_socket_name == b.actual_remote_socket_name &&
           a.actual_data_type == b.actual_data_type;
}

bool DirectorySlotConflict(const std::string& first_name, const std::string& first_namespace,
        const std::string& second_name, const std::string& second_namespace)
{
    // Called only within the SAME role. Current pinned DSoftBus stores one
    // socketClient/socketServer listener per named server node (manager:2219).
    // This configuration check is not evidence of board callback behaviour.
    return first_name == second_name && first_namespace != second_namespace;
}

} // namespace

bool LocatorCodec::Encode(const EndpointId& endpoint, Locator& output)
{
    if (!DirectoryEndpointValid(endpoint)) return false;
    Locator encoded(kind, port);
    std::copy(endpoint.begin(), endpoint.end(), encoded.address);
    output = encoded;
    return true;
}

bool LocatorCodec::Decode(const Locator& locator, EndpointId& output)
{
    if (locator.kind != kind || locator.port != port) return false;
    EndpointId endpoint{};
    std::copy(std::begin(locator.address), std::end(locator.address), endpoint.begin());
    if (!DirectoryEndpointValid(endpoint)) return false;
    output = endpoint;
    return true;
}

bool LocatorCodec::IsValid(const Locator& locator)
{
    EndpointId endpoint{};
    return Decode(locator, endpoint);
}

DirectoryResult<EndpointDirectory::Snapshot> EndpointDirectory::Build(const DirectoryConfiguration& config)
{
    // All validation is against this input, with no mutation/publication of an
    // existing snapshot. The trusted configuration's startup checks are O(n^2).
    // Network callback input never grows the directory or its reverse index.
    for (std::size_t i = 0; i < config.locals.size(); ++i) {
        const auto& local = config.locals[i];
        if (!LocatorCodec::IsValid(local.local_locator))
            return {std::nullopt, {DirectoryError::InvalidLocator, i}};
        if (!DirectoryBytesValid(local.callback_namespace, 255) ||
                !DirectoryBytesValid(local.local_listen_name, 255))
            return {std::nullopt, {DirectoryError::InvalidString, i}};
        for (std::size_t j = 0; j < i; ++j) {
            const auto& previous = config.locals[j];
            if (local.local_locator == previous.local_locator)
                return {std::nullopt, {DirectoryError::DuplicateLocal, i, j}};
            if (DirectorySlotConflict(local.local_listen_name, local.callback_namespace,
                    previous.local_listen_name, previous.callback_namespace))
                return {std::nullopt, {DirectoryError::NamespaceConflict, i, j}};
        }
    }

    // The sized constructor can propagate Debug STL proxy allocation failure;
    // vector's noexcept default constructor would terminate before callers can
    // handle bad_alloc. Keep transactional construction without publishing.
    std::vector<ReverseEntry> reverse(std::initializer_list<ReverseEntry>{});
    std::vector<std::size_t> local_indexes(std::size_t{0});
    local_indexes.reserve(config.routes.size());
    for (std::size_t i = 0; i < config.routes.size(); ++i) {
        const auto& route = config.routes[i];
        if (!LocatorCodec::IsValid(route.key.local_locator) ||
                !LocatorCodec::IsValid(route.key.remote_locator))
            return {std::nullopt, {DirectoryError::InvalidLocator, i}};
        const auto local_it = std::find_if(config.locals.begin(), config.locals.end(),
                [&](const LocalBinding& binding) { return binding.local_locator == route.key.local_locator; });
        if (local_it == config.locals.end())
            return {std::nullopt, {DirectoryError::MissingLocal, i}};
        local_indexes.push_back(static_cast<std::size_t>(local_it - config.locals.begin()));
        if (route.route_version == 0)
            return {std::nullopt, {DirectoryError::InvalidVersion, i}};
        if (route.fields.data_type != RecordDataType::Bytes)
            return {std::nullopt, {DirectoryError::UnsupportedDataType, i}};
        const auto& fields = route.fields;
        if (!DirectoryBytesValid(fields.remote_network_id, 64) ||
                !DirectoryBytesValid(fields.remote_listen_name, 255) ||
                !DirectoryBytesValid(fields.local_initiator_name, 255) ||
                fields.expected_remote_initiator_names.empty())
            return {std::nullopt, {DirectoryError::InvalidString, i}};
        for (std::size_t alias = 0; alias < fields.expected_remote_initiator_names.size(); ++alias) {
            const auto& name = fields.expected_remote_initiator_names[alias];
            if (!DirectoryBytesValid(name, 255))
                return {std::nullopt, {DirectoryError::InvalidString, i}};
            for (std::size_t previous = 0; previous < alias; ++previous) {
                if (name == fields.expected_remote_initiator_names[previous])
                    return {std::nullopt, {DirectoryError::DuplicateAlias, i}};
            }
        }

        for (std::size_t j = 0; j < i; ++j) {
            const auto& previous = config.routes[j];
            if (DirectorySameKey(route.key, previous.key))
                return {std::nullopt, {DirectoryError::DuplicateRoute, i, j}};
            if (DirectorySlotConflict(fields.local_initiator_name, local_it->callback_namespace,
                    previous.fields.local_initiator_name, config.locals[local_indexes[j]].callback_namespace))
                return {std::nullopt, {DirectoryError::NamespaceConflict, i, j}};
        }

        for (const auto& name : fields.expected_remote_initiator_names) {
            ObservedPeerKey key{local_it->callback_namespace, fields.remote_network_id, name, fields.data_type};
            for (const auto& previous : reverse) {
                if (DirectorySameObserved(key, previous.key))
                    return {std::nullopt, {DirectoryError::ReverseConflict, i, previous.route_index}};
            }
            reverse.emplace_back(key, i);
        }
    }

    std::shared_ptr<EndpointDirectory> directory(new EndpointDirectory);
    directory->config_ = config;
    directory->reverse_ = std::move(reverse);
    return {Snapshot(std::move(directory)), {}};
}

DirectoryResult<LocalBinding> EndpointDirectory::FindLocal(const Locator& local) const
{
    if (!LocatorCodec::IsValid(local))
        return {std::nullopt, {DirectoryError::InvalidLocator}};
    for (const auto& entry : config_.locals) {
        if (entry.local_locator == local) return {entry, {}};
    }
    return {std::nullopt, {DirectoryError::NotFound}};
}

DirectoryResult<Route> EndpointDirectory::ResolveOutbound(const RouteKey& key) const
{
    if (!LocatorCodec::IsValid(key.local_locator) || !LocatorCodec::IsValid(key.remote_locator))
        return {std::nullopt, {DirectoryError::InvalidLocator}};
    for (const auto& entry : config_.routes) {
        if (DirectorySameKey(entry.key, key)) return {entry, {}};
    }
    return {std::nullopt, {DirectoryError::NotFound}};
}

DirectoryResult<Route> EndpointDirectory::ResolveInbound(const ObservedPeerKey& observed) const
{
    if (!DirectoryBytesValid(observed.callback_namespace, 255) ||
            !DirectoryBytesValid(observed.remote_network_id, 64) ||
            !DirectoryBytesValid(observed.actual_remote_socket_name, 255))
        return {std::nullopt, {DirectoryError::InvalidString}};
    if (observed.actual_data_type != RecordDataType::Bytes)
        return {std::nullopt, {DirectoryError::UnsupportedDataType}};
    for (const auto& entry : reverse_) {
        if (DirectorySameObserved(entry.key, observed)) return {config_.routes[entry.route_index], {}};
    }
    return {std::nullopt, {DirectoryError::NotFound}};
}

DirectoryResult<std::vector<Route>> EndpointDirectory::RoutesTo(const Locator& remote) const
{
    if (!LocatorCodec::IsValid(remote))
        return {std::nullopt, {DirectoryError::InvalidLocator}};
    std::vector<Route> results(std::size_t{0});
    for (const auto& route : config_.routes) {
        if (route.key.remote_locator == remote) results.push_back(route);
    }
    if (results.empty()) return {std::nullopt, {DirectoryError::NotFound}};
    // Copy into the result so Debug STL proxy allocation can throw normally;
    // vector's noexcept move constructor cannot propagate this failure.
    return {results, {}};
}

DirectoryResult<EndpointDirectory::Snapshot> EndpointDirectory::WithUpdatedRoute(
        const RouteKey& key, std::uint64_t expected_version, const RouteFields& replacement) const
{
    if (!LocatorCodec::IsValid(key.local_locator) || !LocatorCodec::IsValid(key.remote_locator))
        return {std::nullopt, {DirectoryError::InvalidLocator}};
    for (std::size_t i = 0; i < config_.routes.size(); ++i) {
        const auto& current = config_.routes[i];
        if (!DirectorySameKey(current.key, key)) continue;
        if (expected_version != current.route_version)
            return {std::nullopt, {DirectoryError::StaleVersion, i}};
        if (current.route_version == (std::numeric_limits<std::uint64_t>::max)())
            return {std::nullopt, {DirectoryError::VersionExhausted, i}};
        auto replacement_config = config_;
        replacement_config.routes[i].fields = replacement;
        ++replacement_config.routes[i].route_version;
        return Build(replacement_config);
    }
    return {std::nullopt, {DirectoryError::NotFound}};
}

} // namespace mdds::dsoftbus
