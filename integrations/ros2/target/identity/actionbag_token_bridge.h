#pragma once
#include "token_bridge.h" // Reuse only the existing C/POD result layout.
#ifdef __cplusplus
extern "C" {
#define MDDS_ACTIONBAG_NOEXCEPT noexcept
#else
#define MDDS_ACTIONBAG_NOEXCEPT
#endif
// Queries only the approved exact ActionBag name; never provisions or reloads.
int32_t MddsActionbagTokenBridgeVerify(uint32_t token, MddsTokenBridgeResult* out) MDDS_ACTIONBAG_NOEXCEPT;
uint64_t MddsActionbagTokenBridgeGetSelfToken(void) MDDS_ACTIONBAG_NOEXCEPT;
int32_t MddsActionbagTokenBridgeSetSelfToken(uint64_t token) MDDS_ACTIONBAG_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#undef MDDS_ACTIONBAG_NOEXCEPT
