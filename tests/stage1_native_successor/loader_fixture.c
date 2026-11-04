#include "native_contract.h"
#include <time.h>
static uint32_t constructed;
static uint64_t construct_ns;
static NS_LoaderSlot* borrowed;
static uint64_t ticks(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
__attribute__((constructor)) static void attach(void){constructed=1;construct_ns=ticks();}
__attribute__((destructor)) static void detach(void){if(borrowed){borrowed->dtor_count=1;borrowed->dtor_ns=ticks();}}
static int32_t snapshot(NS_LoaderSlot* out){
    int rc;if(!out)return NS_BOUND;rc=ns_header(&out->header,sizeof(*out));if(rc)return rc;
    if(!constructed||!construct_ns)return NS_STATE;
    out->ctor_count=constructed;out->ctor_ns=construct_ns;return NS_OK;
}
static int32_t bind_slot(NS_LoaderSlot* slot){
    int rc;if(!slot)return NS_BOUND;rc=ns_header(&slot->header,sizeof(*slot));if(rc)return rc;
    if(slot->ownership!=NS_BORROWED||slot->cookie!=NS_COOKIE||slot->reserved)return NS_OWNER;
    if(borrowed)return NS_STATE;borrowed=slot;return snapshot(slot);
}
NS_EXPORT int32_t ns_loader_get_api(uint32_t version,uint32_t capacity,NS_LoaderApi* out){
    NS_LoaderApi api={{NS_VERSION,sizeof(NS_LoaderApi),NS_SCOPE,0},bind_slot,snapshot};
    if(version!=NS_VERSION)return NS_ABI;if(capacity!=sizeof(api))return NS_SIZE;if(!out)return NS_BOUND;*out=api;return NS_OK;
}
