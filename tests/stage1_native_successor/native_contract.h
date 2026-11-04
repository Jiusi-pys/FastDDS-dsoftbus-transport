#ifndef STAGE1_NATIVE_SUCCESSOR_CONTRACT_H
#define STAGE1_NATIVE_SUCCESSOR_CONTRACT_H
#include <stdint.h>
#include <stddef.h>
#define NS_EXPORT __attribute__((visibility("default")))
#define NS_VERSION 1u
#define NS_SCOPE 0x4e415456u
#define NS_BORROWED 0x424f5252u
#define NS_COOKIE UINT64_C(0x4e415449564531)
#define NS_ACTIONBAG "mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16"
#define NS_PACKAGE "com.kaihong.mdds"
#define NS_EXTERNAL_APPROVAL 0x41505052u
#define NS_STR_INNER(x) #x
#define NS_STR(x) NS_STR_INNER(x)
enum {NS_OK=0,NS_ABI=-1,NS_SIZE=-2,NS_CONTEXT=-3,NS_OWNER=-4,NS_BOUND=-5,
      NS_QUERY=-6,NS_NOT_READY=-7,NS_AUTH=-8,NS_STATE=-9,NS_CANCELLED=-10,NS_UNLOAD=-11};
typedef struct NS_Header {uint32_t version,size,scope,reserved;} NS_Header;
typedef struct NS_Observation {
    NS_Header header;uint64_t self;uint32_t lookup;
    int32_t type,native_type,info_rc,apl,normal_apl,query_rc;
    uint32_t query_count;int32_t query_state,granted;uint32_t called;
    char process_name[128];char cpp_abi[8];
} NS_Observation;
typedef struct NS_IdentityApi {NS_Header header;int32_t (*query)(uint32_t,NS_Observation*);} NS_IdentityApi;
typedef int32_t (*NS_GetIdentityApi)(uint32_t,uint32_t,NS_IdentityApi*);
typedef struct NS_LoaderSlot {
    NS_Header header;uint64_t cookie;uint32_t ownership,ctor_count,dtor_count,reserved;
    uint64_t ctor_ns,dtor_ns;
} NS_LoaderSlot;
typedef struct NS_LoaderApi {NS_Header header;int32_t (*bind)(NS_LoaderSlot*);int32_t (*snapshot)(NS_LoaderSlot*);} NS_LoaderApi;
typedef int32_t (*NS_GetLoaderApi)(uint32_t,uint32_t,NS_LoaderApi*);
typedef struct NS_Request {
    NS_Header header;uint64_t cookie,deadline_ns;uint32_t ownership,external_approval;
    char name_space[117],udid[129],peer_udid[129],peer_network[65],package_name[65],prefix[96],initiator[128],peer_initiator[128];
    uint32_t domain,slot,peer_slot;
} NS_Request;
typedef struct NS_Control {
    NS_Header header;uint64_t cookie;void* context;
    int32_t (*allow)(void*,uint32_t,uint64_t);int32_t (*cancelled)(void*);
} NS_Control;
typedef struct NS_Result {NS_Header header;int32_t stage,closed;uint32_t quiescence_unproven;int32_t status;} NS_Result;
typedef struct NS_ConsumerApi {
    NS_Header header;int32_t (*describe)(char*,uint32_t);
    int32_t (*prepare)(const NS_Request*,const NS_Control*,NS_Result*);
} NS_ConsumerApi;
typedef int32_t (*NS_GetConsumerApi)(uint32_t,uint32_t,NS_ConsumerApi*);
/* Fixed C/POD, caller-owned storage and synchronous borrowed buffers/context.
 * No C++ exception/object/string/vector/allocation ownership crosses this ABI.
 * No API/function pointer remains usable after the owning DSO is unloaded.
 * External approval flags are operator assertions, not an OS grant or proof of
 * user authorization. No persistent identity creation/change is provided. */
#ifdef __cplusplus
extern "C" {
#endif
int ns_c_layout_check(void);
#ifdef __cplusplus
}
#endif
static inline int ns_header(const NS_Header* p,uint32_t size) {
    if(!p)return NS_BOUND;
    if(p->version!=NS_VERSION)return NS_ABI;
    if(p->size!=size)return NS_SIZE;
    if(p->scope!=NS_SCOPE||p->reserved)return NS_CONTEXT;
    return NS_OK;
}
#endif
