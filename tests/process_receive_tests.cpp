#include "../src/ProcessReceiveInternal.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
thread_local bool fail_allocations=false;
thread_local void (*before_allocation)()=nullptr;
void* operator new(std::size_t n)
{
    if(auto hook=before_allocation) {before_allocation=nullptr;hook();}
    if(fail_allocations) throw std::bad_alloc();
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
class ProcessReceiveTestAccess
{
public:
    static ProcessClock Clock(SendQueue::Clock::time_point now)
    {return ProcessClock(std::make_shared<std::atomic<ProcessClock::Tick>>(now.time_since_epoch().count()));}
    static auto Admit(ApplicationAttachments& a,NativeOwnerTable& n,const ApplicationClaim& c,
            const NativeIdentityConstraint& pin,const void* bytes,std::size_t size,
            SendQueue::Clock::time_point expiry,SendQueue::Clock::time_point now)
    {return detail::ProcessReceiveBridge::Admit(a,n,c,pin,bytes,size,expiry,Clock(now));}
    static auto Enter(ApplicationAttachments& a,NativeOwnerTable& n,ReceiveWork& w,SendQueue::Clock::time_point now)
    {return detail::ProcessReceiveBridge::TryEnter(a,n,w,Clock(now));}
    static auto Next(ApplicationAttachments& a,NativeOwnerTable& n,SendQueue::Clock::time_point now)
    {return detail::ProcessReceiveBridge::Next(a,n,Clock(now));}
    static auto NextWithClock(ApplicationAttachments& a,NativeOwnerTable& n,const ProcessClock& clock)
    {return detail::ProcessReceiveBridge::Next(a,n,clock);}
};
class ApplicationAttachmentsTestAccess
{
public:
    static std::unique_ptr<ApplicationAttachments> Make(const Locator& l,std::function<void()> copy)
    {return std::unique_ptr<ApplicationAttachments>(new ApplicationAttachments({l},{},{{},std::move(copy),{},{},{}}));}
    static auto Copying(ApplicationAttachments& a,const ApplicationClaim& c) {return a.TestCopying(c);}
    static auto Core(ApplicationAttachments& a,const ApplicationClaim& c) {return a.TestCore(c).lock();}
};
}
namespace {
struct Event {
    std::mutex gate;std::condition_variable cv;bool set=false;
    void Signal(){std::lock_guard<std::mutex> l(gate);set=true;cv.notify_all();}
    void Wait(){std::unique_lock<std::mutex> l(gate);if(!cv.wait_for(l,3s,[&]{return set;})) std::abort();}
};
template<class T> T Finish(std::future<T>& f) {if(f.wait_for(3s)!=std::future_status::ready)std::abort();return f.get();}
Locator L(std::uint8_t id){EndpointId e{};e[15]=id;Locator l;CHECK(LocatorCodec::Encode(e,l));return l;}
struct Receiver final : InputChannels::Receiver {
    std::atomic<int> calls{0};
    void OnDataReceived(const std::uint8_t* p,std::uint32_t n,const Locator& l,const Locator& r) override
    {CHECK(n==3&&p[0]==1&&p[2]==3&&l==L(2)&&r==L(10));++calls;}
};
struct Fixture {
    NativeTableConfiguration config;
    std::unique_ptr<NativeOwnerTable> table;
    EndpointDirectory::Snapshot directory;
    NamespaceLease space;NativeOwnerLease owner;std::optional<BindCallTicket> binding;
    explicit Fixture(bool ready=true) {
        config.service_pkg_name="com.kaihong.mdds";
        config.directory.locals={{L(2),"ns-original","listen-original"}};
        Route route;route.key={L(2),L(10)};
        route.fields={"network-original","listen-peer","dial-original",{"dial-peer"},RecordDataType::Bytes};
        config.directory.routes.push_back(route);config.directions.push_back(RouteDirection::Dial);
        auto built=NativeOwnerTable::Build(config);CHECK(built);table=std::move(*built.value);
        auto d=EndpointDirectory::Build(config.directory);CHECK(d.value);directory=*d.value;
        auto ns=table->AcquireNamespace(L(2),config);CHECK(ns);space=*ns.value;
        auto dial=table->ReserveDial(space,route.key);CHECK(dial);owner=dial.value->owner;
        CHECK(dial.value->socket_call->CallEntered()==NativeStatus::None);
        CHECK(dial.value->socket_call->Complete(NativeSocketResult::Id(70)).status==NativeStatus::None);
        auto b=table->BeginBind(owner);CHECK(b);binding.emplace(std::move(*b.value));
        CHECK(binding->CallEntered()==NativeStatus::None);if(ready)Ready();
    }
    void Ready(){CHECK(binding->Complete(SyncBindResult::Success()).status==NativeStatus::None);binding.reset();}
    NativeIdentityConstraint Pin(){auto cb=space.AdmitCallback(70);CHECK(cb);auto pin=cb.value->CaptureIdentity();CHECK(pin);return *pin.value;}
    ApplicationClaim Claim(ApplicationAttachments& a){auto c=a.ClaimApplication({L(2)},directory);CHECK(c.result==ApplicationClaimResult::Claimed);return c.claim;}
};
constexpr std::uint8_t payload[]={1,2,3};
auto Admit(ApplicationAttachments& a,Fixture& f,const ApplicationClaim& c,const NativeIdentityConstraint& p,
        SendQueue::Clock::time_point expiry=SendQueue::Clock::time_point::max(),
        SendQueue::Clock::time_point now=SendQueue::Clock::now())
{return ProcessReceiveTestAccess::Admit(a,*f.table,c,p,payload,sizeof(payload),expiry,now);}
void OriginalIdentityAndLease()
{
    Fixture f;Receiver receiver;ApplicationAttachments a({L(2)});auto c=f.Claim(a);
    CHECK(a.OpenReceiver(c,L(2),&receiver).result==ReceiverOpenResult::Opened);auto pin=f.Pin();
    CHECK(f.table->SnapshotUsage().callbacks==0);
    CHECK(Admit(a,f,c,pin)==ReceiveAdmissionResult::Accepted);
    CHECK(a.TryTake().Deliver()!=InputDeliveryResult::Delivered);CHECK(receiver.calls==0);
    CHECK(Admit(a,f,c,pin)==ReceiveAdmissionResult::Accepted);auto w=a.TryTake();
    auto permit=ProcessReceiveTestAccess::Enter(a,*f.table,w,SendQueue::Clock::now());CHECK(permit);CHECK(!w);
    CHECK(f.table->SnapshotUsage().receives==1&&f.table->SnapshotUsage().callbacks==0);
    CHECK(a.SnapshotUsage().delivering==1);f.owner.RequestClose();
    CHECK(permit.value->Run()==InputDeliveryResult::Delivered);
    CHECK(permit.value->Run()!=InputDeliveryResult::Delivered);CHECK(receiver.calls==1);
    CHECK(f.table->SnapshotUsage().receives==0&&a.SnapshotUsage().records==0);
    a.AwaitDetached(c);
}
void ReceiverAndApplicationIsolation()
{
    Fixture f;Receiver old,fresh;Event copied,resume;
    auto a=ApplicationAttachmentsTestAccess::Make(L(2),[&]{copied.Signal();resume.Wait();});auto c=f.Claim(*a);
    CHECK(a->OpenReceiver(c,L(2),&old).result==ReceiverOpenResult::Opened);auto pin=f.Pin();
    auto copying=std::async(std::launch::async,[&]{return Admit(*a,f,c,pin);});copied.Wait();
    CHECK(ApplicationAttachmentsTestAccess::Copying(*a,c)==1);
    a->AwaitDetached(c);auto next=f.Claim(*a);CHECK(a->OpenReceiver(next,L(2),&fresh).result==ReceiverOpenResult::Opened);
    resume.Signal();CHECK(Finish(copying)==ReceiveAdmissionResult::Revoked);
    CHECK(ApplicationAttachmentsTestAccess::Copying(*a,c)==0&&a->SnapshotUsage().records==0);
    CHECK(old.calls==0&&fresh.calls==0);a->AwaitDetached(next);
    // Same application, different receiver incarnation must also reject.
    ApplicationAttachments b({L(2)});auto cb=f.Claim(b);auto binding=b.OpenReceiver(cb,L(2),&old);CHECK(binding.result==ReceiverOpenResult::Opened);
    CHECK(Admit(b,f,cb,pin)==ReceiveAdmissionResult::Accepted);auto work=b.TryTake();b.CloseReceiver(binding.binding);
    CHECK(b.OpenReceiver(cb,L(2),&fresh).result==ReceiverOpenResult::Opened);
    CHECK(!ProcessReceiveTestAccess::Enter(b,*f.table,work,SendQueue::Clock::now()));work={};
    CHECK(old.calls==0&&fresh.calls==0);b.AwaitDetached(cb);
}
void BindingHeldAndExpired()
{
    for(bool ready : {false,true}) {
        Fixture f(false);Receiver receiver;ApplicationAttachments a({L(2)});auto c=f.Claim(a);
        CHECK(a.OpenReceiver(c,L(2),&receiver).result==ReceiverOpenResult::Opened);auto pin=f.Pin();
        const SendQueue::Clock::time_point now{};CHECK(Admit(a,f,c,pin,now+10ms,now)==ReceiveAdmissionResult::Accepted);
        CHECK(!ProcessReceiveTestAccess::Next(a,*f.table,now));
        CHECK(a.SnapshotUsage().queued==1&&f.table->SnapshotUsage().callbacks==0&&f.table->SnapshotUsage().receives==0);
        if(ready) f.Ready();
        auto next=ProcessReceiveTestAccess::Next(a,*f.table,ready?now+1ms:now+10ms);
        CHECK(bool(next)==ready);if(next)CHECK(next.value->Run()==InputDeliveryResult::Delivered);
        CHECK(a.SnapshotUsage().records==0&&receiver.calls==(ready?1:0));a.AwaitDetached(c);
    }
}
}
void ExpirySurvivesAllocationFailure()
{
    for(bool ready : {false,true}) {
        Fixture f(false);Receiver receiver;ApplicationAttachments a({L(2)});auto c=f.Claim(a);
        CHECK(a.OpenReceiver(c,L(2),&receiver).result==ReceiverOpenResult::Opened);
        const SendQueue::Clock::time_point now{};
        CHECK(Admit(a,f,c,f.Pin(),now+10ms,now)==ReceiveAdmissionResult::Accepted);
        if(ready) f.Ready();
        const auto expired=ProcessReceiveTestAccess::Clock(now+20ms);
        fail_allocations=true;
        auto next=ProcessReceiveTestAccess::NextWithClock(a,*f.table,expired);
        fail_allocations=false;
        CHECK(!next&&a.SnapshotUsage().records==0&&receiver.calls==0);
        CHECK(f.table->SnapshotUsage().receives==0);a.AwaitDetached(c);
    }
}
void NativeInvalidationAndAbandonedPermit()
{
    for(bool take : {false,true}) {
        Fixture f;Receiver receiver;ApplicationAttachments a({L(2)});auto c=f.Claim(a);
        CHECK(a.OpenReceiver(c,L(2),&receiver).result==ReceiverOpenResult::Opened);
        CHECK(Admit(a,f,c,f.Pin())==ReceiveAdmissionResult::Accepted);
        auto work=take?a.TryTake():ReceiveWork{};f.owner.RequestClose();
        if(take) CHECK(!ProcessReceiveTestAccess::Enter(a,*f.table,work,SendQueue::Clock::now()));
        else CHECK(!ProcessReceiveTestAccess::Next(a,*f.table,SendQueue::Clock::now()));
        work={};CHECK(receiver.calls==0&&a.SnapshotUsage().records==0);a.AwaitDetached(c);
    }
    Fixture f,foreign;Receiver receiver;ApplicationAttachments a({L(2)});auto c=f.Claim(a);
    CHECK(a.OpenReceiver(c,L(2),&receiver).result==ReceiverOpenResult::Opened);
    CHECK(Admit(a,f,c,f.Pin())==ReceiveAdmissionResult::Accepted);auto work=a.TryTake();
    // Same numeric Socket ID in another State cannot supply a new identity.
    CHECK(!ProcessReceiveTestAccess::Enter(a,*foreign.table,work,SendQueue::Clock::now()));
    CHECK(work&&a.SnapshotUsage().taken==1);
    {
        auto permit=ProcessReceiveTestAccess::Enter(a,*f.table,work,SendQueue::Clock::now());CHECK(permit);
        CHECK(a.SnapshotUsage().delivering==1&&f.table->SnapshotUsage().receives==1);
    }
    CHECK(receiver.calls==0&&a.SnapshotUsage().records==0&&f.table->SnapshotUsage().receives==0);
    a.AwaitDetached(c);
}
void CopyCancellation()
{
    for(bool native : {false,true}) {
        Fixture f;Receiver receiver;Event captured,resume;
        auto a=ApplicationAttachmentsTestAccess::Make(L(2),[&]{captured.Signal();resume.Wait();});auto c=f.Claim(*a);
        const auto binding=a->OpenReceiver(c,L(2),&receiver);CHECK(binding.result==ReceiverOpenResult::Opened);
        auto pin=f.Pin();auto copy=std::async(std::launch::async,[&]{return Admit(*a,f,c,pin);});captured.Wait();
        CHECK(ApplicationAttachmentsTestAccess::Copying(*a,c)==1);
        if(native) f.owner.RequestClose();else a->CloseReceiver(binding.binding);
        resume.Signal();CHECK(Finish(copy)==ReceiveAdmissionResult::Revoked);
        CHECK(ApplicationAttachmentsTestAccess::Copying(*a,c)==0&&a->SnapshotUsage().records==0);
        CHECK(receiver.calls==0);a->AwaitDetached(c);
    }
}
void BindingRequiresFiniteDeadline()
{
    Fixture f(false);Receiver receiver;ApplicationAttachments a({L(2)});auto c=f.Claim(a);
    CHECK(a.OpenReceiver(c,L(2),&receiver).result==ReceiverOpenResult::Opened);
    CHECK(Admit(a,f,c,f.Pin())==ReceiveAdmissionResult::InvalidDeadline);
    CHECK(a.SnapshotUsage().records==0&&ApplicationAttachmentsTestAccess::Copying(a,c)==0);a.AwaitDetached(c);
}
thread_local Event* allocation_entered=nullptr;
thread_local Event* allocation_resume=nullptr;
void PausePermitAllocation() {allocation_entered->Signal();allocation_resume->Wait();}
void ReceiverReplacementAfterPreflight()
{
    Fixture f;Receiver old,fresh;ApplicationAttachments a({L(2)});auto c=f.Claim(a);
    CHECK(a.OpenReceiver(c,L(2),&old).result==ReceiverOpenResult::Opened);
    CHECK(Admit(a,f,c,f.Pin())==ReceiveAdmissionResult::Accepted);
    auto core=ApplicationAttachmentsTestAccess::Core(a,c);CHECK(core);
    const auto clock=ProcessReceiveTestAccess::Clock(SendQueue::Clock::now());
    Event entered,resume;
    auto pending=std::async(std::launch::async,[&]{
        allocation_entered=&entered;allocation_resume=&resume;before_allocation=PausePermitAllocation;
        return ProcessReceiveTestAccess::NextWithClock(a,*f.table,clock);
    });
    entered.Wait(); // First allocation is after the nonallocating preflight.
    // Deliberately keep the outer AA incarnation intact. Only the real Input
    // receiver changes: an outer incarnation check alone cannot catch this.
    core->Close(L(2));CHECK(core->Open(L(2),&fresh)==InputOpenResult::Opened);
    resume.Signal();CHECK(!Finish(pending));
    CHECK(a.SnapshotUsage().records==0&&old.calls==0&&fresh.calls==0&&core->SnapshotUsage().in_flight==0);
    CHECK(f.table->SnapshotUsage().receives==0);a.AwaitDetached(c);
}
int main(){try{
    OriginalIdentityAndLease();std::cout<<"PASS N3-PINNED-LEASE\n";
    ReceiverAndApplicationIsolation();std::cout<<"PASS N3-PINNED-ISOLATION\n";
    BindingHeldAndExpired();std::cout<<"PASS N3-PINNED-BINDING\n";
    ExpirySurvivesAllocationFailure();std::cout<<"PASS N3-PINNED-EXPIRY-OOM\n";
    NativeInvalidationAndAbandonedPermit();std::cout<<"PASS N3-PINNED-NATIVE-ABANDON\n";
    CopyCancellation();std::cout<<"PASS N3-PINNED-COPY-CANCEL\n";
    BindingRequiresFiniteDeadline();std::cout<<"PASS N3-PINNED-FINITE-DEADLINE\n";
    ReceiverReplacementAfterPreflight();std::cout<<"PASS N3-PINNED-INPUT-REPLACEMENT\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
