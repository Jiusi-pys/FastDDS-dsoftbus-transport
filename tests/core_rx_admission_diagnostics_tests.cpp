#include <fastdds_dsoftbus/dsoftbus/ProcessCoordinator.hpp>
#include <fastdds_dsoftbus/dsoftbus/ReceiveAdmissionDiagnostic.hpp>
#include "../src/ProcessReceiveInternal.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
thread_local std::size_t allocation_count=0;
thread_local bool fail_next_allocation=false;
void* operator new(std::size_t n) {
    ++allocation_count;
    if(fail_next_allocation){fail_next_allocation=false;throw std::bad_alloc();}
    if(auto p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace fastdds_dsoftbus::dsoftbus;
using namespace std::chrono_literals;
#define CHECK(x) do {if(!(x)) throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x);} while(false)
namespace fastdds_dsoftbus::dsoftbus {
class ProcessReceiveTestAccess {
public:
    static ProcessClock Clock(ProcessClock::Clock::time_point now={})
    {return ProcessClock(std::make_shared<std::atomic<ProcessClock::Tick>>(now.time_since_epoch().count()));}
    template<class B=detail::ProcessReceiveBridge> static auto AdmitImpl(ApplicationAttachments& a,NativeOwnerTable& n,
            const ApplicationClaim& c,const NativeIdentityConstraint& p,const void* bytes,std::size_t size,
            ProcessClock::Clock::time_point expiry,const ProcessClock& clock,ReceiveAdmissionDiagnostic* d,int)
        ->decltype(B::Admit(a,n,c,p,bytes,size,expiry,clock,0,{},d))
    {return B::Admit(a,n,c,p,bytes,size,expiry,clock,0,{},d);}
    template<class B=detail::ProcessReceiveBridge> static auto AdmitImpl(ApplicationAttachments& a,NativeOwnerTable& n,
            const ApplicationClaim& c,const NativeIdentityConstraint& p,const void* bytes,std::size_t size,
            ProcessClock::Clock::time_point expiry,const ProcessClock& clock,ReceiveAdmissionDiagnostic*,long)
        ->decltype(B::Admit(a,n,c,p,bytes,size,expiry,clock))
    {return B::Admit(a,n,c,p,bytes,size,expiry,clock);}
};
class ApplicationAttachmentsTestAccess {
public:
    static std::unique_ptr<ApplicationAttachments> Make(const std::vector<Locator>& locals,AttachmentLimits limits={},
            std::function<void()> copy={},std::function<void()> publish={})
    {return std::unique_ptr<ApplicationAttachments>(new ApplicationAttachments(locals,limits,{{},std::move(copy),std::move(publish),{},{}}));}
    template<class A> static auto CaptureImpl(A& a,const ApplicationClaim& c,const Locator& l,const Locator& r,
            const NativeIdentityConstraint& p,ProcessClock::Clock::time_point expiry,std::size_t size,
            const ProcessClock& clock,detail::AttachmentReceiveCapture& capture,ReceiveAdmissionDiagnostic* d,int)
        ->decltype(a.CaptureReceive(c,l,r,p,expiry,size,clock,capture,d))
    {return a.CaptureReceive(c,l,r,p,expiry,size,clock,capture,d);}
    template<class A> static auto CaptureImpl(A& a,const ApplicationClaim& c,const Locator& l,const Locator& r,
            const NativeIdentityConstraint& p,ProcessClock::Clock::time_point expiry,std::size_t size,
            const ProcessClock& clock,detail::AttachmentReceiveCapture& capture,ReceiveAdmissionDiagnostic*,long)
        ->decltype(a.CaptureReceive(c,l,r,p,expiry,size,clock,capture))
    {return a.CaptureReceive(c,l,r,p,expiry,size,clock,capture);}
    template<class A> static auto CopyImpl(A& a,detail::AttachmentReceiveCapture& c,const void* p,ReceiveAdmissionDiagnostic* d,int)
        ->decltype(a.CopyReceive(c,p,d)) {return a.CopyReceive(c,p,d);}
    template<class A> static auto CopyImpl(A& a,detail::AttachmentReceiveCapture& c,const void* p,ReceiveAdmissionDiagnostic*,long)
        ->decltype(a.CopyReceive(c,p)) {return a.CopyReceive(c,p);}
    template<class A> static auto CommitImpl(A& a,detail::AttachmentReceiveCapture& c,const ProcessClock& clock,ReceiveAdmissionDiagnostic* d,int)
        ->decltype(a.CommitReceive(c,clock,d)) {return a.CommitReceive(c,clock,d);}
    template<class A> static auto CommitImpl(A& a,detail::AttachmentReceiveCapture& c,const ProcessClock& clock,ReceiveAdmissionDiagnostic*,long)
        ->decltype(a.CommitReceive(c,clock)) {return a.CommitReceive(c,clock);}
    template<class A> static auto Saturate(A& a,const ApplicationClaim& c,std::size_t value,int)
        ->decltype(a.TestSetCopyingActivity(c,value),bool()) {a.TestSetCopyingActivity(c,value);return true;}
    template<class A> static bool Saturate(A&,const ApplicationClaim&,std::size_t,long) {return false;}
};
}
namespace {
bool baseline=false;
constexpr std::uint8_t bytes[]={1,2,3};
constexpr auto never=ProcessClock::Clock::time_point::max();
Locator L(std::uint8_t id) {EndpointId e{};e[15]=id;Locator l;CHECK(LocatorCodec::Encode(e,l));return l;}
void Diagnosis(const ReceiveAdmissionDiagnostic& d,RxAdmissionStage stage,RxAdmissionReason reason,ReceiveAdmissionResult result)
{
    if(baseline)return;
    if(!d.result_known) throw std::runtime_error("NEW_DIAGNOSTIC_NOT_PRESENT");
    CHECK(d.stage==stage);CHECK(d.reason==reason);CHECK(d.result==static_cast<int>(result));
}
template<class D> auto RecordLimit(const D& d,std::size_t expected,int)
    ->decltype(d.record_limit_known,d.record_limit,void())
{CHECK(d.record_limit_known);CHECK(d.record_limit==expected);}
template<class D> void RecordLimit(const D&,std::size_t,long)
{throw std::runtime_error("NEW_DIAGNOSTIC_LIMIT_NOT_PRESENT");}
struct Receiver:InputChannels::Receiver {
    int calls=0;
    void OnDataReceived(const std::uint8_t*,std::uint32_t,const Locator&,const Locator&) override {++calls;}
};
NativeTableConfiguration Config() {
    NativeTableConfiguration c;c.service_pkg_name="com.kaihong.mdds";
    c.directory.locals={{L(2),"diagnostic-ns","diagnostic-listen"},{L(3),"diagnostic-ns-3","diagnostic-listen-3"}};
    for(auto local:{L(2),L(3)}) {Route r;r.key={local,L(10)};
        r.fields={"peer-network","peer-listener",local==L(2)?"diagnostic-dial":"diagnostic-dial-3",{"peer-dial"},RecordDataType::Bytes};
        c.directory.routes.push_back(r);c.directions.push_back(RouteDirection::Dial);}
    return c;
}
struct Fixture {
    NativeTableConfiguration config=Config();EndpointDirectory::Snapshot directory;
    std::unique_ptr<NativeOwnerTable> native;NamespaceLease space;NativeOwnerLease owner;
    std::optional<BindCallTicket> binding;
    explicit Fixture(bool ready=true) {
        auto d=EndpointDirectory::Build(config.directory);CHECK(d.value);directory=*d.value;
        auto n=NativeOwnerTable::Build(config);CHECK(n);native=std::move(*n.value);
        auto ns=native->AcquireNamespace(L(2),config);CHECK(ns);space=*ns.value;
        auto dial=native->ReserveDial(space,{L(2),L(10)});CHECK(dial);owner=dial.value->owner;
        CHECK(dial.value->socket_call->CallEntered()==NativeStatus::None);
        CHECK(dial.value->socket_call->Complete(NativeSocketResult::Id(70)).status==NativeStatus::None);
        auto b=native->BeginBind(owner);CHECK(b);binding.emplace(std::move(*b.value));
        CHECK(binding->CallEntered()==NativeStatus::None);
        if(ready){CHECK(binding->Complete(SyncBindResult::Success()).status==NativeStatus::None);binding.reset();}
    }
    NativeIdentityConstraint Pin() {auto cb=space.AdmitCallback(70);CHECK(cb);auto p=cb.value->CaptureIdentity();CHECK(p);return *p.value;}
    ApplicationClaim Claim(ApplicationAttachments& a) {auto c=a.ClaimApplication({L(2),L(3)},directory);CHECK(c.result==ApplicationClaimResult::Claimed);return c.claim;}
};
ReceiveAdmissionResult Capture(ApplicationAttachments& a,const ApplicationClaim& c,detail::AttachmentReceiveCapture& capture,
        ReceiveAdmissionDiagnostic* d,std::size_t size=3,const Locator& local=L(2),const Locator& remote=L(10),
        ProcessClock::Clock::time_point expiry=never,const ProcessClock& clock=ProcessReceiveTestAccess::Clock())
{return ApplicationAttachmentsTestAccess::CaptureImpl(a,c,local,remote,{},expiry,size,clock,capture,d,0);}
ReceiveAdmissionResult Copy(ApplicationAttachments& a,detail::AttachmentReceiveCapture& c,ReceiveAdmissionDiagnostic* d)
{return ApplicationAttachmentsTestAccess::CopyImpl(a,c,bytes,d,0);}
ReceiveAdmissionResult Commit(ApplicationAttachments& a,detail::AttachmentReceiveCapture& c,ReceiveAdmissionDiagnostic* d,
        const ProcessClock& clock=ProcessReceiveTestAccess::Clock())
{return ApplicationAttachmentsTestAccess::CommitImpl(a,c,clock,d,0);}
ReceiveAdmissionResult Admit(ApplicationAttachments& a,Fixture& f,const ApplicationClaim& c,const NativeIdentityConstraint& pin,
        ReceiveAdmissionDiagnostic* d,ProcessClock::Clock::time_point expiry=never,const void* payload=bytes,std::size_t size=3)
{return ProcessReceiveTestAccess::AdmitImpl(a,*f.native,c,pin,payload,size,expiry,ProcessReceiveTestAccess::Clock(),d,0);}
template<class P> auto ReceiveImpl(P& p,const NativeIdentityConstraint& pin,ReceiveAdmissionDiagnostic* d,int)
    ->decltype(p.Receive(pin,bytes,3,never,0,d)){return p.Receive(pin,bytes,3,never,0,d);}
template<class P> auto ReceiveImpl(P& p,const NativeIdentityConstraint& pin,ReceiveAdmissionDiagnostic*,long)
    ->decltype(p.Receive(pin,bytes,3)){return p.Receive(pin,bytes,3);}
void CaptureFailures() {
    Fixture f;Receiver receiver;ApplicationAttachments a({L(2),L(3)});auto c=f.Claim(a);
    auto check=[&](const ApplicationClaim& claim,std::size_t size,const Locator& local,const Locator& remote,
            ReceiveAdmissionResult result,RxAdmissionReason reason,ProcessClock::Clock::time_point expiry=never){
        ReceiveAdmissionDiagnostic d;detail::AttachmentReceiveCapture capture;
        CHECK(Capture(a,claim,capture,&d,size,local,remote,expiry)==result);
        Diagnosis(d,RxAdmissionStage::Capture,reason,result);CHECK(a.SnapshotUsage().records==0);
        if(!baseline&&reason==RxAdmissionReason::TooLarge)RecordLimit(d,60000,0);
        if(!baseline&&(reason==RxAdmissionReason::ReceiverNotOpen||reason==RxAdmissionReason::ApplicationInactive||
                reason==RxAdmissionReason::Expired||reason==RxAdmissionReason::InvalidRoute)) {
            const auto id=a.ObserveApplication(claim);CHECK(d.application_known&&d.lifecycle_known);
            CHECK(d.application_incarnation==id.application_incarnation&&d.logical_token==id.logical_token);
        }
    };
    check({},3,L(2),L(10),ReceiveAdmissionResult::Revoked,RxAdmissionReason::InvalidClaim);
    check(c,0,L(2),L(10),ReceiveAdmissionResult::InvalidBuffer,RxAdmissionReason::ZeroSize);
    check(c,60001,L(2),L(10),ReceiveAdmissionResult::TooLarge,RxAdmissionReason::TooLarge);
    {
        std::vector<std::uint8_t> oversized(60001);ReceiveAdmissionDiagnostic d;
        const auto before=a.SnapshotUsage();
        CHECK(a.AdmitReceive(L(2),L(10),oversized.data(),oversized.size(),&d)==ReceiveAdmissionResult::TooLarge);
        Diagnosis(d,RxAdmissionStage::Capture,RxAdmissionReason::TooLarge,ReceiveAdmissionResult::TooLarge);
        if(!baseline)RecordLimit(d,60000,0);
        const auto after=a.SnapshotUsage();
        CHECK(after.records==before.records&&after.bytes==before.bytes&&after.copying==before.copying);
    }
    check(c,3,L(9),L(10),ReceiveAdmissionResult::InvalidLocal,RxAdmissionReason::InvalidLocal);
    check(c,3,L(2),L(10),ReceiveAdmissionResult::NoReceiver,RxAdmissionReason::ReceiverNotOpen);
    auto binding=a.OpenReceiver(c,L(2),&receiver);CHECK(binding.result==ReceiverOpenResult::Opened);
    check(c,3,L(2),L(11),ReceiveAdmissionResult::InvalidRoute,RxAdmissionReason::InvalidRoute);
    {
        // A real captured pin names L2 -> L10. Keep its local receiver valid so
        // this exercises the pin/route mismatch gate, not the directory path.
        auto pin=f.Pin();ReceiveAdmissionDiagnostic d;detail::AttachmentReceiveCapture capture;
        const auto before=a.SnapshotUsage();
        CHECK(ApplicationAttachmentsTestAccess::CaptureImpl(a,c,L(2),L(11),pin,never,3,
                ProcessReceiveTestAccess::Clock(),capture,&d,0)==ReceiveAdmissionResult::InvalidRoute);
        Diagnosis(d,RxAdmissionStage::Capture,RxAdmissionReason::InvalidRoute,ReceiveAdmissionResult::InvalidRoute);
        const auto after=a.SnapshotUsage();
        CHECK(after.records==before.records&&after.bytes==before.bytes&&after.copying==before.copying&&after.queued==before.queued);
        CHECK(f.native->SnapshotUsage().callbacks==0&&f.native->SnapshotUsage().receives==0);
    }
    check(c,3,L(2),L(10),ReceiveAdmissionResult::Expired,RxAdmissionReason::Expired,{});
    CHECK(a.CloseReceiver(binding.binding)==AttachmentControlResult::Completed);
    check(c,3,L(2),L(10),ReceiveAdmissionResult::NoReceiver,RxAdmissionReason::ReceiverNotOpen);
    CHECK(a.RequestDetach(c)==AttachmentControlResult::Requested);
    check(c,3,L(2),L(10),ReceiveAdmissionResult::NoReceiver,RxAdmissionReason::ApplicationInactive);
    a.AwaitDetached(c);
}
void Capacities() {
    // Each rejection checks the original first predicate and a six-bit mask.
    for(int scenario=0;scenario<7;++scenario) {
        AttachmentLimits n;n.records=8;n.bytes=24;n.local_records=4;n.local_bytes=12;n.copying=4;n.max_record_bytes=3;
        RxAdmissionReason reason;std::uint32_t mask;
        switch(scenario) {
            case 0:n.records=1;n.local_records=1;n.copying=1;reason=RxAdmissionReason::GlobalRecords;mask=1;break;
            case 1:n.bytes=3;n.local_bytes=3;reason=RxAdmissionReason::GlobalBytes;mask=2;break;
            case 2:n.local_records=1;reason=RxAdmissionReason::LocalRecords;mask=4;break;
            case 3:n.local_bytes=3;reason=RxAdmissionReason::LocalBytes;mask=8;break;
            case 4:n.copying=1;reason=RxAdmissionReason::CopyingLimit;mask=16;break;
            case 5:reason=RxAdmissionReason::ApplicationCopyingExhausted;mask=32;break;
            default:n.records=1;n.local_records=1;n.copying=1;n.bytes=3;n.local_bytes=3;reason=RxAdmissionReason::GlobalRecords;mask=31;break;
        }
        Fixture f;Receiver receiver,other_receiver;auto a=ApplicationAttachmentsTestAccess::Make({L(2),L(3)},n);auto c=f.Claim(*a);
        CHECK(a->OpenReceiver(c,L(2),&receiver).result==ReceiverOpenResult::Opened);
        CHECK(a->OpenReceiver(c,L(3),&other_receiver).result==ReceiverOpenResult::Opened);
        {
            detail::AttachmentReceiveCapture held;
            if(scenario!=5) {CHECK(Capture(*a,c,held,nullptr,3,scenario<2?L(3):L(2))==ReceiveAdmissionResult::Accepted);
                if(scenario<4){CHECK(Copy(*a,held,nullptr)==ReceiveAdmissionResult::Accepted);CHECK(Commit(*a,held,nullptr)==ReceiveAdmissionResult::Accepted);}}
            // Saturation cannot be reached with an actual number of producers;
            // the private test seam is restored before any capture destructor.
            const bool saturated=scenario==5&&ApplicationAttachmentsTestAccess::Saturate(*a,c,std::numeric_limits<std::size_t>::max(),0);
            if(scenario==5&&!saturated) {CHECK(baseline);continue;}
            ReceiveAdmissionDiagnostic d;detail::AttachmentReceiveCapture rejected,without;
            const auto before=a->SnapshotUsage();CHECK(Capture(*a,c,rejected,&d)==ReceiveAdmissionResult::Capacity);
            CHECK(Capture(*a,c,without,nullptr)==ReceiveAdmissionResult::Capacity);
            if(saturated)ApplicationAttachmentsTestAccess::Saturate(*a,c,0,0);
            Diagnosis(d,RxAdmissionStage::Capture,reason,ReceiveAdmissionResult::Capacity);
            if(!baseline){CHECK(d.capacity_known&&d.capacity_mask==mask);CHECK(d.records_used==before.records&&d.records_limit==n.records);
                CHECK(d.bytes_used==before.bytes&&d.bytes_limit==n.bytes);CHECK(d.local_records_limit==n.local_records);
                CHECK(d.local_bytes_limit==n.local_bytes&&d.copying_limit==n.copying);}
            CHECK(a->SnapshotUsage().records==before.records&&a->SnapshotUsage().bytes==before.bytes);
        }
        a->AwaitDetached(c);CHECK(a->SnapshotUsage().records==0&&receiver.calls==0);
    }
}
void CommitRacesAndCopyFault() {
    for(int action=0;action<5;++action) {
        Fixture f;Receiver receiver;ApplicationAttachments a({L(2),L(3)});auto c=f.Claim(a);
        auto b=a.OpenReceiver(c,L(2),&receiver);CHECK(b.result==ReceiverOpenResult::Opened);
        const auto original=a.ObserveReceiver(b.binding);
        {
            detail::AttachmentReceiveCapture capture;ReceiveAdmissionDiagnostic d;
            CHECK(Capture(a,c,capture,&d,3,L(2),L(10),10ms+ProcessClock::Clock::time_point{})==ReceiveAdmissionResult::Accepted);
            CHECK(Copy(a,capture,&d)==ReceiveAdmissionResult::Accepted);
            auto result=ReceiveAdmissionResult::Revoked;auto reason=RxAdmissionReason::CommitApplicationInactive;
            if(action==0)a.RequestDetach(c);
            if(action==1){a.CloseReceiver(b.binding);reason=RxAdmissionReason::CommitReceiverNotOpen;}
            if(action==2){a.CloseReceiver(b.binding);CHECK(a.OpenReceiver(c,L(2),&receiver).result==ReceiverOpenResult::Opened);reason=RxAdmissionReason::CommitReceiverChanged;}
            if(action==3){result=ReceiveAdmissionResult::Expired;reason=RxAdmissionReason::Expired;}
            if(action==4){result=ReceiveAdmissionResult::Accepted;reason=RxAdmissionReason::AcceptedQueue;}
            CHECK(Commit(a,capture,&d,ProcessReceiveTestAccess::Clock(ProcessClock::Clock::time_point{}+(action==3?10ms:0ms)))==result);
            Diagnosis(d,RxAdmissionStage::Commit,reason,result);
            if(!baseline){CHECK(d.application_known&&d.receiver_known&&d.lifecycle_known);
                CHECK(d.application_incarnation==original.application_incarnation&&d.logical_token==original.logical_token);
                CHECK(d.receiver_incarnation==original.receiver_incarnation);}
        }
        a.AwaitDetached(c);CHECK(a.SnapshotUsage().records==0&&receiver.calls==0);
    }
    Fixture f;Receiver receiver;auto a=ApplicationAttachmentsTestAccess::Make({L(2),L(3)},{},[]{throw std::bad_alloc();});auto c=f.Claim(*a);
    CHECK(a->OpenReceiver(c,L(2),&receiver).result==ReceiverOpenResult::Opened);ReceiveAdmissionDiagnostic d;
    CHECK(Admit(*a,f,c,f.Pin(),&d)==ReceiveAdmissionResult::AllocationFailure);
    Diagnosis(d,RxAdmissionStage::Copy,RxAdmissionReason::CopyAllocation,ReceiveAdmissionResult::AllocationFailure);
    CHECK(a->SnapshotUsage().records==0&&a->SnapshotUsage().copying==0);a->AwaitDetached(c);
}
void NativeAndCoordinatorGates() {
    for(int mode=0;mode<4;++mode) {
        Fixture f(mode<2);Receiver receiver;
        auto a=ApplicationAttachmentsTestAccess::Make({L(2),L(3)},{},mode==1?std::function<void()>([&]{f.owner.RequestClose();}):std::function<void()>{});
        auto c=f.Claim(*a);CHECK(a->OpenReceiver(c,L(2),&receiver).result==ReceiverOpenResult::Opened);auto pin=f.Pin();
        if(mode==0)f.owner.RequestClose();ReceiveAdmissionDiagnostic d;
        const auto result=mode<2?ReceiveAdmissionResult::Revoked:mode==2?ReceiveAdmissionResult::InvalidDeadline:ReceiveAdmissionResult::Expired;
        CHECK(Admit(*a,f,c,pin,&d,mode==3?ProcessClock::Clock::time_point{}:never)==result);
        Diagnosis(d,mode==1?RxAdmissionStage::CommitGate:mode==3?RxAdmissionStage::Capture:RxAdmissionStage::CaptureGate,
            mode==0?RxAdmissionReason::NativeCaptureRejected:mode==1?RxAdmissionReason::NativeCommitRejected:mode==2?RxAdmissionReason::InvalidDeadline:RxAdmissionReason::Expired,result);
        if(!baseline){CHECK(d.native_status_known);CHECK(d.native_status==(mode<2?NativeStatus::Revoked:NativeStatus::NotReady));}
        CHECK(a->SnapshotUsage().records==0&&f.native->SnapshotUsage().receives==0&&f.native->SnapshotUsage().callbacks==0);a->AwaitDetached(c);
    }
    auto process=ProcessCoordinator::Build(Config());CHECK(process);ReceiveAdmissionDiagnostic d;
    CHECK(ReceiveImpl(**process.value,{},&d,0)==ReceiveAdmissionResult::Revoked);
    Diagnosis(d,RxAdmissionStage::Coordinator,RxAdmissionReason::InvalidPin,ReceiveAdmissionResult::Revoked);
    Fixture f;d={};CHECK(ReceiveImpl(**process.value,f.Pin(),&d,0)==ReceiveAdmissionResult::NoReceiver);
    Diagnosis(d,RxAdmissionStage::Coordinator,RxAdmissionReason::NoApplication,ReceiveAdmissionResult::NoReceiver);
    if(!baseline)CHECK(d.candidate_count==0&&!d.application_known);
    (*process.value)->BeginShutdown();CHECK((*process.value)->PollShutdown().phase==ProcessShutdownPhase::ApplicationsDrained);
}
void FaultsAndDiagnosticNullParity() {
    Fixture f;Receiver receiver;ApplicationAttachments a({L(2),L(3)});auto c=f.Claim(a);
    auto opened=a.OpenReceiver(c,L(2),&receiver);CHECK(opened.result==ReceiverOpenResult::Opened);
    const auto clock=ProcessReceiveTestAccess::Clock();ReceiveAdmissionDiagnostic d;
    {
        detail::AttachmentReceiveCapture invalid;
        CHECK(Copy(a,invalid,&d)==ReceiveAdmissionResult::InvalidBuffer);
        Diagnosis(d,RxAdmissionStage::Copy,RxAdmissionReason::InvalidCopy,ReceiveAdmissionResult::InvalidBuffer);
        CHECK(Commit(a,invalid,&d)==ReceiveAdmissionResult::Revoked);
        Diagnosis(d,RxAdmissionStage::Commit,RxAdmissionReason::InvalidCommit,ReceiveAdmissionResult::Revoked);
    }
    {
        detail::AttachmentReceiveCapture capture;d={};
        fail_next_allocation=true;
        const auto result=Capture(a,c,capture,&d,3,L(2),L(10),never,clock);
        fail_next_allocation=false;CHECK(result==ReceiveAdmissionResult::AllocationFailure);
        Diagnosis(d,RxAdmissionStage::Capture,RxAdmissionReason::CaptureAllocation,result);
        CHECK(a.SnapshotUsage().records==0);
    }
    auto pin=f.Pin();
    for(int invalid=0;invalid<2;++invalid) {
        d={};const void* payload=invalid?bytes:nullptr;const auto size=invalid?0u:3u;
        CHECK(Admit(a,f,c,pin,&d,never,payload,size)==ReceiveAdmissionResult::InvalidBuffer);
        Diagnosis(d,RxAdmissionStage::CaptureGate,invalid?RxAdmissionReason::ZeroSize:RxAdmissionReason::NullBuffer,
                ReceiveAdmissionResult::InvalidBuffer);
        if(!baseline)CHECK(!d.native_status_known&&!d.application_known&&!d.capacity_known);
    }
    Fixture foreign;
    for(bool empty:{false,true}) {
        d={};
        CHECK(Admit(a,f,c,empty?NativeIdentityConstraint{}:foreign.Pin(),&d)==ReceiveAdmissionResult::Revoked);
        Diagnosis(d,RxAdmissionStage::CaptureGate,RxAdmissionReason::NativeCaptureRejected,ReceiveAdmissionResult::Revoked);
        if(!baseline)CHECK(d.native_status_known&&d.native_status==(empty?NativeStatus::InvalidHandle:NativeStatus::ForeignState));
        CHECK(a.SnapshotUsage().records==0&&f.native->SnapshotUsage().receives==0);
    }
    std::size_t allocated[2]{};
    for(int observed=0;observed<2;++observed) {
        d={};
        const auto before=allocation_count;
        const auto result=ProcessReceiveTestAccess::AdmitImpl(a,*f.native,c,pin,bytes,3,never,clock,observed?&d:nullptr,0);
        allocated[observed]=allocation_count-before;
        CHECK(result==ReceiveAdmissionResult::Accepted);
        if(observed)Diagnosis(d,RxAdmissionStage::Commit,RxAdmissionReason::AcceptedQueue,result);
        const auto usage=a.SnapshotUsage();CHECK(usage.records==1&&usage.bytes==3&&usage.copying==0&&usage.queued==1);
        CHECK(f.native->SnapshotUsage().callbacks==0&&f.native->SnapshotUsage().receives==0);
        auto work=a.TryTake();work={};CHECK(a.SnapshotUsage().records==0&&a.SnapshotUsage().bytes==0);
    }
    CHECK(allocated[0]==allocated[1]);a.AwaitDetached(c);CHECK(receiver.calls==0);
}
void CoordinatorSelectionAndOverflow() {
    Receiver receiver;
    for(bool overflow:{false,true}) {
        auto clock=ProcessReceiveTestAccess::Clock(overflow?never:ProcessClock::Clock::time_point{});
        auto built=ProcessCoordinator::Build(Config(),{},clock);CHECK(built);auto& p=**built.value;
        auto app=p.AttachApplication({L(2)},{},{8,1024,16,4096,1024});CHECK(app);
        auto ns=p.Native().AcquireNamespace(L(2),Config());CHECK(ns);
        auto owner=p.Native().ReserveDial(*ns.value,{L(2),L(10)});CHECK(owner);
        CHECK(owner.value->socket_call->CallEntered()==NativeStatus::None);
        CHECK(owner.value->socket_call->Complete(NativeSocketResult::Id(70)).status==NativeStatus::None);
        auto bind=p.Native().BeginBind(owner.value->owner);CHECK(bind);CHECK(bind.value->CallEntered()==NativeStatus::None);
        CHECK(bind.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);
        NativeIdentityConstraint pin;
        {auto cb=ns.value->AdmitCallback(70);CHECK(cb);auto id=cb.value->CaptureIdentity();CHECK(id);pin=*id.value;}
        auto b=p.OpenReceiver(*app.value,L(2),&receiver);CHECK(b.result==ReceiverOpenResult::Opened);
        ReceiveAdmissionDiagnostic d;
        const auto result=ReceiveImpl(p,pin,&d,0);
        CHECK(result==(overflow?ReceiveAdmissionResult::Expired:ReceiveAdmissionResult::Accepted));
        Diagnosis(d,overflow?RxAdmissionStage::Coordinator:RxAdmissionStage::Commit,
                overflow?RxAdmissionReason::ClockOverflow:RxAdmissionReason::AcceptedQueue,result);
        if(!baseline)CHECK(d.candidate_count==1);
        p.BeginShutdown();CHECK(p.ProgressDetach(*app.value).phase==ProcessApplicationPhase::Detached);
        CHECK(p.PollShutdown().phase==ProcessShutdownPhase::ApplicationsDrained&&p.ReceiveUsage().records==0);
    }
}
}
int main(int argc,char** argv) {try {
    baseline=argc>1&&std::string(argv[1])=="--baseline";
    CaptureFailures();std::cout<<"PASS core capture decisions\n";
    Capacities();std::cout<<"PASS core capacity masks and null parity\n";
    CommitRacesAndCopyFault();std::cout<<"PASS core commit races and copy failure\n";
    NativeAndCoordinatorGates();std::cout<<"PASS native guard and coordinator decisions\n";
    FaultsAndDiagnosticNullParity();std::cout<<"PASS allocation fault and diagnostic-null allocation-budget parity\n";
    CoordinatorSelectionAndOverflow();std::cout<<"PASS first application selection and clock overflow\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
