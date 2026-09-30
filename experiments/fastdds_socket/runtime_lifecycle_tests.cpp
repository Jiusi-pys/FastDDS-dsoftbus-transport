// Reuse the real Socket-signature fake and direct Transport fixture. No SDK
// generation/context parameters are invented by these production-path tests.
#define main previous_multi_instance_test_main
#include "multi_instance_tests.cpp"
#undef main

namespace fastdds_dsoftbus::experiment {
class RuntimeTestAccess {
public:
    static void Copy(Control& c,std::function<void()> f){c.before_rx_copy_=std::move(f);}
    static void Admit(Control& c,std::function<void()> f){c.after_rx_admit_=std::move(f);}
    static void Detach(Control& c,std::function<void()> f){c.detach_requested_=std::move(f);}
    static void Prepare(Control& c,std::function<void()> f){c.before_attach_publish_=std::move(f);}
    static void Allocate(Control& c,std::function<void()> f){c.before_app_allocate_=std::move(f);}
    static void Retire(Control& c,std::function<void()> f){c.before_final_retire_=std::move(f);}
    static void AcquireRetained(std::function<void(std::uint64_t)> f){ProcessRuntime::SetAcquireRetainedHook(std::move(f));}
};
}

namespace {
struct RuntimeApp {
    Config config;std::shared_ptr<Control> control=std::make_shared<Control>();
    ExperimentalTransport transport;Receiver receiver;
    RuntimeApp(Config c,std::shared_ptr<ProcessRuntime> runtime,SocketApi api={Create,Listen,Bind,Send,Shutdown})
        :config(std::move(c)),transport(ExperimentalDescriptor(config,api,control,std::move(runtime))){}
    bool Start(){return transport.init()&&transport.OpenInputChannel(config.local_locator,&receiver,60000);}
    ISocketListener Server()const{std::lock_guard<std::mutex> l(native.mutex);for(auto& item:native.sockets)if(item.second.service==config.local_service&&item.second.server)return item.second.table;throw std::runtime_error("listener missing");}
    PeerSocketInfo Peer()const{PeerSocketInfo p{};p.name=const_cast<char*>(config.peer_service.c_str());p.networkId=const_cast<char*>(config.peer_network_id.c_str());p.dataType=DATA_TYPE_BYTES;return p;}
    void Accept(int socket){auto table=Server();Require(table.OnNegotiate(socket,Peer()),"accepted owner");{std::lock_guard<std::mutex> l(native.mutex);native.sockets[socket]={config.local_service,table,true};}table.OnBind(socket,Peer());}
    void Inject(int socket,std::uint8_t value){Server().OnBytes(socket,&value,1);}
};
struct BlockingReceiver final:rtps::TransportReceiverInterface {
    Stage& entered;std::atomic<unsigned> calls{0};
    explicit BlockingReceiver(Stage& s):entered(s){}
    void OnDataReceived(const std::uint8_t*,std::uint32_t,const Locator&,const Locator&)override{++calls;entered.Hold();}
};
void BarrierCase(const std::string& name){
    auto config=MakeConfig(1);auto runtime=ProcessRuntime::Acquire({config},{Create,Listen,Bind,Send,Shutdown});
    Require(bool(runtime),"barrier runtime");RuntimeApp old(config,runtime);Stage held,detach;BlockingReceiver blocking(held);
    Require(old.transport.init(),"barrier application");
    Require(old.transport.OpenInputChannel(config.local_locator,name=="L6"||name=="L7"?static_cast<rtps::TransportReceiverInterface*>(&blocking):&old.receiver,60000),"barrier receiver");old.Accept(1001);
    if(name=="L4")RuntimeTestAccess::Admit(*old.control,[&]{held.Hold();});
    if(name=="L5")RuntimeTestAccess::Copy(*old.control,[&]{held.Hold();});
    RuntimeTestAccess::Detach(*old.control,[&]{detach.Arrive();});
    auto incoming=std::async(std::launch::async,[&]{old.Inject(1001,11);});
    Require(held.Wait(),"original ownership stage reached");
    if(name=="L7")old.Inject(1001,12); // Unique RX worker remains in first receiver.
    auto close=std::async(std::launch::async,[&]{old.transport.shutdown();});
    Require(detach.Wait(),"detach admission sealed");const bool pending=close.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready;
    held.Release();incoming.get();close.get();Require(pending,"detach must wait original admitted/copy/executing invocation");
    const unsigned old_count=name=="L6"||name=="L7"?blocking.calls.load():static_cast<unsigned>(old.receiver.messages.size());
    if(name=="L7")Require(old_count==1,"queued second old record dropped, not run after freeze");
    RuntimeApp fresh(config,runtime);Require(fresh.Start(),"fresh app after barrier");fresh.Inject(1001,21);Require(fresh.receiver.Wait(1),"fresh receiver delivery");
    Require((name=="L6"||name=="L7"?blocking.calls.load():static_cast<unsigned>(old.receiver.messages.size()))==old_count,"old receiver never called after completed detach");
    fresh.transport.shutdown();Require(runtime->Finalize(),"barrier final stop");FinalEmpty();
}
void RuntimeCase(const std::string& name){
    const std::vector<std::string> cases{"L1","L2","L3","L8","L9","L10","L11","L12"};
    Require(std::find(cases.begin(),cases.end(),name)!=cases.end(),"unknown runtime case rejected");
    auto p=MakeConfig(1),q=MakeConfig(2);auto runtime=ProcessRuntime::Acquire({p,q},{Create,Listen,Bind,Send,Shutdown});
    Require(bool(runtime),"runtime acquisition");RuntimeApp first(p,runtime),persistent(q,runtime);
    Require(first.Start()&&persistent.Start(),"two application start");first.Accept(1001);persistent.Accept(1002);
    auto before=first.control->Snapshot(),anchor=persistent.control->Snapshot();
    if(name=="L12"){
        const auto previous=runtime->Snapshot();const int creates=native.creates;
        RuntimeApp conflicting(p,runtime);Require(!conflicting.Start(),"attachment prepare collision rejected");
        const auto after=runtime->Snapshot();Require(after.active_applications==previous.active_applications&&after.runtime_id==previous.runtime_id&&creates==native.creates,"failed attachment leaves runtime and Q untouched before SDK");
    }
    if(name=="L1")Require(before.runtime_id!=0&&before.runtime_id==anchor.runtime_id,"L1 shared runtime identity");
    first.transport.shutdown();persistent.Inject(1002,21);Require(persistent.receiver.Wait(1),"Q works through gap");
    auto table=first.Server();std::uint8_t gap=99;table.OnBytes(1001,&gap,1);
    if(name=="L10")Require(runtime->Snapshot().active_applications==1&&!runtime->Snapshot().stopped,"L10 runtime alive while Q attached");
    if(name=="L11")Require(GetProcessStats().tombstone_entries==0,"L11 live table never retired by detach");
    RuntimeApp replacement(p,runtime);const bool attached=replacement.Start();
    if(!attached){persistent.transport.shutdown();runtime->Finalize();Require(false,"same live native table must permit new application");}
    auto after=replacement.control->Snapshot();
    if(name=="L3")Require(before.application_incarnation!=after.application_incarnation&&before.receiver_incarnation!=after.receiver_incarnation&&before.logical_token!=after.logical_token,"L3 fresh actual application receiver token identities");
    if(name=="L2"||name=="L8")Require(replacement.receiver.messages.empty(),"no-app gap record not adopted");
    if(name=="L9"){auto now=persistent.control->Snapshot();Require(now.application_incarnation==anchor.application_incarnation&&now.receiver_incarnation==anchor.receiver_incarnation&&now.logical_token==anchor.logical_token,"Q lifecycle continuity");}
    replacement.Inject(1001,31);Require(replacement.receiver.Wait(1),"new app receives on retained native owner");
    Require(first.receiver.messages.empty(),"old app not invoked after detach");
    replacement.transport.shutdown();persistent.transport.shutdown();Require(runtime->Finalize(),"explicit final native cleanup");FinalEmpty();
}
void PrepareRollback(bool allocate=false){
    auto p=MakeConfig(1),q=MakeConfig(2);auto runtime=ProcessRuntime::Acquire({p,q},{Create,Listen,Bind,Send,Shutdown});
    RuntimeApp persistent(q,runtime);Require(persistent.Start(),"Q before fault");persistent.Accept(1002);const auto before=runtime->Snapshot();const int creates=native.creates;
    RuntimeApp failed(p,runtime);if(allocate)RuntimeTestAccess::Allocate(*failed.control,[]{throw std::bad_alloc();});else RuntimeTestAccess::Prepare(*failed.control,[]{throw std::bad_alloc();});
    const bool admitted=failed.Start();if(admitted)failed.transport.shutdown();
    const auto after=runtime->Snapshot();persistent.Inject(1002,32);Require(persistent.receiver.Wait(1),"Q after attach failure");
    persistent.transport.shutdown();runtime->Finalize();
    Require(!admitted,"prepare fault must abort attachment");Require(creates==native.creates,"prepare rollback has no native side effects");
    Require(after.active_applications==before.active_applications&&after.retained_applications==before.retained_applications,"failed claim released without modifying Q");
}
void AcquireClosing(){
    auto p=MakeConfig(1);auto runtime=ProcessRuntime::Acquire({p},{Create,Listen,Bind,Send,Shutdown});RuntimeApp app(p,runtime);Require(app.Start(),"closing app");
    Stage finalizing;RuntimeTestAccess::Retire(*app.control,[&]{finalizing.Hold();});app.transport.shutdown();
    auto final=std::async(std::launch::async,[&]{return runtime->Finalize();});Require(finalizing.Wait(),"final native retirement paused");
    auto acquired=ProcessRuntime::Acquire({p},{Create,Listen,Bind,Send,Shutdown});finalizing.Release();Require(final.get(),"final shutdown completes");
    Require(!acquired,"Acquire rejects an identical runtime whose final shutdown has started");FinalEmpty();
}
void FailedFinalResult(){
    auto p=MakeConfig(1);auto runtime=ProcessRuntime::Acquire({p},{Create,Listen,Bind,Send,Shutdown});RuntimeApp app(p,runtime);Require(app.Start(),"failed final app");app.Accept(1001);
    // Real public callback signature supplies an observed SDK error. Cleanup
    // still runs, but the first terminal result is deliberately unsuccessful.
    app.Server().OnError(1001,-901);Stage finalizing,second_started;
    RuntimeTestAccess::Retire(*app.control,[&]{finalizing.Hold();});app.transport.shutdown();
    auto first=std::async(std::launch::async,[&]{return runtime->Finalize();});Require(finalizing.Wait(),"first Finalize paused");
    auto second=std::async(std::launch::async,[&]{second_started.Arrive();return runtime->Finalize();});Require(second_started.Wait(),"concurrent finalizer started");
    finalizing.Release();const bool a=first.get(),b=second.get(),c=runtime->Finalize();
    Require(!a,"native fatal observation makes original terminal result fail");Require(!b&&!c,"concurrent/repeated Finalize retain first terminal failure");FinalEmpty();
}
std::atomic<unsigned> sdk_reentries{0};
void ReentrantShutdown(int socket){
    Shutdown(socket);
    if(sdk_reentries.fetch_add(1)==0){
        std::printf("RUNTIME_REGISTRY_RED sdk_shutdown_reenter_acquire\n");std::fflush(stdout);
        auto nested=ProcessRuntime::Acquire({MakeConfig(3)},{Create,Listen,Bind,Send,ReentrantShutdown});
        Require(bool(nested),"SDK can reenter process acquisition during last release");
        Require(nested->Finalize(),"nested unstarted runtime cleanup");
    }
}
void RegistryLastRelease(){
    const SocketApi api{Create,Listen,Bind,Send,ReentrantShutdown};auto p=MakeConfig(1),q=MakeConfig(2);
    auto old=ProcessRuntime::Acquire({p},api);Require(bool(old),"old registry entry");
    {auto control=std::make_shared<Control>();ExperimentalTransport app(ExperimentalDescriptor(p,api,control,old));Require(app.init(),"old real native listener");app.shutdown();}
    const auto target=old->Snapshot().runtime_id;std::weak_ptr<ProcessRuntime> weak=old;Stage pinned;
    RuntimeTestAccess::AcquireRetained([&](std::uint64_t id){if(id==target)pinned.Hold();});
    auto lookup=std::async(std::launch::async,[&]{return ProcessRuntime::Acquire({q},api);});
    Require(pinned.Wait(),"nonmatching lookup retained original registry entry");old.reset();
    Require(!weak.expired(),"registry lookup owns the sole remaining strong runtime reference");pinned.Release();
    auto result=lookup.get();RuntimeTestAccess::AcquireRetained({});
    Require(bool(result)&&weak.expired()&&sdk_reentries.load()==1,"last release ran SDK outside registry gate");Require(result->Finalize(),"lookup runtime final cleanup");FinalEmpty();
}
void EndpointMessageCap(){
    auto small=MakeConfig(1),large=MakeConfig(2);small.max_message_size=512;
    auto runtime=ProcessRuntime::Acquire({small,large},{Create,Listen,Bind,Send,Shutdown});Require(bool(runtime),"heterogeneous runtime caps");
    RuntimeApp p(small,runtime),q(large,runtime);
    Require(p.transport.init()&&p.transport.OpenInputChannel(small.local_locator,&p.receiver,512)&&q.Start(),"actual 512-byte receiver capacity");p.Accept(1001);q.Accept(1002);
    std::vector<std::uint8_t> too_big(513,42),boundary(512,24);
    p.Server().OnBytes(1001,too_big.data(),static_cast<std::uint32_t>(too_big.size()));
    const bool oversized_owned=p.control->Snapshot().rx_owned!=0;
    p.Server().OnBytes(1001,boundary.data(),static_cast<std::uint32_t>(boundary.size()));
    q.Server().OnBytes(1002,too_big.data(),static_cast<std::uint32_t>(too_big.size()));
    Require(p.receiver.Wait(1)&&q.receiver.Wait(1),"M accepted locally and M+1 accepted by larger endpoint");
    p.transport.shutdown();q.transport.shutdown();Require(runtime->Finalize(),"cap final cleanup");
    Require(!oversized_owned,"M+1 rejected before allocation/owned-copy at small endpoint");
    Require(p.receiver.messages==std::vector<std::vector<std::uint8_t>>{boundary},"small receiver receives only exact M");
    Require(q.receiver.messages==std::vector<std::vector<std::uint8_t>>{too_big},"larger receiver retains own cap");FinalEmpty();
}
Stage* setup_pause=nullptr;bool pause_bind=false;std::string paused_service;
int PausedSetupCreate(SocketInfo info){
    if(setup_pause&&!pause_bind&&info.peerName&&info.name==paused_service)setup_pause->Hold();
    return Create(info);
}
int PausedSetupBind(int socket,const QosTV* qos,std::uint32_t count,const ISocketListener* listener){
    bool pause=false;{std::lock_guard<std::mutex> lock(native.mutex);pause=setup_pause&&pause_bind&&native.sockets.at(socket).service==paused_service;}
    if(pause)setup_pause->Hold();return (Bind)(socket,qos,count,listener);
}
void ConcurrentEndpointSetup(bool bind){
    const SocketApi api{PausedSetupCreate,Listen,PausedSetupBind,Send,Shutdown};auto p=MakeConfig(1),q=MakeConfig(2);
    auto runtime=ProcessRuntime::Acquire({p,q},api);Require(bool(runtime),"two-endpoint setup runtime");RuntimeApp first(p,runtime,api),second(q,runtime,api);
    Require(first.Start(),"P1 listener initialized");Stage held,settled;setup_pause=&held;pause_bind=bind;paused_service=p.local_service;
    ContractTestAccess::AfterTxSettle(*first.control,[&]{settled.Arrive();});
    rtps::SendResourceList senders;Require(first.transport.OpenOutputChannel(senders,p.peer_locator),"P1 SenderResource");
    const std::uint8_t bytes[]{1,2,3};std::vector<rtps::NetworkBuffer> buffers{{bytes,3}};std::vector<Locator> targets{p.peer_locator};Iterator begin(targets,0),end(targets,1);
    Require(senders.front()->send(buffers,3,&begin,&end,Clock::now()+std::chrono::seconds(10),0),"P1 TX accepted");
    Require(held.Wait(),"P1 actual public setup call held after native ticket entry");
    const bool q_started=second.Start(); // Must have a bounded independent setup slot.
    const auto q_before_release=second.control->Snapshot();held.Release();Require(settled.Wait(),"P1 setup/send completes after release");setup_pause=nullptr;
    if(q_started){first.Accept(1001);second.Accept(1002);first.Inject(1001,1);second.Inject(1002,2);Require(first.receiver.Wait(1)&&second.receiver.Wait(1),"both endpoint receivers work after overlapping setup");}
    first.transport.shutdown();second.transport.shutdown();senders.clear();Require(runtime->Finalize(),"setup test final cleanup");
    Require(q_started&&q_before_release.listener_creations==1,"Q listener must initialize while P1 legal Socket/Bind operation is in flight");FinalEmpty();
}
void RepeatedApplications(){
    auto p=MakeConfig(1),q=MakeConfig(2);auto runtime=ProcessRuntime::Acquire({p,q},{Create,Listen,Bind,Send,Shutdown});RuntimeApp persistent(q,runtime);Require(persistent.Start(),"persistent Q");persistent.Accept(1002);auto anchor=persistent.control->Snapshot();
    std::uint64_t old_app=0,old_receiver=0,old_token=0;std::vector<std::shared_ptr<Control>> audit;
    int sockets=0;int slot=-1;
    for(unsigned generation=1;generation<=100;++generation){
        RuntimeApp p1(p,runtime);Require(p1.Start(),"repeat attach");if(generation==1){p1.Accept(1001);sockets=native.creates;slot=p1.control->Snapshot().server_callback_slot;}
        auto identity=p1.control->Snapshot();Require(identity.application_incarnation>old_app&&identity.receiver_incarnation>old_receiver&&identity.logical_token>old_token,"identities never reused");
        old_app=identity.application_incarnation;old_receiver=identity.receiver_incarnation;old_token=identity.logical_token;
        p1.Inject(1001,1);Require(p1.receiver.Wait(1),"repeat actual callback to receiver");p1.transport.shutdown();audit.push_back(p1.control);
        const auto usage=runtime->Snapshot();Require(usage.active_applications==1&&usage.retained_applications==1&&usage.active_receivers==1,"bounded actual application metadata, despite retained audit Controls");
        Require(native.creates==sockets&&identity.server_callback_slot==slot&&usage.tombstone_entries==0,"live endpoint table retained across generations");
        persistent.Inject(1002,2);Require(persistent.receiver.Wait(generation),"Q gap traffic");auto now=persistent.control->Snapshot();Require(now.application_incarnation==anchor.application_incarnation&&now.receiver_incarnation==anchor.receiver_incarnation&&now.logical_token==anchor.logical_token,"same Q through all generations");
    }
    persistent.transport.shutdown();Require(runtime->Finalize(),"repetition final stop");FinalEmpty();
}
void SubmitDrain(){
    auto p=MakeConfig(1);auto runtime=ProcessRuntime::Acquire({p},{Create,Listen,Bind,Send,Shutdown});RuntimeApp app(p,runtime);Require(app.Start(),"submit drain app");
    rtps::SendResourceList resources;Require(app.transport.OpenOutputChannel(resources,p.peer_locator),"sender");
    Stage admitted,settled,detach;ContractTestAccess::AfterTxAdmit(*app.control,[&](auto){admitted.Hold();});ContractTestAccess::AfterTxSettle(*app.control,[&]{settled.Arrive();});RuntimeTestAccess::Detach(*app.control,[&]{detach.Arrive();});
    const std::vector<Locator> targets{p.peer_locator};const std::uint8_t data[]{1,2,3};const std::vector<rtps::NetworkBuffer> buffers{{data,3}};
    auto submit=[&]{Iterator b(targets,0),e(targets,1);return resources.front()->send(buffers,3,&b,&e,Clock::now()+std::chrono::seconds(10),0);};
    auto sending=std::async(std::launch::async,submit);Require(admitted.Wait()&&settled.Wait(),"task settled while submission still owns payload");
    auto closing=std::async(std::launch::async,[&]{app.transport.shutdown();});Require(detach.Wait(),"detach requested");const bool pending=closing.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready;
    admitted.Release();Require(sending.get(),"accepted before detach");closing.get();Require(pending,"joint detach waits submission and payload after queue settlement");
    RuntimeApp fresh(p,runtime);Require(fresh.Start(),"new attachment after joint drain");Require(!submit(),"stale sender cannot submit into new application");fresh.transport.shutdown();resources.clear();Require(runtime->Finalize(),"submit final stop");FinalEmpty();
}
}
int main(int argc,char** argv){try{Require(argc==2,"case argument");const std::string name=argv[1];if(name=="setup-create")ConcurrentEndpointSetup(false);else if(name=="setup-bind")ConcurrentEndpointSetup(true);else if(name=="endpoint-cap")EndpointMessageCap();else if(name=="registry-last-release")RegistryLastRelease();else if(name=="final-result")FailedFinalResult();else if(name=="allocate-rollback")PrepareRollback(true);else if(name=="acquire-closing")AcquireClosing();else if(name=="prepare-rollback")PrepareRollback();else if(name=="repeat100")RepeatedApplications();else if(name=="submit-drain")SubmitDrain();else if(name=="L4"||name=="L5"||name=="L6"||name=="L7")BarrierCase(name);else RuntimeCase(name);return 0;}catch(const std::exception& e){std::fprintf(stderr,"RUNTIME_CONTRACT_FAIL %s\n",e.what());return 1;}}
