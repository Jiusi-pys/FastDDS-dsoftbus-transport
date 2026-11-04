// A separate NONBUSINESS DLL build. Original fixture.cpp and its old artifact
// remain unchanged. Only the wrapped normal V2 getter/drop contract is qualified.
#define ns_consumer_get_run_api legacy_fixture_get_run_api
#define ns_control_drop_all_callbacks legacy_fixture_drop_all_callbacks
#include "fixture.cpp"
#undef ns_consumer_get_run_api
#undef ns_control_drop_all_callbacks
#include <new>
static int32_t safe_create(const NS_RunRequest* q,const NS_RunControl* c,NS_RunHandle** out){
    if(!out)return NS_BOUND;*out=nullptr;
    try{if(q&&q->reserved==0x53544658u)throw std::bad_alloc();return create(q,c,out);}
    catch(...){return *out?NS_UNLOAD:NS_STATE;}
}
static int32_t safe_execute(NS_RunHandle* h,NS_RunResult* out){
    try{return execute(h,out);}catch(...){if(out)*out={{NS_RUN_VERSION,sizeof(*out),NS_SCOPE,0},NS_UNLOAD,0,0,0,1,0,1,0};return NS_UNLOAD;}
}
static int32_t safe_cancel(NS_RunHandle* h){try{return cancel(h);}catch(...){return NS_UNLOAD;}}
static int32_t safe_destroy(NS_RunHandle* h){try{return destroy(h);}catch(...){return NS_UNLOAD;}}
EXPORTED int32_t ns_consumer_get_run_api(uint32_t v,uint32_t cap,NS_RunApi* out){
    try{if(v!=NS_RUN_VERSION)return NS_ABI;if(cap!=sizeof(NS_RunApi))return NS_SIZE;if(!out)return NS_BOUND;*out={{NS_RUN_VERSION,sizeof(*out),NS_SCOPE,0},safe_create,safe_execute,safe_cancel,safe_destroy};return NS_OK;}catch(...){return NS_STATE;}
}
EXPORTED int32_t ns_control_drop_all_callbacks(){try{return legacy_fixture_drop_all_callbacks();}catch(...){return NS_UNLOAD;}}
