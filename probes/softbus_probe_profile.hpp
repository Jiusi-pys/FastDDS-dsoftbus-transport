#pragma once

#include <cstddef>
#include <string_view>
#include <string>
#include <vector>

namespace fastdds_dsoftbus_probe {
// Explicit private identity-output contract for the existing read-only query.
bool ValidIdentityExportPath(std::string_view) noexcept;
bool ValidIdentityExportBytes(const void*, std::size_t) noexcept;
struct ProbeIdentityCheckSummary {
    bool local_valid=false, complete=false;
    std::size_t remote_valid=0, remote_expected=0;
};
// The same dispatch loop is consumed by the real public GetNodeKeyInfo path.
// Explicit local export queries no peer UDID; route crossmatch stays separate.
template<class Query>
ProbeIdentityCheckSummary CheckProbeIdentitySet(const std::string& local,
        const std::vector<std::string>& peers,bool local_export,Query&& query)
{
    ProbeIdentityCheckSummary result;
    result.local_valid=query(local,"local",0);
    result.remote_expected=local_export?0:peers.size();
    for(std::size_t i=0;i<result.remote_expected;++i)
        if(query(peers[i],"remote",i))++result.remote_valid;
    result.complete=result.local_valid&&!peers.empty()&&result.remote_valid==result.remote_expected;
    return result;
}
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
} // namespace fastdds_dsoftbus_probe
