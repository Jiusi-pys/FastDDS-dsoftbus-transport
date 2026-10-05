// Included inside the existing public Socket fixture's anonymous namespace.
// Fake replaces only the public node/Socket API boundaries, never production maps.
struct RouteFixture {
    Receiver receiver;Peer peer;DSoftBusTransportDescriptor config=Config();
    v1::DeviceIdentity remote{"route.fixture",201,"fixture-peer-udid"};
    std::shared_ptr<PublicSocketBackend> backend;std::shared_ptr<HostProviderLoop> loop;
    std::unique_ptr<DSoftBusTransport> facade;rtps::SendResourceList senders;
    bool permit=false;
    RouteFixture(std::function<void(RouteFixture&)> before_open={},bool close_race=false){
        fake_node::Reset();V1Deployment deployment;
        deployment.identity={remote.name_space,remote.domain,"fixture-local-udid"};
        deployment.callback_namespace="route.fixture.callback";
        deployment.native_name_scope={v1::NativeNameContract::PackageScopedV1,"host.reaccept","host.reaccept.nodes"};
        deployment.peers.push_back({remote,1,"model.net","host.reaccept.nodes.local",{"host.reaccept.nodes.peer"},deployment.native_name_scope});
        Check(config.ConfigureV1(deployment).code==DirectoryError::None,"canonical V1 route fixture");
        config.default_metatraffic_unicast={Local()};config.default_user_unicast={Local()};
        const auto& route=config.directory.routes.front();PublicSocketConfiguration c;
        c.identity={route.fields.local_initiator_name,{1701},{1801}};
        c.listener_identity={config.directory.locals.front().local_listen_name,{1702},{1802}};
        c.package_name="host.reaccept";c.peer_name=route.fields.remote_listen_name;c.peer_network_id=route.fields.remote_network_id;
        c.expected_peer_initiator_name=route.fields.expected_remote_initiator_names.front();c.bind_qos={{0,64}};c.listen_qos=c.bind_qos;
        c.max_message_size=64;c.bounded_reaccept_host=true;c.route_lifecycle=true;
        peer.name=c.expected_peer_initiator_name;peer.network="model.net";
        {std::lock_guard<std::mutex> lock(fake_node::gate);fake_node::nodes[peer.network]=remote.udid;}
        backend=std::make_shared<PublicSocketBackend>(c);loop=std::make_shared<HostProviderLoop>(backend);
        Check(loop->SetBoundedReplacementGate([this]{return permit;}),"original peer-ready gate retained");
        facade=HostFacadeTestAccess::Create(config,loop);
        if(before_open)before_open(*this);
        if(close_race){Check(facade->init()&&!facade->OpenInputChannel(Local(),&receiver,64),"late registration cannot publish closed runtime");return;}
        Check(facade->init()&&facade->OpenInputChannel(Local(),&receiver,64)&&facade->OpenOutputChannel(senders,Remote()),"real canonical runtime/retained sender");
        Check(fake_node::registrations==1,"one designated node subscription");Accept(30);
    }
    ~RouteFixture(){reaccept_fake::in_send={};reaccept_fake::in_bind={};fake_node::before_query={};fake_node::before_registration={};if(facade)facade->shutdown();}
    Locator Local()const{return config.directory.locals.front().local_locator;}
    Locator Remote()const{return config.directory.routes.front().key.remote_locator;}
    bool Send(unsigned char value){std::vector<rtps::NetworkBuffer> b{{&value,1}};std::vector<Locator> p{Remote()};rtps::Locators begin(p.begin()),end(p.end());return senders.front()->send(b,1,&begin,&end,std::chrono::steady_clock::now()+2s,0);}
    void Pump(){const auto result=loop->PumpReceive();Check(result==BackendStatus::None||result==BackendStatus::NoWork,"lifecycle control/receiver pump");}
    void Accept(int id){Check(reaccept_fake::server->OnNegotiate(id,peer.Get()),"current operational accepted route");{std::lock_guard<std::mutex> l(reaccept_fake::gate);reaccept_fake::alive[id]=true;}reaccept_fake::server->OnBind(id,peer.Get());}
    void Offline(){fake_node::Offline(peer.network);Check(!Send(9),"formal SDK node offline rejects retained SenderResource");Pump();
        const auto observation=loop->Observe();
        Check(observation.adapter.retired_outgoing.shutdown_returned||observation.adapter.retired_outgoing.on_shutdown_seen,"offline old outgoing settlement remains observable before fresh resolution");}
    void Online(const std::string& network){
        {std::lock_guard<std::mutex> lock(fake_node::gate);fake_node::nodes.clear();fake_node::nodes[network]=remote.udid;}
        fake_node::Online(network);
    }
    void Finish(){facade->shutdown();const auto v=loop->Observe();Check(v.close.status==BackendStatus::None&&v.close.adapter_drained,"normal original close");Check(fake_node::unregistrations==1,"one node unregister, no drain claim");}
};
void RouteResolve(bool same){
    RouteFixture f;Check(f.Send(1)&&f.loop->PumpSend().outcome==NativeSendOutcome::ReturnedSdkCode,"PRE real native send");
    const auto* sender=f.senders.front().get();const auto local=f.Local(),remote=f.Remote();f.Offline();
    const auto before=reaccept_fake::socket_calls;const auto network=same?"model.net":"model.new.net";f.Online(network);f.Pump();
    Check(fake_node::queries==1&&fake_node::identity_queries==1,"fresh actual public directory+identity resolution even same string");
    Check(reaccept_fake::socket_calls==before&&!f.Send(2),"resolver alone does not replace or reuse old Ready owner");
    fake_node::Offline("unrelated.online.hint");
    f.permit=true;f.Pump();
    Check(reaccept_fake::socket_networks.back()==network&&f.loop->Observe().adapter.binding.socket==21,"actual replacement Socket consumes resolved network");
    f.peer.network=network;f.Accept(31);
    Check(f.senders.front().get()==sender&&f.Local()==local&&f.Remote()==remote,"original sender and complete V1 identity retained");
    Check(f.Send(3)&&f.loop->PumpSend().outcome==NativeSendOutcome::ReturnedSdkCode&&reaccept_fake::sent_ids.back()==21,"POST new submission uses current owner/version");
    fake_node::Offline(network);Check(!f.Send(4),"later offline invalidates current replacement too");f.Pump();
    Check(reaccept_fake::closes[21]==1&&reaccept_fake::closes[31]==1,"later offline settles current owner responsibilities without another renewal requirement");
    f.Finish();
}
void RouteUnrelatedHint(){
    RouteFixture f;f.Offline();
    {std::lock_guard<std::mutex> lock(fake_node::gate);fake_node::nodes.clear();fake_node::nodes["model.new.net"]=f.remote.udid;}
    fake_node::Online("unrelated.online.hint");f.Pump();
    Check(fake_node::queries==1,"online hint triggers real expected-identity resolver");
    fake_node::Offline("unrelated.online.hint");f.permit=true;f.Pump();
    Check(reaccept_fake::socket_networks.back()=="model.new.net","offline stale hint cannot invalidate resolved expected peer");
    f.peer.network="model.new.net";f.Accept(31);Check(f.Send(3),"resolved current peer remains admitted");f.Finish();
}
void RouteRegistrationClose(bool fail=false){
    Event entered,release;RouteFixture* observed=nullptr;
    auto opening=std::async(std::launch::async,[&]{return std::make_unique<RouteFixture>([&](RouteFixture& f){observed=&f;fake_node::before_registration=[&]{entered.Signal();release.Wait();};},true);});
    entered.Wait();fake_node::unregistration_result=fail?-1:0;auto closing=std::async(std::launch::async,[&]{observed->facade->shutdown();});
    Check(observed->loop->WaitSealed(std::chrono::steady_clock::now()+2s),"close seals during public Reg");release.Signal();
    auto f=opening.get();closing.get();
    Check(fake_node::registrations==1&&fake_node::unregistrations==1&&(fail||fake_node::callback==nullptr),"late successful registration reconciles exactly one original Unreg");
    Check(f->loop->Observe().closed,"original Runtime stays closed after late Reg");
    Check((f->loop->Observe().close.status==BackendStatus::None)==!fail,"Unreg failure stays visible despite successful socket cleanup");
}
void RouteOfflineNoAllocation(){
    RouteFixture f;NodeBasicInfo offline{};std::memcpy(offline.networkId,"model.net",10);
    loop_deny_allocations=true;fake_node::callback->onNodeOffline(&offline);loop_deny_allocations=false;
    Check(!f.Send(3),"authoritative offline callback invalidates retained sender without allocation");f.Pump();f.Finish();
}
void RouteFailureThenReturn(){
    RouteFixture f;f.Offline();f.Online("model.new.net");fake_node::query_result=-1;f.Pump();
    Check(!f.Send(3)&&fake_node::queries==1,"failed initial resolution stays unavailable");
    fake_node::Offline("model.new.net");fake_node::query_result=0;fake_node::Online("model.new.net");f.Pump();
    Check(fake_node::queries==2,"new authoritative return after failure performs fresh resolution");f.permit=true;f.Pump();
    Check(reaccept_fake::socket_networks.back()=="model.new.net","successful later resolution creates current candidate");f.Finish();
}
void RouteResolverReentry(){
    RouteFixture f;f.Offline();f.Online("model.new.net");
    bool entered=false;fake_node::before_query=[&]{if(!entered){entered=true;const auto result=f.loop->PumpReceive();Check(result==BackendStatus::None||result==BackendStatus::NoWork,"public resolver can reenter existing control pump");}};
    f.Pump();fake_node::before_query={};Check(entered&&fake_node::queries==1,"one resolver call under reentry, no parallel attempt");
    f.permit=true;f.Pump();Check(reaccept_fake::socket_networks.back()=="model.new.net","reentrant resolver publishes only original current request");f.Finish();
}
void RouteResolutionCollision(){
    RouteFixture f;
    for(bool reverse:{true,false}){
        NativeTableConfiguration c;c.directory=f.config.directory;c.service_pkg_name="host.reaccept";
        c.bounded_reaccept_host=true;c.allow_closed_dial_retry=true;c.dial_only_egress=true;c.directions={RouteDirection::Both,RouteDirection::Both};
        auto other=c.directory.routes.front();v1::DeviceIdentity identity{f.remote.name_space,f.remote.domain,"fixture-other-udid"};EndpointId key{};
        Check(v1::AddressCodec::DeriveKey(identity,key)&&v1::AddressCodec::EncodeLocator({key,1,v1::Profile::BytesNative},other.key.remote_locator),"second canonical route identity");
        other.fields.remote_network_id="model.other.net";
        if(reverse)other.fields.remote_listen_name+=".other";
        else other.fields.expected_remote_initiator_names={"host.reaccept.nodes.other"};
        c.directory.routes.push_back(other);c.directory.identities.push_back({key,identity.name_space,identity.domain,identity.udid});
        auto built=NativeOwnerTable::Build(c);Check(bool(built),"initial nonambiguous bounded directory");auto& table=**built.value;
        Check(table.ObserveNodeOffline("model.net")==NativeStatus::None&&table.ObserveNodeOnline("model.other.net")==NativeStatus::None,"formal node route transitions");
        auto request=table.TakeNodeResolution();Check(bool(request),"actual pending route request");
        {std::lock_guard<std::mutex> lock(fake_node::gate);fake_node::nodes.clear();fake_node::nodes["model.other.net"]=f.remote.udid;}
        auto result=f.backend->ResolveNode(request.value->identity(),request.value->network_hint());
        Check(table.CompleteNodeResolution(*request.value,result)==NativeStatus::ConfigurationMismatch,"reverse/create operational collision rejected transactionally");
        Check(!table.RouteAvailable(c.directory.routes.front().key)&&table.RouteAvailable(other.key),"colliding result cannot publish or invalidate other live route");
    }
    f.Finish();
}
void RouteEntered(){
    RouteFixture f;Event entered,release;reaccept_fake::in_send=[&]{entered.Signal();release.Wait();};Check(f.Send(1),"R2 original admission");
    auto old=std::async(std::launch::async,[&]{return f.loop->PumpSend();});entered.Wait();fake_node::Offline("model.net");
    Check(!f.Send(2)&&f.loop->Observe().native.send.entered==1,"old native call remains owned; new admission rejects");
    release.Signal();Check(old.get().outcome==NativeSendOutcome::ReturnedSdkCode,"old original entered ticket completes");reaccept_fake::in_send={};f.Pump();f.Finish();
}
void RouteResolveFailure(const std::string& kind){
    RouteFixture f;f.Offline();f.Online("model.new.net");
    {std::lock_guard<std::mutex> lock(fake_node::gate);
        if(kind=="query")fake_node::query_result=-1;
        else if(kind=="identity")fake_node::nodes.begin()->second="wrong-udid";
        else if(kind=="missing")fake_node::nodes.clear();
        else if(kind=="key")fake_node::key_result=-1;
        else if(kind=="ambiguous")fake_node::nodes["model.duplicate.net"]=f.remote.udid;
    }
    f.permit=true;const auto before=reaccept_fake::socket_calls;f.Pump();
    Check(fake_node::queries==1&&!f.Send(3)&&reaccept_fake::socket_calls==before,"failed/missing/mismatched resolver never falls back to startup route");
    fake_node::Online("model.new.net");f.Pump();Check(fake_node::queries==1,"duplicate refused-result event does not retry indefinitely");f.Finish();
}
void RouteLateResolve(){
    RouteFixture f;f.Offline();Event entered,release;fake_node::before_query=[&]{entered.Signal();release.Wait();};
    f.Online("model.new.net");f.permit=true;const auto before=reaccept_fake::socket_calls;
    auto old=std::async(std::launch::async,[&]{return f.loop->PumpReceive();});entered.Wait();
    fake_node::Offline("model.new.net");f.Online("model.last.net");release.Signal();
    const auto outcome=old.get();const bool denied=!f.Send(3);
    std::cout<<"R6_LATE_RESOLVE status="<<static_cast<unsigned>(outcome)<<" denied="<<denied<<" sockets_before="<<before<<" sockets_after="<<reaccept_fake::socket_calls<<std::endl;
    Check((outcome==BackendStatus::None||outcome==BackendStatus::NoWork)&&denied&&reaccept_fake::socket_calls==before,"old request cannot publish even a matching newer directory result");
    fake_node::before_query={};f.Pump();Check(fake_node::queries==2,"new request really queries again");
    f.permit=true;f.Pump();Check(reaccept_fake::socket_networks.back()=="model.last.net","only current request publishes new destination");f.Finish();
}
void RouteLateBind(){
    RouteFixture f;f.Offline();f.Online("model.new.net");f.Pump();Event entered,release;
    reaccept_fake::in_bind=[&]{entered.Signal();release.Wait();};f.permit=true;
    auto old=std::async(std::launch::async,[&]{return f.loop->PumpReceive();});entered.Wait();fake_node::Offline("model.new.net");
    Check(f.loop->Observe().native.setup.entered==1&&!f.Send(2),"in-flight original Bind retained but new route not admitted");
    release.Signal();Check(old.get()==BackendStatus::BindingRejected&&!f.Send(3),"late Bind success cannot publish READY");reaccept_fake::in_bind={};
    f.Finish();Check(reaccept_fake::closes[21]==1,"late successful Socket retains original close responsibility");
}
void RouteCloseAndDuplicates(){
    RouteFixture f;f.Offline();f.Online("model.new.net");for(int i=0;i<5;++i)fake_node::Online("model.new.net");
    Event entered,release;fake_node::before_query=[&]{entered.Signal();release.Wait();};
    auto resolving=std::async(std::launch::async,[&]{return f.loop->PumpReceive();});entered.Wait();INodeStateCb* retained=fake_node::callback;
    auto close=std::async(std::launch::async,[&]{f.facade->shutdown();});Check(f.loop->WaitSealed(std::chrono::steady_clock::now()+2s),"final shutdown seals original authority");
    release.Signal();(void)resolving.get();close.get();fake_node::before_query={};
    NodeBasicInfo late{};std::memcpy(late.networkId,"model.new.net",14);const auto calls=reaccept_fake::socket_calls;
    retained->onNodeOnline(&late);Check(reaccept_fake::socket_calls==calls&&fake_node::queries==1&&!f.Send(3),"copied late node callback cannot revive closed runtime or fan out attempts");
    Check(f.loop->Observe().closed&&f.loop->Observe().close.status==BackendStatus::None,"final original close succeeded");
}
