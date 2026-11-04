#include <windows.h>
#include <string.h>
#include <process.h>
#include "contract.h"
static uint32_t constructed_phase;
static volatile LONG active;
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) {
    (void)instance;(void)reserved;
    if(reason==DLL_PROCESS_ATTACH) constructed_phase=dd_host_witness(DD_ATTACH);
    if(reason==DLL_PROCESS_DETACH) (void)dd_host_witness(DD_DETACH);
    return TRUE;
}
static int32_t DD_CALL run(const DD_Request* q,const DD_Host* h,DD_Result* r) {
    uintptr_t a,b;
    int32_t status=DD_OK;
    if(!q||!h||!r) return DD_INPUT;
    if(q->version!=DD_VERSION||h->version!=DD_VERSION||r->version!=DD_VERSION) return DD_ABI;
    if(q->size!=sizeof(*q)||h->size!=sizeof(*h)||r->size!=sizeof(*r)) return DD_SIZE;
    if(q->scope!=DD_SCOPE||h->scope!=DD_SCOPE||r->scope!=DD_SCOPE) return DD_BAD_SCOPE;
    if(q->ownership!=DD_BORROWED||q->cookie!=DD_COOKIE||h->cookie!=q->cookie||r->cookie!=q->cookie) return DD_OWNER;
    if(q->reserved||h->reserved||r->reserved||!h->context||!h->event||!h->cancelled||q->scenario>DD_HARD_EXIT) return DD_INPUT;
    if(q->input_bytes>256||(!q->input&&q->input_bytes)||!q->output) return DD_INPUT;
    if(q->output_capacity>256||q->output_capacity<q->input_bytes) return DD_CAPACITY;
    a=(uintptr_t)q->input;b=(uintptr_t)q->output;
    if(q->input_bytes>UINTPTR_MAX-a||q->output_capacity>UINTPTR_MAX-b) return DD_INPUT;
    if(q->input_bytes&&a<b+(uintptr_t)q->output_capacity&&b<a+(uintptr_t)q->input_bytes) return DD_ALIAS;
    if(InterlockedCompareExchange(&active,1,0)!=0) return DD_BUSY;
    h->event(h->context,DD_OPEN);
    if(q->scenario==DD_HARD_EXIT) _exit(90); /* Explicit Host no-Restore failure case. */
    if(h->cancelled(h->context)) status=DD_CANCEL;
    else if(q->scenario==DD_FAIL_STEP) status=DD_APP_ERROR;
    else if(q->input_bytes) memcpy(q->output,q->input,(size_t)q->input_bytes);
    h->event(h->context,DD_CLOSED);
    InterlockedExchange(&active,0);
    r->status=status;r->closed=1;r->output_bytes=status==DD_OK?q->input_bytes:0;
    return status;
}
DD_EXPORT int32_t DD_CALL dd_get_api(uint32_t version,uint32_t capacity,DD_Api* out) {
    DD_Api api;
    if(version!=DD_VERSION) return DD_ABI;
    if(capacity!=sizeof(api)) return DD_SIZE;
    if(!out) return DD_INPUT;
    if(constructed_phase!=2) return DD_CONTEXT;
    api.version=DD_VERSION;api.size=(uint32_t)sizeof(api);api.scope=DD_SCOPE;api.reserved=0;api.run=run;
    memcpy(out,&api,sizeof(api));return DD_OK;
}
/* Deliberately foreign pointer is rejected before invocation by the frontend. */
DD_EXPORT int32_t DD_CALL dd_get_api_foreign(uint32_t version,uint32_t capacity,DD_Api* out) {
    uint32_t (DD_CALL *witness)(uint32_t)=dd_host_witness;
    int32_t rc=dd_get_api(version,capacity,out);
    if(rc==DD_OK) memcpy(&out->run,&witness,sizeof(out->run));
    return rc;
}
