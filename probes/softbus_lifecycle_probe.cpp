// Short-lived S2 public Socket experiment. No persistent permission writes.
#include "softbus_lifecycle_contract.hpp"
#include "token_first_entry.hpp"
#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
#define NOMINMAX
#include <windows.h>
#else
#include <accesstoken_kit.h>
#include <token_setproc.h>
#include <socket.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {
using namespace mdds_probe;
using Clock=std::chrono::steady_clock;
constexpr auto run_budget=std::chrono::seconds(7), close_budget=std::chrono::seconds(3);
constexpr auto send_budget=std::chrono::seconds(1);
constexpr auto quiet_budget=std::chrono::milliseconds(200), hold_budget=std::chrono::milliseconds(500);
constexpr char package_name[]="com.kaihong.mdds", process_name[]="fastdds_dsoftbus_probe";
constexpr char permission_name[]="ohos.permission.DISTRIBUTED_DATASYNC";
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
constexpr int bytes_type=2; // Explicit host boundary value, not SDK capability evidence.
#else
constexpr int bytes_type=static_cast<int>(DATA_TYPE_BYTES);
#endif
enum class ProbeCase { Alias, CallbackClose };
enum class Call { Socket=1, Listen=2, Bind=3, Send=4 };
struct Options {
    ProbeCase test=ProbeCase::Alias;
    bool server=false,single=false;
    std::uint64_t token=0;
    std::string run_id,local_name,remote_listen,peer_network,expected_name,expected_network;
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
    std::string fixture;
#endif
};
std::uint64_t ProcessId() noexcept {
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
    return GetCurrentProcessId();
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}
std::uint64_t ThreadId() noexcept {
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
    return GetCurrentThreadId();
#else
    return static_cast<std::uint64_t>(syscall(SYS_gettid));
#endif
}
bool ValidSimple(const std::string& value,std::size_t maximum) {
    if(value.empty() || value.size()>maximum)return false;
    for(unsigned char c:value)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;
    return true;
}
bool ValidSocketName(const std::string& name) {
    return ValidSimple(name,63)&&name.rfind("com.kaihong.mdds.",0)==0&&name.size()>17;
}
bool ValidNetwork(const std::string& value) {
    if(value.size()!=64)return false;
    for(unsigned char c:value)if(!((c>='a'&&c<='f')||(c>='A'&&c<='F')||(c>='0'&&c<='9')))return false;
    return true;
}
bool ParseOptions(int argc,char** argv,Options& options) {
    bool mode=false,test=false,token=false,preflight=false;
    for(int i=1;i<argc;++i) {
        const std::string key=argv[i];
        if((key=="--single"||key=="--server"||key=="--client")&&!mode) {
            mode=true;options.single=key=="--single";options.server=key=="--server";
        }else if(key=="--preflight-confirmed"&&!preflight)preflight=true;
        else if(key=="--case"&&!test&&i+1<argc) {
            test=true;const std::string value=argv[++i];
            if(value=="alias")options.test=ProbeCase::Alias;
            else if(value=="callback-close")options.test=ProbeCase::CallbackClose;
            else return false;
        }else if(key=="--expected-token"&&!token&&i+1<argc) {
            token=true;if(!ParseTokenArgument(argv[++i],options.token))return false;
        }else {
            std::string* field=key=="--run-id"?&options.run_id:key=="--local-name"?&options.local_name:
                key=="--remote-listen-name"?&options.remote_listen:key=="--peer-network-id"?&options.peer_network:
                key=="--expected-remote-initiator-name"?&options.expected_name:
                key=="--expected-peer-network-id"?&options.expected_network:nullptr;
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
            if(key=="--host-fixture")field=&options.fixture;
#endif
            if(!field||!field->empty()||i+1==argc)return false;
            *field=argv[++i];if(field->empty())return false;
        }
    }
    if(!mode||!test||!token||!preflight||!AdmitTokenEntry({TokenEntryMode::Record,true,true,options.token})||
        !ValidSimple(options.run_id,64)||!ValidSocketName(options.local_name)||!ValidNetwork(options.expected_network))return false;
    if(options.single!=(options.test==ProbeCase::Alias))return false;
    if(options.server) {
        if(!ValidSocketName(options.expected_name)||!options.remote_listen.empty()||!options.peer_network.empty())return false;
    }else {
        if(!options.expected_name.empty()||!ValidSocketName(options.remote_listen)||
            !ValidNetwork(options.peer_network)||options.peer_network!=options.expected_network)return false;
    }
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
    const std::array<const char*,9> fixtures{{"normal","different-id","worker-token","blocked-close","thread-failure","callback-copy","unknown-callback","close-thread-failure","event-full"}};
    if(std::none_of(fixtures.begin(),fixtures.end(),[&](const char* name){return options.fixture==name;}))return false;
#endif
    return true;
}

struct ProbeCallbacks {
    virtual ~ProbeCallbacks()=default;
    virtual void Bound(int,const char*,const char*,const char*,int) noexcept=0;
    virtual void Bytes(int,const void*,std::uint32_t) noexcept=0;
    virtual void Closed(int,int) noexcept=0;
    virtual void Error(int,int) noexcept=0;
};
// Only this seam varies in host subprocess tests. The runtime, workers, entry,
// deadline accounting, callbacks and process-exit decisions are the same code.
class SocketBoundary {
public:
    virtual ~SocketBoundary()=default;
    virtual std::uint64_t WorkerToken()=0;
    virtual void Initialize(ProbeCallbacks&)=0;
    virtual int Create(const Options&)=0;
    virtual int ListenSocket(int,int)=0;
    virtual int BindSocket(int,int)=0;
    virtual int SendRecord(int,const std::vector<std::uint8_t>&)=0;
    virtual void ShutdownSocket(int)=0;
};

class LifecycleRuntime final : public ProbeCallbacks {
public:
    LifecycleRuntime(Options value,SocketBoundary& boundary,LifecycleThreadFactory& factory)
        :options(std::move(value)),contract(options.server?options.expected_name:options.remote_listen,options.expected_network,bytes_type),
         backend(boundary),threads(factory),run_deadline(Clock::now()+run_budget),work_deadline(run_deadline-close_budget) {}
    void Bound(int socket,const char* name,const char* network,const char* package,int type) noexcept override {
        LifecycleCallbackLease lease(contract,socket,ThreadId());
        try {
            // Capture the positive ID without allocating before copying SDK strings.
            // Accept() owns before validating; the actual copy must not lose a new ID on exception.
            const auto admission=contract.RegisterSocket(socket);
            if(admission==LifecycleAdmission::Full||admission==LifecycleAdmission::Conflict||admission==LifecycleAdmission::Invalid)return;
            if(contract.Accept(socket,CopyRecordPeer(name,network,package,type)))contract.SetReady(socket);
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
            if(options.fixture=="event-full")for(std::size_t i=0;i<=LifecycleContract::event_limit;++i)
                contract.Report(LifecycleEventKind::CallbackEnter,socket,0,0,ThreadId());
#endif
        }catch(...){contract.Fail("callback_exception");}
    }
    void Bytes(int socket,const void* bytes,std::uint32_t length) noexcept override {
        LifecycleCallbackLease lease(contract,socket,ThreadId());
        try {
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
            if(options.fixture=="callback-copy")throw std::bad_alloc();
#endif
            contract.Receive(socket,bytes,length);
            if(options.test==ProbeCase::CallbackClose&&options.server&&!hold_claimed.exchange(true)&&!contract.Snapshot().failed) {
                contract.Report(LifecycleEventKind::CallbackHeld,socket,0,length,ThreadId());held=true;
                // A self-expiring public callback hold; never depends on Shutdown returning.
                contract.Wait([](const LifecycleSnapshot&){return false;},Clock::now()+hold_budget);
                contract.Report(LifecycleEventKind::CallbackReleased,socket,0,length,ThreadId());
            }
        }catch(...){contract.Fail("callback_exception");}
    }
    void Closed(int socket,int reason) noexcept override {
        LifecycleCallbackLease lease(contract,socket,ThreadId());contract.PeerShutdown(socket,reason);
        contract.RequestClose(run_deadline);
    }
    void Error(int socket,int code) noexcept override {
        LifecycleCallbackLease lease(contract,socket,ThreadId());contract.PeerError(socket,code);contract.Fail("peer_error");
        contract.RequestClose(run_deadline);
    }
    bool Run() {
        if(!StartCall(Call::Socket,0,[this]{backend.Initialize(*this);return CreateOwned();})||!WaitCall(work_deadline))return false;
        const int socket=call_result.load();if(socket<=0)return false;
        if(options.test==ProbeCase::Alias) {
            if(!StartCall(Call::Socket,0,[this]{return CreateOwned();})||!WaitCall(work_deadline))return false;
            observed=call_result.load()==socket&&!contract.Snapshot().failed;return observed;
        }
        const int remaining=static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(work_deadline-Clock::now()).count());
        if(remaining<=0){contract.Fail("work_deadline_before_bind");return false;}
        if(!StartCall(options.server?Call::Listen:Call::Bind,socket,[this,socket,remaining]{
            return options.server?backend.ListenSocket(socket,std::min(4000,remaining)):backend.BindSocket(socket,std::min(4000,remaining));
        })||!WaitCall(work_deadline))return false;
        if(call_result.load()!=0){contract.Fail("bind_or_listen_failed");return false;}
        if(options.server) {
            std::cout<<"stage=Listen action=RETURN rc=0 run_id="<<options.run_id<<'\n';
            if(!WaitState([this](const LifecycleSnapshot& state){return held.load()||state.failed;},work_deadline)||contract.Snapshot().failed)return false;
            RecordEvent event;if(!contract.Pop(event)||!ValidateProbeRecord(4,event.bytes)){contract.Fail("held_payload_mismatch");return false;}
            observed=true;return true;
        }
        if(!contract.SetReady(socket))return false;
        auto payload=std::make_shared<const std::vector<std::uint8_t>>(MakeProbeRecord(4));
        if(!StartCall(Call::Send,socket,[this,socket,payload]{return backend.SendRecord(socket,*payload);},payload->size())||
            !WaitCall(std::min(work_deadline,Clock::now()+send_budget)))return false;
        if(call_result.load()!=0){contract.Fail("send_failed");return false;}
        if(!WaitState([](const LifecycleSnapshot& state){return state.peer_shutdown>0||state.failed;},work_deadline)||contract.Snapshot().failed)return false;
        observed=true;return true;
    }
    bool Close() {
        const auto deadline=contract.RequestClose(run_deadline);
        Clock::time_point quiet_until{};
        while(Clock::now()<deadline) {
            if(operation.Joinable()&&operation.Finished()) {
                operation.JoinUntil(deadline);contract.Report(LifecycleEventKind::WorkerJoined);
                if(operation.Threw())contract.Fail("sdk_call_exception");
            }
            if(closer.Joinable()&&closer.Finished()) {
                closer.JoinUntil(deadline);contract.Report(LifecycleEventKind::WorkerJoined);
                if(closer.Threw())contract.Fail("sdk_close_exception");
            }
            if(!closer.Joinable())if(const int socket=contract.ClaimShutdown()) {
                if(!closer.Start(threads,[this,socket]{
                    if(!CheckWorkerToken())return;
                    contract.Report(LifecycleEventKind::ShutdownStarted,socket,0,0,ThreadId());
                    ++shutdown_calls;backend.ShutdownSocket(socket);contract.ShutdownReturned(socket,ThreadId());
                })) {contract.Fail("close_thread_creation_failed");Drain();return false;}
            }
            auto state=contract.Snapshot();
            const bool collected=!operation.Joinable()&&!closer.Joinable()&&state.active_callbacks==0&&state.shutdown_returned==state.owned;
            if(collected) {
                if(quiet_until==Clock::time_point{})quiet_until=Clock::now()+quiet_budget;
                if(Clock::now()>=quiet_until){close_observed=true;Drain();return !state.failed;}
            }else quiet_until={};
            Drain();contract.Wait([](const LifecycleSnapshot&){return false;},std::min(deadline,Clock::now()+std::chrono::milliseconds(10)));
        }
        contract.Fail("close_deadline_exceeded");Drain();return false;
    }
    bool RequiresRetainedExit() const {
        const auto state=contract.Snapshot();return operation.Joinable()||closer.Joinable()||state.active_callbacks!=0||
            state.fatal||state.shutdown_returned!=state.owned;
    }
    void Drain() {
        LifecycleEvent event;
        while(contract.EventAt(logged,event)) {
            ++logged;
            std::cout<<"event="<<LifecycleEventName(event.kind)<<" run_id="<<options.run_id<<" pid="<<ProcessId()<<" tid="<<event.tid
                <<" seq="<<event.sequence<<" steady_us="<<std::chrono::duration_cast<std::chrono::microseconds>(event.at-contract.Start()).count()
                <<" wall_us="<<std::chrono::duration_cast<std::chrono::microseconds>(event.wall.time_since_epoch()).count()
                <<" socket="<<event.socket<<" code="<<event.code<<" bytes="<<event.bytes<<" active="<<event.active_callbacks<<" closing="<<event.closing;
            if(event.kind==LifecycleEventKind::ShutdownReturned)std::cout<<" return_kind=VOID quiescence=NOT_VERIFIED";
            std::cout<<'\n';
        }
    }
    Options options;
    LifecycleContract contract;
    bool observed=false,close_observed=false;
    std::atomic<unsigned> socket_calls{0},shutdown_calls{0};
    ManagedLifecycleWorker operation,closer;
private:
    bool CheckWorkerToken() {
        const auto actual=backend.WorkerToken();contract.Report(LifecycleEventKind::WorkerToken,0,0,static_cast<std::size_t>(actual),ThreadId());
        if(actual==0||actual!=options.token){contract.Fail("worker_token_mismatch");return false;}return true;
    }
    int CreateOwned() {
        ++socket_calls;const int socket=backend.Create(options);contract.RegisterSocket(socket);return socket;
    }
    bool StartCall(Call kind,int socket,std::function<int()> call,std::size_t bytes=0) {
        call_result=std::numeric_limits<int>::min();
        if(!operation.Start(threads,[this,kind,socket,call=std::move(call),bytes]{
            if(!CheckWorkerToken())return;
            contract.Report(LifecycleEventKind::CallBegin,socket,static_cast<int>(kind),bytes,ThreadId());
            const auto start=Clock::now();const int rc=call();call_result=rc;
            contract.Report(LifecycleEventKind::CallReturn,socket,rc,bytes,ThreadId());
            if(kind==Call::Send&&Clock::now()-start>send_budget)contract.Fail("send_observation_threshold_exceeded");
        })) {contract.Fail("thread_creation_failed");return false;}return true;
    }
    bool WaitCall(Clock::time_point deadline) {
        while(!operation.Finished()&&Clock::now()<deadline){Drain();operation.JoinUntil(std::min(deadline,Clock::now()+std::chrono::milliseconds(20)));}
        if(!operation.JoinUntil(deadline)){contract.Fail("sdk_call_still_inflight_result_unknown");return false;}
        contract.Report(LifecycleEventKind::WorkerJoined);Drain();
        if(operation.Threw())contract.Fail("sdk_call_exception");return !contract.Snapshot().failed;
    }
    bool WaitState(const std::function<bool(const LifecycleSnapshot&)>& predicate,Clock::time_point deadline) {
        while(Clock::now()<deadline) {
            if(contract.Wait(predicate,std::min(deadline,Clock::now()+std::chrono::milliseconds(20)))){Drain();return true;}
            Drain();
        }
        return false;
    }
    SocketBoundary& backend;
    LifecycleThreadFactory& threads;
    const Clock::time_point run_deadline,work_deadline;
    std::atomic<int> call_result{0};
    std::atomic<bool> hold_claimed{false},held{false};
    std::size_t logged=0;
};

#ifndef MDDS_LIFECYCLE_HOST_FIXTURE
std::atomic<ProbeCallbacks*> callbacks{nullptr};
void NativeBound(int socket,PeerSocketInfo info) noexcept {if(auto* cb=callbacks.load())cb->Bound(socket,info.name,info.networkId,info.pkgName,info.dataType);}
void NativeBytes(int socket,const void* data,std::uint32_t length) noexcept {if(auto* cb=callbacks.load())cb->Bytes(socket,data,length);}
void NativeClosed(int socket,ShutdownReason reason) noexcept {if(auto* cb=callbacks.load())cb->Closed(socket,static_cast<int>(reason));}
void NativeError(int socket,std::int32_t code) noexcept {if(auto* cb=callbacks.load())cb->Error(socket,code);}
void NativeMessage(int socket,const void*,std::uint32_t) noexcept {if(auto* cb=callbacks.load())cb->Error(socket,-1);}
class NativeBoundary final : public SocketBoundary {
public:
    std::uint64_t WorkerToken() override {return GetSelfTokenID();}
    void Initialize(ProbeCallbacks& cb) override {
        callbacks=&cb;
        provider=dlopen("/system/lib64/platformsdk/libsoftbus_client.z.so",RTLD_NOW|RTLD_LOCAL);
        if(!provider)throw std::runtime_error("provider_load_failed");
        create=reinterpret_cast<decltype(&Socket)>(dlsym(provider,"Socket"));
        listen=reinterpret_cast<decltype(&Listen)>(dlsym(provider,"Listen"));
        bind=reinterpret_cast<decltype(&Bind)>(dlsym(provider,"Bind"));
        send=reinterpret_cast<decltype(&SendBytes)>(dlsym(provider,"SendBytes"));
        shutdown=reinterpret_cast<decltype(&Shutdown)>(dlsym(provider,"Shutdown"));
        if(!create||!listen||!bind||!send||!shutdown)throw std::runtime_error("provider_symbols_missing");
        listener={};listener.OnBind=NativeBound;listener.OnBytes=NativeBytes;listener.OnShutdown=NativeClosed;
        listener.OnError=NativeError;listener.OnMessage=NativeMessage;
    }
    int Create(const Options& options) override {
        SocketInfo info{};info.name=const_cast<char*>(options.local_name.c_str());info.pkgName=package;info.dataType=DATA_TYPE_BYTES;
        if(!options.server){info.peerName=const_cast<char*>(options.remote_listen.c_str());info.peerNetworkId=const_cast<char*>(options.peer_network.c_str());}
        return create(info);
    }
    int ListenSocket(int socket,int wait) override {QosTV qos[]={{QOS_TYPE_MIN_BW,80},{QOS_TYPE_MAX_WAIT_TIMEOUT,wait}};return listen(socket,qos,2,&listener);}
    int BindSocket(int socket,int wait) override {QosTV qos[]={{QOS_TYPE_MIN_BW,80},{QOS_TYPE_MAX_WAIT_TIMEOUT,wait}};return bind(socket,qos,2,&listener);}
    int SendRecord(int socket,const std::vector<std::uint8_t>& bytes) override {return send(socket,bytes.data(),static_cast<std::uint32_t>(bytes.size()));}
    void ShutdownSocket(int socket) override {shutdown(socket);}
private:
    void* provider=nullptr; // Retained until this short-lived process terminates; no unverified dlclose.
    decltype(&Socket) create=nullptr;decltype(&Listen) listen=nullptr;decltype(&Bind) bind=nullptr;
    decltype(&SendBytes) send=nullptr;decltype(&Shutdown) shutdown=nullptr;
    ISocketListener listener{};char package[sizeof(package_name)]="com.kaihong.mdds";
};
TaskCheckpoint ObserveTasks() {
    TaskCheckpoint result;result.process_id=ProcessId();result.current_tid=ThreadId();
    DIR* directory=opendir("/proc/self/task");result.opened=directory!=nullptr;
    if(directory) {
        bool valid=true;for(;;) {
            errno=0;const auto* entry=readdir(directory);if(!entry){result.complete=valid&&errno==0;break;}
            if(std::strcmp(entry->d_name,".")==0||std::strcmp(entry->d_name,"..")==0)continue;
            const char* end=entry->d_name+std::strlen(entry->d_name);std::uint64_t tid=0;
            const auto parsed=std::from_chars(entry->d_name,end,tid);if(parsed.ec!=std::errc{}||parsed.ptr!=end||tid==0)valid=false;
            if(result.count==0)result.sole_tid=tid;if(result.count<2)++result.count;
        }
        if(closedir(directory)!=0)result.complete=false;
    }
    std::cout<<"stage=local_thread_checkpoint pid="<<result.process_id<<" tid="<<result.current_tid<<" count="<<result.count
        <<" complete="<<result.complete<<" single_main="<<IsSingleMainTask(result)<<'\n';return result;
}
OwnedTokenObservation VerifyOwned(std::uint64_t target,const char* phase) {
    using namespace OHOS::Security::AccessToken;
    const auto id=static_cast<AccessTokenID>(target);const auto lookup=AccessTokenKit::GetNativeTokenId(process_name);
    const auto type=AccessTokenKit::GetTokenTypeFlag(id);NativeTokenInfo info{};const int rc=AccessTokenKit::GetNativeTokenInfo(id,info);
    std::vector<std::int32_t> states;const int query=AccessTokenKit::VerifyAccessToken(id,std::vector<std::string>{permission_name},states,true);
    std::cout<<"stage=identity phase="<<phase<<" lookup="<<lookup<<" type="<<static_cast<int>(type)<<" info_rc="<<rc
        <<" query_rc="<<query<<" state_count="<<states.size()<<'\n';
    return {lookup,static_cast<int>(type),static_cast<int>(TOKEN_NATIVE),rc,std::move(info.processName),static_cast<int>(info.apl),
        static_cast<int>(APL_NORMAL),query,std::move(states),static_cast<int>(PERMISSION_GRANTED)};
}
#else
std::atomic<std::uint64_t> fixture_token{0x100000001ULL};
class FixtureBoundary final : public SocketBoundary {
public:
    explicit FixtureBoundary(const Options& value):options(value){}
    std::uint64_t WorkerToken() override {return options.fixture=="worker-token"?0:fixture_token.load();}
    void Initialize(ProbeCallbacks& value) override {cb=&value;}
    int Create(const Options&) override {return options.fixture=="different-id"?++next:1;}
    int ListenSocket(int,int) override {
        cb->Bound(2,options.expected_name.c_str(),options.expected_network.c_str(),"diagnostic-only",2);
        // This thread represents a real asynchronous callback at the public-call
        // fixture boundary, not a production callback-drain guarantee.
        callback_worker=std::thread([this]{auto bytes=MakeProbeRecord(4);cb->Bytes(options.fixture=="unknown-callback"?99:2,bytes.data(),static_cast<std::uint32_t>(bytes.size()));});
        return 0;
    }
    int BindSocket(int,int) override {return 0;}
    int SendRecord(int,const std::vector<std::uint8_t>&) override {return 0;}
    void ShutdownSocket(int) override {
        if(options.fixture=="blocked-close") {std::unique_lock<std::mutex> lock(block_mutex);block_cv.wait(lock,[]{return false;});}
    }
private:
    Options options;ProbeCallbacks* cb=nullptr;int next=0;
    std::thread callback_worker;std::mutex block_mutex;std::condition_variable block_cv;
};
class FixtureFactory final : public LifecycleThreadFactory {
public:
    explicit FixtureFactory(std::string fixture):fixture_(std::move(fixture)){}
    std::thread Create(std::function<void()> job) override {
        ++calls_;
        if((calls_==2&&fixture_=="thread-failure")||(calls_==3&&fixture_=="close-thread-failure"))
            throw std::runtime_error("injected thread creation failure");
        return std::thread(std::move(job));
    }
private:std::string fixture_;unsigned calls_=0;
};
#endif

class LifecycleTokenOperations final : public TokenEntryOperations {
public:
    explicit LifecycleTokenOperations(Options value):options(std::move(value)){}
    TaskCheckpoint ObserveTasks() override {
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
        return {true,true,ProcessId(),ProcessId(),1,ProcessId()};
#else
        return ::ObserveTasks();
#endif
    }
    std::uint64_t GetSelf() override {
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
        return fixture_token.load();
#else
        return GetSelfTokenID();
#endif
    }
    int SetSelf(std::uint64_t token) override {
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
        fixture_token=token;return 0;
#else
        return SetSelfTokenID(token);
#endif
    }
    OwnedTokenObservation VerifyOwned(std::uint64_t token,const char* phase) override {
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
        (void)phase;return {token,1,1,0,process_name,0,0,0,{0},0};
#else
        return ::VerifyOwned(token,phase);
#endif
    }
    void Diagnostic(const char* stage,std::uint64_t first,std::uint64_t second,int rc) override {
        std::cout<<"stage="<<stage<<" pid="<<ProcessId()<<" tid="<<ThreadId()<<" first="<<first<<" second="<<second<<" rc="<<rc
            <<" scope=CURRENT_MAIN_THREAD_ONLY\n";
    }
    bool EnterProvider() override {
        // All of these objects are process-lifetime, including unjoined failure
        // workers. Normal close joins application workers; no callback sees stack state.
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
        auto* boundary=new FixtureBoundary(options);auto* factory=new FixtureFactory(options.fixture);
#else
        auto* boundary=new NativeBoundary;auto* factory=new LifecycleThreadFactory;
#endif
        runtime=new LifecycleRuntime(options,*boundary,*factory);return runtime->Run();
    }
    bool CloseProvider() override {return runtime?runtime->Close():true;}
    Options options;LifecycleRuntime* runtime=nullptr;
};
int RunLifecycleProbe(int argc,char** argv) {
    std::cout<<std::unitbuf;Options options;
    if(!ParseOptions(argc,argv,options)) {
        std::cerr<<"Usage: softbus_lifecycle_probe --case alias|callback-close --single|--server|--client --run-id <id> "
            "--local-name <project-name> --expected-peer-network-id <64hex> --expected-token <owned-id> --preflight-confirmed "
            "[--remote-listen-name <name> --peer-network-id <64hex> | --expected-remote-initiator-name <name>]\n";return 64;
    }
#ifdef MDDS_LIFECYCLE_HOST_FIXTURE
    std::cout<<"scope=HOST_FIXTURE_NOT_DEVICE\n";
#else
    std::cout<<"scope=NATIVE_SOCKET_LIFECYCLE\n";
#endif
    std::cout<<"run_id="<<options.run_id<<" pid="<<ProcessId()<<" process_name="<<process_name<<" package="<<package_name
        <<" writes=NONE external_preflight=OPERATOR_ASSERTED run_ms=7000 close_ms=3000 send_ms=1000 hold_ms=500 quiet_ms=200\n";
    LifecycleTokenOperations operations(std::move(options));TokenFirstEntry entry(operations);
    const bool successful=entry.Run({TokenEntryMode::Record,true,true,operations.options.token});
    auto* runtime=operations.runtime;
    if(!runtime){std::cout<<"result=FAIL token_restored="<<entry.RestoreSucceeded()<<" provider_entered=0\n";return 6;}
    runtime->Drain();const auto state=runtime->contract.Snapshot();const bool retain=runtime->RequiresRetainedExit();
    const int rc=retain?70:successful?0:!entry.RestoreSucceeded()||state.failed?6:2;
    std::cout<<"result="<<(rc==0?"OBSERVATION_COMPLETE":rc==2?"NOT_EXERCISED":"FAIL")<<" socket_calls="<<runtime->socket_calls.load()
        <<" shutdown_calls="<<runtime->shutdown_calls.load()<<" owned="<<state.owned<<" aliases="<<state.aliases
        <<" shutdown_returned="<<state.shutdown_returned<<" active_callbacks="<<state.active_callbacks<<" events="<<state.events
        <<" application_workers_joinable="<<(runtime->operation.Joinable()||runtime->closer.Joinable())<<" untracked_id="<<state.untracked_id
        <<" reason="<<state.reason<<" token_restored="<<entry.RestoreSucceeded()<<" cleanup="<<(retain?"UNCONFIRMED":"OBSERVED_KNOWN_HANDLES_ONLY")
        <<" sdk_race=REQUIRES_EXTERNAL_LOGS quiescence=NOT_VERIFIED transport=NOT_IMPLEMENTED\n";
    if(retain) {
        std::cout<<"termination=NONZERO_PROCESS_EXIT_RETAINED exit_code=70 token_restored="<<entry.RestoreSucceeded()<<'\n';
        std::cout.flush();std::cerr.flush();std::_Exit(70);
    }
    return rc;
}
} // namespace
int main(int argc,char** argv){return RunLifecycleProbe(argc,argv);}
