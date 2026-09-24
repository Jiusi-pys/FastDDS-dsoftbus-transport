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

using namespace mdds::experiment;
namespace mdds::experiment {
// Scheduling hooks expose adapter stages only. Every native callback below
// uses the unchanged public SDK signature and the table supplied to Listen/Bind.
class ContractTestAccess {
public:
    static void BeforeSend(Control& c, std::function<void()> h) { c.before_send_=std::move(h); }
    static void BeforeTxJoin(Control& c, std::function<void()> h) { c.before_tx_join_=std::move(h); }
    static void AfterTxSettle(Control& c, std::function<void()> h) { c.after_tx_settle_=std::move(h); }
    static void AfterRxSettle(Control& c, std::function<void()> h) { c.after_rx_settle_=std::move(h); }
    static void AfterTxAdmit(Control& c, std::function<void(std::weak_ptr<const void>)> h) { c.after_tx_admit_=std::move(h); }
};
}
namespace {
using Clock=std::chrono::steady_clock;
void Require(bool ok, const char* message) { if(!ok) throw std::runtime_error(message); }

// Condition waits are safety guards; the required ordering is established by
// explicit arrivals and releases, never by elapsed sleeps or scheduling luck.
struct Stage {
    std::mutex mutex;
    std::condition_variable changed;
    unsigned arrivals=0;
    bool released=false;
    void Arrive() { std::lock_guard<std::mutex> lock(mutex); ++arrivals; changed.notify_all(); }
    void Hold() {
        std::unique_lock<std::mutex> lock(mutex); ++arrivals; changed.notify_all();
        if(!changed.wait_for(lock,std::chrono::seconds(10),[&]{return released;}))
            throw std::runtime_error("test safety deadline: stage release absent");
    }
    bool Wait(unsigned count=1) {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock,std::chrono::seconds(10),[&]{return arrivals>=count;});
    }
    void Release() { std::lock_guard<std::mutex> lock(mutex); released=true; changed.notify_all(); }
};

struct NativeFake {
    std::mutex mutex;
    ISocketListener server{}, client{};
    std::map<int,int> closes;
    std::vector<std::string> events;
    std::vector<int> sends;
    int creates=0, bind_result=0;
    enum Terminal { None, Closed, Error } bind_terminal=None;
} native;

int Create(SocketInfo) {
    std::lock_guard<std::mutex> lock(native.mutex);
    return ++native.creates==1?11:22;
}
int Listen(int socket,const QosTV*,std::uint32_t,const ISocketListener* table) {
    Require(socket==11,"expected listener socket");
    std::lock_guard<std::mutex> lock(native.mutex); native.server=*table; return 0;
}
int Bind(int socket,const QosTV*,std::uint32_t,const ISocketListener* table) {
    Require(socket==22,"expected outbound socket");
    NativeFake::Terminal terminal;
    int result;
    {
        std::lock_guard<std::mutex> lock(native.mutex);
        native.client=*table; terminal=native.bind_terminal; result=native.bind_result;
        native.events.push_back("bind-enter");
    }
    if(terminal==NativeFake::Closed) table->OnShutdown(socket,static_cast<ShutdownReason>(0));
    if(terminal==NativeFake::Error) table->OnError(socket,-7);
    { std::lock_guard<std::mutex> lock(native.mutex); native.events.push_back("bind-return"); }
    return result;
}
int Send(int socket,const void*,std::uint32_t) {
    std::lock_guard<std::mutex> lock(native.mutex);
    native.events.push_back("send-enter"); native.sends.push_back(socket);
    native.events.push_back("send-return"); return 0;
}
void Shutdown(int socket) {
    ISocketListener table;
    {
        std::lock_guard<std::mutex> lock(native.mutex);
        ++native.closes[socket]; native.events.push_back("shutdown-enter");
        table=socket==22?native.client:native.server;
    }
    if(table.OnShutdown) table.OnShutdown(socket,static_cast<ShutdownReason>(0));
    { std::lock_guard<std::mutex> lock(native.mutex); native.events.push_back("shutdown-return"); }
}
class Iterator final: public rtps::LocatorsIterator {
public:
    Iterator(const std::vector<Locator>& values,std::size_t at):values_(values),at_(at) {}
    rtps::LocatorsIterator& operator++() override {++at_;return *this;}
    bool operator==(const rtps::LocatorsIterator& other) const override {return at_==static_cast<const Iterator&>(other).at_;}
    bool operator!=(const rtps::LocatorsIterator& other) const override {return !(*this==other);}
    const Locator& operator*() const override {return values_[at_];}
private:
    const std::vector<Locator>& values_;
    std::size_t at_;
};
Config MakeConfig(const char* run) {
    using Codec=mdds::dsoftbus::v1::AddressCodec;
    using Profile=mdds::dsoftbus::v1::Profile;
    Config c;c.run_id=run;c.peer_network_id=std::string(64,'a');
    mdds::dsoftbus::EndpointId local{},peer{};local[0]=1;peer[0]=2;
    Require(Codec::EncodeLocator({local,7,Profile::BytesNative},c.local_locator),"local locator");
    Require(Codec::EncodeLocator({peer,9,Profile::BytesNative},c.peer_locator),"peer locator");
    Require(EncodeNativeServiceName({"experiment",7,7,Profile::BytesNative},c.local_service),"local service");
    Require(EncodeNativeServiceName({"experiment",7,9,Profile::BytesNative},c.peer_service),"peer service");
    return c;
}
struct Fixture {
    Config config;
    std::shared_ptr<Control> control=std::make_shared<Control>();
    ExperimentalTransport transport;
    rtps::SendResourceList resources;
    explicit Fixture(const char* name):config(MakeConfig(name)),
        transport(ExperimentalDescriptor(config,{Create,Listen,Bind,Send,Shutdown},control)) {
        Require(transport.init(),"transport init");
        Require(transport.OpenOutputChannel(resources,config.peer_locator),"output resource");
    }
    bool Submit() {
        const std::uint8_t data[]={1,2,3,4,5,6,7,8};
        const std::vector<rtps::NetworkBuffer> buffers{{data,sizeof(data)}};
        const std::vector<Locator> locators{config.peer_locator};
        Iterator begin(locators,0),end(locators,1);
        return resources.front()->send(buffers,sizeof(data),&begin,&end,Clock::now()+std::chrono::seconds(10),0);
    }
    PeerSocketInfo Peer() const {
        PeerSocketInfo p{};p.name=const_cast<char*>(config.peer_service.c_str());
        p.networkId=const_cast<char*>(config.peer_network_id.c_str());p.dataType=DATA_TYPE_BYTES;return p;
    }
};

void BindTerminal(bool error) {
    native.bind_terminal=error?NativeFake::Error:NativeFake::Closed;
    Fixture f(error?"bind-error":"bind-shutdown");
    Stage settled;
    ContractTestAccess::AfterTxSettle(*f.control,[&]{settled.Arrive();});
    const bool admitted=f.Submit();
    const bool first=settled.Wait();
    // A subsequent upper-layer task may be locally accepted then fail. It must
    // neither reconnect nor send through the terminal owner.
    const bool again=f.Submit();
    const bool second=!again||settled.Wait(2);
    f.transport.shutdown();
    Require(admitted&&first&&second,"terminal Bind tasks reached deterministic settlement");
    Require(native.sends.empty(),"Bind rc=0 must not restore READY after native terminal callback");
    Require(native.creates==2,"terminal owner must not cause fresh outbound native admission");
    Require(native.closes[22]==1,"terminal owner retains unique close responsibility");
}

void StopSend() {
    Fixture f("stop-send");Stage selected,joining;
    ContractTestAccess::BeforeSend(*f.control,[&]{selected.Hold();});
    ContractTestAccess::BeforeTxJoin(*f.control,[&]{joining.Arrive();});
    const bool admitted=f.Submit();const bool reached=selected.Wait();
    auto stop=std::async(std::launch::async,[&]{f.transport.shutdown();});
    const bool at_join=joining.Wait();
    std::size_t closes_before_send;
    {std::lock_guard<std::mutex> lock(native.mutex);closes_before_send=native.closes.size();}
    selected.Release();stop.get(); // Release and join before any failing assertion.
    Require(admitted&&reached&&at_join,"selected-send and stop stages reached");
    Require(closes_before_send==0,"Stop must finish unique TX worker before starting native Shutdown");
    const auto sent=std::find(native.events.begin(),native.events.end(),"send-return");
    const auto closed=std::find(native.events.begin(),native.events.end(),"shutdown-enter");
    Require(sent!=native.events.end()&&closed!=native.events.end()&&sent<closed,
        "selected native Send must return before the first local Shutdown enters");
    Require(f.control->Snapshot().clean_observed,"normal observable stop completes");
}

void Reservation(bool bound) {
    Fixture f(bound?"reservation":"reserved-without-bind");
    const auto peer=f.Peer();
    const bool first=native.server.OnNegotiate(33,peer);
    const bool second=native.server.OnNegotiate(44,peer);
    const auto reserved=f.control->Snapshot();
    if(bound) native.server.OnBind(33,peer);
    f.transport.shutdown();
    Require(first&&!second,"accepted capacity must be reserved by first OnNegotiate before any OnBind");
    Require(reserved.native_owners==2,"successful negotiation immediately retains native owner");
    Require(native.closes[33]==1,"admitted socket retains one close duty even without OnBind");
    Require(native.closes.count(44)==0,"rejected or unknown numeric ID must never be blindly closed");
    Require(f.control->Snapshot().clean_observed,"reserved owner participates in normal cleanup");
}

void ReservedTerminal() {
    native.bind_result=-17;
    Fixture f("reserved-terminal");Stage settled;
    ContractTestAccess::AfterTxSettle(*f.control,[&]{settled.Arrive();});
    const auto peer=f.Peer();const bool admitted=native.server.OnNegotiate(33,peer);
    native.server.OnShutdown(33,static_cast<ShutdownReason>(0));
    native.server.OnBind(33,peer); // Late success must not resurrect this reservation.
    const bool queued=f.Submit();const bool done=settled.Wait();
    f.transport.shutdown();
    Require(admitted&&queued&&done,"terminal reservation sequence executed");
    Require(native.sends.empty(),"late OnBind cannot restore a terminal reservation to READY");
    Require(native.closes[33]==1,"terminal reservation remains a known owned socket");
    Require(native.closes.count(44)==0,"no unknown old ID close");
}

void TxLifetime() {
    Fixture f("lifetime-tx");Stage admitted,settled;
    std::weak_ptr<const void> retained;
    ContractTestAccess::AfterTxAdmit(*f.control,[&](std::weak_ptr<const void> block){retained=std::move(block);admitted.Hold();});
    ContractTestAccess::AfterTxSettle(*f.control,[&]{settled.Arrive();});
    auto submit=std::async(std::launch::async,[&]{return f.Submit();});
    const bool published=admitted.Wait();const bool complete=settled.Wait();
    const auto during=f.control->Snapshot();const bool alive=!retained.expired();
    admitted.Release();const bool accepted=submit.get();f.transport.shutdown();
    const auto after=f.control->Snapshot();
    Require(published&&complete&&accepted,"TX lifetime stages reached");
    Require(during.tx_records==0&&during.tx_bytes==0&&alive,"settled TX task can still own payload references");
    Require(during.live_tx_blocks==1&&during.live_tx_payload_bytes==8,
        "TX live payload accounting must survive logical queue settlement");
    Require(retained.expired()&&after.live_tx_blocks==0&&after.live_tx_payload_bytes==0,
        "TX lifetime counters decrement only after final shared payload release");
}

struct Receiver final:rtps::TransportReceiverInterface {
    std::vector<std::uint8_t> received;
    void OnDataReceived(const std::uint8_t* data,std::uint32_t size,const Locator&,const Locator&) override {
        received.assign(data,data+size);
    }
};
void RxLifetime() {
    Fixture f("lifetime-rx");Stage settled;Receiver receiver;
    ContractTestAccess::AfterRxSettle(*f.control,[&]{settled.Hold();});
    Require(f.transport.OpenInputChannel(f.config.local_locator,&receiver,60000),"RX input");
    const auto peer=f.Peer();const bool accepted=native.server.OnNegotiate(33,peer);
    native.server.OnBind(33,peer);
    std::vector<std::uint8_t> payload{1,2,3,4,5};native.server.OnBytes(33,payload.data(),5);
    std::fill(payload.begin(),payload.end(),0xee);
    const bool complete=settled.Wait();const auto during=f.control->Snapshot();
    settled.Release();f.transport.shutdown();const auto after=f.control->Snapshot();
    Require(accepted&&complete,"RX lifetime stages reached");
    Require(receiver.received==std::vector<std::uint8_t>({1,2,3,4,5}),"RX owns original callback bytes");
    Require(during.rx_records==0&&during.rx_bytes==0&&during.receiver_leases==0,
        "RX task and receiver lease settled at explicit stage");
    Require(during.live_rx_blocks==1&&during.live_rx_payload_bytes==5,
        "RX local record retains payload after logical task settlement");
    Require(after.live_rx_blocks==0&&after.live_rx_payload_bytes==0,
        "RX payload accounting reaches zero only after local record destruction");
}
}
int main(int argc,char** argv) {
    try {
        Require(argc==2,"exactly one case required; each case needs fresh callback incarnation process");
        const std::string mode=argv[1];
        if(mode=="bind-shutdown")BindTerminal(false);
        else if(mode=="bind-error")BindTerminal(true);
        else if(mode=="stop-send")StopSend();
        else if(mode=="reservation")Reservation(true);
        else if(mode=="reserved-without-bind")Reservation(false);
        else if(mode=="reserved-terminal")ReservedTerminal();
        else if(mode=="lifetime-tx")TxLifetime();
        else if(mode=="lifetime-rx")RxLifetime();
        else Require(false,"unknown case");
        std::printf("lifecycle hardening %s PASS\n",mode.c_str());return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"lifecycle hardening FAIL: %s\n",e.what());return 1;}
}
