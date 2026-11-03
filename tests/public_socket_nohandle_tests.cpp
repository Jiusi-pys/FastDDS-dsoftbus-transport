// Real PublicSocketBackend and Runtime over explicitly test-only C symbols.
// Each CTest case has a fresh process. No service-table reset is available.
#include "V1NativeNameTestSupport.hpp"
#include "HostProviderTestSupport.hpp"
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds_dsoftbus/dsoftbus/RuntimeProbeAccess.hpp>
#include <socket.h>

namespace nohandle_sdk {
std::atomic<unsigned> sockets{0}, listens{0}, binds{0}, shutdowns{0};
std::atomic<int> next_id{40};
int socket_error=0, listen_error=0, bind_error=0;
std::function<void()> on_socket;
std::function<void(int,const ISocketListener*)> on_listen,on_bind;
std::mutex gate;
std::vector<const ISocketListener*> listeners;
}
extern "C" {
int32_t Socket(SocketInfo) {
    ++nohandle_sdk::sockets;
    if(nohandle_sdk::on_socket)nohandle_sdk::on_socket();
    return nohandle_sdk::socket_error ? nohandle_sdk::socket_error : nohandle_sdk::next_id++;
}
int32_t Listen(int32_t id,const QosTV*,uint32_t,const ISocketListener* listener) {
    ++nohandle_sdk::listens;
    {std::lock_guard<std::mutex> lock(nohandle_sdk::gate);nohandle_sdk::listeners.push_back(listener);}
    if(nohandle_sdk::on_listen)nohandle_sdk::on_listen(id,listener);
    return nohandle_sdk::listen_error;
}
int32_t Bind(int32_t id,const QosTV*,uint32_t,const ISocketListener* listener) {
    ++nohandle_sdk::binds;
    {std::lock_guard<std::mutex> lock(nohandle_sdk::gate);nohandle_sdk::listeners.push_back(listener);}
    if(nohandle_sdk::on_bind)nohandle_sdk::on_bind(id,listener);
    return nohandle_sdk::bind_error;
}
int32_t SendBytes(int32_t,const void*,uint32_t){return 0;}
void Shutdown(int32_t){++nohandle_sdk::shutdowns;}
}

namespace {
namespace ex=fastdds_dsoftbus::dsoftbus::experimental;
using Transport=eprosima::fastdds::rtps::TransportInterface;
v1::DeviceIdentity Identity(const std::string& epoch,unsigned side) {
    return {"nohandle."+epoch,151,side?"synthetic-B":"synthetic-A"};
}
Locator Local(const std::string& epoch,unsigned side) {
    EndpointId key{};Locator locator;
    Check(bool(v1::AddressCodec::DeriveKey(Identity(epoch,side),key)),"derive V1 key");
    Check(bool(v1::AddressCodec::EncodeLocator({key,17+side,v1::Profile::BytesNative},locator)),"encode V1 locator");
    return locator;
}
std::string Listener(const std::string& epoch,unsigned side) {
    const auto id=Identity(epoch,side);std::string name;
    Check(bool(v1::AddressCodec::EncodeScopedServiceName({id.name_space,id.domain,17+side,v1::Profile::BytesNative},
        {v1::NativeNameContract::PackageScopedV1,"host.public.dds","host.public.dds.v1"},name)),"encode V1 name");
    return name;
}
PublicSocketConfiguration NativeConfig(const std::string& epoch,bool opt_in=true) {
    PublicSocketConfiguration c;
    c.identity={"nohandle."+epoch+".outgoing",{910},{1010}};
    c.listener_identity={Listener(epoch,0),{911},{1011}};
    c.package_name="host.public.dds";c.peer_name=Listener(epoch,1);c.peer_network_id="model.net.B";
    c.listen_qos={{0,80},{1,2000}};c.bind_qos=c.listen_qos;c.max_message_size=8192;
    c.expected_peer_initiator_name="nohandle."+epoch+".peer.outgoing";
    c.immutable_deployment_no_listener_record_reuse=opt_in;return c;
}
DSoftBusTransportDescriptor Deployment(const std::string& epoch) {
    const auto c=NativeConfig(epoch);DSoftBusTransportDescriptor d(8192);
    V1Deployment input{Identity(epoch,0),17,v1::Profile::BytesNative,"library.test",
        {{Identity(epoch,1),18,c.peer_network_id,c.identity.service_name,{c.expected_peer_initiator_name}}}};
    Check(d.ConfigureV1(ScopedHostDeployment(input)).code==DirectoryError::None,"V1 configuration");return d;
}
struct Fixture {
    Receiver receiver;
    ex::Runtime runtime;
    std::shared_ptr<rtps::TransportDescriptorInterface> descriptor;
    std::unique_ptr<Transport> transport;
    Locator local;
    explicit Fixture(const std::string& epoch,bool opt_in=true):runtime(NativeConfig(epoch,opt_in)),
        descriptor(runtime.MakeDescriptor(Deployment(epoch))),local(Local(epoch,0)) {
        Check(bool(descriptor),"public Runtime descriptor");transport.reset(descriptor->create_transport());
        Check(bool(transport),"public Runtime transport");
    }
    bool Open(){return transport->init()&&transport->OpenInputChannel(local,&receiver,8192);}
    bool Close(){transport->shutdown();transport.reset();descriptor.reset();return runtime.Close();}
};
ServiceRecordUsage Usage(){return ServiceRecordTable::Process().SnapshotUsage();}
void Negative(bool same_name) {
    nohandle_sdk::socket_error=-7654;const auto before=Usage();
    for(unsigned round=0;round<12;++round) {
        const auto count=nohandle_sdk::sockets.load();
        {
            Fixture f(same_name?"same":"distinct"+std::to_string(round));
            nohandle_sdk::on_socket=[&]{const auto u=Usage();Check(u.slots_reserved==1&&u.records_created==before.records_created,"Socket has reservation but no issued entrance");};
            Check(!f.Open(),"negative Socket cannot open");
            const auto socket=f.runtime.ObserveSocket();
            const auto& op=socket.operations[std::size_t(PublicSocketOperation::Socket)];
            Check(op.last_call&&op.last_call->called&&op.last_call->returned&&op.last_call->provider_return==-7654&&!op.handle,"raw negative facts");
            Check(!f.Close(),"sticky NativeCallFailed bool preserved");
            nohandle_sdk::on_socket={};
        }
        const auto u=Usage();
        Check(nohandle_sdk::sockets==count+1,"every new Runtime reaches actual Socket");
        Check(u.slots_free==before.slots_free&&u.slots_reserved==0&&u.slots_unanchored==before.slots_unanchored&&u.records_created==before.records_created,"unexposed slot/name reclaimed after destruction");
        Check(nohandle_sdk::listens==0&&nohandle_sdk::binds==0&&nohandle_sdk::shutdowns==0,"no callback API or invented shutdown");
        std::cout<<"round="<<round+1<<" socket="<<nohandle_sdk::sockets<<" free="<<u.slots_free<<" unanchored="<<u.slots_unanchored<<'\n';
    }
}
void Retained(const std::string& which) {
    const auto before=Usage();
    if(which=="unknown")nohandle_sdk::on_socket=[]{throw std::runtime_error("unknown native outcome");};
    if(which=="listen-retained")nohandle_sdk::listen_error=-7655;
    if(which=="bind-retained")nohandle_sdk::bind_error=-7656;
    {Fixture f(which);Check(!f.Open(),"actual setup failure");Check(!f.Close(),"first failure remains observable");}
    const auto u=Usage();
    Check(u.slots_unanchored>before.slots_unanchored&&u.slots_free<before.slots_free,"unknown or published entrance never recycled");
    Check(nohandle_sdk::shutdowns==(which=="unknown"?0:which=="bind-retained"?2:1),"all and only obtained ids retain one shutdown owner");
    Check(nohandle_sdk::listens==(which=="unknown"?0:1)&&nohandle_sdk::binds==(which=="bind-retained"?1:0),"exact API boundary");
    nohandle_sdk::on_socket={};
    nohandle_sdk::socket_error=-7654;
    Fixture again(which);Check(!again.Open()&&nohandle_sdk::sockets==(which=="bind-retained"?2:1),"same exposed/unknown name stays sealed before Socket");again.Close();
}
void Synchronous() {
    unsigned callbacks=0;
    auto check=[&](int id,const ISocketListener* l){
        Check(l&&l->OnBytes&&l->OnShutdown,"callback table published before API");
        const auto u=Usage();Check(u.records_created>0&&u.slots_reserved==0,"both roles exposed before synchronous callback");
        const std::uint8_t value=8;l->OnBytes(id,&value,1);++callbacks;
    };
    nohandle_sdk::on_listen=check;nohandle_sdk::on_bind=check;
    Fixture f("sync");Check(f.Open(),"positive setup despite early bytes");
    Check(callbacks==2&&f.receiver.calls==0,"setup callback has no later receiver delivery");
    Check(f.Close()&&nohandle_sdk::shutdowns==2,"positive ids shut down exactly once");
}
void OldEntry() {
    const ISocketListener* old=nullptr;
    {Fixture f("old");Check(f.Open(),"original open");old=nohandle_sdk::listeners.back();Check(f.Close(),"original close");}
    nohandle_sdk::socket_error=-7654;
    for(unsigned i=0;i<10;++i){Fixture f("negative"+std::to_string(i));Check(!f.Open(),"negative open");f.Close();}
    nohandle_sdk::socket_error=0;
    Fixture current("new");Check(current.Open(),"new positive open");
    const auto now=nohandle_sdk::listeners.back();Check(old!=now&&old->OnBytes!=now->OnBytes,"previous exposed entrance not reused");
    const auto id=current.runtime.ObserveSocket().operations[std::size_t(PublicSocketOperation::Socket)].handle;
    Check(bool(id),"current native id");const std::uint8_t value=9;old->OnBytes(*id,&value,1);
    Check(RuntimeProbeAccess::ObserveRuntime(current.runtime).receive.queued==0,"old callback cannot select a new Runtime");
    old->OnError(*id,-902);Check(!current.runtime.ObserveSocket().first_callback_error,"old callback cannot fault current backend");
    now->OnError(*id,-901);Check(current.runtime.ObserveSocket().first_callback_error==-901,"current callback still reaches its original backend");current.Close();
}
void ConcurrentRollback() {
    nohandle_sdk::socket_error=-7654;const auto before=Usage();
    for(unsigned round=0;round<6;++round) {
        Fixture a("concurrentA"+std::to_string(round)),b("concurrentB"+std::to_string(round));
        Event entered,release;std::atomic<unsigned> arrivals{0};
        nohandle_sdk::on_socket=[&]{if(++arrivals==2)entered.Signal();release.Wait();};
        bool aa=true,bb=true;std::thread x([&]{aa=a.Open();}),y([&]{bb=b.Open();});
        Cleanup join{[&]{release.Signal();if(x.joinable())x.join();if(y.joinable())y.join();}};
        entered.Wait();Check(Usage().slots_reserved==2,"concurrent private reservations");release.Signal();x.join();y.join();
        nohandle_sdk::on_socket={};Check(!aa&&!bb,"both real negative outcomes");a.Close();b.Close();
        Check(Usage().slots_free==before.slots_free&&Usage().slots_unanchored==before.slots_unanchored,"concurrent rollback cannot abandon another record");
    }
    Check(nohandle_sdk::sockets==12&&nohandle_sdk::listens==0&&nohandle_sdk::shutdowns==0,"no callback publication in rollback");
}
void GenericConservative() {
    struct Generic final:NativeBackend {
        unsigned creates=0;
        BackendKind Kind()const noexcept override{return BackendKind::HostMock;}
        BackendIdentity Identity()const override{return {"host.facade.loop",{51},{61}};}
        BackendSocketResult Create(ProviderCallbacks) override {++creates;return {BackendStatus::NativeCallFailed,{},true,true,-7654};}
        BackendCallResult Listen(std::int32_t)override{return BackendStatus::None;}
        BackendCallResult Bind(std::int32_t)override{return BackendStatus::None;}
        BackendSendResult Send(std::int32_t,const std::uint8_t*,std::size_t,std::chrono::steady_clock::time_point)override{return {};}
        BackendCallResult Shutdown(std::int32_t)override{return BackendStatus::None;}
    };
    const auto before=Usage();auto backend=std::make_shared<Generic>();auto d=Config();Receiver receiver;
    NativeTableConfiguration cfg;cfg.directory=d.directory;cfg.directions={RouteDirection::Both};cfg.service_pkg_name="host.public.pkg";
    auto built_native=NativeOwnerTable::Build(cfg);Check(bool(built_native),"generic native table");auto native=std::move(*built_native.value);
    ApplicationAttachments attachments({L(2)});ProviderAdapterDomain domain;BindingRequest q;
    q.service_name="host.facade.loop";q.local=L(2);q.route={L(2),L(3)};q.has_route=true;q.config=cfg;
    q.directory=*EndpointDirectory::Build(cfg.directory).value;q.receiver=&receiver;
    auto built=ProviderAdapter::Build(domain,ServiceRecordTable::Process(),*native,attachments,backend,q);
    Check(bool(built.adapter),"generic adapter");Check(built.adapter->Initialize()!=BackendStatus::None,"generic failure");built.adapter->Close();
    Check(backend->creates==1&&Usage().slots_unanchored==before.slots_unanchored+1,"default backend does not opt in to rollback");
}
void DefaultPublic() {
    nohandle_sdk::socket_error=-7654;const auto before=Usage();
    {Fixture f("default-public",false);Check(!f.Open(),"default public Socket failure");f.Close();}
    Check(Usage().slots_unanchored==before.slots_unanchored+1,"default public keeps early exposure");
    Fixture again("default-public",false);Check(!again.Open()&&nohandle_sdk::sockets==1,"default same name remains sealed");again.Close();
    auto c=NativeConfig("capability");c.bounded_reaccept_host=true;
    Check(!PublicSocketBackend(c).CanCreateBeforeEntrance(),"bounded reaccept never stages");
    c.route_lifecycle=true;
    Check(!PublicSocketBackend(c).CanCreateBeforeEntrance(),"NodeRoute never stages");
}
void PromotionFailure() {
    // Provider-port failure injection after a real C-boundary positive id. This
    // does not pretend that public SDK ConfigureEntrance has a failure knob.
    struct Provider final:NativeBackend {
        std::optional<int> obtained;unsigned configured=0;
        BackendKind Kind()const noexcept override{return BackendKind::HostMock;}
        BackendIdentity Identity()const override{return {"promotion.failure",{51},{61}};}
        bool CanCreateBeforeEntrance()const noexcept override{return true;}
        BackendSocketResult Create(ProviderCallbacks)override{throw std::runtime_error("legacy creation must not run");}
        BackendSocketResult CreateBeforeEntrance(ProviderCallbacks,RecordRole)override {
            obtained=::Socket({});return {BackendStatus::None,*obtained,true,true,{},true};
        }
        BackendStatus ConfigureEntrance(const RecordEntrance&)override{++configured;return BackendStatus::InvalidIdentity;}
        BackendCallResult Listen(std::int32_t)override{throw std::runtime_error("Listen must not run");}
        BackendCallResult Bind(std::int32_t)override{throw std::runtime_error("Bind must not run");}
        BackendSendResult Send(std::int32_t,const std::uint8_t*,std::size_t,std::chrono::steady_clock::time_point)override{return {};}
        BackendCallResult Shutdown(std::int32_t id)override {
            Check(obtained==id,"shutdown owns actual returned id");::Shutdown(id);return {BackendStatus::None,true,true,{}};
        }
    };
    auto backend=std::make_shared<Provider>();auto d=Config();Receiver receiver;
    NativeTableConfiguration cfg;cfg.directory=d.directory;cfg.directions={RouteDirection::Both};cfg.service_pkg_name="host.public.pkg";
    auto built_native=NativeOwnerTable::Build(cfg);Check(bool(built_native),"promotion native table");auto native=std::move(*built_native.value);
    ApplicationAttachments attachments({L(2)});ProviderAdapterDomain domain;BindingRequest q;
    q.service_name="promotion.failure";q.local=L(2);q.route={L(2),L(3)};q.has_route=true;q.config=cfg;
    q.directory=*EndpointDirectory::Build(cfg.directory).value;q.receiver=&receiver;
    auto built=ProviderAdapter::Build(domain,ServiceRecordTable::Process(),*native,attachments,backend,q);
    Check(bool(built.adapter)&&built.adapter->Initialize()==BackendStatus::InvalidIdentity,"positive id then explicit entrance failure");
    Check(backend->configured==1&&nohandle_sdk::sockets==1&&native->SnapshotUsage().owned_ids==1,"original ticket adopted the actual id before promotion");
    Check(built.adapter->Close().adapter_drained&&nohandle_sdk::shutdowns==1,"rollback invokes one real shutdown");
    built.adapter->Close();Check(nohandle_sdk::shutdowns==1&&nohandle_sdk::listens==0&&nohandle_sdk::binds==0,"no duplicate close or setup after promotion failure");
}
}
int main(int argc,char** argv) {
    try {
        Check(argc==2,"case required");const std::string which=argv[1];
        if(which=="negative-distinct")Negative(false);else if(which=="negative-same")Negative(true);
        else if(which=="unknown"||which=="listen-retained"||which=="bind-retained")Retained(which);
        else if(which=="synchronous")Synchronous();else if(which=="old-entry")OldEntry();
        else if(which=="concurrent")ConcurrentRollback();else if(which=="generic")GenericConservative();
        else if(which=="default-public")DefaultPublic();
        else if(which=="promotion-failure")PromotionFailure();
        else throw std::runtime_error("unknown case");
        std::cout<<"PASS public_socket_nohandle "<<which<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL public_socket_nohandle "<<e.what()<<'\n';return 1;}
}
