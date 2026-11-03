#include "TokenBridge.h"

#include <accesstoken_kit.h>
#include <token_setproc.h>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

static_assert(std::is_standard_layout<MddsTokenBridgeResult>::value, "C layout required");
static_assert(std::is_trivially_copyable<MddsTokenBridgeResult>::value, "POD only");

extern "C" __attribute__((visibility("default")))
int32_t MddsTokenBridgeVerify(uint32_t expectedToken, MddsTokenBridgeResult* out) noexcept
{
    if (!out) return -1;
    *out = {};
    out->status = -2;
    if (!expectedToken) return out->status;
    using namespace OHOS::Security::AccessToken;
    out->nativeType = static_cast<int32_t>(TOKEN_NATIVE);
    out->normalApl = static_cast<int32_t>(APL_NORMAL);
    out->permissionGranted = static_cast<int32_t>(PERMISSION_GRANTED);
    try {
        out->called |= FASTDDS_DSOFTBUS_TOKEN_LOOKUP_CALLED;
        out->lookupToken = AccessTokenKit::GetNativeTokenId("fastdds_dsoftbus_probe");
        out->called |= FASTDDS_DSOFTBUS_TOKEN_TYPE_CALLED;
        out->type = static_cast<int32_t>(AccessTokenKit::GetTokenTypeFlag(expectedToken));
        NativeTokenInfo info{};
        out->called |= FASTDDS_DSOFTBUS_TOKEN_INFO_CALLED;
        out->infoRc = AccessTokenKit::GetNativeTokenInfo(expectedToken, info);
        if (info.processName.size() >= sizeof(out->processName) ||
                info.processName.find('\0') != std::string::npos) {
            out->status = -4;
            return out->status;
        }
        std::memcpy(out->processName, info.processName.data(), info.processName.size());
        out->apl = static_cast<int32_t>(info.apl);
        out->identityMatch = out->infoRc == 0 && info.processName == "fastdds_dsoftbus_probe" &&
                info.apl == APL_NORMAL;
        std::vector<int32_t> states;
        out->called |= FASTDDS_DSOFTBUS_TOKEN_PERMISSION_CALLED;
        out->permissionRc = AccessTokenKit::VerifyAccessToken(expectedToken,
                std::vector<std::string>{"ohos.permission.DISTRIBUTED_DATASYNC"}, states, true);
        if (states.size() > std::numeric_limits<uint32_t>::max()) {
            out->status = -3;
            return out->status;
        }
        out->permissionCount = static_cast<uint32_t>(states.size());
        if (states.size() == 1) out->permissionState = states.front();
        out->granted = out->lookupToken == expectedToken && out->type == out->nativeType &&
                out->identityMatch && out->permissionRc == 0 && states.size() == 1 &&
                states.front() == PERMISSION_GRANTED;
        out->status = 0;
    } catch (...) {
        out->status = -3;
        out->granted = 0;
    }
    return out->status;
}

extern "C" __attribute__((visibility("default")))
uint64_t MddsTokenBridgeGetSelfToken(void) noexcept
{
    return GetSelfTokenID();
}

extern "C" __attribute__((visibility("default")))
int32_t MddsTokenBridgeSetSelfToken(uint64_t token) noexcept
{
    return SetSelfTokenID(token);
}
