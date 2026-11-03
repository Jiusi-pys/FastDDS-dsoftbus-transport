#include "v2_client.hpp"
#include <cstring>
#include <stdexcept>
namespace stage1_control {
namespace {
template<class F> const void* address(F f){static_assert(sizeof(F)==sizeof(void*),"qualified pointer ABI");const void* p=nullptr;std::memcpy(&p,&f,sizeof(p));return p;}
bool abi(const NS_Header& h,size_t n){return h.version==NS_RUN_VERSION&&h.size==n&&h.scope==NS_SCOPE&&!h.reserved;}
uint64_t nanos(Clock::time_point t){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());}
int status(Error e){return e==Error::None?NS_OK:e==Error::Cancelled||e==Error::Expired?NS_CANCELLED:e==Error::Envelope?NS_CONTEXT:NS_STATE;}
}
struct Port::State {
    std::mutex gate;std::condition_variable changed;bool sealed=false,fixture=false,ack=false;unsigned entered=0;std::atomic<bool> callback_error{false};
    std::shared_ptr<Channel> channel;NS_Envelope identity;
    State(std::shared_ptr<Channel> c,NS_Envelope e,bool f):fixture(f),channel(std::move(c)),identity(e){}
    struct Entry {State* s=nullptr;explicit Entry(State* p){std::lock_guard<std::mutex> l(p->gate);if(!p->sealed){s=p;++s->entered;}}~Entry(){if(s){std::lock_guard<std::mutex> l(s->gate);--s->entered;s->changed.notify_all();}}};
    static int32_t allow(void* p,uint64_t deadline){try{
        if(!p)return NS_CONTEXT;auto& s=*static_cast<State*>(p);Entry call(&s);if(!call.s)return NS_CANCELLED;
#ifdef STAGE1_CONTROL_HOST_FIXTURE
        // This definition exists only in the separately marked Windows fixture
        // driver. The Native frontend cannot be armed by argv/env/record.
        return s.fixture&&deadline==nanos(s.channel->deadline())&&Clock::now()<s.channel->deadline()?NS_OK:NS_AUTH;
#else
        (void)deadline;return NS_AUTH;
#endif
    }catch(...){if(p)static_cast<State*>(p)->callback_error=true;return NS_STATE;}}
    static int32_t emit(void* p,const NS_RunEvent* e){try{
        if(!p||!e)return NS_BOUND;auto& s=*static_cast<State*>(p);Entry call(&s);if(!call.s)return NS_CANCELLED;
        if(!abi(e->header,sizeof(*e))||!same_envelope(e->envelope,s.identity))return NS_CONTEXT;
        if(e->kind==NS_ROOT_LISTENER&&e->flags!=static_cast<uint32_t>(s.fixture?NS_HOST_FIXTURE_LISTEN:NS_REAL_ROOT_LISTEN_PUBLISHED))return NS_CONTEXT;
        Frame f;f.kind=Kind::Event;f.event=*e;return status(s.channel->send(f));
    }catch(...){if(p)static_cast<State*>(p)->callback_error=true;return NS_STATE;}}
    static int32_t wait(void* p,uint32_t release,uint64_t deadline){try{
        if(!p)return NS_BOUND;auto& s=*static_cast<State*>(p);Entry call(&s);if(!call.s)return NS_CANCELLED;
        if(deadline!=nanos(s.channel->deadline())||release<1||release>4)return NS_CONTEXT;
        Frame f;auto e=s.channel->receive(f);if(e!=Error::None)return status(e);
        if(f.kind==Kind::Abort)return NS_CANCELLED;
        return f.kind==Kind::Release&&f.release==release?NS_OK:NS_STATE;
    }catch(...){if(p)static_cast<State*>(p)->callback_error=true;return NS_STATE;}}
    static void abort(void* p){try{if(!p)return;auto& s=*static_cast<State*>(p);Entry call(&s);if(call.s)s.channel->cancel();}catch(...){if(p)static_cast<State*>(p)->callback_error=true;/* no foreign unwind; terminal fault prevents clean release */}}
};
Port::Port(std::shared_ptr<Channel> c,NS_Envelope e,bool fixture):state_(new State(std::move(c),e,fixture)){if(!state_->channel||!valid_envelope(e))throw std::invalid_argument("port");}
Port::~Port(){if(state_){(void)seal(Clock::now()+std::chrono::seconds(1));std::lock_guard<std::mutex> l(state_->gate);if(!state_->ack||state_->entered)(void)state_.release();}}
NS_RunControl Port::callbacks()const{return {{NS_RUN_VERSION,sizeof(NS_RunControl),NS_SCOPE,0},state_.get(),State::allow,State::emit,State::wait,State::abort};}
bool Port::seal(Clock::time_point end){if(!state_)return true;auto& s=*state_;{std::lock_guard<std::mutex> l(s.gate);s.sealed=true;}s.channel->cancel();std::unique_lock<std::mutex> l(s.gate);return s.changed.wait_until(l,end,[&]{return !s.entered;})&&!s.callback_error;}
bool Port::acknowledge_fixture_no_more_callbacks(){
#ifdef STAGE1_CONTROL_HOST_FIXTURE
    std::lock_guard<std::mutex> l(state_->gate);if(state_->fixture&&state_->sealed&&!state_->entered&&!state_->callback_error){state_->ack=true;return true;}
#endif
    return false;
}
bool Port::reclaimable()const{std::lock_guard<std::mutex> l(state_->gate);return state_->ack&&state_->sealed&&!state_->entered&&!state_->callback_error;}
bool Port::matches(const NS_Envelope& e)const{return same_envelope(state_->identity,e);}
struct Client::State {
    NS_RunApi api{};NS_RunHandle* handle=nullptr;std::shared_ptr<void> module;std::shared_ptr<Port> port;
    std::mutex gate;std::condition_variable changed;bool running=false,finishing=false;std::atomic<bool> retained{false};unsigned cancels=0;Clock::time_point end;
    State(std::shared_ptr<void> m,std::shared_ptr<Port> p,Clock::time_point e):module(std::move(m)),port(std::move(p)),end(e){}
};
Client::Client(std::shared_ptr<void> m,std::shared_ptr<Port> p,Clock::time_point end):state_(std::make_shared<State>(std::move(m),std::move(p),end)),pin_(new std::shared_ptr<State>(state_)){}
Client::~Client(){
    auto s=state_;std::lock_guard<std::mutex> l(s->gate);
    if(s->handle||s->running||s->retained||s->cancels||!s->port||!s->port->reclaimable())(void)pin_.release(); // ALL bookkeeping retained without allocation
}
bool Client::retained()const noexcept{return state_->retained.load();}
int Client::bind(GetRunApi getter,OwnerCheck check,void* context){
    auto s=state_;if(!s->module||!s->port||!getter||!check||!check(context,address(getter)))return NS_OWNER;
    NS_RunApi a{};int rc;try{rc=getter(NS_RUN_VERSION,sizeof(a),&a);}catch(...){return NS_ABI;}
    if(rc!=NS_OK||!abi(a.header,sizeof(a))||!a.create||!a.execute||!a.cancel||!a.destroy)return NS_ABI;
    if(!check(context,address(a.create))||!check(context,address(a.execute))||!check(context,address(a.cancel))||!check(context,address(a.destroy)))return NS_OWNER;
    s->api=a;return NS_OK;
}
int Client::create(const NS_RunRequest& q){
    auto s=state_;if(!s->api.create||s->handle||s->retained||Clock::now()>=s->end)return NS_STATE;auto control=s->port->callbacks();int rc;
    if(!abi(q.header,sizeof(q))||ns_header(&q.deployment.header,sizeof(q.deployment))||q.deployment.deadline_ns!=nanos(s->end)||q.deployment.cookie!=NS_COOKIE||q.deployment.ownership!=NS_BORROWED||q.sample_count!=3||!s->port->matches(q.local))return NS_CONTEXT;
    try{rc=s->api.create(&q,&control,&s->handle);}catch(...){s->retained=true;return NS_STATE;}
    if(rc!=NS_OK&&s->handle){s->retained=true;return NS_UNLOAD;}return rc==NS_OK&&!s->handle?NS_STATE:rc;
}
int Client::cancel(){
    auto s=state_;NS_RunHandle* h;{std::lock_guard<std::mutex> l(s->gate);if(!s->running||s->finishing||!s->handle)return NS_STATE;++s->cancels;h=s->handle;}
    int rc;try{rc=s->api.cancel(h);}catch(...){rc=NS_STATE;s->retained=true;}if(rc==NS_UNLOAD)s->retained=true;
    {std::lock_guard<std::mutex> l(s->gate);--s->cancels;s->changed.notify_all();}return rc;
}
int Client::execute(NS_RunResult& result){
    auto s=state_;{std::lock_guard<std::mutex> l(s->gate);if(!s->handle||s->running||s->finishing||s->retained)return NS_STATE;s->running=true;}
    result={{NS_RUN_VERSION,sizeof(result),NS_SCOPE,0},NS_STATE,0,0,0,0,0,1,0};int rc;
    try{rc=s->api.execute(s->handle,&result);}catch(...){rc=NS_STATE;s->retained=true;}
    {std::unique_lock<std::mutex> l(s->gate);s->finishing=true;if(!s->changed.wait_until(l,s->end,[&]{return s->cancels==0;}))s->retained=true;s->running=false;}
    if(!s->port->seal(s->end))s->retained=true;
    if(!abi(result.header,sizeof(result))||result.owners_retained||rc==NS_UNLOAD)s->retained=true;
    if(rc==NS_OK&&result.status!=NS_OK)rc=NS_STATE;
    if(!s->retained){int closed;try{closed=s->api.destroy(s->handle);}catch(...){closed=NS_STATE;}if(closed==NS_OK)s->handle=nullptr;else s->retained=true;}
    if(s->retained)return NS_UNLOAD;return rc;
}
}
