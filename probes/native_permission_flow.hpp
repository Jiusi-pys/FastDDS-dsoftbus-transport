#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace fastdds_dsoftbus_probe {

constexpr const char* native_probe_process_name = "fastdds_dsoftbus_probe";
constexpr const char* native_probe_apl = "normal";
constexpr const char* native_probe_permission = "ohos.permission.DISTRIBUTED_DATASYNC";

enum class NativePermissionMode { Grant, SocketCheck, Revoke, VerifyDenied };
// Explicit external JSON/ownership proof, while the service name is unregistered.
// This is not a successful service lookup and never proves DENIED.
constexpr int native_lookup_owned_unregistered = 1;

struct NativePermissionOptions
{
    NativePermissionMode mode = NativePermissionMode::SocketCheck;
    uint64_t expected_token = 0;
    // Only Revoke with a nonzero owned ID may recover an unregistered JSON entry.
    bool recover_unregistered = false;
};

struct TokenLookup
{
    // Zero is a successful identity query or explicit external confirmation of
    // first-use absence. RPC/query errors must never become {0, 0} implicitly.
    // {native_lookup_owned_unregistered, 0} requires explicit external ownership
    // verification and is accepted only by the special Revoke recovery option.
    int status = -1;
    uint64_t token = 0;
};

class NativePermissionApi
{
public:
    virtual ~NativePermissionApi() = default;
    virtual TokenLookup LookupProjectToken() = 0;
    virtual uint64_t GetSelfToken() = 0;
    // The concrete bridge must use only the fixed constants above, no dcaps or
    // ACLs, and exactly DATASYNC for grant or an empty permission list for revoke.
    // The caller separately owns JSON integrity/ownership preflight and backup.
    virtual uint64_t WriteProjectToken(bool grant) = 0;
    virtual int SetSelfToken(uint64_t token) = 0;
    virtual int ReloadNativeTokens() = 0;
    // True requires positive recognition of a native token; unknown/error=false.
    virtual bool IsNativeToken(uint64_t token) = 0;
    // 0=GRANTED, -1=DENIED. Only call after same-name/native identity validation.
    virtual int VerifyDatasync(uint64_t token) = 0;
    // Concrete bridge supplies a unique Socket name and validates its ownership.
    virtual int32_t CreateSocket() = 0;
    virtual void ShutdownSocket(int32_t socket) = 0;
};

struct NativePermissionResult
{
    int exit_code = 1;
    bool success = false;
    uint64_t token = 0;
    bool restored = false;
    // This host flow cannot attest JSON integrity/ownership or persistent
    // cleanup. The external runner must verify those separately after exit.
    bool persistent_state_confirmed = false;
    std::string reason;
};

// Synchronous, single-mode probe. No chained revoke and no hidden SDK/network
// calls. Exceptions from an injected API are reported and identity restoration
// is attempted if SetSelfToken was attempted, including failed switches.
NativePermissionResult RunNativePermissionProbe(const NativePermissionOptions& options,
        NativePermissionApi& api, std::ostream& log);

} // namespace fastdds_dsoftbus_probe
