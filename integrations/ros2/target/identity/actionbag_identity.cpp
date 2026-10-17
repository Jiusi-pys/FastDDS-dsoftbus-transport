// Explicit one-shot provisioning tool; never linked into a business process.
#include "actionbag_identity.hpp"
#include "actionbag_token_bridge.h"
#include <accesstoken_kit.h>
#include <nativetoken_kit.h>
#include <token_setproc.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

static_assert(sizeof(NativeTokenInfoParams) == 56 && offsetof(NativeTokenInfoParams, dcapsNum) == 0 &&
    offsetof(NativeTokenInfoParams, permsNum) == 4 && offsetof(NativeTokenInfoParams, aclsNum) == 8 &&
    offsetof(NativeTokenInfoParams, dcaps) == 16 &&
    offsetof(NativeTokenInfoParams, perms) == 24 && offsetof(NativeTokenInfoParams, acls) == 32 &&
    offsetof(NativeTokenInfoParams, processName) == 40 && offsetof(NativeTokenInfoParams, aplStr) == 48,
    "Require the hash-bound platform AArch64 no-uid provisioning ABI");

namespace mdds_actionbag {
using namespace OHOS::Security::AccessToken;
class ActionbagPlatform final : public ActionbagIdentityApi {
public:
    std::uint64_t GetSelf() override { return GetSelfTokenID(); }
    bool LegacyHealthy(char side) override {
        const AccessTokenID expected = side == 'A' ? 671793191u : 671320465u;
        std::uint32_t version = 0;
        const int version_rc = AccessTokenKit::GetVersion(version);
        const auto lookup = AccessTokenKit::GetNativeTokenId("fastdds_dsoftbus_probe");
        if (version_rc != 0 || lookup != expected) {
            std::printf("ACTIONBAG_HEALTH version_rc=%d legacy_lookup_matches=%d\n", version_rc, int(lookup == expected));
            return false;
        }
        NativeTokenInfo info{};
        const auto type = AccessTokenKit::GetTokenTypeFlag(expected);
        const int info_rc = AccessTokenKit::GetNativeTokenInfo(expected, info);
        std::vector<int32_t> states;
        const int query_rc = AccessTokenKit::VerifyAccessToken(expected,
            std::vector<std::string>{permission_name}, states, true);
        const bool known = query_rc == 0 && states.size() == 1 &&
            (states[0] == PERMISSION_GRANTED || states[0] == PERMISSION_DENIED);
        const bool healthy = type == TOKEN_NATIVE && info_rc == 0 &&
            info.processName == "fastdds_dsoftbus_probe" && info.apl == APL_NORMAL && known;
        std::printf("ACTIONBAG_HEALTH version_rc=%d legacy_lookup_matches=1 native=%d info_rc=%d name_matches=%d normal=%d query_rc=%d states=%zu healthy=%d\n",
            version_rc, int(type == TOKEN_NATIVE), info_rc, int(info_rc == 0 && info.processName == "fastdds_dsoftbus_probe"),
            int(info_rc == 0 && info.apl == APL_NORMAL), query_rc, states.size(), int(healthy));
        return healthy;
    }
    std::uint32_t LookupNew() override {
        const auto token = AccessTokenKit::GetNativeTokenId(identity_name);
        std::printf("ACTIONBAG_LOOKUP token=%u absent=%d\n", token, int(token == 0));
        return token;
    }
    bool ClaimAttempt() override {
        // Retained even after failure. An existing attempt is never removed or retried.
        const int fd = open("mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16.create-attempt",
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) return false;
        constexpr char contents[] = "schema=1\nprocessName=mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16\napl=normal\npermissions=ohos.permission.DISTRIBUTED_DATASYNC\ndcaps=0\nacls=0\nno_automatic_retry=1\n";
        const bool written = write(fd, contents, sizeof(contents) - 1) == static_cast<ssize_t>(sizeof(contents) - 1);
        const bool synced = fsync(fd) == 0;
        const bool closed = close(fd) == 0;
        return written && synced && closed;
    }
    std::uint64_t CreateExact() override {
        const char* permissions[] = {permission_name};
        NativeTokenInfoParams params{};
        params.processName = identity_name;
        params.aplStr = "normal";
        params.permsNum = 1;
        params.perms = permissions;
        std::printf("ACTIONBAG_WRITE attempt=1 processName=%s apl=normal permissions=1 dcaps=0 acls=0 persistent=1\n", identity_name);
        std::fflush(stdout);
        const auto token = GetAccessTokenId(&params); // Upsert, not atomic create-only.
        std::printf("ACTIONBAG_WRITE_RETURN token=%llu retry=PROHIBITED\n", static_cast<unsigned long long>(token));
        return token;
    }
    int SetSelf(std::uint64_t token) override { return SetSelfTokenID(token); }
    int Reload() override {
        const int rc = AccessTokenKit::ReloadNativeTokenInfo();
        std::printf("ACTIONBAG_RELOAD rc=%d\n", rc);
        return rc;
    }
    bool NewGranted(std::uint32_t token) override {
        MddsTokenBridgeResult observation{};
        const int rc = MddsActionbagTokenBridgeVerify(token, &observation);
        std::printf("ACTIONBAG_NEW token=%u bridge_rc=%d called=%u lookup_matches=%d native=%d info_rc=%d name_normal=%d permission_rc=%d states=%u permission=%d granted=%d\n",
            token, rc, observation.called, int(observation.lookupToken == token), int(observation.type == observation.nativeType),
            observation.infoRc, observation.identityMatch, observation.permissionRc, observation.permissionCount,
            observation.permissionState, observation.granted);
        return rc == 0 && observation.called == 15 && observation.granted == 1;
    }
};
} // namespace mdds_actionbag

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3 || (std::strcmp(argv[1], "--query") && std::strcmp(argv[1], "--create-once")) ||
            (std::strcmp(argv[2], "A") && std::strcmp(argv[2], "B"))) return 2;
    mdds_actionbag::ActionbagPlatform api;
    if (!std::strcmp(argv[1], "--create-once")) {
        const auto result = mdds_actionbag::ActionbagProvision(api, argv[2][0]);
        std::printf("ACTIONBAG_FINAL result=%s stage=%s token=%llu write_attempted=%d restore_attempted=%d restored=%d persistent_record_may_remain=%d retry=PROHIBITED\n",
            result.success ? "CREATED_VERIFIED" : "STOPPED", result.stage, static_cast<unsigned long long>(result.token),
            int(result.write_attempted), int(result.restore_attempted), int(result.restored), int(result.write_attempted));
        return result.success ? 0 : 3;
    }
    const auto original = api.GetSelf();
    bool observed = false, absent = false;
    try {
        if (original && api.LegacyHealthy(argv[2][0])) {
            const auto token = api.LookupNew();
            absent = token == 0;
            observed = absent || api.NewGranted(token);
        }
    } catch (...) { observed = false; }
    const bool unchanged = api.GetSelf() == original;
    std::printf("ACTIONBAG_QUERY_FINAL observed=%d new_absent=%d self_unchanged=%d mutation_calls=0\n", int(observed), int(absent), int(unchanged));
    return observed && unchanged ? 0 : 3;
}
