#include <mdds/dsoftbus/V1Address.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace mdds::dsoftbus::v1 {
namespace {

bool ValidBytes(std::string_view value, std::size_t maximum) noexcept
{
    return !value.empty() && value.size() <= maximum && value.find('\0') == std::string_view::npos;
}

bool ValidProfile(Profile profile) noexcept
{
    return profile == Profile::BytesNative || profile == Profile::RawPlain || profile == Profile::RawEncrypted;
}

bool SameIdentity(const DeviceIdentity& first, const DeviceIdentity& second) noexcept
{
    return first.name_space == second.name_space && first.domain == second.domain && first.udid == second.udid;
}

void AppendBigEndian(std::string& output, std::uint32_t value, unsigned width)
{
    for (unsigned remaining = width; remaining != 0; --remaining)
        output.push_back(static_cast<char>((value >> ((remaining - 1) * 8)) & 0xff));
}

void AppendHex(std::string& output, std::uint32_t value, unsigned width)
{
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned remaining = width; remaining != 0; --remaining)
        output.push_back(digits[(value >> ((remaining - 1) * 4)) & 15]);
}

bool ReadHex(std::string_view text, std::uint32_t& value) noexcept
{
    if (text.empty() || text.size() > 8) return false;
    std::uint32_t parsed = 0;
    for (const auto c : text) {
        if (c >= '0' && c <= '9') parsed = (parsed << 4) | static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') parsed = (parsed << 4) | static_cast<unsigned>(c - 'a' + 10);
        else return false;
    }
    value = parsed;
    return true;
}

std::uint32_t Rotate(std::uint32_t value, unsigned bits) noexcept
{
    return (value >> bits) | (value << (32 - bits));
}

void ShaBlock(const std::uint8_t* input, std::array<std::uint32_t, 8>& state) noexcept
{
    // FIPS 180-4 SHA-256 round constants; byte loads avoid alignment/endian assumptions.
    constexpr std::array<std::uint32_t, 64> constants{{
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2}};
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i != 16; ++i) {
        words[i] = (static_cast<std::uint32_t>(input[i * 4]) << 24) |
            (static_cast<std::uint32_t>(input[i * 4 + 1]) << 16) |
            (static_cast<std::uint32_t>(input[i * 4 + 2]) << 8) | input[i * 4 + 3];
    }
    for (std::size_t i = 16; i != words.size(); ++i) {
        const auto s0 = Rotate(words[i - 15], 7) ^ Rotate(words[i - 15], 18) ^ (words[i - 15] >> 3);
        const auto s1 = Rotate(words[i - 2], 17) ^ Rotate(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    auto working = state;
    for (std::size_t i = 0; i != words.size(); ++i) {
        const auto s1 = Rotate(working[4], 6) ^ Rotate(working[4], 11) ^ Rotate(working[4], 25);
        const auto choose = (working[4] & working[5]) ^ (~working[4] & working[6]);
        const auto t1 = working[7] + s1 + choose + constants[i] + words[i];
        const auto s0 = Rotate(working[0], 2) ^ Rotate(working[0], 13) ^ Rotate(working[0], 22);
        const auto majority = (working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]);
        working = {{t1 + s0 + majority, working[0], working[1], working[2],
                working[3] + t1, working[4], working[5], working[6]}};
    }
    for (std::size_t i = 0; i != state.size(); ++i) state[i] += working[i];
}

} // namespace

std::array<std::uint8_t, 32> detail::Sha256(std::string_view bytes)
{
    std::array<std::uint32_t, 8> state{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    const auto* input = reinterpret_cast<const std::uint8_t*>(bytes.data());
    std::size_t offset = 0;
    while (bytes.size() - offset >= 64) {
        ShaBlock(input + offset, state); offset += 64;
    }
    std::array<std::uint8_t, 128> tail{};
    const auto remaining = bytes.size() - offset;
    // Avoid arithmetic on a null empty string_view data pointer.
    if (remaining != 0) std::copy_n(input + offset, remaining, tail.begin());
    tail[remaining] = 0x80;
    const auto padded_size = remaining < 56 ? std::size_t{64} : std::size_t{128};
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8;
    for (unsigned i = 0; i != 8; ++i)
        tail[padded_size - 1 - i] = static_cast<std::uint8_t>(bit_length >> (i * 8));
    ShaBlock(tail.data(), state);
    if (padded_size == 128) ShaBlock(tail.data() + 64, state);
    std::array<std::uint8_t, 32> result{};
    for (std::size_t i = 0; i != state.size(); ++i)
        for (unsigned j = 0; j != 4; ++j)
            result[i * 4 + j] = static_cast<std::uint8_t>(state[i] >> ((3 - j) * 8));
    return result;
}

bool AddressCodec::ValidIdentity(const DeviceIdentity& identity) noexcept
{
    return ValidBytes(identity.name_space, max_namespace_bytes) && ValidBytes(identity.udid, max_udid_bytes);
}

bool AddressCodec::DeriveKey(const DeviceIdentity& identity, EndpointId& output)
{
    if (!ValidIdentity(identity)) return false;
    std::string canonical("MDDS-V1-DEVICE", sizeof("MDDS-V1-DEVICE")); // Include the terminating NUL.
    AppendBigEndian(canonical, static_cast<std::uint32_t>(identity.name_space.size()), 2);
    canonical += identity.name_space;
    AppendBigEndian(canonical, identity.domain, 4);
    AppendBigEndian(canonical, static_cast<std::uint32_t>(identity.udid.size()), 2);
    canonical += identity.udid;
    const auto digest = detail::Sha256(canonical);
    std::copy_n(digest.begin(), output.size(), output.begin());
    return true;
}

bool AddressCodec::EncodeLocator(const EndpointAddress& address, Locator& output) noexcept
{
    if (!ValidProfile(address.profile) || address.slot > max_slot) return false;
    Locator encoded(kind, (static_cast<std::uint32_t>(address.profile) << 24) | (address.slot + 1));
    std::copy(address.device_key.begin(), address.device_key.end(), encoded.address);
    output = encoded;
    return true;
}

bool AddressCodec::DecodeLocator(const Locator& locator, EndpointAddress& output) noexcept
{
    if (locator.kind != kind) return false;
    const auto profile = static_cast<Profile>(locator.port >> 24);
    const auto encoded_slot = locator.port & 0x00ffffff;
    if (!ValidProfile(profile) || encoded_slot == 0) return false;
    EndpointAddress decoded{};
    decoded.profile = profile;
    decoded.slot = encoded_slot - 1;
    std::copy_n(locator.address, decoded.device_key.size(), decoded.device_key.begin());
    output = decoded;
    return true;
}

bool AddressCodec::Supports(const Locator& locator, Profile selected) noexcept
{
    EndpointAddress decoded;
    return DecodeLocator(locator, decoded) && decoded.profile == selected;
}

bool AddressCodec::EncodeServiceName(const ServiceAddress& address, std::string& output)
{
    if (!ValidBytes(address.name_space, max_namespace_bytes) ||
            address.slot > max_slot || !ValidProfile(address.profile)) return false;
    std::string encoded = "md1.";
    for (const auto byte : address.name_space) AppendHex(encoded, static_cast<unsigned char>(byte), 2);
    encoded += '.'; AppendHex(encoded, address.domain, 8);
    encoded += '.'; AppendHex(encoded, address.slot, 6);
    encoded += '.'; AppendHex(encoded, static_cast<std::uint8_t>(address.profile), 2);
    output = std::move(encoded);
    return true;
}

bool AddressCodec::DecodeServiceName(std::string_view name, ServiceAddress& output)
{
    if (name.size() < 25 || name.size() > max_service_name_bytes || name.substr(0, 4) != "md1.") return false;
    const auto separator = name.find('.', 4);
    if (separator == std::string_view::npos || (separator - 4) % 2 != 0 || separator + 19 != name.size() ||
            name[separator + 9] != '.' || name[separator + 16] != '.') return false;
    ServiceAddress decoded;
    decoded.name_space.reserve((separator - 4) / 2);
    std::uint32_t byte = 0;
    for (std::size_t i = 4; i != separator; i += 2) {
        if (!ReadHex(name.substr(i, 2), byte) || byte == 0) return false;
        decoded.name_space.push_back(static_cast<char>(byte));
    }
    std::uint32_t profile = 0;
    if (!ReadHex(name.substr(separator + 1, 8), decoded.domain) ||
            !ReadHex(name.substr(separator + 10, 6), decoded.slot) ||
            !ReadHex(name.substr(separator + 17, 2), profile)) return false;
    decoded.profile = static_cast<Profile>(profile);
    if (!ValidBytes(decoded.name_space, max_namespace_bytes) || decoded.slot > max_slot || !ValidProfile(decoded.profile)) return false;
    output = std::move(decoded);
    return true;
}

DeviceDirectory::DeviceDirectory(std::size_t maximum, KeyDeriver derive)
    : maximum_(maximum), derive_key_(std::move(derive)), entries_(std::size_t{0})
{
    if (!derive_key_) throw std::invalid_argument("DeviceDirectory requires a key deriver");
}

DirectoryStatus DeviceDirectory::Online(const DeviceIdentity& identity, const std::string& network_id)
{
    if (!AddressCodec::ValidIdentity(identity)) return DirectoryStatus::InvalidIdentity;
    if (!ValidBytes(network_id, 64)) return DirectoryStatus::InvalidNetworkId;
    EndpointId key{};
    if (!derive_key_(identity, key)) return DirectoryStatus::InvalidIdentity;
    for (auto& entry : entries_) {
        if (entry.key != key) continue;
        if (entry.collision || !SameIdentity(entry.identity, identity)) {
            entry.collision = true; entry.network_id.clear(); return DirectoryStatus::Collision;
        }
        entry.network_id = network_id;
        return DirectoryStatus::Ok;
    }
    if (entries_.size() >= maximum_) return DirectoryStatus::Capacity;
    entries_.push_back({key, identity, network_id, false});
    return DirectoryStatus::Ok;
}

DirectoryStatus DeviceDirectory::Offline(const DeviceIdentity& identity, const std::string& network_id)
{
    if (!AddressCodec::ValidIdentity(identity)) return DirectoryStatus::InvalidIdentity;
    if (!ValidBytes(network_id, 64)) return DirectoryStatus::InvalidNetworkId;
    EndpointId key{};
    if (!derive_key_(identity, key)) return DirectoryStatus::InvalidIdentity;
    for (auto& entry : entries_) {
        if (entry.key != key) continue;
        if (entry.collision || !SameIdentity(entry.identity, identity)) {
            entry.collision = true; entry.network_id.clear(); return DirectoryStatus::Collision;
        }
        if (entry.network_id != network_id) return DirectoryStatus::StaleRoute;
        entry.network_id.clear();
        return DirectoryStatus::Ok;
    }
    return DirectoryStatus::NotFound;
}

RouteResult DeviceDirectory::Resolve(const EndpointId& key) const
{
    for (const auto& entry : entries_) {
        if (entry.key != key) continue;
        if (entry.collision) return {DirectoryStatus::Collision, std::nullopt};
        if (entry.network_id.empty()) return {DirectoryStatus::Offline, std::nullopt};
        return {DirectoryStatus::Ok, DeviceRoute{entry.identity, entry.network_id}};
    }
    return {};
}

std::size_t DeviceDirectory::size() const noexcept { return entries_.size(); }
} // namespace mdds::dsoftbus::v1
