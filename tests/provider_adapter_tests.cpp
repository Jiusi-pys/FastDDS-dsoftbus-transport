#include <fastdds_dsoftbus/dsoftbus/ProviderAdapter.hpp>
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
#include <cstring>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <vector>
#include <cstdlib>
#include <new>

// Host-only fault injection at the real owned-copy admission allocation path.
thread_local bool phase0_deny_allocations = false;
void* operator new(std::size_t n) {
    if (phase0_deny_allocations) throw std::bad_alloc();
    if (void* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

using namespace fastdds_dsoftbus::dsoftbus;
using namespace fastdds_dsoftbus::dsoftbus::phase0;
namespace {
void Check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
Locator Loc(std::uint8_t n) { EndpointId id{}; id[0]=0x0A; id[14]=0x7F; id[15]=n; Locator l; Check(LocatorCodec::Encode(id,l),"locator"); return l; }
struct Receiver final : InputChannels::Receiver {
    int calls=0; bool throwing=false; std::vector<std::uint8_t> bytes;
    void OnDataReceived(const eprosima::fastdds::rtps::octet* p, std::uint32_t n,
            const Locator&, const Locator&) override {
        ++calls; if(throwing) throw std::runtime_error("receiver callback"); bytes.assign(p,p+n);
    }
};
struct MockProviderBackend final : NativeBackend {
    BackendIdentity identity{"provider.phase0",{101},{202}};
    ProviderCallbacks callbacks;
    int created=0, bound=0, listened=0, closed=0, sends=0;
    bool throw_create=false, inline_bytes=false, inline_stop=false, throw_shutdown=false;
    std::vector<std::int32_t> create_stops;
    BackendStatus inline_result=BackendStatus::NotImplemented;
    std::function<void()> during_setup;
    BackendKind Kind() const noexcept override {return BackendKind::HostMock;}
    BackendIdentity Identity() const override {return identity;}
    BackendSocketResult Create(ProviderCallbacks c) override {
        callbacks=c; ++created; if(throw_create) throw std::runtime_error("create");
        for(auto id:create_stops) inline_result=callbacks.OnShutdown(id);
        return {BackendStatus::None,11};
    }
    BackendCallResult Listen(std::int32_t) override {++listened; return Setup();}
    BackendCallResult Bind(std::int32_t) override {++bound; return Setup();}
    BackendStatus Setup() {
        if(during_setup)during_setup();
        const std::uint8_t byte=7;
        if(inline_bytes) inline_result=callbacks.OnBytes(11,&byte,1);
        if(inline_stop) inline_result=callbacks.OnShutdown(11);
        return BackendStatus::None;
    }
    BackendSendResult Send(std::int32_t,const std::uint8_t*,std::size_t,std::chrono::steady_clock::time_point) override {
        ++sends; return {BackendStatus::NotImplemented,std::nullopt};
    }
    BackendCallResult Shutdown(std::int32_t id) override {Check(id==11,"owned socket close"); ++closed; if(throw_shutdown) throw std::runtime_error("shutdown"); return BackendStatus::None;}
};
struct Fixture {
    ProviderAdapterDomain domain;
    NativeTableConfiguration config;
    std::unique_ptr<NativeOwnerTable> native;
    std::unique_ptr<ApplicationAttachments> attachments;
    Receiver receiver;
    std::shared_ptr<MockProviderBackend> provider=std::make_shared<MockProviderBackend>();
    BindingRequest request;
    Fixture() {
        config.directory.locals={{Loc(1),"phase0.ns","phase0.listen"}};
        config.directory.routes={{{Loc(1),Loc(2)},{"net.model","remote.model","dial.model",{"peer.model"},RecordDataType::Bytes},1}};
        config.directions={RouteDirection::Dial};config.service_pkg_name="phase0.host";
        auto n=NativeOwnerTable::Build(config);Check(bool(n),"native model");native=std::move(*n.value);
        attachments=std::make_unique<ApplicationAttachments>(std::vector<Locator>{Loc(1)});
        request.service_name=provider->identity.service_name; request.role=RecordRole::Client;
        request.local=Loc(1);request.route=config.directory.routes[0].key;request.has_route=true;
        request.config=config;request.directory=*EndpointDirectory::Build(config.directory).value;request.receiver=&receiver;
    }
    AdapterBuildResult Build(std::shared_ptr<NativeBackend> p={}, const BindingRequest* q=nullptr) {
        return ProviderAdapter::Build(domain,ServiceRecordTable::Process(),*native,*attachments,p?p:provider,q?*q:request);
    }
    std::unique_ptr<ProviderAdapter> Make() {auto r=Build();Check(r.status==BackendStatus::None && r.adapter,"adapter build");return std::move(r.adapter);}
    void Ready(ProviderAdapter& a) {Check(a.Initialize()==BackendStatus::None,"initialize");Check(a.Publish()==BackendStatus::None,"publish");}
    void Deliver(ProviderAdapter& a) {
        std::uint8_t data[]={1,2,3,4};Check(provider->callbacks.OnBytes(11,data,4)==BackendStatus::None,"adapter ingress");
        std::memset(data,9,4);Check(receiver.calls==0,"callback only copies, not direct receiver");
        Check(a.DispatchOne()==BackendStatus::None,"binding delivery");Check(receiver.bytes==std::vector<std::uint8_t>({1,2,3,4}),"owned copy");
    }
};
void P0() {
    NullBackend n;Check(n.Kind()==BackendKind::Unavailable,"null mode");Check(!n.Create({}).socket,"no socket");
    Check(n.Listen(11).status==BackendStatus::NativeBackendUnavailable && n.Bind(11).status==BackendStatus::NativeBackendUnavailable,"null setup");
    auto send=n.Send(11,nullptr,0,{});Check(send.status==BackendStatus::NativeBackendUnavailable && !send.provider_return,"notcalled no fabricated rc");
    Check(n.Shutdown(11).status==BackendStatus::NativeBackendUnavailable,"null shutdown");
    Fixture f; DSoftBusTransportDescriptor d(64);d.directory=f.config.directory;
    d.default_metatraffic_unicast={Loc(1)};d.default_user_unicast={Loc(1)};
    Check(d.ConfigureV1({{"provider-default",0,"synthetic-local"},17,v1::Profile::BytesNative,"default.ns",{}}).code==DirectoryError::None,"V1 default precondition");
    DSoftBusTransport t(d);Check(t.mode()==FacadeMode::NativeUnavailable,"real facade default");
    Check(!t.init() && t.last_error().error==FacadeError::NativeBackendUnavailable,"real init remains unavailable");
}
void P1() {
    Fixture f;auto a=f.Make();f.Ready(*a);auto before=a->Observe();
    Check(before.binding.record_incarnation && before.binding.owner_incarnation && before.binding.socket==11,"distinct captured model identities");
    f.Deliver(*a);Check(a->Observe().binding.admitted==1,"real binding admission");
    auto closed=a->Close();Check(closed.status==BackendStatus::None && closed.adapter_drained && closed.sdk_drain==SdkDrain::Unproven,"only adapter drain");
    Check(f.provider->created==1 && f.provider->bound==1 && f.provider->closed==1 && f.provider->sends==0,"provider counts");
    Check(a->Close().adapter_drained && f.provider->closed==1,"idempotent close");
}
void P2() {
    Fixture f;auto a=f.Make();f.Ready(*a);std::uint8_t byte=1;f.receiver.throwing=true;
    Check(f.provider->callbacks.OnBytes(11,&byte,1)==BackendStatus::None,"admit throwing receiver");
    Check(a->DispatchOne()==BackendStatus::CallbackFailure,"contain callback exception");
    f.receiver.throwing=false;f.receiver.calls=0;f.Deliver(*a);Check(a->Close().adapter_drained,"binding remains closeable");
}
void P3() {
    Fixture f;auto a=f.Make();f.Ready(*a);Check(f.provider->callbacks.OnShutdown(11)==BackendStatus::None,"provider stop");
    std::uint8_t b=1;Check(f.provider->callbacks.OnBytes(11,&b,1)==BackendStatus::ProviderStopped,"fail closed");
    Check(a->Publish()==BackendStatus::ProviderStopped && a->DispatchOne()==BackendStatus::ProviderStopped,"no reopen or delivery");
    Check(f.receiver.calls==0 && f.provider->closed==0,"notification not binding close");
    Check(a->Close().adapter_drained && f.provider->closed==1,"control closes binding");
}
void P4() {
    Fixture f;auto usage=ServiceRecordTable::Process().SnapshotUsage();f.provider->identity.record.value=0;
    Check(f.Build().status==BackendStatus::InvalidIdentity,"missing record");f.provider->identity.record.value=101;f.provider->identity.owner.value=0;
    Check(f.Build().status==BackendStatus::InvalidIdentity,"missing owner");
    Check(f.provider->created==0 && ServiceRecordTable::Process().SnapshotUsage().records_created==usage.records_created,"refuse before exposure");
}
void P5() {
    Fixture f;auto a=f.Make();f.Ready(*a);auto other=std::make_shared<MockProviderBackend>();auto request=f.request;
    other->identity.service_name="alias.phase0";request.service_name=other->identity.service_name;
    Check(f.Build(other,&request).status==BackendStatus::DuplicateRecord,"same token different name");
    other->identity.record.value=303;other->identity.service_name=f.request.service_name;
    Check(f.Build(other).status==BackendStatus::DuplicateRecord,"same name different token");
    Check(other->created==0 && f.provider->closed==0,"no side effect");f.Deliver(*a);Check(a->Close().adapter_drained,"first stays healthy");
}
void P6() {
    Fixture f;f.provider->inline_bytes=true;auto a=f.Make();f.Ready(*a);
    Check(f.provider->inline_result==BackendStatus::InvalidState && f.receiver.calls==0,"inline prepublish rejected");
    auto old=f.provider->callbacks;Check(a->Close().adapter_drained,"close");a.reset();std::uint8_t b=1;
    Check(old.OnBytes(11,&b,1)==BackendStatus::InvalidState,"late weak callback rejected");
}
void P7() {
    Fixture f;auto a=f.Make();f.Ready(*a);std::uint8_t b=1;
    Check(f.provider->callbacks.OnBytes(12,&b,1)==BackendStatus::ForeignSocket,"foreign socket");
    Check(f.provider->callbacks.OnShutdown(12)==BackendStatus::ForeignSocket,"unknown shutdown cannot close owner");
    Check(f.provider->callbacks.OnBytes(11,nullptr,1)==BackendStatus::InvalidBuffer,"invalid buffer");
    f.Deliver(*a);Check(a->Close().adapter_drained,"still healthy");
}
void EarlyStop() {
    Fixture f;f.provider->inline_stop=true;auto a=f.Make();
    Check(a->Initialize()==BackendStatus::ProviderStopped,"setup stop wins");
    Check(a->Publish()==BackendStatus::ProviderStopped && f.provider->inline_result==BackendStatus::None,"no late publication");
    Check(a->Close().adapter_drained,"close after early stop");
}
void ProviderThrow() {
    Fixture f;f.provider->throw_create=true;auto a=f.Make();
    Check(a->Initialize()==BackendStatus::ProviderException,"provider exception caught before noexcept");
    Check(a->Observe().provider_exceptions==1,"exception accounted");Check(a->Close().adapter_drained,"failed setup close");
}
void Listen() {
    Fixture f;f.request.role=RecordRole::Server;f.request.has_route=false;auto a=f.Make();f.Ready(*a);
    Check(f.provider->listened==1 && f.provider->bound==0,"listen path");Check(a->Close().adapter_drained,"listener close");
}
void CreateStop() {
    Fixture f;f.provider->create_stops={11};auto a=f.Make();
    Check(a->Initialize()==BackendStatus::ProviderStopped,"inline Create stop is terminal");
    Check(f.provider->inline_result==BackendStatus::PendingIdentity,"stop first waits for returned identity");
    Check(a->Publish()==BackendStatus::ProviderStopped && f.provider->bound==0,"stopped create must not Bind or Publish");
    Check(a->Close().adapter_drained && f.provider->closed==1,"only returned owned socket closed");
}
void CreateForeign() {
    Fixture f;f.provider->create_stops={12};auto a=f.Make();f.Ready(*a);
    Check(f.provider->inline_result==BackendStatus::PendingIdentity,"candidate not guessed");
    Check(!a->Observe().provider_stopped && a->Observe().ingress_rejected==1,"foreign candidate rejected after identity resolves");
    Check(f.provider->closed==0,"foreign id never shut down");f.Deliver(*a);Check(a->Close().adapter_drained,"healthy owner closes");
}
void CreateConflict() {
    Fixture f;f.provider->create_stops={11,12};auto a=f.Make();
    Check(a->Initialize()==BackendStatus::ProviderStopped,"conflicting candidates fail closed");
    Check(a->Observe().provider_status==BackendStatus::InvalidIdentity && a->Publish()==BackendStatus::ProviderStopped,"ambiguous identity visible");
    Check(a->Close().adapter_drained && f.provider->closed==1,"only original owned ticket closes");
}
void ShutdownFailure() {
    Fixture f;f.provider->throw_shutdown=true;auto a=f.Make();f.Ready(*a);
    const auto first=a->Close(),second=a->Close();
    Check(first.status==BackendStatus::ProviderException && first.adapter_drained,"first shutdown exception retained");
    Check(second.status==first.status && second.adapter_drained && f.provider->closed==1,"repeated close cannot replace failure with success");
    Check(second.sdk_drain==SdkDrain::Unproven,"failure never implies SDK quiescence");
}
void IngressAllocation() {
    Fixture f;auto a=f.Make();f.Ready(*a);std::uint8_t byte=1;
    phase0_deny_allocations=true;const auto status=f.provider->callbacks.OnBytes(11,&byte,1);phase0_deny_allocations=false;
    Check(status==BackendStatus::None,"forwarding receipt is not admission guarantee");
    const auto view=a->Observe();
    Check(view.binding.admitted==0 && view.binding.refused_receive==1 && view.ingress_active==0,"allocation refusal settled at actual binding");
    Check(f.receiver.calls==0 && view.binding.receive.queued==0 && view.binding.receive.copying==0,"no partial delivery or future queue");
    f.Deliver(*a);Check(a->Close().adapter_drained,"binding recovers after callback allocation fault");
}
void SetupObserve() {
    Fixture f;auto a=f.Make();bool observed=false;
    f.provider->during_setup=[&]{std::cerr<<"PROVIDER_SETUP_REENTER_OBSERVE\n"<<std::flush;auto v=a->Observe();observed=v.phase==AdapterPhase::Deploying;};
    f.Ready(*a);Check(observed,"provider call is outside binding gate");Check(a->Close().adapter_drained,"close");
}
struct Decisions final:ProcessReceiveObserver {
    ReceiveDecision last{};std::size_t adapter_count=0,binding_count=0;
    void OnReceive(ReceiveStage,const ReceiveDiagnostic&,const std::uint8_t*,std::size_t) noexcept override {}
    void OnDecision(const ReceiveDecision& d) noexcept override {
        if(d.layer==ReceiveDecisionLayer::Adapter){last=d;++adapter_count;}else ++binding_count;
    }
};
void DecisionBranches() {
    Fixture f;auto observer=std::make_shared<Decisions>();f.request.receive_observer=observer;
    f.provider->inline_bytes=true;auto a=f.Make();f.Ready(*a);
    Check(observer->adapter_count==1 && observer->binding_count==0,"early adapter only decision");
    Check(observer->last.branch==ReceiveDecisionBranch::AdapterPhase && observer->last.status_known &&
          observer->last.status==static_cast<int>(BackendStatus::InvalidState) && !observer->last.bridge_called,
          "actual early InvalidState branch without invented bridge");
    const std::uint8_t byte=3;
    Check(f.provider->callbacks.OnBytes(999,&byte,1)==BackendStatus::ForeignSocket,"foreign rejects");
    Check(observer->last.branch==ReceiveDecisionBranch::ForeignSocket && observer->binding_count==0,"no binding on foreign");
    Check(f.provider->callbacks.OnBytes(11,nullptr,1)==BackendStatus::InvalidBuffer,"invalid buffer rejects");
    Check(observer->last.branch==ReceiveDecisionBranch::InvalidBuffer,"before parsing");
    Check(f.provider->callbacks.OnBytes(11,&byte,1)==BackendStatus::None,"forward");
    Check(observer->last.branch==ReceiveDecisionBranch::Forwarded && observer->binding_count==1,"layers distinct");
    Check(a->CloseInput()==BackendStatus::None,"input close");
    Check(f.provider->callbacks.OnBytes(11,&byte,1)==BackendStatus::ChannelClosed,"gap rejects");
    Check(observer->last.branch==ReceiveDecisionBranch::ChannelClosed,"gap branch");
    Check(a->Close().adapter_drained,"close");
    Check(f.provider->callbacks.OnBytes(11,&byte,1)==BackendStatus::InvalidState,"closed phase rejects");
    Check(observer->last.branch==ReceiveDecisionBranch::AdapterPhase,"closed phase branch");
}
}
int main(int argc,char** argv) {
    try {
        Check(argc==2,"one case required");std::string s=argv[1];
        const std::pair<const char*,void(*)()> cases[]={{"P0",P0},{"P1",P1},{"P2",P2},{"P3",P3},{"P4",P4},{"P5",P5},{"P6",P6},{"P7",P7},{"early-stop",EarlyStop},{"provider-throw",ProviderThrow},{"listen",Listen},{"create-stop",CreateStop},{"create-foreign",CreateForeign},{"create-conflict",CreateConflict},{"shutdown-failure",ShutdownFailure},{"ingress-allocation",IngressAllocation},{"setup-observe",SetupObserve}};
        if(s=="decision-branches"){DecisionBranches();std::cout<<"PASS "<<s<<'\n';return 0;}
        for(const auto& c:cases)if(s==c.first){c.second();std::cout<<"PASS "<<s<<'\n';return 0;}
        throw std::runtime_error("unknown case");
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
