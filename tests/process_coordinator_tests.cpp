#include <mdds/dsoftbus/ProcessCoordinator.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
thread_local long fail_after=-1;
thread_local std::size_t allocation_count=0;
void* operator new(std::size_t n)
{
    ++allocation_count;
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
using namespace mdds::dsoftbus;
using namespace std::chrono_literals;
#define CHECK(x) do {if(!(x)) throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x);} while(false)
namespace mdds::dsoftbus {
class ProcessCoordinatorTestAccess
{
public:
    using Hooks=ProcessCoordinator::TestHooks;
    static auto Build(const NativeTableConfiguration& config,Hooks hooks={},std::shared_ptr<ProcessReceiveObserver> observer={})
    {return ProcessCoordinator::BuildWithHooks(config,{},ProcessClock{},1s,std::move(hooks),std::move(observer));}
};
}
namespace {
struct Event
{
    std::mutex gate;std::condition_variable cv;bool set=false;
    void Signal(){std::lock_guard<std::mutex> lock(gate);set=true;cv.notify_all();}
    void Wait(){std::unique_lock<std::mutex> lock(gate);if(!cv.wait_for(lock,3s,[&]{return set;}))std::abort();}
};
template<class T> T Finish(std::future<T>& f)
{if(f.wait_for(3s)!=std::future_status::ready)std::abort();return f.get();}
Locator L(std::uint8_t id)
{EndpointId e{};e[15]=id;Locator l;CHECK(LocatorCodec::Encode(e,l));return l;}
NativeTableConfiguration Config()
{
    NativeTableConfiguration c;c.service_pkg_name="com.kaihong.mdds";
    c.directory.locals={{L(2),"same-namespace","same-listener"}};
    Route r;r.key={L(2),L(10)};
    r.fields={"peer-network","peer-listener","same-dial",{"peer-dial"},RecordDataType::Bytes};
    c.directory.routes.push_back(r);c.directions.push_back(RouteDirection::Dial);return c;
}
struct Receiver final:InputChannels::Receiver
{
    std::atomic<int> calls{0};
    std::function<void()> action;
    void OnDataReceived(const std::uint8_t* p,std::uint32_t n,const Locator& l,const Locator& r) override
    {CHECK(n==3&&p[0]==1&&p[2]==3&&l==L(2)&&r==L(10));++calls;if(action)action();}
};
struct Fixture
{
    NativeTableConfiguration config;
    std::unique_ptr<ProcessCoordinator> process;
    NamespaceLease space;
    NativeOwnerLease owner;
    explicit Fixture(const NativeTableConfiguration& c=Config(),ProcessCoordinatorTestAccess::Hooks hooks={},
            std::shared_ptr<ProcessReceiveObserver> observer={}) :config(c)
    {
        auto built=ProcessCoordinatorTestAccess::Build(config,std::move(hooks),std::move(observer));CHECK(built);process=std::move(*built.value);
        auto& native=process->ModelNative();auto ns=native.AcquireNamespace(L(2),config);CHECK(ns);space=*ns.value;
        auto dial=native.ReserveDial(space,{L(2),L(10)});CHECK(dial);owner=dial.value->owner;
        CHECK(dial.value->socket_call->CallEntered()==NativeStatus::None);
        CHECK(dial.value->socket_call->Complete(NativeSocketResult::Id(70)).status==NativeStatus::None);
        auto bind=native.BeginBind(owner);CHECK(bind);CHECK(bind.value->CallEntered()==NativeStatus::None);
        CHECK(bind.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);
    }
    NativeIdentityConstraint Pin()
    {auto callback=space.AdmitCallback(70);CHECK(callback);auto pin=callback.value->CaptureIdentity();CHECK(pin);return *pin.value;}
    void Finish()
    {process->BeginShutdown();CHECK(process->PollShutdown().phase==ProcessShutdownPhase::ApplicationsDrained);}
};
constexpr std::uint8_t bytes[]={1,2,3};
SendQueueLimits Limits() {return {8,1024,16,4096,1024};} // Model test envelope only.
void SameLocalRebuild()
{
    Fixture f;auto& p=*f.process;
    for(int round=0;round<80;++round) {
        {
            Receiver receiver;auto app=p.AttachApplication({L(2)},{},Limits());CHECK(app);
            CHECK(p.OpenReceiver(*app.value,L(2),&receiver).result==ReceiverOpenResult::Opened);
            CHECK(p.Receive(f.Pin(),bytes,sizeof(bytes))==ReceiveAdmissionResult::Accepted);
            auto permit=p.NextReceive();CHECK(permit);CHECK(p.ReceiveUsage().delivering==1);
            CHECK(p.BeginDetach(*app.value)==NativeStatus::None);
            CHECK(p.ProgressDetach(*app.value).phase==ProcessApplicationPhase::Detaching);
            CHECK(!p.AttachApplication({L(2)},{},Limits())); // A real permit exists, even before Run.
            CHECK(permit.value->Run()==InputDeliveryResult::Delivered);CHECK(receiver.calls==1);
            CHECK(p.ProgressDetach(*app.value).phase==ProcessApplicationPhase::Detached);
            CHECK(p.BeginDetach(*app.value)==NativeStatus::None);
            CHECK(p.ReceiveUsage().records==0&&p.ModelNative().SnapshotUsage().receives==0);
        }
        CHECK(p.EgressUsage().runtimes==0); // No global ledger reset between rounds.
    }
    CHECK(f.owner.Observe().phase==NativeOwnerPhase::Ready);f.Finish();
}
void GapAndNoReceiver()
{
    Fixture f;auto& p=*f.process;auto pin=f.Pin();Receiver receiver;
    CHECK(p.Receive(pin,bytes,sizeof(bytes))==ReceiveAdmissionResult::NoReceiver);
    auto a=p.AttachApplication({L(2)},{},Limits());CHECK(a);
    CHECK(p.Receive(pin,bytes,sizeof(bytes))==ReceiveAdmissionResult::NoReceiver);
    CHECK(p.OpenReceiver(*a.value,L(2),&receiver).result==ReceiverOpenResult::Opened);
    CHECK(!p.NextReceive()&&receiver.calls==0);
    CHECK(p.BeginDetach(*a.value)==NativeStatus::None);
    CHECK(p.ProgressDetach(*a.value).phase==ProcessApplicationPhase::Detached);
    CHECK(p.Receive(pin,bytes,sizeof(bytes))==ReceiveAdmissionResult::NoReceiver);
    auto b=p.AttachApplication({L(2)},{},Limits());CHECK(b);
    CHECK(p.OpenReceiver(*b.value,L(2),&receiver).result==ReceiverOpenResult::Opened);
    CHECK(!p.NextReceive()&&receiver.calls==0);
    CHECK(p.BeginDetach(*b.value)==NativeStatus::None);
    CHECK(p.ProgressDetach(*b.value).phase==ProcessApplicationPhase::Detached);f.Finish();
}
void ExplicitShutdownPending()
{
    Fixture f;auto& p=*f.process;Receiver receiver;auto a=p.AttachApplication({L(2)},{},Limits());CHECK(a);
    CHECK(p.OpenReceiver(*a.value,L(2),&receiver).result==ReceiverOpenResult::Opened);
    CHECK(p.Receive(f.Pin(),bytes,sizeof(bytes))==ReceiveAdmissionResult::Accepted);
    auto permit=p.NextReceive();CHECK(permit);p.BeginShutdown();
    CHECK(p.PollShutdown().phase==ProcessShutdownPhase::Pending);
    CHECK(!p.AttachApplication({L(2)},{},Limits()));
    CHECK(p.ProgressDetach(*a.value).phase==ProcessApplicationPhase::Detaching);
    permit.value.reset(); // Abandon the real receiver lease; no callback required.
    CHECK(p.ProgressDetach(*a.value).phase==ProcessApplicationPhase::Detached);
    CHECK(p.PollShutdown().phase==ProcessShutdownPhase::ApplicationsDrained);
    CHECK(receiver.calls==0);f.Finish();
}
NativeTableConfiguration TwoLocals()
{auto c=Config();c.directory.locals.push_back({L(3),"independent-namespace","independent-listener"});return c;}
void Detach(ProcessCoordinator& p,const ProcessApplication& app)
{CHECK(p.BeginDetach(app)==NativeStatus::None);CHECK(p.ProgressDetach(app).phase==ProcessApplicationPhase::Detached);}
void PreparingRollbackAndShutdown()
{
    for(bool shutdown : {false,true}) {
        Event claimed,resume,rollback,resume_rollback;std::atomic<bool> first{true};
        ProcessCoordinatorTestAccess::Hooks hooks;
        hooks.after_claim=[&]{if(first.exchange(false)){claimed.Signal();resume.Wait();}};
        hooks.before_rollback=[&]{rollback.Signal();resume_rollback.Wait();};
        Fixture f(TwoLocals(),std::move(hooks));auto& p=*f.process;
        auto limits=Limits();if(!shutdown) limits.max_record_bytes=0;
        auto pending=std::async(std::launch::async,[&]{return p.AttachApplication({L(2)},{},limits);});claimed.Wait();
        CHECK(p.PollShutdown().preparing==1);
        CHECK(p.Receive(f.Pin(),bytes,sizeof(bytes))==ReceiveAdmissionResult::NoReceiver);
        CHECK(!p.AttachApplication({L(2)},{},Limits()));
        {
            auto unrelated=p.AttachApplication({L(3)},{},Limits());CHECK(unrelated);Detach(p,*unrelated.value);
        }
        if(shutdown) {p.BeginShutdown();CHECK(p.PollShutdown().phase==ProcessShutdownPhase::Pending);}
        resume.Signal();rollback.Wait(); // Actual rollback, not just an early Attach failure.
        CHECK(p.PollShutdown().preparing==1);
        CHECK(!p.AttachApplication({L(2)},{},Limits()));
        resume_rollback.Signal();auto failed=Finish(pending);CHECK(!failed);
        CHECK(failed.issue.code==(shutdown?NativeStatus::ServiceClosing:NativeStatus::InvalidLimits));
        CHECK(p.ReceiveUsage().retained_applications==0&&p.EgressUsage().runtimes==0);
        if(!shutdown) {auto next=p.AttachApplication({L(2)},{},Limits());CHECK(next);Detach(p,*next.value);}
        f.Finish();
    }
}
void CopyAndSelectionDetach()
{
    for(bool captured : {false,true}) {
        Event entered,resume;ProcessCoordinatorTestAccess::Hooks hooks;
        auto pause=[&]{entered.Signal();resume.Wait();};
        if(captured) hooks.copy_reserved=pause;else hooks.after_receive_selection=pause;
        Fixture f(Config(),std::move(hooks));auto& p=*f.process;Receiver old,fresh;
        auto app=p.AttachApplication({L(2)},{},Limits());CHECK(app);
        CHECK(p.OpenReceiver(*app.value,L(2),&old).result==ReceiverOpenResult::Opened);
        auto pin=f.Pin();auto receiving=std::async(std::launch::async,[&]{return p.Receive(pin,bytes,sizeof(bytes));});entered.Wait();
        CHECK(p.BeginDetach(*app.value)==NativeStatus::None);
        auto status=p.ProgressDetach(*app.value);
        CHECK(status.copying==(captured?1u:0u));
        CHECK(status.phase==(captured?ProcessApplicationPhase::Detaching:ProcessApplicationPhase::Detached));
        NativeResult<ProcessApplication> next;
        if(captured) CHECK(!p.AttachApplication({L(2)},{},Limits()));
        else {next=p.AttachApplication({L(2)},{},Limits());CHECK(next);}
        resume.Signal();const auto result=Finish(receiving);
        CHECK(result==(captured?ReceiveAdmissionResult::Revoked:ReceiveAdmissionResult::NoReceiver));
        if(captured) {
            CHECK(p.ProgressDetach(*app.value).phase==ProcessApplicationPhase::Detached);
            next=p.AttachApplication({L(2)},{},Limits());CHECK(next);
        }
        CHECK(p.OpenReceiver(*next.value,L(2),&fresh).result==ReceiverOpenResult::Opened);
        CHECK(!p.NextReceive()&&old.calls==0&&fresh.calls==0);
        CHECK(p.ReceiveUsage().records==0&&p.PollDetach(*app.value).copying==0);
        Detach(p,*next.value);f.Finish();
    }
}
namespace rtps=eprosima::fastdds::rtps;
bool Send(rtps::SenderResource& sender,SendQueue::Clock::time_point deadline)
{
    const std::vector<Locator> targets{L(10)};rtps::Locators begin(targets.cbegin()),end(targets.cend());
    const std::vector<rtps::NetworkBuffer> buffers{{bytes,sizeof(bytes)}};
    return sender.send(buffers,sizeof(bytes),&begin,&end,deadline,37);
}
struct Provider final:NativeSendProvider
{
    std::atomic<int> calls{0};std::function<void()> action;
    SendQueue::Clock::time_point expected;
    std::int32_t Send(NativeSocketId id,const Route& route,const std::uint8_t* p,std::size_t n,
            SendQueue::Clock::time_point deadline,std::int32_t priority) override
    {
        CHECK(id==70&&route.key.local_locator==L(2)&&route.key.remote_locator==L(10));
        CHECK(n==3&&p[0]==1&&p[2]==3&&deadline==expected&&priority==37);
        ++calls;if(action)action();return 0;
    }
};
void SendDrainAndUnrelatedLocal()
{
    for(bool entered : {false,true}) {
        Fixture f(TwoLocals());auto& p=*f.process;auto app=p.AttachApplication({L(2)},{},Limits());CHECK(app);
        rtps::SendResourceList senders;CHECK(p.AttachSender(*app.value,senders).ok());
        auto provider=std::make_shared<Provider>();provider->expected=SendQueue::Clock::now()+10s;
        CHECK(Send(*senders.front(),provider->expected));auto work=p.TakeSend(*app.value);CHECK(work);
        auto call=p.TryAdmitSend(*work.value,provider);CHECK(call);
        Event in_provider,resume;std::future<NativeSendRunResult> running;
        if(entered) {
            provider->action=[&]{in_provider.Signal();resume.Wait();};
            running=std::async(std::launch::async,[&]{return call.value->Run();});in_provider.Wait();
        }
        CHECK(p.BeginDetach(*app.value)==NativeStatus::None);
        CHECK(p.PollDetach(*app.value).send.admitted_outstanding==1);
        auto result=p.ProgressDetach(*app.value);
        CHECK(result.phase==(entered?ProcessApplicationPhase::Detaching:ProcessApplicationPhase::Detached));
        CHECK(!Send(*senders.front(),provider->expected));
        if(entered) {
            CHECK(!p.AttachApplication({L(2)},{},Limits()));
            auto other=p.AttachApplication({L(3)},{},Limits());CHECK(other);Detach(p,*other.value);
            resume.Signal();CHECK(Finish(running).status==NativeStatus::None);
            CHECK(p.ProgressDetach(*app.value).phase==ProcessApplicationPhase::Detached);CHECK(provider->calls==1);
        } else {CHECK(call.value->Run().outcome==NativeSendOutcome::NotEntered);CHECK(provider->calls==0);}
        auto next=p.AttachApplication({L(2)},{},Limits());CHECK(next);Detach(p,*next.value);f.Finish();
    }
}
void CallbackFreezeAndDuplicateDetach()
{
    Fixture f;auto& p=*f.process;Receiver receiver;auto app=p.AttachApplication({L(2)},{},Limits());CHECK(app);
    receiver.action=[&]{CHECK(p.BeginDetach(*app.value)==NativeStatus::None);CHECK(p.PollDetach(*app.value).dispatching==1);};
    CHECK(p.OpenReceiver(*app.value,L(2),&receiver).result==ReceiverOpenResult::Opened);
    CHECK(p.Receive(f.Pin(),bytes,sizeof(bytes))==ReceiveAdmissionResult::Accepted);auto permit=p.NextReceive();CHECK(permit);
    CHECK(permit.value->Run()==InputDeliveryResult::Delivered);CHECK(receiver.calls==1);
    auto one=std::async(std::launch::async,[&]{return p.ProgressDetach(*app.value);});
    auto two=std::async(std::launch::async,[&]{CHECK(p.BeginDetach(*app.value)==NativeStatus::None);return p.ProgressDetach(*app.value);});
    Finish(one);Finish(two);CHECK(p.ProgressDetach(*app.value).phase==ProcessApplicationPhase::Detached);f.Finish();
}
void RetainedRuntimeCapacity()
{
    Fixture f;auto& p=*f.process;std::vector<rtps::SendResourceList> held;
    for(std::size_t i=0;i<NativeEgressUsage::runtime_limit;++i) {
        auto app=p.AttachApplication({L(2)},{},Limits());CHECK(app);rtps::SendResourceList senders;
        CHECK(p.AttachSender(*app.value,senders).ok());Detach(p,*app.value);
        CHECK(!Send(*senders.front(),SendQueue::Clock::now()+1s));held.push_back(std::move(senders));
    }
    CHECK(p.EgressUsage().runtimes==NativeEgressUsage::runtime_limit);
    CHECK(!p.AttachApplication({L(2)},{},Limits()));CHECK(p.ReceiveUsage().retained_applications==0);
    held.front().clear();
    {auto next=p.AttachApplication({L(2)},{},Limits());CHECK(next);Detach(p,*next.value);}
    held.clear();CHECK(p.EgressUsage().runtimes==0);f.Finish();
}
void AttachAllocationFailures()
{
    Fixture f;auto& p=*f.process;const std::vector<Locator> locals{L(2)};const std::vector<EgressSelection> selections;
    const auto before=allocation_count;auto measured=p.AttachApplication(locals,selections,Limits());
    const auto count=allocation_count-before;CHECK(measured);Detach(p,*measured.value);measured.value.reset();
    for(std::size_t i=0;i<count;++i) {
        fail_after=static_cast<long>(i);auto result=p.AttachApplication(locals,selections,Limits());fail_after=-1;
        CHECK(!result&&result.issue.code==NativeStatus::AllocationFailure);
        CHECK(p.ReceiveUsage().retained_applications==0&&p.EgressUsage().runtimes==0);
        CHECK(p.PollShutdown().applications==0);
    }
    std::cout<<"Coordinator attach allocation_points="<<count<<'\n';f.Finish();
}
void BindingBudgetAndFairness()
{
    for(int terminal=0;terminal<5;++terminal) {
        auto config=Config();auto route=config.directory.routes.front();route.key.remote_locator=L(11);
        route.fields={"second-network","second-listener","second-dial",{"second-peer-dial"},RecordDataType::Bytes};
        config.directory.routes.push_back(route);config.directions.push_back(RouteDirection::Dial);
        auto ticks=std::make_shared<std::atomic<ProcessClock::Tick>>(0);
        auto built=ProcessCoordinator::Build(config,{},ProcessClock(ticks),10ms);CHECK(built);auto& p=**built.value;
        auto& n=p.ModelNative();auto ns=n.AcquireNamespace(L(2),config);CHECK(ns);
        auto dial=n.ReserveDial(*ns.value,{L(2),L(10)});CHECK(dial);
        CHECK(dial.value->socket_call->CallEntered()==NativeStatus::None);
        CHECK(dial.value->socket_call->Complete(NativeSocketResult::Id(70)).status==NativeStatus::None);
        auto second=n.ReserveDial(*ns.value,{L(2),L(11)});CHECK(second);
        CHECK(second.value->socket_call->CallEntered()==NativeStatus::None);
        CHECK(second.value->socket_call->Complete(NativeSocketResult::Id(71)).status==NativeStatus::None);
        auto ready=n.BeginBind(second.value->owner);CHECK(ready);CHECK(ready.value->CallEntered()==NativeStatus::None);
        CHECK(ready.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);
        // N1 intentionally admits one setup call at a time. Establish the
        // other Ready owner first, then leave this bind genuinely in flight.
        auto binding=n.BeginBind(dial.value->owner);CHECK(binding);CHECK(binding.value->CallEntered()==NativeStatus::None);
        auto pin=[&](int id){auto cb=ns.value->AdmitCallback(id);CHECK(cb);auto value=cb.value->CaptureIdentity();CHECK(value);return *value.value;};
        struct BothReceiver final:InputChannels::Receiver {
            int first=0,second=0;
            void OnDataReceived(const std::uint8_t* b,std::uint32_t size,const Locator& local,const Locator& remote) override
            {CHECK(size==3&&b[0]==1&&local==L(2));if(remote==L(10))++first;else {CHECK(remote==L(11));++second;}}
        } receiver;
        auto app=p.AttachApplication({L(2)},{},Limits());CHECK(app);
        CHECK(p.OpenReceiver(*app.value,L(2),&receiver).result==ReceiverOpenResult::Opened);
        CHECK(p.Receive(pin(70),bytes,sizeof(bytes),ProcessClock::Clock::time_point{}+7ms)==ReceiveAdmissionResult::Accepted);
        CHECK(p.Receive(pin(71),bytes,sizeof(bytes))==ReceiveAdmissionResult::Accepted);
        CHECK(n.SnapshotUsage().callbacks==0&&p.ReceiveUsage().records==2);
        auto deliver=p.NextReceive();CHECK(deliver);CHECK(deliver.value->Run()==InputDeliveryResult::Delivered);
        CHECK(receiver.second==1&&receiver.first==0); // Binding head did not block Ready owner.
        for(int i=1;i<=6;++i) {
            ticks->store(std::chrono::duration_cast<ProcessClock::Clock::duration>(std::chrono::milliseconds(i)).count());
            CHECK(!p.NextReceive());CHECK(p.ReceiveUsage().records==1&&p.ReceiveUsage().bytes==3);
            CHECK(n.SnapshotUsage().callbacks==0&&n.SnapshotUsage().receives==0);
        }
        if(terminal==0) CHECK(binding.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);
        if(terminal==1) { // Original 7ms deadline wins over the configured 10ms hold; polls never extend it.
            ticks->store(std::chrono::duration_cast<ProcessClock::Clock::duration>(7ms).count());
            CHECK(binding.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);
        }
        if(terminal==2) dial.value->owner.RequestClose();
        if(terminal==3) {
            Detach(p,*app.value);
            CHECK(binding.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);
            app=p.AttachApplication({L(2)},{},Limits());CHECK(app);
            CHECK(p.OpenReceiver(*app.value,L(2),&receiver).result==ReceiverOpenResult::Opened);
        }
        if(terminal==4) CHECK(binding.value->Complete(SyncBindResult::Error(-95)).status==NativeStatus::None);
        auto last=p.NextReceive();CHECK(bool(last)==(terminal==0));
        if(last) CHECK(last.value->Run()==InputDeliveryResult::Delivered);
        CHECK(receiver.first==(terminal==0?1:0)&&p.ReceiveUsage().records==0&&p.ReceiveUsage().bytes==0);
        Detach(p,*app.value);p.BeginShutdown();CHECK(p.PollShutdown().phase==ProcessShutdownPhase::ApplicationsDrained);
    }
}
void TakenSendCannotAdmitAfterDetach()
{
    Fixture f;auto& p=*f.process;auto app=p.AttachApplication({L(2)},{},Limits());CHECK(app);
    rtps::SendResourceList senders;CHECK(p.AttachSender(*app.value,senders).ok());
    auto provider=std::make_shared<Provider>();provider->expected=SendQueue::Clock::now()+10s;
    CHECK(Send(*senders.front(),provider->expected));auto work=p.TakeSend(*app.value);CHECK(work);
    Detach(p,*app.value);auto next=p.AttachApplication({L(2)},{},Limits());CHECK(next);
    CHECK(!p.TryAdmitSend(*work.value,provider));CHECK(provider->calls==0);Detach(p,*next.value);f.Finish();
}
void PublicationFaultRollback()
{
    ProcessCoordinatorTestAccess::Hooks hooks;bool fail=true;
    hooks.before_publish=[&]{if(fail)throw std::bad_alloc();};
    Fixture f(Config(),std::move(hooks));auto& p=*f.process;
    CHECK(!p.AttachApplication({L(2)},{},Limits()));
    CHECK(p.ReceiveUsage().retained_applications==0&&p.EgressUsage().runtimes==0&&p.PollShutdown().applications==0);
    fail=false;auto app=p.AttachApplication({L(2)},{},Limits());CHECK(app);Detach(p,*app.value);f.Finish();
}
void ReturnedProviderStillSettling()
{
    Fixture f(TwoLocals());auto& p=*f.process;Event cleanup,resume;std::atomic<int> calls{0};
    struct CleanupProvider final:NativeSendProvider {
        Event& cleanup;Event& resume;std::atomic<int>& calls;
        CleanupProvider(Event& c,Event& r,std::atomic<int>& n):cleanup(c),resume(r),calls(n) {}
        ~CleanupProvider() override {cleanup.Signal();resume.Wait();}
        std::int32_t Send(NativeSocketId,const Route&,const std::uint8_t*,std::size_t,
                SendQueue::Clock::time_point,std::int32_t) override {++calls;return 0;}
    };
    auto app=p.AttachApplication({L(2)},{},Limits());CHECK(app);
    rtps::SendResourceList senders;CHECK(p.AttachSender(*app.value,senders).ok());
    CHECK(Send(*senders.front(),SendQueue::Clock::now()+10s));auto work=p.TakeSend(*app.value);CHECK(work);
    auto provider=std::make_shared<CleanupProvider>(cleanup,resume,calls);
    auto call=p.TryAdmitSend(*work.value,provider);CHECK(call);provider.reset();
    auto running=std::async(std::launch::async,[&]{return call.value->Run();});cleanup.Wait();
    // Run has really returned from Send and reached last-provider destruction,
    // not an unrun invocation's cancellation/destruction path.
    CHECK(calls==1);CHECK(p.BeginDetach(*app.value)==NativeStatus::None);
    auto pending=p.ProgressDetach(*app.value);
    CHECK(pending.phase==ProcessApplicationPhase::Detaching&&pending.send.admitted_outstanding==1);
    CHECK(!p.AttachApplication({L(2)},{},Limits()));
    auto unrelated=std::async(std::launch::async,[&]{
        auto other=p.AttachApplication({L(3)},{},Limits());CHECK(other);Detach(p,*other.value);return true;
    });CHECK(Finish(unrelated));
    resume.Signal();CHECK(Finish(running).status==NativeStatus::None);
    CHECK(p.PollDetach(*app.value).send.admitted_outstanding==0);
    CHECK(p.ProgressDetach(*app.value).phase==ProcessApplicationPhase::Detached);
    auto next=p.AttachApplication({L(2)},{},Limits());CHECK(next);Detach(p,*next.value);f.Finish();
}
}
void ReceiveObservationBarriers()
{
    for(const auto stage:{ReceiveStage::Captured,ReceiveStage::OwnedCopy,ReceiveStage::Admitted,ReceiveStage::Running}) {
        struct Observer final:ProcessReceiveObserver {
            ReceiveStage pause;Event entered,resume;std::atomic<unsigned> released{0},settled{0},bad{0};
            ReceiverIdentity original;const std::uint8_t* sdk=nullptr;
            void OnReceive(ReceiveStage s,const ReceiveDiagnostic& d,const std::uint8_t* b,std::size_t n) noexcept override {
                if(!(d.identity==original)||d.correlation!=89) ++bad;
                if(s==ReceiveStage::OwnedCopy&&(b==sdk||n!=3||b[0]!=1||b[2]!=3)) ++bad;
                if(s==ReceiveStage::Settled) ++settled;
                if(s==ReceiveStage::RecordReleased) ++released;
                if(s==pause) {entered.Signal();resume.Wait();}
            }
        };
        auto observer=std::make_shared<Observer>();observer->pause=stage;observer->sdk=bytes;
        Fixture f(TwoLocals(),{},observer);auto& p=*f.process;Receiver old,fresh;
        auto app=p.AttachApplication({L(2)},{},Limits());CHECK(app);
        auto binding=p.OpenReceiver(*app.value,L(2),&old);CHECK(binding.result==ReceiverOpenResult::Opened);
        observer->original=p.ObserveReceiver(binding.binding);
        const bool before_queue=stage==ReceiveStage::Captured||stage==ReceiveStage::OwnedCopy;
        if(!before_queue) CHECK(p.Receive(f.Pin(),bytes,sizeof(bytes),ProcessClock::Clock::time_point::max(),89)==ReceiveAdmissionResult::Accepted);
        auto activity=std::async(std::launch::async,[&]{
            if(before_queue) return p.Receive(f.Pin(),bytes,sizeof(bytes),ProcessClock::Clock::time_point::max(),89)==ReceiveAdmissionResult::Revoked;
            auto invocation=p.NextReceive();CHECK(invocation);return invocation.value->Run()==InputDeliveryResult::Delivered;
        });
        observer->entered.Wait();CHECK(p.BeginDetach(*app.value)==NativeStatus::None);
        const auto pending=p.ProgressDetach(*app.value);CHECK(pending.phase==ProcessApplicationPhase::Detaching);
        CHECK(before_queue?pending.copying==1:pending.dispatching==1);
        auto q=p.AttachApplication({L(3)},{},Limits());CHECK(q);Detach(p,*q.value);
        observer->resume.Signal();CHECK(Finish(activity));
        CHECK(p.ProgressDetach(*app.value).phase==ProcessApplicationPhase::Detached);
        CHECK(observer->released==1&&observer->bad==0&&observer->settled==(before_queue?0u:1u));
        auto replacement=p.AttachApplication({L(2)},{},Limits());CHECK(replacement);
        CHECK(p.OpenReceiver(*replacement.value,L(2),&fresh).result==ReceiverOpenResult::Opened);
        CHECK(!p.NextReceive()&&fresh.calls==0&&old.calls==(before_queue?0:1));
        Detach(p,*replacement.value);f.Finish();
    }
}

void BidirectionalSendKeepsOriginalOwner()
{
    auto config=Config();config.directions[0]=RouteDirection::Both;
    auto built=ProcessCoordinator::Build(config);CHECK(built);auto& p=**built.value;auto& native=p.Native();
    auto ns=native.AcquireNamespace(L(2),config);CHECK(ns);
    auto inbound=native.ObserveInbound(*ns.value,702,{"same-namespace","peer-network","peer-dial",RecordDataType::Bytes});
    CHECK(inbound.accepted&&inbound.owner);
    auto app=p.AttachApplication({L(2)},{},Limits());CHECK(app);rtps::SendResourceList senders;
    CHECK(p.AttachSender(*app.value,senders).ok());const auto deadline=SendQueue::Clock::now()+10s;
    CHECK(Send(*senders.front(),deadline));auto original=p.TakeSend(*app.value);CHECK(original);
    auto dial=native.ReserveDial(*ns.value,{L(2),L(10)});CHECK(dial);
    CHECK(dial.value->socket_call->CallEntered()==NativeStatus::None);
    CHECK(dial.value->socket_call->Complete(NativeSocketResult::Id(703)).status==NativeStatus::None);
    auto bind=native.BeginBind(dial.value->owner);CHECK(bind);CHECK(bind.value->CallEntered()==NativeStatus::None);
    CHECK(bind.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);
    struct RecordingProvider final:NativeSendProvider {
        int calls=0;NativeSocketId id=-1;
        std::int32_t Send(NativeSocketId socket,const Route&,const std::uint8_t*,std::size_t,
                SendQueue::Clock::time_point,std::int32_t) override {++calls;id=socket;return 0;}
    };
    auto provider=std::make_shared<RecordingProvider>();
    auto invocation=p.TryAdmitSend(*original.value,provider);CHECK(invocation);
    CHECK(invocation.value->Run().status==NativeStatus::None&&provider->calls==1&&provider->id==702);
    CHECK(Send(*senders.front(),deadline));auto next=p.TakeSend(*app.value);CHECK(next);
    CHECK(dial.value->owner.RequestClose()==NativeStatus::None);
    auto stale=p.TryAdmitSend(*next.value,provider);CHECK(!stale&&provider->calls==1);
    CHECK(Send(*senders.front(),deadline));auto fallback=p.TakeSend(*app.value);CHECK(fallback);
    auto fallback_call=p.TryAdmitSend(*fallback.value,provider);CHECK(fallback_call);
    CHECK(fallback_call.value->Run().status==NativeStatus::None&&provider->calls==2&&provider->id==702);
    Detach(p,*app.value);p.BeginShutdown();CHECK(p.PollShutdown().phase==ProcessShutdownPhase::ApplicationsDrained);
}

void ObservableLifecycleIdentities()
{
    Fixture f(TwoLocals());auto& p=*f.process;Receiver old,fresh,q;
    auto a=p.AttachApplication({L(2)},{},Limits());auto peer=p.AttachApplication({L(3)},{},Limits());CHECK(a&&peer);
    auto r1=p.OpenReceiver(*a.value,L(2),&old);auto qr=p.OpenReceiver(*peer.value,L(3),&q);
    CHECK(r1.result==ReceiverOpenResult::Opened&&qr.result==ReceiverOpenResult::Opened);
    const auto old_id=p.ObserveReceiver(r1.binding),q_id=p.ObserveReceiver(qr.binding);
    CHECK(old_id.application_incarnation&&old_id.receiver_incarnation&&old_id.logical_token);
    CHECK(old_id.application_incarnation!=q_id.application_incarnation);
    CHECK(old_id.receiver_incarnation!=q_id.receiver_incarnation&&old_id.logical_token!=q_id.logical_token);
    CHECK(&p.Native()==&p.ModelNative());
    CHECK(p.Receive(f.Pin(),bytes,sizeof(bytes),ProcessClock::Clock::time_point::max(),73)==ReceiveAdmissionResult::Accepted);
    auto invocation=p.NextReceive();CHECK(invocation);
    const auto record=invocation.value->Observation();CHECK(record.correlation==73&&record.identity==old_id);
    CHECK(p.BeginDetach(*a.value)==NativeStatus::None);
    CHECK(p.ProgressDetach(*a.value).phase==ProcessApplicationPhase::Detaching);
    CHECK(invocation.value->Observation().identity==old_id);
    CHECK(invocation.value->Run()==InputDeliveryResult::Delivered);
    CHECK(p.ProgressDetach(*a.value).phase==ProcessApplicationPhase::Detached);
    CHECK(p.Receive(f.Pin(),bytes,sizeof(bytes),ProcessClock::Clock::time_point::max(),74)==ReceiveAdmissionResult::NoReceiver);
    auto b=p.AttachApplication({L(2)},{},Limits());CHECK(b);
    auto r2=p.OpenReceiver(*b.value,L(2),&fresh);CHECK(r2.result==ReceiverOpenResult::Opened);
    const auto new_id=p.ObserveReceiver(r2.binding);
    CHECK(new_id.application_incarnation>old_id.application_incarnation);
    CHECK(new_id.receiver_incarnation>old_id.receiver_incarnation&&new_id.logical_token>old_id.logical_token);
    CHECK(p.ObserveReceiver(qr.binding)==q_id&&p.ObserveReceiver(r1.binding)==old_id);
    CHECK(!p.NextReceive()&&fresh.calls==0&&old.calls==1);
    Detach(p,*b.value);Detach(p,*peer.value);f.Finish();
}

int main()
{
    try {
        BidirectionalSendKeepsOriginalOwner();std::cout<<"PASS N4-BIDIRECTIONAL-ORIGINAL-SEND\n";
        ReceiveObservationBarriers();std::cout<<"PASS N4-RECEIVE-OBSERVATION-BARRIERS\n";
        ObservableLifecycleIdentities();std::cout<<"PASS N4-OBSERVABLE-LIFECYCLE-IDENTITY\n";
        SameLocalRebuild();std::cout<<"PASS N3-COORDINATOR-SAME-LOCAL-80\n";
        GapAndNoReceiver();std::cout<<"PASS N3-COORDINATOR-NO-RECIPIENT\n";
        ExplicitShutdownPending();std::cout<<"PASS N3-COORDINATOR-SHUTDOWN-PENDING\n";
        PreparingRollbackAndShutdown();std::cout<<"PASS N3-COORDINATOR-PREPARING-ROLLBACK\n";
        CopyAndSelectionDetach();std::cout<<"PASS N3-COORDINATOR-COPY-SELECTION\n";
        SendDrainAndUnrelatedLocal();std::cout<<"PASS N3-COORDINATOR-SEND-DRAIN\n";
        CallbackFreezeAndDuplicateDetach();std::cout<<"PASS N3-COORDINATOR-CALLBACK-DETACH\n";
        RetainedRuntimeCapacity();std::cout<<"PASS N3-COORDINATOR-RETAINED-CAPACITY\n";
        AttachAllocationFailures();std::cout<<"PASS N3-COORDINATOR-ATTACH-OOM\n";
        BindingBudgetAndFairness();std::cout<<"PASS N3-COORDINATOR-BINDING-FAIRNESS\n";
        TakenSendCannotAdmitAfterDetach();std::cout<<"PASS N3-COORDINATOR-TAKEN-REVOKE\n";
        PublicationFaultRollback();std::cout<<"PASS N3-COORDINATOR-PUBLICATION-FAULT\n";
        ReturnedProviderStillSettling();std::cout<<"PASS N3-COORDINATOR-RETURNED-SETTLING\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
