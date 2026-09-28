#include "PublicSocketDdsExperiment.hpp"
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

using namespace mdds::dsoftbus::dds_experiment;
using mdds::dsoftbus::runtime_probe::DumpTrace;
using mdds::dsoftbus::runtime_probe::TraceOverflow;
namespace {
struct Invocation {Config config;std::string control;std::uint64_t token=0;};
bool Parse(int argc,char** argv,Invocation& v){
    std::map<std::string,std::string> args;
    for(int i=1;i<argc;i+=2){if(i+1==argc||!args.emplace(argv[i],argv[i+1]).second)return false;}
    for(auto& e:args)if(e.first!="--cycles"&&e.first!="--scenario"&&e.first!="--delete-first"&&e.first!="--run"&&e.first!="--side"&&e.first!="--control"&&e.first!="--peer"&&e.first!="--token")return false;
    if(args.count("--cycles")){const auto& s=args["--cycles"];auto r=std::from_chars(s.data(),s.data()+s.size(),v.config.cycles);if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size())return false;}
    if(args.count("--scenario"))v.config.scenario=args["--scenario"];v.config.run=args["--run"];v.control=args["--control"];
    if(args["--delete-first"]!="A"&&args["--delete-first"]!="B")return false;v.config.delete_first=args["--delete-first"]=="A"?0:1;
    if(args["--side"]!="A"&&args["--side"]!="B")return false;v.config.side=args["--side"]=="A"?0:1;
    if(v.control.rfind("/data/local/tmp/",0)||v.control.find("..")!=std::string::npos)return false;
    for(char c:v.control)if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='/'||c=='.'||c=='-'||c=='_'))return false;
    if(!mdds_probe::ParseTokenArgument(args["--token"],v.token))return false;
    if(v.token!=(v.config.side?671320465u:671793191u))return false;
    v.config.peer_network=args["--peer"];if(v.config.peer_network.size()!=64)return false;
    for(char c:v.config.peer_network)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return Valid(v.config);
}
bool Stage(const Invocation& v,const char* stage){
    std::printf("DDS_STAGE run=%s side=%c pid=%d stage=%s\n",v.config.run.c_str(),v.config.side?'B':'A',getpid(),stage);std::fflush(stdout);
    if(std::strcmp(stage,"DDS_DELETED")==0||std::string(stage)=="C"+std::to_string(v.config.cycles)+"_DDS_DELETED")return true;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    const auto marker=v.config.run+" "+(v.config.cycles>1?std::string(v.config.side?"B ":"A "):std::string())+stage;
    while(std::chrono::steady_clock::now()<deadline){std::ifstream file(v.control+"."+stage);std::string line;std::getline(file,line);if(line==marker)return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    std::printf("DDS_CONTROL_TIMEOUT stage=%s\n",stage);return false;
}
bool Symbols(){
    for(const char* name:{"Socket","Listen","Bind","SendBytes","Shutdown"}){void* p=dlsym(RTLD_DEFAULT,name);Dl_info info{};if(!p||!dladdr(p,&info)||!info.dli_fname)return false;std::printf("DDS_SYMBOL name=%s origin=%s\n",name,info.dli_fname);}
    return true;
}
class Entry final:public mdds_probe::TokenEntryOperations {
public:
    explicit Entry(const Invocation& v):invocation(v){}
    mdds_probe::TaskCheckpoint ObserveTasks()override{
        mdds_probe::TaskCheckpoint out;out.process_id=getpid();out.current_tid=syscall(SYS_gettid);DIR* directory=opendir("/proc/self/task");out.opened=directory!=nullptr;
        if(directory){bool valid=true;for(;;){errno=0;auto* item=readdir(directory);if(!item){out.complete=valid&&!errno;break;}if(item->d_name[0]=='.')continue;std::uint64_t tid=0;const char* end=item->d_name+std::strlen(item->d_name);auto rc=std::from_chars(item->d_name,end,tid);if(rc.ec!=std::errc{}||rc.ptr!=end||!tid)valid=false;if(!out.count)out.sole_tid=tid;if(out.count<2)++out.count;}if(closedir(directory))out.complete=false;}
        if(!mdds_probe::IsSingleMainTask(out)&&first_failure.empty())first_failure="INITIAL_TASKS_NOT_SINGLE_MAIN";
        std::printf("stage=INITIAL_TASKS pid=%d single_main=%d\n",getpid(),mdds_probe::IsSingleMainTask(out));std::fflush(stdout);return out;
    }
    std::uint64_t GetSelf()override{return MddsTokenBridgeGetSelfToken();}
    int SetSelf(std::uint64_t token)override{++set_calls;return MddsTokenBridgeSetSelfToken(token);}
    mdds_probe::OwnedTokenObservation VerifyOwned(std::uint64_t token,const char* phase)override{
        MddsTokenBridgeResult result{};const int rc=MddsTokenBridgeVerify(static_cast<std::uint32_t>(token),&result);
        if((rc||!result.granted||result.called!=15)&&first_failure.empty())first_failure=std::string("TOKEN_VERIFY_")+phase;
        std::printf("PROBE_TOKEN_VERIFY phase=%s bridge=%d called=%u info_rc=%d permission_rc=%d count=%u granted=%d\n",phase,rc,result.called,result.infoRc,result.permissionRc,result.permissionCount,result.granted);
        mdds_probe::OwnedTokenObservation out;if(rc||result.called!=15||strnlen(result.processName,sizeof(result.processName))==sizeof(result.processName))return out;
        out.lookup=result.lookupToken;out.type=result.type;out.native_type=result.nativeType;out.info_rc=result.infoRc;out.process_name=result.processName;out.apl=result.apl;out.normal_apl=result.normalApl;out.query_rc=result.permissionRc;out.permission_granted=result.permissionGranted;if(result.permissionCount==1)out.states.push_back(result.permissionState);return out;
    }
    void Diagnostic(const char* phase,std::uint64_t expected,std::uint64_t actual,int rc)override{const std::string name=phase;const bool failed=(name=="original_token"&&expected==0)||((name=="select_token"||name=="restore_token")&&(rc!=0||expected!=actual))||name=="initialization_or_record_exception";if(failed&&first_failure.empty())first_failure=name;std::printf("PROBE_TOKEN phase=%s matches=%d rc=%d\n",phase,expected==actual,rc);std::fflush(stdout);}
    bool EnterProvider()override{if(!IsolateProcess()){first_failure="DDS_PROCESS_ISOLATION";result.drained=true;return false;}result=Run(invocation.config,[&](const char* s){return Stage(invocation,s);});return result.success;}
    bool CloseProvider()override{return result.drained;}
    const Invocation& invocation;Result result;unsigned set_calls=0;std::string first_failure;
};
}
int main(int argc,char** argv){
    Invocation invocation;if(!Parse(argc,argv,invocation)){std::fputs("PROBE_FIRST_FAILURE invalid_cli\n",stderr);return 2;}
    std::printf("DDS_MAIN run=%s side=%c pid=%d mode=dds headers=AUTHORIZED_SOURCE_CHECKOUT sdk=API23 sdk_abi=NOT_ESTABLISHED\n",invocation.config.run.c_str(),invocation.config.side?'B':'A',getpid());std::fflush(stdout);
    std::printf("DDS_BUILD source_receipt=%s socket_header=%s trans_type_header=%s\n",MDDS_RUNTIME_SOURCE_SHA256,MDDS_RUNTIME_SOCKET_SHA256,MDDS_RUNTIME_TYPE_SHA256);std::fflush(stdout);
    if(!Symbols()){std::fputs("PROBE_FIRST_FAILURE symbol_origin\n",stderr);return 3;}
    Entry operations(invocation);mdds_probe::TokenFirstEntry entry(operations);
    const bool result=entry.Run({mdds_probe::TokenEntryMode::Record,true,true,invocation.token});
    if(!result){const auto& first=operations.result.first_failure.empty()?operations.first_failure:operations.result.first_failure;std::printf("PROBE_FIRST_FAILURE predicate=%s\n",first.empty()?"TOKEN_ENTRY_UNKNOWN":first.c_str());}
    std::printf("PROBE_RESTORATION state=%s set_calls=%u\n",(operations.set_calls==0?"NOT_NEEDED":entry.RestoreSucceeded()?"PASS":"FAIL"),operations.set_calls);
    DumpFrames();DumpTrace();std::printf("DDS_EXIT run=%s pass=%d restored=%d trace_overflow=%zu frame_overflow=%zu\n",invocation.config.run.c_str(),result,entry.RestoreSucceeded(),TraceOverflow(),FrameOverflow());std::fflush(stdout);
    return result&&entry.RestoreSucceeded()&&TraceOverflow()==0&&FrameOverflow()==0?0:5;
}
