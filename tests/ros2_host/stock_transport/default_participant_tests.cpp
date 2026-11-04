// Public default Runtime lifecycle; actual stock DDS and explicit Fake Socket C API.
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
#include <cstdlib>
#include <new>
namespace {thread_local long dp_fail_after=-1;thread_local bool dp_failed=false,dp_block_facade=false;void DpAllocation(std::size_t);}
void* operator new(std::size_t n){
    DpAllocation(n);
    if(dp_fail_after==0){dp_fail_after=-1;dp_failed=true;throw std::bad_alloc();}
    if(dp_fail_after>0)--dp_fail_after;
    if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#define main DpPriorViewsMain
#include "runtime_views_tests.cpp"
#undef main
namespace {
using Transport=eprosima::fastdds::rtps::TransportInterface;
struct FaultGate {std::mutex gate;std::condition_variable changed;bool entered=false,released=false,timed_out=false;};
FaultGate dp_fault;
void DpAllocation(std::size_t n){
    if(!dp_block_facade||n!=sizeof(DSoftBusTransport))return;
    dp_block_facade=false;
    {std::unique_lock<std::mutex> lock(dp_fault.gate);dp_fault.entered=true;dp_fault.changed.notify_all();
        if(!dp_fault.changed.wait_for(lock,2s,[]{return dp_fault.released;}))dp_fault.timed_out=true;}
    dp_failed=true;throw std::bad_alloc();
}
struct Observer final:ProcessReceiveObserver {
    std::atomic<unsigned> decisions{0},stages{0};
    void OnReceive(ReceiveStage,const ReceiveDiagnostic&,const std::uint8_t*,std::size_t)noexcept override{++stages;}
    void OnDecision(const ReceiveDecision&)noexcept override{++decisions;}
};
void NoSdk(){const auto s=public_socket_fake::Observe();T2Check(!s.sockets[0]&&!s.sockets[1]&&!s.live&&!s.shutdowns[0]&&!s.shutdowns[1],"metadata/borrow unexpectedly called SDK");}
void Borrowed(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
    experimental::Runtime runtime(T2Configuration(0,0));NoSdk();
    const auto d=runtime.MakeDescriptor(T2Descriptor(0,0));T2Check(bool(d),"default descriptor");NoSdk();
    auto fresh=runtime.MakeDescriptor(T2Descriptor(0,0));T2Check(!fresh,"normal descriptor issuance must remain one-shot");
    std::unique_ptr<Transport> a(d->create_transport()),b(d->create_transport());
    T2Check(a&&b&&!d->create_transport()&&!runtime.Close(),"default two before init / third refusal / Busy");NoSdk();
    T2Check(b->init()&&a->init(),"reverse init order");NoSdk();
    a.reset();T2Check(!runtime.Close(),"first borrow close removed second");b.reset();
    T2Check(runtime.Close()&&!d->create_transport(),"last unused borrowed facade failed to close permanently");NoSdk();
    std::cout<<"DEFAULT_BORROWED two_before_init=1 reverse_init=1 third_refused=1 natural_close=1 SDK_calls=0\n";
}
void Allocation(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
    experimental::Runtime runtime(T2Configuration(0,0));
    const auto config=T2Descriptor(0,0);dp_fail_after=0;try{auto x=runtime.MakeDescriptor(config);T2Check(!x,"descriptor allocation unexpectedly succeeded");}catch(const std::bad_alloc&){}dp_fail_after=-1;
    T2Check(!phase0::RuntimeProbeAccess::ObserveRuntime(runtime).started,"failed first descriptor started seed");NoSdk();
    auto d=runtime.MakeDescriptor(T2Descriptor(0,0));T2Check(bool(d),"descriptor after failure");
    unsigned failures=0;
    for(long fail=0;fail<24;++fail){dp_failed=false;dp_fail_after=fail;std::unique_ptr<Transport> v(d->create_transport());dp_fail_after=-1;
        T2Check(dp_failed&&!v,"injected unpublished facade did not fail");++failures;
        const auto s=phase0::RuntimeProbeAccess::ObserveRuntime(runtime);T2Check(s.started&&!s.closed&&s.live_views==0,"unpublished failure consumed seed");NoSdk();}
    std::unique_ptr<Transport> a(d->create_transport()),b(d->create_transport());T2Check(a&&b,"seed was not refundable");
    a.reset();T2Check(!runtime.Close(),"published unused sibling not charged");b.reset();T2Check(runtime.Close()&&!d->create_transport(),"published unused last close");NoSdk();
    std::cout<<"DEFAULT_ALLOCATION actual_bad_alloc="<<failures<<" unused_seed_reused=1 last_published_borrow_closed=1\n";
}
using Included=std::array<std::array<bool,2>,2>;
const Included all{{{{true,true}},{{true,true}}}};
unsigned Cross(T2LifecycleFixture& f,const Included& included,unsigned cycle){
    for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group){auto& app=f.endpoints[side][group];if(!included[side][group])continue;
        std::vector<dds::InstanceHandle_t> expected;for(unsigned remote=0;remote<2;++remote)if(included[1-side][remote])expected.push_back(f.endpoints[1-side][remote].writer->get_instance_handle());
        {std::unique_lock<std::mutex> lock(app.received->gate);T2Check(app.received->changed.wait_for(lock,2s,[&]{
            if(app.received->matched_writers.size()!=expected.size()||app.received->matches!=int(expected.size()))return false;
            for(const auto& h:expected)if(std::find(app.received->matched_writers.begin(),app.received->matched_writers.end(),h)==app.received->matched_writers.end())return false;return true;}),"default exact remote GUID set");}
        {std::unique_lock<std::mutex> lock(app.sent->gate);T2Check(app.sent->changed.wait_for(lock,2s,[&]{return app.sent->matches==int(expected.size());}),"default remote reader count");}
    }
    const auto before=public_socket_fake::Observe();unsigned takes=0;
    for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group){if(!included[side][group])continue;auto& source=f.endpoints[side][group];
        T2Sample sample;sample.cycle=cycle;sample.source=side+2*group;sample.ordinal=cycle*4+sample.source;sample.payload.fill(std::uint8_t(17*cycle+sample.source));
        T2Check(source.writer->write(&sample,dds::HANDLE_NIL)==0,"default actual write");
        for(unsigned remote=0;remote<2;++remote)if(included[1-side][remote]){f.endpoints[1-side][remote].received->WaitData(sample,source.writer->guid());++takes;
            const auto h=source.writer->get_instance_handle();const char* hex="0123456789abcdef";std::cout<<"DEFAULT_EDGE cycle="<<cycle<<" from="<<side<<':'<<group<<" to="<<1-side<<':'<<remote<<" writer=";
            for(unsigned i=0;i<16;++i)std::cout<<hex[h.value[i]>>4]<<hex[h.value[i]&15];std::cout<<" exact_GUID_payload=1\n";}
        T2Check(source.writer->wait_for_acknowledgments({1,0})==0,"default reliable acknowledgment");
    }
    const auto after=public_socket_fake::Observe();T2Check(after.forwards[0]>before.forwards[0]&&after.forwards[1]>before.forwards[1],"default data bypassed actual C link");
    return takes;
}
void CrossClose(bool observed){
    auto observer=observed?std::make_shared<Observer>():std::shared_ptr<Observer>{};
    T2LifecycleFixture f(0,true,true,true,observer);f.Pair(0,true);f.Pair(1,true);T2Check(Cross(f,all,1)==8,"default all eight remote edges");
    const auto native=public_socket_fake::Observe();T2Check(!f.runtimes[0]->Close(),"default owner not Busy");
    f.Remove(0,0);Included remaining=all;remaining[0][0]=false;T2Check(Cross(f,remaining,2)==4,"default sibling survived first close");
    T2Check(f.Create(0,0,151,true),"default returned slot replacement");T2Check(Cross(f,all,3)==8,"default replacement edges");
    const auto after=public_socket_fake::Observe();for(unsigned i=0;i<2;++i)T2Check(after.sockets[i]==native.sockets[i]&&after.shutdowns[i]==native.shutdowns[i],"default view recreated live anchor");
    if(observed)T2Check(observer->decisions>0&&observer->stages>0,"observer overload did not receive original decisions/stages");
    f.Finish();std::cout<<"DEFAULT_CROSS takes=20 observed="<<observed<<" default_ctor=1 local_edges=NOT_PROVIDED other_transport=OFF intraprocess=OFF DataSharing=OFF\n";
}
void RefundAfterClose(){
    T2LifecycleFixture f(0,true,true,true);f.Pair(0);f.Traffic(0,1);bool returned_nonnull=false;std::exception_ptr error;
    std::thread pending([&]{try{dp_block_facade=true;std::unique_ptr<Transport> b(f.descriptors[0]->create_transport());returned_nonnull=bool(b);}catch(...){error=std::current_exception();}});
    {std::unique_lock<std::mutex> lock(dp_fault.gate);T2Check(dp_fault.changed.wait_for(lock,2s,[]{return dp_fault.entered;}),"pending B not at facade allocation");}
    const auto pending_state=phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[0]);T2Check(pending_state.live_views==2,"pending B not charged");
    f.Remove(0,0);const auto before=public_socket_fake::Observe();T2Check(!before.shutdowns[0],"A close ignored reserved B");
    {std::lock_guard<std::mutex> lock(dp_fault.gate);dp_fault.released=true;}dp_fault.changed.notify_all();pending.join();if(error)std::rethrow_exception(error);
    T2Check(!returned_nonnull&&!dp_fault.timed_out,"B did not fail construction at controlled point");
    const auto state=phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[0]);T2Check(state.closed&&!state.live_views,"last unpublished refund orphaned native lifetime");
    T2Check(!f.descriptors[0]->create_transport(),"refund resurrected closed default");f.Finish();
    std::cout<<"DEFAULT_PENDING_REFUND published_A_closed=1 reserved_B_bad_alloc=1 native_last_close=1\n";
}
void UniqueStop(){
    T2LifecycleFixture f(0,true,true,true);f.Pair(0);f.Traffic(0,1);FaultGate stop;std::exception_ptr error;bool close_result=false;
    public_socket_fake::SetShutdownBarrier([&]{std::unique_lock<std::mutex> lock(stop.gate);stop.entered=true;stop.changed.notify_all();if(!stop.changed.wait_for(lock,2s,[&]{return stop.released;}))stop.timed_out=true;});
    const auto before=public_socket_fake::Observe();
    std::thread remove([&]{try{f.Remove(0,0);}catch(...){error=std::current_exception();}});
    {std::unique_lock<std::mutex> lock(stop.gate);T2Check(stop.changed.wait_for(lock,2s,[&]{return stop.entered;}),"natural close not entered");}
    auto competing=std::async(std::launch::async,[&]{return f.runtimes[0]->Close();});
    T2Check(competing.wait_for(30ms)==std::future_status::timeout,"competing Close did not wait for stop owner");
    {std::lock_guard<std::mutex> lock(stop.gate);stop.released=true;}stop.changed.notify_all();remove.join();close_result=competing.get();public_socket_fake::SetShutdownBarrier({});
    if(error)std::rethrow_exception(error);T2Check(close_result&&!stop.timed_out,"unique stop did not settle");
    const auto after=public_socket_fake::Observe();T2Check(after.shutdowns[0]-before.shutdowns[0]==3,"duplicate native Shutdown owner");f.Finish();
    std::cout<<"DEFAULT_UNIQUE_STOP native_shutdown_calls=3 competing_Close_waited=1\n";
}
void ObserverFault(bool reentry){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
    auto backend=std::make_shared<phase0::PublicSocketBackend>(T2Configuration(0,0));auto root=host_test::HostProviderLoop::CreateParticipantShared(backend);
    T2Check(root->SetBeforeNativeClose([&](const host_test::RuntimeShutdownPoint&){if(reentry)T2Check(!root->Shutdown(),"observer reentry unexpectedly closed");else throw 7;}),"normal owner observer install");
    T2Check(root->Start(T2Descriptor(0,0)),"normal root metadata");T2Check(!root->Join()&&!root->Join(host_test::HostProviderLoop::CreateShared(backend)),"wrong owner admitted");
    auto view=root->Join(root);T2Check(view&&!view->Join(view),"non-root granted admission authority");
    auto facade=HostFacadeTestAccess::Create(T2Descriptor(0,0),view);T2Check(view->PublishFacade()&&facade->init(),"complete facade publication");
    T2Check(!view->Shutdown()&&!view->Shutdown()&&!root->Shutdown(),"observer failure ceased to be sticky");facade.reset();view.reset();NoSdk();
    std::cout<<"DEFAULT_OBSERVER_FAULT reentry="<<reentry<<" repeated_view_and_owner_false=1\n";
}
void StartFailure(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);experimental::Runtime runtime(T2Configuration(0,0));auto bad=T2Descriptor(0,0);bad.maxMessageSize=65501;bad.send_limits.max_record_bytes=65501;
    T2Check(!runtime.MakeDescriptor(bad),"metadata backend bound should fail");T2Check(phase0::RuntimeProbeAccess::ObserveRuntime(runtime).closed,"actual Start failure not terminal");
    T2Check(!runtime.MakeDescriptor(T2Descriptor(0,0)),"terminal Start failure reopened");NoSdk();T2Check(runtime.Close(),"metadata-only failed Start cleanup");
    std::cout<<"DEFAULT_START_FAILURE terminal=1 SDK_calls=0\n";
}
}
int main(int argc,char** argv){try{T2LoadedDll();T2Check(argc==2,"case required");const std::string c=argv[1];
    if(c=="borrowed")Borrowed();else if(c=="allocation")Allocation();else if(c=="cross-close")CrossClose(false);else if(c=="observed")CrossClose(true);
    else if(c=="pending-refund")RefundAfterClose();else if(c=="unique-stop")UniqueStop();else if(c=="observer-throw")ObserverFault(false);else if(c=="observer-reentry")ObserverFault(true);else if(c=="start-failure")StartFailure();else throw std::runtime_error("unknown default case");
    std::cout<<"PASS default_participant "<<c<<" HOST_FAKE_SOCKET_ONLY DEVICE_TESTED=0\n";return 0;
}catch(const std::exception& e){dp_fail_after=-1;std::cerr<<"FAIL default_participant "<<e.what()<<'\n';return 1;}}
