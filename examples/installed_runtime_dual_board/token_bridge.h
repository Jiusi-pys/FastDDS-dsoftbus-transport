/* VERBATIM COPY of experiments/fastdds_socket/TokenBridge.h
 * source sha256: cfb2615da8b8e47dff69e220b77410a51637edcb2c4d1db80991643bc8ee858e
 * repository HEAD at copy time: 507e31a91dce9d4f4165971fc922ba71f4884f80
 * This is the platform identity bridge ABI, NOT an MDDS public or probe header.
 * The installed-runtime consumer never includes MDDS private/probe headers. */
#ifndef MDDS_EXPERIMENT_TOKEN_BRIDGE_H
#define MDDS_EXPERIMENT_TOKEN_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
#define MDDS_TOKEN_NOEXCEPT noexcept
extern "C" {
#else
#define MDDS_TOKEN_NOEXCEPT
#endif

/* Experimental C/POD boundary between the platform and app C++ runtimes.
 * status belongs to this bridge, never to an SDK API. Native return values
 * are meaningful only when their corresponding called bit is present.
 * No token-file writes, permissions changes, strings or allocations cross it. */
enum MddsTokenBridgeCalled {
    MDDS_TOKEN_LOOKUP_CALLED = 1,
    MDDS_TOKEN_TYPE_CALLED = 2,
    MDDS_TOKEN_INFO_CALLED = 4,
    MDDS_TOKEN_PERMISSION_CALLED = 8
};

typedef struct MddsTokenBridgeResult {
    int32_t status;
    uint32_t called;
    uint32_t lookupToken;
    int32_t type;
    int32_t nativeType;
    int32_t infoRc;
    char processName[128];
    int32_t identityMatch;
    int32_t apl;
    int32_t normalApl;
    int32_t permissionRc;
    uint32_t permissionCount;
    int32_t permissionState;
    int32_t permissionGranted;
    int32_t granted;
} MddsTokenBridgeResult;

/* Fixed identity: fastdds_dsoftbus_probe, APL_NORMAL; exactly DATASYNC queried.
 * 0 means queries completed, not that identity or permission was accepted.
 * -1 invalid output, -2 zero token, -3 caught platform C++ exception,
 * -4 process name cannot be represented exactly as a bounded C string. */
int32_t MddsTokenBridgeVerify(uint32_t expectedToken, MddsTokenBridgeResult* out) MDDS_TOKEN_NOEXCEPT;
uint64_t MddsTokenBridgeGetSelfToken(void) MDDS_TOKEN_NOEXCEPT;
int32_t MddsTokenBridgeSetSelfToken(uint64_t token) MDDS_TOKEN_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#undef MDDS_TOKEN_NOEXCEPT
#endif
