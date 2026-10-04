#include "V1NativeNameTestSupport.hpp"
// Actual assembly of the installed experimental entry over the test-only Fake C
// boundary. The installed entry documents one Runtime and one Participant per
// process, and the process-global service-record table is not reusable across two
// live pairs, so each case runs in its own process and the CTest entries select
// the case by argument. Within one process the two runtimes still exist
// simultaneously, which is what makes the per-object ownership of the gate
// observable. No case substitutes a precomputed "verified" flag for an event.
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include "FakePublicSocketLink.hpp"
#include <fastdds/rtps/transport/TransportReceiverInterface.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace fastdds_dsoftbus::dsoftbus;
v1::DeviceIdentity identity(unsigned side) { return {"host-runtime", 151, side ? "synthetic-B" : "synthetic-A"}; }
Locator local(unsigned side) { EndpointId key{}; Locator value; auto id=identity(side); if(!v1::AddressCodec::DeriveKey(id,key)||!v1::AddressCodec::EncodeLocator({key,17+side,v1::Profile::BytesNative},value))std::abort(); return value; }
std::string listener(unsigned side) { std::string name; auto id=identity(side); if(!v1::AddressCodec::EncodeScopedServiceName({id.name_space,id.domain,17+side,v1::Profile::BytesNative},{v1::NativeNameContract::PackageScopedV1,"host.public.dds","host.public.dds.v1"},name))std::abort();return name; }
phase0::PublicSocketConfiguration config(unsigned side) {
    const std::string me=side?"host.public.B.":"host.public.A.", peer=side?"host.public.A.":"host.public.B.";
    phase0::PublicSocketConfiguration c;
    c.identity={me+"outgoing",{910+side*4},{1010+side*4}};
    c.listener_identity={listener(side),{911+side*4},{1011+side*4}};
    c.package_name="host.public.dds";c.peer_name=listener(1-side);c.peer_network_id=side?"model.net.A":"model.net.B";
    c.listen_qos={{0,80},{1,2000}};c.bind_qos=c.listen_qos;c.max_message_size=8192;c.expected_peer_initiator_name=peer+"outgoing";return c;
}
DSoftBusTransportDescriptor descriptor(unsigned side) {
    auto c=config(side);DSoftBusTransportDescriptor d(8192);
    V1Deployment input{identity(side),17+side,v1::Profile::BytesNative,"library.test",
        {{identity(1-side),18-side,c.peer_network_id,c.identity.service_name,{config(1-side).identity.service_name}}}};
    if(d.ConfigureV1(ScopedHostDeployment(input)).code!=DirectoryError::None)std::abort();return d;
}
namespace {
using Transport = eprosima::fastdds::rtps::TransportInterface;
struct NullReceiver final : eprosima::fastdds::rtps::TransportReceiverInterface {
    void OnDataReceived(const eprosima::fastdds::rtps::octet*,const uint32_t,const Locator&,const Locator&)override{}
};
// The installed entry only reaches the provider adapter through an opened input
// channel, so every case drives init() and the channel exactly as the real
// consumer's participant creation does.
bool Bring(Transport& t, NullReceiver& r, unsigned side) {
    return t.init() && t.OpenInputChannel(local(side), &r, 8192);
}
bool NoResidue() { const auto s=public_socket_fake::Observe(); return !s.live&&!s.queued&&!s.bytes&&!s.invalid; }

int Baseline() {
    auto trace=std::make_shared<host_dds::Trace>();public_socket_fake::Configure(trace,true);public_socket_fake::SetListenerNames({listener(0),listener(1)});
    experimental::Runtime a(config(0)),b(config(1));
    auto da=a.MakeDescriptor(descriptor(0)),db=b.MakeDescriptor(descriptor(1));
    if(!da||!db||a.MakeDescriptor(descriptor(0)))return 1;
    std::unique_ptr<Transport> ta(da->create_transport()),tb(db->create_transport());
    if(!ta||!tb||da->create_transport())return 2;
    bool aa=false,bb=false;std::thread x([&]{aa=ta->init();}),y([&]{bb=tb->init();});x.join();y.join();
    if(!aa||!bb)return 3;
    if(a.ObserveSocket().fake_sdk||b.ObserveSocket().fake_sdk)return 4; // actual real shim, explicitly Fake C boundary in this test only
    ta->shutdown();tb->shutdown();ta.reset();tb.reset();
    if(!a.Close()||!b.Close())return 5;
    if(!NoResidue())return 6;
    std::cout<<"BASELINE_ONE_SHOT_AND_CLOSE_PASS"<<std::endl;return 0;
}

// The assembly hook must run once per runtime, strictly after that side published
// its listener and strictly before that side's outgoing connect, and it must be
// able to hold both connects until the peer's listener exists. The two-party
// barrier can only complete if the hook really is the last point before the
// connect: if either side had already called Bind, a recorded snapshot would show
// a nonzero bind count. Without a gate the connect is unconditional, which the
// baseline case already exercised.
int Gate() {
    auto trace=std::make_shared<host_dds::Trace>();public_socket_fake::Configure(trace,true);public_socket_fake::SetListenerNames({listener(0),listener(1)});
    experimental::Runtime a(config(0)),b(config(1));
    std::mutex gate_mutex;std::condition_variable gate_ready;
    int arrived=0,admissions=0,replaced=0;std::vector<public_socket_fake::Snapshot> at_entry;
    // A gate installed before any use may be replaced explicitly; the superseded
    // callable must never run.
    if(!a.SetPreConnectGate([&]{++replaced;return false;}))return 1;
    std::function<bool()> gate=[&]{
        std::unique_lock<std::mutex> lock(gate_mutex);
        at_entry.push_back(public_socket_fake::Observe());
        ++arrived;gate_ready.notify_all();
        if(!gate_ready.wait_for(lock,std::chrono::seconds(5),[&]{return arrived==2;}))return false;
        ++admissions;return true;};
    if(!a.SetPreConnectGate(gate))return 2;
    if(!b.SetPreConnectGate(gate))return 3;              // separate object, separate gate
    auto da=a.MakeDescriptor(descriptor(0)),db=b.MakeDescriptor(descriptor(1));
    if(!da||!db)return 4;
    if(a.SetPreConnectGate(gate))return 5;               // refused: this runtime already issued its one descriptor
    std::unique_ptr<Transport> ta(da->create_transport()),tb(db->create_transport());
    if(!ta||!tb)return 6;
    NullReceiver ra,rb;
    bool aa=false,bb=false;
    std::thread x([&]{aa=Bring(*ta,ra,0);}),y([&]{bb=Bring(*tb,rb,1);});x.join();y.join();
    if(!aa||!bb)return 7;                                // the gate admitted both connects
    if(arrived!=2||admissions!=2||at_entry.size()!=2)return 8;
    if(replaced!=0)return 9;                             // the superseded callable never ran
    bool saw_a=false,saw_b=false;
    for(const auto& snap:at_entry){
        if(snap.binds[0]!=0||snap.binds[1]!=0)return 10; // no side may connect before the hook admits
        saw_a=saw_a||snap.listens[0]!=0;saw_b=saw_b||snap.listens[1]!=0;}
    if(!saw_a||!saw_b)return 11;                         // both listeners were already published
    if(a.SetPreConnectGate(gate))return 12;              // refused: the loop has started and may be using it
    ta->CloseInputChannel(local(0));tb->CloseInputChannel(local(1));
    ta->shutdown();tb->shutdown();ta.reset();tb.reset();
    if(!a.Close()||!b.Close())return 13;
    if(!NoResidue())return 14;
    std::cout<<"PRECONNECT_GATE_ORDER_AND_ONE_SHOT_PASS"<<std::endl;return 0;
}

// A refusing gate must fail the channel open instead of continuing the connect,
// and the refusal must be reported rather than hidden as a clean local close.
int Refusal() {
    auto trace=std::make_shared<host_dds::Trace>();public_socket_fake::Configure(trace,true);public_socket_fake::SetListenerNames({listener(0),listener(1)});
    experimental::Runtime a(config(0));
    if(!a.SetPreConnectGate([]{return false;}))return 1;
    auto d=a.MakeDescriptor(descriptor(0));
    if(!d)return 2;
    std::unique_ptr<Transport> t(d->create_transport());
    if(!t)return 3;
    NullReceiver r;
    if(!t->init())return 4;
    if(t->OpenInputChannel(local(0),&r,8192))return 5;   // refusal is not a warning
    const auto after=public_socket_fake::Observe();
    if(after.listens[0]==0)return 6;                     // the listener really was published first
    if(after.binds[0]!=0)return 7;                       // and the refused outgoing connect never ran
    t->shutdown();t.reset();
    if(a.Close())return 8;                               // a recorded fault must not read as a clean close
    if(!NoResidue())return 9;
    std::cout<<"PRECONNECT_GATE_REFUSAL_PASS"<<std::endl;return 0;
}

// A gate belongs to the Runtime that installed it. Both runtimes exist at the same
// time and both install a gate; clearing one must leave the other's in place, and
// each connect must consult only its own. A single process-wide slot would either
// refuse the second install or clear both, so this case fails under that design.
int Ownership() {
    auto trace=std::make_shared<host_dds::Trace>();public_socket_fake::Configure(trace,true);public_socket_fake::SetListenerNames({listener(0),listener(1)});
    experimental::Runtime a(config(0)),b(config(1));
    int a_runs=0,b_runs=0;
    if(!a.SetPreConnectGate([&]{++a_runs;return true;}))return 1;
    if(!b.SetPreConnectGate([&]{++b_runs;return true;}))return 2;   // not refused: a different object
    if(!a.SetPreConnectGate(nullptr))return 3;                     // clear only A's
    auto da=a.MakeDescriptor(descriptor(0)),db=b.MakeDescriptor(descriptor(1));
    if(!da||!db)return 4;
    std::unique_ptr<Transport> ta(da->create_transport()),tb(db->create_transport());
    if(!ta||!tb)return 5;
    NullReceiver ra,rb;
    bool aa=false,bb=false;
    std::thread x([&]{aa=Bring(*ta,ra,0);}),y([&]{bb=Bring(*tb,rb,1);});x.join();y.join();
    if(!aa||!bb)return 6;
    if(a_runs!=0)return 7;                               // A's gate was cleared and never ran
    if(b_runs!=1)return 8;                               // B's gate survived A's clear and ran once
    if(a.SetPreConnectGate(nullptr))return 9;            // both loops have started: refused
    if(b.SetPreConnectGate(nullptr))return 10;
    ta->CloseInputChannel(local(0));tb->CloseInputChannel(local(1));
    ta->shutdown();tb->shutdown();ta.reset();tb.reset();
    if(!a.Close()||!b.Close())return 11;
    if(!NoResidue())return 12;
    std::cout<<"PRECONNECT_GATE_PER_RUNTIME_OWNERSHIP_PASS"<<std::endl;return 0;
}
// An exception is a refusal, not a successful connect or an escaped exception.
int Throwing() {
    auto trace=std::make_shared<host_dds::Trace>();public_socket_fake::Configure(trace,true);public_socket_fake::SetListenerNames({listener(0),listener(1)});
    experimental::Runtime a(config(0));int calls=0;
    if(!a.SetPreConnectGate([&]() -> bool {++calls;throw 7;}))return 1;
    auto d=a.MakeDescriptor(descriptor(0));if(!d)return 2;
    std::unique_ptr<Transport> t(d->create_transport());if(!t||!t->init())return 3;
    NullReceiver receiver;
    if(t->OpenInputChannel(local(0),&receiver,8192))return 4;
    const auto o=public_socket_fake::Observe();
    if(calls!=1||o.listens[0]==0||o.binds[0]!=0)return 5;
    t->shutdown();t.reset();if(a.Close()||!NoResidue())return 6;
    std::cout<<"PRECONNECT_THROW_REFUSAL_PASS"<<std::endl;return 0;
}

// Re-enter read-only observation and the post-issuance setter from the actual
// gate. Holding either assembly mutex across the callable would deadlock this
// test (bounded by CTest). A shared capture outlives the caller's copy and is
// released after the owning runtime/descriptors are destroyed.
int ReentrantLifetime() {
    auto trace=std::make_shared<host_dds::Trace>();public_socket_fake::Configure(trace,true);public_socket_fake::SetListenerNames({listener(0),listener(1)});
    struct Lifetime { explicit Lifetime(int& x):destroyed(x){} ~Lifetime(){++destroyed;} int& destroyed; };
    int destroyed=0,calls=0;std::weak_ptr<Lifetime> weak;
    {
        experimental::Runtime a(config(0)),b(config(1));
        auto capture=std::make_shared<Lifetime>(destroyed);weak=capture;
        if(!a.SetPreConnectGate([&,held=capture]{
            if(!held||destroyed)return false;
            ++calls;
            const auto observed=a.ObserveSocket();
            if(!observed.listener_configured)return false;
            return !a.SetPreConnectGate(nullptr);
        }))return 1;
        capture.reset();if(weak.expired())return 2;
        auto da=a.MakeDescriptor(descriptor(0)),db=b.MakeDescriptor(descriptor(1));
        std::unique_ptr<Transport> ta(da->create_transport()),tb(db->create_transport());
        NullReceiver ra,rb;bool aa=false,bb=false;
        std::thread x([&]{aa=Bring(*ta,ra,0);}),y([&]{bb=Bring(*tb,rb,1);});x.join();y.join();
        if(!aa||!bb||calls!=1||weak.expired())return 3;
        ta->CloseInputChannel(local(0));tb->CloseInputChannel(local(1));
        ta->shutdown();tb->shutdown();ta.reset();tb.reset();
        if(!a.Close()||!b.Close()||!NoResidue())return 4;
    }
    if(!weak.expired()||destroyed!=1)return 5;
    std::cout<<"PRECONNECT_REENTRANT_AND_CAPTURE_LIFETIME_PASS"<<std::endl;return 0;
}
}
int main(int argc,char** argv) {
    const char* mode = argc>1?argv[1]:"";
    if(std::strcmp(mode,"--service-scope")==0){
        auto trace=std::make_shared<host_dds::Trace>();public_socket_fake::Configure(trace,true);
        std::array<std::string,2> expected;
        for(unsigned side=0;side<2;++side){auto id=identity(side);std::string body;if(!v1::AddressCodec::EncodeServiceName({id.name_space,id.domain,17+side,v1::Profile::BytesNative},body))return 1;expected[side]="host.public.dds.v1."+body;}
        public_socket_fake::SetListenerNames(expected);
        experimental::Runtime a(config(0)),b(config(1));auto da=a.MakeDescriptor(descriptor(0)),db=b.MakeDescriptor(descriptor(1));
        if(!da||!db)return 2;std::unique_ptr<Transport> ta(da->create_transport()),tb(db->create_transport());NullReceiver ra,rb;
        if(!ta||!tb||!ta->init()||!tb->init())return 3;
        bool openedA=false,openedB=false;std::thread x([&]{openedA=ta->OpenInputChannel(local(0),&ra,8192);}),y([&]{openedB=tb->OpenInputChannel(local(1),&rb,8192);});x.join();y.join();
        ta->shutdown();tb->shutdown();ta.reset();tb.reset();a.Close();b.Close();
        if(!openedA||!openedB){std::cerr<<"SCOPED_SERVICE_PROVIDER_RED actual SocketInfo listener/peerName did not match explicit scoped canonical names\n";return 4;}
        if(!NoResidue())return 5;std::cout<<"SCOPED_SERVICE_ACTUAL_PUBLIC_API_PASS\n";return 0;
    }
    if(std::strcmp(mode,"--service-reject")==0){
        public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
        experimental::Runtime runtime(config(0));
        for(int mutation=0;mutation<7;++mutation){auto d=descriptor(0);
            if(mutation==0)d.native_name_scope.deployment_prefix.clear();
            if(mutation==1)d.native_name_scope.authorized_package="another.package";
            if(mutation==2)d.native_name_scope.deployment_prefix="host.public.dds.host.public.dds.v1";
            if(mutation==3)d.directory.routes.front().fields.remote_listen_name="wrong.peer";
            if(mutation==4)d.directory.routes.front().fields.expected_remote_initiator_names={"host.public.wrong.peer"};
            if(mutation==5)d.directory.routes.front().fields.local_initiator_name=d.directory.locals.front().local_listen_name;
            if(mutation==6){d.directory.locals.front().local_listen_name="host.public.dds.v1."+d.directory.locals.front().local_listen_name;}
            if(runtime.MakeDescriptor(d))return 10+mutation;
        }
        for(const auto& scope:std::array<v1::NativeNameScope,2>{{{v1::NativeNameContract::PackageScopedV1,"host.public.dds","host.public.dds.rogue"},{v1::NativeNameContract::PackageScopedV1,"another.package","another.package.v1"}}}) {
            auto input=ScopedHostDeployment({identity(0),17,v1::Profile::BytesNative,"library.test",{{identity(1),18,"model.net.B","host.public.A.outgoing",{"host.public.B.outgoing"}}}});
            input.native_name_scope=scope;input.peers.front().native_name_scope=scope;
            DSoftBusTransportDescriptor wrong(8192);if(wrong.ConfigureV1(input).code!=DirectoryError::None||wrong.ValidateV1().code!=DirectoryError::None)return 26;
            if(runtime.MakeDescriptor(wrong))return 27;
        }
        auto deployment=ScopedHostDeployment({identity(0),17,v1::Profile::BytesNative,"library.test",{{identity(1),18,"model.net.B","host.public.A.outgoing",{"host.public.B.outgoing"}}}});
        DSoftBusTransportDescriptor d(8192);auto unchanged=descriptor(0);d=unchanged;
        deployment.peers.front().native_name_scope.deployment_prefix="host.public.dds.other";
        if(d.ConfigureV1(deployment).code==DirectoryError::None||d.default_user_unicast!=unchanged.default_user_unicast)return 30;
        deployment.peers.front().native_name_scope=deployment.native_name_scope;deployment.identity.name_space=std::string(116,'n');deployment.peers.front().identity.name_space=deployment.identity.name_space;
        if(d.ConfigureV1(deployment).code==DirectoryError::None||d.default_user_unicast!=unchanged.default_user_unicast)return 31;
        auto scope=deployment.native_name_scope;scope.deployment_prefix="host.public.dds.v1.host.public.dds.v1";if(v1::AddressCodec::ValidNativeScope(scope))return 32;
        scope=deployment.native_name_scope;scope.deployment_prefix="host.public.dds.bad*";if(v1::AddressCodec::ValidNativeScope(scope))return 33;
        if(!runtime.MakeDescriptor(descriptor(0)))return 34;
        auto observation=public_socket_fake::Observe();if(observation.sockets[0]||observation.sockets[1]||observation.live)return 35;
        std::cout<<"SCOPED_SERVICE_REJECTIONS_BEFORE_NATIVE_PASS\n";return 0;
    }
    if(std::strcmp(mode,"--identity")==0){
        public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
        experimental::Runtime runtime(config(0));auto invalid=descriptor(0);invalid.directory.identities.clear();
        if(runtime.MakeDescriptor(invalid))return 1;
        invalid=descriptor(0);invalid.directory.identities.front().udid.clear();
        if(runtime.MakeDescriptor(invalid))return 2;
        auto valid=runtime.MakeDescriptor(descriptor(0));if(!valid)return 3;
        const auto observed=public_socket_fake::Observe();if(observed.sockets[0]||observed.sockets[1]||observed.live)return 4;
        std::cout<<"V1_IDENTITY_REJECTED_BEFORE_NATIVE_AND_ISSUANCE_PASS\n";return 0;
    }
    if(std::strcmp(mode,"--gate")==0)return Gate();
    if(std::strcmp(mode,"--refusal")==0)return Refusal();
    if(std::strcmp(mode,"--ownership")==0)return Ownership();
    if(std::strcmp(mode,"--throw")==0)return Throwing();
    if(std::strcmp(mode,"--reentrant-lifetime")==0)return ReentrantLifetime();
    if(*mode!=0){std::cerr<<"unknown mode"<<std::endl;return 64;}
    return Baseline();
}
