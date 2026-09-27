#include "HostProviderTestSupport.hpp"
#include <mdds/dsoftbus/PublicSocketBackend.hpp>
#include <socket.h>
#include <algorithm>

namespace accepted_fake {
int sockets=0,listens=0,binds=0,sends=0;
int bind_result=0;bool throw_shutdown=false;
std::function<void()> during_bind;
const ISocketListener* server=nullptr;
const ISocketListener* client=nullptr;
std::vector<int32_t> shutdowns;
std::vector<std::uint8_t> sent;
std::array<bool,3> alive{};
std::size_t Index(int32_t id){if(id==10)return 0;if(id==20)return 1;if(id==30)return 2;throw std::runtime_error("unknown Fake SDK ID");}
void AcceptedCreated(){alive[2]=true;}
void AcceptedStopped(){alive[2]=false;server->OnShutdown(30,SHUTDOWN_REASON_PEER);}
}
extern "C" {
int32_t Socket(SocketInfo info){++accepted_fake::sockets;const auto id=info.peerName?20:10;auto& live=accepted_fake::alive[accepted_fake::Index(id)];Check(!live,"Fake ID still active");live=true;return id;}
int32_t Listen(int32_t id,const QosTV*,uint32_t,const ISocketListener* listener){Check(id==10,"listener socket");++accepted_fake::listens;accepted_fake::server=listener;return 0;}
int32_t Bind(int32_t id,const QosTV*,uint32_t,const ISocketListener* listener){Check(id==20,"outgoing socket");++accepted_fake::binds;accepted_fake::client=listener;if(accepted_fake::during_bind)accepted_fake::during_bind();return accepted_fake::bind_result;}
int32_t SendBytes(int32_t id,const void* data,uint32_t size){Check(id==20,"only outgoing may send");++accepted_fake::sends;auto p=static_cast<const std::uint8_t*>(data);accepted_fake::sent.assign(p,p+size);return 0;}
void Shutdown(int32_t id){auto& live=accepted_fake::alive[accepted_fake::Index(id)];Check(live,"no duplicate or uncreated Fake Shutdown");live=false;accepted_fake::shutdowns.push_back(id);if(accepted_fake::throw_shutdown)throw std::runtime_error("Fake SDK close exception");}
}
namespace {
struct Peer {
    std::string name="model.peer",network="model.net",package="host.peer.package";
    PeerSocketInfo Info(){PeerSocketInfo p{};p.name=name.data();p.networkId=network.data();p.pkgName=package.data();p.dataType=DATA_TYPE_BYTES;return p;}
};
struct AcceptedFixture {
    DSoftBusTransportDescriptor d=Config();Receiver receiver;
    std::shared_ptr<PublicSocketBackend> backend;
    std::shared_ptr<HostProviderLoop> loop;
    std::unique_ptr<DSoftBusTransport> facade;rtps::SendResourceList senders;
    explicit AcceptedFixture(const std::string& suffix="",std::shared_ptr<ProcessReceiveObserver> observer={}){
        d.directory.locals[0].local_listen_name+=suffix;d.directory.routes[0].fields.local_initiator_name+=suffix;
        PublicSocketConfiguration c;c.identity={"model.dial"+suffix,{810},{811}};
        c.listener_identity={"host.loop.local"+suffix,{812},{813}};
        c.package_name="host.accepted.pkg";c.peer_name="model.remote";c.peer_network_id="model.net";
        c.bind_qos={{QOS_TYPE_MIN_BW,65536}};c.listen_qos=c.bind_qos;c.max_message_size=64;
        backend=std::make_shared<PublicSocketBackend>(c);loop=std::make_shared<HostProviderLoop>(backend,std::move(observer));
        facade=HostFacadeTestAccess::Create(d,loop);
    }
    void Open(){Check(facade->init(),"real injected facade init");Check(facade->OpenInputChannel(L(2),&receiver,64),"dual listener/outgoing input");Check(facade->OpenOutputChannel(senders,L(3)),"real SenderResource");Check(accepted_fake::server&&accepted_fake::client&&accepted_fake::alive[0]&&accepted_fake::alive[1],"actual listener and outgoing calls");}
    void Close(){facade->shutdown();auto v=loop->Observe();Check(v.close.adapter_drained&&v.receive.records==0&&v.receive.active_applications==0&&v.tx.records==0,"real controller settles resources");}
    void Commit(Peer& peer){accepted_fake::AcceptedCreated();Check(accepted_fake::server->OnNegotiate(30,peer.Info()),"valid negotiation must succeed");accepted_fake::server->OnBind(30,peer.Info());}
    void Data(){std::uint8_t b[]={1,4,7};accepted_fake::server->OnBytes(30,b,3);std::memset(b,9,3);Check(loop->PumpReceive()==BackendStatus::None&&receiver.bytes==std::vector<std::uint8_t>({1,4,7}),"accepted owned copy reaches actual receiver");}
};
void ConnectedWindow(bool split){
    AcceptedFixture f;Peer peer;ProviderAdapterDomain domain;
    NativeTableConfiguration config;config.directory=f.d.directory;config.directions={RouteDirection::Both};config.dial_only_egress=true;config.service_pkg_name="mdds.host.loop";
    auto native=NativeOwnerTable::Build(config);Check(bool(native),"native table");ApplicationAttachments attachments({L(2)});
    BindingRequest request;request.service_name=f.backend->Identity().service_name;request.role=RecordRole::Client;request.local=L(2);request.route=config.directory.routes[0].key;request.has_route=true;request.config=config;request.directory=*EndpointDirectory::Build(config.directory).value;request.receiver=&f.receiver;
    auto built=ProviderAdapter::Build(domain,ServiceRecordTable::Process(),**native.value,attachments,f.backend,request);Check(bool(built.adapter),"actual adapter/shim assembly");auto& adapter=*built.adapter;
    Cleanup close{[&]{accepted_fake::during_bind={};(void)adapter.Close();}};
    if(split)accepted_fake::during_bind=[&]{accepted_fake::AcceptedCreated();Check(accepted_fake::server->OnNegotiate(30,peer.Info()),"prepare in actual SDK Bind while deploying");};
    Check(adapter.Initialize()==BackendStatus::None&&adapter.Observe().phase==AdapterPhase::Connected,"exact initialize-to-publish window");
    if(!split){accepted_fake::AcceptedCreated();const bool accepted=accepted_fake::server->OnNegotiate(30,peer.Info());if(!accepted)accepted_fake::alive[2]=false;Check(accepted,"live published listener can negotiate while control is Connected");}
    accepted_fake::server->OnBind(30,peer.Info());
    Check(adapter.Observe().accepted_phase==AcceptedPhase::Active,"prepared owner commits while control is Connected");
    std::uint8_t b=7;accepted_fake::server->OnBytes(30,&b,1);Check(attachments.SnapshotUsage().records==0,"pre-publication bytes still terminal, no future adoption");
    Check(adapter.Publish()==BackendStatus::None,"normal publication");accepted_fake::server->OnBytes(30,&b,1);Check(adapter.DispatchOne()==BackendStatus::None&&f.receiver.calls==1,"new post-publication data only");
    Check(adapter.Close().adapter_drained&&attachments.SnapshotUsage().records==0,"normal original ownership cleanup");
}
void Normal(){
    AcceptedFixture f;f.Open();Peer peer;
    accepted_fake::AcceptedCreated();Check(accepted_fake::server->OnNegotiate(30,peer.Info()),"budget preparation");
    Check(f.loop->Observe().native.owners==3&&f.loop->Observe().native.owned_ids==2,"native budget prepared without handle rights");
    Check(!f.backend->Shutdown(30).called,"prepared number does not grant close");
    std::uint8_t b=8;accepted_fake::server->OnBytes(30,&b,1);
    Check(f.loop->Observe().receive.queued==0,"prepared owner not yet RX active");
    accepted_fake::server->OnBind(30,peer.Info());Check(f.loop->Observe().adapter.accepted_activation_count==1,"actual accepted admission publication");f.Data();
    const auto rejected=f.loop->Observe().adapter.ingress_rejected;
    accepted_fake::server->OnBytes(10,&b,1);accepted_fake::client->OnBytes(20,&b,1);
    Check(f.loop->Observe().receive.queued==0&&f.loop->Observe().adapter.ingress_rejected==rejected+2,"dual-only wrong RX lanes rejected before Binding");
    std::vector<rtps::NetworkBuffer> bufs{{&b,1}};std::vector<Locator> locators{L(3)};rtps::Locators it(locators.begin()),end(locators.end());
    Check(f.senders.front()->send(bufs,1,&it,&end,std::chrono::steady_clock::now()+2s,0),"original sender admission");
    Check(f.loop->PumpSend().actual_return_code==0&&accepted_fake::sends==1,"original ticket reaches Fake SendBytes");
    f.Close();auto ids=accepted_fake::shutdowns;std::sort(ids.begin(),ids.end());Check(ids==std::vector<int32_t>({10,20,30}),"three distinct owned close responsibilities");
}
void RejectAndRetry(){AcceptedFixture f;f.Open();Peer peer;peer.network="wrong";Check(!accepted_fake::server->OnNegotiate(30,peer.Info()),"wrong peer rejected");peer.network="model.net";loop_deny_allocations=true;const bool accepted=accepted_fake::server->OnNegotiate(30,peer.Info());loop_deny_allocations=false;Check(!accepted,"allocation failure refuses preparation");f.Commit(peer);f.Data();f.Close();}
void DuplicateAndForeign(){AcceptedFixture f;f.Open();Peer peer;f.Commit(peer);Check(accepted_fake::server->OnNegotiate(30,peer.Info()),"same committed identity duplicate is idempotent");accepted_fake::server->OnBind(30,peer.Info());accepted_fake::server->OnBind(999,peer.Info());accepted_fake::client->OnBind(30,peer.Info());f.Data();f.Close();Check(accepted_fake::shutdowns.size()==3&&std::find(accepted_fake::shutdowns.begin(),accepted_fake::shutdowns.end(),999)==accepted_fake::shutdowns.end(),"foreign notice never gains Shutdown");}
void EarlyLate(){AcceptedFixture f;f.Open();Peer peer;std::uint8_t b=8;
    accepted_fake::server->OnBytes(30,&b,1);Check(f.loop->Observe().receive.records==0,"pre-negotiation data terminal");
    f.Commit(peer);f.Data();const auto calls=f.receiver.calls.load();accepted_fake::AcceptedStopped();
    accepted_fake::server->OnBind(30,peer.Info());accepted_fake::server->OnBytes(30,&b,1);
    Check(f.loop->Observe().receive.records==0&&f.receiver.calls==calls,"terminal owner never revived");f.Close();
    Check(accepted_fake::shutdowns.size()==2&&std::find(accepted_fake::shutdowns.begin(),accepted_fake::shutdowns.end(),30)==accepted_fake::shutdowns.end(),"observed close no duplicate API call");}
void StopBeforeCommit(){AcceptedFixture f;f.Open();Peer peer;Event prepared,release;std::thread callback;
    Cleanup joined{[&]{release.Signal();if(callback.joinable())callback.join();}};accepted_fake::AcceptedCreated();
    callback=std::thread([&]{Check(accepted_fake::server->OnNegotiate(30,peer.Info()),"prepare");prepared.Signal();release.Wait();accepted_fake::server->OnBind(30,peer.Info());});
    prepared.Wait();accepted_fake::AcceptedStopped();Check(f.loop->CloseAccepted()==BackendStatus::None,"control cancellation of provisional owner");
    Check(f.loop->Observe().native.owners==2,"reservation returned without owner");release.Signal();callback.join();
    Check(f.loop->Observe().adapter.accepted_phase==AcceptedPhase::Closed,"late bind cannot revive");f.Close();
    Check(accepted_fake::shutdowns.size()==2,"uncommitted candidate not closed by adapter");}
void LocalClose(){AcceptedFixture f;f.Open();Peer peer;f.Commit(peer);
    auto before=f.loop->Observe();std::uint8_t b=5;accepted_fake::server->OnBytes(30,&b,1);
    Check(f.loop->Observe().receive.queued==1,"old receive is really queued");
    Check(f.loop->CloseAccepted()==BackendStatus::None,"local accepted close progresses without external pump");
    Check(f.loop->CloseAccepted()==BackendStatus::None,"repeat close idempotent");auto after=f.loop->Observe();
    Check(after.receive.records==0&&f.receiver.calls==0&&after.receive.active_applications==1,"cancel only accepted RX, keep listener claim");
    Check(after.adapter.binding.owner_incarnation==before.adapter.binding.owner_incarnation&&after.adapter.listener.owner_incarnation==before.adapter.listener.owner_incarnation,"unrelated owner identities unchanged");
    Check(accepted_fake::shutdowns==std::vector<int32_t>{30}&&accepted_fake::alive[0]&&accepted_fake::alive[1],"only accepted API responsibility closed");
    accepted_fake::server->OnBytes(30,&b,1);Check(f.loop->Observe().receive.records==0,"no future adoption");f.Close();Check(accepted_fake::shutdowns.size()==3,"final remaining owners close");}
void BorrowedParameters(){AcceptedFixture f;f.Open();Peer original;accepted_fake::AcceptedCreated();
    Check(accepted_fake::server->OnNegotiate(30,original.Info()),"prepare borrowed peer");original.name.assign(280,'x');original.network.assign(80,'y');original.package.assign(80,'z');
    Peer expected;accepted_fake::server->OnBind(30,expected.Info());f.Data();f.Close();}
struct CopyBarrier final:ProcessReceiveObserver {
    Event entered,release;
    void OnReceive(ReceiveStage stage,const ReceiveDiagnostic&,const std::uint8_t*,std::size_t)noexcept override {if(stage==ReceiveStage::Captured){entered.Signal();release.Wait();}}
};
template<class Predicate>void Until(Predicate p){auto end=std::chrono::steady_clock::now()+3s;while(!p()){Check(std::chrono::steady_clock::now()<end,"state predicate deadline");std::this_thread::yield();}}
void Drain(const std::string& mode){auto observer=mode=="copy"?std::make_shared<CopyBarrier>():nullptr;AcceptedFixture f("",observer);f.Open();Peer peer;f.Commit(peer);
    Event entered,release;std::thread producer,pump,close;std::atomic<bool> done=false;
    Cleanup joined{[&]{release.Signal();if(observer)observer->release.Signal();if(producer.joinable())producer.join();if(pump.joinable())pump.join();if(close.joinable())close.join();}};
    if(mode=="running")f.receiver.pause=[&]{Check(f.loop->CloseAccepted()==BackendStatus::InvalidState,"callback cannot synchronously close itself");entered.Signal();release.Wait();};
    std::uint8_t b=3;
    if(observer){producer=std::thread([&]{accepted_fake::server->OnBytes(30,&b,1);});observer->entered.Wait();}
    else{accepted_fake::server->OnBytes(30,&b,1);pump=std::thread([&]{f.loop->PumpReceive();});entered.Wait();}
    close=std::thread([&]{Check(f.loop->CloseAccepted()==BackendStatus::None,"external drain");done=true;});
    Until([&]{return f.loop->Observe().adapter.accepted_phase==AcceptedPhase::Stopping;});
    Check(!done&&accepted_fake::shutdowns.empty(),"close waits original captured activity");
    release.Signal();if(observer)observer->release.Signal();if(producer.joinable())producer.join();if(pump.joinable())pump.join();close.join();
    const auto count=f.receiver.calls.load();accepted_fake::server->OnBytes(30,&b,1);Check(f.loop->Observe().receive.records==0&&f.receiver.calls==count,"no old receiver entry after local drain");f.Close();}
void Repeated(){for(int i=0;i<2;++i){std::weak_ptr<HostProviderLoop> old;{AcceptedFixture f(std::to_string(i));f.Open();Peer peer;f.Commit(peer);f.Data();old=f.loop;f.Close();auto v=f.loop->Observe();Check(v.native.callbacks==0&&v.native.receives==0&&v.receive.active_applications==0&&v.receive.records==0&&v.tx.records==0,"dynamic resources settled; retired native facts separately retained");Check(v.native.owners==3,"bounded terminal native metadata remains, not claimed zero");}Check(old.expired(),"controller lifetime released");}Check(accepted_fake::shutdowns.size()==6,"two complete actual bounded lifecycles");}
void Unknown(){AcceptedFixture f;f.Open();Peer peer;accepted_fake::server->OnBind(999,peer.Info());accepted_fake::server->OnShutdown(999,SHUTDOWN_REASON_PEER);Check(!f.backend->Shutdown(999).called&&accepted_fake::shutdowns.empty(),"unknown numeric event grants no close");f.Commit(peer);f.Data();f.Close();Check(accepted_fake::shutdowns.size()==3,"legitimate closing still works");}
void Negotiation2(){AcceptedFixture f;f.Open();Peer peer;accepted_fake::AcceptedCreated();Check(accepted_fake::server->OnNegotiate(30,peer.Info()),"ordinary prepare");Check(!accepted_fake::server->OnNegotiate2(30,peer.Info(),nullptr,nullptr),"access-info path explicitly unsupported");Check(f.loop->CloseAccepted()==BackendStatus::None&&f.loop->Observe().native.owners==2,"unsupported access negotiation releases provisional budget on control");accepted_fake::alive[2]=false;f.Close();Check(accepted_fake::shutdowns.size()==2,"no claim from unsupported negotiation");}

void CallbackError(){AcceptedFixture f;f.Open();Peer peer;f.Commit(peer);accepted_fake::server->OnError(30,-901);std::uint8_t b=1;accepted_fake::server->OnBytes(30,&b,1);Check(f.loop->Observe().receive.records==0,"accepted error seals only that receive lane");accepted_fake::server->OnError(30,-902);Check(f.loop->Observe().adapter.first_callback_error==-901,"first accepted callback error retained");Check(f.loop->Observe().adapter.binding.setup_complete,"outgoing not closed by accepted error");f.Close();Check(accepted_fake::shutdowns.size()==3,"error is not a native shutdown notification");}
void FirstError(){accepted_fake::bind_result=-903;accepted_fake::throw_shutdown=true;AcceptedFixture f;Check(f.facade->init(),"init");Check(!f.facade->OpenInputChannel(L(2),&f.receiver,64),"actual Bind rejected");auto view=f.loop->Observe();Check(view.failed_open_cleanup&&view.failed_open_cleanup->status==BackendStatus::NativeCallFailed&&view.failed_open_cleanup->cleanup_status==BackendStatus::ProviderException,"cleanup exception must not replace first API failure");Check(f.backend->Observe().first_api_error&&f.backend->Observe().first_api_error->code==-903,"raw first result survives close");f.facade->shutdown();}

void CommitWindow(){AcceptedFixture f;f.Open();Peer peer;accepted_fake::AcceptedCreated();Check(accepted_fake::server->OnNegotiate(30,peer.Info()),"prepare");
    struct Context {Event entered,release;HostProviderLoop* loop;std::atomic<bool> callback_refused=false;} ctx;ctx.loop=f.loop.get();
    f.backend->SetAcceptedCommitHookForTest([](void* value)noexcept{auto& c=*static_cast<Context*>(value);c.callback_refused=c.loop->CloseAccepted()==BackendStatus::InvalidState;c.entered.Signal();c.release.Wait();},&ctx);
    std::thread callback,close;std::atomic<bool> done=false;Cleanup joined{[&]{ctx.release.Signal();if(callback.joinable())callback.join();if(close.joinable())close.join();f.backend->SetAcceptedCommitHookForTest(nullptr,nullptr);}};
    callback=std::thread([&]{accepted_fake::server->OnBind(30,peer.Info());});ctx.entered.Wait();Check(ctx.callback_refused&&f.loop->Observe().native.owned_ids==3,"commit exists but callback control cannot self-wait");
    accepted_fake::AcceptedStopped();close=std::thread([&]{Check(f.loop->CloseAccepted()==BackendStatus::None,"settle committed terminal window");done=true;});Until([&]{return f.loop->Observe().adapter.accepted_closing;});
    Check(!done&&accepted_fake::shutdowns.empty(),"close cannot pass commit-to-capability event");ctx.release.Signal();callback.join();close.join();f.backend->SetAcceptedCommitHookForTest(nullptr,nullptr);
    Check(f.loop->Observe().adapter.accepted_phase==AcceptedPhase::Closed&&f.loop->Observe().adapter.accepted_activation_count==0,"terminal commit never published, including transient activation");f.Close();Check(accepted_fake::shutdowns.size()==2,"observed accepted has no second close responsibility");}

void ObservedAll(){AcceptedFixture f;f.Open();Peer peer;f.Commit(peer);f.Data();accepted_fake::AcceptedStopped();accepted_fake::alive[1]=false;accepted_fake::client->OnShutdown(20,SHUTDOWN_REASON_PEER);f.Close();Check(f.backend->Observe().closed,"all owned handles terminal must be reflected by shim closed observation");Check(accepted_fake::shutdowns==std::vector<int32_t>{10},"only unnotified listener requires explicit Shutdown");}

void NoAllocationDrain(){AcceptedFixture f;f.Open();Peer peer;f.Commit(peer);std::uint8_t b=3;accepted_fake::server->OnBytes(30,&b,1);Check(f.loop->Observe().receive.queued==1,"real queued record");accepted_fake::shutdowns.reserve(8);loop_deny_allocations=true;auto result=f.loop->CloseAccepted();loop_deny_allocations=false;Check(result==BackendStatus::None&&f.loop->Observe().receive.records==0&&f.loop->Observe().receive.bytes==0,"close cannot mistake failed allocating take for empty queue");Check(f.receiver.calls==0,"queued cancel does not invoke receiver");f.Close();}

}
int main(int argc,char** argv){try{Check(argc==2,"case required");std::string c=argv[1];if(c=="connected-window")ConnectedWindow(false);else if(c=="split-bind-window")ConnectedWindow(true);else if(c=="normal")Normal();else if(c=="reject")RejectAndRetry();else if(c=="duplicate")DuplicateAndForeign();else if(c=="early-late")EarlyLate();else if(c=="stop-before-commit")StopBeforeCommit();else if(c=="local-close")LocalClose();else if(c=="borrow")BorrowedParameters();else if(c=="copy"||c=="running")Drain(c);else if(c=="repeat")Repeated();else if(c=="unknown")Unknown();else if(c=="negotiation2")Negotiation2();else if(c=="callback-error")CallbackError();else if(c=="first-error")FirstError();else if(c=="commit-window")CommitWindow();else if(c=="observed-all")ObservedAll();else if(c=="no-allocation-drain")NoAllocationDrain();else throw std::runtime_error("unknown case");std::cout<<"PASS actual PublicSocketBackend / Fake public API "<<c<<'\n';return 0;}catch(const std::exception& e){loop_deny_allocations=false;std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
