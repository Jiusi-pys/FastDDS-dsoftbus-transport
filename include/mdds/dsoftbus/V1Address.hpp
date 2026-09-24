#pragma once

#include <mdds/dsoftbus/EndpointDirectory.hpp>
#include <functional>
#include <string_view>

namespace mdds::dsoftbus::v1 {

// MDDS V1 wire tags, NOT the native Socket DataType numeric values (2/4/8).
enum class Profile : std::uint8_t { BytesNative = 1, RawPlain = 2, RawEncrypted = 3 };

struct ServiceAddress
{
    std::string name_space;
    std::uint32_t domain = 0;
    std::uint32_t slot = 0;
    Profile profile = Profile::BytesNative;
};

struct DeviceIdentity
{
    std::string name_space;
    std::uint32_t domain = 0;
    std::string udid;
};

struct EndpointAddress
{
    EndpointId device_key{};
    std::uint32_t slot = 0;
    Profile profile = Profile::BytesNative;
};

class AddressCodec final
{
public:
    // Project-private candidate. Not a registered interoperable RTPS kind.
    static constexpr std::int32_t kind = 0x40000000;
    static constexpr std::uint32_t max_slot = 0x00fffffe;
    static constexpr std::size_t max_namespace_bytes = 116;
    static constexpr std::size_t max_udid_bytes = 64;
    static constexpr std::size_t max_service_name_bytes = 255;

    // Opaque nonempty bytes, no embedded NUL; no case/Unicode normalization.
    static bool ValidIdentity(const DeviceIdentity& identity) noexcept;
    // SHA-256 input: ASCII "MDDS-V1-DEVICE", NUL, u16BE(namespace length),
    // namespace bytes, u32BE(domain), u16BE(UDID length), UDID bytes.
    // Keep the first 16 digest bytes unchanged, including FF/zero/::1 patterns.
    static bool DeriveKey(const DeviceIdentity& identity, EndpointId& output);
    static bool EncodeLocator(const EndpointAddress& address, Locator& output) noexcept;
    static bool DecodeLocator(const Locator& locator, EndpointAddress& output) noexcept;
    static bool Supports(const Locator& locator, Profile selected_profile) noexcept;

    // Exact canonical grammar: md1.<lowercase namespace hex>.<8 domain hex>
    // .<6 slot hex>.<2 profile hex>. All numbers big-endian display order.
    // Failed codecs leave output unchanged. Parsing accepts canonical text only.
    static bool EncodeServiceName(const ServiceAddress& address, std::string& output);
    static bool DecodeServiceName(std::string_view name, ServiceAddress& output);
};

enum class DirectoryStatus
{
    Ok, InvalidIdentity, InvalidNetworkId, Collision, Capacity,
    NotFound, Offline, StaleRoute
};

struct DeviceRoute
{
    DeviceIdentity identity;
    std::string network_id;
};

struct RouteResult
{
    DirectoryStatus status = DirectoryStatus::NotFound;
    std::optional<DeviceRoute> route;
};

// SDK-free, externally serialized logical table. It proves neither device
// authentication nor native callback ownership. networkId is never identity.
// Digest collisions poison the key for this table's lifetime, including its
// old route. Reconstructing the table is an explicit caller operation.
class DeviceDirectory final
{
public:
    using KeyDeriver = std::function<bool(const DeviceIdentity&, EndpointId&)>;
    explicit DeviceDirectory(std::size_t max_devices,
            KeyDeriver derive_key = AddressCodec::DeriveKey);
    DirectoryStatus Online(const DeviceIdentity& identity, const std::string& network_id);
    DirectoryStatus Offline(const DeviceIdentity& identity, const std::string& network_id);
    RouteResult Resolve(const EndpointId& key) const;
    std::size_t size() const noexcept;
private:
    struct Entry
    {
        EndpointId key;
        DeviceIdentity identity;
        std::string network_id;
        bool collision = false;
    };
    std::size_t maximum_;
    // Explicit injection seam for deterministic collision tests. Callers using
    // anything other than AddressCodec::DeriveKey do not produce MDDS V1 keys.
    KeyDeriver derive_key_;
    std::vector<Entry> entries_;
};

namespace detail {
// Internal portable primitive, exposed here for independent SHA-256 vectors.
std::array<std::uint8_t, 32> Sha256(std::string_view bytes);
}

} // namespace mdds::dsoftbus::v1
