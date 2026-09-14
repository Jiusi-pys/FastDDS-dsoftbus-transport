#include "softbus_lifecycle_contract.hpp"
#include <algorithm>
#include <cstring>

namespace mdds_probe {
const char* LifecycleEventName(LifecycleEventKind kind) noexcept {
    switch(kind) {
#define LC_EVENT(name) case LifecycleEventKind::name: return #name
        LC_EVENT(Created); LC_EVENT(Alias); LC_EVENT(Accepted); LC_EVENT(Ready);
        LC_EVENT(CloseRequested); LC_EVENT(ShutdownClaimed); LC_EVENT(ShutdownStarted); LC_EVENT(ShutdownReturned);
        LC_EVENT(CallbackEnter); LC_EVENT(CallbackExit); LC_EVENT(Bytes); LC_EVENT(Discarded);
        LC_EVENT(Failure); LC_EVENT(WorkerToken); LC_EVENT(CallBegin); LC_EVENT(CallReturn);
        LC_EVENT(WorkerJoined); LC_EVENT(CallbackHeld); LC_EVENT(CallbackReleased);
        LC_EVENT(PeerShutdown); LC_EVENT(PeerError); LC_EVENT(NegotiateHeld); LC_EVENT(NegotiateReleased);
#undef LC_EVENT
    }
    return "Unknown";
}
LifecycleContract::LifecycleContract(std::string name, std::string network, int type)
    : expected_name_(std::move(name)), expected_network_(std::move(network)), bytes_type_(type) {}
LifecycleContract::Owner* LifecycleContract::Find(int socket) noexcept {
    for(std::size_t i=0;i<state_.owned;++i) if(owners_[i].socket==socket)return &owners_[i];
    return nullptr;
}
void LifecycleContract::FailLocked(const char* reason, bool fatal, int untracked) noexcept {
    if(!state_.failed)state_.reason=reason;
    state_.failed=true; state_.ready=false; state_.fatal|=fatal;
    if(untracked>0 && state_.untracked_id==0)state_.untracked_id=untracked;
    cv_.notify_all();
}
void LifecycleContract::EventLocked(LifecycleEventKind kind,int socket,int code,std::size_t bytes,std::uint64_t tid) noexcept {
    if(state_.events==event_limit){FailLocked("event_ledger_full");return;}
    auto& event=events_[state_.events++];
    event={state_.events,tid,Clock::now(),std::chrono::system_clock::now(),kind,socket,code,bytes,state_.active_callbacks,state_.closing};
    cv_.notify_all();
}
LifecycleAdmission LifecycleContract::RegisterSocket(int socket) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(socket<=0){FailLocked("socket_failed");return LifecycleAdmission::Invalid;}
    if(auto* owner=Find(socket)) {
        if(owner->claimed || owner->returned){FailLocked("retired_id_conflict",true,socket);return LifecycleAdmission::Conflict;}
        ++state_.aliases; EventLocked(LifecycleEventKind::Alias,socket,0,0,0); return LifecycleAdmission::Alias;
    }
    if(state_.owned==owner_limit){FailLocked("owner_ledger_full",true,socket);return LifecycleAdmission::Full;}
    owners_[state_.owned++]={socket,false,false,false};
    EventLocked(LifecycleEventKind::Created,socket,0,0,0); return LifecycleAdmission::Owner;
}
bool LifecycleContract::Accept(int socket,RecordPeer peer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(socket<=0){FailLocked("invalid_accepted_id",true,socket);return false;}
    auto* owner=Find(socket);
    if(!owner) {
        if(state_.owned==owner_limit){FailLocked("accepted_owner_ledger_full",true,socket);return false;}
        owner=&owners_[state_.owned++]; *owner={socket,true,false,false};
    }
    if(owner->claimed || owner->returned){FailLocked("accepted_retired_id",true,socket);return false;}
    owner->accepted=true; EventLocked(LifecycleEventKind::Accepted,socket,0,0,0);
    if(!peer.fields_valid || peer.name!=expected_name_ || peer.network_id!=expected_network_ || peer.data_type!=bytes_type_) {
        FailLocked("peer_mismatch");return false;
    }
    if(state_.closing)return false;
    if(state_.connection!=0 && state_.connection!=socket){FailLocked("duplicate_connection");return false;}
    state_.connection=socket;return !state_.failed;
}
bool LifecycleContract::SetReady(int socket) {
    std::lock_guard<std::mutex> lock(mutex_);auto* owner=Find(socket);
    if(state_.closing || state_.failed)return false;
    if(!owner || owner->claimed){FailLocked("ready_without_owner");return false;}
    if(state_.connection!=0 && state_.connection!=socket){FailLocked("ready_wrong_connection");return false;}
    state_.connection=socket;state_.ready=true;EventLocked(LifecycleEventKind::Ready,socket,0,0,0);return !state_.failed;
}
void LifecycleContract::EnterCallback(int socket,std::uint64_t tid) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);++state_.active_callbacks;++state_.callback_count;
    EventLocked(LifecycleEventKind::CallbackEnter,socket,0,0,tid);
}
void LifecycleContract::LeaveCallback(int socket,std::uint64_t tid) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if(state_.active_callbacks==0)FailLocked("callback_count_underflow",true);
    else --state_.active_callbacks;
    EventLocked(LifecycleEventKind::CallbackExit,socket,0,0,tid);
}
void LifecycleContract::Receive(int socket,const void* data,std::size_t length) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);++state_.received;
        if(!Find(socket)){FailLocked("bytes_unknown_id",true,socket);return;}
        if(!data || length==0 || length>60000){FailLocked("invalid_bytes");return;}
        if(state_.closing){++state_.discarded;EventLocked(LifecycleEventKind::Discarded,socket,0,length,0);return;}
        if(state_.queued==record_queue_count || length>record_queue_bytes-state_.queued_bytes){FailLocked("receive_queue_full");return;}
        RecordEvent event;event.kind=RecordEventKind::Bytes;event.socket=socket;event.at=Clock::now();
        const auto* first=static_cast<const std::uint8_t*>(data);event.bytes.assign(first,first+length);
        records_.push_back(std::move(event));++state_.queued;state_.queued_bytes+=length;
        EventLocked(LifecycleEventKind::Bytes,socket,0,length,0);
    } catch(...) {Fail("callback_copy_exception");}
}
bool LifecycleContract::Pop(RecordEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);if(records_.empty())return false;
    event=std::move(records_.front());records_.pop_front();--state_.queued;state_.queued_bytes-=event.bytes.size();return true;
}
void LifecycleContract::PeerShutdown(int socket,int reason) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!Find(socket)){FailLocked("shutdown_unknown_id",true,socket);return;}
    ++state_.peer_shutdown;state_.ready=false;EventLocked(LifecycleEventKind::PeerShutdown,socket,reason,0,0);
}
void LifecycleContract::PeerError(int socket,int code) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!Find(socket))FailLocked("error_unknown_id",true,socket);
    EventLocked(LifecycleEventKind::PeerError,socket,code,0,0);
}
LifecycleContract::Clock::time_point LifecycleContract::RequestClose(Clock::time_point run,std::chrono::milliseconds budget) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!state_.closing) {
        state_.closing=true;state_.ready=false;close_deadline_=std::min(run,Clock::now()+budget);
        state_.discarded+=records_.size();records_.clear();state_.queued=0;state_.queued_bytes=0;
        EventLocked(LifecycleEventKind::CloseRequested,0,0,0,0);
    }
    return close_deadline_;
}
int LifecycleContract::ClaimShutdown() {
    std::lock_guard<std::mutex> lock(mutex_);if(!state_.closing)return 0;
    for(std::size_t i=state_.owned;i>0;--i)if(!owners_[i-1].claimed) {
        auto& owner=owners_[i-1];owner.claimed=true;++state_.shutdown_claimed;
        EventLocked(LifecycleEventKind::ShutdownClaimed,owner.socket,0,0,0);return owner.socket;
    }
    return 0;
}
bool LifecycleContract::ShutdownReturned(int socket,std::uint64_t tid) {
    std::lock_guard<std::mutex> lock(mutex_);auto* owner=Find(socket);
    if(!owner || !owner->claimed || owner->returned){FailLocked("unexpected_shutdown_return",true,socket);return false;}
    owner->returned=true;++state_.shutdown_returned;EventLocked(LifecycleEventKind::ShutdownReturned,socket,0,0,tid);return true;
}
void LifecycleContract::Report(LifecycleEventKind kind,int socket,int code,std::size_t bytes,std::uint64_t tid) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);EventLocked(kind,socket,code,bytes,tid);
}
void LifecycleContract::Fail(const char* reason,bool fatal,int untracked) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);FailLocked(reason,fatal,untracked);EventLocked(LifecycleEventKind::Failure,untracked,0,0,0);
}
LifecycleSnapshot LifecycleContract::Snapshot() const {std::lock_guard<std::mutex> lock(mutex_);return state_;}
bool LifecycleContract::Wait(const std::function<bool(const LifecycleSnapshot&)>& predicate,Clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);return cv_.wait_until(lock,deadline,[&]{return predicate(state_);});
}
bool LifecycleContract::EventAt(std::size_t index,LifecycleEvent& event) const {
    std::lock_guard<std::mutex> lock(mutex_);if(index>=state_.events)return false;event=events_[index];return true;
}
LifecycleCallbackLease::LifecycleCallbackLease(LifecycleContract& c,int socket,std::uint64_t tid) noexcept
    :contract_(c),socket_(socket),tid_(tid){contract_.EnterCallback(socket_,tid_);}
LifecycleCallbackLease::~LifecycleCallbackLease(){contract_.LeaveCallback(socket_,tid_);}
std::thread LifecycleThreadFactory::Create(std::function<void()> job){return std::thread(std::move(job));}
bool ManagedLifecycleWorker::Start(LifecycleThreadFactory& factory,std::function<void()> job) noexcept {
    if(worker_.joinable())return false;
    finished_=false;threw_=false;
    try {
        worker_=factory.Create([this,job=std::move(job)]() mutable {
            try{job();}catch(...){threw_=true;}
            {std::lock_guard<std::mutex> lock(mutex_);finished_=true;}
            cv_.notify_all();
        });
        if(worker_.joinable())return true;
    }catch(...){}
    finished_=true;return false;
}
bool ManagedLifecycleWorker::JoinUntil(std::chrono::steady_clock::time_point deadline) {
    if(!worker_.joinable())return true;
    std::unique_lock<std::mutex> lock(mutex_);if(!cv_.wait_until(lock,deadline,[&]{return finished_.load();}))return false;
    lock.unlock();worker_.join();return true;
}
} // namespace mdds_probe
