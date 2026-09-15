#include <mdds/dsoftbus/ApplicationAttachments.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include "application_attachments_stack.hpp"

thread_local long attachment_fail_after = -1;
thread_local std::size_t attachment_allocations = 0;
void* operator new(std::size_t n)
{
    ++attachment_allocations;
    if (attachment_fail_after == 0) { attachment_fail_after = -1; throw std::bad_alloc(); }
    if (attachment_fail_after > 0) --attachment_fail_after;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace mdds::dsoftbus;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x); } while(false)
namespace mdds::dsoftbus {
class ApplicationAttachmentsTestAccess
{
public:
    static std::unique_ptr<ApplicationAttachments> Make(const std::vector<Locator>& locals,
            AttachmentLimits limits = {}, std::function<void()> opened = {},
            std::function<void()> copy = {}, std::function<void()> publish = {},
            std::function<void()> close = {}, std::function<void()> detach = {})
    { return std::unique_ptr<ApplicationAttachments>(new ApplicationAttachments(locals, limits,
            {std::move(opened),std::move(copy),std::move(publish),std::move(close),std::move(detach)})); }
    static std::weak_ptr<void> App(const ApplicationClaim& claim) { return claim.app_; }
    static std::size_t Copying(ApplicationAttachments& a, const ApplicationClaim& c)
    { return a.TestCopying(c); }
    static auto PollProducers(ApplicationAttachments& a, const ApplicationClaim& c)
    { return a.PollReceiveProducers(c); }
    static auto Core(ApplicationAttachments& a, const ApplicationClaim& c) { return a.TestCore(c); }
    static auto Budget(ApplicationAttachments& a) { return a.TestBudget(); }
    static void Incarnation(ApplicationAttachments& a, const ApplicationClaim& c, const Locator& l, std::uint64_t n)
    { a.TestSetIncarnation(c,l,n); }
};
}
namespace {
struct Event
{
    std::mutex mutex; std::condition_variable cv; std::size_t count=0;
    void Signal() { std::lock_guard<std::mutex> l(mutex); ++count; cv.notify_all(); }
    void Wait(std::size_t n=1) {
        std::unique_lock<std::mutex> l(mutex);
        if (!cv.wait_for(l,3s,[&]{return count>=n;})) { std::cerr<<"FAIL barrier watchdog\n";std::abort(); }
    }
};
template<class T> T Finish(std::future<T>& f) {
    if(f.wait_for(3s)!=std::future_status::ready) { std::cerr<<"FAIL completion watchdog\n";std::abort(); }
    return f.get();
}
Locator L(std::uint8_t n) { EndpointId e{};e[15]=n;Locator l;CHECK(LocatorCodec::Encode(e,l));return l; }
std::vector<Locator> Locals() { return {L(2),L(3),L(5)}; }
EndpointDirectory::Snapshot Directory() {
    DirectoryConfiguration c;
    for(auto n:{2,3,5}) {
        auto s=std::to_string(n);c.locals.push_back({L(static_cast<std::uint8_t>(n)),"namespace"+s,"listen"+s});
        Route r;r.key={L(static_cast<std::uint8_t>(n)),L(4)};
        r.fields={"remote-network-long-enough-to-allocate","listen-peer","dial"+s,{"peer-dial"+s},RecordDataType::Bytes};
        c.routes.push_back(r);
    }
    auto d=EndpointDirectory::Build(c);CHECK(d.value);return *d.value;
}
struct Receiver : InputChannels::Receiver {
    std::atomic<unsigned> calls{0};
    std::function<void(const std::uint8_t*,std::uint32_t,const Locator&,const Locator&)> on;
    void OnDataReceived(const std::uint8_t* p,std::uint32_t n,const Locator& l,const Locator& r) override {
        ++calls;if(on)on(p,n,l,r);
    }
};
ApplicationClaim Claim(ApplicationAttachments& a, const std::vector<Locator>& locals = {L(2)}) {
    auto c=a.ClaimApplication(locals,Directory());CHECK(c.result==ApplicationClaimResult::Claimed);CHECK(c.claim);return c.claim;
}
ReceiverBinding Open(ApplicationAttachments& a,const ApplicationClaim& c,Receiver& r,Locator l=L(2)) {
    auto b=a.OpenReceiver(c,l,&r);CHECK(b.result==ReceiverOpenResult::Opened);CHECK(b.binding);return b.binding;
}
ReceiveAdmissionResult Admit(ApplicationAttachments& a,Locator l=L(2)) {
    const std::uint8_t bytes[]={0,0x52,0x54,255};return a.AdmitReceive(l,L(4),bytes,sizeof(bytes));
}
void Detach(ApplicationAttachments& a,const ApplicationClaim& c) {
    auto r=a.AwaitDetached(c);CHECK(r==AttachmentControlResult::Completed||r==AttachmentControlResult::AlreadyDetached);
}
void Empty(const ApplicationAttachments& a) {
    auto u=a.SnapshotUsage();CHECK(u.records==0&&u.bytes==0&&u.copying==0&&u.queued==0&&u.taken==0&&u.delivering==0);
}
void Transactional() {
    Receiver r,s;ApplicationAttachments a(Locals());
    auto c=Claim(a,{L(2),L(3)});auto b=Open(a,c,r);auto same=a.OpenReceiver(c,L(2),&r);
    CHECK(same.result==ReceiverOpenResult::AlreadyOpen&&same.binding==b);
    CHECK(a.OpenReceiver(c,L(2),&s).result==ReceiverOpenResult::Conflict);
    CHECK(a.OpenReceiver(c,L(3),&r).result==ReceiverOpenResult::Conflict);
    CHECK(a.ClaimApplication({L(3),L(5)},Directory()).result==ApplicationClaimResult::InUse);
    auto unrelated=Claim(a,{L(5)});CHECK(a.SnapshotUsage().active_applications==2);
    auto b3=Open(a,c,s,L(3));
    r.on=[](auto*,auto,const Locator& l,const Locator&){CHECK(l==L(2));};
    s.on=[](auto*,auto,const Locator& l,const Locator&){CHECK(l==L(3));};
    CHECK(Admit(a)==ReceiveAdmissionResult::Accepted);CHECK(Admit(a,L(3))==ReceiveAdmissionResult::Accepted);
    CHECK(a.TryTake().Deliver()==InputDeliveryResult::Delivered);CHECK(a.TryTake().Deliver()==InputDeliveryResult::Delivered);
    CHECK(r.calls==1&&s.calls==1);Detach(a,c);Detach(a,unrelated);Empty(a);
    Event start;ApplicationAttachments competing(Locals());const auto dir=Directory();const auto set=std::vector<Locator>{L(2),L(3)};
    auto one=std::async(std::launch::async,[&]{start.Wait();return competing.ClaimApplication(set,dir);});
    auto two=std::async(std::launch::async,[&]{start.Wait();return competing.ClaimApplication(set,dir);});
    start.Signal();auto first=Finish(one),second=Finish(two);
    CHECK((first.result==ApplicationClaimResult::Claimed&&second.result==ApplicationClaimResult::InUse)||
          (second.result==ApplicationClaimResult::Claimed&&first.result==ApplicationClaimResult::InUse));
    CHECK(competing.SnapshotUsage().active_applications==1);Detach(competing,first.claim?first.claim:second.claim);
}
void ActualDrain() {
    Event entered,release,waiters;std::atomic<unsigned> done{0};Receiver r;
    auto a=ApplicationAttachmentsTestAccess::Make(Locals(),{}, {},{},{},{},[&]{waiters.Signal();});
    auto c=Claim(*a);auto b=Open(*a,c,r);
    r.on=[&](auto*,auto,const auto&,const auto&){entered.Signal();release.Wait();};
    CHECK(Admit(*a)==ReceiveAdmissionResult::Accepted);auto w=a->TryTake();
    auto delivery=std::async(std::launch::async,[&]{return w.Deliver();});entered.Wait();
    auto d1=std::async(std::launch::async,[&]{auto v=a->AwaitDetached(c);++done;return v;});waiters.Wait();
    auto d2=std::async(std::launch::async,[&]{auto v=a->AwaitDetached(c);++done;return v;});waiters.Wait(2);
    CHECK(done==0);CHECK(a->SnapshotUsage().delivering==1&&a->SnapshotUsage().detaching_applications==1);
    CHECK(a->ClaimApplication({L(2)},Directory()).result==ApplicationClaimResult::Detaching);
    CHECK(Admit(*a)==ReceiveAdmissionResult::NoReceiver);
    release.Signal();CHECK(Finish(delivery)==InputDeliveryResult::Delivered);Finish(d1);Finish(d2);
    CHECK(done==2);auto fresh=Claim(*a);Detach(*a,fresh);Empty(*a);
}
void OldWork() {
    Event copied,resume;std::atomic<bool> pause{false};Receiver old,fresh;
    auto a=ApplicationAttachmentsTestAccess::Make(Locals(),{}, {},[&]{if(pause){copied.Signal();resume.Wait();}});
    auto c=Claim(*a);auto b=Open(*a,c,old);
    CHECK(Admit(*a)==ReceiveAdmissionResult::Accepted);auto taken=a->TryTake();
    CHECK(Admit(*a)==ReceiveAdmissionResult::Accepted);pause=true;
    auto copying=std::async(std::launch::async,[&]{return Admit(*a);});copied.Wait();
    auto u=a->SnapshotUsage();CHECK(u.copying==1&&u.queued==1&&u.taken==1&&u.records==3);
    Detach(*a,c);u=a->SnapshotUsage();CHECK(u.queued==0&&u.records==2);
    auto c1=Claim(*a);auto b1=Open(*a,c1,fresh);resume.Signal();
    CHECK(Finish(copying)==ReceiveAdmissionResult::Revoked);CHECK(taken.Deliver()==InputDeliveryResult::Disabled);
    CHECK(old.calls==0&&fresh.calls==0);pause=false;CHECK(Admit(*a)==ReceiveAdmissionResult::Accepted);
    CHECK(a->TryTake().Deliver()==InputDeliveryResult::Delivered&&fresh.calls==1);
    Detach(*a,c1);CHECK(Admit(*a)==ReceiveAdmissionResult::NoReceiver);Empty(*a);
}
void OldControl() {
    Event entered,release,closers;Receiver old,fresh;std::atomic<unsigned> done{0};
    auto a=ApplicationAttachmentsTestAccess::Make(Locals(),{},{},{},{},[&]{closers.Signal();});
    auto c=Claim(*a);auto b=Open(*a,c,old);
    old.on=[&](auto*,auto,const auto&,const auto&){entered.Signal();release.Wait();};
    CHECK(Admit(*a)==ReceiveAdmissionResult::Accepted);auto w=a->TryTake();
    auto delivery=std::async(std::launch::async,[&]{return w.Deliver();});entered.Wait();
    auto d1=std::async(std::launch::async,[&]{auto v=a->CloseReceiver(b);++done;return v;});closers.Wait();
    auto d2=std::async(std::launch::async,[&]{auto v=a->CloseReceiver(b);++done;return v;});closers.Wait(2);
    CHECK(done==0&&a->OpenReceiver(c,L(2),&fresh).result==ReceiverOpenResult::Busy);
    release.Signal();Finish(delivery);Finish(d1);Finish(d2);CHECK(done==2);
    auto b1=Open(*a,c,fresh);CHECK(!(b1==b));CHECK(a->CloseReceiver(b)==AttachmentControlResult::Completed);
    CHECK(Admit(*a)==ReceiveAdmissionResult::Accepted);CHECK(a->TryTake().Deliver()==InputDeliveryResult::Delivered);
    CHECK(Admit(*a)==ReceiveAdmissionResult::Accepted);auto old_taken=a->TryTake();a->CloseReceiver(b1);
    auto b2=Open(*a,c,fresh);CHECK(old_taken.Deliver()==InputDeliveryResult::Closed&&fresh.calls==1);
    CHECK(a->CloseReceiver(b1)==AttachmentControlResult::Completed);CHECK(!(b1==b2));
    Detach(*a,c);auto next=Claim(*a);auto bn=Open(*a,next,fresh);
    Detach(*a,c);CHECK(a->CloseReceiver(b1)==AttachmentControlResult::Completed);
    CHECK(Admit(*a)==ReceiveAdmissionResult::Accepted);CHECK(a->TryTake().Deliver()==InputDeliveryResult::Delivered);
    a->CloseReceiver(bn);ApplicationAttachmentsTestAccess::Incarnation(*a,next,L(2),UINT64_MAX);
    CHECK(a->OpenReceiver(next,L(2),&fresh).result==ReceiverOpenResult::IncarnationExhausted);Detach(*a,next);
    // A completed old Close must not wait for a new, unpublished Open of the
    // same app/local. Its old incarnation was drained before Opening began.
    Event publishing,resume_open;std::atomic<bool> pause_open{false},old_waited{false};
    auto late=ApplicationAttachmentsTestAccess::Make(Locals(),{},[&]{if(pause_open){publishing.Signal();resume_open.Wait();}},
        {},{},[&]{if(pause_open){old_waited=true;resume_open.Signal();}});
    auto lc=Claim(*late);auto lb=Open(*late,lc,old);late->CloseReceiver(lb);pause_open=true;
    auto opening=std::async(std::launch::async,[&]{return late->OpenReceiver(lc,L(2),&fresh);});publishing.Wait();
    CHECK(late->CloseReceiver(lb)==AttachmentControlResult::Completed);resume_open.Signal();
    CHECK(Finish(opening).result==ReceiverOpenResult::Opened);CHECK(!old_waited);Detach(*late,lc);
    // R1: unlike the late-entry case above, this duplicate has already seen
    // Closing before the owner drains. It must recognize that old completion
    // even if the slot has since entered a new, unpublished Opening.
    Event owner_captured,owner_resume,duplicate_captured,duplicate_resume;
    Event new_registered,new_resume,duplicate_resumed;
    std::atomic<unsigned> close_calls{0};std::atomic<bool> pause_new{false};
    Receiver original,replacement;
    auto prior=ApplicationAttachmentsTestAccess::Make(Locals(),{},[&]{
        if(pause_new){new_registered.Signal();new_resume.Wait();}
    },{},{},[&]{
        const auto number=++close_calls;
        if(number==1){owner_captured.Signal();owner_resume.Wait();}
        if(number==2){duplicate_captured.Signal();duplicate_resume.Wait();duplicate_resumed.Signal();}
    });
    auto pc=Claim(*prior);auto pb=Open(*prior,pc,original);
    auto owner=std::async(std::launch::async,[&]{return prior->CloseReceiver(pb);});owner_captured.Wait();
    auto duplicate=std::async(std::launch::async,[&]{return prior->CloseReceiver(pb);});duplicate_captured.Wait();
    owner_resume.Signal();CHECK(Finish(owner)==AttachmentControlResult::Completed);
    std::cout<<"R1 OWNER_OLD_DRAIN_COMPLETED"<<std::endl;
    pause_new=true;
    auto pending_open=std::async(std::launch::async,[&]{return prior->OpenReceiver(pc,L(2),&replacement);});new_registered.Wait();
    std::cout<<"R1 NEW_OPEN_REGISTERED_UNPUBLISHED"<<std::endl;
    duplicate_resume.Signal();duplicate_resumed.Wait();
    // Barriers establish every causal precondition. This bounded observation
    // measures missing old completion, not a quiet period or SDK quiescence.
    const bool old_completed=duplicate.wait_for(500ms)==std::future_status::ready;
    std::cout<<"R1 OLD_DUPLICATE_COMPLETED_WHILE_NEW_OPEN_PAUSED="<<old_completed<<std::endl;
    // Always release/join normally before asserting, including in the RED run.
    new_resume.Signal();CHECK(Finish(pending_open).result==ReceiverOpenResult::Opened);
    CHECK(Finish(duplicate)==AttachmentControlResult::Completed);
    CHECK(Admit(*prior)==ReceiveAdmissionResult::Accepted);
    CHECK(prior->TryTake().Deliver()==InputDeliveryResult::Delivered);
    CHECK(original.calls==0&&replacement.calls==1);Detach(*prior,pc);Empty(*prior);
    CHECK(old_completed);
}
void OpenRaceAndFaults() {
    Event registered,resume,waiting;std::atomic<bool> open_done{false},detach_done{false};Receiver r;
    auto a=ApplicationAttachmentsTestAccess::Make(Locals(),{},[&]{registered.Signal();resume.Wait();},{},{},{},[&]{waiting.Signal();});
    auto c=Claim(*a);
    auto opening=std::async(std::launch::async,[&]{auto v=a->OpenReceiver(c,L(2),&r);open_done=true;return v;});registered.Wait();
    CHECK(Admit(*a)==ReceiveAdmissionResult::NoReceiver);
    auto closing=std::async(std::launch::async,[&]{auto v=a->AwaitDetached(c);detach_done=true;return v;});waiting.Wait();
    CHECK(!open_done&&!detach_done);resume.Signal();CHECK(Finish(opening).result==ReceiverOpenResult::Detached);Finish(closing);
    CHECK(r.calls==0);Empty(*a);
    // Real allocation sweeps: construct all fixture arguments before arming.
    const auto locals=Locals(),one=std::vector<Locator>{L(2)};const auto dir=Directory();
    std::size_t claim_failures=0,open_failures=0,copy_failures=0;
    for(long n=0;n<256;++n) {
        ApplicationAttachments x(locals);std::cout<<"claim allocation fault "<<n<<std::endl;attachment_fail_after=n;
        auto out=x.ClaimApplication(one,dir);attachment_fail_after=-1;
        if(out.result==ApplicationClaimResult::Claimed){Detach(x,out.claim);break;}
        CHECK(out.result==ApplicationClaimResult::AllocationFailure);CHECK(x.SnapshotUsage().retained_applications==0);++claim_failures;
    }
    for(long n=0;n<256;++n) {
        ApplicationAttachments x(locals);auto cc=Claim(x);std::cout<<"open allocation fault "<<n<<std::endl;attachment_fail_after=n;
        auto out=x.OpenReceiver(cc,L(2),&r);attachment_fail_after=-1;
        if(out.result==ReceiverOpenResult::Opened){Detach(x,cc);break;}
        CHECK(out.result==ReceiverOpenResult::AllocationFailure);CHECK(Admit(x)==ReceiveAdmissionResult::NoReceiver);++open_failures;Detach(x,cc);
    }
    for(long n=0;n<256;++n) {
        ApplicationAttachments x(locals);auto cc=Claim(x);auto bb=Open(x,cc,r);std::cout<<"receive allocation fault "<<n<<std::endl;attachment_fail_after=n;
        auto out=Admit(x);attachment_fail_after=-1;
        if(out==ReceiveAdmissionResult::Accepted){Detach(x,cc);break;}
        CHECK(out==ReceiveAdmissionResult::AllocationFailure);Empty(x);++copy_failures;Detach(x,cc);
    }
    CHECK(claim_failures>0&&open_failures>0&&copy_failures>0);
    std::cout<<"allocations exercised claim="<<claim_failures<<" open="<<open_failures<<" receive="<<copy_failures<<'\n';
    for(int fault=0;fault<2;++fault) {
        auto x=ApplicationAttachmentsTestAccess::Make(locals,{},fault==0?std::function<void()>([]{throw std::bad_alloc();}):std::function<void()>{}, {},fault==1?std::function<void()>([]{throw std::bad_alloc();}):std::function<void()>{});
        auto cc=Claim(*x);auto out=x->OpenReceiver(cc,L(2),&r);
        if(fault==0){CHECK(out.result==ReceiverOpenResult::AllocationFailure);CHECK(Admit(*x)==ReceiveAdmissionResult::NoReceiver);}
        else{CHECK(out.result==ReceiverOpenResult::Opened);CHECK(Admit(*x)==ReceiveAdmissionResult::AllocationFailure);}
        Empty(*x);Detach(*x,cc);
    }
}
void InvalidAndCapacity() {
    Receiver r;ApplicationAttachments a(Locals()),foreign(Locals());auto c=Claim(a);auto b=Open(a,c,r);
    CHECK(a.ClaimApplication({},Directory()).result==ApplicationClaimResult::InvalidLocals);
    CHECK(a.ClaimApplication({L(3),L(3)},Directory()).result==ApplicationClaimResult::InvalidLocals);
    CHECK(a.ClaimApplication({L(3)},{}).result==ApplicationClaimResult::InvalidDirectory);
    CHECK(a.OpenReceiver({},L(2),&r).result==ReceiverOpenResult::InvalidHandle);
    CHECK(foreign.OpenReceiver(c,L(2),&r).result==ReceiverOpenResult::InvalidHandle);
    CHECK(foreign.CloseReceiver(b)==AttachmentControlResult::InvalidHandle);
    CHECK(foreign.AwaitDetached(c)==AttachmentControlResult::InvalidHandle);
    CHECK(a.CloseReceiver({})==AttachmentControlResult::InvalidHandle);
    CHECK(a.OpenReceiver(c,L(3),&r).result==ReceiverOpenResult::InvalidLocal);
    CHECK(a.OpenReceiver(c,L(2),nullptr).result==ReceiverOpenResult::NullReceiver);
    std::uint8_t value=1;
    CHECK(a.AdmitReceive(L(2),L(4),nullptr,1)==ReceiveAdmissionResult::InvalidBuffer);
    CHECK(a.AdmitReceive(L(2),L(4),&value,0)==ReceiveAdmissionResult::InvalidBuffer);
    CHECK(a.AdmitReceive(L(2),L(4),&value,60001)==ReceiveAdmissionResult::TooLarge);
    CHECK(a.AdmitReceive(L(2),L(8),&value,1)==ReceiveAdmissionResult::InvalidRoute);
    CHECK(a.AdmitReceive(L(8),L(4),&value,1)==ReceiveAdmissionResult::InvalidLocal);
    for(int i=0;i<32;++i)CHECK(Admit(a)==ReceiveAdmissionResult::Accepted);
    CHECK(Admit(a)==ReceiveAdmissionResult::Capacity);auto w=a.TryTake();CHECK(Admit(a)==ReceiveAdmissionResult::Capacity);
    w={};CHECK(Admit(a)==ReceiveAdmissionResult::Accepted);Detach(a,c);Empty(a);
    for(int i=0;i<8;++i){auto n=AttachmentLimits{};if(i==0)n.logical_slots=0;if(i==1)n.retained_applications=0;if(i==2)n.records=0;if(i==3)n.bytes=0;if(i==4)n.local_records=65;if(i==5)n.local_bytes=n.bytes+1;if(i==6)n.copying=65;if(i==7)n.max_record_bytes=n.local_bytes+1;
        bool rejected=false;try{ApplicationAttachments bad(Locals(),n);}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);}
    for(auto slots:{std::vector<Locator>{},std::vector<Locator>{L(2),L(2)},std::vector<Locator>{Locator{}}}){
        bool rejected=false;try{ApplicationAttachments bad(slots);}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);}
    for(int field=0;field<4;++field){auto n=AttachmentLimits{};auto huge=std::numeric_limits<std::size_t>::max();
        if(field==0)n.logical_slots=huge;if(field==1)n.retained_applications=huge;if(field==2)n.records=huge;if(field==3)n.bytes=huge;
        bool rejected=false;try{ApplicationAttachments bad(Locals(),n);}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);}
    // Global byte and record ceilings are independent of per-local ceilings.
    Receiver rr,ss,tt;ApplicationAttachments global(Locals());auto all=Claim(global,Locals());
    auto br=Open(global,all,rr),bs=Open(global,all,ss,L(3)),bt=Open(global,all,tt,L(5));
    for(int i=0;i<32;++i){CHECK(Admit(global)==ReceiveAdmissionResult::Accepted);CHECK(Admit(global,L(3))==ReceiveAdmissionResult::Accepted);}
    CHECK(Admit(global,L(5))==ReceiveAdmissionResult::Capacity);Detach(global,all);Empty(global);
    all=Claim(global,Locals());br=Open(global,all,rr);bs=Open(global,all,ss,L(3));bt=Open(global,all,tt,L(5));
    std::vector<std::uint8_t> full(60000);for(std::size_t i=0;i<full.size();++i)full[i]=static_cast<std::uint8_t>(i%251);
    rr.on=[&](auto* data,auto n,const auto&,const auto&){CHECK(n==60000);for(std::size_t i=0;i<n;++i)CHECK(data[i]==static_cast<std::uint8_t>(i%251));};
    for(int i=0;i<4;++i){CHECK(global.AdmitReceive(L(2),L(4),full.data(),full.size())==ReceiveAdmissionResult::Accepted);
        CHECK(global.AdmitReceive(L(3),L(4),full.data(),full.size())==ReceiveAdmissionResult::Accepted);}
    CHECK(global.AdmitReceive(L(2),L(4),full.data(),full.size())==ReceiveAdmissionResult::Capacity);
    CHECK(global.AdmitReceive(L(5),L(4),full.data(),44000)==ReceiveAdmissionResult::Accepted);
    CHECK(global.SnapshotUsage().bytes==524000);
    CHECK(global.AdmitReceive(L(5),L(4),full.data(),289)==ReceiveAdmissionResult::Capacity);
    std::fill(full.begin(),full.end(),std::uint8_t{0});CHECK(global.TryTake().Deliver()==InputDeliveryResult::Delivered); // Owned whole copy.
    Detach(global,all);Empty(global);
    // All eight copying reservations remain charged across detach and reclaim.
    Event copying,release;auto copies=ApplicationAttachmentsTestAccess::Make(Locals(),{},{},[&]{copying.Signal();release.Wait();});
    auto cc=Claim(*copies);auto bc=Open(*copies,cc,rr);std::vector<std::future<ReceiveAdmissionResult>> tasks;
    for(int i=0;i<8;++i)tasks.push_back(std::async(std::launch::async,[&]{return Admit(*copies);}));
    copying.Wait(8);CHECK(copies->SnapshotUsage().copying==8&&Admit(*copies)==ReceiveAdmissionResult::Capacity);
    Detach(*copies,cc);auto next=Claim(*copies);auto bn=Open(*copies,next,ss);
    CHECK(Admit(*copies)==ReceiveAdmissionResult::Capacity);release.Signal();
    for(auto& task:tasks)CHECK(Finish(task)==ReceiveAdmissionResult::Revoked);
    Empty(*copies);Detach(*copies,next);
}
void ReuseAndRetention() {
    Receiver r;ApplicationAttachments a(Locals());
    for(int i=0;i<64;++i) {
        auto c=Claim(a);auto b=Open(a,c,r);auto weak=ApplicationAttachmentsTestAccess::App(c);
        CHECK(Admit(a)==ReceiveAdmissionResult::Accepted);CHECK(a.TryTake().Deliver()==InputDeliveryResult::Delivered);
        Detach(a,c);b={};c={};CHECK(weak.expired());CHECK(a.SnapshotUsage().retained_applications==0);Empty(a);
    }
    CHECK(r.calls==64&&a.SnapshotUsage().high_applications==1&&a.SnapshotUsage().logical_slots==3);
    std::vector<ApplicationClaim> claims;std::vector<ReceiverBinding> bindings;std::vector<ReceiveWork> works;
    for(int i=0;i<16;++i) {
        auto c=Claim(a);auto b=Open(a,c,r);
        if(i%3==0){CHECK(Admit(a)==ReceiveAdmissionResult::Accepted);works.push_back(a.TryTake());}
        Detach(a,c);if(i%3==1)claims.push_back(c);if(i%3==2)bindings.push_back(b);
    }
    CHECK(a.SnapshotUsage().retained_applications==16);
    CHECK(a.ClaimApplication({L(2)},Directory()).result==ApplicationClaimResult::Capacity);
    claims.clear();bindings.clear();for(auto& w:works)CHECK(w.Deliver()==InputDeliveryResult::Disabled);works.clear();
    CHECK(a.SnapshotUsage().retained_applications==0);Empty(a);auto c=Claim(a);Detach(a,c);
}
void OwnershipAndException() {
    Receiver r;auto a=std::make_unique<ApplicationAttachments>(Locals());auto c=Claim(*a);auto b=Open(*a,c,r);
    r.on=[](auto*,auto,const auto&,const auto&){throw 42;};
    CHECK(Admit(*a)==ReceiveAdmissionResult::Accepted);auto throwing=a->TryTake();
    CHECK(throwing.Deliver()==InputDeliveryResult::CallbackFailure);CHECK(!throwing);CHECK(throwing.Deliver()==InputDeliveryResult::InvalidTicket);CHECK(r.calls==1);Empty(*a);
    CHECK(Admit(*a)==ReceiveAdmissionResult::Accepted);auto work=a->TryTake();
    auto app=ApplicationAttachmentsTestAccess::App(c);auto core=ApplicationAttachmentsTestAccess::Core(*a,c);auto budget=ApplicationAttachmentsTestAccess::Budget(*a);
    c={};b={};a.reset();CHECK(!app.expired()&&!core.expired()&&!budget.expired());
    CHECK(work.Deliver()==InputDeliveryResult::Disabled);CHECK(r.calls==1);
    CHECK(app.expired()&&core.expired()&&budget.expired());
    // Actual receiver destruction is not safe until control destruction has
    // crossed the same callback barrier, even if public handles are returned.
    Event entered,release,draining;std::atomic<bool> destroyed{false};Receiver blocked;
    auto owner=ApplicationAttachmentsTestAccess::Make(Locals(),{},{},{},{},{},[&]{draining.Signal();});
    auto claim=Claim(*owner);auto binding=Open(*owner,claim,blocked);
    blocked.on=[&](auto*,auto,const auto&,const auto&){entered.Signal();release.Wait();};
    CHECK(Admit(*owner)==ReceiveAdmissionResult::Accepted);auto delivering=owner->TryTake();
    auto callback=std::async(std::launch::async,[&]{return delivering.Deliver();});entered.Wait();
    claim={};binding={};auto destroy=std::async(std::launch::async,[&]{owner.reset();destroyed=true;});draining.Wait();
    CHECK(!destroyed);release.Signal();CHECK(Finish(callback)==InputDeliveryResult::Delivered);Finish(destroy);CHECK(destroyed);
}
}
void ApplicationCopyActivity() {
    Event reserved, copy_resume, published, publish_resume;
    Receiver receiver, other_receiver;
    auto a=ApplicationAttachmentsTestAccess::Make(Locals(),{}, {},
        [&]{reserved.Signal();copy_resume.Wait();},
        [&]{published.Signal();publish_resume.Wait();});
    auto c=Claim(*a);auto binding=Open(*a,c,receiver);
    ApplicationAttachments foreign(Locals());
    CHECK(ApplicationAttachmentsTestAccess::PollProducers(foreign,c)==ReceiveProducerState::InvalidHandle);
    auto other=Claim(*a,{L(3)});auto other_binding=Open(*a,other,other_receiver,L(3));
    auto work=std::async(std::launch::async,[&]{return Admit(*a);});reserved.Wait();
    CHECK(ApplicationAttachmentsTestAccess::Copying(*a,c)==1);
    CHECK(ApplicationAttachmentsTestAccess::Copying(*a,other)==0);
    CHECK(ApplicationAttachmentsTestAccess::PollProducers(*a,c)==ReceiveProducerState::Open);
    CHECK(ApplicationAttachmentsTestAccess::PollProducers(*a,{})==ReceiveProducerState::InvalidHandle);
    CHECK(a->SnapshotUsage().queued==0);
    copy_resume.Signal();published.Wait();
    CHECK(ApplicationAttachmentsTestAccess::Copying(*a,c)==1); // memcpy done, ownership not settled.
    a->RequestDetach(c);
    CHECK(ApplicationAttachmentsTestAccess::PollProducers(*a,c)==ReceiveProducerState::Pending);
    CHECK(ApplicationAttachmentsTestAccess::PollProducers(*a,other)==ReceiveProducerState::Open);
    // The legacy drain deliberately does not wait for copying. N3 must not
    // interpret its Detached phase as permission to skip this producer ledger.
    Detach(*a,c);
    CHECK(ApplicationAttachmentsTestAccess::PollProducers(*a,c)==ReceiveProducerState::Pending);
    publish_resume.Signal();
    CHECK(Finish(work)==ReceiveAdmissionResult::Revoked);
    CHECK(ApplicationAttachmentsTestAccess::PollProducers(*a,c)==ReceiveProducerState::Drained);
    CHECK(ApplicationAttachmentsTestAccess::Copying(*a,c)==0);Empty(*a);
    Detach(*a,c);Detach(*a,other);
    // Allocation/commit failure must refund before the per-app producer leaves.
    auto failed=ApplicationAttachmentsTestAccess::Make(Locals(),{}, {},{},[]{throw std::bad_alloc();});
    auto fc=Claim(*failed);auto fb=Open(*failed,fc,receiver);
    CHECK(Admit(*failed)==ReceiveAdmissionResult::AllocationFailure);
    CHECK(ApplicationAttachmentsTestAccess::Copying(*failed,fc)==0);Empty(*failed);Detach(*failed,fc);
}
int main(int argc,char** argv) {
    // Test-only diagnostic termination: a noexcept allocation failure must be
    // an explicit failed subprocess, never an interactive Windows crash dialog.
    std::set_terminate([]{std::cerr<<"FAIL unexpected std::terminate"<<std::endl;PrintAttachmentFailureStack();std::_Exit(86);});
    static_assert(!std::is_copy_constructible_v<ReceiveWork>);
    static_assert(std::is_nothrow_move_constructible_v<ReceiveWork>);
    static_assert(!std::is_move_constructible_v<ApplicationAttachments>);
    const std::pair<const char*,void(*)()> cases[]={
        {"AA-01-transactional-claims",Transactional},{"AA-02-real-callback-two-detach-waiters",ActualDrain},
        {"AA-03-old-copy-queue-taken",OldWork},{"AA-04-old-close-and-reopen",OldControl},
        {"AA-05-open-race-allocation-rollback",OpenRaceAndFaults},{"AA-06-invalid-and-capacity",InvalidAndCapacity},
        {"AA-07-64-rebuilds-retained-quota",ReuseAndRetention},{"AA-08-ownership-throwing-callback",OwnershipAndException},
        {"AA-N3-copy-activity",ApplicationCopyActivity}};
    unsigned run=0,failed=0;
    for(const auto& t:cases){if(argc==2&&std::string(t.first).find(argv[1])==std::string::npos)continue;++run;
        try{t.second();std::cout<<"PASS "<<t.first<<std::endl;}catch(const std::exception& e){attachment_fail_after=-1;++failed;std::cerr<<"FAIL "<<t.first<<": "<<e.what()<<std::endl;}}
    std::cout<<"RESULT "<<run-failed<<'/'<<run<<" application attachment groups passed\n";
    return !run?2:failed?1:0;
}
