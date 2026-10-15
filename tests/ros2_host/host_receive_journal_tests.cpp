#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#undef SendMessage
#include <fastdds_dsoftbus/dsoftbus/ApplicationAttachments.hpp>
#include <fastdds_dsoftbus/dsoftbus/HostReceiveJournal.hpp>
#include "../../src/ApplicationAttachmentsInternal.hpp"
#include "../../integrations/ros2/HostReceiveObservation.hpp"
#include "../../integrations/ros2/HostCallbackObservation.hpp"
#include <atomic>
#include <future>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fastdds_dsoftbus::dsoftbus;
namespace rx=fastdds_dsoftbus::dsoftbus::host_rx_observation;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x);}while(false)
namespace fastdds_dsoftbus::dsoftbus {
class ApplicationAttachmentsTestAccess {
public:
    static std::unique_ptr<ApplicationAttachments> Make(const std::vector<Locator>& locals,AttachmentLimits limits,
            std::function<void()> copy={},std::function<void()> publish={}) {
        return std::unique_ptr<ApplicationAttachments>(new ApplicationAttachments(locals,limits,
                {{},std::move(copy),std::move(publish),{},{}}));
    }
    static auto Capture(ApplicationAttachments& a,const ApplicationClaim& claim,const Locator& l,const Locator& r,
            detail::AttachmentReceiveCapture& capture,ReceiveAdmissionDiagnostic& d) {
        return a.CaptureReceive(claim,l,r,{},ProcessClock::Clock::time_point::max(),3,{},capture,&d);
    }
    static auto Copy(ApplicationAttachments& a,detail::AttachmentReceiveCapture& capture,const void* bytes,
            ReceiveAdmissionDiagnostic& d){return a.CopyReceive(capture,bytes,&d);}
    static auto Commit(ApplicationAttachments& a,detail::AttachmentReceiveCapture& capture,ReceiveAdmissionDiagnostic& d)
        {return a.CommitReceive(capture,{},&d);}
    static bool Requeue(ApplicationAttachments& a,ReceiveWork& work){return a.RequeueReceive(work);}
};
}
namespace {
const std::uint8_t payload[]={1,2,3};
Locator L(std::uint8_t n){EndpointId e{};e[15]=n;Locator l;CHECK(LocatorCodec::Encode(e,l));return l;}
EndpointDirectory::Snapshot Directory(){
    DirectoryConfiguration c;c.locals={{L(2),"journal-ns","journal-listen"}};
    Route r;r.key={L(2),L(4)};r.fields={"journal-network","peer-listen","journal-dial",{"peer-dial"},RecordDataType::Bytes};
    c.routes.push_back(r);auto d=EndpointDirectory::Build(c);CHECK(d.value);return *d.value;
}
struct Receiver:InputChannels::Receiver {
    std::size_t count=0;
    void OnDataReceived(const std::uint8_t*,std::uint32_t,const Locator&,const Locator&) override {++count;}
};
struct Fixture {
    Receiver receiver;std::unique_ptr<ApplicationAttachments> a;ApplicationClaim claim;ReceiverBinding binding;
    explicit Fixture(std::function<void()> copy={},std::function<void()> publish={},bool large=false){
        AttachmentLimits limits;limits.records=large?128:4;limits.bytes=large?1024:32;
        limits.local_records=large?128:4;limits.local_bytes=large?1024:8;limits.copying=2;limits.max_record_bytes=8;
        a=ApplicationAttachmentsTestAccess::Make({L(2)},limits,std::move(copy),std::move(publish));
        auto c=a->ClaimApplication({L(2)},Directory());CHECK(c.result==ApplicationClaimResult::Claimed);claim=c.claim;
        auto b=a->OpenReceiver(claim,L(2),&receiver);CHECK(b.result==ReceiverOpenResult::Opened);binding=b.binding;
    }
    ~Fixture(){if(a)a->AwaitDetached(claim);}
    ReceiveAdmissionResult Admit(ReceiveAdmissionDiagnostic* d=nullptr){return a->AdmitReceive(L(2),L(4),payload,3,d);}
};
// Independent callback ledger model; not a socket/SDK callback test.
void CallbackLedgerModel(const std::string& mode){
    namespace cb=fastdds_dsoftbus::ros2_host_callback;
    CHECK(cb::reserved.load()==0&&!cb::overflow.load());
    WSASetLastError(WSAECONNRESET);
    {
        cb::HostCallbackObservation receipt({1,3,4,5,6});
        CHECK(WSAGetLastError()==WSAECONNRESET);
        if(mode!="off"){
            CHECK(cb::reserved.load()==1&&cb::slots[0].entered.load());
            CHECK(!cb::slots[0].returned.load());
            CHECK(cb::slots[0].ingress.ordinal==1&&cb::slots[0].ingress.socket==3&&
                cb::slots[0].ingress.epoch==4&&cb::slots[0].ingress.handle==5&&cb::slots[0].ingress.size==6);
        }
    }
    CHECK(WSAGetLastError()==WSAECONNRESET);
    if(mode=="off"){CHECK(cb::reserved.load()==0&&!cb::slots[0].entered.load());return;}
    CHECK(cb::slots[0].returned.load());
    auto append=[] {for(unsigned i=0;i<16;++i){cb::HostCallbackObservation receipt({i+2,3,4,5,6});}};
    auto a=std::async(std::launch::async,append),b=std::async(std::launch::async,append);a.get();b.get();
    CHECK(cb::reserved.load()==33);
    for(std::size_t i=0;i<33;++i)CHECK(cb::slots[i].entered.load()&&cb::slots[i].returned.load());
    if(mode=="exhaustion"){
        for(std::size_t i=33;i<cb::capacity;++i){cb::HostCallbackObservation receipt({i+1,3,4,5,6});}
        CHECK(cb::reserved.load()==cb::capacity&&!cb::overflow.load());
        {cb::HostCallbackObservation extra({99999,3,4,5,6});}
        CHECK(cb::reserved.load()==cb::capacity&&cb::overflow.load());
        for(std::size_t i=0;i<cb::capacity;++i)CHECK(cb::slots[i].entered.load()&&cb::slots[i].returned.load());
    }
}
void ActualBudgetPaths(){
    Fixture f;ReceiveAdmissionDiagnostic first,second,refused;
    {
        rx::ScopedIngress ingress({1,70,9,700,3});CHECK(f.Admit(&first)==ReceiveAdmissionResult::Accepted);
    }
    CHECK(f.Admit(&second)==ReceiveAdmissionResult::Accepted);
    CHECK(f.Admit(&refused)==ReceiveAdmissionResult::Capacity);
    CHECK(refused.reason==RxAdmissionReason::LocalBytes&&refused.local_bytes_used==6);
    if(rx::Enabled()){
        CHECK(first.observation_record&&first.observation_record!=second.observation_record);
        CHECK(first.observation_scope==second.observation_scope&&refused.observation_scope==first.observation_scope);
        CHECK(refused.observation_record==0&&refused.observation_gate>first.observation_gate);
    }
    auto taken=f.a->TryTake();CHECK(taken);CHECK(ApplicationAttachmentsTestAccess::Requeue(*f.a,taken));CHECK(!taken);
    CHECK(f.a->TryTake().Deliver()==InputDeliveryResult::Delivered);
    CHECK(f.Admit()==ReceiveAdmissionResult::Accepted);
    CHECK(f.a->AwaitDetached(f.claim)==AttachmentControlResult::Completed);
    CHECK(f.a->SnapshotUsage().records==0&&f.a->SnapshotUsage().bytes==0);
    // Same local/application/correlation in another budget is a distinct scope.
    Fixture other;ReceiveAdmissionDiagnostic d;CHECK(other.Admit(&d)==ReceiveAdmissionResult::Accepted);
    if(rx::Enabled())CHECK(d.observation_scope!=first.observation_scope);
}
void Rollbacks(){
    {Fixture f;ReceiveAdmissionDiagnostic d;{
        detail::AttachmentReceiveCapture capture;
        CHECK(ApplicationAttachmentsTestAccess::Capture(*f.a,f.claim,L(2),L(4),capture,d)==ReceiveAdmissionResult::Accepted);
    }CHECK(f.a->SnapshotUsage().records==0);}
    // Fault before AttachmentRecord allocation: capture owns reservation.
    {Fixture f([]{throw std::bad_alloc();});CHECK(f.Admit()==ReceiveAdmissionResult::AllocationFailure);CHECK(f.a->SnapshotUsage().records==0);}
    // Fault after record allocation: record owns the one original reservation.
    {Fixture f({},[]{throw std::bad_alloc();});CHECK(f.Admit()==ReceiveAdmissionResult::AllocationFailure);CHECK(f.a->SnapshotUsage().records==0);}
    {Fixture f;ReceiveAdmissionDiagnostic d;{
        detail::AttachmentReceiveCapture capture;
        CHECK(ApplicationAttachmentsTestAccess::Capture(*f.a,f.claim,L(2),L(4),capture,d)==ReceiveAdmissionResult::Accepted);
        CHECK(ApplicationAttachmentsTestAccess::Copy(*f.a,capture,payload,d)==ReceiveAdmissionResult::Accepted);
        CHECK(f.a->RequestDetach(f.claim)==AttachmentControlResult::Requested);
        CHECK(ApplicationAttachmentsTestAccess::Commit(*f.a,capture,d)==ReceiveAdmissionResult::Revoked);
    }CHECK(f.a->SnapshotUsage().records==0);}
    {Fixture f;CHECK(f.Admit()==ReceiveAdmissionResult::Accepted);auto work=f.a->TryTake();CHECK(work);
        f.a.reset();work={};} // Original budget survives control destruction.
}
void ConcurrentGate(){
    Fixture f({}, {},true);
    auto producer=[&]{for(int i=0;i<16;++i)CHECK(f.Admit()==ReceiveAdmissionResult::Accepted);};
    auto a=std::async(std::launch::async,producer),b=std::async(std::launch::async,producer);a.get();b.get();
    CHECK(f.a->SnapshotUsage().queued==32);
    while(auto work=f.a->TryTake())CHECK(work.Deliver()==InputDeliveryResult::Delivered);
    CHECK(f.a->SnapshotUsage().bytes==0);
}
void HostStageAndRefusals(){
    using fastdds_dsoftbus::ros2_host_receive::Observer;
    Observer observer;ReceiveDecision closing;closing.branch=ReceiveDecisionBranch::AdapterPhase;
    closing.bridge.reason=RxAdmissionReason::Closing;closing.sequence=1;
    ReceiveDecision capacity;capacity.branch=ReceiveDecisionBranch::BridgeRefused;capacity.bridge_called=true;
    capacity.bridge.reason=RxAdmissionReason::LocalBytes;capacity.sequence=2;
    rx::ScopedIngress outer({45,7,8,9,3});CHECK(rx::CurrentIngress().ordinal==45);
    {rx::ScopedIngress inner({46,17,18,19,3});observer.OnDecision(closing);CHECK(rx::CurrentIngress().ordinal==46);}
    CHECK(rx::CurrentIngress().ordinal==45);observer.OnDecision(capacity);
    ReceiveDiagnostic d;d.correlation=5;d.observation_scope=999;d.observation_record=100;
    observer.OnReceive(ReceiveStage::RecordReleased,d,nullptr,0);
    observer.OnReceive(ReceiveStage::Queued,d,nullptr,0); // Deliberate callback arrival overtake.
}
void Reconcile(){
    struct State {std::uint64_t gate=0;std::map<std::uint64_t,std::pair<int,std::size_t>> records;};
    std::map<std::uint64_t,State> scopes;
    std::size_t rollback=0,refusal=0,decision=0,release_stage=0,queue_stage=0;
    for(std::size_t i=0;i<rx::journal.Reserved();++i){rx::Event e;CHECK(rx::journal.Read(i,e));
        if(e.kind==rx::Kind::Decision){++decision;continue;}
        if(e.kind==rx::Kind::Stage){if(e.stage==int(ReceiveStage::RecordReleased))++release_stage;
            if(e.stage==int(ReceiveStage::Queued))++queue_stage;continue;}
        CHECK(e.scope&&e.gate);auto& s=scopes[e.scope];CHECK(e.gate==++s.gate);
        if(e.kind==rx::Kind::Charge){CHECK(e.record&&s.records.emplace(e.record,std::make_pair(0,e.size)).second);}
        else if(e.kind==rx::Kind::CapacityRefusal){CHECK(e.record==0);++refusal;}
        else {
            auto at=s.records.find(e.record);CHECK(at!=s.records.end()&&at->second.second==e.size);
            const auto old=at->second.first;
            if(e.kind==rx::Kind::RefundRecord||e.kind==rx::Kind::RefundCapture){
                CHECK(old==e.phase);s.records.erase(at);if(e.kind==rx::Kind::RefundCapture)++rollback;
            }else{
                if(e.kind==rx::Kind::Queue)CHECK(old==0&&e.phase==1);
                if(e.kind==rx::Kind::Take)CHECK(old==1&&e.phase==2);
                if(e.kind==rx::Kind::Enter)CHECK(old==2&&e.phase==3);
                if(e.kind==rx::Kind::Requeue)CHECK(old==2&&e.phase==1);
                at->second.first=e.phase;
            }
        }
        std::size_t bytes=0;std::array<std::size_t,4> phases{};
        for(const auto& item:s.records){bytes+=item.second.second;++phases.at(item.second.first);}
        CHECK(e.records==s.records.size()&&e.bytes==bytes&&e.local_records==e.records&&e.local_bytes==bytes);
        CHECK(e.copying==phases[0]&&e.queued==phases[1]&&e.taken==phases[2]&&e.delivering==phases[3]);
    }
    for(const auto& scope:scopes)CHECK(scope.second.records.empty());
    CHECK(rollback==2&&refusal==1&&decision==2&&release_stage==1&&queue_stage==1);
    CHECK(rx::journal.Complete());
}
}
int main(int argc,char** argv){try{
    {fastdds_dsoftbus::ros2_host_callback::HostCallbackObservation default_off({1,2,3,4,5});}
    CHECK(fastdds_dsoftbus::ros2_host_callback::reserved.load()==0);
    const std::string mode=argc>1?argv[1]:"on";CHECK(rx::Configure(mode!="off"));
    CallbackLedgerModel(mode);
    if(mode=="exhaustion"){
        auto value=(std::numeric_limits<std::uint64_t>::max)();CHECK(rx::Next(value)==0);
        CHECK(!rx::journal.Complete());std::cout<<"PASS exhausted journal rejected\n";return 0;
    }
    rx::Ledger<2> capacity_ledger;rx::Event event;capacity_ledger.Append(event);capacity_ledger.Append(event);CHECK(capacity_ledger.Complete());capacity_ledger.Append(event);CHECK(!capacity_ledger.Complete());
    ActualBudgetPaths();Rollbacks();ConcurrentGate();HostStageAndRefusals();CHECK(rx::CurrentIngress().ordinal==0);
    if(mode=="off")CHECK(rx::journal.Reserved()==0);else Reconcile();
    std::cout<<"PASS actual budget charge/refund/rollback/requeue/concurrency and synthetic decision observer model mode="<<mode<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
