#include <fastdds_dsoftbus/dsoftbus/NativeSendExecution.hpp>
#include "../src/NativeSendExecutionInternal.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <limits>
#include <mutex>
#include <new>
#include <thread>

thread_local long fail_after=-1;
thread_local std::size_t allocation_count=0;
thread_local long close_after=-1;
thread_local fastdds_dsoftbus::dsoftbus::NativeOwnerTable* close_target=nullptr;
void* operator new(std::size_t n)
{
    ++allocation_count;
    if(close_after==0) {close_after=-1;close_target->RequestServiceClose();}
    else if(close_after>0) --close_after;
    if(fail_after==0) {fail_after=-1;throw std::bad_alloc();}
    if(fail_after>0) --fail_after;
    if(auto p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace fastdds_dsoftbus::dsoftbus;
namespace rtps = eprosima::fastdds::rtps;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x); } while(false)
namespace fastdds_dsoftbus::dsoftbus {
class NativeSendExecutionTestAccess
{
public:
    static auto Scoped(NativeSendExecution& e,const NativeTableConfiguration& c,
            const std::vector<Locator>& locals,const std::vector<EgressSelection>& selections,SendQueueLimits limits)
    {return e.CreateRuntimeForLocals(c,locals,selections,limits);}
    static std::mutex& Gate(NativeSendExecution& e) {return detail::NativeEgressBridge::TestGate(e.state_);}
    static RuntimeSendPath& Runtime(EgressRegistration r) {return *r.state_->runtime_;}
    static NativeStatus Stale(NativeSendExecution& e,EgressRegistration r,const Route& route,std::uint64_t gen,
            RuntimeIdentityHandle identity) {
        detail::NativeSendCallTicket ticket;NativeSocketId id;
        return detail::NativeEgressBridge::Admit(e.state_,*r.state_,identity,route,gen,SendQueue::Clock::now()+1s,ticket,id);
    }
};
class RuntimeSendPathTestAccess
{
public:
    static void AttachHook(RuntimeSendPath& r,std::function<void()> f) {r.hooks_.before_attach_commit=std::move(f);}
    static void CloseHook(RuntimeSendPath& r,std::function<void()> f) {r.hooks_.after_close_request=std::move(f);}
};
}
namespace {
Locator L(std::uint8_t id)
{ EndpointId e{};e[15]=id;Locator l;CHECK(LocatorCodec::Encode(e,l));return l; }
NativeTableConfiguration Config()
{
    NativeTableConfiguration c;c.service_pkg_name="com.kaihong.mdds";
    c.directory.locals={{L(2),"namespace-original","listen-original"}};
    for(int i=0;i<3;++i) {
        Route r;r.key={L(2),L(static_cast<std::uint8_t>(10+i))};
        const auto n=std::to_string(i);
        r.fields={"network-original-"+n,"listen-remote-"+n,"dial-original-"+n,{"remote-initiator-"+n},RecordDataType::Bytes};
        c.directory.routes.push_back(r);c.directions.push_back(RouteDirection::Dial);
    }
    return c;
}
struct Fixture
{
    NativeTableConfiguration config=Config();
    std::unique_ptr<NativeOwnerTable> table;
    std::vector<NativeOwnerLease> owners;
    Fixture() {
        auto built=NativeOwnerTable::Build(config);CHECK(built);table=std::move(*built.value);
        auto ns=table->AcquireNamespace(L(2),config);CHECK(ns);
        for(std::size_t i=0;i<3;++i) {
            auto r=table->ReserveDial(*ns.value,config.directory.routes[i].key);CHECK(r);
            CHECK(r.value->socket_call->CallEntered()==NativeStatus::None);
            CHECK(r.value->socket_call->Complete(NativeSocketResult::Id(70+static_cast<int>(i))).status==NativeStatus::None);
            auto b=table->BeginBind(r.value->owner);CHECK(b);CHECK(b.value->CallEntered()==NativeStatus::None);
            CHECK(b.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);owners.push_back(r.value->owner);
        }
    }
};
SendQueueLimits Limits() {return {8,1024,16,4096,1024};}
EgressRegistration Registration(NativeSendExecution& e,const NativeTableConfiguration& c)
{auto r=e.CreateRuntime(c,{},Limits());CHECK(r);return *r.value;}
void Creation()
{
    Fixture f;NativeSendExecution e(*f.table),other(*f.table);
    auto r=Registration(e,f.config);CHECK(r);CHECK(r.identity());
    CHECK(e.SnapshotUsage().runtimes==1);CHECK(other.SnapshotUsage().records==16);
    auto altered=f.config;altered.directory.routes[0].fields.remote_network_id="different-network";
    CHECK(!e.CreateRuntime(altered,{},Limits()));
    auto invalid=Limits();invalid.max_record_bytes=0;CHECK(!e.CreateRuntime(f.config,{},invalid));
    CHECK(e.SnapshotUsage().runtimes==1);
    CHECK(e.Revoke(r)==NativeStatus::None);CHECK(r.SnapshotUsage().closed);
    r={};CHECK(e.SnapshotUsage().runtimes==0);CHECK(f.owners[0].Observe().phase==NativeOwnerPhase::Ready);
}
class Event
{
public:
    void Signal() {std::lock_guard<std::mutex> l(gate);set=true;changed.notify_all();}
    void Wait() {std::unique_lock<std::mutex> l(gate);if(!changed.wait_for(l,3s,[&]{return set;})) std::abort();}
private:
    std::mutex gate;std::condition_variable changed;bool set=false;
};
template<class T> T Finish(std::future<T>& f)
{if(f.wait_for(3s)!=std::future_status::ready) std::abort();return f.get();}
struct Provider final : NativeSendProvider
{
    std::function<std::int32_t(NativeSocketId,const Route&,const std::uint8_t*,std::size_t,
            SendQueue::Clock::time_point,std::int32_t)> action;
    std::atomic<int> calls{0};
    std::int32_t Send(NativeSocketId id,const Route& r,const std::uint8_t* p,std::size_t n,
            SendQueue::Clock::time_point d,std::int32_t priority) override
    {++calls;return action?action(id,r,p,n,d,priority):0;}
};
bool Send(rtps::SenderResource& s,std::uint8_t remote=10,
        SendQueue::Clock::time_point deadline=SendQueue::Clock::now()+2s)
{
    const std::vector<Locator> targets{L(remote)};rtps::Locators begin(targets.cbegin()),end(targets.cend());
    const std::uint8_t a[]={1,2},b[]={3,4,5};const std::vector<rtps::NetworkBuffer> buffers{{a,2},{b,3}};
    return s.send(buffers,5,&begin,&end,deadline,37);
}
NativeSendWork Work(NativeSendExecution& e,EgressRegistration r,rtps::SendResourceList& senders,std::uint8_t remote=10,
        SendQueue::Clock::time_point deadline=SendQueue::Clock::now()+2s)
{
    CHECK(e.Attach(r,senders).ok());CHECK(Send(*senders.front(),remote,deadline));
    auto w=e.Take(r);CHECK(w);return std::move(*w.value);
}
void Windows()
{
    Fixture f;NativeSendExecution e(*f.table),e2(*f.table);auto p=std::make_shared<Provider>();
    auto old=Registration(e,f.config);rtps::SendResourceList a,b;
    auto w=Work(e,old,a);const auto old_identity=old.identity();const auto generation=f.owners[0].Observe().generation;
    CHECK(e.Revoke(old)==NativeStatus::None);auto fresh=Registration(e2,f.config);
    CHECK(fresh.identity()!=old_identity);CHECK(f.owners[0].Observe().generation==generation);
    CHECK(!e.TryAdmit(w,p));CHECK(w);CHECK(p->calls==0);w={};
    auto admitted_work=Work(e2,fresh,b);auto invocation=e.TryAdmit(admitted_work,p);CHECK(invocation);CHECK(!admitted_work);
    CHECK(e.Revoke(fresh)==NativeStatus::None);CHECK(f.table->SnapshotUsage().send.reserved==1);
    p->action=[&](auto id,const Route& route,const auto* bytes,auto n,auto,auto priority) {
        CHECK(id==70);CHECK(route.fields.remote_network_id=="network-original-0");CHECK(n==5&&bytes[0]==1&&bytes[4]==5);
        CHECK(priority==37);CHECK(e.SnapshotUsage().runtimes==2);CHECK(f.table->SnapshotUsage().send.entered==1);return 0;
    };
    auto result=invocation.value->Run();CHECK(result.status==NativeStatus::None);
    CHECK(result.outcome==NativeSendOutcome::ReturnedSdkCode&&result.actual_return_code==0);
    CHECK(invocation.value->Run().status!=NativeStatus::None);CHECK(p->calls==1);
    CHECK(f.table->SnapshotUsage().send.entered==0);CHECK(fresh.SnapshotUsage().records==0);
    CHECK(!Send(*b.front()));CHECK(!e.Attach(fresh,b).ok());
}
void OfflineIsolation()
{
    Fixture f;NativeSendExecution e(*f.table);auto r=Registration(e,f.config);
    rtps::SendResourceList senders;CHECK(e.Attach(r,senders).ok());auto* retained=senders.front().get();
    auto provider=std::make_shared<Provider>();CHECK(Send(*retained,10));auto initial=e.Take(r);CHECK(initial);
    auto admitted=e.TryAdmit(*initial.value,provider);CHECK(admitted);CHECK(admitted.value->Run().outcome==NativeSendOutcome::ReturnedSdkCode);
    CHECK(f.table->ObserveNodeOffline("network-original-0")==NativeStatus::None);
    CHECK(senders.front().get()==retained&&!Send(*retained,10));
    CHECK(Send(*retained,11));auto unaffected=e.Take(r);CHECK(unaffected);auto other=e.TryAdmit(*unaffected.value,provider);CHECK(other);
    CHECK(other.value->Run().outcome==NativeSendOutcome::ReturnedSdkCode&&provider->calls==2);
    CHECK(f.table->ObserveNodeOffline("unknown-node")==NativeStatus::NotFound&&Send(*retained,12));
    CHECK(e.Revoke(r)==NativeStatus::None);
}
void Retention()
{
    Fixture f;NativeSendExecution e(*f.table),second(*f.table);std::vector<rtps::SendResourceList> retained(16);
    for(std::size_t i=0;i<retained.size();++i) {
        auto r=Registration(e,f.config);CHECK(e.Attach(r,retained[i]).ok());
        if(i%2) CHECK(dynamic_cast<DSoftBusSenderResource*>(retained[i].front().get())->Release());
    }
    CHECK(e.SnapshotUsage().runtimes==16);CHECK(!second.CreateRuntime(f.config,{},Limits()));
    retained[0].clear();CHECK(e.SnapshotUsage().runtimes==15);
    {auto r=Registration(second,f.config);CHECK(e.SnapshotUsage().runtimes==16);}
    retained.clear();CHECK(e.SnapshotUsage().runtimes==0);
    for(int i=0;i<64;++i) {
        {auto r=Registration(e,f.config);rtps::SendResourceList list;auto w=Work(e,r,list);
            CHECK(e.Revoke(r)==NativeStatus::None);r={};list.clear();CHECK(e.SnapshotUsage().runtimes==1);}
        CHECK(e.SnapshotUsage().runtimes==0&&e.SnapshotUsage().bytes==0);
    }
    auto r=Registration(e,f.config);rtps::SendResourceList list;auto w=Work(e,r,list);
    auto p=std::make_shared<Provider>();auto call=e.TryAdmit(w,p);CHECK(call);
    e.Revoke(r);r={};list.clear();CHECK(e.SnapshotUsage().runtimes==1);
    call.value.reset();CHECK(e.SnapshotUsage().runtimes==0);CHECK(f.table->SnapshotUsage().send.reserved==0);
}
void QuotasAndBlockedCall()
{
    Fixture f;NativeSendExecution e(*f.table),other(*f.table);auto r=Registration(e,f.config),r2=Registration(other,f.config);
    rtps::SendResourceList a,b;auto w=Work(e,r,a),same=Work(other,r2,b),two=Work(other,r2,b,11),three=Work(other,r2,b,12);
    auto p=std::make_shared<Provider>();auto first=e.TryAdmit(w,p);CHECK(first);
    CHECK(other.TryAdmit(same,p).issue.code==NativeStatus::SendBusy);CHECK(same);
    auto second=other.TryAdmit(two,p);CHECK(second);
    CHECK(other.TryAdmit(three,p).issue.code==NativeStatus::SendBusy);CHECK(three);
    Event entered,release;p->action=[&](auto,const auto&,const auto* bytes,auto n,auto,auto) {
        entered.Signal();release.Wait();CHECK(n==5&&bytes[4]==5);return 0;
    };
    auto running=std::async(std::launch::async,[&]{return first.value->Run();});entered.Wait();
    CHECK(running.wait_for(20ms)==std::future_status::timeout);
    CHECK(f.table->SnapshotUsage().send.entered==1&&f.table->SnapshotUsage().send.reserved==1);
    CHECK(e.Revoke(r)==NativeStatus::None);CHECK(r.SnapshotUsage().records==1);
    CHECK(f.owners[0].RequestClose()==NativeStatus::None);auto close=f.owners[0].TryClaimShutdown();CHECK(close);
    CHECK(close.value->CallEntered()==NativeStatus::None);CHECK(close.value->CompleteShutdownReturned()==NativeStatus::None);
    CHECK(f.table->SnapshotUsage().send.entered==1);release.Signal();CHECK(Finish(running).status==NativeStatus::None);
    CHECK(f.table->SnapshotUsage().send.entered==0);second.value.reset();
    CHECK(other.TryAdmit(three,p));CHECK(f.owners[0].Observe().shutdown_returned);
}
void DeadlinesAndPhysicalClose()
{
    Fixture f;NativeSendExecution e(*f.table);auto p=std::make_shared<Provider>();auto r=Registration(e,f.config);rtps::SendResourceList list;
    auto deadline=SendQueue::Clock::now()+50ms;auto w=Work(e,r,list,10,deadline);
    std::this_thread::sleep_until(deadline+1ms);CHECK(!e.TryAdmit(w,p));CHECK(w);w={};CHECK(r.SnapshotUsage().records==0);
    deadline=SendQueue::Clock::now()+50ms;w=Work(e,r,list,10,deadline);auto call=e.TryAdmit(w,p);CHECK(call);
    std::this_thread::sleep_until(deadline+1ms);CHECK(call.value->Run().outcome==NativeSendOutcome::NotEntered);
    CHECK(p->calls==0);CHECK(f.table->SnapshotUsage().send.reserved==0);CHECK(r.SnapshotUsage().records==0);
    w=Work(e,r,list);call.value.reset();auto closed=e.TryAdmit(w,p);CHECK(closed);f.owners[0].RequestClose();
    CHECK(closed.value->Run().outcome==NativeSendOutcome::NotEntered);CHECK(p->calls==0);
}
void Results()
{
    for(int mode=0;mode<4;++mode) {
        Fixture f;NativeSendExecution e(*f.table);auto r=Registration(e,f.config);rtps::SendResourceList list;
        auto w=Work(e,r,list);auto p=std::make_shared<Provider>();
        p->action=[=](auto,const auto&,auto*,auto,auto,auto)->std::int32_t {
            if(mode==3) throw std::runtime_error("provider frame unwound");return mode==0?0:mode==1?-123:17;
        };
        auto call=e.TryAdmit(w,p);CHECK(call);auto result=call.value->Run();
        CHECK(p->calls==1&&f.table->SnapshotUsage().send.entered==0);CHECK(!f.table->SnapshotUsage().entered_context_lost);
        CHECK(r.SnapshotUsage().records==0);
        if(mode<2) {CHECK(result.outcome==NativeSendOutcome::ReturnedSdkCode);CHECK(result.actual_return_code==(mode==0?0:-123));}
        if(mode==2) {CHECK(result.outcome==NativeSendOutcome::ReturnedInvalidCode);CHECK(result.actual_return_code==17);CHECK(f.owners[0].Observe().error_code==0);}
        if(mode==3) {CHECK(result.outcome==NativeSendOutcome::ReturnedByException);CHECK(!result.actual_return_code);CHECK(f.owners[0].Observe().error_code==0);}
        CHECK(f.owners[0].Observe().close_intent==(mode!=0));
    }
}
void ConstructionFailures()
{
    Fixture f;NativeSendExecution e(*f.table);const std::vector<EgressSelection> selections;
    const auto start=allocation_count;{auto r=e.CreateRuntime(f.config,selections,Limits());CHECK(r);}
    const auto count=allocation_count-start;
    for(std::size_t i=0;i<count;++i) {
        fail_after=static_cast<long>(i);auto r=e.CreateRuntime(f.config,selections,Limits());fail_after=-1;
        CHECK(!r);CHECK(e.SnapshotUsage().runtimes==0&&e.SnapshotUsage().bytes==0);
    }
    auto r=Registration(e,f.config);rtps::SendResourceList list;CHECK(e.Attach(r,list).ok());CHECK(Send(*list.front()));
    fail_after=0;auto taken=e.Take(r);fail_after=-1;CHECK(!taken);CHECK(r.SnapshotUsage().queued==1);
    auto w=e.Take(r);CHECK(w);auto p=std::make_shared<Provider>();
    fail_after=0;auto call=e.TryAdmit(*w.value,p);fail_after=-1;CHECK(!call);CHECK(*w.value);
    CHECK(f.table->SnapshotUsage().send.reserved==0);
    Fixture foreign;NativeSendExecution other(*foreign.table);
    CHECK(!other.Take(r));CHECK(!other.TryAdmit(*w.value,p));CHECK(other.Revoke(r)==NativeStatus::ForeignState);
    CHECK(e.Revoke(r)==NativeStatus::None);
    f.table->RequestServiceClose();CHECK(!e.CreateRuntime(f.config,{},Limits()));
}
void PublicationAndEntry()
{
    Fixture f;NativeSendExecution e(*f.table);auto r=Registration(e,f.config);rtps::SendResourceList list;
    Event attached,release;
    auto& runtime=NativeSendExecutionTestAccess::Runtime(r);
    RuntimeSendPathTestAccess::AttachHook(runtime,[&]{attached.Signal();release.Wait();});
    auto attaching=std::async(std::launch::async,[&]{return e.Attach(r,list);});attached.Wait();
    CHECK(e.Revoke(r)==NativeStatus::None);release.Signal();CHECK(!Finish(attaching).ok());CHECK(list.empty());
    RuntimeSendPathTestAccess::AttachHook(runtime,{});
    r=Registration(e,f.config);CHECK(e.Attach(r,list).ok());CHECK(Send(*list.front()));
    Event both,finish;std::atomic<int> calls{0};
    RuntimeSendPathTestAccess::CloseHook(NativeSendExecutionTestAccess::Runtime(r),[&]{if(++calls==2) both.Signal();finish.Wait();});
    auto first=std::async(std::launch::async,[&]{return e.Revoke(r);});
    auto second=std::async(std::launch::async,[&]{return e.Revoke(r);});both.Wait();
    CHECK(first.wait_for(1ms)==std::future_status::timeout&&second.wait_for(1ms)==std::future_status::timeout);
    CHECK(r.SnapshotUsage().queued==1);finish.Signal();CHECK(Finish(first)==NativeStatus::None);CHECK(Finish(second)==NativeStatus::None);
    CHECK(r.SnapshotUsage().queued==0);RuntimeSendPathTestAccess::CloseHook(NativeSendExecutionTestAccess::Runtime(r),{});
    list.clear();r=Registration(e,f.config);
    const auto deadline=SendQueue::Clock::now()+100ms;auto w=Work(e,r,list,10,deadline);auto p=std::make_shared<Provider>();
    auto invocation=e.TryAdmit(w,p);CHECK(invocation);
    std::unique_lock<std::mutex> lock(NativeSendExecutionTestAccess::Gate(e));Event run_started;
    auto running=std::async(std::launch::async,[&]{run_started.Signal();return invocation.value->Run();});run_started.Wait();
    CHECK(running.wait_for(1ms)==std::future_status::timeout);
    std::this_thread::sleep_until(deadline+1ms);lock.unlock();
    CHECK(Finish(running).status==NativeStatus::DeadlineExpired);CHECK(p->calls==0);
    CHECK(r.SnapshotUsage().records==0&&f.table->SnapshotUsage().send.reserved==0);
}
void RollbackAndCeilings()
{
    const std::vector<EgressSelection> selections;std::size_t count;
    {Fixture f;NativeSendExecution e(*f.table);const auto start=allocation_count;
        {auto r=e.CreateRuntime(f.config,selections,Limits());CHECK(r);}count=allocation_count-start;}
    for(std::size_t i=0;i<count;++i) {
        Fixture f;NativeSendExecution e(*f.table);close_target=f.table.get();close_after=static_cast<long>(i);
        auto r=e.CreateRuntime(f.config,selections,Limits());close_after=-1;close_target=nullptr;
        CHECK(!r);CHECK(e.SnapshotUsage().runtimes==0&&e.SnapshotUsage().records==0&&e.SnapshotUsage().bytes==0);
    }
    Fixture f;NativeSendExecution e(*f.table);auto l=Limits();l.global_records=2048;
    auto a=e.CreateRuntime(f.config,{},l);CHECK(a);CHECK(!e.CreateRuntime(f.config,{},Limits()));a.value.reset();
    l=Limits();l.global_bytes=16*1024*1024;auto b=e.CreateRuntime(f.config,{},l);CHECK(b);
    CHECK(!e.CreateRuntime(f.config,{},Limits()));b.value.reset();CHECK(e.SnapshotUsage().bytes==0);
    l.global_bytes=std::numeric_limits<std::size_t>::max();CHECK(!e.CreateRuntime(f.config,{},l));
    l=Limits();l.global_records=std::numeric_limits<std::size_t>::max();CHECK(!e.CreateRuntime(f.config,{},l));
    auto r=Registration(e,f.config);auto stale=f.config.directory.routes[0];++stale.route_version;
    const auto g=f.owners[0].Observe().generation;
    CHECK(NativeSendExecutionTestAccess::Stale(e,r,stale,g,r.identity())==NativeStatus::StaleVersion);
    CHECK(NativeSendExecutionTestAccess::Stale(e,r,f.config.directory.routes[0],g+1,r.identity())==NativeStatus::StaleGeneration);
    CHECK(NativeSendExecutionTestAccess::Stale(e,r,f.config.directory.routes[0],g,{})==NativeStatus::InvalidHandle);
    CHECK(f.table->SnapshotUsage().send.reserved==0);
}
void LastReferencesAndShutdownFault()
{
    Fixture f;std::optional<NativeSendInvocation> call;std::weak_ptr<Provider> weak;
    Event entered,release;
    {
        NativeSendExecution e(*f.table);auto r=Registration(e,f.config);rtps::SendResourceList list;
        auto w=Work(e,r,list);auto p=std::make_shared<Provider>();weak=p;
        p->action=[&](auto,const auto&,auto* bytes,auto n,auto,auto) {
            entered.Signal();release.Wait();CHECK(n==5&&bytes[3]==4);
            auto ns=f.table->AcquireNamespace(L(2),f.config);CHECK(ns);auto callback=ns.value->AdmitCallback(70);CHECK(callback);
            CHECK(callback.value->ObserveShutdown()==NativeStatus::None);return 29;
        };
        auto admitted=e.TryAdmit(w,p);CHECK(admitted);call.emplace(std::move(*admitted.value));e.Revoke(r);
    }
    NativeSendExecution observer(*f.table);CHECK(observer.SnapshotUsage().runtimes==1);CHECK(!weak.expired());
    auto running=std::async(std::launch::async,[&]{return call->Run();});entered.Wait();
    CHECK(f.table->SnapshotUsage().send.entered==1);CHECK(!weak.expired());release.Signal();
    auto result=Finish(running);CHECK(result.outcome==NativeSendOutcome::ReturnedInvalidCode&&result.actual_return_code==29);
    CHECK(weak.expired());CHECK(observer.SnapshotUsage().runtimes==0);
    auto facts=f.owners[0].Observe();CHECK(facts.on_shutdown_seen&&facts.close_intent);
    CHECK(facts.phase==NativeOwnerPhase::RetainedNotQuiescent);CHECK(facts.provider_fault==NativeProviderFault::InvalidPositiveReturn);
    CHECK(facts.invalid_provider_return==29&&facts.error_code==0);
}
void StrongRevokeBeforeRun()
{
    Fixture f;NativeSendExecution e(*f.table);auto r=Registration(e,f.config);
    rtps::SendResourceList list;auto w=Work(e,r,list);auto p=std::make_shared<Provider>();
    auto call=e.TryAdmit(w,p);CHECK(call);
    CHECK(e.Observe(r).admitted_outstanding==1);
    auto revoke=e.BeginRevoke(r);CHECK(revoke);
    CHECK(e.Observe(r).admission_closed&&!e.Observe(r).drained);
    CHECK(e.Observe(r).admitted_outstanding==1);CHECK(!Send(*list.front()));
    revoke.value->Progress();
    CHECK(e.Observe(r).drained&&e.Observe(r).admitted_outstanding==0);
    CHECK(r.SnapshotUsage().records==0&&f.table->SnapshotUsage().send.reserved==0);
    CHECK(call.value->Run().outcome==NativeSendOutcome::NotEntered);CHECK(p->calls==0);
    revoke.value->Progress();CHECK(e.Observe(r).drained);
}
void StrongRevokeEntered()
{
    Fixture f;NativeSendExecution e(*f.table);auto r=Registration(e,f.config);
    rtps::SendResourceList list;auto w=Work(e,r,list);auto p=std::make_shared<Provider>();
    Event entered,release;p->action=[&](auto,const auto&,auto* bytes,auto n,auto,auto) {
        entered.Signal();release.Wait();CHECK(n==5&&bytes[4]==5);return 0;
    };
    auto call=e.TryAdmit(w,p);CHECK(call);
    auto running=std::async(std::launch::async,[&]{return call.value->Run();});entered.Wait();
    auto revoke=e.BeginRevoke(r);CHECK(revoke);revoke.value->Progress();
    CHECK(e.Observe(r).admitted_outstanding==1&&!e.Observe(r).drained);
    CHECK(r.SnapshotUsage().records==1);release.Signal();CHECK(Finish(running).status==NativeStatus::None);
    CHECK(e.Observe(r).drained&&r.SnapshotUsage().records==0);
}
struct CleanupProvider final : NativeSendProvider
{
    CleanupProvider(Event& a,Event& b) : entered(a),release(b) {}
    ~CleanupProvider() override {entered.Signal();release.Wait();}
    std::int32_t Send(NativeSocketId,const Route&,const std::uint8_t*,std::size_t,
            SendQueue::Clock::time_point,std::int32_t) override {return 0;}
    Event& entered;Event& release;
};
void StrongRevokeCleanup()
{
    // Both a ticket-owned cancellation and an expired weak control (the last
    // shell is already destructing) remain outstanding through actual cleanup.
    for(bool shell_destructor : {false,true}) {
        Fixture f;NativeSendExecution e(*f.table);auto r=Registration(e,f.config);
        rtps::SendResourceList list;auto w=Work(e,r,list);Event entered,release;
        auto p=std::make_shared<CleanupProvider>(entered,release);
        auto call=e.TryAdmit(w,p);CHECK(call);p.reset();
        std::optional<NativeRevokeTicket> first;
        if(!shell_destructor) {auto revoke=e.BeginRevoke(r);CHECK(revoke);first.emplace(std::move(*revoke.value));}
        auto cleaning=std::async(std::launch::async,[&]{
            if(shell_destructor) call.value.reset();else first->Progress();
        });
        entered.Wait(); // Actual provider destructor, not a pre-call timer.
        CHECK(e.Observe(r).admitted_outstanding==1);
        CHECK(f.table->SnapshotUsage().send.reserved==0);
        auto second=e.BeginRevoke(r);CHECK(second);second.value->Progress();
        CHECK(e.Observe(r).queue_barrier_crossed&&!e.Observe(r).drained);
        CHECK(r.SnapshotUsage().records==1); // Record cleanup follows provider.
        release.Signal();Finish(cleaning);
        CHECK(e.Observe(r).drained&&r.SnapshotUsage().records==0);
        if(call.value) CHECK(call.value->Run().outcome==NativeSendOutcome::NotEntered);
    }
}
void StrongRevokeTickets()
{
    Fixture f;NativeSendExecution e(*f.table);auto r=Registration(e,f.config);
    rtps::SendResourceList list;auto w=Work(e,r,list);auto p=std::make_shared<Provider>();
    auto call=e.TryAdmit(w,p);CHECK(call);
    Fixture other;NativeSendExecution foreign(*other.table);
    CHECK(!foreign.BeginRevoke(r));CHECK(!r.SnapshotUsage().closed);
    CHECK(foreign.Observe(r).status==NativeStatus::ForeignState);
    {
        auto a=e.BeginRevoke(r),b=e.BeginRevoke(r);CHECK(a&&b);
        b.value->Progress();CHECK(!e.Observe(r).drained); // A still owns cleanup.
        NativeRevokeTicket moved(std::move(*a.value));a.value->Progress();
        CHECK(!e.Observe(r).drained);
        // Move assignment settles B outside all gates then transfers A's work.
        *b.value=std::move(moved);CHECK(!e.Observe(r).drained);
    } // Dropping the owning ticket performs terminal cleanup, no Run required.
    CHECK(e.Observe(r).drained);CHECK(call.value->Run().outcome==NativeSendOutcome::NotEntered);
    CHECK(p->calls==0&&r.SnapshotUsage().records==0);
}
void StrongRevokeControlCapacity()
{
    Fixture f;NativeSendExecution e(*f.table);auto r=Registration(e,f.config);
    rtps::SendResourceList list;auto p=std::make_shared<Provider>();
    auto a=Work(e,r,list,10),b=Work(e,r,list,11),c=Work(e,r,list,12);
    auto one=e.TryAdmit(a,p),two=e.TryAdmit(b,p);CHECK(one&&two);
    CHECK(f.owners[0].RequestClose()==NativeStatus::None);
    CHECK(f.owners[1].RequestClose()==NativeStatus::None);
    CHECK(f.table->SnapshotUsage().send.reserved==0&&e.Observe(r).admitted_outstanding==2);
    CHECK(e.TryAdmit(c,p).issue.code==NativeStatus::SendBusy);CHECK(c);
    one.value.reset();CHECK(e.Observe(r).admitted_outstanding==1);
    auto fresh=e.TryAdmit(c,p);CHECK(fresh);
    two.value.reset(); // Old native token cannot cancel the reused send cell.
    CHECK(f.table->SnapshotUsage().send.reserved==1);
    CHECK(fresh.value->Run().status==NativeStatus::None);CHECK(p->calls==1);
    CHECK(e.Observe(r).admitted_outstanding==0&&r.SnapshotUsage().records==0);
}
void StrongRevokeAdmissionAllocations()
{
    Fixture f;NativeSendExecution e(*f.table);auto r=Registration(e,f.config);
    rtps::SendResourceList list;auto p=std::make_shared<Provider>();
    auto w=Work(e,r,list);const auto start=allocation_count;
    auto measured=e.TryAdmit(w,p);const auto count=allocation_count-start;CHECK(measured);measured.value.reset();
    CHECK(count>0);
    for(std::size_t i=0;i<count;++i) {
        w=Work(e,r,list);fail_after=static_cast<long>(i);auto call=e.TryAdmit(w,p);fail_after=-1;
        CHECK(!call&&call.issue.code==NativeStatus::AllocationFailure);CHECK(w);
        CHECK(e.Observe(r).admitted_outstanding==0&&f.table->SnapshotUsage().send.reserved==0);
        CHECK(r.SnapshotUsage().records==1);w={};CHECK(r.SnapshotUsage().records==0);
    }
    std::cout<<"N3 strong admission allocation_points="<<count<<'\n';
}
void ApplicationLocalProjection()
{
    auto config=Config();
    config.directory.locals.push_back({L(3),"namespace-b","listen-b"});
    config.directory.locals.push_back({L(4),"namespace-no-egress","listen-no-egress"});
    for(auto remote : {10,20}) {
        Route route;route.key={L(3),L(static_cast<std::uint8_t>(remote))};
        route.fields={"network-b","remote-b","dial-b-"+std::to_string(remote),
                {"initiator-b-"+std::to_string(remote)},RecordDataType::Bytes};
        config.directory.routes.push_back(route);config.directions.push_back(RouteDirection::Dial);
    }
    auto table=NativeOwnerTable::Build(config);CHECK(table);NativeSendExecution e(**table.value);
    const std::vector<EgressSelection> none;
    // Full process configuration is still ambiguous; no legacy behavior change.
    CHECK(!e.CreateRuntime(config,none,Limits()));
    {
        auto a=NativeSendExecutionTestAccess::Scoped(e,config,{L(2)},none,Limits());
        auto b=NativeSendExecutionTestAccess::Scoped(e,config,{L(3)},none,Limits());CHECK(a&&b);
        auto ap=NativeSendExecutionTestAccess::Runtime(*a.value).plan();
        auto bp=NativeSendExecutionTestAccess::Runtime(*b.value).plan();
        CHECK(ap->size()==3&&bp->size()==2);CHECK(!ap->Find(L(20))&&!bp->Find(L(11)));
        CHECK(ap->Find(L(10))->key.local_locator==L(2)&&bp->Find(L(10))->key.local_locator==L(3));
        CHECK(ap->selected_locals()==std::vector<Locator>{L(2)});
        CHECK(bp->selected_locals()==std::vector<Locator>{L(3)});
    }
    for(const auto& locals : {std::vector<Locator>{},std::vector<Locator>{L(99)},
            std::vector<Locator>{L(2),L(2)}})
        CHECK(!NativeSendExecutionTestAccess::Scoped(e,config,locals,none,Limits()));
    CHECK(!NativeSendExecutionTestAccess::Scoped(e,config,{L(2)},{{L(10),L(3),1}},Limits()));
    CHECK(!NativeSendExecutionTestAccess::Scoped(e,config,{L(2),L(3)},none,Limits()));
    {
        auto empty=NativeSendExecutionTestAccess::Scoped(e,config,{L(4)},none,Limits());CHECK(empty);
        CHECK(NativeSendExecutionTestAccess::Runtime(*empty.value).plan()->size()==0);
        rtps::SendResourceList resources;CHECK(!e.Attach(*empty.value,resources).ok());CHECK(resources.empty());
    }
    auto changed=config;changed.directory.routes[0].fields.remote_network_id="changed";
    CHECK(NativeSendExecutionTestAccess::Scoped(e,changed,{L(3)},none,Limits()).issue.code==NativeStatus::ConfigurationMismatch);
    CHECK(e.SnapshotUsage().runtimes==0&&e.SnapshotUsage().bytes==0);
    const std::vector<Locator> locals{L(2)};
    const auto start=allocation_count;
    {auto measured=NativeSendExecutionTestAccess::Scoped(e,config,locals,none,Limits());CHECK(measured);}
    const auto count=allocation_count-start;CHECK(count>0);
    for(std::size_t i=0;i<count;++i) {
        fail_after=static_cast<long>(i);
        auto r=NativeSendExecutionTestAccess::Scoped(e,config,locals,none,Limits());fail_after=-1;
        CHECK(!r&&r.issue.code==NativeStatus::AllocationFailure);
        CHECK(e.SnapshotUsage().runtimes==0&&e.SnapshotUsage().bytes==0);
    }
    std::cout<<"N3 scoped runtime allocation_points="<<count<<'\n';
}
}
int main()
{
    static_assert(!std::is_copy_constructible_v<NativeSendWork>);
    static_assert(!std::is_copy_constructible_v<NativeSendInvocation>);
    static_assert(!std::is_move_constructible_v<NativeSendExecution>);
    static_assert(!std::is_constructible_v<NativeSendWork,RuntimeIdentityHandle,OwnedSendRecord>);
    try {
        Creation();std::cout<<"PASS N2-CREATE\n";
        OfflineIsolation();std::cout<<"PASS ROUTE-OFFLINE-ISOLATION\n";
        Windows();std::cout<<"PASS N2-WINDOWS\n";
        Retention();std::cout<<"PASS N2-RETENTION\n";
        QuotasAndBlockedCall();std::cout<<"PASS N2-QUOTAS\n";
        DeadlinesAndPhysicalClose();std::cout<<"PASS N2-DEADLINES\n";
        Results();std::cout<<"PASS N2-RESULTS\n";
        ConstructionFailures();std::cout<<"PASS N2-OOM\n";
        PublicationAndEntry();std::cout<<"PASS N2-PUBLICATION-ENTRY\n";
        RollbackAndCeilings();std::cout<<"PASS N2-ROLLBACK-CEILINGS\n";
        LastReferencesAndShutdownFault();std::cout<<"PASS N2-LAST-REFERENCES\n";
        StrongRevokeBeforeRun();std::cout<<"PASS N3-SEND-REVOKE-RESERVED\n";
        StrongRevokeEntered();std::cout<<"PASS N3-SEND-REVOKE-ENTERED\n";
        StrongRevokeCleanup();std::cout<<"PASS N3-SEND-CLEANUP-DRAIN\n";
        StrongRevokeTickets();std::cout<<"PASS N3-SEND-REVOKE-TICKETS\n";
        StrongRevokeControlCapacity();std::cout<<"PASS N3-SEND-CONTROL-CAPACITY\n";
        StrongRevokeAdmissionAllocations();std::cout<<"PASS N3-SEND-ADMISSION-OOM\n";
        ApplicationLocalProjection();std::cout<<"PASS N3-APPLICATION-LOCAL-PROJECTION\n";return 0;
    }
    catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
