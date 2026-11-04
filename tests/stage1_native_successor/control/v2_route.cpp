#include "v2_route.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
namespace stage1_control {
// Origin-contained NONBUSINESS C/POD fixture. No library loader, identity,
// Transport Runtime, listener or DDS entity is reachable from these methods.
namespace {
struct Denial {NS_RunControl control;};
int32_t create(const NS_RunRequest* q,const NS_RunControl* c,NS_RunHandle** out){if(!q||!c||!out||!c->allow)return NS_BOUND;*out=nullptr;auto* h=static_cast<Denial*>(std::calloc(1,sizeof(Denial)));if(!h)return NS_STATE;h->control=*c;*out=reinterpret_cast<NS_RunHandle*>(h);return NS_OK;}
int32_t execute(NS_RunHandle* h,NS_RunResult* out){if(!h||!out)return NS_BOUND;auto* p=reinterpret_cast<Denial*>(h);const int denied=p->control.allow(p->control.context,0);*out={{NS_RUN_VERSION,sizeof(*out),NS_SCOPE,0},denied,0,0,0,0,0,1,0};return denied;}
int32_t cancel(NS_RunHandle* h){return h?NS_OK:NS_BOUND;}
int32_t destroy(NS_RunHandle* h){if(!h)return NS_BOUND;std::free(h);return NS_OK;}
int32_t getter(uint32_t version,uint32_t size,NS_RunApi* out){if(version!=NS_RUN_VERSION)return NS_ABI;if(size!=sizeof(NS_RunApi))return NS_SIZE;if(!out)return NS_BOUND;*out={{NS_RUN_VERSION,sizeof(NS_RunApi),NS_SCOPE,0},create,execute,cancel,destroy};return NS_OK;}
template<class F>const void* address(F f){const void* p=nullptr;static_assert(sizeof(f)==sizeof(p),"local C/POD code pointer");std::memcpy(&p,&f,sizeof(p));return p;}
bool owner(void*,const void* p){return p==address(getter)||p==address(create)||p==address(execute)||p==address(cancel)||p==address(destroy);}
}
int run_loaded_v2(std::shared_ptr<void> module,std::shared_ptr<Port> port,GetRunApi get,OwnerCheck check,void* context,const NS_RunRequest& q,NS_RunResult& result){
    if(!module||!port)return NS_OWNER;Client client(std::move(module),std::move(port),Clock::time_point(std::chrono::nanoseconds(q.deployment.deadline_ns)));int rc=client.bind(get,check,context);if(rc!=NS_OK)return rc;rc=client.create(q);if(rc!=NS_OK)return rc;return client.execute(result);
}
int native_v2_denial(){
    static_assert(sizeof(NS_RunApi)==48,"current consumer C/POD API");const auto end=Clock::now()+std::chrono::seconds(1);NS_RunRequest q{};q.header={NS_RUN_VERSION,sizeof(q),NS_SCOPE,0};q.sample_count=3;
    q.deployment.header={NS_VERSION,sizeof(NS_Request),NS_SCOPE,0};q.deployment.cookie=NS_COOKIE;q.deployment.ownership=NS_BORROWED;q.deployment.deadline_ns=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count());
    q.local.run=1;q.local.self_token=1;q.local.side=0;q.local.domain=151;q.local.pid=1;constexpr char board[]="3e01ff55454d202020104033bf453b00",space[]="NONBUSINESS_DENIAL_METADATA_ONLY";static_assert(sizeof(board)<=sizeof(q.local.board)&&sizeof(space)<=sizeof(q.local.name_space),"bounded fixture literals");std::memcpy(q.local.board,board,sizeof(board));std::memset(q.local.executable_sha,'a',64);std::memset(q.local.deployment_sha,'b',64);std::memcpy(q.local.name_space,space,sizeof(space));
    auto pipes=Pipe::local_pair();auto channel=std::make_shared<Channel>(pipes.a,q.local,Direction::Endpoint,end);auto port=std::make_shared<Port>(channel,q.local,false);auto module=std::make_shared<int>(0);NS_RunResult r{};
    const int rc=run_loaded_v2(module,port,getter,owner,nullptr,q,r);pipes.b->cancel();const bool closed=pipes.b->close(end),pin_required=!port->reclaimable();
    std::printf("V2_MECHANICAL_DENIAL raw=%d result=%d local_fixture=1 no_consumer_load=1 no_identity=1 no_SDK=1 no_READY=1 producer_context_requires_retention=%d\n",rc,r.status,int(pin_required));
    return rc==NS_AUTH&&r.status==NS_AUTH&&closed&&pin_required?0:4;
}
}
