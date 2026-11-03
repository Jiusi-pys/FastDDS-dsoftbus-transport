#ifndef STAGE1_HOST_DELAYED_CONTRACT_H
#define STAGE1_HOST_DELAYED_CONTRACT_H
#include <stdint.h>
#include <stddef.h>
#if !defined(_WIN32)
#error Host Windows fixture only; no Native identity adapter implemented
#endif
#define DD_CALL __cdecl
#define DD_EXPORT __declspec(dllexport)
#if defined(DD_FRONTEND)
#define DD_WITNESS DD_EXPORT
#else
#define DD_WITNESS __declspec(dllimport)
#endif
#define DD_VERSION 1u
#define DD_SCOPE 0x484f5354u
#define DD_BORROWED 0x424f5252u
#define DD_COOKIE UINT64_C(0x484f535446495831)
#define DD_ACTIONBAG "mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16"
#define DD_PACKAGE "com.kaihong.mdds"
enum { DD_OK=0, DD_ABI=-1, DD_SIZE=-2, DD_BAD_SCOPE=-3, DD_OWNER=-4,
       DD_INPUT=-5, DD_CAPACITY=-6, DD_ALIAS=-7, DD_CANCEL=-8,
       DD_APP_ERROR=-9, DD_BUSY=-10, DD_CONTEXT=-11 };
enum { DD_SELECTED=10, DD_VERIFIED=20, DD_ATTACH=30, DD_LOADED=40,
       DD_API=50, DD_OPEN=60, DD_CLOSED=70, DD_RETURNED=80,
       DD_DETACH=90, DD_UNLOADED=100, DD_RESTORED=110 };
enum { DD_NORMAL=0, DD_FAIL_STEP=1, DD_HARD_EXIT=2 };
#ifdef __cplusplus
extern "C" {
#endif
/* Loader-lock-safe witness: bounded preallocated atomics only, no real identity. */
DD_WITNESS uint32_t DD_CALL dd_host_witness(uint32_t event);
typedef struct DD_Request {
    uint32_t version,size,scope,ownership;
    uint64_t cookie;
    const unsigned char* input;
    uint64_t input_bytes;
    unsigned char* output;
    uint64_t output_capacity;
    uint32_t scenario,reserved;
} DD_Request;
typedef struct DD_Result {
    uint32_t version,size,scope,reserved;
    uint64_t cookie;
    int32_t status;
    uint32_t closed;
    uint64_t output_bytes;
} DD_Result;
typedef struct DD_Host {
    uint32_t version,size,scope,reserved;
    uint64_t cookie;
    void* context;
    int32_t (DD_CALL *cancelled)(void*);
    void (DD_CALL *event)(void*,uint32_t);
} DD_Host;
typedef int32_t (DD_CALL *DD_Run)(const DD_Request*,const DD_Host*,DD_Result*);
typedef struct DD_Api {
    uint32_t version,size,scope,reserved;
    DD_Run run;
} DD_Api;
typedef int32_t (DD_CALL *DD_GetApi)(uint32_t,uint32_t,DD_Api*);
#ifdef __cplusplus
}
#endif
/* All buffers/tables are caller-owned and borrowed only during synchronous run.
 * No retained pointers, background work, C++ objects, exceptions or heap ownership
 * cross this Host-only ABI. API function pointer is usable only while DLL loaded.
 * Invalid metadata returns an error without writing result/output. */
#endif
