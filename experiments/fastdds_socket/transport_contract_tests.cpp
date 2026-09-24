#include "ExperimentalTransport.hpp"
#include <cstdio>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using namespace mdds::experiment;
namespace mdds::experiment {
class ContractTestAccess {
public:
    static void BeforeRetire(Control& c,std::function<void()> hook){c.before_final_retire_=std::move(hook);}
};
}
namespace {
using Clock=std::chrono::steady_clock;
struct Fake {
    std::mutex gate;std::condition_variable cv;
    ISocketListener server{},client{};
    bool in_bind=false, release_bind=false;
    int creates=0,listens=0;std::map<int,int> closes;
    std::vector<std::uint8_t> sent;
    Config config;
} fake;
int Create(SocketInfo info) {
    std::lock_guard<std::mutex> lock(fake.gate);
    Require(info.name==fake.config.local_service,"both roles use same service record name");
    Require(info.dataType==DATA_TYPE_BYTES,"only native BYTES");
    return ++fake.creates==1?11:22;
}
int Listen(int socket,const QosTV*,std::uint32_t,const ISocketListener* listener) {
    Require(socket==11,"one listener owner");std::lock_guard<std::mutex> lock(fake.gate);fake.server=*listener;++fake.listens;return 0;
}
int Bind(int socket,const QosTV*,std::uint32_t,const ISocketListener* listener) {
    Require(socket==22,"outbound owner");
    {std::unique_lock<std::mutex> lock(fake.gate);fake.client=*listener;fake.in_bind=true;fake.cv.notify_all();fake.cv.wait(lock,[]{return fake.release_bind;});}
    // Real signature, no hidden fake epoch. Data can reach a native callback
    // before synchronous Bind returns to the worker.
    std::vector<std::uint8_t> early{31,32,33,34};
    listener->OnBytes(socket,early.data(),static_cast<std::uint32_t>(early.size()));
    std::fill(early.begin(),early.end(),0xff);
    return 0;
}
int Send(int socket,const void* data,std::uint32_t size) {
    Require(socket==22||socket==33,"known send owner");
    {std::lock_guard<std::mutex> lock(fake.gate);const auto* p=static_cast<const std::uint8_t*>(data);fake.sent.assign(p,p+size);fake.cv.notify_all();}
    return 0;
}
void Shutdown(int socket) {
    {std::lock_guard<std::mutex> lock(fake.gate);++fake.closes[socket];}
    const auto& table=socket==22?fake.client:fake.server;
    table.OnShutdown(socket,static_cast<ShutdownReason>(0));
}
class Iterator final:public rtps::LocatorsIterator {
public:
    Iterator(const std::vector<Locator>& v,std::size_t p):values(v),position(p){}
    rtps::LocatorsIterator& operator++()override{++position;return *this;}
    bool operator==(const rtps::LocatorsIterator& x)const override{return position==static_cast<const Iterator&>(x).position;}
    bool operator!=(const rtps::LocatorsIterator& x)const override{return !(*this==x);}
    const Locator& operator*()const override{return values[position];}
    const std::vector<Locator>& values;std::size_t position;
};
struct Receiver:rtps::TransportReceiverInterface {
    std::mutex gate;std::condition_variable cv;bool hold=false,entered=false,released=false;std::vector<std::vector<std::uint8_t>> received;
    void OnDataReceived(const std::uint8_t* p,std::uint32_t n,const Locator&,const Locator&)override {
        std::unique_lock<std::mutex> lock(gate);entered=true;cv.notify_all();
        if(hold)cv.wait(lock,[&]{return released;});
        received.emplace_back(p,p+n);cv.notify_all();
    }
    bool WaitCount(std::size_t n) {std::unique_lock<std::mutex> lock(gate);return cv.wait_for(lock,std::chrono::seconds(2),[&]{return received.size()>=n;});}
};
void ParserContracts()
{
    using namespace mdds::experiment;
    std::vector<std::uint8_t> packet(44, 0);
    packet[0]='R'; packet[1]='T'; packet[2]='P'; packet[3]='S'; packet[4]=2; packet[5]=3;
    packet[20]=0x15; packet[21]=1; packet[22]=20;
    packet[32]=0; packet[33]=1; packet[34]=0; packet[35]=0xc2;
    const auto seen = ObserveRtps(packet.data(), packet.size());
    Require(seen.valid && seen.spdp == 1 && seen.user == 0, "SPDP writer observation");
    Require(!ObserveRtps(packet.data(), 35).valid, "truncated submessage rejected");
    packet[32]=0;packet[33]=0;packet[34]=3;packet[35]=0xc2;
    Require(ObserveRtps(packet.data(),packet.size()).sedp_publications==1,"SEDP publications classified");
    packet[34]=4;
    Require(ObserveRtps(packet.data(),packet.size()).sedp_subscriptions==1,"SEDP subscriptions classified");
    packet[35]=2;
    Require(ObserveRtps(packet.data(),packet.size()).user==1,"user writer classified");
    for(std::size_t n=0;n<44;++n)Require(!ObserveRtps(packet.data(),n).valid || n==20,"bounded parser truncated input");
    std::vector<std::uint8_t> info(packet.begin(),packet.begin()+20);
    info.resize(44,0);info[20]=0x0c;info[21]=1;info[22]=20;
    for(unsigned i=0;i<12;++i)info[32+i]=static_cast<std::uint8_t>(i+1);
    info.insert(info.end(),packet.begin()+20,packet.end());
    const auto sourced=ObserveRtps(info.data(),info.size());
    Require(sourced.valid&&sourced.first_writer_guid[0]==1&&sourced.first_writer_guid[11]==12&&sourced.first_writer_guid[15]==2,
        "INFO_SRC must qualify observed writer GUID");
}
void TransportContracts()
{
    using Codec=mdds::dsoftbus::v1::AddressCodec;
    using Profile=mdds::dsoftbus::v1::Profile;
    Config c;c.run_id="host-contract";c.peer_network_id=std::string(64,'a');
    mdds::dsoftbus::EndpointId lk{},pk{};lk[0]=0xff;pk[0]=0;
    Require(Codec::EncodeLocator({lk,7,Profile::BytesNative},c.local_locator),"FF key accepted");
    Require(Codec::EncodeLocator({pk,9,Profile::BytesNative},c.peer_locator),"zero key accepted");
    Require(EncodeNativeServiceName({"experiment",7,7,Profile::BytesNative},c.local_service),"service local");
    Require(EncodeNativeServiceName({"experiment",7,9,Profile::BytesNative},c.peer_service),"service peer");
    fake.config=c;
    auto control=std::make_shared<Control>();
    ExperimentalDescriptor descriptor(c,{Create,Listen,Bind,Send,Shutdown},control);
    ExperimentalTransport transport(descriptor);
    Require(transport.init(),"init");
    Receiver first,second;
    Require(transport.OpenInputChannel(c.local_locator,&first,60000),"first input");
    Require(transport.OpenInputChannel(c.local_locator,&first,60000),"metadata and user share one input");
    Require(!transport.OpenInputChannel(c.local_locator,&second,60000),"different receiver rejected while active");
    Require(fake.listens==1&&fake.creates==1,"one native listener");
    Require(transport.is_locator_reachable(c.peer_locator),"not connected remains reachable");
    rtps::SendResourceList resources;
    Require(transport.OpenOutputChannel(resources,c.local_locator),"output open");
    Require(transport.OpenOutputChannel(resources,c.peer_locator)&&resources.size()==1,"aggregate sender stable");
    std::vector<std::uint8_t> input{1,2,3,4,5,6,7,8,99};
    std::vector<rtps::NetworkBuffer> buffers{{input.data(),3},{input.data()+3,6}};
    const std::vector<Locator> locators{c.peer_locator,c.peer_locator};Iterator begin(locators,0),end(locators,locators.size());
    Require(resources.front()->send(buffers,8,&begin,&end,Clock::now()+std::chrono::seconds(2),0),"owned queue accepted");
    std::fill(input.begin(),input.end(),0xee);
    {std::unique_lock<std::mutex> lock(fake.gate);Require(fake.cv.wait_for(lock,std::chrono::seconds(2),[]{return fake.in_bind;}),"lazy native bind");fake.release_bind=true;fake.cv.notify_all();}
    Require(first.WaitCount(1),"callback before Bind return must keep owned RX");
    {std::lock_guard<std::mutex> lock(first.gate);Require(first.received[0]==std::vector<std::uint8_t>({31,32,33,34}),"RX overwrite cannot alter owned data");}
    {std::unique_lock<std::mutex> lock(fake.gate);Require(fake.cv.wait_for(lock,std::chrono::seconds(2),[]{return !fake.sent.empty();}),"native send executed");Require(fake.sent==std::vector<std::uint8_t>({1,2,3,4,5,6,7,8}),"TX copy uses total_bytes boundary and survives overwrite");}
    fake.client.OnError(999,-7); // Unknown old native ID, not the current owner.
    fake.server.OnError(22,-8); // Correct numeric ID through the wrong role.
    Require(!control->Snapshot().fatal,"unknown/wrong-role OnError must not poison current owner");
    // Affirmative negotiation reserves the native owner under the shared role.
    PeerSocketInfo peer{};peer.name=const_cast<char*>(c.peer_service.c_str());peer.networkId=const_cast<char*>(c.peer_network_id.c_str());peer.dataType=DATA_TYPE_BYTES;
    Require(fake.server.OnNegotiate(33,peer),"accepted owner negotiated before OnBind");
    fake.server.OnBind(33,peer);
    Require(fake.server.OnBytes!=fake.client.OnBytes,"two real role entrances");
    {std::lock_guard<std::mutex> lock(first.gate);first.hold=true;first.entered=false;}
    std::vector<std::uint8_t> old{41,42,43};fake.server.OnBytes(33,old.data(),3);std::fill(old.begin(),old.end(),0xff);
    {std::unique_lock<std::mutex> lock(first.gate);Require(first.cv.wait_for(lock,std::chrono::seconds(2),[&]{return first.entered;}),"old receiver entered");}
    std::vector<std::uint8_t> queued{51,52};fake.server.OnBytes(33,queued.data(),2);
    auto close=std::async(std::launch::async,[&]{return transport.CloseInputChannel(c.local_locator);});
    const auto until=Clock::now()+std::chrono::seconds(2);
    while(transport.IsInputChannelOpen(c.local_locator)&&Clock::now()<until)std::this_thread::yield();
    Require(!transport.IsInputChannelOpen(c.local_locator),"close forbids new lease");
    Require(close.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout,"close waits old receiver lease");
    fake.server.OnBytes(33,queued.data(),2); // An actual no-app record, never cached for second.
    Require(!transport.OpenInputChannel(c.local_locator,&second,60000),"cannot reopen while draining");
    {std::lock_guard<std::mutex> lock(first.gate);first.released=true;first.cv.notify_all();}
    Require(close.get(),"close completed");
    Require(transport.OpenInputChannel(c.local_locator,&second,60000),"new app after old lease drain");
    std::vector<std::uint8_t> fresh{61,62};fake.server.OnBytes(33,fresh.data(),2);std::fill(fresh.begin(),fresh.end(),0xff);
    Require(second.WaitCount(1),"new receiver delivery");
    {std::lock_guard<std::mutex> lock(first.gate);Require(first.received.size()==2&&first.received.back()==std::vector<std::uint8_t>({41,42,43}),"old callback data and lease remain old");}
    {std::lock_guard<std::mutex> lock(second.gate);Require(second.received.size()==1&&second.received[0]==std::vector<std::uint8_t>({61,62}),"old/no-app records not retargeted");}
    Require(transport.CloseInputChannel(c.local_locator),"close second");
    transport.shutdown();transport.shutdown();
    const auto s=control->Snapshot();
    Require(s.clean_observed&&s.closed&&s.tx_worker_joined&&s.rx_worker_joined,"observable normal cleanup");
    Require(s.native_owners==3&&s.shutdown_returns==3,"all distinct owners closed");
    Require(s.no_app_drops==1&&s.receiver_leases==0&&s.tx_bytes==0&&s.rx_bytes==0,"leases and budget returned");
    for(const auto& p:fake.closes)Require(p.second==1,"idempotent unique native close responsibility");
    fake.client.OnBytes(22,fresh.data(),2);
    fake.client.OnError(22,-9);
    Require(!control->Snapshot().fatal,"retired OnError cannot poison closed experiment");
    Require(control->Snapshot().receiver_calls==3,"tombstone drops late native callback");
    control->PrintSummary();
}
void LateBindCloseContract()
{
    using Codec=mdds::dsoftbus::v1::AddressCodec;
    using Profile=mdds::dsoftbus::v1::Profile;
    Config c;c.run_id="late-bind-close";c.peer_network_id=std::string(64,'a');
    mdds::dsoftbus::EndpointId lk{},pk{};lk[0]=1;pk[0]=2;
    Require(Codec::EncodeLocator({lk,7,Profile::BytesNative},c.local_locator),"local locator");
    Require(Codec::EncodeLocator({pk,9,Profile::BytesNative},c.peer_locator),"peer locator");
    Require(EncodeNativeServiceName({"experiment",7,7,Profile::BytesNative},c.local_service),"local service");
    Require(EncodeNativeServiceName({"experiment",7,9,Profile::BytesNative},c.peer_service),"peer service");
    fake.config=c;auto control=std::make_shared<Control>();
    ExperimentalTransport transport(ExperimentalDescriptor(c,{Create,Listen,Bind,Send,Shutdown},control));
    Require(transport.init(),"race init");
    PeerSocketInfo pending{};pending.name=const_cast<char*>(c.peer_service.c_str());pending.networkId=const_cast<char*>(c.peer_network_id.c_str());pending.dataType=DATA_TYPE_BYTES;
    Require(fake.server.OnNegotiate(33,pending),"known accepted reservation before stop");
    ContractTestAccess::BeforeRetire(*control,[&]{
        fake.server.OnError(11,-10); // Closing known owner: diagnostic cannot reopen/fail it.
        PeerSocketInfo peer{};peer.name=const_cast<char*>(c.peer_service.c_str());peer.networkId=const_cast<char*>(c.peer_network_id.c_str());peer.dataType=DATA_TYPE_BYTES;
        // Actual late success for the known reservation enters at retirement.
        // No test-only generation or alternate callback signature is used.
        fake.server.OnBind(33,peer);
    });
    transport.shutdown();
    const auto s=control->Snapshot();
    Require(s.native_owners==2&&s.shutdown_returns==2&&fake.closes[33]==1,
        "accepted owner admitted before retirement must be closed once");
    Require(s.clean_observed&&s.closed,"late bind observable cleanup");
    control->PrintSummary();
}
void NativeServiceNameContract()
{
    using Codec=mdds::dsoftbus::v1::AddressCodec;
    using Profile=mdds::dsoftbus::v1::Profile;
    Config c;c.run_id="native-service-name";c.peer_network_id=std::string(64,'a');
    mdds::dsoftbus::EndpointId lk{},pk{};lk[0]=1;pk[0]=2;
    Require(Codec::EncodeLocator({lk,7,Profile::BytesNative},c.local_locator),"local locator");
    Require(Codec::EncodeLocator({pk,9,Profile::BytesNative},c.peer_locator),"peer locator");
    Require(Codec::EncodeServiceName({"experiment",7,7,Profile::BytesNative},c.local_service),"bare local service");
    Require(Codec::EncodeServiceName({"experiment",7,9,Profile::BytesNative},c.peer_service),"bare peer service");
    fake.config=c;auto control=std::make_shared<Control>();
    ExperimentalTransport bare(ExperimentalDescriptor(c,{Create,Listen,Bind,Send,Shutdown},control));
    Require(!bare.init()&&fake.creates==0,
        "PublicSdkNameAdmissionMismatch: bare md1 name must be rejected before native SDK call");
    const Locator preserved=c.local_locator;
    const auto canonical_local=c.local_service,canonical_peer=c.peer_service;
    Require(EncodeNativeServiceName({"experiment",7,7,Profile::BytesNative},c.local_service),"prefixed local");
    Require(EncodeNativeServiceName({"experiment",7,9,Profile::BytesNative},c.peer_service),"prefixed peer");
    Require(c.local_service=="com.kaihong.mdds."+canonical_local&&c.peer_service=="com.kaihong.mdds."+canonical_peer,"fixed package prefix and canonical payload");
    Require(c.local_locator==preserved,"native deployment name mapping does not mutate locator");
    const auto reject=[&](Config bad){ExperimentalTransport invalid(ExperimentalDescriptor(bad,{Create,Listen,Bind,Send,Shutdown},control));Require(!invalid.init()&&fake.creates==0,"invalid native name rejected before SDK");};
    auto bad=c;bad.local_service="another.package."+canonical_local;reject(bad);
    bad=c;bad.peer_service="com.kaihong.mdds.other."+canonical_peer;reject(bad);
    bad=c;bad.local_service=c.local_service+".extra";reject(bad);
    bad=c;Require(EncodeNativeServiceName({"experiment",7,8,Profile::BytesNative},bad.local_service),"mismatch constructed");reject(bad);
    bad=c;Require(EncodeNativeServiceName({"other",7,9,Profile::BytesNative},bad.peer_service),"namespace mismatch constructed");reject(bad);
    bad=c;Require(EncodeNativeServiceName({"experiment",8,9,Profile::BytesNative},bad.peer_service),"domain mismatch constructed");reject(bad);
    bad=c;Require(EncodeNativeServiceName({"experiment",7,9,Profile::RawPlain},bad.peer_service),"profile mismatch constructed");reject(bad);
    std::string output="unchanged";
    Require(!EncodeNativeServiceName({std::string(108,'a'),7,9,Profile::BytesNative},output)&&output=="unchanged","total native length rejection preserves output");
    Require(EncodeNativeServiceName({std::string(107,'a'),7,9,Profile::BytesNative},output)&&output.size()<=255,"largest fitting namespace remains usable");
    fake.config=c;
    ExperimentalTransport mapped(ExperimentalDescriptor(c,{Create,Listen,Bind,Send,Shutdown},control));
    Require(mapped.init()&&fake.creates==1,"mapped native namespace admitted by fake configuration contract");
    mapped.shutdown();Require(control->Snapshot().clean_observed,"mapped config cleanup");
}
}
int main(int argc,char** argv)
{
    try {if(argc==2&&std::strcmp(argv[1],"--late-bind-close")==0)LateBindCloseContract();else if(argc==2&&std::strcmp(argv[1],"--native-service-name")==0)NativeServiceNameContract();else{ParserContracts();TransportContracts();}std::puts("experimental transport contracts PASS");return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"contract FAIL: %s\n",e.what());return 1;}
}
