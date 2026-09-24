#pragma once

#include <cstddef>
#include <string_view>

namespace mdds_probe {
// SDK-free probe policy. The native boundary asserts these ABI enum values
// against the actual Socket headers used for the device build.
inline constexpr int probe_bytes_data_type = 2;
inline constexpr int probe_raw_data_type = 4;
inline constexpr int probe_encrypted_raw_data_type = 8;

enum class ProbeProfile { Bytes, Raw, RawEncrypted };
enum class ProbeSendMethod { Bytes, Stream };

struct ProbeProfileDescription {
    const char* name;
    int socket_data_type;
    int peer_data_type;
    ProbeSendMethod send_method;
};

// An explicit invalid value is an error, never a request for the default.
bool ParseProbeProfile(std::string_view text, ProbeProfile& result) noexcept;
const ProbeProfileDescription* DescribeProbeProfile(ProbeProfile profile) noexcept;

// Checks the SDK's signed StreamData length before converting it to size_t.
// It does not dereference the buffer, copy it, or extend its lifetime.
bool ValidateProbeStreamFrame(bool has_data, const void* bytes, int length,
        std::size_t maximum) noexcept;

struct ProbeIdentityObservation {
    bool nul_terminated = false;
    bool valid = false;
    std::size_t length = 0;
};

// Qualification observation only. Does not retain or emit the identity bytes.
// A failed native query cannot turn an old buffer into a valid observation.
ProbeIdentityObservation ObserveProbeIdentity(int native_result, const void* bytes,
        std::size_t capacity) noexcept;
} // namespace mdds_probe
