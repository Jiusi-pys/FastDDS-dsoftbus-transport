#pragma once
// Included only by the ordinary stock214 driver. These are Transport Host
// contracts, never native SDK/DDS/board qualification or an SDK OnError claim.
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <trans_type.h>
#include <fastdds_dsoftbus/dsoftbus/DomainCircuitName.hpp>
// Existing test friend: snapshot only; never modifies queue or grants authority.
namespace fastdds_dsoftbus::dsoftbus {
class RuntimeSendPathTestAccess {
public:
    static SendQueueUsage Snapshot(const DSoftBusSenderResource& sender) {
        std::lock_guard<std::mutex> lock(sender.gate_);
        const auto path=sender.pending_?sender.pending_->target():sender.runtime_;
        if(!path)throw std::runtime_error("bound sender path absent");
        return path->SnapshotUsage();
    }
};
}
namespace {
struct PairContract {
    std::shared_ptr<host_dds::Trace> trace=std::make_shared<host_dds::Trace>();
    Receiver ra,rb;std::unique_ptr<experimental::Runtime> a,b;
    DSoftBusTransportDescriptor da=Descriptor(0),db=Descriptor(1);
    std::shared_ptr<rtps::TransportDescriptorInterface> ad,bd;
    std::unique_ptr<rtps::TransportInterface> ta,tb;rtps::SendResourceList as,bs;
    Locator al=da.directory.locals.front().local_locator,bl=db.directory.locals.front().local_locator;
    Locator ar=da.directory.routes.front().key.remote_locator,br=db.directory.routes.front().key.remote_locator;
    explicit PairContract(bool explicit_domain=false) {
        public_socket_fake::Configure(trace,true);public_socket_fake::SetListenerNames({Listener(0),Listener(1)});
        public_socket_fake::SetInitiatorNames({Configuration(0).identity.service_name,Configuration(1).identity.service_name});
        a=std::make_unique<experimental::Runtime>(Configuration(0));b=std::make_unique<experimental::Runtime>(Configuration(1));
        ad=explicit_domain?a->MakeStockDescriptor(da):a->MakeDescriptor(da);bd=explicit_domain?b->MakeStockDescriptor(db):b->MakeDescriptor(db);
        Check(ad&&bd,"contract descriptors");ta.reset(ad->create_transport());tb.reset(bd->create_transport());
        Check(ta&&tb&&ta->init()&&tb->init(),"contract facade init");
    }
    bool Did(bool side,unsigned domain=151) {
        fastdds_dsoftbus::binding::PortParameters ports;rtps::LocatorList peers;
        return (side?tb:ta)->configureInitialPeerLocator(side?br:ar,ports,domain,peers);
    }
    void Cache(){Check(ta->OpenOutputChannel(as,ar)&&tb->OpenOutputChannel(bs,br)&&as.size()==1&&bs.size()==1,"same cached senders");}
    void BInput(){Check(tb->OpenInputChannel(bl,&rb,8192),"B public input");}
    void AInput(){Check(ta->OpenInputChannel(al,&ra,8192),"A public input");}
    void Deliver(bool side,std::uint8_t marker,unsigned expected) {
        Check(Send(*(side?bs:as).front(),side?br:ar,marker),"public sender submission");
        Check((side?b:a)->PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"real PumpSend/Fake forwarding");
        (side?a:b)->PumpReceive();Check((side?ra:rb).Exact(expected,marker),"exact application marker");
    }
    void Finish(bool failed_b=false) {
        ta.reset();tb.reset();as.clear();bs.clear();const bool ac=a->Close(),bc=b->Close();
        const auto f=public_socket_fake::Observe();
        Check(ac&&(failed_b?!bc:bc),"explicit retained close status");
        Check(!f.live&&!f.queued&&!f.bytes&&!f.invalid&&trace->Good(),"contract final ownership/queue/trace");
        Check(!phase0::RuntimeProbeAccess::ObserveRuntime(*a).runtime_admitted_outstanding,"runtime outstanding");
    }
    ~PairContract(){ta.reset();tb.reset();as.clear();bs.clear();if(a)a->Close();if(b)b->Close();}
};
struct BindGate {
    std::mutex gate;std::condition_variable changed;bool entered=false,released=false;
    std::future<bool> sending,closing;
    BindGate(){public_socket_fake::SetBindBarrier([this]{std::unique_lock<std::mutex> lock(gate);entered=true;changed.notify_all();changed.wait(lock,[&]{return released;});});}
    void Entered(){std::unique_lock<std::mutex> lock(gate);Check(changed.wait_for(lock,2s,[&]{return entered;}),"precise native Bind gate not reached");}
    void Release(){std::lock_guard<std::mutex> lock(gate);released=true;changed.notify_all();}
    ~BindGate(){Release();if(sending.valid())try{sending.get();}catch(...){}if(closing.valid())try{closing.get();}catch(...){}public_socket_fake::SetBindBarrier({});}
};
bool TimedSend(fastdds_dsoftbus::binding::SenderResource& sender,const Locator& remote,std::chrono::steady_clock::time_point deadline){
    std::vector<Locator> locators{remote};rtps::Locators first(locators.begin()),last(locators.end());
    const std::uint8_t bytes[]{221,19,83,7};return sender.send(bytes,4,&first,&last,deadline);
}
// The internal OnError port is exercised at ProviderAdapter with a declared
// model backend. ISocketListener has no corresponding SDK event in this source.
void AdapterErrorContract(bool foreign) {
    using namespace phase0;
    struct Model final:NativeBackend {
        ProviderCallbacks callbacks;unsigned closed=0;
        BackendKind Kind()const noexcept override{return BackendKind::HostMock;}
        BackendIdentity Identity()const override{return {"model.error.anchor",{7001},{7002}};}
        BackendSocketResult Create(ProviderCallbacks c)override{callbacks=c;return {BackendStatus::None,11};}
        BackendCallResult Listen(int)override{return BackendStatus::None;}
        BackendCallResult Bind(int)override{return BackendStatus::None;}
        BackendCallResult Bind(int,std::chrono::steady_clock::time_point)override{return BackendStatus::None;}
        BackendStatus CommitAcceptedOwner(const RecordEntrance&,const NativeOwnerLease&)override{return BackendStatus::None;}
        BackendSendResult Send(int,const std::uint8_t*,std::size_t,std::chrono::steady_clock::time_point)override{return {BackendStatus::None,0};}
        BackendCallResult Shutdown(int id)override{Check(id==11||id==12,"model original owner");++closed;return BackendStatus::None;}
    };
    auto backend=std::make_shared<Model>();auto d=Descriptor(0);NativeTableConfiguration config;
    config.directory=d.directory;config.directions={RouteDirection::Both};config.dial_only_egress=true;config.service_pkg_name=Configuration(0).package_name;
    auto native=NativeOwnerTable::Build(config);Check(bool(native),"model native table");
    ApplicationAttachments apps({d.directory.locals.front().local_locator});ProviderAdapterDomain domain;BindingRequest request;
    request.service_name=backend->Identity().service_name;request.role=RecordRole::Client;request.has_route=true;request.route=d.directory.routes.front().key;
    request.local=d.directory.locals.front().local_locator;request.config=config;request.directory=*EndpointDirectory::Build(d.directory).value;request.shared_input_channel=true;
    auto built=ProviderAdapter::Build(domain,ServiceRecordTable::Process(),*native.value.value(),apps,backend,request);
    Check(built.status==BackendStatus::None&&built.adapter->PrepareCircuitAnchor()==BackendStatus::None,"model external anchor");
    Receiver receiver;auto input=built.adapter->AttachSharedInput(&receiver);Check(bool(input),"real metadata-anchor lease");
    Check(built.adapter->InitializeCircuitOutgoing(std::chrono::steady_clock::now()+2s)==BackendStatus::None,"model real native generation");
    const auto incoming=built.adapter->IncomingCallbacks();const auto c=Configuration(0);
    BackendPeer peer{c.expected_peer_initiator_name,c.peer_network_id,c.package_name,RecordDataType::Bytes};
    Check(incoming.OnNegotiate(12,peer)==BackendStatus::None&&incoming.OnBind(12,peer)==BackendStatus::None,"model accepted owner");
    const auto original=input.value->Identity();
    if(foreign){Check(backend->callbacks.OnError(999,-701)==BackendStatus::ForeignSocket&&backend->callbacks.OnNativeShutdown(999)==BackendStatus::ForeignSocket,"unknown identity original refusal");Check(!built.adapter->Observe().outgoing_stopped,"foreign callback cannot close known TX");}
    else {Check(backend->callbacks.OnError(11,-702)==BackendStatus::None,"known client error");const auto o=built.adapter->Observe();Check(o.outgoing_stopped&&!o.provider_stopped&&o.first_callback_error==-702,"error isolated with original evidence");}
    const std::uint8_t frame[]{222,19,83,7};Check(incoming.OnBytes(12,frame,4)==BackendStatus::None&&apps.SnapshotUsage().records==1,"RX admission to original lease survives");
    Check(input.value->Identity()==original&&input.value->Close()==NativeStatus::None,"original lease retirement");
    Check(built.adapter->Close().status==BackendStatus::None&&backend->closed==2&&!apps.SnapshotUsage().records,"model exact original owners cleanup");
    std::cout<<"INTERNAL_ERROR_PORT model_only=1 RX_admission=1 application_dispatch=NOT_EXERCISED nativeSDK_OnError=NOT_CLAIMED\n";
}
// A public stable-generation boundary is observed; no claim about the CV's
// internal sleeping instruction is made. Only failure cleanup requests stop.
struct ScanDrainWorker {
    experimental::Runtime& runtime;
    std::promise<NativeSendRunResult> first;
    std::future<NativeSendRunResult> first_done=first.get_future();
    std::promise<void> stable_wait;
    std::future<void> at_wait=stable_wait.get_future();
    std::promise<WorkWaitResult> terminal;
    std::future<WorkWaitResult> done=terminal.get_future();
    std::thread thread;
    explicit ScanDrainWorker(experimental::Runtime& r):runtime(r),thread([this]{
        bool initial=true,marked=false;
        try {
            for(;;){
                auto generation=runtime.WorkGeneration(); // enqueue has already completed
                for(;;){
                    const auto tx=runtime.PumpSend();
                    if(initial){first.set_value(tx);initial=false;}
                    bool progress=tx.status==NativeStatus::None;
                    if(progress)Check(tx.actual_return_code&&*tx.actual_return_code==0,"scan native send result");
                    // No Fake delivery or later enqueue can wake this runtime.
                    if(runtime.PumpReceive()==phase0::BackendStatus::None)progress=true;
                    if(!progress)break;
                }
                if(!marked&&generation==runtime.WorkGeneration()){
                    stable_wait.set_value();marked=true;
                }
                const auto result=runtime.WaitForWork(generation);
                if(result!=WorkWaitResult::Work&&result!=WorkWaitResult::Deadline){terminal.set_value(result);return;}
            }
        }catch(...){if(initial)first.set_exception(std::current_exception());terminal.set_exception(std::current_exception());}
    }){}
    void JoinClosed(){Check(done.wait_for(2s)==std::future_status::ready,"scan last facade did not wake worker");
        Check(done.get()==WorkWaitResult::Closed,"scan worker did not close naturally");thread.join();}
    ~ScanDrainWorker(){if(thread.joinable()){runtime.RequestPumpStop();thread.join();if(done.valid())try{done.get();}catch(...){}}}
};
void TwoCircuitScanContract(const std::string& name){
    const bool side=name.find("-B-")!=std::string::npos;
    const bool idle_second=name.find("slot1")!=std::string::npos;
    Receiver idle_receiver; // outlives both facades and every worker/error path
    PairContract p;
    auto& runtime=*(side?p.b:p.a);
    auto& primary=side?p.tb:p.ta;
    auto& descriptor=side?p.bd:p.ad;
    const auto& config=side?p.db:p.da;
    std::unique_ptr<rtps::TransportInterface> extra(descriptor->create_transport());
    Check(extra&&extra->init(),"second ordinary automatic view");
    auto* idle=idle_second?extra.get():primary.get();
    auto* active=idle_second?primary.get():extra.get();
    const auto local=config.directory.locals.front().local_locator;
    auto remote=config.directory.routes.front().key.remote_locator;
    rtps::SendResourceList sender;
    Check(idle->OpenInputChannel(local,&idle_receiver,8192),"idle view public input");
    fastdds_dsoftbus::binding::PortParameters ports;rtps::LocatorList peers;
    Check(idle->configureInitialPeerLocator(remote,ports,151,peers),"idle genuine domain151");peers.clear();
    Check(active->configureInitialPeerLocator(remote,ports,152,peers),"active genuine domain152");
    Check(p.Did(!side,152),"peer genuine domain152");
    if(side)p.AInput();else p.BInput();
    auto* idle_view=dynamic_cast<DSoftBusTransport*>(idle);auto* active_view=dynamic_cast<DSoftBusTransport*>(active);
    Check(idle_view&&active_view&&idle_view->domain_snapshot().state==ViewDomainState::Ready&&
        active_view->domain_snapshot().state==ViewDomainState::Ready&&idle_view->domain_snapshot().first_observed_domain==151&&
        active_view->domain_snapshot().first_observed_domain==152,"independent Ready151/152 guards");
    Check(active->OpenOutputChannel(sender,remote)&&sender.size()==1,"active cached public sender");
    if(idle_second){const auto empty=runtime.PumpSend();Check(!empty.actual_return_code,"empty parity pump sent work");}
    const auto before=public_socket_fake::Observe();
    Check(!before.binds[side]&&!before.sends[side],"idle view dialed or sent");
    Check(Send(*sender.front(),remote,222),"second-domain public submission");
    auto* actual_sender=dynamic_cast<DSoftBusSenderResource*>(sender.front().get());
    Check(actual_sender,"actual project sender");
    const auto report=actual_sender->last_report();
    const auto queued=RuntimeSendPathTestAccess::Snapshot(*actual_sender);
    Check(report&&report->status==RuntimeSendStatus::Accepted&&report->queue_result==SendAdmission::Accepted&&
        queued.records==1&&queued.queued==1&&queued.bytes==4&&!queued.closed,
        "exact queued TX prerequisite");
    Check(!phase0::RuntimeProbeAccess::ObserveRuntime(runtime).runtime_admitted_outstanding,
        "queue was already admitted to a native invocation");
    const auto before_worker=public_socket_fake::Observe();
    Check(!before_worker.sends[side]&&!before_worker.queued&&!before_worker.bytes,
        "queued submission invoked Fake Send or forwarded before worker");
    std::cout<<"SCAN_QUEUED records="<<queued.records<<" queued="<<queued.queued
        <<" bytes="<<queued.bytes<<" native_admitted=0\n";
    {
        ScanDrainWorker worker(runtime);
        Check(worker.first_done.wait_for(2s)==std::future_status::ready,"first scan did not finish");
        const auto first=worker.first_done.get();
        Check(first.status==NativeStatus::None&&first.actual_return_code&&*first.actual_return_code==0,
            "RX-only first scan blocked other domain queued TX");
        Check(worker.at_wait.wait_for(2s)==std::future_status::ready,"stable capture/drain/wait boundary absent");
        Check(worker.done.wait_for(0ms)==std::future_status::timeout,"worker ended before natural close");
        const auto sent=public_socket_fake::Observe();
        Check(sent.binds[side]==1&&sent.sends[side]==1&&sent.queued==1&&!idle_receiver.calls,
            "exact one active native call and isolated idle receiver");
        const auto settled=RuntimeSendPathTestAccess::Snapshot(*actual_sender);
        Check(!settled.records&&!settled.queued&&!settled.bytes&&
            !phase0::RuntimeProbeAccess::ObserveRuntime(runtime).runtime_admitted_outstanding,
            "TX queue/native ownership did not settle");
        Check(public_socket_fake::PumpOne()&&!public_socket_fake::PumpOne(),"exact one forwarded record");
        (side?p.a:p.b)->PumpReceive();
        Check((side?p.ra:p.rb).Exact(1,222)&&!idle_receiver.calls,"domain152 exact peer receipt and domain151 isolation");
        extra.reset();primary.reset();worker.JoinClosed();
    }
    const auto closed=RuntimeSendPathTestAccess::Snapshot(*actual_sender);
    Check(closed.closed&&!closed.records&&!closed.queued&&!closed.bytes,"closed sender queue retained work");
    Check(!Send(*sender.front(),remote,223),"closed cached sender accepted");sender.clear();
    p.Finish();
    std::cout<<"SCAN_PROTOCOL side="<<side<<" idle_slot="<<idle_second
        <<" first_pump_native=0 stable_public_wait=1 natural_closed=1 SDK_ABI=UNKNOWN\n";
}

int ContractCase(const std::string& name) {
    try {
        LoadedDll();
        if(name.rfind("rx-only-scan-",0)==0)TwoCircuitScanContract(name);
        else if(name=="client-error-RX-survives"||name=="unknown-identity-original-policy")AdapterErrorContract(name=="unknown-identity-original-policy");
        else {
            PairContract p(name=="explicit-input-first"||name=="explicit-DID-first"||name=="wrong-domain-reject");p.Cache();
            if(name=="await-reject") {
                Check(!Send(*p.as.front(),p.ar,210)&&!Send(*p.bs.front(),p.br,211),"Await cached sender refusal");
                const auto f=public_socket_fake::Observe();Check(!f.sockets[0]&&!f.sockets[1],"Await attempted SDK");p.Finish();
            } else if(name=="wrong-domain-reject") {
                p.BInput();p.Did(true,152);auto* b=dynamic_cast<DSoftBusTransport*>(p.tb.get());Check(b&&b->domain_snapshot().state==ViewDomainState::Refused&&!Send(*p.bs.front(),p.br,212),"explicit wrong domain refusal");
                p.Did(true,151);Check(b->domain_snapshot().state==ViewDomainState::Refused,"Refused resurrected");Check(!public_socket_fake::Observe().binds[1],"wrong domain dial");p.Finish();
            } else if(name=="attach-rollback") {
                p.BInput();bool reached=false;
                {phase0::ArmedDomainInputPublicationGate gate([&]{reached=true;throw std::runtime_error("intentional attachment rollback");});Check(!p.Did(true),"failed attachment DID accepted");}
                const auto o=phase0::RuntimeProbeAccess::ObserveRuntime(*p.b);Check(reached&&!o.receive.active_applications&&!o.receive.detaching_applications&&!public_socket_fake::Observe().binds[1],"unpublished lease rollback");p.Finish();
            } else {
                if(name=="generic-DID-first"||name=="explicit-DID-first"){Check(p.Did(true),"B DID first");p.BInput();}
                else {p.BInput();Check(p.Did(true),"B DID after input");}
                Check(!public_socket_fake::Observe().sockets[1]||public_socket_fake::Observe().sockets[1]==1,"B hidden outgoing creation");
                Check(!public_socket_fake::Observe().binds[1],"B DID eagerly dialed");Check(p.Did(false),"A DID");
                if(name=="close-during-TX-prepare"||name=="prepare-deadline") {
                    if(name=="prepare-deadline") {
                        const auto before=public_socket_fake::Observe();Check(!TimedSend(*p.as.front(),p.ar,std::chrono::steady_clock::now()-1ms)&&!TimedSend(*p.as.front(),p.ar,std::chrono::steady_clock::now()+std::chrono::nanoseconds(1)),"expired/subms setup accepted");
                        const auto after=public_socket_fake::Observe();Check(after.sockets[0]==before.sockets[0]&&after.binds[0]==before.binds[0],"expired deadline attempted native");
                    }
                    BindGate gate;gate.sending=std::async(std::launch::async,[&]{return Send(*p.as.front(),p.ar,221);});gate.Entered();
                    Check(phase0::RuntimeProbeAccess::ObserveRuntime(*p.a).active_pumps>0,"Runtime setup pin absent");
                    if(name=="close-during-TX-prepare") {
                        gate.closing=std::async(std::launch::async,[&]{p.ta.reset();return true;});
                        const auto until=std::chrono::steady_clock::now()+2s;bool sealed=false;
                        auto* sender=dynamic_cast<DSoftBusSenderResource*>(p.as.front().get());Check(sender,"real sender");
                        do {Check(!Send(*p.as.front(),p.ar,220),"parallel prepare published");const auto report=sender->last_report();sealed=report&&report->status==RuntimeSendStatus::DomainClosed;if(!sealed)std::this_thread::yield();}while(!sealed&&std::chrono::steady_clock::now()<until);
                        Check(sealed&&gate.closing.wait_for(0ms)==std::future_status::timeout,"public seal/close pin precision");gate.Release();Check(!gate.sending.get()&&gate.closing.get(),"close versus prepared submit");
                    } else {
                        Check(!Send(*p.as.front(),p.ar,220),"concurrent first setup entered twice");gate.Release();Check(gate.sending.get(),"first setup failed");
                        Check(p.a->PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"deadline actual pump");p.b->PumpReceive();Check(p.rb.Exact(1,221),"deadline exact RX");
                        const auto f=public_socket_fake::Observe();Check(f.sockets[0]==1&&f.binds[0]==1,"concurrent setup duplicated native");
                        bool timeout=false;for(unsigned i=0;i<f.bind_qos[0].count;++i)if(f.bind_qos[0].type[i]==1){timeout=true;Check(f.bind_qos[0].value[i]>0&&f.bind_qos[0].value[i]<=2000,"remaining per-call QoS");}Check(timeout,"timed Bind QoS missing");
                    }
                    p.Finish();
                } else if(name=="failed-first-TX-RX-survives") {
                    p.AInput();public_socket_fake::FailNext(public_socket_fake::InjectedOperation::Bind,-709);Check(!Send(*p.bs.front(),p.br,214),"intentional TX failure accepted");
                    const auto f=public_socket_fake::Observe();p.Deliver(false,215,1);Check(!Send(*p.bs.front(),p.br,216),"sticky failed TX implicitly recovered");
                    const auto after=public_socket_fake::Observe();Check(after.sockets[1]==f.sockets[1]&&after.binds[1]==f.binds[1],"sticky TX redialed");p.Finish(true);
                } else {
                    Check(NeverOpen(*p.a,*p.trace),"A hidden input/anchor");p.Deliver(false,217,1);p.AInput();p.Deliver(true,218,1);
                    if(name=="client-shutdown-RX-survives") {
                        public_socket_fake::DisconnectDirection(1,true);p.Deliver(false,219,2);const auto f=public_socket_fake::Observe();Check(!Send(*p.bs.front(),p.br,220),"stopped TX accepted");
                        Check(public_socket_fake::Observe().sockets[1]==f.sockets[1],"stopped TX resurrected");
                    } else if(name=="listener-stop") {
                        public_socket_fake::StopListener(1);std::string encoded;Check(phase0::EncodeDomainCircuitName(Configuration(0).identity.service_name,151,encoded),"candidate name");
                        Check(!public_socket_fake::ProbeCandidate(1,990,encoded,Configuration(1).peer_network_id,Configuration(1).package_name,DATA_TYPE_BYTES),"late candidate after listener stop admitted");
                    } else Check(name=="generic-DID-first"||name=="explicit-input-first"||name=="explicit-DID-first"||name=="both-cached-before-DID","unknown contract case");
                    p.Finish();
                }
            }
        }
        std::cout<<"PASS contract "<<name<<" SDK_ABI=UNKNOWN DEVICE_TESTED=0 SDK_HARD_DEADLINE=UNKNOWN\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL contract "<<name<<" "<<e.what()<<'\n';return 1;}
}
}
