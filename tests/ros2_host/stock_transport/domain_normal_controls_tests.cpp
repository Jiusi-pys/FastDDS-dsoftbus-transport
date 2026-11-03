// Additive coverage only. The original domain/default-domain oracles stay intact.
// Every DDS case uses Runtime(config), real public Factory calls and the original
// descriptor/owned Transport pointer. No Persistent mode or Factory wrapper.
#define main NormalControlPriorViewsMain
#include "runtime_views_tests.cpp"
#undef main
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>

namespace {
enum class NPolicy { Generic, NativeBound };
struct NormalControlFixture {
    std::array<std::unique_ptr<experimental::Runtime>,2> runtimes;
    std::array<std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>,2> descriptors;
    std::array<std::unique_ptr<T2Worker>,2> workers;
    std::array<std::array<T2Endpoint,2>,2> apps;
    dds::TypeSupport type{new T2SampleType};
    std::mutex gate;std::condition_variable changed;unsigned arrived=0;bool finished=false;
    explicit NormalControlFixture(NPolicy policy){
        public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
        public_socket_fake::SetListenerNames({T2Listener(0,0),T2Listener(1,0)});
        public_socket_fake::SetInitiatorNames({T2Configuration(0,0).identity.service_name,T2Configuration(1,0).identity.service_name});
        public_socket_fake::SetFrameDetail(false);
        const char xml[]="<dds xmlns=\"http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles\"><library_settings><intraprocess_delivery>OFF</intraprocess_delivery></library_settings></dds>";
        T2Check(dds::DomainParticipantFactory::get_instance()->load_XML_profiles_string(xml,sizeof(xml)-1)==0,"normal intraprocess OFF");
        for(unsigned side=0;side<2;++side){
            runtimes[side]=std::make_unique<experimental::Runtime>(T2Configuration(side,0));
            T2Check(runtimes[side]->SetPreConnectGate([this]{std::unique_lock<std::mutex> lock(gate);++arrived;changed.notify_all();
                return changed.wait_for(lock,2s,[&]{return arrived==2;});}),"normal listener readiness gate");
            const auto d=T2Descriptor(side,0);
            descriptors[side]=policy==NPolicy::NativeBound?runtimes[side]->MakeStockDescriptor(d):runtimes[side]->MakeDescriptor(d);
            T2Check(bool(descriptors[side]),"normal original descriptor issuance");
            T2Check(!runtimes[side]->MakeDescriptor(d),"normal descriptor issuance is one-shot");
            workers[side]=std::make_unique<T2Worker>(*runtimes[side]);
        }
    }
    ~NormalControlFixture(){if(!finished)try{for(unsigned group=0;group<2;++group)for(unsigned side=0;side<2;++side)Remove(side,group);
        for(auto& r:runtimes)if(r)r->Close();}catch(...){}}
    void Create(unsigned side,unsigned group,unsigned actual){
        auto& a=apps[side][group];T2Check(!a.participant,"normal slot occupied");
        a.received=std::make_unique<T2Reader>();a.sent=std::make_unique<T2Writer>();const auto d=T2Descriptor(side,0);
        dds::DomainParticipantQos q;q.transport().use_builtin_transports=false;q.transport().user_transports={descriptors[side]};
        q.properties().properties().emplace_back("fastdds.ignore_local_endpoints","true");auto& w=q.wire_protocol();
        w.builtin.metatrafficMulticastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.clear();
        w.builtin.metatrafficUnicastLocatorList.push_back(d.directory.locals.front().local_locator);
        w.default_unicast_locator_list.clear();w.default_unicast_locator_list.push_back(d.directory.locals.front().local_locator);
        w.default_multicast_locator_list.clear();w.builtin.initialPeersList.clear();w.builtin.initialPeersList.push_back(d.directory.routes.front().key.remote_locator);
        w.builtin.discovery_config.initial_announcements.count=1;
        a.participant=dds::DomainParticipantFactory::get_instance()->create_participant(actual,q);
        T2Check(a.participant&&a.participant->get_domain_id()==actual,"original Factory and actual DDS domain");
        T2Check(type.register_type(a.participant)==0,"normal application type");
        auto* topic=a.participant->create_topic("NormalDomainControl",type.get_type_name(),dds::TOPIC_QOS_DEFAULT);
        auto* pub=a.participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);auto* sub=a.participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
        T2Check(topic&&pub&&sub,"normal application entities");auto wq=dds::DATAWRITER_QOS_DEFAULT;auto rq=dds::DATAREADER_QOS_DEFAULT;
        wq.reliability().kind=rq.reliability().kind=dds::RELIABLE_RELIABILITY_QOS;wq.data_sharing().off();rq.data_sharing().off();
        a.writer=pub->create_datawriter(topic,wq,a.sent.get());a.reader=sub->create_datareader(topic,rq,a.received.get());
        T2Check(a.writer&&a.reader,"normal real Writer/Reader");
    }
    void FirstPair(unsigned left,unsigned right){
        std::exception_ptr errors[2];std::thread threads[2];
        for(unsigned side=0;side<2;++side)threads[side]=std::thread([&,side]{try{Create(side,0,side?right:left);}catch(...){errors[side]=std::current_exception();}});
        for(auto& t:threads)t.join();for(auto e:errors)if(e)std::rethrow_exception(e);
    }
    void Remove(unsigned side,unsigned group){
        auto& a=apps[side][group];if(!a.participant)return;
        T2Check(a.participant->delete_contained_entities()==0,"normal delete entities");
        T2Check(dds::DomainParticipantFactory::get_instance()->delete_participant(a.participant)==0,"normal original Factory delete");
        a.participant=nullptr;a.writer=nullptr;a.reader=nullptr;a.received.reset();a.sent.reset();
    }
    void WaitMatches(){
        for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group){
            auto& a=apps[side][group];if(!a.participant)continue;std::vector<dds::InstanceHandle_t> expected;
            for(const auto& remote:apps[1-side])if(remote.participant&&remote.participant->get_domain_id()==a.participant->get_domain_id())
                expected.push_back(remote.writer->get_instance_handle());
            {std::unique_lock<std::mutex> lock(a.received->gate);T2Check(a.received->changed.wait_for(lock,2s,[&]{
                if(a.received->matches!=int(expected.size())||a.received->matched_writers.size()!=expected.size())return false;
                for(const auto& id:expected)if(std::find(a.received->matched_writers.begin(),a.received->matched_writers.end(),id)==a.received->matched_writers.end())return false;
                return true;}),"normal exact remote Writer GUID set/domain isolation");}
            {std::unique_lock<std::mutex> lock(a.sent->gate);T2Check(a.sent->changed.wait_for(lock,2s,[&]{return a.sent->matches==int(expected.size());}),"normal exact remote reader count");}
        }
    }
    static T2Sample Sample(unsigned side,unsigned group,unsigned cycle){
        T2Sample s;s.cycle=cycle;s.source=side+2*group;s.ordinal=cycle*16+s.source;
        for(unsigned i=0;i<s.payload.size();++i)s.payload[i]=std::uint8_t(cycle*13+s.source*29+i);return s;
    }
    void PairTraffic(unsigned left_group,unsigned right_group,unsigned cycle){
        WaitMatches();const unsigned groups[]{left_group,right_group};const auto before=public_socket_fake::Observe();
        for(unsigned side=0;side<2;++side){auto& a=apps[side][groups[side]];auto& b=apps[1-side][groups[1-side]];auto s=Sample(side,groups[side],cycle);
            T2Check(a.participant->get_domain_id()==b.participant->get_domain_id(),"legal pair selected wrong domain");
            T2Check(a.writer->write(&s,dds::HANDLE_NIL)==0,"normal real DDS write");b.received->WaitData(s,a.writer->guid());
            T2Check(a.writer->wait_for_acknowledgments({1,0})==0,"normal reliable acknowledgment");}
        const auto after=public_socket_fake::Observe();T2Check(after.forwards[0]>before.forwards[0]&&after.forwards[1]>before.forwards[1],"normal pair bypassed Fake Socket");
        std::cout<<"NORMAL_PAIR actual_domain="<<apps[0][left_group].participant->get_domain_id()<<" writes=2 takes=2 exact_GUID_payload=1 cycle="<<cycle<<'\n';
    }
    unsigned AllRemoteTraffic(unsigned cycle){
        WaitMatches();unsigned writes=0,takes=0;const auto before=public_socket_fake::Observe();
        for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group){auto& a=apps[side][group];if(!a.participant)continue;auto s=Sample(side,group,cycle);
            T2Check(a.writer->write(&s,dds::HANDLE_NIL)==0,"NativeBound real DDS write");++writes;
            for(unsigned remote=0;remote<2;++remote){auto& b=apps[1-side][remote];if(!b.participant)continue;
                T2Check(a.participant->get_domain_id()==b.participant->get_domain_id(),"NativeBound cross-domain target");b.received->WaitData(s,a.writer->guid());++takes;
                std::cout<<"NORMAL_BOUND_EDGE from="<<side<<':'<<group<<" to="<<1-side<<':'<<remote<<" exact_GUID_payload=1 ordinal="<<s.ordinal<<'\n';}
            T2Check(a.writer->wait_for_acknowledgments({1,0})==0,"NativeBound acknowledgment");}
        const auto after=public_socket_fake::Observe();T2Check(after.forwards[0]>before.forwards[0]&&after.forwards[1]>before.forwards[1],"NativeBound bypassed Fake Socket");
        std::cout<<"NORMAL_BOUND_LEDGER writes="<<writes<<" takes="<<takes<<" local_edges=NOT_PROVIDED\n";return takes;
    }
    void NegativeFirstPair(bool write){
        WaitMatches();for(unsigned side=0;side<2;++side){auto& a=apps[side][0];a.received->WaitNone();
            if(write){auto s=Sample(side,0,999);const auto rc=a.writer->write(&s,dds::HANDLE_NIL);
                T2Check(rc==0,"unmatched public DDS write submission");
                std::cout<<"CROSS_NEGATIVE_UNMATCHED_WRITE side="<<side<<" raw_rc="<<int(rc())<<" transport_DATA_success=NOT_CLAIMED\n";}}
        for(unsigned side=0;side<2;++side)apps[side][0].received->WaitNone();
        const auto native=public_socket_fake::Observe();T2Check(native.forwards[0]&&native.forwards[1],"negative discovery never traversed Socket seam");
        std::cout<<"NORMAL_CROSS_NEGATIVE domains="<<apps[0][0].participant->get_domain_id()<<','<<apps[1][0].participant->get_domain_id()<<" matches=0 takes=0\n";
    }
    void Finish(){
        for(unsigned group=0;group<2;++group)for(unsigned side=0;side<2;++side)Remove(side,group);
        for(auto& r:runtimes)T2Check(r->Close(),"normal final close");for(auto& w:workers)w->Join();
        const auto sdk=public_socket_fake::Observe();T2Check(!sdk.live&&!sdk.queued&&!sdk.bytes&&!sdk.invalid,"normal Fake residue");
        for(auto& r:runtimes){const auto s=phase0::RuntimeProbeAccess::ObserveRuntime(*r);
            T2Check(s.closed&&!s.live_views&&!s.active_pumps&&s.close.adapter_drained&&!s.receive.records&&!s.receive.bytes&&
                !s.receive.active_applications&&!s.receive.retained_applications&&!s.native.callbacks&&!s.native.receives&&
                !s.native.setup.reserved&&!s.native.setup.entered&&!s.native.send.reserved&&!s.native.send.entered&&
                !s.native.close.reserved&&!s.native.close.entered,"normal final queue/lease/native-operation residue");
            std::cout<<"NORMAL_CLOSED retained_ids="<<s.native.owned_ids<<" SDK_drain=UNKNOWN\n";}
        for(const auto& d:descriptors)T2Check(!d->create_transport(),"normal owner reopened after last view");finished=true;
    }
};
void NormalMixed(unsigned first,unsigned second){
    NormalControlFixture f(NPolicy::Generic);f.FirstPair(first,second);f.NegativeFirstPair(false);
    f.Create(0,1,second);f.Create(1,1,first);f.PairTraffic(0,1,1);f.PairTraffic(1,0,2);
    for(unsigned side=0;side<2;++side){const auto s=phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[side]);
        T2Check(s.live_views==2&&s.receive.active_applications==2&&s.native.owned_ids==5,"normal two-domain view/resource footprint");
        T2Check(!f.runtimes[side]->Close()&&!f.descriptors[side]->create_transport(),"normal busy/third facade bound");}
    f.Remove(0,1);f.Remove(1,1);f.NegativeFirstPair(true);f.Finish();
    std::cout<<"NORMAL_MIXED root_primitive=151 actual_domains="<<first<<','<<second<<" real_public_callback=1 manual_callback=0 one_descriptor_per_Runtime=1\n";
}
void NormalNativeBound(){
    NormalControlFixture f(NPolicy::NativeBound);f.FirstPair(151,151);f.Create(0,1,151);f.Create(1,1,151);
    T2Check(f.AllRemoteTraffic(1)==8,"NativeBound all four crosspairs");const auto native=public_socket_fake::Observe();
    for(unsigned side=0;side<2;++side)T2Check(!f.runtimes[side]->Close()&&!f.descriptors[side]->create_transport(),"NativeBound busy/third slot bound");
    const auto old=f.apps[0][0].writer->get_instance_handle();f.Remove(0,0);
    T2Check(f.AllRemoteTraffic(2)==4&&!f.runtimes[0]->Close(),"NativeBound sibling survives independent close");
    f.Create(0,0,151);T2Check(!(f.apps[0][0].writer->get_instance_handle()==old),"replacement GUID reused");
    T2Check(f.AllRemoteTraffic(3)==8,"NativeBound replacement all remote edges");const auto after=public_socket_fake::Observe();
    for(unsigned side=0;side<2;++side)T2Check(after.sockets[side]==native.sockets[side]&&after.binds[side]==native.binds[side]&&after.shutdowns[side]==native.shutdowns[side],"NativeBound rebuilt native on view replacement");
    f.Finish();std::cout<<"NORMAL_NATIVE_BOUND entry=MakeStockDescriptor expected=151 directed_takes=20 manual_callback=0\n";
}
struct NormalRawReceiver final:eprosima::fastdds::rtps::TransportReceiverInterface {
    unsigned calls=0;std::uint8_t marker=0;
    void OnDataReceived(const std::uint8_t* p,std::uint32_t n,const Locator&,const Locator&)override{
        T2Check(n==4&&p[1]==19&&p[2]==83&&p[3]==7,"normal raw exact payload");++calls;marker=p[0];}
};
bool NormalRawSend(fastdds_dsoftbus::binding::SenderResource& sender,const Locator& remote,std::uint8_t marker){
    std::vector<Locator> targets{remote};eprosima::fastdds::rtps::Locators first(targets.begin()),last(targets.end());const std::uint8_t bytes[]{marker,19,83,7};
    return sender.send(bytes,sizeof(bytes),&first,&last,std::chrono::steady_clock::now()+2s);
}
std::size_t NormalSendCalls(const experimental::Runtime& r){return r.ObserveSocket().operations[static_cast<std::size_t>(phase0::PublicSocketOperation::Send)].calls;}
void NormalAwaitSenders(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);public_socket_fake::SetListenerNames({T2Listener(0,0),T2Listener(1,0)});
    public_socket_fake::SetInitiatorNames({T2Configuration(0,0).identity.service_name,T2Configuration(1,0).identity.service_name});
    experimental::Runtime a(T2Configuration(0,0)),b(T2Configuration(1,0));std::mutex gate;std::condition_variable changed;unsigned arrived=0;
    auto barrier=[&]{std::unique_lock<std::mutex> lock(gate);++arrived;changed.notify_all();return changed.wait_for(lock,2s,[&]{return arrived==2;});};
    T2Check(a.SetPreConnectGate(barrier)&&b.SetPreConnectGate(barrier),"normal raw readiness");auto ca=T2Descriptor(0,0),cb=T2Descriptor(1,0);
    auto da=a.MakeDescriptor(ca),db=b.MakeDescriptor(cb);T2Check(da&&db,"normal raw original generic descriptor");
    using Transport=eprosima::fastdds::rtps::TransportInterface;
    std::unique_ptr<Transport> ta(da->create_transport()),keep(da->create_transport()),tb(db->create_transport());T2Check(ta&&keep&&tb,"normal raw original facades");
    NormalRawReceiver ra,rb,replacement;std::exception_ptr errors[2];std::thread other([&]{try{T2Check(tb->init()&&tb->OpenInputChannel(cb.directory.locals.front().local_locator,&rb,8192),"normal raw B input");}catch(...){errors[1]=std::current_exception();}});
    try{T2Check(ta->init()&&ta->OpenInputChannel(ca.directory.locals.front().local_locator,&ra,8192),"normal raw A input");}catch(...){errors[0]=std::current_exception();}
    other.join();for(auto e:errors)if(e)std::rethrow_exception(e);
    auto ar=ca.directory.routes.front().key.remote_locator,br=cb.directory.routes.front().key.remote_locator;
    eprosima::fastdds::rtps::SendResourceList cached,remote,fresh;
    T2Check(ta->OpenOutputChannel(cached,ar)&&tb->OpenOutputChannel(remote,br),"normal raw actual sender resources");
    const auto initial_calls=NormalSendCalls(a);T2Check(!NormalRawSend(*cached.front(),ar,60),"initial cached Await sender admitted");
    (void)a.PumpSend();T2Check(NormalSendCalls(a)==initial_calls,"initial Await entered native provider");
    auto* av=dynamic_cast<DSoftBusTransport*>(ta.get());T2Check(av&&av->domain_snapshot().state==ViewDomainState::Await,"initial raw guard");
    eprosima::fastdds::rtps::LocatorList peers;fastdds_dsoftbus::binding::PortParameters ports;
    // Raw controlled driver chooses151. This is never presented as a stock
    // Factory callback; the DDS cases above use only actual Factory callbacks.
    T2Check(ta->configureInitialPeerLocator(ar,ports,151,peers)&&tb->configureInitialPeerLocator(br,ports,151,peers),"controlled raw domain151");
    auto wire=[&](unsigned marker){T2Check(NormalRawSend(*remote.front(),br,std::uint8_t(marker)),"normal raw B submit");
        T2Check(b.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"normal raw actual C forwarding");};
    wire(71); // Admission pins the original A receiver; leave it queued.
    const auto native=public_socket_fake::Observe();ta.reset();const auto before=NormalSendCalls(a);
    T2Check(!NormalRawSend(*cached.front(),ar,72),"old cached sender reopened after Close");
    ta.reset(da->create_transport());av=dynamic_cast<DSoftBusTransport*>(ta.get());
    T2Check(av&&av->domain_snapshot().state==ViewDomainState::Await,"fresh replacement inherited Ready");
    T2Check(ta->init()&&ta->OpenInputChannel(ca.directory.locals.front().local_locator,&replacement,8192)&&ta->OpenOutputChannel(fresh,ar),"fresh raw receiver/sender");
    T2Check(cached.front().get()!=fresh.front().get(),"old sender resource retargeted as fresh");
    T2Check(!NormalRawSend(*cached.front(),ar,73)&&!NormalRawSend(*fresh.front(),ar,74),"cached Closed or fresh Await sender admitted");
    (void)a.PumpSend();a.PumpReceive();
    T2Check(NormalSendCalls(a)==before&&!ra.calls&&!replacement.calls&&!phase0::RuntimeProbeAccess::ObserveRuntime(a).receive.active_applications,
        "fresh Await provider entry/old lease retarget/delivery lease");
    T2Check(av->domain_snapshot().denied[0]>0,"fresh actual sender did not reach its guard denial");
    wire(75);a.PumpReceive();T2Check(!replacement.calls&&!ra.calls&&av->domain_snapshot().state==ViewDomainState::Await&&
        !phase0::RuntimeProbeAccess::ObserveRuntime(a).receive.active_applications,"Await late RX acquired a lease or delivered");
    T2Check(ta->configureInitialPeerLocator(ar,ports,151,peers),"replacement own controlled151 callback");
    wire(76);a.PumpReceive();T2Check(replacement.calls==1&&replacement.marker==76&&!ra.calls,"replacement precise marker/old receiver isolation");
    T2Check(!NormalRawSend(*cached.front(),ar,77)&&NormalRawSend(*fresh.front(),ar,78),"old sender retargeted or fresh Ready blocked");
    T2Check(a.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"fresh Ready actual native call");b.PumpReceive();
    T2Check(rb.calls==1&&rb.marker==78,"fresh sender exact payload");
    const auto after=public_socket_fake::Observe();for(unsigned side=0;side<2;++side)T2Check(after.sockets[side]==native.sockets[side]&&after.binds[side]==native.binds[side]&&after.shutdowns[side]==native.shutdowns[side],"raw view replacement rebuilt native");
    ta.reset();keep.reset();tb.reset();cached.clear();fresh.clear();remote.clear();T2Check(a.Close()&&b.Close()&&!da->create_transport(),"raw final normal close/reopen");
    const auto end=public_socket_fake::Observe();T2Check(!end.live&&!end.queued&&!end.bytes&&!end.invalid,"raw final residue");
    std::cout<<"NORMAL_AWAIT_SENDERS cached_Closed=DENIED fresh_Await=DENIED native_provider_delta=0 old_lease=NOT_RETARGETED fresh_Ready_marker=78 callback_source=CONTROLLED_RAW_DRIVER SDK_drain=UNKNOWN\n";
}
void NativeBoundZero(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);auto c=T2Configuration(0,0);DSoftBusTransportDescriptor d(8192);
    const std::array<v1::DeviceIdentity,2> identities{{{"stock-normal-zero",0,"A"},{"stock-normal-zero",0,"B"}}};std::array<std::string,2> names;
    for(unsigned side=0;side<2;++side)T2Check(v1::AddressCodec::EncodeScopedServiceName({"stock-normal-zero",0,17+side,v1::Profile::BytesNative},
        {v1::NativeNameContract::PackageScopedV1,"host.public.dds","host.public.dds.v1"},names[side]),"zero native name");
    c.listener_identity.service_name=names[0];c.peer_name=names[1];V1Deployment deployment{identities[0],17,v1::Profile::BytesNative,"normal.zero",
        {{identities[1],18,c.peer_network_id,c.identity.service_name,{c.expected_peer_initiator_name}}}};
    T2Check(d.ConfigureV1(ScopedHostDeployment(deployment)).code==DirectoryError::None,"domain-zero validated descriptor");experimental::Runtime r(c);
    auto bad=d;bad.expected_domain_id=1;T2Check(!r.MakeDescriptor(bad),"explicit contradiction accepted or consumed issuance");
    auto native=r.MakeStockDescriptor(d);T2Check(bool(native),"real explicit NativeBound zero entry");
    using Transport=eprosima::fastdds::rtps::TransportInterface;std::unique_ptr<Transport> a(native->create_transport()),b(native->create_transport());
    T2Check(a&&b&&!native->create_transport()&&!r.Close(),"zero two borrowed views/busy/bound");
    for(auto* view:{a.get(),b.get()}){auto* concrete=dynamic_cast<DSoftBusTransport*>(view);T2Check(concrete,"zero concrete original Transport");const auto s=concrete->domain_snapshot();
        T2Check(s.configured&&s.expected_domain_known&&s.expected_domain_id==0&&s.state==ViewDomainState::Await,"zero optional was absent or not NativeBound");}
    a.reset();b.reset();T2Check(r.Close()&&!native->create_transport(),"zero final close/reopen");const auto sdk=public_socket_fake::Observe();
    T2Check(!sdk.live&&!sdk.sockets[0]&&!sdk.sockets[1],"zero unused metadata test opened native sockets");
    std::cout<<"NORMAL_NATIVE_BOUND_ZERO optional=ENGAGED expected=0 two_views=1 SDK_calls=0 actual_DDS_domain0_communication=NOT_TESTED\n";
}
}
int main(int argc,char** argv){try{
    T2LoadedDll();T2Check(argc==2,"one additive normal-control case");const std::string c=argv[1];
    if(c=="mixed151-152")NormalMixed(151,152);else if(c=="mixed152-153")NormalMixed(152,153);
    else if(c=="native-bound")NormalNativeBound();else if(c=="await-senders")NormalAwaitSenders();else if(c=="expected-zero")NativeBoundZero();
    else throw std::runtime_error("unknown normal control");
    std::cout<<"PASS normal_control "<<c<<" HOST_FAKE_SOCKET_ONLY DEVICE_TESTED=0 contract_migration=NOT_APPROVED\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL normal_control "<<e.what()<<'\n';return 1;}}
