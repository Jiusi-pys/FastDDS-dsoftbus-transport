#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "native_files.hpp"
#include "goal_control.hpp"
#include "pair_record.hpp"
#include "goal_base.h"
#include "goal_diagnostics.hpp"
#include "goal_record.hpp"
#include "softbus_bus_center.h"
#include <unistd.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <thread>
#include <stdexcept>

extern "C" int32_t ns_consumer_get_run_api(uint32_t,uint32_t,NS_RunApi*);
namespace {
using namespace stage1_goal;
void need(bool v,const char* c){if(!v)throw std::runtime_error(c);}
uint64_t decimal(const std::string& s){need(!s.empty(),"GOAL_DECIMAL");uint64_t n=0;for(char c:s){need(c>='0'&&c<='9'&&n<=(UINT64_MAX-unsigned(c-'0'))/10,"GOAL_DECIMAL");n=n*10+unsigned(c-'0');}return n;}
std::string field(const std::string& b,const char* name){return goal_field(b,name);}
bool main_restore(Scalar& s,const ResumeState& q,const Admission& original,bool& invoked,int32_t& rc,bool& rc_valid){
    if(invoked)return false;invoked=true; // Before any effect; output failure never retries SetSelf.
    auto p=native_point();if(p.pid!=q.pid||p.tid!=q.pid||p.parent!=q.parent||p.birth!=q.birth||p.uid!=original.uid||p.euid!=original.euid||p.gid!=original.gid||p.egid!=original.egid)return false;
    if(q.attempted){rc=s.set(q.original);rc_valid=true;if(rc)return false;}return s.get()==q.original;
}
template<class F>void* address(F f){void* p=nullptr;static_assert(sizeof(p)==sizeof(f),"native fn pointer ABI");std::memcpy(&p,&f,sizeof(p));return p;}
template<class F>F sdk(const char* name){void* p=dlsym(RTLD_DEFAULT,name);Dl_info d{};need(p&&dladdr(p,&d)&&d.dli_fname,"BUSCENTER_SYMBOL");need(file_hash(d.dli_fname)==GOAL_SOFTBUS_SHA,"BUSCENTER_PROVIDER_BYTES");F f{};std::memcpy(&f,&p,sizeof(f));return f;}
struct Guarded {
    void* mapping=MAP_FAILED;size_t page=0,bytes=0;uint8_t* data=nullptr;
    explicit Guarded(size_t n):bytes(n){long p=sysconf(_SC_PAGESIZE);need(p>=4096&&p<=65536&&n<=static_cast<size_t>(p),"OUTPUT_PAGE");page=static_cast<size_t>(p);mapping=mmap(nullptr,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);need(mapping!=MAP_FAILED&&mprotect(static_cast<uint8_t*>(mapping)+page,page,PROT_READ|PROT_WRITE)==0,"OUTPUT_GUARD");data=static_cast<uint8_t*>(mapping)+2*page-n;std::memset(data,0,n);}
    ~Guarded(){if(mapping!=MAP_FAILED)munmap(mapping,page*3);}
};
struct Node {std::string network,udid;};
Node local_node(GoalDiagnostic& diagnostic){
    static_assert(offsetof(NodeBasicInfo,networkId)==0&&sizeof(NodeBasicInfo)<=2048,"Selected C header prefix/maximum, not full deployed ABI proof");
    using Local=int32_t(*)(const char*,NodeBasicInfo*);using Key=int32_t(*)(const char*,const char*,NodeDeviceInfoKey,uint8_t*,int32_t);
    auto get=sdk<Local>("GetLocalNodeDeviceInfo");auto key=sdk<Key>("GetNodeKeyInfo");
    // Header/deployed layout association remains a Native qualification item.
    // Guard pages isolate oversized writes; a fault is FAIL, never ABI PASS.
    diagnostic.at(GoalPhase::LocalNode);Guarded info(sizeof(NodeBasicInfo));const auto local_rc=get(NS_PACKAGE,reinterpret_cast<NodeBasicInfo*>(info.data));diagnostic.returned(local_rc);need(local_rc==0,"LOCAL_NODE_API");
    const char* network=reinterpret_cast<const char*>(info.data);need(std::memchr(network,0,NETWORK_ID_BUF_LEN)&&std::strlen(network)==NETWORK_ID_BUF_LEN-1,"LOCAL_NETWORK_FIELD");
    diagnostic.at(GoalPhase::NodeKey);Guarded udid(UDID_BUF_LEN);const auto key_rc=key(NS_PACKAGE,network,NODE_KEY_UDID,udid.data,UDID_BUF_LEN);diagnostic.returned(key_rc);need(key_rc==0,"LOCAL_UDID_API");
    const char* value=reinterpret_cast<const char*>(udid.data);need(std::memchr(value,0,UDID_BUF_LEN)&&std::strlen(value)>0&&std::strlen(value)<UDID_BUF_LEN,"LOCAL_UDID_FIELD");
    Node n{network,value};for(const auto& s:{n.network,n.udid})for(unsigned char c:s)need(c>=33&&c<=126,"LOCAL_NODE_TEXT");return n;
}
int run_data(Files& files,Scalar& scalar,const ResumeState& state,const stage1_entry::Record& admission,const std::string& side,bool& local_safe,GoalDiagnostic& diagnostic){
    diagnostic.at(GoalPhase::LocalNode);need(scalar.get()==state.target,"DATA_SELF_BEFORE_SDK");files.publish("stage."+side,diagnostic.fields());const auto node=local_node(diagnostic);
    files.publish("node."+side,"format=S1GOALNODE1\nrun="+std::to_string(state.run)+"\nside="+side+"\npid="+std::to_string(state.pid)+"\nparent="+std::to_string(state.parent)+"\ntarget="+std::to_string(state.target)+"\nnetwork="+node.network+"\nudid="+node.udid+"\nimage_sha="+state.image_sha+"\n");
    diagnostic.at(GoalPhase::Pair);const auto end=Clock::time_point(std::chrono::nanoseconds(state.until));need(files.wait("pair",end),"PAIR_DEADLINE");const auto bytes=files.read("pair",8192);const auto pair=PairRecord::parse(text(bytes));const auto digest=pure_hash::digest(bytes.data(),bytes.size());
    need(pair.f[PImageSha]==state.image_sha&&pair.f[PBridgeSha]==GOAL_BRIDGE_SHA&&pair.f[PNamespace]==admission.value[stage1_entry::NameSpace]&&pair.f[PPrefix]==admission.value[stage1_entry::Prefix]&&pair.num(PDomain,UINT32_MAX)==admission.domain,"PAIR_ADMISSION_SCOPE");
    const unsigned base=state.side?BPid:APid;need(pair.num(static_cast<PairField>(base))==state.pid&&pair.num(static_cast<PairField>(base+1))==state.parent&&pair.num(static_cast<PairField>(base+3))==state.target&&pair.f[base+4]==node.udid&&pair.f[base+5]==node.network,"PAIR_ACTUAL_NODE");
    auto request=pair.request(state.side,digest,state.until);need(request.local.run==state.run,"PAIR_CURRENT_RUN");
    diagnostic.at(GoalPhase::Api);NS_RunApi api{};const auto getter_rc=ns_consumer_get_run_api(NS_RUN_VERSION,sizeof(api),&api);diagnostic.returned(getter_rc);need(getter_rc==NS_OK,"CURRENT_CONSUMER_GETTER");need(api.header.version==NS_RUN_VERSION&&api.header.size==sizeof(api)&&api.header.scope==NS_SCOPE&&!api.header.reserved&&api.create&&api.execute&&api.cancel&&api.destroy,"CURRENT_CONSUMER_ABI");
    Dl_info owner{};need(dladdr(address(&ns_consumer_get_run_api),&owner)&&owner.dli_fbase,"CURRENT_CONSUMER_OWNER");for(void* p:{address(api.create),address(api.execute),address(api.cancel),address(api.destroy)}){Dl_info d{};need(dladdr(p,&d)&&d.dli_fbase==owner.dli_fbase,"SAME_IMAGE_API_OWNER");}
    // Retain this callback capsule until process exit; never keep a hidden
    // Runtime facade/anchor or call dlclose while SDK last-use is unproved.
    auto* control=new GoalControl(files,scalar,request);const auto callbacks=control->callbacks();NS_RunHandle* handle=nullptr;
    diagnostic.at(GoalPhase::Create);local_safe=false;int rc=api.create(&request,&callbacks,&handle);diagnostic.returned(rc);
    if(rc!=NS_OK||!handle){if(!handle)local_safe=true;need(false,"CONSUMER_CREATE");}
    diagnostic.at(GoalPhase::Watch);files.publish("stage."+side,diagnostic.fields());control->start_watch(api,handle);
    diagnostic.at(GoalPhase::Execute);files.publish("stage."+side,diagnostic.fields());NS_RunResult result{{NS_RUN_VERSION,sizeof(result),NS_SCOPE,0},NS_STATE,0,0,0,0,0,1,0};rc=api.execute(handle,&result);diagnostic.returned(rc);const auto primary=diagnostic;
    diagnostic.at(GoalPhase::Drain);const bool drained=control->finish(end);const bool result_abi=result.header.version==NS_RUN_VERSION&&result.header.size==sizeof(result)&&result.header.scope==NS_SCOPE&&!result.header.reserved;
    const bool users_done=drained&&result_abi&&!result.owners_retained&&rc!=NS_UNLOAD;
    diagnostic.at(GoalPhase::Destroy);const int destroy=users_done?api.destroy(handle):NS_UNLOAD;if(users_done)diagnostic.returned(destroy);local_safe=users_done&&destroy==NS_OK;
    const bool local_complete=local_safe&&rc==NS_OK&&result.status==NS_OK&&result.phase==4&&result.writes==3&&result.takes==3;
    // With users not drained, even the Files publication map may still be in
    // use by a callback. Retain it; do not write a result through unknown owners.
    if(!local_safe){std::printf("GOAL_LIFETIME_UNCERTAIN phase=%u native_rc_valid=%d native_rc=%d retained_until_exit=1 restore_NOT_ATTEMPTED=1\n",static_cast<unsigned>(diagnostic.phase),int(diagnostic.rc_valid),diagnostic.native_rc);std::fflush(stdout);std::_Exit(90);}
    files.publish("data-result."+side,"format=S1GOALRESULT1\nstatus="+std::to_string(rc)+"\nresult_status="+std::to_string(result.status)+"\nphase="+std::to_string(result.phase)+"\nwrites="+std::to_string(result.writes)+"\ntakes="+std::to_string(result.takes)+"\nlocal_control_drained="+std::to_string(drained)+"\nowners_retained="+std::to_string(result.owners_retained)+"\nSDK_quiescence_unproven="+std::to_string(result.quiescence_unproven)+"\ndestroy_rc="+std::to_string(destroy)+"\nprimary_"+primary.fields());
    if(rc!=NS_OK)diagnostic=primary;
    return local_complete&&destroy==NS_OK?NS_OK:NS_STATE;
}
}
int main(int argc,char** argv){
    using namespace stage1_goal;
    if(argc!=6||std::strcmp(argv[1],"--resume-owned")||(std::strcmp(argv[2],"A")&&std::strcmp(argv[2],"B"))||(std::strcmp(argv[5],"--probe")&&std::strcmp(argv[5],"--data"))){std::puts("GOAL_RESUME_REFUSED no_identity_api=1");return 78;}
    std::unique_ptr<Scalar> scalar;std::unique_ptr<Files> files;ResumeState state{};Admission original{};GoalDiagnostic diagnostic;int32_t restore_rc=0;bool restore_rc_valid=false;bool origin_bound=false,local_safe=true,restore_invoked=false,restored_entry=false;
    try{
        diagnostic.at(GoalPhase::Resume);const auto fd64=decimal(argv[4]);need(fd64>=3&&fd64<=INT32_MAX,"STATE_FD_NUMBER");state=read_state(static_cast<int>(fd64));close(static_cast<int>(fd64));
        const std::string side=argv[2];files=std::make_unique<Files>(argv[3]);const auto boot=files->read("boot."+side,4096);const auto lookup=files->read("lookup."+side,4096);const auto btext=text(boot);
        need(field(btext,"format")=="S1GOALBOOT1"&&field(btext,"side")==side&&decimal(field(btext,"pid"))==state.pid&&decimal(field(btext,"parent"))==state.parent&&decimal(field(btext,"birth"))==state.birth&&decimal(field(btext,"caller"))==state.original,"OWNED_BOOT_ORIGINAL");
        stage1_entry::Record admission{};char digest[65]{};const char* why=nullptr;const auto path=files->root()+"/admission."+side;
        need(stage1_entry::verify_admission(path.c_str(),side.c_str(),field(btext,"image").c_str(),admission,digest,why),"RESUME_ADMISSION");
        auto a=bound_admission(admission,native_point(),digest,true);a.until=state.until;
        auto uid=[&](const char* key){const auto v=decimal(field(btext,key));need(v<=UINT32_MAX,"BOOT_UID_RANGE");return static_cast<uint32_t>(v);};a.uid=uid("uid");a.euid=uid("euid");a.gid=uid("gid");a.egid=uid("egid");
        const auto bsha=pure_hash::digest(boot.data(),boot.size()),lsha=pure_hash::digest(lookup.data(),lookup.size());
        need(resume_bound(state,a,native_point(),bsha.c_str(),lsha.c_str())&&self_image_hash()==state.image_sha&&!std::strcmp(admission.value[stage1_entry::BridgeSha],GOAL_BRIDGE_SHA),"RESUME_PROVENANCE");original=a;origin_bound=true;
        diagnostic.at(GoalPhase::Scalar);scalar=std::make_unique<Scalar>(GOAL_SCALAR_SHA);diagnostic.at(GoalPhase::Self);need(scalar->get()==state.target,"EXEC_TOKEN_PROPAGATION_NOT_PROVED");
        int rc=NS_STATE;
        if(!std::strcmp(argv[5],"--probe")){
            diagnostic.at(GoalPhase::Probe);need(!std::strcmp(admission.value[stage1_entry::Intent],"own"),"PROBE_INTENT");bool worker=false;auto* t=new std::thread([&]{worker=scalar->get()==state.target;});local_safe=false;t->join();local_safe=true;delete t;need(worker,"OWN_THREAD_TOKEN");
            files->publish("probe."+side,"format=S1GOALPROBE1\nrun="+std::to_string(state.run)+"\npid="+std::to_string(state.pid)+"\nentry_selected=1\none_owned_worker_selected=1\nall_SDK_workers=UNKNOWN\nproject_business_calls=0\n");rc=NS_OK;
        }else{need(!std::strcmp(admission.value[stage1_entry::Intent],"traffic"),"DATA_INTENT");rc=run_data(*files,*scalar,state,admission,side,local_safe,diagnostic);}
        const auto primary=diagnostic;restored_entry=main_restore(*scalar,state,original,restore_invoked,restore_rc,restore_rc_valid);files->publish("exit."+side,"format=S1GOALEXIT1\nrun="+std::to_string(state.run)+"\npid="+std::to_string(state.pid)+"\nstatus="+std::to_string(rc)+"\nrestored_entry="+std::to_string(restored_entry)+"\nrestore_rc_valid="+std::to_string(restore_rc_valid)+"\nrestore_rc="+std::to_string(restore_rc)+"\nworker_parent_restoration=UNKNOWN\nSDK_quiet=UNKNOWN\n"+primary.fields());
        return rc==NS_OK&&restored_entry?0:88;
    }catch(const std::exception&){
        if(!local_safe){
            // Never destroy files/scalar state still referenced by own watcher
            // or consumer callbacks, nor claim a normal restored exit.
            (void)files.release();(void)scalar.release();
            std::puts("GOAL_FAILED_LIFETIME_UNCERTAIN restore_NOT_ATTEMPTED=1");std::fflush(stdout);std::_Exit(90);
        }
        bool restored=restored_entry;if(origin_bound&&scalar&&local_safe&&!restore_invoked){try{restored=main_restore(*scalar,state,original,restore_invoked,restore_rc,restore_rc_valid);restored_entry=restored;}catch(...){}}
        if(files){try{files->publish(std::string("failure.")+argv[2],"format=S1GOALFAIL1\n"+diagnostic.fields()+"origin_bound="+std::to_string(origin_bound)+"\nrestored_entry="+std::to_string(restored)+"\nrestore_rc_valid="+std::to_string(restore_rc_valid)+"\nrestore_rc="+std::to_string(restore_rc)+"\nSDK_or_data_success=0\n");}catch(...){}}
        std::printf("GOAL_FAILED phase=%u native_rc_valid=%d native_rc=%d no_data_success=1 detailed_private_output_not_reported=1\n",static_cast<unsigned>(diagnostic.phase),int(diagnostic.rc_valid),diagnostic.native_rc);return 88;
    }
}
