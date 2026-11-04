#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "goal_control.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <cstring>
#include <stdexcept>
namespace stage1_goal {
namespace {
bool header(const NS_Header& h,size_t n){return h.version==NS_RUN_VERSION&&h.size==n&&h.scope==NS_SCOPE&&!h.reserved;}
Clock::time_point end_at(uint64_t n){return Clock::time_point(std::chrono::nanoseconds(n));}
std::string side(const NS_RunRequest& q){return q.local.side?"B":"A";}
}
GoalControl::Entry::Entry(GoalControl* s){if(!s)return;std::lock_guard<std::mutex> l(s->gate_);if(!s->sealed_){p=s;++s->entered_;}}
GoalControl::Entry::~Entry(){if(p){std::lock_guard<std::mutex> l(p->gate_);--p->entered_;p->changed_.notify_all();}}
GoalControl::GoalControl(Files& f,Scalar& s,const NS_RunRequest& q):files_(f),scalar_(s),request_(q),entry_pid_(getpid()),entry_tid_(static_cast<uint64_t>(syscall(SYS_gettid))){
    if(!stage1_control::valid_envelope(q.local)||q.local.pid!=entry_pid_||entry_pid_!=entry_tid_||s.get()!=q.local.self_token||now_ns()>=q.deployment.deadline_ns)throw std::invalid_argument("GOAL_SELECTED_CONTEXT");
    int p[2];if(pipe2(p,O_NONBLOCK|O_CLOEXEC))throw std::runtime_error("GOAL_CANCEL_PIPE");wake_read_=p[0];wake_write_=p[1];
}
GoalControl::~GoalControl(){if(watch_.joinable())std::terminate();if(wake_read_>=0)close(wake_read_);if(wake_write_>=0)close(wake_write_);}
bool GoalControl::selected(bool main_only)const{return !cancelled_&&now_ns()<request_.deployment.deadline_ns&&uint64_t(getpid())==entry_pid_&&(!main_only||uint64_t(syscall(SYS_gettid))==entry_tid_)&&scalar_.get()==request_.local.self_token;}
void GoalControl::wake()noexcept{const char b=1;if(wake_write_>=0){const auto ignored=write(wake_write_,&b,1);(void)ignored;}}
NS_RunControl GoalControl::callbacks(){return {{NS_RUN_VERSION,sizeof(NS_RunControl),NS_SCOPE,0},this,allow,emit,wait,abort};}
int32_t GoalControl::allow(void* p,uint64_t n){Entry e(static_cast<GoalControl*>(p));if(!e.p)return NS_CANCELLED;try{return n==e.p->request_.deployment.deadline_ns&&e.p->selected(true)?NS_OK:NS_AUTH;}catch(...){e.p->fault_=true;return NS_STATE;}}
int32_t GoalControl::emit(void* p,const NS_RunEvent* event){Entry call(static_cast<GoalControl*>(p));if(!call.p)return NS_CANCELLED;auto& s=*call.p;try{
    if(!event||!header(event->header,sizeof(*event))||!stage1_control::same_envelope(event->envelope,s.request_.local)||!s.selected(false))return NS_CONTEXT;
    if(event->kind<NS_ROOT_LISTENER||event->kind>NS_TEARDOWN)return NS_STATE;
    if(event->kind==NS_ROOT_LISTENER&&(event->flags!=NS_REAL_ROOT_LISTEN_PUBLISHED||event->socket<0||event->listen_rc))return NS_CONTEXT;
    std::lock_guard<std::mutex> lock(s.output_gate_);if(s.tx_>stage1_control::frame_count_limit)return NS_BOUND;
    stage1_control::Frame f;f.kind=stage1_control::Kind::Event;f.direction=stage1_control::Direction::Endpoint;f.sequence=s.tx_;f.nonce=s.request_.local.run;f.envelope=s.request_.local;f.event=*event;
    auto bytes=stage1_control::encode(f);if(s.journal_.size()+bytes.size()>stage1_control::byte_limit)return NS_BOUND;s.journal_.insert(s.journal_.end(),bytes.begin(),bytes.end());
    s.files_.publish("events."+side(s.request_),s.journal_);++s.tx_;return NS_OK;
}catch(...){s.fault_=true;return NS_STATE;}}
int32_t GoalControl::wait(void* p,uint32_t release,uint64_t n){Entry call(static_cast<GoalControl*>(p));if(!call.p)return NS_CANCELLED;auto& s=*call.p;try{
    if(n!=s.request_.deployment.deadline_ns||release<1||release>4||!s.selected(false)||s.rx_>4)return NS_CONTEXT;
    const auto name="release."+side(s.request_)+"."+std::to_string(s.rx_);
    if(!s.files_.wait(name,end_at(n),s.wake_read_))return NS_CANCELLED;
    auto bytes=s.files_.read(name,stage1_control::frame_limit);stage1_control::Frame f;
    if(stage1_control::decode(bytes.data(),bytes.size(),f)!=stage1_control::Error::None||f.direction!=stage1_control::Direction::Coordinator||f.sequence!=s.rx_||f.nonce!=s.request_.local.run||!stage1_control::same_envelope(f.envelope,s.request_.local))return NS_CONTEXT;
    ++s.rx_;if(f.kind==stage1_control::Kind::Abort)return NS_CANCELLED;
    return f.kind==stage1_control::Kind::Release&&f.release==release&&s.selected(false)?NS_OK:NS_STATE;
}catch(...){s.fault_=true;return NS_STATE;}}
void GoalControl::abort(void* p){Entry call(static_cast<GoalControl*>(p));if(call.p){call.p->cancelled_=true;call.p->wake();}}
void GoalControl::start_watch(const NS_RunApi& api,NS_RunHandle* h){
    if(watch_.joinable()||!h||!api.cancel)throw std::invalid_argument("GOAL_WATCH_STATE");api_=api;handle_=h;
    watch_=std::thread([this]{
        try{const bool context=scalar_.get()==request_.local.self_token;const bool cancellation=!context||files_.wait("cancel."+side(request_),end_at(request_.deployment.deadline_ns),wake_read_);
            if(!stop_&&(cancellation||Clock::now()>=end_at(request_.deployment.deadline_ns))){cancelled_=true;if(api_.cancel(handle_)!=NS_OK)fault_=true;}
        }catch(...){fault_=true;if(!stop_){try{(void)api_.cancel(handle_);}catch(...){}}}
        std::lock_guard<std::mutex> l(gate_);watch_done_=true;changed_.notify_all();
    });
}
bool GoalControl::finish(Clock::time_point end){
    stop_=true;wake();std::unique_lock<std::mutex> l(gate_);
    if(watch_.joinable()&&!changed_.wait_until(l,end,[&]{return watch_done_;}))return false;
    l.unlock();if(watch_.joinable())watch_.join();l.lock();sealed_=true;wake();
    return changed_.wait_until(l,end,[&]{return entered_==0;})&&!fault_;
}
}
