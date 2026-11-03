#include "../run_protocol.hpp"
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#ifdef _WIN32
#define EXPORTED extern "C" __declspec(dllexport)
#else
#error This is an explicitly marked Windows Host fixture, not the Native business module
#endif
struct NS_RunHandle {NS_RunRequest q;NS_RunControl c;std::atomic<bool> cancelled{false},executing{false};};
static std::mutex gate;static std::condition_variable changed;static NS_RunControl late[2]{};static std::atomic<int> active{0},handles{0},cancelling{0};
static unsigned late_refs=0;static bool hold_late=false,late_entered=false,slow_entered=false,cancel_entered=false,release_cancel=false;
static int32_t create(const NS_RunRequest* q,const NS_RunControl* c,NS_RunHandle** out){if(!q||!c||!out)return NS_BOUND;*out=nullptr;if(q->sample_count!=3||q->local.side>1||q->header.version!=NS_RUN_VERSION||q->header.size!=sizeof(*q))return NS_CONTEXT;*out=new NS_RunHandle{*q,*c};++handles;return NS_OK;}
static int32_t execute(NS_RunHandle* h,NS_RunResult* out){
    if(!h||!out)return NS_BOUND;++active;h->executing=true;
    struct Leave{NS_RunHandle* h;~Leave(){h->executing=false;--active;}} leave{h};
    int rc=NS_STATE;
    try{
        if(h->c.allow(h->c.context,h->q.deployment.deadline_ns)!=NS_OK)throw int(NS_AUTH);
        auto emit=[&](unsigned kind,unsigned ordinal=0){if(h->cancelled)throw int(NS_CANCELLED);NS_RunEvent e{};e.header={NS_RUN_VERSION,sizeof(e),NS_SCOPE,0};e.envelope=h->q.local;e.kind=kind;e.ordinal=ordinal;e.socket=100+static_cast<int32_t>(h->q.local.side);e.flags=NS_HOST_FIXTURE_LISTEN;e.writer_matches=e.reader_matches=1;
            const unsigned producer=kind==NS_TAKE?1-h->q.local.side:h->q.local.side;e.sequence=ordinal;e.writer_guid[0]=static_cast<uint8_t>(producer+1);const auto bytes=stage1_run::payload(h->q.local.run,producer,ordinal);std::memcpy(e.payload,bytes.data(),32);int code=h->c.emit(h->c.context,&e);if(code!=NS_OK)throw code;};
        auto wait=[&](unsigned phase){int code=h->c.wait(h->c.context,phase,h->q.deployment.deadline_ns);if(code!=NS_OK)throw code;};
        emit(NS_ROOT_LISTENER);wait(NS_CONNECT_RELEASE);emit(NS_MATCHED);wait(NS_SEND_RELEASE);
        for(unsigned i=1;i<=3;++i)emit(NS_WRITE,i);for(unsigned i=1;i<=3;++i)emit(NS_TAKE,i);
        emit(NS_ACK);wait(NS_DATA_CHECK_RELEASE);emit(NS_DATA_DONE);wait(NS_DELETE_RELEASE);emit(NS_TEARDOWN);rc=NS_OK;
    }catch(int code){rc=code;}catch(...){rc=NS_STATE;}
    {std::lock_guard<std::mutex> l(gate);late[h->q.local.side]=h->c;}
    *out={{NS_RUN_VERSION,sizeof(*out),NS_SCOPE,0},rc,rc==NS_OK?4u:0u,rc==NS_OK?3u:0u,rc==NS_OK?3u:0u,0,0,1,0};return rc;
}
static int32_t cancel(NS_RunHandle* h){if(!h)return NS_BOUND;h->cancelled=true;h->c.abort(h->c.context);return NS_OK;}
static int32_t destroy(NS_RunHandle* h){if(h&&h->executing)return NS_STATE;if(h)--handles;delete h;return NS_OK;}
EXPORTED int32_t ns_consumer_get_run_api(uint32_t v,uint32_t cap,NS_RunApi* out){if(v!=NS_RUN_VERSION)return NS_ABI;if(cap!=sizeof(NS_RunApi))return NS_SIZE;if(!out)return NS_BOUND;*out={{NS_RUN_VERSION,sizeof(*out),NS_SCOPE,0},create,execute,cancel,destroy};return NS_OK;}
EXPORTED int32_t ns_control_bad_api(uint32_t,uint32_t,NS_RunApi* out){*out={{999,sizeof(*out),NS_SCOPE,0},create,execute,cancel,destroy};return NS_OK;}
static int32_t partial(const NS_RunRequest* q,const NS_RunControl* c,NS_RunHandle** out){*out=new NS_RunHandle{*q,*c};++handles;return NS_STATE;}
EXPORTED int32_t ns_control_partial_api(uint32_t,uint32_t,NS_RunApi* out){*out={{NS_RUN_VERSION,sizeof(*out),NS_SCOPE,0},partial,execute,cancel,destroy};return NS_OK;}
static int32_t reject(const NS_RunRequest*,const NS_RunControl*,NS_RunHandle** out){*out=nullptr;return NS_AUTH;}
EXPORTED int32_t ns_control_reject_api(uint32_t,uint32_t,NS_RunApi* out){*out={{NS_RUN_VERSION,sizeof(*out),NS_SCOPE,0},reject,execute,cancel,destroy};return NS_OK;}
EXPORTED const char* ns_control_fixture_scope(){return "HOST_FIXTURE_V2_ONLY_NO_IDENTITY_NO_DDS_NO_SDK";}
EXPORTED int32_t ns_control_late(unsigned side,const NS_RunEvent* event){
    NS_RunControl c{};
    {std::lock_guard<std::mutex> l(gate);if(side>1||!late[side].emit)return NS_STATE;++late_refs;c=late[side];late_entered=true;changed.notify_all();}
    struct Ref {~Ref(){std::lock_guard<std::mutex> l(gate);--late_refs;changed.notify_all();}} reference;
    try{{std::unique_lock<std::mutex> l(gate);if(!changed.wait_for(l,std::chrono::seconds(2),[]{return !hold_late;}))return NS_STATE;}return c.emit(c.context,event);}catch(...){return NS_STATE;}
}
// This fixture controls all producer workers/references. Unlike SDK Shutdown,
// this explicit method proves this producer dropped its saved callback pointers.
EXPORTED int32_t ns_control_drop_all_callbacks(){std::lock_guard<std::mutex> l(gate);if(active||handles||cancelling||late_refs)return NS_STATE;late[0]={};late[1]={};return NS_OK;}
EXPORTED void ns_control_hold_late(){std::lock_guard<std::mutex> l(gate);hold_late=true;late_entered=false;}
EXPORTED int32_t ns_control_wait_late(){std::unique_lock<std::mutex> l(gate);return changed.wait_for(l,std::chrono::seconds(1),[]{return late_entered;})?NS_OK:NS_STATE;}
EXPORTED void ns_control_release_late(){std::lock_guard<std::mutex> l(gate);hold_late=false;changed.notify_all();}
static int32_t slow_execute(NS_RunHandle* h,NS_RunResult* out){
    ++active;h->executing=true;struct Exit {NS_RunHandle* h;~Exit(){h->executing=false;--active;}} exit{h};
    std::unique_lock<std::mutex> l(gate);slow_entered=true;changed.notify_all();const bool entered=changed.wait_for(l,std::chrono::seconds(1),[]{return cancel_entered;});
    const int code=entered?NS_CANCELLED:NS_STATE;*out={{NS_RUN_VERSION,sizeof(*out),NS_SCOPE,0},code,0,0,0,0,0,1,0};return code;
}
static int32_t slow_cancel(NS_RunHandle* h){
    ++cancelling;struct Exit{~Exit(){--cancelling;}} exit;
    {std::unique_lock<std::mutex> l(gate);cancel_entered=true;changed.notify_all();if(!changed.wait_for(l,std::chrono::seconds(2),[]{return release_cancel;}))return NS_STATE;}
    h->cancelled=true;h->c.abort(h->c.context);return NS_OK;
}
EXPORTED int32_t ns_control_slow_api(uint32_t,uint32_t,NS_RunApi* out){std::lock_guard<std::mutex> l(gate);slow_entered=false;cancel_entered=false;release_cancel=false;*out={{NS_RUN_VERSION,sizeof(*out),NS_SCOPE,0},create,slow_execute,slow_cancel,destroy};return NS_OK;}
EXPORTED int32_t ns_control_wait_execute(){std::unique_lock<std::mutex> l(gate);return changed.wait_for(l,std::chrono::seconds(1),[]{return slow_entered;})?NS_OK:NS_STATE;}
EXPORTED int32_t ns_control_wait_cancel(){std::unique_lock<std::mutex> l(gate);return changed.wait_for(l,std::chrono::seconds(1),[]{return cancel_entered;})?NS_OK:NS_STATE;}
EXPORTED void ns_control_release_cancel(){std::lock_guard<std::mutex> l(gate);release_cancel=true;changed.notify_all();}
