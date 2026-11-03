// Stock public Factory/Writer/Reader and an explicitly extended Fake Socket C
// seam. This is Host evidence, not SDK capacity or device qualification.
#define main DcPriorViewsMain
#include "runtime_views_tests.cpp"
#undef main
#include <fastdds_dsoftbus/dsoftbus/DomainCircuitName.hpp>
#ifdef SendMessage
#undef SendMessage
#endif
#include <socket.h>
#include "FakeNodeDirectory.hpp"

namespace {
// Observation-only delegate: returns precisely the owned pointer supplied by
// the production descriptor. Fixed storage neither allocates nor wraps Transport.
struct CapturedDescriptor final:eprosima::fastdds::rtps::TransportDescriptorInterface {
    std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> original;
    mutable std::array<DSoftBusTransport*,8> views{};
    mutable std::atomic<unsigned> count{0};
    explicit CapturedDescriptor(std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> d)
        :TransportDescriptorInterface(d->maxMessageSize,1),original(std::move(d)){}
    eprosima::fastdds::rtps::TransportInterface* create_transport()const override{
        auto* value=original->create_transport();
        if(value){const auto n=count.fetch_add(1);if(n<views.size())views[n]=dynamic_cast<DSoftBusTransport*>(value);}
        return value;
    }
    std::uint32_t min_send_buffer_size()const override{return original->min_send_buffer_size();}
};
using Included=std::array<std::array<bool,2>,2>;
const Included all{{{{true,true}},{{true,true}}}};
std::array<std::shared_ptr<CapturedDescriptor>,2> Scope(T2LifecycleFixture& f){
    std::array<std::shared_ptr<CapturedDescriptor>,2> captures;
    for(unsigned side=0;side<2;++side){
        auto d=f.runtimes[side]->MakeStockDescriptor(T2Descriptor(side,f.epoch));T2Check(bool(d),"explicit scoped descriptor");
        captures[side]=std::make_shared<CapturedDescriptor>(d);f.descriptors[side]=captures[side];
    }
    return captures;
}
void WaitEdges(T2LifecycleFixture& f,const Included& included){
    for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group){
        auto& app=f.endpoints[side][group];if(!app.participant||!included[side][group])continue;
        std::vector<dds::InstanceHandle_t> expected;
        for(unsigned remote=0;remote<2;++remote)if(included[1-side][remote]&&f.endpoints[1-side][remote].participant)
            expected.push_back(f.endpoints[1-side][remote].writer->get_instance_handle());
        auto& reader=*app.received;
        {std::unique_lock<std::mutex> lock(reader.gate);
            T2Check(reader.changed.wait_for(lock,2s,[&]{
                if(reader.matched_writers.size()!=expected.size()||reader.matches!=int(expected.size()))return false;
                for(const auto& h:expected)if(std::find(reader.matched_writers.begin(),reader.matched_writers.end(),h)==reader.matched_writers.end())return false;
                return true;
            }),"exact remote writer GUID set on each independent reader");}
        auto& writer=*app.sent;
        {std::unique_lock<std::mutex> lock(writer.gate);
            T2Check(writer.changed.wait_for(lock,2s,[&]{return writer.matches==int(expected.size());}),"exact remote reader count");}
    }
}
unsigned Traffic(T2LifecycleFixture& f,const Included& included,unsigned cycle){
    WaitEdges(f,included);unsigned takes=0,writes=0;const auto before=public_socket_fake::Observe();
    for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group){
        auto& source=f.endpoints[side][group];if(!source.participant||!included[side][group])continue;
        T2Sample value;value.cycle=cycle;value.source=side+2*group;value.ordinal=cycle*4+value.source;
        for(unsigned i=0;i<value.payload.size();++i)value.payload[i]=std::uint8_t(cycle*29+value.source*17+i);
        T2Check(source.writer->write(&value,dds::HANDLE_NIL)==0,"real stock DDS write");++writes;
        for(unsigned remote=0;remote<2;++remote){
            auto& target=f.endpoints[1-side][remote];if(!target.participant||!included[1-side][remote])continue;
            target.received->WaitData(value,source.writer->guid());++takes;
            const auto handle=source.writer->get_instance_handle();const char* digits="0123456789abcdef";
            std::cout<<"CROSS_EDGE cycle="<<cycle<<" from="<<side<<':'<<group<<" to="<<1-side<<':'<<remote<<" writer=";
            for(unsigned i=0;i<16;++i)std::cout<<digits[handle.value[i]>>4]<<digits[handle.value[i]&15];
            std::cout<<" exact_GUID_payload=1 ordinal="<<value.ordinal<<'\n';
        }
        T2Check(source.writer->wait_for_acknowledgments({1,0})==0,"real reliable acknowledgment");
    }
    const auto after=public_socket_fake::Observe();
    T2Check(after.forwards[0]>before.forwards[0]&&after.forwards[1]>before.forwards[1],"actual Socket C link frames in both directions");
    std::cout<<"CROSS_LEDGER cycle="<<cycle<<" writes="<<writes<<" takes="<<takes<<" local_edges=NOT_PROVIDED other_transport=OFF intraprocess=OFF DataSharing=OFF\n";
    return takes;
}
void NativeUnchanged(const public_socket_fake::Snapshot& before){
    const auto after=public_socket_fake::Observe();
    for(unsigned side=0;side<2;++side)T2Check(after.sockets[side]==before.sockets[side]&&after.listens[side]==before.listens[side]&&
        after.binds[side]==before.binds[side]&&after.shutdowns[side]==before.shutdowns[side],"view changes rebuilt or closed native anchors");
}
void CircuitNames(){
    using namespace phase0;std::string out="unchanged";std::string_view base;std::uint32_t domain=0;
    T2Check(EncodeDomainCircuitName("com.kaihong.mdds.run.A.tx",151,out)&&
        out=="com.kaihong.mdds.run.A.tx.dds1d00000097","exact complete authorized prefix and canonical suffix");
    T2Check(DecodeDomainCircuitName(out,base,domain)&&base=="com.kaihong.mdds.run.A.tx"&&domain==151,"round-trip identity");
    T2Check(EncodeDomainCircuitName("authorized",0,out)&&out=="authorized.dds1d00000000","domain zero is a value");
    for(const std::string bad:{"authorized.dds1d0000097","authorized.dds1d000000097","authorized.dds1d0000009A",
            ".dds1d00000097","authorized.dds1d00000097.trailing","authorized.dds1d-0000097"})
        T2Check(!DecodeDomainCircuitName(bad,base,domain),"noncanonical suffix accepted");
    T2Check(EncodeDomainCircuitName(std::string(241,'a'),0xffffffffu,out)&&out.size()==255,"exact SDK name bound");
    const auto before=out;T2Check(!EncodeDomainCircuitName(std::string(242,'a'),151,out)&&out==before,"overlength name truncated");
    T2Check(!EncodeDomainCircuitName(std::string("a\0b",3),151,out),"embedded NUL name accepted");
    std::cout<<"CIRCUIT_NAMES full_prefix=1 canonical_uint32=1 zero=1 SDK_name_limit=255 package_limit=64 policy_or_actor_permission=NOT_PROVED\n";
}
void DefaultRemote(){
    T2LifecycleFixture f(0,true,true,true);std::array<std::shared_ptr<CapturedDescriptor>,2> captured;
    for(unsigned side=0;side<2;++side){
        captured[side]=std::make_shared<CapturedDescriptor>(f.descriptors[side]);f.descriptors[side]=captured[side];
        T2Check(!f.runtimes[side]->MakeDescriptor(T2Descriptor(side,0)),"normal descriptor issuance ceased being one-shot");
    }
    f.Pair(0,true);f.Pair(1,true);T2Check(Traffic(f,all,1)==8,"normal default all remote edges");
    for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group){
        auto* view=captured[side]->views[group];T2Check(view,"capture production facade");const auto s=view->domain_snapshot();
        T2Check(s.expected_domain_known&&s.expected_domain_id==151&&s.first_observed_domain==151&&s.state==ViewDomainState::Ready,
            "real public callback did not bind independent automatic guard");
    }
    const auto native=public_socket_fake::Observe();f.Remove(0,0);Included remaining=all;remaining[0][0]=false;
    T2Check(Traffic(f,remaining,2)==4&&!f.runtimes[0]->Close(),"sibling activity/CloseBusy");
    T2Check(f.Create(0,0,151,true),"same-incarnation replacement");T2Check(Traffic(f,all,3)==8,"replacement remote edges");
    NativeUnchanged(native);f.Finish();
    std::cout<<"DEFAULT_CIRCUIT_REMOTE directed_takes=20 real_Runtime_config=1 one_descriptor=1 two_views=1 local_edges=NOT_PROVIDED\n";
}
void CircuitIdentity(){
    T2LifecycleFixture f;f.Pair(0);T2Check(f.Create(0,1,152)&&f.Create(1,1,152),"second real domain circuit");
    f.Traffic(0,1);f.Traffic(1,2);const auto native=public_socket_fake::Observe();
    const auto original=f.runtimes[0]->ObserveSocket();T2Check(original.domain_circuits==2&&original.family_ids_retained==5,"actual five native family IDs");
    const auto usage=f.records.SnapshotUsage();const auto left=phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[0]);
    const auto right=phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[1]);
    T2Check(usage.records_created==6&&usage.slots_free==2&&usage.slots_exposed==6&&left.native.owned_ids==5&&right.native.owned_ids==5,
        "two-domain resource footprint exceeded planned unchanged budget");
    std::cout<<"DOMAIN_BUDGET records_created="<<usage.records_created<<" exposed="<<usage.slots_exposed<<" free="<<usage.slots_free
        <<" project_record_capacity="<<record_slot_capacity<<" entry_upper_bound="<<record_slot_capacity*unsigned(RecordRole::Count)
        <<" native_ids="<<left.native.owned_ids<<','<<right.native.owned_ids<<" retained_silence=UNKNOWN\n";
    // Inputs are explicit test values, independent of production name encoding.
    const std::string name="host.public.B.outgoing.dds1d00000097",network="model.net.B",package="host.public.dds";
    auto probe=[&](int id,const std::string& n,const std::string& net,const std::string& pkg,int type){return public_socket_fake::ProbeCandidate(0,id,n,net,pkg,type);};
    const auto accepted=public_socket_fake::CurrentSocket(0,2);
    T2Check(probe(accepted,name,network,package,DATA_TYPE_BYTES),"identical original active negotiation should be idempotent");
    T2Check(!probe(accepted,name,"foreign.network",package,DATA_TYPE_BYTES)&&
        !probe(accepted,name,network,"foreign.package",DATA_TYPE_BYTES)&&
        !probe(accepted,name,network,package,DATA_TYPE_FILE),"same numeric ID bypassed full peer identity");
    T2Check(!probe(public_socket_fake::CurrentSocket(0,0),name,network,package,DATA_TYPE_BYTES)&&
        !probe(public_socket_fake::CurrentSocket(0,1),name,network,package,DATA_TYPE_BYTES),"listener/outgoing ID accepted as a second circuit owner");
    T2Check(!probe(800,"host.public.B.outgoing.dds1d000000097",network,package,DATA_TYPE_BYTES)&&
        !probe(801,"host.public.B.outgoing.dds1d0000009A",network,package,DATA_TYPE_BYTES)&&
        !probe(802,"foreign.dds1d00000097",network,package,DATA_TYPE_BYTES)&&
        !probe(803,name,"foreign.network",package,DATA_TYPE_BYTES)&&
        !probe(804,name,network,"foreign.package",DATA_TYPE_BYTES)&&
        !probe(805,name,network,package,DATA_TYPE_FILE)&&
        !probe(806,"host.public.B.outgoing.dds1d00000099",network,package,DATA_TYPE_BYTES),"unauthorized peer/third-domain accepted");
    const auto after=f.runtimes[0]->ObserveSocket();T2Check(after.domain_circuits==2&&after.family_ids_retained==12,"rejected ID tombstones or circuit bound changed");
    T2Check(after.domain_accepted_sockets==original.domain_accepted_sockets&&after.domain_outgoing_sockets==original.domain_outgoing_sockets,"negative candidate changed original owners");
    f.Traffic(0,3);f.Traffic(1,4);NativeUnchanged(native);f.Finish();
    std::cout<<"CIRCUIT_IDENTITY full_peer=1 family_collision=REFUSED third_domain=REFUSED old_owners_healthy=1 exposed_entry_reuse=0\n";
}
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
void CircuitRaw(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
    public_socket_fake::SetListenerNames({T2Listener(0,0),T2Listener(1,0)});
    public_socket_fake::SetInitiatorNames({T2Configuration(0,0).identity.service_name,T2Configuration(1,0).identity.service_name});
    experimental::Runtime a(T2Configuration(0,0)),b(T2Configuration(1,0));
    std::mutex gate;std::condition_variable changed;unsigned arrived=0;
    auto barrier=[&]{std::unique_lock<std::mutex> lock(gate);++arrived;changed.notify_all();return changed.wait_for(lock,2s,[&]{return arrived==2;});};
    T2Check(a.SetPreConnectGate(barrier)&&b.SetPreConnectGate(barrier),"raw normal preconnect");
    auto ca=T2Descriptor(0,0),cb=T2Descriptor(1,0);auto da=a.MakeDescriptor(ca),db=b.MakeDescriptor(cb);T2Check(da&&db,"raw generic descriptors");
    std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> ta(da->create_transport()),keep(da->create_transport()),tb(db->create_transport());
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
    wire(73);a.PumpReceive();T2Check(!replacement.calls&&av->domain_snapshot().state==ViewDomainState::Await&&
        !phase0::RuntimeProbeAccess::ObserveRuntime(a).receive.active_applications,"fresh Await acquired a delivery lease or delivered bytes");
    T2Check(ta->configureInitialPeerLocator(ar,ports,151,peers),"fresh raw actual-domain confirmation");
    wire(74);a.PumpReceive();T2Check(replacement.calls==1&&replacement.marker==74&&!ra.calls,"fresh Ready payload/old lease isolation");
    const auto after=public_socket_fake::Observe();for(unsigned side=0;side<2;++side)T2Check(after.sockets[side]==native.sockets[side]&&after.shutdowns[side]==native.shutdowns[side],"same-incarnation replacement rebuilt native");
    ta.reset();keep.reset();tb.reset();as.clear();bs.clear();T2Check(a.Close()&&b.Close(),"normal raw final close");
    T2Check(!da->create_transport(),"normal last close reopened");const auto end=public_socket_fake::Observe();T2Check(!end.live&&!end.queued&&!end.bytes&&!end.invalid,"raw final residue");
    std::cout<<"CIRCUIT_RAW Await_RX=no_lease Await_TX=DENIED unsupported=AWAIT late_actual=READY mismatch=STICKY queued_provider=NOT_ENTERED Refused_RX=DENIED replacement=FRESH old_lease=NOT_RETARGETED same_native_incarnation=1 SDK_drain=UNKNOWN\n";
}
void SocketIdCollision(){
    T2LifecycleFixture f;f.Pair(0);f.Traffic(0,1);const auto before=public_socket_fake::Observe();
    // Fault injection deliberately returns a known live listener ID. It is not
    // a claim that a conforming SDK normally reuses a still-live native handle.
    public_socket_fake::FailNext(public_socket_fake::InjectedOperation::Socket,public_socket_fake::CurrentSocket(0,0));
    T2Check(f.Create(0,1,152),"Factory return must not be mapped on circuit failure");
    const auto s=f.runtimes[0]->ObserveSocket();T2Check(s.family_collisions==1,"positive Socket collision not diagnosed");
    const auto after=public_socket_fake::Observe();T2Check(after.shutdowns[0]==before.shutdowns[0],"collision acquired a foreign Shutdown right");
    f.endpoints[0][1].received->WaitNone();f.Traffic(0,2);
    for(unsigned group=0;group<2;++group)for(unsigned side=0;side<2;++side)f.Remove(side,group);
    T2Check(!f.runtimes[0]->Close()&&f.runtimes[1]->Close(),"failed native operation lost sticky close evidence");
    for(auto& worker:f.workers)worker->Join();
    const auto closed=phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[0]);const auto final=public_socket_fake::Observe();
    T2Check(closed.closed&&closed.close.status!=phase0::BackendStatus::None&&closed.close.adapter_drained&&
        !closed.live_views&&!closed.active_pumps&&!closed.receive.active_applications&&!closed.receive.records&&
        !closed.native.callbacks&&!closed.native.receives&&!final.live&&!final.queued&&!final.invalid,"failed-ID cleanup/residue");
    T2Check(!f.runtimes[0]->Close()&&!f.descriptors[0]->create_transport(),"failed close forgot sticky error or resurrected");
    f.finished=true;
    std::cout<<"COLLISION_CLOSE bool=0 closed=1 drained=1 status="<<unsigned(closed.close.status)<<" native_ID_shutdown_authority=ORIGINAL_ONLY\n";
    std::cout<<"SOCKET_ID_COLLISION raw_positive=1 original_owner_retained=1 foreign_Shutdown=0 failed_view=REFUSED\n";
}
}
// Route loss and recovery on one live domain circuit: the SDK ends both circuit
// connections with LINK_DOWN while the peer node is offline; the transport must
// not redial before the peer is online again, then renews the dial lane with a
// fresh Socket/Bind, re-accepts the peer's new connection, and the same stock
// Participants exchange DDS samples again. Host Fake Socket evidence only.
void RouteRenewal(){
    T2LifecycleFixture f;f.Pair(0);f.Traffic(0,1);
    public_socket_fake::EnableFreshReplacementIds();
    auto set_online=[](bool online){std::lock_guard<std::mutex> lock(fake_node::gate);fake_node::nodes.clear();
        if(online){fake_node::nodes["model.net.A"]="udid.A";fake_node::nodes["model.net.B"]="udid.B";}};
    set_online(false);
    const auto before=public_socket_fake::Observe();
    const auto old_out=public_socket_fake::CurrentSocket(0,1),old_acc=public_socket_fake::CurrentSocket(0,2);
    const auto ended=public_socket_fake::LinkDown(SHUTDOWN_REASON_LINK_DOWN);
    T2Check(ended==4,"route loss must end both circuit connections on both sides");
    std::this_thread::sleep_for(1500ms);
    const auto offline=public_socket_fake::Observe();
    for(unsigned side=0;side<2;++side)T2Check(offline.sockets[side]==before.sockets[side]&&offline.binds[side]==before.binds[side],
        "redial attempted while the peer node was offline");
    set_online(true);
    const auto deadline=std::chrono::steady_clock::now()+10s;public_socket_fake::Snapshot renewed;
    for(;;){renewed=public_socket_fake::Observe();
        if(renewed.binds[0]>before.binds[0]&&renewed.binds[1]>before.binds[1])break;
        T2Check(std::chrono::steady_clock::now()<deadline,"circuit dial lane was not renewed after the peer came back online");
        std::this_thread::sleep_for(50ms);}
    for(unsigned side=0;side<2;++side)T2Check(renewed.sockets[side]==before.sockets[side]+1&&renewed.binds[side]==before.binds[side]+1,
        "exactly one fresh Socket/Bind per side");
    T2Check(public_socket_fake::CurrentSocket(0,1)!=old_out&&public_socket_fake::CurrentSocket(0,2)!=old_acc,"renewal reused a retired SDK ID");
    f.Traffic(0,2);f.Traffic(0,3);
    const auto s=f.runtimes[0]->ObserveSocket();
    T2Check(s.domain_circuits==1&&s.domain_outgoing_sockets[0]&&*s.domain_outgoing_sockets[0]==public_socket_fake::CurrentSocket(0,1),
        "renewed outgoing socket not owned by the circuit");
    f.Finish();set_online(false);
    std::cout<<"ROUTE_RENEWAL link_down=4 offline_redial=0 fresh_socket_bind=1/side reaccept=1 dds_after=2_cycles same_participants=1 close_residue=0\n";
}
int main(int argc,char** argv){try{
    T2LoadedDll();T2Check(argc==2,"one circuit case");const std::string c=argv[1];
    if(c=="names")CircuitNames();else if(c=="default-remote")DefaultRemote();else if(c=="identity")CircuitIdentity();
    else if(c=="socket-collision")SocketIdCollision();else if(c=="raw")CircuitRaw();else if(c=="route-renewal")RouteRenewal();else throw std::runtime_error("unknown circuit case");
    std::cout<<"PASS domain_circuit "<<c<<" HOST_FAKE_SOCKET_ONLY DEVICE_TESTED=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL domain_circuit "<<e.what()<<'\n';return 1;}}
