#include "PublicSocketRuntimeProbe.hpp"
#include "PublicSocketRuntimeTrace.hpp"
#include "token_first_entry.hpp"
#include "../experiments/fastdds_socket/TokenBridge.h"
#include <dirent.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <dlfcn.h>
#include <charconv>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <map>
#include <thread>
#include <chrono>

using namespace fastdds_dsoftbus::dsoftbus::runtime_probe;
namespace {
struct Invocation {ProbeConfig config;std::string mode,control;std::uint64_t token=0;};
bool Parse(int argc,char** argv,Invocation& v){
    std::map<std::string,std::string> args;
    for(int i=1;i<argc;i+=2){if(i+1==argc||!args.emplace(argv[i],argv[i+1]).second)return false;}
    for(auto& e:args)if(e.first!="--mode"&&e.first!="--run"&&e.first!="--side"&&e.first!="--control"&&e.first!="--peer"&&e.first!="--token")return false;
    v.mode=args["--mode"];v.config.run=args["--run"];v.control=args["--control"];
    if(args["--side"]!="A"&&args["--side"]!="B")return false;v.config.side=args["--side"]=="A"?0:1;
    if(v.control.rfind("/data/local/tmp/",0)||v.control.find("..")!=std::string::npos)return false;
    for(char c:v.control)if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='/'||c=='.'||c=='-'||c=='_'))return false;
    if(v.mode=="load-check"){
        if(args.count("--token")||args.count("--peer"))return false;
        v.config.peer_network="load-only";return ValidConfig(v.config);
    }
if(v.mode!="socket"||!fastdds_dsoftbus_probe::ParseTokenArgument(args["--token"],v.token))return false;
    if(v.token!=(v.config.side?671320465u:671793191u))return false;
    v.config.peer_network=args["--peer"];if(v.config.peer_network.size()!=64)return false;
    for(char c:v.config.peer_network)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return ValidConfig(v.config);
}
bool Stage(const Invocation& v,const char* stage){
    std::printf("PROBE_STAGE run=%s side=%c pid=%d stage=%s\n",v.config.run.c_str(),v.config.side?'B':'A',getpid(),stage);std::fflush(stdout);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(std::chrono::steady_clock::now()<deadline){std::ifstream file(v.control+"."+stage);std::string line;std::getline(file,line);if(line==v.config.run+" "+stage)return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    std::printf("PROBE_CONTROL_TIMEOUT stage=%s\n",stage);return false;
}
bool Symbols(){
    for(const char* name:{"Socket","Listen","Bind","SendBytes","Shutdown"}){void* p=dlsym(RTLD_DEFAULT,name);Dl_info info{};if(!p||!dladdr(p,&info)||!info.dli_fname)return false;std::printf("PROBE_SYMBOL name=%s origin=%s\n",name,info.dli_fname);}
    return true;
}
class Entry final:public fastdds_dsoftbus_probe::TokenEntryOperations {
public:
    explicit Entry(const Invocation& v):invocation(v){}
fastdds_dsoftbus_probe::TaskCheckpoint ObserveTasks()override{
fastdds_dsoftbus_probe::TaskCheckpoint out;out.process_id=getpid();out.current_tid=syscall(SYS_gettid);DIR* directory=opendir("/proc/self/task");out.opened=directory!=nullptr;
        if(directory){bool valid=true;for(;;){errno=0;auto* item=readdir(directory);if(!item){out.complete=valid&&!errno;break;}if(item->d_name[0]=='.')continue;std::uint64_t tid=0;const char* end=item->d_name+std::strlen(item->d_name);auto rc=std::from_chars(item->d_name,end,tid);if(rc.ec!=std::errc{}||rc.ptr!=end||!tid)valid=false;if(!out.count)out.sole_tid=tid;if(out.count<2)++out.count;}if(closedir(directory))out.complete=false;}
if(!fastdds_dsoftbus_probe::IsSingleMainTask(out)&&first_failure.empty())first_failure="INITIAL_TASKS_NOT_SINGLE_MAIN";
std::printf("stage=INITIAL_TASKS pid=%d single_main=%d\n",getpid(),fastdds_dsoftbus_probe::IsSingleMainTask(out));std::fflush(stdout);return out;
    }
    std::uint64_t GetSelf()override{return MddsTokenBridgeGetSelfToken();}
    int SetSelf(std::uint64_t token)override{++set_calls;return MddsTokenBridgeSetSelfToken(token);}
fastdds_dsoftbus_probe::OwnedTokenObservation VerifyOwned(std::uint64_t token,const char* phase)override{
        MddsTokenBridgeResult result{};const int rc=MddsTokenBridgeVerify(static_cast<std::uint32_t>(token),&result);
        if((rc||!result.granted||result.called!=15)&&first_failure.empty())first_failure=std::string("TOKEN_VERIFY_")+phase;
        std::printf("PROBE_TOKEN_VERIFY phase=%s bridge=%d called=%u info_rc=%d permission_rc=%d count=%u granted=%d\n",phase,rc,result.called,result.infoRc,result.permissionRc,result.permissionCount,result.granted);
fastdds_dsoftbus_probe::OwnedTokenObservation out;if(rc||result.called!=15||strnlen(result.processName,sizeof(result.processName))==sizeof(result.processName))return out;
        out.lookup=result.lookupToken;out.type=result.type;out.native_type=result.nativeType;out.info_rc=result.infoRc;out.process_name=result.processName;out.apl=result.apl;out.normal_apl=result.normalApl;out.query_rc=result.permissionRc;out.permission_granted=result.permissionGranted;if(result.permissionCount==1)out.states.push_back(result.permissionState);return out;
    }
    void Diagnostic(const char* phase,std::uint64_t expected,std::uint64_t actual,int rc)override{ObserveTokenDiagnostic(first_failure,phase,expected,actual,rc);std::printf("PROBE_TOKEN phase=%s matches=%d rc=%d\n",phase,expected==actual,rc);std::fflush(stdout);}
    bool EnterProvider()override{result=RunProbe(invocation.config,[&](const char* s){return Stage(invocation,s);});return result.success;}
    bool CloseProvider()override{return result.drained;}
    const Invocation& invocation;ProbeResult result;unsigned set_calls=0;std::string first_failure;
};
}
int main(int argc,char** argv){
    Invocation invocation;if(!Parse(argc,argv,invocation)){std::fputs("PROBE_FIRST_FAILURE invalid_cli\n",stderr);return 2;}
    std::printf("PROBE_MAIN run=%s side=%c pid=%d mode=%s headers=AUTHORIZED_SOURCE_CHECKOUT sdk=API23 sdk_abi=NOT_ESTABLISHED\n",invocation.config.run.c_str(),invocation.config.side?'B':'A',getpid(),invocation.mode.c_str());std::fflush(stdout);
    std::printf("PROBE_BUILD source_receipt=%s socket_header=%s trans_type_header=%s\n",FASTDDS_DSOFTBUS_RUNTIME_SOURCE_SHA256,FASTDDS_DSOFTBUS_RUNTIME_SOCKET_SHA256,FASTDDS_DSOFTBUS_RUNTIME_TYPE_SHA256);std::fflush(stdout);
    if(!Symbols()){std::fputs("PROBE_FIRST_FAILURE symbol_origin\n",stderr);return 3;}
    if(invocation.mode=="load-check"){
        const int rc=ProbeLoadCheck();
        // Read-only existing permission bridge; no grant, SetSelf or Socket API.
        MddsTokenBridgeResult permission{};const std::uint32_t expected=invocation.config.side?671320465u:671793191u;
        const int query=MddsTokenBridgeVerify(expected,&permission);
        const bool denied=query==0&&permission.called==15&&permission.lookupToken==expected&&permission.identityMatch&&permission.type==permission.nativeType&&permission.permissionRc==0&&permission.permissionCount==1&&permission.permissionState==-1;
        std::printf("PROBE_DENIED_READBACK bridge=%d called=%u identity=%d permission_rc=%d count=%u state=%d denied=%d\n",query,permission.called,permission.identityMatch,permission.permissionRc,permission.permissionCount,permission.permissionState,denied);std::fflush(stdout);
        const bool collected=Stage(invocation,"LOAD");return !rc&&collected&&denied?0:4;
    }
Entry operations(invocation);fastdds_dsoftbus_probe::TokenFirstEntry entry(operations);
const bool result=entry.Run({fastdds_dsoftbus_probe::TokenEntryMode::Record,true,true,invocation.token});
    if(!result){const auto& first=operations.result.first_failure.empty()?operations.first_failure:operations.result.first_failure;std::printf("PROBE_FIRST_FAILURE predicate=%s\n",first.empty()?"TOKEN_ENTRY_UNKNOWN":first.c_str());}
    std::printf("PROBE_RESTORATION state=%s set_calls=%u\n",RestorationLabel(operations.set_calls,entry.RestoreSucceeded()),operations.set_calls);
    DumpTrace();std::printf("PROBE_EXIT run=%s pass=%d restored=%d trace_overflow=%zu\n",invocation.config.run.c_str(),result,entry.RestoreSucceeded(),TraceOverflow());std::fflush(stdout);
    return result&&entry.RestoreSucceeded()&&TraceOverflow()==0?0:5;
}
