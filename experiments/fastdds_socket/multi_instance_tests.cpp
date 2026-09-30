#include "ExperimentalTransport.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fastdds_dsoftbus::experiment;
namespace fastdds_dsoftbus::experiment {
// Hooks expose adapter scheduling stages, never a fake SDK generation/context.
class ContractTestAccess {
public:
    static void BeforeSend(Control& c,std::function<void()> h){c.before_send_=std::move(h);}
    static void AfterTxSettle(Control& c,std::function<void()> h){c.after_tx_settle_=std::move(h);}
    static void AfterRxSettle(Control& c,std::function<void()> h){c.after_rx_settle_=std::move(h);}
    static void AfterTxAdmit(Control& c,std::function<void(std::weak_ptr<const void>)> h){c.after_tx_admit_=std::move(h);}
};
}
namespace {
using Clock=std::chrono::steady_clock;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
// Arrivals/releases establish order. Time limits guard failed tests only.
struct Stage {
    std::mutex mutex;std::condition_variable cv;unsigned arrived=0;bool released=false;
    void Arrive(){std::lock_guard<std::mutex> l(mutex);++arrived;cv.notify_all();}
    void Hold(){std::unique_lock<std::mutex> l(mutex);++arrived;cv.notify_all();
        if(!cv.wait_for(l,std::chrono::seconds(10),[&]{return released;}))throw std::runtime_error("stage release safety deadline");}
    bool Wait(unsigned count=1){std::unique_lock<std::mutex> l(mutex);return cv.wait_for(l,std::chrono::seconds(10),[&]{return arrived>=count;});}
    void Release(){std::lock_guard<std::mutex> l(mutex);released=true;cv.notify_all();}
};
struct NativeFake {
    std::mutex mutex;
    struct SocketRecord {std::string service;ISocketListener table{};bool server=false;};
    std::map<int,SocketRecord> sockets;std::map<int,int> closes;
    std::map<std::string,std::vector<ISocketListener>> bindings;
    std::map<std::string,int> failed_binds;std::vector<int> sends;
    int next=100,creates=0,listens=0;bool fail_create=false,fail_listen=false;
    std::function<void()> on_create,on_listen;
} native;
int Create(SocketInfo info){
    if(native.on_create)native.on_create();
    std::lock_guard<std::mutex> l(native.mutex);++native.creates;
    if(native.fail_create){native.fail_create=false;return -101;}
    const int id=++native.next;native.sockets[id].service=info.name;return id;
}
int Listen(int socket,const QosTV*,std::uint32_t,const ISocketListener* table){
    if(native.on_listen)native.on_listen();
    std::lock_guard<std::mutex> l(native.mutex);auto& r=native.sockets.at(socket);r.table=*table;r.server=true;++native.listens;
    if(native.fail_listen){native.fail_listen=false;return -102;}return 0;
}
int Bind(int socket,const QosTV*,std::uint32_t,const ISocketListener* table){
    std::lock_guard<std::mutex> l(native.mutex);auto& r=native.sockets.at(socket);r.table=*table;
    native.bindings[r.service].push_back(*table);auto& failures=native.failed_binds[r.service];
    if(failures){--failures;return -103;}return 0;
}
int Send(int socket,const void*,std::uint32_t){
    (void)GetProcessStats(); // Public SDK reentry must not inherit registry lock.
    std::lock_guard<std::mutex> l(native.mutex);native.sends.push_back(socket);return 0;
}
void Shutdown(int socket){
    ISocketListener table{};
    {std::lock_guard<std::mutex> l(native.mutex);++native.closes[socket];const auto f=native.sockets.find(socket);if(f!=native.sockets.end())table=f->second.table;}
    (void)GetProcessStats();
    if(table.OnShutdown)table.OnShutdown(socket,static_cast<ShutdownReason>(0));
}
class Iterator final:public rtps::LocatorsIterator {
public:
    Iterator(const std::vector<Locator>& v,std::size_t at):values(v),position(at){}
    rtps::LocatorsIterator& operator++()override{++position;return *this;}
    bool operator==(const rtps::LocatorsIterator& other)const override{return position==static_cast<const Iterator&>(other).position;}
    bool operator!=(const rtps::LocatorsIterator& other)const override{return !(*this==other);}
    const Locator& operator*()const override{return values[position];}
    const std::vector<Locator>& values;std::size_t position;
};
Config MakeConfig(unsigned slot){
using Codec=fastdds_dsoftbus::dsoftbus::v1::AddressCodec;using Profile=fastdds_dsoftbus::dsoftbus::v1::Profile;
    Config c;c.run_id="multi-"+std::to_string(slot);c.peer_network_id=std::string(64,'a');
fastdds_dsoftbus::dsoftbus::EndpointId local{},peer{};local[0]=1;peer[0]=2;
    Require(Codec::EncodeLocator({local,slot,Profile::BytesNative},c.local_locator),"local locator");
    Require(Codec::EncodeLocator({peer,slot+10,Profile::BytesNative},c.peer_locator),"peer locator");
    Require(EncodeNativeServiceName({"experiment",7,slot,Profile::BytesNative},c.local_service),"local service");
    Require(EncodeNativeServiceName({"experiment",7,slot+10,Profile::BytesNative},c.peer_service),"peer service");return c;
}
struct Receiver final:rtps::TransportReceiverInterface {
    std::mutex mutex;std::condition_variable cv;std::vector<std::vector<std::uint8_t>> messages;
    void OnDataReceived(const std::uint8_t* p,std::uint32_t n,const Locator&,const Locator&)override{
        (void)GetProcessStats(); // Receiver reentry must not inherit process lock.
        std::lock_guard<std::mutex> l(mutex);messages.emplace_back(p,p+n);cv.notify_all();
    }
    bool Wait(std::size_t count){std::unique_lock<std::mutex> l(mutex);return cv.wait_for(l,std::chrono::seconds(10),[&]{return messages.size()>=count;});}
};
struct Fixture {
    Config config;std::shared_ptr<Control> control=std::make_shared<Control>();ExperimentalTransport transport;rtps::SendResourceList resources;
    explicit Fixture(unsigned slot):config(MakeConfig(slot)),transport(ExperimentalDescriptor(config,{Create,Listen,Bind,Send,Shutdown},control)){}
    bool Init(){return transport.init();}
    bool Input(Receiver& r){return transport.OpenInputChannel(config.local_locator,&r,60000);}
    bool Submit(){
        if(!transport.OpenOutputChannel(resources,config.peer_locator))return false;
        const std::uint8_t bytes[]{1,2,3,4,5,6,7,8};const std::vector<rtps::NetworkBuffer> buffers{{bytes,sizeof(bytes)}};
        const std::vector<Locator> destinations{config.peer_locator};Iterator begin(destinations,0),end(destinations,1);
        return resources.front()->send(buffers,sizeof(bytes),&begin,&end,Clock::now()+std::chrono::seconds(10),0);
    }
    int ListenerIdUnlocked()const{for(const auto& i:native.sockets)if(i.second.service==config.local_service&&i.second.server)return i.first;throw std::runtime_error("listener absent");}
    int ListenerId()const{std::lock_guard<std::mutex> l(native.mutex);return ListenerIdUnlocked();}
    ISocketListener Server()const{std::lock_guard<std::mutex> l(native.mutex);return native.sockets.at(ListenerIdUnlocked()).table;}
    PeerSocketInfo Peer()const{PeerSocketInfo p{};p.name=const_cast<char*>(config.peer_service.c_str());p.networkId=const_cast<char*>(config.peer_network_id.c_str());p.dataType=DATA_TYPE_BYTES;return p;}
    bool Accept(int id){const auto table=Server();const bool admitted=table.OnNegotiate(id,Peer());
        if(admitted){{std::lock_guard<std::mutex> l(native.mutex);native.sockets[id]={config.local_service,table,true};}table.OnBind(id,Peer());}return admitted;}
    void Inject(int id,std::initializer_list<std::uint8_t> bytes){std::vector<std::uint8_t> owned(bytes);Server().OnBytes(id,owned.data(),static_cast<std::uint32_t>(owned.size()));std::fill(owned.begin(),owned.end(),0xee);}
};
void FinalEmpty(){
    const auto p=GetProcessStats();Require(p.active_states==0&&p.reserved_workers==0&&p.native_owners==0,"final process active-owner/worker budget settled");
    Require(p.tx_records==0&&p.rx_records==0&&p.tx_bytes==0&&p.rx_bytes==0,"final process task budget settled");
    Require(p.live_tx_blocks==0&&p.live_rx_blocks==0&&p.live_tx_payload_bytes==0&&p.live_rx_payload_bytes==0,"final process live payload budget settled");
}
void SecondInstance(){
    Receiver ra,rb;Fixture a(1),b(2);native.on_create=[] {(void)GetProcessStats();};native.on_listen=[] {(void)GetProcessStats();};
    Require(a.Init()&&a.Input(ra)&&a.Accept(1001),"first independent instance initialized");
    a.Inject(1001,{11});Require(ra.Wait(1),"first works before second creation");
    const bool second=b.Init();if(!second){a.transport.shutdown();Require(false,"second independent transport must initialize without replacing first");}
    Require(b.Input(rb)&&b.Accept(1002),"second input");const auto at=a.Server(),bt=b.Server();
    Require(at.OnBytes!=bt.OnBytes&&at.OnBind!=bt.OnBind&&at.OnShutdown!=bt.OnShutdown&&at.OnNegotiate!=bt.OnNegotiate,"different service records have distinguishable real fixed entries");
    const auto p=GetProcessStats();Require(p.active_states==2&&p.reserved_workers==4&&p.native_owners==4,"two instances have bounded process budget");
    const int creates_before_third=native.creates;Fixture third(3);const bool third_ok=third.Init();third.transport.shutdown();
    Require(!third_ok&&native.creates==creates_before_third,"third simultaneous instance rejected before SDK by explicit process budget");
    const auto after_third=GetProcessStats();Require(after_third.active_states==p.active_states&&after_third.native_owners==p.native_owners&&after_third.exposed_entries==p.exposed_entries,"third rejection preserves both active instance budgets and entries");
    a.Inject(1001,{12});b.Inject(1002,{21});Require(ra.Wait(2)&&rb.Wait(1),"both native entries retain own receiver");
    a.transport.shutdown();b.Inject(1002,{22});Require(rb.Wait(2),"second receiver continues after first stop");b.transport.shutdown();
    Require(ra.messages==std::vector<std::vector<std::uint8_t>>({{11},{12}})&&rb.messages==std::vector<std::vector<std::uint8_t>>({{21},{22}}),"receiver content isolation independent of topic filtering");
    Require(a.control->Snapshot().clean_observed&&b.control->Snapshot().clean_observed,"independent normal cleanup");FinalEmpty();
}
void Conflict(){
    Receiver ra;Fixture a(1);Require(a.Init()&&a.Input(ra)&&a.Accept(1001),"existing instance healthy");
    const auto before=GetProcessStats();const auto own_before=a.control->Snapshot();const int creates=native.creates,listens=native.listens;
    Fixture duplicate(1);const bool accepted=duplicate.Init();duplicate.transport.shutdown();
    const auto separate_name=MakeConfig(2);
    ExperimentalTransport shared_control(ExperimentalDescriptor(separate_name,{Create,Listen,Bind,Send,Shutdown},a.control));
    const bool shared_control_ok=shared_control.init();shared_control.shutdown();
    const auto after=GetProcessStats();const auto own_after=a.control->Snapshot();a.Inject(1001,{31});const bool delivery=ra.Wait(1);
    const bool no_side_effect=native.creates==creates&&native.listens==listens;
    const bool existing_open=native.closes.count(a.ListenerId())==0&&native.closes.count(1001)==0;a.transport.shutdown();
    Require(!accepted&&no_side_effect,"same service rejected before related SDK create/listen");
    Require(!shared_control_ok,"distinct state cannot alias existing instance Control statistics/lifetime");
    Require(own_after.instance_id==own_before.instance_id&&own_after.native_owners==own_before.native_owners&&own_after.listener_creations==own_before.listener_creations&&own_after.receiver_lease_id==own_before.receiver_lease_id&&!own_after.closed,"shared active Control rejection never resets live ownership or lease statistics");
    Require(after.active_states==before.active_states&&after.exposed_entries==before.exposed_entries&&after.tombstone_entries==before.tombstone_entries,"conflict does not pollute active incarnation");
    Require(existing_open&&delivery&&!a.control->Snapshot().fatal,"conflict leaves first owner and receiver intact");FinalEmpty();
}
void Lifecycle(){
    const auto start=GetProcessStats();native.fail_create=true;
    {Fixture failed(1);Require(!failed.Init(),"pre-exposure failure");failed.transport.shutdown();}
    const auto create_failed=GetProcessStats();
    Require(create_failed.free_entries==start.free_entries&&create_failed.tombstone_entries==start.tombstone_entries&&create_failed.active_states==0,"unexposed reservation rollback does not consume slot");
    native.fail_listen=true;ISocketListener retired{};
    {Fixture failed(2);Require(!failed.Init(),"exposed Listen failure");retired=failed.Server();failed.transport.shutdown();}
    const auto listen_failed=GetProcessStats();
    Require(listen_failed.tombstone_entries==start.tombstone_entries+1&&listen_failed.free_entries==start.free_entries-1,"exposed server tombstones; unexposed client reservation returns");
    Receiver receiver;Fixture live(3);Require(live.Init()&&live.Input(receiver)&&live.Accept(1003),"new service after failure");
    const auto current=live.Server();Require(current.OnBytes!=retired.OnBytes,"retired callback entry never rebound");
    const std::uint8_t old[]{91};retired.OnBytes(1003,old,1);retired.OnShutdown(1003,static_cast<ShutdownReason>(0));
    live.Inject(1003,{32});Require(receiver.Wait(1),"correct callback still works after retired entry");
    // Retry has no accepted channel: new upper tasks reuse the same live client table.
    Fixture retry(4);Require(retry.Init(),"retry instance");native.failed_binds[retry.config.local_service]=2;
    Stage settled;ContractTestAccess::AfterTxSettle(*retry.control,[&]{settled.Arrive();});const auto before_bind=GetProcessStats();
    for(unsigned n=1;n<=3;++n)Require(retry.Submit()&&settled.Wait(n),"new upper task and native Bind settle");
    const auto after_bind=GetProcessStats();
    {std::lock_guard<std::mutex> l(native.mutex);const auto& tables=native.bindings[retry.config.local_service];
        Require(tables.size()==3,"three Bind calls");
        Require(tables[0].OnBytes==tables[1].OnBytes&&tables[1].OnBytes==tables[2].OnBytes&&tables[0].OnShutdown==tables[2].OnShutdown,"same active client table repeats identical entrance");}
    Require(after_bind.exposed_entries==before_bind.exposed_entries+1&&after_bind.tombstone_entries==before_bind.tombstone_entries,"repeat Bind exposes one incarnation only");
    live.transport.shutdown();retry.transport.shutdown();
    Require(receiver.messages==std::vector<std::vector<std::uint8_t>>({{32}}),"retired table never routes into new owner");
    for(const auto& close:native.closes)Require(close.second==1,"each created/admitted owner retains unique close duty");FinalEmpty();
}
void WrongEntry(){
    Receiver ra,rb;Fixture a(1),b(2);Require(a.Init()&&b.Init()&&a.Input(ra)&&b.Input(rb)&&b.Accept(1002),"two inputs, B bound first");
    // A still has accepted capacity and the expected peer. Only the native
    // owner collision index can reject B's already-live ID here.
    const auto collision_before=GetProcessStats();const bool collided=a.Server().OnNegotiate(1002,a.Peer());const auto collision_after=GetProcessStats();
    Require(collision_after.native_owners==collision_before.native_owners&&native.closes.count(1002)==0,"foreign owner collision neither changes process ownership nor closes B");
    Require(!collided&&a.Accept(1001),"wrong service entry cannot reserve another instance's live numeric owner");
    const auto at=a.Server(),bt=b.Server();const std::uint8_t wrong[]{99};
    at.OnBytes(1002,wrong,1);at.OnBind(1002,b.Peer());at.OnShutdown(1002,static_cast<ShutdownReason>(0));at.OnError(1002,-99);
    const bool negotiate=at.OnNegotiate(1900,b.Peer());
    a.Inject(1001,{41});b.Inject(1002,{42});Require(ra.Wait(1)&&rb.Wait(1),"correct owners survive wrong ingress");
    const bool clean_a=!a.control->Snapshot().fatal;const bool no_blind_close=native.closes.count(1002)==0&&native.closes.count(1900)==0;
    a.transport.shutdown();at.OnBytes(1002,wrong,1);at.OnShutdown(1002,static_cast<ShutdownReason>(0));at.OnError(1002,-88);
    bt.OnBytes(1001,wrong,1);b.Inject(1002,{43});Require(rb.Wait(2),"B active after A entry retires");b.transport.shutdown();
    Require(!negotiate&&clean_a&&no_blind_close,"wrong slot/owner cannot claim or blindly close ID");
    Require(ra.messages==std::vector<std::vector<std::uint8_t>>({{41}})&&rb.messages==std::vector<std::vector<std::uint8_t>>({{42},{43}}),"wrong and retired entries never enter another receiver");FinalEmpty();
}
void StopIsolation(){
    Fixture a(1),b(2);Require(a.Init()&&b.Init(),"two live transports");Stage selected,settled;
    ContractTestAccess::BeforeSend(*b.control,[&]{selected.Hold();});ContractTestAccess::AfterTxSettle(*b.control,[&]{settled.Arrive();});
    const bool admitted=b.Submit(),at_send=selected.Wait();const auto held=b.control->Snapshot();
    auto stop=std::async(std::launch::async,[&]{a.transport.shutdown();});
    const bool independent=stop.wait_for(std::chrono::seconds(2))==std::future_status::ready;const auto p=GetProcessStats();
    bool b_not_closed=true;{std::lock_guard<std::mutex> l(native.mutex);for(const auto& item:native.sockets)if(item.second.service==b.config.local_service&&native.closes.count(item.first))b_not_closed=false;}
    selected.Release();stop.get();const bool done=settled.Wait();const bool subsequent=b.Submit()&&settled.Wait(2);b.transport.shutdown();
    Require(admitted&&at_send&&held.tx_records==1&&held.live_tx_blocks==1,"B task and payload held at explicit pre-send stage");
    Require(independent&&b_not_closed,"Stop A does not join B worker or close B owner");
    Require(p.active_states==1&&p.reserved_workers==2&&p.tx_records==1&&p.live_tx_blocks==1,"process budget remains nonzero for B after A stop");
    Require(done&&subsequent&&b.control->Snapshot().tx_accepted==2&&b.control->Snapshot().tx_send_returns==2,"unrecreated B accepts a new task after A stop");FinalEmpty();
}
void TxLifetime(){
    Fixture a(1),b(2);Require(a.Init()&&b.Init(),"two TX lifetime instances");Stage admitted,settled;std::weak_ptr<const void> weak;
    ContractTestAccess::AfterTxAdmit(*b.control,[&](std::weak_ptr<const void> block){weak=std::move(block);admitted.Hold();});
    ContractTestAccess::AfterTxSettle(*b.control,[&]{settled.Arrive();});
    auto submit=std::async(std::launch::async,[&]{return b.Submit();});const bool published=admitted.Wait(),task_done=settled.Wait();const auto local=b.control->Snapshot();
    auto stop=std::async(std::launch::async,[&]{a.transport.shutdown();});const bool independent=stop.wait_for(std::chrono::seconds(2))==std::future_status::ready;
    const auto process=GetProcessStats();const bool alive=!weak.expired();admitted.Release();const bool accepted=submit.get();stop.get();b.transport.shutdown();
    Require(published&&task_done&&accepted&&independent,"A stop independent of B submitter payload ownership");
    Require(local.tx_records==0&&local.tx_bytes==0&&local.tx_accepted==1&&alive&&local.live_tx_blocks==1&&local.live_tx_payload_bytes==8,"B task settles once while caller still owns live payload");
    Require(process.tx_records==0&&process.live_tx_blocks==1&&process.live_tx_payload_bytes==8,"process budget distinguishes task settlement from final release");
    Require(weak.expired()&&b.control->Snapshot().tx_accepted==1&&b.control->Snapshot().tx_send_returns==1,"one-time task settlement and final payload release");FinalEmpty();
}
void RxLifetime(){
    Receiver rb;Fixture a(1),b(2);Require(a.Init()&&b.Init()&&b.Input(rb)&&b.Accept(1002),"two RX lifetime instances");Stage settled;
    ContractTestAccess::AfterRxSettle(*b.control,[&]{settled.Hold();});b.Inject(1002,{51,52,53,54,55});
    const bool task_done=settled.Wait();const auto local=b.control->Snapshot();
    auto stop=std::async(std::launch::async,[&]{a.transport.shutdown();});const bool independent=stop.wait_for(std::chrono::seconds(2))==std::future_status::ready;
    const auto process=GetProcessStats();settled.Release();stop.get();b.transport.shutdown();
    Require(task_done&&independent,"A stop independent of B post-receiver payload");
    Require(local.rx_records==0&&local.rx_bytes==0&&local.receiver_leases==0&&local.live_rx_blocks==1&&local.live_rx_payload_bytes==5,"B logical RX task settles before final local record release");
    Require(process.rx_records==0&&process.live_rx_blocks==1&&process.live_rx_payload_bytes==5,"process budget retains B payload after A stop");
    Require(rb.messages==std::vector<std::vector<std::uint8_t>>({{51,52,53,54,55}})&&b.control->Snapshot().receiver_calls==1&&b.control->Snapshot().receiver_returns==1,"copied content and one-time receiver lease settlement");FinalEmpty();
}
}
int main(int argc,char** argv){
    try{Require(argc==2,"one fresh-process mode required");const std::string mode=argv[1];
        if(mode=="second-instance")SecondInstance();else if(mode=="conflict")Conflict();else if(mode=="lifecycle")Lifecycle();
        else if(mode=="wrong-entry")WrongEntry();else if(mode=="stop-isolation")StopIsolation();else if(mode=="tx-lifetime")TxLifetime();else if(mode=="rx-lifetime")RxLifetime();else Require(false,"unknown multi-instance mode");
        std::printf("MULTI_INSTANCE_CONTRACT_PASS mode=%s\n",mode.c_str());return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"MULTI_INSTANCE_CONTRACT_FAIL %s\n",e.what());return 1;}
}
