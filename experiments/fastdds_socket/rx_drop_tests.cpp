#include "ExperimentalTransport.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
using namespace fastdds_dsoftbus::experiment;
namespace fastdds_dsoftbus::experiment {
class ContractTestAccess {
public:
    static void AfterRx(Control& c,std::function<void()> f){c.after_rx_settle_=std::move(f);}
};
}
namespace {
void Require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
struct Fake {
    ISocketListener server{};
    unsigned creates=0;
    std::map<int,unsigned> closes;
} fake;
int Create(SocketInfo){++fake.creates;return 11;}
int Listen(int,const QosTV*,std::uint32_t,const ISocketListener* table){fake.server=*table;return 0;}
int Bind(int,const QosTV*,std::uint32_t,const ISocketListener*){return -1;}
int Send(int,const void*,std::uint32_t){return -1;}
void Shutdown(int socket){++fake.closes[socket];fake.server.OnShutdown(socket,static_cast<ShutdownReason>(0));}
Config MakeConfig(bool enabled){
    Config c;c.run_id="rx-drop-contract";c.peer_network_id=std::string(64,'a');c.enable_rx_drop=enabled;
using Codec=fastdds_dsoftbus::dsoftbus::v1::AddressCodec;using Profile=fastdds_dsoftbus::dsoftbus::v1::Profile;
fastdds_dsoftbus::dsoftbus::EndpointId local{},peer{};local[0]=1;peer[0]=2;
    Require(Codec::EncodeLocator({local,7,Profile::BytesNative},c.local_locator),"local locator");
    Require(Codec::EncodeLocator({peer,9,Profile::BytesNative},c.peer_locator),"peer locator");
    Require(EncodeNativeServiceName({"experiment",7,7,Profile::BytesNative},c.local_service),"local service");
    Require(EncodeNativeServiceName({"experiment",7,9,Profile::BytesNative},c.peer_service),"peer service");return c;
}
RxDropRule Rule(){RxDropRule r;for(unsigned i=0;i<12;++i)r.writer_guid[i]=static_cast<std::uint8_t>(i+1);r.writer_guid[14]=1;r.writer_guid[15]=3;r.sample_size=20;return r;}
std::vector<std::uint8_t> Packet(){
    std::vector<std::uint8_t> p(60,0);p[0]='R';p[1]='T';p[2]='P';p[3]='S';p[4]=2;p[5]=3;p[6]=1;p[7]=15;
    const auto rule=Rule();std::copy_n(rule.writer_guid.begin(),12,p.begin()+8);
    p[20]=0x16;p[21]=1;p[22]=36;p[26]=28;
    std::copy(rule.writer_guid.begin()+12,rule.writer_guid.end(),p.begin()+32);
    p[40]=1;p[44]=3;p[48]=1;p[50]=4;p[52]=20;
    p[56]=13;p[57]=14;p[58]=15;p[59]=16;return p;
}
struct Receiver:rtps::TransportReceiverInterface {
    std::vector<std::vector<std::uint8_t>> messages;
    void OnDataReceived(const std::uint8_t* p,std::uint32_t size,const Locator&,const Locator&)override{messages.emplace_back(p,p+size);}
};
struct Settlements {
    std::mutex mutex;std::condition_variable cv;unsigned count=0;
    void Mark(){std::lock_guard<std::mutex> l(mutex);++count;cv.notify_all();}
    bool Wait(unsigned wanted){std::unique_lock<std::mutex> l(mutex);return cv.wait_for(l,std::chrono::seconds(3),[&]{return count>=wanted;});}
};
void Run(const std::string& mode){
    auto control=std::make_shared<Control>();auto config=MakeConfig(true);
    ExperimentalTransport transport(ExperimentalDescriptor(config,{Create,Listen,Bind,Send,Shutdown},control));
#ifndef FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
    const bool started=transport.init();
    if(started)transport.shutdown();
    Require(!started&&fake.creates==0,"disabled build rejects requested injection before SDK create");
    Require(!control->ArmRxDrop(Rule()),"disabled build cannot arm a rule");
#else
    if(mode=="runtime-off")config.enable_rx_drop=false;
    // The disabled-runtime check uses its own immutable descriptor.
    ExperimentalTransport runtime_off(ExperimentalDescriptor(config,{Create,Listen,Bind,Send,Shutdown},control));
    auto& active=mode=="runtime-off"?runtime_off:transport;
    Require(active.init(),"native facade initialized");
    if(mode=="runtime-off"){
        Require(!control->ArmRxDrop(Rule()),"compiled capability still requires explicit config opt-in");active.shutdown();return;
    }
    Receiver receiver;Settlements settled;
    ContractTestAccess::AfterRx(*control,[&]{settled.Mark();});
    Require(active.OpenInputChannel(config.local_locator,&receiver,60000),"input installed");
    PeerSocketInfo peer{};peer.name=const_cast<char*>(config.peer_service.c_str());peer.networkId=const_cast<char*>(config.peer_network_id.c_str());peer.dataType=DATA_TYPE_BYTES;
    Require(fake.server.OnNegotiate(33,peer),"accepted reserved");fake.server.OnBind(33,peer);
    const bool armed=control->ArmRxDrop(Rule());
    if(!armed){active.shutdown();Require(false,"valid explicit rule armed before incoming target");}
    Require(!control->ArmRxDrop(Rule()),"rule cannot be silently replaced");
    std::vector<std::vector<std::uint8_t>> inputs;
    auto p=Packet();p[8]^=0x40;inputs.push_back(p); // wrong GUID
    p=Packet();p[40]=2;inputs.push_back(p); // wrong sequence
    p=Packet();p[44]=2;inputs.push_back(p); // wrong range
    p=Packet();p.resize(56);p[22]=32;inputs.push_back(p); // header without declared payload
    p=Packet();p.resize(84,0);p[60]=0x15;p[61]=1;p[62]=20;inputs.push_back(p); // coalesced DATA
    if(mode!="miss"){
        inputs.push_back(Packet()); // selected whole packet is dropped
        p=Packet();p.resize(56);p[22]=32;inputs.push_back(p); // declared range without payload is not recovery
        inputs.push_back(Packet()); // new, complete native source delivery
    }
    for(auto input:inputs){fake.server.OnBytes(33,input.data(),static_cast<std::uint32_t>(input.size()));std::fill(input.begin(),input.end(),0xee);}
    const bool completed=settled.Wait(static_cast<unsigned>(inputs.size()));
    active.shutdown();
    Require(completed,"every copied task settles without sleeps");
    const auto s=control->Snapshot();const auto drop=control->DropSnapshot();
    const auto expected=mode=="miss"?0u:1u;
    Require(drop.armed&&drop.dropped_messages==expected,"only first complete exact target native message dropped");
    Require(receiver.messages.size()==inputs.size()-expected,"dropped invocation did not reach receiver");
    for(std::size_t i=0;i<5;++i)Require(receiver.messages[i]==inputs[i],"non-target/invalid-shape bytes never filtered or modified");
    if(expected){Require(receiver.messages.back()==Packet(),"later source callback is delivered, no cached replay");Require(drop.recovered_range_deliveries==1,"count receiver-return of same missing range");Require(drop.dropped_invocation>0&&drop.dropped_len==Packet().size()&&drop.dropped_hash!=0,"drop records exact owned packet boundary");}
    else Require(drop.recovered_range_deliveries==0,"no-hit is not recovery");
    Require(s.rx_owned==inputs.size()&&s.receiver_calls==inputs.size()-expected&&s.receiver_returns==s.receiver_calls,"owned-copy and delivery accounting are distinct");
    Require(s.clean_observed&&s.rx_records==0&&s.rx_bytes==0&&s.receiver_leases==0&&s.live_rx_blocks==0&&s.live_rx_payload_bytes==0,"drop path releases task, lease and final payload ownership");
    Require(!control->ArmRxDrop(Rule()),"closed experiment cannot arm another rule");
    Require(fake.closes[11]==1&&fake.closes[33]==1,"every known native owner closes once");
#endif
}
}
int main(int argc,char** argv){try{Run(argc>1?argv[1]:"hit");std::puts("rx drop contract PASS");return 0;}catch(const std::exception& e){std::fprintf(stderr,"rx drop contract FAIL: %s\n",e.what());return 1;}}
