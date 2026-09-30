#ifndef FASTDDS_DSOFTBUS_DSOFTBUS_ENDPOINT_DIRECTORY_HPP
#define FASTDDS_DSOFTBUS_DSOFTBUS_ENDPOINT_DIRECTORY_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fastdds_dsoftbus/dsoftbus/FastddsApi.hpp>

namespace fastdds_dsoftbus::dsoftbus {

using Locator = fastdds_dsoftbus::binding::Locator_t;
using EndpointId = std::array<std::uint8_t, 16>;

// Project-private candidate, not an assigned interoperable RTPS transport kind.
class LocatorCodec final
{
public:
    static constexpr std::int32_t kind = 0x40000000;
    static constexpr std::uint32_t port = 1;
    // Reject zero, ::1 and addresses beginning FF: current Fast DDS IP helpers
    // would otherwise classify private addresses as unspecified/local/multicast.
    static bool Encode(const EndpointId& endpoint, Locator& output);
    static bool Decode(const Locator& locator, EndpointId& output);
    static bool IsValid(const Locator& locator);
};

// Independent value type, not an SDK enum/ABI dependency. An SDK adapter must
// explicitly translate and reject unsupported observed values.
enum class RecordDataType { Bytes, Unsupported };

struct LocalBinding
{
    Locator local_locator;
    std::string callback_namespace;
    std::string local_listen_name;
};

struct RouteKey
{
    Locator local_locator;
    Locator remote_locator;
};

struct RouteFields
{
    std::string remote_network_id;
    std::string remote_listen_name;
    std::string local_initiator_name;
    std::vector<std::string> expected_remote_initiator_names;
    RecordDataType data_type = RecordDataType::Bytes;
};

struct Route
{
    RouteKey key;
    RouteFields fields;
    // Configuration supplies a nonzero starting version; a fresh route uses 1.
    // A version is a stale-task guard, not authentication or a Socket generation.
    std::uint64_t route_version = 1;
};

struct ObservedPeerKey
{
    std::string callback_namespace;
    std::string remote_network_id;
    std::string actual_remote_socket_name;
    RecordDataType actual_data_type = RecordDataType::Bytes;
};

struct DirectoryConfiguration
{
    std::vector<LocalBinding> locals;
    std::vector<Route> routes;
};

enum class DirectoryError
{
    None,
    InvalidLocator,
    InvalidString,
    UnsupportedDataType,
    MissingLocal,
    DuplicateLocal,
    DuplicateRoute,
    DuplicateAlias,
    ReverseConflict,
    NamespaceConflict,
    InvalidVersion,
    NotFound,
    StaleVersion,
    VersionExhausted
};

struct DirectoryIssue
{
    DirectoryError code = DirectoryError::None;
    // For Build: indexes in locals for local errors, routes for route errors.
    // The second index is provided for a duplicate/conflict when applicable.
    std::size_t entry = static_cast<std::size_t>(-1);
    std::size_t conflicting_entry = static_cast<std::size_t>(-1);
};

template<typename T>
struct DirectoryResult
{
    std::optional<T> value;
    DirectoryIssue issue;
};

class EndpointDirectory final
{
public:
    using Snapshot = std::shared_ptr<const EndpointDirectory>;

    // Transactional construction. Strings are opaque, exact byte sequences:
    // namespace/name 1..255 bytes, network ID 1..64 bytes; no embedded NUL.
    // An empty directory is valid but resolves nothing. No SDK calls are made.
    static DirectoryResult<Snapshot> Build(const DirectoryConfiguration& config);

    DirectoryResult<LocalBinding> FindLocal(const Locator& local) const;
    DirectoryResult<Route> ResolveOutbound(const RouteKey& key) const;
    DirectoryResult<Route> ResolveInbound(const ObservedPeerKey& observed) const;
    // Returns ALL local choices. Never chooses a local endpoint implicitly.
    DirectoryResult<std::vector<Route>> RoutesTo(const Locator& remote) const;

    // Key and callback namespace cannot be changed by this API. A new snapshot
    // is returned only after full validation. Existing snapshots remain valid.
    // Atomic runtime publication/connection migration are deliberately external.
    DirectoryResult<Snapshot> WithUpdatedRoute(
            const RouteKey& key, std::uint64_t expected_version,
            const RouteFields& replacement) const;

    EndpointDirectory(const EndpointDirectory&) = delete;
    EndpointDirectory& operator=(const EndpointDirectory&) = delete;
    EndpointDirectory(EndpointDirectory&&) = delete;
    EndpointDirectory& operator=(EndpointDirectory&&) = delete;

private:
    // Explicitly use throwing empty-vector construction for Debug iterator
    // proxies, so an allocation failure can reach the caller's handler.
    EndpointDirectory()
        : config_{std::vector<LocalBinding>(std::size_t{0}), std::vector<Route>(std::size_t{0})}
        , reverse_(std::initializer_list<ReverseEntry>{})
    {
    }
    struct ReverseEntry
    {
        ReverseEntry(const ObservedPeerKey& observed, std::size_t index)
            : key(observed), route_index(index)
        {
        }
        // Copy on vector growth: MSVC Debug string moves are noexcept but
        // allocate iterator proxies. Their failure must propagate, not terminate.
        ReverseEntry(const ReverseEntry&) = default;
        ObservedPeerKey key;
        std::size_t route_index;
    };
    DirectoryConfiguration config_;
    std::vector<ReverseEntry> reverse_;
};

// This directory proves routing uniqueness, not peer authentication, actual
// callback namespace recovery, record delivery, or callback/Socket lifecycle.

} // namespace fastdds_dsoftbus::dsoftbus
#endif
