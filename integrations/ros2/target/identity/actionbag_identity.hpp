#pragma once
#include <cstdint>

namespace mdds_actionbag {
inline constexpr char identity_name[] = "mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16";
inline constexpr char permission_name[] = "ohos.permission.DISTRIBUTED_DATASYNC";

// The platform adapter alone constructs the fixed official provisioning schema.
// This flow never accepts names, permission arrays, APL, dcaps or ACL inputs.
class ActionbagIdentityApi {
public:
    virtual ~ActionbagIdentityApi() = default;
    virtual std::uint64_t GetSelf() = 0;
    virtual bool LegacyHealthy(char side) = 0;
    virtual std::uint32_t LookupNew() = 0;
    virtual bool ClaimAttempt() = 0;
    virtual std::uint64_t CreateExact() = 0;
    virtual int SetSelf(std::uint64_t token) = 0;
    virtual int Reload() = 0;
    virtual bool NewGranted(std::uint32_t token) = 0;
};

struct ActionbagResult {
    bool success = false, write_attempted = false, restore_attempted = false, restored = false;
    std::uint64_t token = 0;
    const char* stage = "INPUT";
};

inline ActionbagResult ActionbagProvision(ActionbagIdentityApi& api, char side) noexcept
{
    ActionbagResult result;
    std::uint64_t original = 0;
    bool switched = false;
    try {
        do {
            original = api.GetSelf();
            if ((side != 'A' && side != 'B') || !original) break;
            result.stage = "MANAGER_HEALTH";
            if (!api.LegacyHealthy(side)) break;
            result.stage = "NEW_ABSENCE";
            if (api.LookupNew() != 0) break;
            result.stage = "ONCE_MARKER";
            if (!api.ClaimAttempt()) break;
            result.stage = "CREATE_EXACT";
            result.write_attempted = true; // Never retry, including zero/error/exception.
            result.token = api.CreateExact();
            if (!result.token || result.token > UINT32_MAX) break;
            result.stage = "SELECT";
            switched = true; // A failed Set may still have changed process state.
            if (api.SetSelf(result.token) != 0 || api.GetSelf() != result.token) break;
            result.stage = "RELOAD";
            if (api.Reload() != 0) break;
            result.stage = "NEW_VERIFICATION";
            if (!api.NewGranted(static_cast<std::uint32_t>(result.token))) break;
            result.success = true;
            result.stage = "CREATED_VERIFIED";
        } while (false);
    } catch (...) { result.success = false; }
    try {
        if (switched) {
            result.restore_attempted = true;
            const int rc = api.SetSelf(original);
            result.restored = rc == 0 && api.GetSelf() == original;
        } else {
            result.restored = original != 0 && api.GetSelf() == original;
        }
    } catch (...) { result.restored = false; }
    result.success = result.success && result.restored;
    return result;
}
} // namespace mdds_actionbag
