#include "actionbag_token_bridge.h"
#include "actionbag_identity.hpp"
#include <accesstoken_kit.h>
#include <token_setproc.h>
#include <cstring>
#include <vector>

extern "C" __attribute__((visibility("default")))
int32_t MddsActionbagTokenBridgeVerify(uint32_t token, MddsTokenBridgeResult* out) noexcept
{
    if (!out) return -1;
    *out = {};
    out->status = -2;
    if (!token) return out->status;
    using namespace OHOS::Security::AccessToken;
    out->nativeType = TOKEN_NATIVE;
    out->normalApl = APL_NORMAL;
    out->permissionGranted = PERMISSION_GRANTED;
    try {
        out->called |= FASTDDS_DSOFTBUS_TOKEN_LOOKUP_CALLED;
        out->lookupToken = AccessTokenKit::GetNativeTokenId(mdds_actionbag::identity_name);
        out->called |= FASTDDS_DSOFTBUS_TOKEN_TYPE_CALLED;
        out->type = AccessTokenKit::GetTokenTypeFlag(token);
        NativeTokenInfo info{};
        out->called |= FASTDDS_DSOFTBUS_TOKEN_INFO_CALLED;
        out->infoRc = AccessTokenKit::GetNativeTokenInfo(token, info);
        if (info.processName.size() >= sizeof(out->processName) || info.processName.find('\0') != std::string::npos)
            return out->status = -4;
        std::memcpy(out->processName, info.processName.data(), info.processName.size());
        out->apl = info.apl;
        out->identityMatch = out->infoRc == 0 && info.processName == mdds_actionbag::identity_name && info.apl == APL_NORMAL;
        std::vector<int32_t> states;
        out->called |= FASTDDS_DSOFTBUS_TOKEN_PERMISSION_CALLED;
        out->permissionRc = AccessTokenKit::VerifyAccessToken(token,
            std::vector<std::string>{mdds_actionbag::permission_name}, states, true);
        if (states.size() > UINT32_MAX) return out->status = -3;
        out->permissionCount = static_cast<uint32_t>(states.size());
        if (states.size() == 1) out->permissionState = states[0];
        out->granted = out->lookupToken == token && out->type == out->nativeType && out->identityMatch &&
            out->permissionRc == 0 && states.size() == 1 && states[0] == PERMISSION_GRANTED;
        out->status = 0;
    } catch (...) { out->status = -3; out->granted = 0; }
    return out->status;
}

extern "C" __attribute__((visibility("default")))
uint64_t MddsActionbagTokenBridgeGetSelfToken(void) noexcept { return GetSelfTokenID(); }
extern "C" __attribute__((visibility("default")))
int32_t MddsActionbagTokenBridgeSetSelfToken(uint64_t token) noexcept { return SetSelfTokenID(token); }
