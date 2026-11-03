// Real stock public Factory domains; explicit Fake Socket C API, no upper patch.
#include <cstdlib>
#include <new>
#include <future>
namespace {
thread_local long dg_fail=-1,dg_count=0,dg_guard_index=-1;
thread_local std::size_t dg_fail_size=0,dg_guard_size=0;
thread_local bool dg_failed=false,dg_counting=false;
}
void* operator new(std::size_t n){
    if(dg_counting){if(n==dg_guard_size)dg_guard_index=dg_count;++dg_count;}
    if(dg_fail==0||(dg_fail_size&&n==dg_fail_size)){
        dg_fail=-1;dg_fail_size=0;dg_failed=true;throw std::bad_alloc();
    }
    if(dg_fail>0)--dg_fail;
    if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#define main DgPriorScopedMain
#include "runtime_views_tests.cpp"
#undef main
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>

namespace {
using DgTransport=eprosima::fastdds::rtps::TransportInterface;
bool DomainSend(fastdds_dsoftbus::binding::SenderResource& sender,const Locator& remote,std::uint8_t marker=42){
    std::vector<Locator> targets{remote};eprosima::fastdds::rtps::Locators first(targets.begin()),last(targets.end());
    const std::uint8_t bytes[]{marker,19,83,7};
    return sender.send(bytes,sizeof(bytes),&first,&last,std::chrono::steady_clock::now()+2s);
}
struct DomainReceiver final:eprosima::fastdds::rtps::TransportReceiverInterface {
    unsigned calls=0;std::uint8_t marker=0;
    void OnDataReceived(const std::uint8_t* bytes,std::uint32_t size,const Locator&,const Locator&)override{
        T2Check(size==4&&bytes[1]==19&&bytes[2]==83&&bytes[3]==7,"raw exact payload");++calls;marker=bytes[0];
    }
};
struct DgDeployment {
    std::array<phase0::PublicSocketConfiguration,2> sockets;
    std::array<DSoftBusTransportDescriptor,2> descriptors{{DSoftBusTransportDescriptor(8192),DSoftBusTransportDescriptor(8192)}};
    explicit DgDeployment(unsigned domain){
        const std::string ns="stock-default-domain-"+std::to_string(domain);
        std::array<v1::DeviceIdentity,2> identities{{{ns,domain,"A"},{ns,domain,"B"}}};
        std::array<std::string,2> listeners;
        for(unsigned side=0;side<2;++side)
            T2Check(v1::AddressCodec::EncodeScopedServiceName({ns,domain,17+side,v1::Profile::BytesNative},
                {v1::NativeNameContract::PackageScopedV1,"host.public.dds","host.public.dds.v1"},listeners[side]),"selected-domain service");
        for(unsigned side=0;side<2;++side){
            sockets[side]=T2Configuration(side,0);auto& c=sockets[side];
            c.listener_identity.service_name=listeners[side];c.peer_name=listeners[1-side];
            V1Deployment value{identities[side],17+side,v1::Profile::BytesNative,"stock.default.domain",
                {{identities[1-side],18-side,c.peer_network_id,c.identity.service_name,{c.expected_peer_initiator_name}}}};
            T2Check(descriptors[side].ConfigureV1(ScopedHostDeployment(value)).code==DirectoryError::None,"selected primitive deployment");
        }
    }
};
// Observation only: original owned Transport pointer is returned unchanged.
struct DgCaptured final:eprosima::fastdds::rtps::TransportDescriptorInterface {
    std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> original;
    mutable std::array<DSoftBusTransport*,4> views{};mutable unsigned count=0;
    explicit DgCaptured(std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> d)
        :TransportDescriptorInterface(d->maxMessageSize,1),original(std::move(d)){}
    DgTransport* create_transport()const override{
        auto* p=original->create_transport();if(p&&count<views.size())views[count++]=dynamic_cast<DSoftBusTransport*>(p);return p;
    }
    std::uint32_t min_send_buffer_size()const override{return original->min_send_buffer_size();}
};
// Selected-domain compatibility fixture intentionally chooses the real NativeBound API.
struct DgPair {
    DgDeployment config;dds::TypeSupport type{new T2SampleType};
    std::array<std::unique_ptr<experimental::Runtime>,2> runtime;
    std::array<std::shared_ptr<DgCaptured>,2> descriptors;
    std::array<std::unique_ptr<T2Worker>,2> workers;
    std::array<T2Endpoint,2> apps;
    std::mutex gate;std::condition_variable changed;unsigned arrived=0;bool finished=false;
    explicit DgPair(unsigned native_domain):config(native_domain){
        public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
        public_socket_fake::SetListenerNames({config.sockets[0].listener_identity.service_name,config.sockets[1].listener_identity.service_name});
        public_socket_fake::SetInitiatorNames({config.sockets[0].identity.service_name,config.sockets[1].identity.service_name});
        const char xml[]="<dds xmlns=\"http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles\"><library_settings><intraprocess_delivery>OFF</intraprocess_delivery></library_settings></dds>";
        T2Check(dds::DomainParticipantFactory::get_instance()->load_XML_profiles_string(xml,sizeof(xml)-1)==0,"intraprocess OFF");
        for(unsigned side=0;side<2;++side){
            runtime[side]=std::make_unique<experimental::Runtime>(config.sockets[side]);
            T2Check(runtime[side]->SetPreConnectGate([this]{std::unique_lock<std::mutex> lock(gate);++arrived;changed.notify_all();return changed.wait_for(lock,2s,[&]{return arrived==2;});}),"selected-domain barrier");
            auto d=runtime[side]->MakeStockDescriptor(config.descriptors[side]);T2Check(bool(d),"NativeBound selected-domain descriptor");
            descriptors[side]=std::make_shared<DgCaptured>(d);workers[side]=std::make_unique<T2Worker>(*runtime[side]);
        }
    }
    void Create(unsigned side,unsigned actual,int peer_mode){
        const auto& d=config.descriptors[side];auto& a=apps[side];
        a.received=std::make_unique<T2Reader>();a.sent=std::make_unique<T2Writer>();dds::DomainParticipantQos q;
        q.transport().use_builtin_transports=false;q.transport().user_transports={descriptors[side]};
        q.properties().properties().emplace_back("fastdds.ignore_local_endpoints","true");
        auto& w=q.wire_protocol();w.builtin.metatrafficMulticastLocatorList.clear();
        w.builtin.metatrafficUnicastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.push_back(d.directory.locals.front().local_locator);
        w.default_unicast_locator_list.clear();w.default_unicast_locator_list.push_back(d.directory.locals.front().local_locator);w.default_multicast_locator_list.clear();
        w.builtin.initialPeersList.clear();
        if(peer_mode==0)w.builtin.initialPeersList.push_back(d.directory.routes.front().key.remote_locator);
        else if(peer_mode==2){Locator p;p.kind=LOCATOR_KIND_UDPv4;p.port=12000;p.address[12]=127;p.address[15]=1;w.builtin.initialPeersList.push_back(p);}
        w.builtin.discovery_config.initial_announcements.count=1;
        a.participant=dds::DomainParticipantFactory::get_instance()->create_participant(actual,q);
        T2Check(a.participant,"original Factory result (no return mapping)");T2Check(type.register_type(a.participant)==0,"type registration");
        auto* topic=a.participant->create_topic("DefaultDomainTopic",type.get_type_name(),dds::TOPIC_QOS_DEFAULT);
        auto* pub=a.participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);auto* sub=a.participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
        T2Check(topic&&pub&&sub,"selected-domain entities");auto wq=dds::DATAWRITER_QOS_DEFAULT;auto rq=dds::DATAREADER_QOS_DEFAULT;
        wq.reliability().kind=rq.reliability().kind=dds::RELIABLE_RELIABILITY_QOS;wq.data_sharing().off();rq.data_sharing().off();
        a.writer=pub->create_datawriter(topic,wq,a.sent.get());a.reader=sub->create_datareader(topic,rq,a.received.get());T2Check(a.writer&&a.reader,"selected-domain endpoints");
    }
    void Pair(unsigned a,unsigned b,int peer_mode=0){
        std::exception_ptr errors[2];std::thread threads[2];
        for(unsigned side=0;side<2;++side)threads[side]=std::thread([&,side]{try{Create(side,side?b:a,side?0:peer_mode);}catch(...){errors[side]=std::current_exception();}});
        for(auto& t:threads)t.join();for(auto e:errors)if(e)std::rethrow_exception(e);
    }
    void Finish(){
        for(auto& a:apps)if(a.participant){T2Check(a.participant->delete_contained_entities()==0,"selected entities close");T2Check(dds::DomainParticipantFactory::get_instance()->delete_participant(a.participant)==0,"selected Factory close");a.participant=nullptr;}
        for(auto& r:runtime)T2Check(r->Close(),"selected runtime close");for(auto& w:workers)w->Join();
        const auto s=public_socket_fake::Observe();T2Check(!s.live&&!s.queued&&!s.bytes&&!s.invalid,"selected Fake residue");finished=true;
    }
    ~DgPair(){if(!finished)try{Finish();}catch(...){}}
};
void DgFactory(unsigned native_domain,bool mismatch,int peer_mode=0){
    DgPair f(native_domain);f.Pair(native_domain,mismatch?native_domain+1:native_domain,peer_mode);
    auto* a=f.descriptors[0]->views[0];auto* b=f.descriptors[1]->views[0];T2Check(a&&b,"original transports captured");
    const auto sa=a->domain_snapshot(),sb=b->domain_snapshot();
    T2Check(sa.configured&&sb.configured&&sa.expected_domain_id==native_domain&&sb.expected_domain_id==native_domain,"default primitive not frozen");
    if(mismatch||peer_mode){
        T2Check((peer_mode?sa.state==ViewDomainState::Await:sa.state==ViewDomainState::Ready)&&
            sb.state==(mismatch?ViewDomainState::Refused:ViewDomainState::Ready),"actual stock callback state");
        for(auto& app:f.apps)app.received->WaitNone();
        auto* denied=mismatch?b:a;const unsigned side=mismatch?1:0;
        const auto remote=f.config.descriptors[side].directory.routes.front().key.remote_locator;
        eprosima::fastdds::rtps::SendResourceList senders;T2Check(denied->OpenOutputChannel(senders,remote),"denied real sender");
        const auto before=public_socket_fake::Observe();T2Check(!DomainSend(*senders.front(),remote),"default denied sender admitted bytes");
        T2Check(denied->domain_snapshot().denied[0]>0,"no actual submission denial");
        T2Sample sample;const auto rc=f.apps[side].writer->write(&sample,dds::HANDLE_NIL);for(auto& app:f.apps)app.received->WaitNone();
        const auto after=public_socket_fake::Observe();T2Check(after.forwards[side]==before.forwards[side],"refused/Await view entered native provider");
        std::cout<<"DEFAULT_DOMAIN_NEGATIVE native="<<native_domain<<" Factory_nonnull=2 actual_B="<<(mismatch?native_domain+1:native_domain)
            <<" peer_mode="<<peer_mode<<" actual_sender_denied=1 upper_matches=0 upper_takes=0 unmatched_write_rc="<<int(rc())<<" missing_callback_terminal=NOT_INFERRED\n";
    }else{
        T2Check(sa.state==ViewDomainState::Ready&&sb.state==ViewDomainState::Ready,"same-domain default not Ready");
        for(auto& app:f.apps){app.received->WaitMatched();app.sent->WaitMatched();}
        for(unsigned side=0;side<2;++side){T2Sample sample;sample.cycle=native_domain;sample.source=side;sample.ordinal=side+41;sample.payload.fill(std::uint8_t(native_domain+side));
            T2Check(f.apps[side].writer->write(&sample,dds::HANDLE_NIL)==0,"selected-domain write");f.apps[1-side].received->WaitData(sample,f.apps[side].writer->guid());
            T2Check(f.apps[side].writer->wait_for_acknowledgments({1,0})==0,"selected-domain acknowledgment");}
        std::cout<<"DEFAULT_DOMAIN_POSITIVE native="<<native_domain<<" actual=CONFIGURED writes=2 takes=2 exact_GUID_payload=1 other_transport=OFF intraprocess=OFF DataSharing=OFF\n";
    }
    f.Finish();
}
struct DgObserver final:ProcessReceiveObserver {
    void OnReceive(ReceiveStage,const ReceiveDiagnostic&,const std::uint8_t*,std::size_t)noexcept override{}
    void OnDecision(const ReceiveDecision&)noexcept override{}
};
void DgAssembly(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);DgDeployment zero(0);
    unsigned modes=2;
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
    modes=3;
#endif
    for(unsigned mode=0;mode<modes;++mode){
        std::unique_ptr<experimental::Runtime> owner;
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
        if(mode==2)owner=std::make_unique<experimental::Runtime>(zero.sockets[0],std::make_shared<DgObserver>());
        else
#endif
        if(mode==0)owner=std::make_unique<experimental::Runtime>(zero.sockets[0]);
        else owner=std::make_unique<experimental::Runtime>(zero.sockets[0],experimental::RuntimeLifetime::PersistentNativeAnchor);
        auto& r=*owner;
        auto bad=zero.descriptors[0];bad.expected_domain_id=1;T2Check(!r.MakeDescriptor(bad),"explicit contradictory expected accepted");
        auto d=r.MakeDescriptor(zero.descriptors[0]);T2Check(bool(d),"zero domain generic descriptor");
        std::unique_ptr<DgTransport> a(d->create_transport()),b(d->create_transport());T2Check(a&&b&&!d->create_transport(),"two-view capacity changed");
        for(auto* p:{a.get(),b.get()}){auto* t=dynamic_cast<DSoftBusTransport*>(p);const auto s=t->domain_snapshot();T2Check(s.configured&&s.expected_domain_id==0&&s.state==ViewDomainState::Await,"domain0 optional/default guard");}
        a.reset();b.reset();T2Check(r.Close(),"zero-domain metadata close");
    }
    const auto s=public_socket_fake::Observe();T2Check(!s.live&&!s.sockets[0]&&!s.sockets[1],"metadata assembly opened SDK");
    std::cout<<"DEFAULT_ASSEMBLY normal=GUARDED persistent=GUARDED domain0=ENGAGED mismatch_rejected_before_issuance=1 SDK_calls=0\n";
}
void DgAllocation(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);experimental::Runtime r(T2Configuration(0,0));const auto config=T2Descriptor(0,0);
    dg_fail=0;dg_failed=false;try{(void)r.MakeDescriptor(config);}catch(const std::bad_alloc&){}dg_fail=-1;
    T2Check(dg_failed&&!phase0::RuntimeProbeAccess::ObserveRuntime(r).started,"first canonical allocation consumed metadata seed");
    auto d=r.MakeDescriptor(config);T2Check(bool(d),"descriptor after canonical failure");
    dg_guard_size=sizeof(ViewDomainGuard);dg_count=0;dg_guard_index=-1;dg_counting=true;dg_fail_size=dg_guard_size;dg_failed=false;
    std::unique_ptr<DgTransport> guard_failure(d->create_transport());dg_counting=false;dg_fail_size=0;
    // Size identifies a candidate, not a unique C++ type. The exhaustive sweep
    // below reaches every allocation before the first complete guarded facade.
    T2Check(dg_failed&&!guard_failure&&dg_guard_index>=0,"guard-sized allocation not failed");const auto guard_index=dg_guard_index;
    unsigned failures=0;long succeeded=-1;
    for(long index=0;index<512;++index){
        dg_failed=false;dg_fail=index;std::unique_ptr<DgTransport> view(d->create_transport());dg_fail=-1;
        if(!dg_failed){T2Check(bool(view),"failure-free facade missing");succeeded=index;view.reset();break;}
        ++failures;T2Check(!view,"partial allocation escaped");const auto s=phase0::RuntimeProbeAccess::ObserveRuntime(r);
        T2Check(s.started&&!s.closed&&!s.live_views&&!s.receive.active_applications,"unpublished guarded view not refunded");
    }
    T2Check(succeeded>guard_index&&failures>24,"allocation sweep did not reach new guard and complete suffix");
    T2Check(r.Close()&&!d->create_transport(),"successful final borrowed guard did not close lifetime");const auto s=public_socket_fake::Observe();
    T2Check(!s.live&&!s.sockets[0]&&!s.sockets[1],"unused seed allocation test opened SDK");
    std::cout<<"DEFAULT_GUARD_ALLOCATION guard_sized_candidate="<<dg_guard_size<<" candidate_index="<<guard_index<<" every_failed_position="<<failures
        <<" first_success_index="<<succeeded<<" seed_live_views=0 SDK_calls=0 native_rebuild=NOT_TESTED\n";
}
void DgRaw(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
    public_socket_fake::SetListenerNames({T2Listener(0,0),T2Listener(1,0)});
    public_socket_fake::SetInitiatorNames({T2Configuration(0,0).identity.service_name,T2Configuration(1,0).identity.service_name});
    experimental::Runtime a(T2Configuration(0,0)),b(T2Configuration(1,0));
    std::mutex gate;std::condition_variable changed;unsigned arrived=0;
    auto barrier=[&]{std::unique_lock<std::mutex> lock(gate);++arrived;changed.notify_all();return changed.wait_for(lock,2s,[&]{return arrived==2;});};
    T2Check(a.SetPreConnectGate(barrier)&&b.SetPreConnectGate(barrier),"raw normal preconnect");
    auto ca=T2Descriptor(0,0),cb=T2Descriptor(1,0);auto da=a.MakeDescriptor(ca),db=b.MakeDescriptor(cb);T2Check(da&&db,"raw generic descriptors");
    std::unique_ptr<DgTransport> ta(da->create_transport()),keep(da->create_transport()),tb(db->create_transport());
    T2Check(ta&&keep&&tb,"normal raw two-view bound");DomainReceiver ra,rb,replacement;
    std::exception_ptr errors[2];std::thread other([&]{try{T2Check(tb->init()&&tb->OpenInputChannel(cb.directory.locals.front().local_locator,&rb,8192),"raw B input");}catch(...){errors[1]=std::current_exception();}});
    try{T2Check(ta->init()&&ta->OpenInputChannel(ca.directory.locals.front().local_locator,&ra,8192),"raw A input");}catch(...){errors[0]=std::current_exception();}
    other.join();for(auto e:errors)if(e)std::rethrow_exception(e);
    auto ar=ca.directory.routes.front().key.remote_locator,br=cb.directory.routes.front().key.remote_locator;
    eprosima::fastdds::rtps::SendResourceList as,bs;T2Check(ta->OpenOutputChannel(as,ar)&&tb->OpenOutputChannel(bs,br),"raw real senders");
    auto* av=dynamic_cast<DSoftBusTransport*>(ta.get());T2Check(av,"raw concrete A");
    T2Check(!DomainSend(*as.front(),ar)&&!DomainSend(*bs.front(),br),"generic raw Await bypass");
    eprosima::fastdds::rtps::LocatorList peers;fastdds_dsoftbus::binding::PortParameters ports;Locator unsupported=ar;unsupported.kind=LOCATOR_KIND_UDPv4;
    T2Check(!ta->configureInitialPeerLocator(unsupported,ports,151,peers)&&av->domain_snapshot().state==ViewDomainState::Await,"unsupported peer manufactured Ready");
    // Actual selected domain comes from this controlled raw driver, never from
    // a helper reading expected. Real Factory cases above receive no manual hook.
    T2Check(ta->configureInitialPeerLocator(ar,ports,151,peers)&&tb->configureInitialPeerLocator(br,ports,151,peers),"raw actual151 confirmation");
    T2Check(DomainSend(*as.front(),ar,60),"Ready queue submission");const auto native=public_socket_fake::Observe();
    T2Check(!ta->configureInitialPeerLocator(ar,ports,152,peers)&&!ta->configureInitialPeerLocator(ar,ports,151,peers),"mismatch not sticky");
    const auto denied=a.PumpSend();T2Check(denied.outcome==NativeSendOutcome::NotEntered&&denied.domain_denial==DomainAdmission::Refused,
        "queued default send entered provider after refusal");
    T2Check(public_socket_fake::Observe().forwards[0]==native.forwards[0],"refused queued provider called C API");
    auto wire=[&](unsigned marker){T2Check(DomainSend(*bs.front(),br,std::uint8_t(marker)),"raw B positive sender");
        T2Check(b.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"raw actual native forwarding");};
    wire(71);a.PumpReceive();T2Check(!ra.calls&&av->domain_snapshot().denied[2]>0,"default Refused delivered bytes");
    wire(72); // Real admission targets the original lease; leave it queued.
    ta.reset();T2Check(!DomainSend(*as.front(),ar),"cached old sender reopened");
    ta.reset(da->create_transport());av=dynamic_cast<DSoftBusTransport*>(ta.get());T2Check(av&&av->domain_snapshot().state==ViewDomainState::Await,"replacement guard inherited state");
    T2Check(ta->init()&&ta->OpenInputChannel(ca.directory.locals.front().local_locator,&replacement,8192),"normal replacement input");
    a.PumpReceive();T2Check(!replacement.calls&&!ra.calls,"queued original lease retargeted replacement");
    wire(73);a.PumpReceive();
    const auto raw_before_assert=av->domain_snapshot();
    std::cout<<"OLD_RAW_COMPOUND_DIAGNOSTIC replacement_calls="<<replacement.calls
        <<" guard_denied_rx="<<raw_before_assert.denied[2]<<" guard_state="<<static_cast<unsigned>(raw_before_assert.state)
        <<" receive_active_applications="<<phase0::RuntimeProbeAccess::ObserveRuntime(a).receive.active_applications<<std::endl;
    T2Check(!replacement.calls&&raw_before_assert.state==ViewDomainState::Await&&
        phase0::RuntimeProbeAccess::ObserveRuntime(a).receive.active_applications==0,"fresh Await attached or delivered bytes");
    eprosima::fastdds::rtps::SendResourceList fresh_as;
    T2Check(ta->OpenOutputChannel(fresh_as,ar)&&!fresh_as.empty(),"fresh Await real sender resource");
    const auto await_native=public_socket_fake::Observe();
    T2Check(!DomainSend(*as.front(),ar)&&!DomainSend(*fresh_as.front(),ar),"old/fresh Await sender admitted bytes");
    T2Check(av->domain_snapshot().denied[0]>0,"fresh Await sender did not consult guard");
    T2Check(a.PumpSend().outcome==NativeSendOutcome::NotEntered,"Await sender entered provider work");
    const auto await_after=public_socket_fake::Observe();
    T2Check(await_after.sends[0]==await_native.sends[0]&&await_after.forwards[0]==await_native.forwards[0],"Await/old sender entered native provider");
    T2Check(ta->configureInitialPeerLocator(ar,ports,151,peers),"fresh raw actual-domain confirmation");
    wire(74);a.PumpReceive();T2Check(replacement.calls==1&&replacement.marker==74&&!ra.calls,"fresh Ready payload/old lease isolation");
    const auto after=public_socket_fake::Observe();for(unsigned side=0;side<2;++side)T2Check(after.sockets[side]==native.sockets[side]&&after.shutdowns[side]==native.shutdowns[side],"same-incarnation replacement rebuilt native");
    fresh_as.clear();
    ta.reset();keep.reset();tb.reset();as.clear();bs.clear();T2Check(a.Close()&&b.Close(),"normal raw final close");
    T2Check(!da->create_transport(),"normal last close reopened");const auto end=public_socket_fake::Observe();T2Check(!end.live&&!end.queued&&!end.bytes&&!end.invalid,"raw final residue");
    std::cout<<"DEFAULT_RAW Await_TX=DENIED unsupported=AWAIT late_actual=READY mismatch=STICKY queued_provider=NOT_ENTERED Refused_RX=DENIED replacement=FRESH old_lease=NOT_RETARGETED same_native_incarnation=1 SDK_drain=UNKNOWN\n";
}
void DgStartupFailure(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);DgDeployment zero(0);
    public_socket_fake::SetListenerNames({zero.sockets[0].listener_identity.service_name,zero.sockets[1].listener_identity.service_name});
    public_socket_fake::SetInitiatorNames({zero.sockets[0].identity.service_name,zero.sockets[1].identity.service_name});
    experimental::Runtime owner(zero.sockets[0]);
    T2Check(owner.SetPreConnectGate([]{public_socket_fake::FailNext(public_socket_fake::InjectedOperation::Socket,-7654);return true;}),"warm failure injection gate");
    auto descriptor=owner.MakeStockDescriptor(zero.descriptors[0]);T2Check(bool(descriptor),"explicit zero NativeBound descriptor");
    std::unique_ptr<DgTransport> transport(descriptor->create_transport());auto* view=dynamic_cast<DSoftBusTransport*>(transport.get());
    T2Check(view&&transport->init(),"warm failure facade start");DomainReceiver receiver;const auto local=zero.descriptors[0].directory.locals.front().local_locator;
    T2Check(!transport->OpenInputChannel(local,&receiver,8192),"negative circuit Socket accepted input");
    const auto first=public_socket_fake::Observe();const auto awaiting=view->domain_snapshot();
    const auto startup_before_assert=phase0::RuntimeProbeAccess::ObserveRuntime(owner);
    std::cout<<"NATIVE_BOUND_FAILURE_DIAGNOSTIC sockets_a="<<first.sockets[0]<<" sockets_b="<<first.sockets[1]
        <<" listens_a="<<first.listens[0]<<" listens_b="<<first.listens[1]<<" binds_a="<<first.binds[0]<<" binds_b="<<first.binds[1]
        <<" live="<<first.live<<" expected_known="<<awaiting.expected_domain_known<<" expected="<<awaiting.expected_domain_id
        <<" guard_state="<<static_cast<unsigned>(awaiting.state)<<" active_applications="<<startup_before_assert.receive.active_applications
        <<" view_controls="<<startup_before_assert.view_controls<<std::endl;
    const auto native_first=owner.ObserveSocket();
    const auto socket_index=static_cast<std::size_t>(phase0::PublicSocketOperation::Socket);
    const auto& socket_first=native_first.operations[socket_index];
    std::cout<<"NATIVE_BOUND_SOCKET_CALL_DIAGNOSTIC calls="<<socket_first.calls<<" returns="<<socket_first.returns
        <<" domain_circuits="<<native_first.domain_circuits<<" error_known="<<bool(native_first.first_api_error)
        <<" error_operation="<<(native_first.first_api_error?static_cast<int>(native_first.first_api_error->operation):-1)
        <<" error_code="<<(native_first.first_api_error?native_first.first_api_error->code:0)<<std::endl;
    T2Check(first.sockets[0]==1&&first.listens[0]==1&&first.binds[0]==0&&first.live==1,"successful listener handle count after failed outgoing Socket");
    T2Check(socket_first.calls==2&&socket_first.returns==2&&native_first.domain_circuits==1&&!native_first.outgoing_socket&&
        native_first.first_api_error&&native_first.first_api_error->operation==phase0::PublicSocketOperation::Socket&&
        native_first.first_api_error->code==-7654,"configured outgoing Socket failure not observed at real API boundary");
    T2Check(awaiting.expected_domain_known&&awaiting.expected_domain_id==0&&awaiting.state==ViewDomainState::Await,"preparation confirmed zero domain");
    T2Check(!transport->OpenInputChannel(local,&receiver,8192),"failed warm retried or published input");
    const auto retried=public_socket_fake::Observe();const auto runtime=phase0::RuntimeProbeAccess::ObserveRuntime(owner);
    const auto native_retried=owner.ObserveSocket();const auto& socket_retried=native_retried.operations[socket_index];
    std::cout<<"NATIVE_BOUND_SOCKET_RETRY_DIAGNOSTIC calls="<<socket_retried.calls<<" returns="<<socket_retried.returns
        <<" successful_sockets_a="<<retried.sockets[0]<<" successful_sockets_b="<<retried.sockets[1]
        <<" live="<<retried.live<<" active_applications="<<runtime.receive.active_applications<<" view_controls="<<runtime.view_controls<<std::endl;
    T2Check(retried.sockets[0]==first.sockets[0]&&retried.sockets[1]==first.sockets[1]&&
        retried.listens[0]==first.listens[0]&&retried.listens[1]==first.listens[1]&&
        retried.binds[0]==first.binds[0]&&retried.binds[1]==first.binds[1]&&retried.live==first.live&&
        socket_retried.calls==socket_first.calls&&socket_retried.returns==socket_first.returns&&
        runtime.receive.active_applications==0&&runtime.view_controls==0,"failed warm repeated real Socket entry or leaked handle/app/control");
    transport.reset();const bool closed=owner.Close();const auto end=public_socket_fake::Observe();
    T2Check(!end.live&&!end.queued&&!end.bytes&&!end.invalid&&end.shutdowns[0]==1,"failed warm lost/double-closed positive listener ID");
    const auto failure_snapshot=phase0::RuntimeProbeAccess::ObserveDomainCircuits(owner);
    const host_test::DomainCircuitObservation* failed_domain=nullptr;
    for(const auto& entry:failure_snapshot.entries)if(entry)failed_domain=&*entry;
    std::cout<<"FAILED_CLOSE_CIRCUIT_SNAPSHOT runtime_close="<<closed<<" status="<<static_cast<int>(failure_snapshot.status)
        <<" root_closed="<<failure_snapshot.root_closed<<" terminal_adapters_available="<<failure_snapshot.terminal_adapters_available
        <<" count="<<failure_snapshot.count<<" entry_present="<<bool(failed_domain)
        <<" domain="<<(failed_domain?std::to_string(failed_domain->domain):"UNAVAILABLE")
        <<" native_available="<<(failed_domain&&bool(failed_domain->native))
        <<" adapter_available="<<(failed_domain&&bool(failed_domain->adapter))<<std::endl;
    if(failed_domain&&failed_domain->native){const auto& actual_native=*failed_domain->native;
        std::cout<<"FAILED_CLOSE_CIRCUIT_NATIVE owners="<<actual_native.owners<<" retained_ids="<<actual_native.owned_ids
            <<" retained_not_quiescent="<<actual_native.phases[static_cast<std::size_t>(NativeOwnerPhase::RetainedNotQuiescent)]
            <<" failed_retained="<<actual_native.phases[static_cast<std::size_t>(NativeOwnerPhase::FailedRetained)]
            <<" callbacks="<<actual_native.callbacks<<" receives="<<actual_native.receives<<std::endl;
    }
    T2Check(failure_snapshot.status==host_test::DomainCircuitObservationStatus::Available&&failure_snapshot.count==1&&
        failed_domain&&failed_domain->domain==0&&failed_domain->native,"failed close lost reserved zero-domain native metadata");
    T2Check(!failure_snapshot.terminal_adapters_available&&!failed_domain->adapter,
        "failed close exposed a terminal adapter snapshot");
    std::cout<<"NATIVE_BOUND_STARTUP_FAILURE explicit_zero=1 Socket_negative=-7654 input=REFUSED retry_sdk=0 app_attachments=0 positive_listener_cleanup=1 runtime_close="<<closed<<" SDK_drain=UNKNOWN\n";
}
void DgStartupClose(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);DgDeployment config(151);
    public_socket_fake::SetListenerNames({config.sockets[0].listener_identity.service_name,config.sockets[1].listener_identity.service_name});
    public_socket_fake::SetInitiatorNames({config.sockets[0].identity.service_name,config.sockets[1].identity.service_name});
    experimental::Runtime owner(config.sockets[0]);std::mutex gate;std::condition_variable changed;bool entered=false,released=false;
    T2Check(owner.SetPreConnectGate([&]{std::unique_lock<std::mutex> lock(gate);entered=true;changed.notify_all();return changed.wait_for(lock,2s,[&]{return released;});}),"startup close control gate");
    auto descriptor=owner.MakeStockDescriptor(config.descriptors[0]);T2Check(bool(descriptor),"startup close NativeBound descriptor");
    std::unique_ptr<DgTransport> transport(descriptor->create_transport());auto* view=dynamic_cast<DSoftBusTransport*>(transport.get());
    T2Check(view&&transport->init(),"startup close facade start");DomainReceiver receiver;
    auto opening=std::async(std::launch::async,[&]{return transport->OpenInputChannel(config.descriptors[0].directory.locals.front().local_locator,&receiver,8192);});
    {std::unique_lock<std::mutex> lock(gate);T2Check(changed.wait_for(lock,2s,[&]{return entered;}),"listener setup not entered");}
    const auto ready=public_socket_fake::Observe();T2Check(ready.live==1&&ready.listens[0]==1&&ready.binds[0]==0,"close control was not after listener readiness");
    auto closing=std::async(std::launch::async,[&]{transport->shutdown();});
    const auto close_deadline=std::chrono::steady_clock::now()+2s;
    while(view->domain_snapshot().state!=ViewDomainState::Closed&&std::chrono::steady_clock::now()<close_deadline)std::this_thread::yield();
    const bool close_entered=view->domain_snapshot().state==ViewDomainState::Closed;
    std::cout<<"NATIVE_BOUND_STARTUP_CLOSE_ENTRY public_guard_closed="<<close_entered<<std::endl;
    T2Check(close_entered,"shutdown did not enter the public guard close");
    T2Check(closing.wait_for(50ms)==std::future_status::timeout,"shutdown did not wait for caller Open control");
    {std::lock_guard<std::mutex> lock(gate);released=true;changed.notify_all();}
    T2Check(opening.wait_for(2s)==std::future_status::ready&&closing.wait_for(2s)==std::future_status::ready,"startup close bounded completion");
    T2Check(!opening.get(),"closing input published application");closing.get();
    const auto runtime=phase0::RuntimeProbeAccess::ObserveRuntime(owner);const auto end=public_socket_fake::Observe();
    T2Check(view->domain_snapshot().state==ViewDomainState::Closed&&runtime.receive.active_applications==0&&runtime.view_controls==0,"startup close retained app/control or Ready guard");
    T2Check(end.sockets[0]==1&&end.binds[0]==0&&end.shutdowns[0]==1&&!end.live&&!end.queued&&!end.bytes&&!end.invalid,"startup close created outgoing or lost listener owner");
    transport.reset();T2Check(owner.Close(),"startup close Runtime completion");T2Check(!descriptor->create_transport(),"closed startup reopened native incarnation");
    std::cout<<"NATIVE_BOUND_STARTUP_CLOSE caller_control_wait=1 application_attachments=0 outgoing_after_close=0 listener_cleanup=1 SDK_drain=UNKNOWN\n";
}
}
int main(int argc,char** argv){try{
    T2LoadedDll();T2Check(argc==2,"one default-domain case");const std::string c=argv[1];
    if(c=="assembly")DgAssembly();else if(c=="allocation")DgAllocation();else if(c=="raw")DgRaw();
    else if(c=="mismatch151")DgFactory(151,true);else if(c=="positive152")DgFactory(152,false);
    else if(c=="startup-failure")DgStartupFailure();else if(c=="startup-close")DgStartupClose();
    else if(c=="mismatch152")DgFactory(152,true);else if(c=="empty-peers")DgFactory(151,false,1);
    else if(c=="unsupported-peer")DgFactory(151,false,2);else throw std::runtime_error("unknown default-domain case");
    std::cout<<"PASS default_domain "<<c<<" HOST_FAKE_SOCKET_ONLY DEVICE_TESTED=0\n";return 0;
}catch(const std::exception& e){dg_fail=-1;dg_fail_size=0;dg_counting=false;std::cerr<<"FAIL default_domain "<<e.what()<<'\n';return 1;}}
