#include "HostProviderTestSupport.hpp"
#include <fastdds_dsoftbus/dsoftbus/PublicSocketBackend.hpp>
#include <socket.h>
#include <map>
#include <set>

namespace reaccept_fake {
std::mutex gate;
std::map<int,bool> alive;
std::map<int,unsigned> closes;
int next_dial=20,forced_socket=-1,socket_calls=0,bind_calls=0;
const ISocketListener *server=nullptr,*client=nullptr;
std::function<void()> in_socket,in_bind,in_send;
std::vector<int> sent_ids;
void Terminal(int id,bool outgoing=false){
    {std::lock_guard<std::mutex> lock(gate);alive[id]=false;}
    (outgoing?client:server)->OnShutdown(id,SHUTDOWN_REASON_PEER);
}
}
extern "C" {
int32_t Socket(SocketInfo info){using namespace reaccept_fake;int id;
    {std::lock_guard<std::mutex> lock(gate);id=info.peerName?(forced_socket>=0?forced_socket:next_dial++):10;forced_socket=-1;++socket_calls;alive[id]=true;}
    if(info.peerName&&in_socket)in_socket();return id;
}
int32_t Listen(int32_t,const QosTV*,uint32_t,const ISocketListener* cb){reaccept_fake::server=cb;return 0;}
int32_t Bind(int32_t,const QosTV*,uint32_t,const ISocketListener* cb){reaccept_fake::client=cb;++reaccept_fake::bind_calls;if(reaccept_fake::in_bind)reaccept_fake::in_bind();return 0;}
int32_t SendBytes(int32_t id,const void*,uint32_t){if(reaccept_fake::in_send)reaccept_fake::in_send();std::lock_guard<std::mutex> lock(reaccept_fake::gate);reaccept_fake::sent_ids.push_back(id);return 0;}
void Shutdown(int32_t id){std::lock_guard<std::mutex> lock(reaccept_fake::gate);Check(reaccept_fake::alive[id],"no unknown or duplicate public Shutdown");reaccept_fake::alive[id]=false;++reaccept_fake::closes[id];}
}
namespace {
struct Peer {
    std::string name="model.peer",network="model.net",package="host.reaccept";
    PeerSocketInfo Get(TransDataType type=DATA_TYPE_BYTES){return {name.data(),network.data(),package.data(),type};}
};
struct Fixture {
    Receiver receiver;Peer peer;DSoftBusTransportDescriptor config=Config();
    std::shared_ptr<PublicSocketBackend> backend;std::shared_ptr<HostProviderLoop> loop;
    std::unique_ptr<DSoftBusTransport> facade;rtps::SendResourceList senders;
    explicit Fixture(bool enabled=true,std::shared_ptr<ProcessReceiveObserver> observer={}){
        PublicSocketConfiguration c;c.identity={"model.dial",{701},{801}};c.listener_identity={"host.loop.local",{702},{802}};
        c.package_name="host.reaccept";c.peer_name="model.remote";c.peer_network_id="model.net";c.bind_qos={{0,64}};c.listen_qos=c.bind_qos;c.max_message_size=64;c.bounded_reaccept_host=enabled;
        backend=std::make_shared<PublicSocketBackend>(c);loop=std::make_shared<HostProviderLoop>(backend,std::move(observer));facade=HostFacadeTestAccess::Create(config,loop);
        Check(facade->init()&&facade->OpenInputChannel(L(2),&receiver,64)&&facade->OpenOutputChannel(senders,L(3)),"real Host facade/owner/adapter/binding setup");
    }
    ~Fixture(){reaccept_fake::in_socket={};reaccept_fake::in_bind={};reaccept_fake::in_send={};if(facade)facade->shutdown();}
    bool Prepare(int id,Peer* override_peer=nullptr){
        const bool ok=reaccept_fake::server->OnNegotiate(id,(override_peer?*override_peer:peer).Get());
        if(ok){std::lock_guard<std::mutex> lock(reaccept_fake::gate);reaccept_fake::alive[id]=true;}return ok;
    }
    void Commit(int id){reaccept_fake::server->OnBind(id,peer.Get());Check(loop->Observe().adapter.accepted_phase==AcceptedPhase::Active,"actual accepted publication");}
    void Accept(int id){Check(Prepare(id),"fresh candidate admitted");Commit(id);}
    void Bytes(int id,unsigned char value){reaccept_fake::server->OnBytes(id,&value,1);}
    void Retire(int id){reaccept_fake::Terminal(id);(void)loop->PumpReceive();}
    void Enqueue(unsigned char value){std::vector<rtps::NetworkBuffer> buffers{{&value,1}};std::vector<Locator> peers{L(3)};rtps::Locators begin(peers.begin()),end(peers.end());Check(senders.front()->send(buffers,1,&begin,&end,std::chrono::steady_clock::now()+2s,0),"real queued sender resource");}
    void Close(){facade->shutdown();auto v=loop->Observe();Check(v.close.status==BackendStatus::None&&v.close.cleanup_status==BackendStatus::None&&v.close.adapter_drained&&v.receive.records==0&&v.tx.records==0&&v.native.callbacks==0&&v.native.receives==0,"settled real adapter resources and original close success");}
};
void DefaultGuard(){Fixture f(false);f.Accept(30);f.Retire(30);Check(!f.Prepare(31),"default NO_REACCEPT remains");f.Close();}
void FreshAndOld(){Fixture f;f.Accept(30);auto before=f.loop->Observe();f.Bytes(30,1);Check(f.loop->Observe().receive.records==1,"old owned queue exists");f.Retire(30);Check(f.receiver.calls==0&&f.loop->Observe().receive.records==0,"old queue cancelled not delivered");f.Accept(31);f.Bytes(30,8);reaccept_fake::server->OnShutdown(30,SHUTDOWN_REASON_PEER);f.Bytes(31,2);Check(f.loop->PumpReceive()==BackendStatus::None&&f.receiver.calls==1&&f.receiver.bytes[0]==2,"new events live, old uncaptured callback cannot alias current route");auto after=f.loop->Observe();Check(after.adapter.accepted.owner_incarnation!=before.adapter.accepted.owner_incarnation&&after.adapter.binding.owner_incarnation==before.adapter.binding.owner_incarnation,"fresh accepted owner; opposite dial unchanged");f.Close();}
void ReusedIds(){Fixture f;f.Accept(30);f.Retire(30);Check(!f.Prepare(30),"retained owned ID rejected");Check(f.Prepare(31),"prepared candidate");reaccept_fake::server->OnNegotiate2(31,f.peer.Get(),nullptr,nullptr);(void)f.loop->PumpReceive();Check(!f.Prepare(31),"abandoned SDK-visible prepared ID rejected");Check(reaccept_fake::closes[31]==0,"uncommitted ID never blindly Shutdown");f.Accept(32);f.Close();}
void WrongPeer(){Fixture f;Peer bad;bad.network="wrong.network";Check(!f.Prepare(40,&bad),"wrong route rejected");Check(!f.Prepare(40),"denied ID cannot become future valid connection");auto p=f.peer.Get(DATA_TYPE_RAW_STREAM);Check(!reaccept_fake::server->OnNegotiate(41,p),"wrong data profile rejected");Check(!f.Prepare(41),"invalid-profile candidate ID retained");f.Accept(42);f.Close();}
void Candidates(){Fixture f;std::atomic<int> wins{0},winner{-1};std::thread a([&]{if(f.Prepare(30)){++wins;winner=30;}}),b([&]{if(f.Prepare(31)){++wins;winner=31;}});a.join();b.join();Check(wins==1,"only one concurrent preparation wins");f.Commit(winner);auto old=f.loop->Observe().adapter.accepted.owner_incarnation;Check(!f.Prepare(32),"candidate cannot overwrite active owner");Check(f.loop->Observe().adapter.accepted.owner_incarnation==old,"active capability preserved");f.Close();}
void Capacity(){Fixture f;int id=30;f.Accept(id);unsigned admitted=1;for(;;){f.Retire(id);++id;if(!f.Prepare(id))break;f.Commit(id);++admitted;Check(admitted<=12,"finite retained owner cells");}auto v=f.loop->Observe();Check(v.native.owners==v.native.limits.total_owners&&!v.native.admission_sealed,"exhaustion retained, healthy lanes not sealed");auto before=reaccept_fake::sent_ids.size();f.Enqueue(3);Check(f.loop->PumpSend().outcome==NativeSendOutcome::ReturnedSdkCode&&reaccept_fake::sent_ids.size()==before+1,"opposite dial continues at accept capacity");f.Close();}
void OldSend(){Fixture f;f.Accept(30);f.Enqueue(7);reaccept_fake::Terminal(20,true);(void)f.loop->PumpReceive();Check(f.loop->Observe().adapter.binding.socket==21,"control automatically creates fresh dial");auto result=f.loop->PumpSend();Check(result.outcome==NativeSendOutcome::NotEntered&&reaccept_fake::sent_ids.empty(),"queued original generation never retargets replacement");f.Enqueue(9);Check(f.loop->PumpSend().outcome==NativeSendOutcome::ReturnedSdkCode&&reaccept_fake::sent_ids==std::vector<int>{21},"fresh generation sends on new ID");f.Close();}
void EnteredSend(){Fixture f;f.Accept(30);Event entered,release;reaccept_fake::in_send=[&]{entered.Signal();release.Wait();};f.Enqueue(1);auto send=std::async(std::launch::async,[&]{return f.loop->PumpSend();});entered.Wait();reaccept_fake::Terminal(20,true);auto before=reaccept_fake::socket_calls;(void)f.loop->PumpReceive();Check(reaccept_fake::socket_calls==before&&f.loop->Observe().native.send.entered==1,"entered original send keeps responsibility before renewal");release.Signal();Check(send.get().outcome==NativeSendOutcome::ReturnedSdkCode,"original entered send completes");reaccept_fake::in_send={};(void)f.loop->PumpReceive();Check(reaccept_fake::socket_calls==before+1,"renewal after original send settlement");f.Close();}
void EnteredReceiver(){Fixture f;f.Accept(30);Event entered,release;f.receiver.pause=[&]{entered.Signal();release.Wait();};f.Bytes(30,1);auto rx=std::async(std::launch::async,[&]{return f.loop->PumpReceive();});entered.Wait();reaccept_fake::Terminal(30);auto control=std::async(std::launch::async,[&]{return f.loop->PumpReceive();});Check(control.wait_for(200ms)==std::future_status::ready,"control defers rather than blocks entered receiver");(void)control.get();Check(!f.Prepare(31),"cannot publish while original receiver entered");release.Signal();Check(rx.get()==BackendStatus::None,"original receiver completes");f.receiver.pause={};(void)f.loop->PumpReceive();f.Accept(32);f.Bytes(32,2);Check(f.loop->PumpReceive()==BackendStatus::None&&f.receiver.calls==2,"new receiver work permitted after settlement");f.Close();}
void LateDuringCreate(){Fixture f;f.Accept(30);reaccept_fake::Terminal(20,true);reaccept_fake::in_socket=[&]{reaccept_fake::client->OnShutdown(20,SHUTDOWN_REASON_PEER);};(void)f.loop->PumpReceive();Check(f.loop->Observe().adapter.binding.socket==21&&!f.loop->Observe().adapter.outgoing_stopped,"old shutdown during creation cannot poison new owner");f.Enqueue(1);Check(f.loop->PumpSend().outcome==NativeSendOutcome::ReturnedSdkCode,"new dial stays legal");f.Close();}
void CloseOnce(){Fixture f;f.Accept(30);f.Retire(30);f.Accept(31);f.Close();f.Close();Check(reaccept_fake::closes[10]==1&&reaccept_fake::closes[20]==1&&reaccept_fake::closes[31]==1&&reaccept_fake::closes[30]==0,"owned close once, terminal observed owner not reclosed");}
void AmbiguousFirstEffect(){Fixture f;reaccept_fake::Terminal(20,true);reaccept_fake::in_socket=[&]{reaccept_fake::client->OnShutdown(55,SHUTDOWN_REASON_PEER);};Check(f.loop->PumpReceive()==BackendStatus::BindingRejected,"ambiguous first effect refuses renewal");Check(!f.Prepare(21)&&!f.Prepare(55),"both callback ID and distinct positive return ID remain quarantined");Check(reaccept_fake::closes[21]==0&&reaccept_fake::closes[55]==0,"no inferred Shutdown right for ambiguous IDs");}
void FailedDialKeepsAccept(){Fixture f;f.Accept(30);reaccept_fake::Terminal(20,true);reaccept_fake::forced_socket=20;Check(f.loop->PumpReceive()==BackendStatus::BindingRejected,"reused dial return refused");f.Bytes(30,3);Check(f.loop->PumpReceive()==BackendStatus::None&&f.receiver.calls==1,"healthy opposite accepted still delivers");f.Retire(30);f.Accept(31);f.Bytes(31,4);Check(f.loop->PumpReceive()==BackendStatus::None&&f.receiver.calls==2,"failed dial does not stop accepted replacement control");Check(reaccept_fake::closes[20]==0,"ambiguous reused handle not blindly closed");}
template<class Predicate>void Until(Predicate p){const auto end=std::chrono::steady_clock::now()+3s;while(!p()){Check(std::chrono::steady_clock::now()<end,"bounded state transition");std::this_thread::yield();}}
struct CopyBarrier final:ProcessReceiveObserver {Event entered,release;void OnReceive(ReceiveStage stage,const ReceiveDiagnostic&,const std::uint8_t*,std::size_t)noexcept override{if(stage==ReceiveStage::Captured){entered.Signal();release.Wait();}}};
void EnteredCopy(){auto observer=std::make_shared<CopyBarrier>();Fixture f(true,observer);f.Accept(30);std::thread copying;Cleanup join{[&]{observer->release.Signal();if(copying.joinable())copying.join();}};copying=std::thread([&]{f.Bytes(30,1);});observer->entered.Wait();reaccept_fake::Terminal(30);Check(f.loop->PumpReceive()==BackendStatus::NoWork&&!f.Prepare(31),"copy credit blocks replacement admission");observer->release.Signal();copying.join();(void)f.loop->PumpReceive();f.Accept(32);f.Bytes(32,2);Check(f.loop->PumpReceive()==BackendStatus::None&&f.receiver.calls==1&&f.receiver.bytes[0]==2,"captured old copy cancelled; new copy delivered");f.Close();}
void CloseDuringBind(){Fixture f;f.Accept(30);Event entered,release;reaccept_fake::Terminal(20,true);reaccept_fake::in_bind=[&]{entered.Signal();release.Wait();};std::thread control,closing;std::atomic<bool> done{false};BackendStatus result=BackendStatus::None;Cleanup join{[&]{release.Signal();if(control.joinable())control.join();if(closing.joinable())closing.join();}};control=std::thread([&]{result=f.loop->PumpReceive();});entered.Wait();closing=std::thread([&]{f.facade->shutdown();done=true;});Until([&]{return f.loop->Observe().sealed;});Check(!done&&reaccept_fake::closes[21]==0,"close cannot release entered new Bind");release.Signal();control.join();closing.join();reaccept_fake::in_bind={};Check(result!=BackendStatus::None&&f.loop->Observe().closed,"closing control suppresses successful replacement");Check(reaccept_fake::closes[21]==1,"losing fresh dial retains exact close responsibility");}
void CloseDuringPublication(){Fixture f;Check(f.Prepare(30),"prepared candidate");struct Context {Event entered,release;} ctx;f.backend->SetAcceptedCommitHookForTest([](void* p)noexcept{auto& c=*static_cast<Context*>(p);c.entered.Signal();c.release.Wait();},&ctx);std::thread callback,closing;std::atomic<bool> done{false};Cleanup join{[&]{ctx.release.Signal();if(callback.joinable())callback.join();if(closing.joinable())closing.join();f.backend->SetAcceptedCommitHookForTest(nullptr,nullptr);}};callback=std::thread([&]{reaccept_fake::server->OnBind(30,f.peer.Get());});ctx.entered.Wait();closing=std::thread([&]{f.facade->shutdown();done=true;});Until([&]{return f.loop->Observe().sealed;});Check(!done&&reaccept_fake::closes[30]==0,"close waits owned candidate callback before publication");ctx.release.Signal();callback.join();closing.join();f.backend->SetAcceptedCommitHookForTest(nullptr,nullptr);Check(f.loop->Observe().adapter.accepted_activation_count==0&&reaccept_fake::closes[30]==1,"losing candidate never publishes and closes once");}
}
int main(int argc,char** argv){try{Check(argc==2,"case required");std::string c=argv[1];if(c=="entered-copy")EnteredCopy();else if(c=="close-bind")CloseDuringBind();else if(c=="close-publication")CloseDuringPublication();else if(c=="ambiguous-first")AmbiguousFirstEffect();else if(c=="failed-dial-accept")FailedDialKeepsAccept();else if(c=="default")DefaultGuard();else if(c=="fresh-old")FreshAndOld();else if(c=="reused-ids")ReusedIds();else if(c=="wrong-peer")WrongPeer();else if(c=="candidates")Candidates();else if(c=="capacity")Capacity();else if(c=="old-send")OldSend();else if(c=="entered-send")EnteredSend();else if(c=="entered-receiver")EnteredReceiver();else if(c=="late-create")LateDuringCreate();else if(c=="close-once")CloseOnce();else throw std::runtime_error("unknown case");std::cout<<"PASS BOUNDED_REACCEPT_HOST "<<c<<std::endl;return 0;}catch(const std::exception& e){std::cerr<<"FIRST_FAILURE "<<e.what()<<std::endl;return 1;}}
