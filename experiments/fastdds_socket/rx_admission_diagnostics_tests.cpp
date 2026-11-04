// Real fixed Socket SDK callback signatures and the existing transport fixture.
#define main prior_multi_diagnostic_fixture_main
#include "multi_instance_tests.cpp"
#undef main
#include <atomic>
#include <set>

namespace fastdds_dsoftbus::experiment {
class RuntimeTestAccess {
public:
    static void Copy(Control& c,std::function<void()> f){c.before_rx_copy_=std::move(f);}
    static void Admitted(Control& c,std::function<void()> f){c.after_rx_admit_=std::move(f);}
    static void Retire(Control& c,std::function<void()> f){c.before_final_retire_=std::move(f);}
};
}
namespace {
using Reason=fastdds_dsoftbus::dsoftbus::RxAdmissionReason;
template<class Record> auto EndpointLimit(const Record& r,std::size_t expected,int)
    ->decltype(r.endpoint_limit_known,r.endpoint_limit,void()) {
    Require(r.endpoint_limit_known&&r.endpoint_limit==expected,"exact immutable endpoint limit observed");
}
template<class Record> void EndpointLimit(const Record&,std::size_t,long) {
    throw std::runtime_error("NEW_DIAGNOSTIC_LIMIT_NOT_PRESENT");
}
Config LimitedConfig(std::uint32_t limit){auto c=MakeConfig(1);c.max_message_size=limit;return c;}
struct App {
    Config config;
    std::shared_ptr<ProcessRuntime> runtime=ProcessRuntime::Acquire({config},{Create,Listen,Bind,Send,Shutdown});
    std::shared_ptr<Control> control=std::make_shared<Control>();
    ExperimentalTransport transport{ExperimentalDescriptor(config,{Create,Listen,Bind,Send,Shutdown},control,runtime)};
    Receiver receiver;ISocketListener table{};PeerSocketInfo peer{};
    explicit App(std::uint32_t limit=60000):config(LimitedConfig(limit)){Require(bool(runtime)&&transport.init(),"actual runtime/init");
        Require(transport.OpenInputChannel(config.local_locator,&receiver,config.max_message_size),"actual receiver");
        {std::lock_guard<std::mutex> lock(native.mutex);for(auto& item:native.sockets)if(item.second.server)table=item.second.table;}
        peer.name=const_cast<char*>(config.peer_service.c_str());peer.networkId=const_cast<char*>(config.peer_network_id.c_str());peer.dataType=DATA_TYPE_BYTES;}
    void Accept(){Require(table.OnNegotiate(1001,peer),"real negotiation");
        {std::lock_guard<std::mutex> lock(native.mutex);native.sockets[1001]={config.local_service,table,true};}
        table.OnBind(1001,peer);}
    RxAdmissionRecord Last(){const auto s=runtime->ReceiveAdmissionSummary();Require(s.total>0&&s.total==s.stored,"stored one per invocation");return runtime->ReceiveAdmissionRecord(0,s.stored-1);}
    void Expect(Reason reason){const auto r=Last();Require(r.decision.reason==reason,"exact terminal reason");Require(r.invocation!=0&&r.callback_slot==control->Snapshot().server_callback_slot,"original callback correlation");}
    void Stop(){transport.shutdown();Require(runtime->Finalize(),"finalize");}
};
void EntryCases(){
    App a(512);const auto poison=reinterpret_cast<const void*>(std::uintptr_t(1));
    a.table.OnBytes(999,poison,60001);a.Expect(Reason::OwnerMissing);
    int listener=0;{std::lock_guard<std::mutex> lock(native.mutex);for(const auto& p:native.sockets)if(p.second.server)listener=p.first;}
    a.table.OnBytes(listener,poison,60001);a.Expect(Reason::WrongRole);
    Require(a.table.OnNegotiate(1001,a.peer),"negotiated before ready");
    a.table.OnBytes(1001,poison,60001);a.Expect(Reason::NotReady);
    {std::lock_guard<std::mutex> lock(native.mutex);native.sockets[1001]={a.config.local_service,a.table,true};}
    a.table.OnBind(1001,a.peer);
    a.table.OnBytes(1001,nullptr,1);a.Expect(Reason::NullBuffer);
    a.table.OnBytes(1001,poison,0);a.Expect(Reason::ZeroSize);
    a.table.OnBytes(1001,poison,513);a.Expect(Reason::TooLarge);EndpointLimit(a.Last(),512,0);
    Require(a.control->Snapshot().rx_owned==0,"invalid buffers never copied or parsed");
    Require(a.control->Snapshot().rx_drops==3&&a.control->Snapshot().unexpected_callbacks==3,"original invalid/native rejection counters unchanged");
    const std::uint8_t bytes[]{21,22,23};a.table.OnBytes(1001,bytes,3);a.Expect(Reason::AcceptedQueue);
    const auto accepted=a.Last().decision;
    Require(accepted.result_known&&accepted.result==0&&accepted.candidate_count==1,"one first-match application admitted");
    Require(accepted.native_known&&accepted.native_generation&&accepted.application_known&&accepted.receiver_known,"original identities known");
    Require(a.receiver.Wait(1),"legitimate delivery unchanged");
    Require(a.receiver.messages==std::vector<std::vector<std::uint8_t>>{{21,22,23}},"original bytes unchanged");
    Require(a.transport.CloseInputChannel(a.config.local_locator),"close original receiver");
    a.table.OnBytes(1001,bytes,3);a.Expect(Reason::ReceiverNotOpen);
    Require(a.control->Snapshot().no_app_drops==1,"original NoReceiver counter records closed receiver once");
    Require(a.Last().decision.candidate_count==1,"closed receiver differs from no selected application");
    a.transport.shutdown();a.table.OnBytes(1001,bytes,3);a.Expect(Reason::NoApplication);
    Require(a.control->Snapshot().no_app_drops==2,"original NoReceiver counter records no selected app once");
    Require(a.Last().decision.candidate_count==0&&!a.Last().decision.application_known,"missing app not reconstructed from old/current map");
    Require(a.runtime->Finalize(),"stop retained native runtime");
    a.table.OnBytes(1001,poison,60001);a.Expect(Reason::Retired);
    Require(a.control->Snapshot().unexpected_callbacks==4,"original retired counter remains once");
    const auto summary=a.runtime->ReceiveAdmissionSummary();std::uint64_t count=0;std::set<std::uint64_t> ids;
    for(auto n:summary.reasons)count+=n;
    for(std::size_t i=0;i<summary.stored;++i)Require(ids.insert(a.runtime->ReceiveAdmissionRecord(0,i).invocation).second,"one terminal outcome per invocation");
    Require(summary.total==10&&count==summary.total&&summary.overflow==0,"all callback outcomes counted once");
    PrintProcessSummary("diagnostic-late-retired");
}
void OverflowCase(){
    App a;a.Accept();
    const auto poison=reinterpret_cast<const void*>(std::uintptr_t(1));
    for(std::size_t i=0;i<RxAdmissionSummary::capacity+7;++i)a.table.OnBytes(1001,poison,0);
    const auto summary=a.runtime->ReceiveAdmissionSummary();
    EndpointLimit(a.runtime->ReceiveAdmissionRecord(0,0),60000,0);
    Require(summary.total==1031&&summary.stored==1024&&summary.overflow==7,"fixed diagnostic overflow explicit");
    Require(summary.reasons[static_cast<std::size_t>(Reason::ZeroSize)]==1031,"reason counts include unstored callbacks");
    Require(a.control->Snapshot().rx_owned==0&&a.control->Snapshot().rx_drops==1031,"overflow never changes data path");
    a.Stop();
}
void NativeCommitCase(){
    App a;a.Accept();Stage copying;RuntimeTestAccess::Copy(*a.control,[&]{copying.Hold();});
    const std::uint8_t bytes[]{4,5};auto call=std::async(std::launch::async,[&]{a.table.OnBytes(1001,bytes,2);});
    Require(copying.Wait(),"original capture reached existing copy barrier");
    a.table.OnShutdown(1001,static_cast<ShutdownReason>(0));copying.Release();call.get();
    a.Expect(Reason::NativeCommitRejected);const auto d=a.Last().decision;
Require(d.native_status_known&&d.native_status!=fastdds_dsoftbus::dsoftbus::NativeStatus::None,"native failure not flattened");
    Require(d.native_known&&d.application_known&&d.receiver_known,"original capture remains diagnostic identity");
    Require(a.receiver.messages.empty(),"revoked native record never delivered");a.Stop();
}
void ClosingCase(){
    App a;a.Accept();Stage joined;
    // Existing final-retirement hook runs after close-claimed ownership. Its
    // actual callback must preserve the earlier CloseClaimed gate's precedence.
    RuntimeTestAccess::Retire(*a.control,[&]{joined.Hold();});a.transport.shutdown();
    auto stop=std::async(std::launch::async,[&]{return a.runtime->Finalize();});Require(joined.Wait(),"stop close-claimed barrier");
    const auto poison=reinterpret_cast<const void*>(std::uintptr_t(1));a.table.OnBytes(1001,poison,60001);a.Expect(Reason::CloseClaimed);
    Require((a.Last().adapter_state&36)==36,"closing and close-claimed captured together once");
    joined.Release();Require(stop.get(),"stop drain unchanged");
}
void CapacityCase(bool byte_limit){
    App a;a.Accept();Stage admitted;RuntimeTestAccess::Admitted(*a.control,[&]{admitted.Hold();});
    const std::uint8_t first=1;a.table.OnBytes(1001,&first,1);Require(admitted.Wait(),"one original receiver lease held");
    const std::vector<std::uint8_t> bytes(byte_limit?60000:1,42);
    const std::size_t queued=byte_limit?17:63;
    for(std::size_t i=0;i<queued;++i){a.table.OnBytes(1001,bytes.data(),static_cast<std::uint32_t>(bytes.size()));a.Expect(Reason::AcceptedQueue);}
    a.table.OnBytes(1001,bytes.data(),static_cast<std::uint32_t>(bytes.size()));
    a.Expect(byte_limit?Reason::LocalBytes:Reason::LocalRecords);const auto d=a.Last().decision;
    Require(d.capacity_known&&d.capacity_mask==(byte_limit?8u:4u),"exact original capacity gate snapshot");
    Require(d.candidate_count==1&&d.result_known&&d.result==6,"one callback one selected candidate one capacity outcome");
    Require(d.local_records_used==queued+1&&d.records_used==queued+1,"held record usage at rejecting gate");
    Require(a.control->Snapshot().rx_drops==1&&a.control->Snapshot().no_app_drops==0,"original capacity rejection counted once");
    admitted.Release();a.Stop();
}
void CopyCapacityCase(){
    App a;a.Accept();Stage copying;RuntimeTestAccess::Copy(*a.control,[&]{copying.Hold();});
    const std::uint8_t byte=3;std::vector<std::future<void>> calls;
    for(unsigned i=0;i<8;++i)calls.push_back(std::async(std::launch::async,[&]{a.table.OnBytes(1001,&byte,1);}));
    Require(copying.Wait(8),"eight actual callback captures reserve copying slots");
    a.table.OnBytes(1001,&byte,1);a.Expect(Reason::CopyingLimit);const auto d=a.Last().decision;
    Require(d.capacity_known&&d.capacity_mask==16&&d.copying_used==8&&d.copying_limit==8,"copying gate and immutable limit");
    copying.Release();for(auto& call:calls)call.get();
    Require(a.runtime->ReceiveAdmissionSummary().total==9,"every concurrent callback accounted once");a.Stop();
}
}
int main(int argc,char** argv){try{
    const std::string name=argc>1?argv[1]:"entry";
    if(name=="entry")EntryCases();else if(name=="overflow")OverflowCase();
    else if(name=="native-commit")NativeCommitCase();else if(name=="closing")ClosingCase();
    else if(name=="capacity-records")CapacityCase(false);else if(name=="capacity-bytes")CapacityCase(true);
    else if(name=="capacity-copying")CopyCapacityCase();else throw std::runtime_error("unknown case");
    std::printf("RX_ADMISSION_DIAGNOSTIC PASS case=%s\n",name.c_str());return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
