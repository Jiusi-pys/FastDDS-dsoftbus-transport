// A scoped native diagnostic: two live outbound Socket handles, one local name,
// two remote slots and distinguishable real SDK callback function entrances.
// No close/rebuild/reuse is performed until all four echo observations finish.
#include "softbus_callback_table_oracle.hpp"
#include "token_first_entry.hpp"
#include <accesstoken_kit.h>
#include <token_setproc.h>
#include <socket.h>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <thread>
#include <sys/syscall.h>
#include <unistd.h>
namespace {
using namespace mdds_probe;
using Clock=std::chrono::steady_clock;
constexpr char pkg[]="com.kaihong.mdds";
constexpr char process_name[]="fastdds_dsoftbus_probe";
constexpr char permission[]="ohos.permission.DISTRIBUTED_DATASYNC";
static_assert(DATA_TYPE_BYTES==2,"BYTES ABI changed");
struct ProbeArguments {
    bool server=false;
    std::uint64_t run=0;
    std::string run_text, network;
};
struct QueuedEcho {
    int socket=0;
    unsigned entrance=0;
    CallbackTableRecord bytes{};
};
struct CallbackTableRuntime {
    const ProbeArguments args;
    const Clock::time_point start=Clock::now();
    const std::string client_name;
    const std::array<std::string,2> server_names;
    std::mutex gate;
    std::array<int,6> owned{};
    std::array<bool,6> claimed{};
    std::array<int,2> created{}, accepted{};
    std::array<bool,2> peer_closed{};
    std::array<QueuedEcho,8> queue{};
    std::size_t queued=0, next=0;
    std::atomic<unsigned> callbacks{0};
    std::atomic<bool> failed{false}, completed{false}, closing{false};
    unsigned records=0, shutdown_returns=0;
    std::array<CallbackTableObservation,4> observations{};
    CallbackTableVerdict verdict=CallbackTableVerdict::Incomplete;
    void* provider=nullptr;
    decltype(&Socket) create=nullptr;
    decltype(&Listen) listen=nullptr;
    decltype(&Bind) bind=nullptr;
    decltype(&SendBytes) send=nullptr;
    decltype(&Shutdown) shutdown=nullptr;
    explicit CallbackTableRuntime(ProbeArguments value) : args(std::move(value)),
        client_name(std::string(pkg)+".cb."+args.run_text+".c"),
        server_names{{std::string(pkg)+".cb."+args.run_text+".s1",std::string(pkg)+".cb."+args.run_text+".s2"}} {}
    long long Now() const { return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count(); }
    void Log(const char* event,int socket,unsigned entrance,int rc,unsigned seq=99) const {
        std::printf("event=%s role=%s run=%s pid=%d tid=%ld steady_us=%lld socket=%d entrance=%u sequence=%u rc=%d\n",
            event,args.server?"server":"client",args.run_text.c_str(),getpid(),syscall(SYS_gettid),Now(),socket,entrance,seq,rc);
    }
    void Fail(const char* reason,int socket=0,unsigned entrance=0) {
        failed=true; Log(reason,socket,entrance,-1);
    }
    // Called under gate. Every new accepted ID is retained before inspecting
    // borrowed peer strings. This fixed run neither retires nor reuses handles.
    bool Retain(int id) {
        if(id<=0) return false;
        for(int item:owned) if(item==id) return false;
        for(auto& item:owned) if(item==0) { item=id; return true; }
        return false;
    }
    bool Pop(QueuedEcho& result) {
        std::lock_guard<std::mutex> lock(gate);
        if(next==queued) return false;
        result=queue[next++]; return true;
    }
};
// Process-lifetime callback storage, no invented SDK generation/context.
// Entrances 1/2 denote distinct template function addresses, never an ID lookup.
CallbackTableRuntime* runtime=nullptr;
template<unsigned Entrance,class Action> void Ingress(Action action) noexcept {
    auto* value=runtime;
    if(!value) return;
    ++value->callbacks;
    try { action(*value); } catch(...) { value->failed=true; }
    --value->callbacks;
}
template<unsigned Entrance> void Bound(int id,PeerSocketInfo peer) noexcept {
    Ingress<Entrance>([&](CallbackTableRuntime& value) {
        value.Log("OnBind",id,Entrance,0);
        std::lock_guard<std::mutex> lock(value.gate);
        const bool retained=value.Retain(id);
        if(!retained || !value.args.server || value.accepted[Entrance-1]!=0) {
            value.Fail("invalid_OnBind",id,Entrance); return;
        }
        value.accepted[Entrance-1]=id;
        if(!peer.name || !peer.networkId || peer.dataType!=DATA_TYPE_BYTES ||
                strnlen(peer.name,256)!=value.client_name.size() ||
                std::memcmp(peer.name,value.client_name.data(),value.client_name.size()) ||
                strnlen(peer.networkId,65)!=value.args.network.size() ||
                std::memcmp(peer.networkId,value.args.network.data(),value.args.network.size()))
            value.Fail("peer_mismatch",id,Entrance);
    });
}
template<unsigned Entrance> void Bytes(int id,const void* data,std::uint32_t size) noexcept {
    Ingress<Entrance>([&](CallbackTableRuntime& value) {
        unsigned sequence=99;
        const bool exact=ValidateCallbackTableRecord(data,size,value.args.run,sequence);
        value.Log("OnBytes",id,Entrance,exact?0:-1,sequence);
        std::lock_guard<std::mutex> lock(value.gate);
        if(!exact || value.queued==value.queue.size() || value.closing) {
            value.Fail("record_not_admitted",id,Entrance); return;
        }
        auto& record=value.queue[value.queued++];
        record.socket=id; record.entrance=Entrance;
        std::memcpy(record.bytes.data(),data,record.bytes.size());
    });
}
template<unsigned Entrance> void Closed(int id,ShutdownReason reason) noexcept {
    Ingress<Entrance>([&](CallbackTableRuntime& value) {
        value.Log("OnShutdown",id,Entrance,reason);
        if(value.args.server) {
            std::lock_guard<std::mutex> lock(value.gate);
            for(std::size_t i=0;i<value.accepted.size();++i) if(value.accepted[i]==id) value.peer_closed[i]=true;
        }
        if(!value.completed && !value.closing) value.Fail("connection_closed_during_observation",id,Entrance);
    });
}
template<unsigned Entrance> void Error(int id,std::int32_t code) noexcept {
    Ingress<Entrance>([&](CallbackTableRuntime& value) { value.Log("OnError",id,Entrance,code); value.failed=true; });
}
template<unsigned Entrance> ISocketListener MakeListener() {
    ISocketListener listener{};
    listener.OnBind=Bound<Entrance>; listener.OnBytes=Bytes<Entrance>;
    listener.OnShutdown=Closed<Entrance>; listener.OnError=Error<Entrance>;
    return listener;
}
bool SendRecord(CallbackTableRuntime& value,int socket,unsigned sequence,const CallbackTableRecord& bytes) {
    value.Log("SendBytes_enter",socket,0,0,sequence);
    const int rc=value.send(socket,bytes.data(),static_cast<std::uint32_t>(bytes.size()));
    value.Log("SendBytes_return",socket,0,rc,sequence);
    return rc==0;
}
bool WaitEcho(CallbackTableRuntime& value,QueuedEcho& echo) {
    const auto deadline=Clock::now()+std::chrono::seconds(2);
    while(!value.failed && value.Now()<10000000 && Clock::now()<deadline) {
        if(value.Pop(echo)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
bool OpenEndpoint(CallbackTableRuntime& value,unsigned slot,ISocketListener& callbacks) {
    SocketInfo info{};
    info.name=const_cast<char*>((value.args.server?value.server_names[slot-1]:value.client_name).c_str());
    info.pkgName=const_cast<char*>(pkg); info.dataType=DATA_TYPE_BYTES;
    if(!value.args.server) {
        info.peerName=const_cast<char*>(value.server_names[slot-1].c_str());
        info.peerNetworkId=const_cast<char*>(value.args.network.c_str());
    }
    const int id=value.create(info);
    value.Log("Socket_return",id,slot,id);
    {
        std::lock_guard<std::mutex> lock(value.gate);
        if(!value.Retain(id)) { value.Fail("socket_creation_or_alias",id,slot); return false; }
        value.created[slot-1]=id;
    }
    QosTV qos[]={{QOS_TYPE_MIN_BW,80},{QOS_TYPE_MAX_WAIT_TIMEOUT,2000}};
    value.Log(value.args.server?"Listen_enter":"Bind_enter",id,slot,0);
    const int rc=value.args.server?value.listen(id,qos,2,&callbacks):value.bind(id,qos,2,&callbacks);
    value.Log(value.args.server?"Listen_return":"Bind_return",id,slot,rc);
    return rc==0 && !value.failed;
}
bool ObserveClient(CallbackTableRuntime& value,ISocketListener& first,ISocketListener& second) {
    if(!OpenEndpoint(value,1,first)) return false;
    for(unsigned sequence=0;sequence<4;++sequence) {
        if(sequence==1) {
            // The first exact echo has already traversed F1 before table F2
            // is submitted. There has been no Shutdown or reconstruction.
            if(!OpenEndpoint(value,2,second)) return false;
            value.Log("both_outbounds_live",value.created[0],0,0);
        }
        const int id=value.created[callback_record_slots[sequence]-1];
        auto bytes=MakeCallbackTableRecord(value.args.run,sequence);
        if(!SendRecord(value,id,sequence,bytes)) return false;
        QueuedEcho echo;
        if(!WaitEcho(value,echo)) return false;
        unsigned observed=99;
        if(!ValidateCallbackTableRecord(echo.bytes.data(),echo.bytes.size(),value.args.run,observed) ||
                observed!=sequence || echo.socket!=id || (sequence==0 && echo.entrance!=1)) {
            value.Fail("wrong_echo_identity",echo.socket,echo.entrance); return false;
        }
        value.observations[sequence]={sequence,echo.socket,echo.entrance};
        value.Log("echo_exact",echo.socket,echo.entrance,0,sequence);
        ++value.records;
    }
    value.verdict=ClassifyCallbackTable(value.observations.data(),value.records,value.created[0],value.created[1]);
    value.completed=value.verdict==CallbackTableVerdict::SharedClientTable || value.verdict==CallbackTableVerdict::PerSocketTable;
    return value.completed;
}
bool ObserveServer(CallbackTableRuntime& value,ISocketListener& first,ISocketListener& second) {
    if(!OpenEndpoint(value,1,first) || !OpenEndpoint(value,2,second)) return false;
    // Pair harness starts the client only after this combined readiness marker.
    std::puts("stage=Listen action=RETURN rc=0 listeners=2 readiness=BOTH_LISTENERS_READY");
    unsigned seen=0;
    while(!value.failed && value.Now()<12000000) {
        QueuedEcho echo;
        if(!value.Pop(echo)) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
        unsigned sequence=99;
        if(!ValidateCallbackTableRecord(echo.bytes.data(),echo.bytes.size(),value.args.run,sequence) ||
                sequence!=value.records || (seen&(1u<<sequence)) || echo.entrance!=callback_record_slots[sequence]) {
            value.Fail("server_wrong_identity_or_duplicate",echo.socket,echo.entrance); return false;
        }
        int accepted=0;
        { std::lock_guard<std::mutex> lock(value.gate); accepted=value.accepted[echo.entrance-1]; }
        if(echo.socket!=accepted || accepted<=0) { value.Fail("server_wrong_socket",echo.socket,echo.entrance); return false; }
        seen|=1u<<sequence;
        if(!SendRecord(value,echo.socket,sequence,echo.bytes)) return false;
        value.Log("server_echo_exact",echo.socket,echo.entrance,0,sequence);
        if(++value.records==4) {
            value.completed=true;
            // SendBytes success alone does not establish the client's final
            // observation. Keep both channels open until that client performs
            // its own cleanup; its OBSERVED summary is also required by the run.
            while(!value.failed && value.Now()<12000000) {
                {
                    std::lock_guard<std::mutex> lock(value.gate);
                    if(value.peer_closed[0] && value.peer_closed[1]) {
                        value.Log("client_closed_both_after_echoes",0,0,0);
                        return true;
                    }
                }
                QueuedEcho duplicate;
                if(value.Pop(duplicate)) value.Fail("unexpected_extra_record",duplicate.socket,duplicate.entrance);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            value.Fail("client_close_not_observed_within_fixture");
            return false;
        }
    }
    return false;
}
bool ObserveCallbackTable(CallbackTableRuntime& value) {
    value.provider=dlopen("/system/lib64/platformsdk/libsoftbus_client.z.so",RTLD_NOW|RTLD_LOCAL);
    if(!value.provider) return false;
    value.create=reinterpret_cast<decltype(&Socket)>(dlsym(value.provider,"Socket"));
    value.listen=reinterpret_cast<decltype(&Listen)>(dlsym(value.provider,"Listen"));
    value.bind=reinterpret_cast<decltype(&Bind)>(dlsym(value.provider,"Bind"));
    value.send=reinterpret_cast<decltype(&SendBytes)>(dlsym(value.provider,"SendBytes"));
    value.shutdown=reinterpret_cast<decltype(&Shutdown)>(dlsym(value.provider,"Shutdown"));
    if(!value.create || !value.listen || !value.bind || !value.send || !value.shutdown) return false;
    auto first=MakeListener<1>(),second=MakeListener<2>();
    std::printf("stage=callback_entries distinct_bytes=%d distinct_bind=%d profile=BYTES_NATIVE\n",
        first.OnBytes!=second.OnBytes,first.OnBind!=second.OnBind);
    if(first.OnBytes==second.OnBytes || first.OnBind==second.OnBind) return false;
    const bool result=value.args.server?ObserveServer(value,first,second):ObserveClient(value,first,second);
    if(result) {
        const auto end=Clock::now()+std::chrono::milliseconds(300);
        while(Clock::now()<end) {
            QueuedEcho duplicate;
            if(value.Pop(duplicate)) value.Fail("unexpected_extra_record",duplicate.socket,duplicate.entrance);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return result && !value.failed;
}
bool CloseCallbackTable(CallbackTableRuntime& value) {
    value.closing=true;
    const auto end=Clock::now()+std::chrono::milliseconds(200);
    do {
        std::array<int,6> targets{};
        {
            std::lock_guard<std::mutex> lock(value.gate);
            for(std::size_t i=0;i<value.owned.size();++i) if(value.owned[i]>0 && !value.claimed[i]) {
                targets[i]=value.owned[i]; value.claimed[i]=true;
            }
        }
        for(int id:targets) if(id>0 && value.shutdown) {
            value.Log("Shutdown_enter",id,0,0); value.shutdown(id);
            ++value.shutdown_returns; value.Log("Shutdown_return",id,0,0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while(Clock::now()<end);
    return value.callbacks==0;
}
class CallbackTokenOperations final : public TokenEntryOperations {
public:
    explicit CallbackTokenOperations(ProbeArguments value) : args(std::move(value)) {}
    TaskCheckpoint ObserveTasks() override {
        TaskCheckpoint out; out.process_id = getpid(); out.current_tid = syscall(SYS_gettid);
        DIR* dir = opendir("/proc/self/task"); out.opened = dir != nullptr;
        if (dir) {
            bool valid = true;
            for (;;) {
                errno = 0; const auto* entry = readdir(dir);
                if (!entry) { out.complete = valid && errno == 0; break; }
                if (entry->d_name[0] == '.') continue;
                std::uint64_t tid = 0; const auto* end = entry->d_name + std::strlen(entry->d_name);
                const auto parsed = std::from_chars(entry->d_name, end, tid);
                if (parsed.ec != std::errc{} || parsed.ptr != end || tid == 0) valid = false;
                if (out.count == 0) out.sole_tid = tid;
                if (out.count < 2) ++out.count;
            }
            if (closedir(dir) != 0) out.complete = false;
        }
        return out;
    }
    std::uint64_t GetSelf() override { return GetSelfTokenID(); }
    int SetSelf(std::uint64_t token) override { return SetSelfTokenID(token); }
    OwnedTokenObservation VerifyOwned(std::uint64_t token, const char* phase) override {
        using namespace OHOS::Security::AccessToken;
        const auto id = static_cast<AccessTokenID>(token);
        const auto lookup = AccessTokenKit::GetNativeTokenId(process_name);
        const auto type = AccessTokenKit::GetTokenTypeFlag(id);
        NativeTokenInfo info{}; const int rc = AccessTokenKit::GetNativeTokenInfo(id, info);
        std::vector<std::int32_t> states;
        const int query = AccessTokenKit::VerifyAccessToken(id, std::vector<std::string>{permission}, states, true);
        std::printf("stage=identity phase=%s lookup_match=%d query_rc=%d states=%zu\n", phase, lookup == id, query, states.size());
        return {lookup, static_cast<int>(type), static_cast<int>(TOKEN_NATIVE), rc, std::move(info.processName),
            static_cast<int>(info.apl), static_cast<int>(APL_NORMAL), query, std::move(states), static_cast<int>(PERMISSION_GRANTED)};
    }
    void Diagnostic(const char* stage, std::uint64_t first, std::uint64_t second, int code) override {
        std::printf("stage=%s expected=%llu observed=%llu rc=%d\n", stage,
                static_cast<unsigned long long>(first), static_cast<unsigned long long>(second), code);
    }
    bool EnterProvider() override { runtime = new CallbackTableRuntime(args); return ObserveCallbackTable(*runtime); }
    bool CloseProvider() override { return runtime ? CloseCallbackTable(*runtime) : true; }
private:
    ProbeArguments args;
};

}
int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    ProbeArguments args;
    std::uint64_t token=0;
    if(argc!=10 || std::strcmp(argv[1],"--role") ||
            (std::strcmp(argv[2],"server") && std::strcmp(argv[2],"client")) ||
            std::strcmp(argv[3],"--run") || std::strlen(argv[4])!=16 ||
            std::strcmp(argv[5],"--peer-network-id") || std::strlen(argv[6])!=64 ||
            std::strcmp(argv[7],"--expected-token") || !ParseTokenArgument(argv[8],token) ||
            std::strcmp(argv[9],"--preflight-confirmed")) {
        std::fprintf(stderr,"Usage: softbus_callback_table_probe --role server|client --run HEX16 --peer-network-id HEX64 --expected-token OWNED_ID --preflight-confirmed\n");
        return 64;
    }
    args.server=std::strcmp(argv[2],"server")==0; args.run_text=argv[4]; args.network=argv[6];
    for(char ch:args.run_text+args.network) if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f'))) return 64;
    const auto parsed=std::from_chars(args.run_text.data(),args.run_text.data()+args.run_text.size(),args.run,16);
    if(parsed.ec!=std::errc{} || parsed.ptr!=args.run_text.data()+args.run_text.size() || args.run==0 ||
            !AdmitTokenEntry({TokenEntryMode::Record,true,true,token})) return 64;
    std::puts("scope=CALLBACK_TABLE_OWNERSHIP topology=TWO_LIVE_OUTBOUNDS profile=BYTES_NATIVE production=NOT_IMPLEMENTED deadlines=FIXTURE_ONLY");
    CallbackTokenOperations operations(args); TokenFirstEntry entry(operations);
    const bool ok=entry.Run({TokenEntryMode::Record,true,true,token});
    const bool success=ok && runtime && runtime->completed && !runtime->failed && runtime->records==4;
    const char* model="INCOMPLETE";
    if(runtime && runtime->args.server) model="SERVER_TWO_SLOTS";
    else if(runtime && runtime->verdict==CallbackTableVerdict::SharedClientTable) model="SHARED_CLIENT_TABLE";
    else if(runtime && runtime->verdict==CallbackTableVerdict::PerSocketTable) model="PER_SOCKET_TABLE";
    else if(runtime && runtime->verdict==CallbackTableVerdict::Inconsistent) model="INCONSISTENT";
    std::printf("result=%s model=%s records=%u token_restored=%d shutdown_returns=%u callbacks_observed_active=%u sdk_quiescence=NOT_PROVEN production_qualification=BLOCKED\n",
        success?"OBSERVED":"INCOMPLETE",model,runtime?runtime->records:0,entry.RestoreSucceeded(),
        runtime?runtime->shutdown_returns:0,runtime?runtime->callbacks.load():0);
    return success?0:6;
}
