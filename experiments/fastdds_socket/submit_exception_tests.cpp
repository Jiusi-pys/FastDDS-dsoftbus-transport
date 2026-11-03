#include "ExperimentalTransport.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fastdds_dsoftbus::experiment;
namespace fastdds_dsoftbus::experiment {
class ContractTestAccess {
public:
    static void AfterTxAdmit(Control& c,std::function<void(std::weak_ptr<const void>)> h) {c.after_tx_admit_=std::move(h);}
    static void BeforeSend(Control& c,std::function<void()> h) {c.before_send_=std::move(h);}
    static void AfterTxSettle(Control& c,std::function<void()> h) {c.after_tx_settle_=std::move(h);}
};
}
namespace {
using Clock=std::chrono::steady_clock;
void Require(bool condition,const char* message) {
    if(condition)return;
    // An old implementation corrupts its task count, making Stop wait forever.
    // Preserve the assertion failure without presenting forced host-test exit
    // as either native cancellation or normal resource cleanup.
    std::fprintf(stderr,"submit exception FAIL: %s (cleanup NOT_RUN on failed host assertion)\n",message);
    std::fflush(nullptr);std::_Exit(1);
}
struct Stage {
    std::mutex mutex;std::condition_variable changed;bool reached=false,released=false;
    void Arrive() {std::lock_guard<std::mutex> lock(mutex);reached=true;changed.notify_all();}
    void Hold() {std::unique_lock<std::mutex> lock(mutex);reached=true;changed.notify_all();
        Require(changed.wait_for(lock,std::chrono::seconds(10),[&]{return released;}),"stage release safety deadline");}
    void Wait() {std::unique_lock<std::mutex> lock(mutex);
        Require(changed.wait_for(lock,std::chrono::seconds(10),[&]{return reached;}),"stage arrival safety deadline");}
    void Release() {std::lock_guard<std::mutex> lock(mutex);released=true;changed.notify_all();}
};
int created=0,sent=0,closed=0;
int Create(SocketInfo) {return ++created==1?11:22;}
int Listen(int,const QosTV*,std::uint32_t,const ISocketListener*) {return 0;}
int Bind(int,const QosTV*,std::uint32_t,const ISocketListener*) {return 0;}
int Send(int,const void*,std::uint32_t) {++sent;return 0;}
void Shutdown(int) {++closed;}
class Iterator final:public rtps::LocatorsIterator {
public:
    Iterator(const std::vector<Locator>& v,std::size_t i):values(v),index(i){}
    rtps::LocatorsIterator& operator++() override {++index;return *this;}
    bool operator==(const rtps::LocatorsIterator& other) const override {return index==static_cast<const Iterator&>(other).index;}
    bool operator!=(const rtps::LocatorsIterator& other) const override {return !(*this==other);}
    const Locator& operator*() const override {return values[index];}
private:const std::vector<Locator>& values;std::size_t index;
};
Config MakeConfig() {
using Codec=fastdds_dsoftbus::dsoftbus::v1::AddressCodec;using Profile=fastdds_dsoftbus::dsoftbus::v1::Profile;
    Config c;c.run_id="submit-exception";c.peer_network_id=std::string(64,'a');
fastdds_dsoftbus::dsoftbus::EndpointId local{},peer{};local[0]=1;peer[0]=2;
    Require(Codec::EncodeLocator({local,7,Profile::BytesNative},c.local_locator),"local locator");
    Require(Codec::EncodeLocator({peer,9,Profile::BytesNative},c.peer_locator),"peer locator");
    Require(EncodeNativeServiceName({"experiment",7,7,Profile::BytesNative},c.local_service),"local name");
    Require(EncodeNativeServiceName({"experiment",7,9,Profile::BytesNative},c.peer_service),"peer name");return c;
}
void Exercise(bool worker_settled) {
    const auto config=MakeConfig();const auto control=std::make_shared<Control>();
    ExperimentalTransport transport(ExperimentalDescriptor(config,{Create,Listen,Bind,Send,Shutdown},control));
    Require(transport.init(),"transport init");rtps::SendResourceList resources;
    Require(transport.OpenOutputChannel(resources,config.peer_locator),"output resource");
    Stage admitted,selected,settled;std::weak_ptr<const void> payload;
    ContractTestAccess::AfterTxAdmit(*control,[&](std::weak_ptr<const void> block){
        payload=std::move(block);admitted.Hold();throw std::runtime_error("diagnostic after irrevocable admission");});
    if(!worker_settled)ContractTestAccess::BeforeSend(*control,[&]{selected.Hold();});
    ContractTestAccess::AfterTxSettle(*control,[&]{settled.Arrive();});
    const std::uint8_t bytes[]={1,2,3,4,5,6,7,8};
    const std::vector<rtps::NetworkBuffer> buffers{{bytes,sizeof(bytes)}};
    const std::vector<Locator> locators{config.peer_locator};Iterator begin(locators,0),end(locators,1);
    auto submit=std::async(std::launch::async,[&]{return resources.front()->send(buffers,sizeof(bytes),&begin,&end,Clock::now()+std::chrono::seconds(10),0);});
    admitted.Wait();if(worker_settled)settled.Wait();else selected.Wait();
    const auto before=control->Snapshot();
    Require(before.tx_accepted==1&&before.tx_records==(worker_settled?0:1),"explicit pre-throw task stage");
    Require(!payload.expired(),"Submit retains owned payload before throwing hook returns");
    admitted.Release();const bool accepted=submit.get();const auto after=control->Snapshot();
    std::printf("SUBMIT_EXCEPTION mode=%s accepted=%d tx_accepted=%llu tx_rejected=%llu tx_records=%llu tx_bytes=%llu\n",
        worker_settled?"settled":"pending",accepted,static_cast<unsigned long long>(after.tx_accepted),
        static_cast<unsigned long long>(after.tx_rejected),static_cast<unsigned long long>(after.tx_records),
        static_cast<unsigned long long>(after.tx_bytes));std::fflush(stdout);
    // Check accounting before acceptance so RED exhibits both consequences:
    // premature settlement while pending, or unsigned wrap after settlement.
    Require(after.tx_records==(worker_settled?0:1)&&after.tx_bytes==(worker_settled?0:sizeof(bytes)),
        "post-admission diagnostic must not roll back the queue-owned task");
    Require(accepted&&after.tx_accepted==1&&after.tx_rejected==0,"diagnostic exception cannot undo admission");
    Require(after.tx_diagnostic_errors==1,"post-admission diagnostic exception is reported exactly once");
    if(!worker_settled){selected.Release();settled.Wait();}
    transport.shutdown();const auto final=control->Snapshot();
    Require(sent==1&&closed==2,"one SDK send and unique owned-handle cleanup");
    Require(final.tx_records==0&&final.tx_bytes==0&&final.live_tx_blocks==0&&final.live_tx_payload_bytes==0&&payload.expired(),
        "task and final payload ownership both settle exactly once");
    Require(final.clean_observed,"normal host fake cleanup after diagnostic exception");
    std::printf("submit exception %s PASS\n",worker_settled?"settled":"pending");
}
}
int main(int argc,char** argv) {
    Require(argc==2,"one mode and fresh process required");const std::string mode=argv[1];
    Require(mode=="pending"||mode=="settled","known mode required");Exercise(mode=="settled");return 0;
}
