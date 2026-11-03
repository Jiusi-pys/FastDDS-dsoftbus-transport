#ifndef STAGE1_CONSUMER_RUN_CONTRACT_H
#define STAGE1_CONSUMER_RUN_CONTRACT_H
#include "native_contract.h"
#define NS_RUN_VERSION 2u
enum { NS_ROOT_LISTENER=1,NS_MATCHED=2,NS_WRITE=3,NS_TAKE=4,NS_ACK=5,NS_DATA_DONE=6,NS_TEARDOWN=7 };
enum { NS_CONNECT_RELEASE=1,NS_SEND_RELEASE=2,NS_DELETE_RELEASE=3,NS_DATA_CHECK_RELEASE=4 };
enum { NS_REAL_ROOT_LISTEN_PUBLISHED=1,NS_HOST_FIXTURE_LISTEN=2 };
typedef struct NS_Envelope {
    uint64_t run,self_token;
    uint32_t side,domain,pid;
    char board[40],executable_sha[65],deployment_sha[65],name_space[117];
} NS_Envelope;
typedef struct NS_RunEvent {
    NS_Header header;NS_Envelope envelope;
    uint32_t kind,ordinal;int64_t sequence;
    uint8_t writer_guid[16],payload[32];
    int32_t socket,listen_rc;uint32_t flags,writer_matches,reader_matches;
} NS_RunEvent;
typedef struct NS_RunRequest {
    NS_Header header;NS_Request deployment;
    NS_Envelope local,peer;uint32_t sample_count,reserved;
} NS_RunRequest;
typedef struct NS_RunControl {
    NS_Header header;void* context;
    int32_t (*allow)(void*,uint64_t);
    int32_t (*emit)(void*,const NS_RunEvent*);
    int32_t (*wait)(void*,uint32_t,uint64_t);
    void (*abort)(void*);
} NS_RunControl;
typedef struct NS_RunResult {
    NS_Header header;int32_t status;uint32_t phase,writes,takes;
    uint32_t owners_retained,unload_safe,quiescence_unproven,reserved;
} NS_RunResult;
typedef struct NS_RunHandle NS_RunHandle;
typedef struct NS_RunApi {
    NS_Header header;
    int32_t (*create)(const NS_RunRequest*,const NS_RunControl*,NS_RunHandle**);
    int32_t (*execute)(NS_RunHandle*,NS_RunResult*);
    int32_t (*cancel)(NS_RunHandle*);
    int32_t (*destroy)(NS_RunHandle*);
} NS_RunApi;
#ifdef __cplusplus
extern "C" {
#endif
int ns_run_c_layout_check(void);
#ifdef __cplusplus
}
#endif
/* Synchronous borrowed callbacks/storage until execute returns. allow is an
 * operator attestation, never OS authorization. wait honors the ONE absolute
 * CLOCK_MONOTONIC deadline and concurrent nonblocking abort; no lifecycle or
 * execute/cancel reentry from callbacks. No callbacks occur after execute return.
 * Handle lifetime must exclude concurrent destroy and any other API call.
 * Failed deletion pins the complete session/DSO; destroy returns NS_UNLOAD.
 * Even successful teardown is NOT SDK callback quiescence: this business DSO
 * must remain mapped until process exit. Current frontend never loads/runs it.
 * No identity API is provided. C/POD and caller-owned synchronous event buffers.
 */
#endif
