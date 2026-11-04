#include "native_contract.h"
#include "entry_admission.hpp"
#include "control/v2_client.hpp"
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <time.h>
static constexpr char cpp_abi[]=NS_STR(_LIBCPP_ABI_NAMESPACE);
static_assert(sizeof(cpp_abi)==5&&cpp_abi[2]=='n'&&cpp_abi[3]=='1',"Actual selected SDK ABI __n1 required");
static_assert(sizeof(NS_Observation)==208&&sizeof(NS_LoaderSlot)==56&&sizeof(NS_IdentityApi)==24&&sizeof(NS_ConsumerApi)==32,"Native C++ POD layout");
static uint64_t ticks(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000u+uint64_t(t.tv_nsec);}
static uint64_t tid(){return static_cast<uint64_t>(syscall(SYS_gettid));}
static bool single_main(){
    if(tid()!=static_cast<uint64_t>(getpid()))return false;
    DIR* p=opendir("/proc/self/task");if(!p)return false;unsigned count=0;uint64_t sole=0;bool valid=true;errno=0;
    while(auto* e=readdir(p)){if(e->d_name[0]=='.')continue;char* end=nullptr;errno=0;auto id=std::strtoull(e->d_name,&end,10);if(errno||!end||*end||!id)valid=false;sole=id;++count;}
    if(errno)valid=false;if(closedir(p))valid=false;return valid&&count==1&&sole==tid();
}
static void observe_tasks(const char* stage){
    unsigned count=0;bool complete=true;DIR* p=opendir("/proc/self/task");
    if(!p)complete=false;
    else {errno=0;while(auto* e=readdir(p)){if(e->d_name[0]=='.')continue;if(++count>64){complete=false;break;}}if(errno)complete=false;if(closedir(p))complete=false;}
    std::printf("TASKS stage=%s pid=%d tid=%llu count=%u complete=%d entry_only_not_worker_tokens=1\n",stage,int(getpid()),static_cast<unsigned long long>(tid()),count,int(complete));
}
static void observe_symbols(const char* stage,void* scope){
    for(const char* name:{"malloc","free","_Znwm","_ZdlPv","__cfi_check","__cfi_slowpath"}){
        dlerror();void* p=dlsym(scope,name);const char* error=dlerror();Dl_info d{};const bool found=p&&!error&&dladdr(p,&d)&&d.dli_fname;
        std::printf("SYMBOL_LOOKUP stage=%s symbol=%s found=%d provider=%s address=%p LOOKUP_ONLY_NOT_GOT_OR_ALLOCATOR_EXECUTION=1\n",stage,name,int(found),found?d.dli_fname:"-",p);
    }
}
static bool owner(void* symbol,const char* expected){
    Dl_info d{};char actual[PATH_MAX]{},want[PATH_MAX]{};
    return symbol&&dladdr(symbol,&d)&&d.dli_fname&&realpath(d.dli_fname,actual)&&realpath(expected,want)&&!std::strcmp(actual,want);
}
static int admission_image_anchor(){return 0;}
static int admission_check(const char* side,const char* path,bool execute){
    auto fp=&admission_image_anchor;void* code=nullptr;static_assert(sizeof(fp)==sizeof(code),"Code address layout");std::memcpy(&code,&fp,sizeof(code));
    Dl_info info{};char image[PATH_MAX]{};
    if(!dladdr(code,&info)||!info.dli_fname||!realpath(info.dli_fname,image)){std::puts("ADMISSION_REFUSED reason=CODE_IMAGE getters_called=0 business_loads=0");return 78;}
    stage1_entry::Record record{};char digest[65]{};const char* why="UNSET";
    if(!stage1_entry::verify_admission(path,side,image,record,digest,why)||!single_main()){
        std::printf("ADMISSION_REFUSED reason=%s getters_called=0 business_loads=0\n",why);return 78;
    }
    const char* scalar="/system/lib64/platformsdk/libtokensetproc_shared.z.so";
    void* token=dlopen(scalar,RTLD_NOW|RTLD_LOCAL);if(!token){std::puts("ADMISSION_QUERY_UNAVAILABLE phase=scalar_load business_loads=0");return 81;}
    using Get=uint64_t(*)();void* p=dlsym(token,"GetSelfTokenID");Get get{};
    if(owner(p,scalar))std::memcpy(&get,&p,sizeof(get));
    if(!get||!single_main()){std::puts("ADMISSION_QUERY_UNAVAILABLE phase=scalar_owner_or_tasks business_loads=0");return 81;}
    if(!stage1_entry::admission_current(record,why)){std::printf("ADMISSION_REFUSED reason=%s getters_called=0 business_loads=0\n",why);return 78;}
    const auto before=get(),after=get();
    if(!before||before!=after){std::puts("ADMISSION_QUERY_UNAVAILABLE phase=self_read no_restore_called=1");return 81;}
    if(before!=record.caller||!stage1_entry::admission_current(record,why)){
        std::printf("ADMISSION_REFUSED reason=%s getters_called=1 caller=%llu unchanged=1 business_loads=0\n",before!=record.caller?"CALLER_MISMATCH":why,static_cast<unsigned long long>(before));return 78;
    }
    std::printf("INPUT_VALID_ONLY run=%s side=%s board=%s pid=%d tid=%llu caller=%llu declared_own_target=%llu record_sha=%s image=%s board_binding=EXTERNAL_HDC_ATTESTATION target_binding=DATED_DECLARATION os_grant=NOT_ESTABLISHED user_approval=NOT_ESTABLISHED business_loads=0 setters=0\n",record.value[stage1_entry::Run],record.value[stage1_entry::Side],record.value[stage1_entry::Board],int(getpid()),static_cast<unsigned long long>(tid()),static_cast<unsigned long long>(before),static_cast<unsigned long long>(record.target),digest,image);
    if(execute){std::puts("ADMISSION_POLICY_LOCKED no_consumer_dlopen=1 no_setter_or_restore=1 not_READY=1");return 78;}
    return 0;
}
template<class F> static F function(void* handle,const char* name,const char* file){
    void* p=dlsym(handle,name);F result{};static_assert(sizeof(result)==sizeof(p),"Native pointer representation");
    if(owner(p,file))std::memcpy(&result,&p,sizeof(result));return result;
}
template<class F> static bool function_owner(F f,const char* file){void* p=nullptr;static_assert(sizeof(p)==sizeof(f),"Function address layout");std::memcpy(&p,&f,sizeof(p));return owner(p,file);}
struct Worker {pthread_mutex_t lock;pthread_cond_t cv;bool ready=false,stop=false,cancelled=false,closed=false;};
static void* worker(void* arg){auto* w=static_cast<Worker*>(arg);pthread_mutex_lock(&w->lock);w->ready=true;pthread_cond_signal(&w->cv);while(!w->stop&&!w->cancelled)pthread_cond_wait(&w->cv,&w->lock);w->closed=true;pthread_mutex_unlock(&w->lock);return nullptr;}
static int selftest(){
    bool ok=single_main()&&ns_c_layout_check();NS_Header h{NS_VERSION,sizeof(h),NS_SCOPE,0};
    ok=ok&&ns_header(&h,sizeof(h))==NS_OK;h.version=2;ok=ok&&ns_header(&h,sizeof(h))==NS_ABI;h.version=NS_VERSION;h.size--;ok=ok&&ns_header(&h,sizeof(h))==NS_SIZE;
    for(int mode=0;mode<2;++mode){Worker w{};pthread_t thread{};bool created=false;
    if(pthread_mutex_init(&w.lock,nullptr))return 101;if(pthread_cond_init(&w.cv,nullptr)){pthread_mutex_destroy(&w.lock);return 101;}
    if(pthread_create(&thread,nullptr,worker,&w)==0){
        created=true;pthread_mutex_lock(&w.lock);while(!w.ready)pthread_cond_wait(&w.cv,&w.lock);
        ok=ok&&!single_main();observe_tasks(mode?"cancel_worker_live":"normal_worker_live");
        if(mode)w.cancelled=true;else w.stop=true;pthread_cond_signal(&w.cv);pthread_mutex_unlock(&w.lock);
    }else ok=false;
    if(created&&pthread_join(thread,nullptr)){std::puts("join_unproven=1 no_identity_restore=1");std::fflush(stdout);std::_Exit(93);}
    ok=ok&&w.ready&&w.closed&&(mode?w.cancelled:w.stop);if(mode)ok=ok&&w.cancelled;
    pthread_cond_destroy(&w.cv);pthread_mutex_destroy(&w.lock);ok=ok&&single_main();
    std::printf("SELFTEST_FLOW mode=%s worker_joined=%d closed=%d result=%d\n",mode?"cancel":"normal",int(created),int(w.closed),int(ok));observe_tasks("worker_joined");}
    std::printf("SELFTEST scope=ARM64_LOCAL_CONTRACT no_identity_api=1 no_optional_dlopen=1 normal_and_cancel=1 result=%d cpp=%s\n",int(ok),cpp_abi);return ok?0:101;
}
static int loader_test(const char* path){
    if(!path||path[0]!='/'||!single_main())return 102;
    const char* base=std::strrchr(path,'/');
    if(!base||std::strcmp(base+1,"libstage1_native_loader_fixture.so"))return 102;
    uint64_t before=ticks();void* h=dlopen(path,RTLD_NOW|RTLD_LOCAL);uint64_t after=ticks();if(!h){std::printf("fixture_load_failed=%s\n",dlerror());return 103;}
    NS_LoaderSlot slot{{NS_VERSION,sizeof(slot),NS_SCOPE,0},NS_COOKIE,NS_BORROWED,0,0,0,0,0};
    NS_LoaderSlot rejected=slot; /* Every possible borrowed slot outlives dlclose. */
    auto get=function<NS_GetLoaderApi>(h,"ns_loader_get_api",path);NS_LoaderApi api{};bool ok=get&&get(NS_VERSION,sizeof(api),&api)==NS_OK&&ns_header(&api.header,sizeof(api))==NS_OK;
    if(ok){NS_LoaderApi canary;std::memset(&canary,0xa5,sizeof(canary));NS_LoaderApi before_api=canary;ok=get(2,sizeof(canary),&canary)==NS_ABI&&!std::memcmp(&canary,&before_api,sizeof(canary));ok=ok&&get(NS_VERSION,sizeof(canary)-1,&canary)==NS_SIZE&&!std::memcmp(&canary,&before_api,sizeof(canary));}
    if(ok){ok=api.bind&&api.snapshot&&function_owner(api.bind,path)&&function_owner(api.snapshot,path);
        if(ok){rejected.ownership=0;ok=api.bind(&rejected)==NS_OWNER;rejected=slot;rejected.cookie^=1;ok=ok&&api.bind(&rejected)==NS_OWNER;}
        if(ok)ok=api.bind(&slot)==NS_OK&&api.bind(&slot)==NS_STATE&&slot.ctor_count==1&&slot.ctor_ns>=before&&slot.ctor_ns<=after;
    }
    observe_tasks("fixture_loaded");observe_symbols("fixture_global",RTLD_DEFAULT);observe_symbols("fixture_handle",h);
    if(dlclose(h)){std::puts("fixture_unload_unproven=1");std::fflush(stdout);std::_Exit(92);}
    uint64_t end=ticks();ok=ok&&slot.dtor_count==1&&slot.dtor_ns>=after&&slot.dtor_ns<=end;
    observe_tasks("fixture_unloaded");
    std::printf("LOADER_SELFTEST scope=ARM64_LIBC_FIXTURE_ONLY ctor=%u dtor=%u before_load=%llu ctor_at=%llu after_load=%llu dtor_at=%llu after_close=%llu no_real_identity=1 no_consumer_bridge_load=1 result=%d\n",slot.ctor_count,slot.dtor_count,static_cast<unsigned long long>(before),static_cast<unsigned long long>(slot.ctor_ns),static_cast<unsigned long long>(after),static_cast<unsigned long long>(slot.dtor_ns),static_cast<unsigned long long>(end),int(ok));return ok?0:104;
}
static int query_own(const char* path){
    const char* base=path?std::strrchr(path,'/'):nullptr;
    if(!path||path[0]!='/'||!base||std::strcmp(base+1,"libstage1_actionbag_identity.so"))return 81;
    observe_tasks("before_readonly_provider");
    const char* scalar="/system/lib64/platformsdk/libtokensetproc_shared.z.so";
    void* token=dlopen(scalar,RTLD_NOW|RTLD_LOCAL);if(!token){std::printf("QUERY_UNAVAILABLE phase=scalar_load reason=%s\n",dlerror());return 81;}
    using Get=uint64_t(*)();auto get=function<Get>(token,"GetSelfTokenID",scalar);
    if(!get){std::puts("QUERY_UNAVAILABLE phase=scalar_owner");return 81;}
    const uint64_t before=get();if(!before){std::puts("QUERY_UNAVAILABLE phase=self_read");return 81;}
    void* bridge=dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!bridge){std::printf("QUERY_UNAVAILABLE phase=bridge_load reason=%s\n",dlerror());return 81;}
    observe_tasks("bridge_loaded");observe_symbols("query_global",RTLD_DEFAULT);observe_symbols("query_bridge_handle",bridge);
    auto api_get=function<NS_GetIdentityApi>(bridge,"ns_identity_get_api",path);NS_IdentityApi api{};
    if(!api_get||api_get(NS_VERSION,sizeof(api),&api)!=NS_OK||ns_header(&api.header,sizeof(api))!=NS_OK||!api.query||!function_owner(api.query,path)){std::puts("QUERY_UNAVAILABLE phase=bridge_ABI_owner");return 81;}
    NS_Observation o{};o.header={NS_VERSION,sizeof(o),NS_SCOPE,0};const int rc=api.query(0,&o);const uint64_t after=get();
    observe_tasks("after_readonly_query");
    std::printf("OWN_QUERY raw_rc=%d lookup=%u type=%d native_type=%d info_rc=%d apl=%d normal_apl=%d query_rc=%d query_count=%u state=%d granted=%d called=%u name=%.127s bridge_cpp=%.7s caller_before=%llu caller_bridge=%llu caller_after=%llu no_identity_setter=1 handles_retained_until_exit=1 worker_parent_tokens=UNKNOWN\n",rc,o.lookup,o.type,o.native_type,o.info_rc,o.apl,o.normal_apl,o.query_rc,o.query_count,o.query_state,o.granted,o.called,o.process_name,o.cpp_abi,static_cast<unsigned long long>(before),static_cast<unsigned long long>(o.self),static_cast<unsigned long long>(after));
    if(before!=after||o.self!=before){std::puts("QUERY_CONTEXT_CHANGED_UNEXPECTED no_restore_called=1");return 82;}
    if(rc==NS_OWNER&&!o.lookup){std::puts("FIXED_NAME_NOT_RESOLVED_OR_LOOKUP_UNAVAILABLE absence_NOT_PROVED=1");return 79;}
    const bool info_ok=o.lookup&&o.info_rc==0&&o.type==o.native_type&&o.apl==o.normal_apl&&!std::strcmp(o.process_name,NS_ACTIONBAG);
    const bool query_ok=(o.called&31u)==31u&&o.query_rc==0&&o.query_count==1;
    if(info_ok&&query_ok&&o.granted&&rc==NS_OK){std::puts("OWN_PROFILE_MEASURED_GRANTED caller_permission_NOT_ESTABLISHED=1 query_NOT_total_inventory=1");return 0;}
    if(info_ok&&query_ok&&!o.granted&&o.query_state==-1&&rc==NS_QUERY)std::puts("OWN_PROFILE_MEASURED_NOT_GRANTED caller_permission_NOT_ESTABLISHED=1");
    else std::puts("QUERY_UNAVAILABLE_OR_PROFILE_MISMATCH permission_DENIED_NOT_INFERRED=1");
    return 80;
}
/* This is future sensitive code, NEVER executed by this increment's tests.
 * Operator attestation is not a grant or proof of user consent. No arbitrary
 * identity name, creation/update/reload/delete or SDK traffic is supported. */
static int approved_window(const char* target_text,const char* bridge_path){
    if(!stage1_entry::execution_enabled()){std::puts("SENSITIVE_EXECUTION_COMPILE_LOCKED no_selector_or_restore=1");return 78;}
    const char* admitted=std::getenv("FASTDDS_DSOFTBUS_IDENTITY_ADMITTED");const char* name=std::getenv("FASTDDS_DSOFTBUS_NATIVE_IDENTITY");
    if(!admitted||std::strcmp(admitted,"1")||!name||std::strcmp(name,NS_ACTIONBAG)||!target_text||!target_text[0]||!bridge_path||bridge_path[0]!='/')return 78;
    const char* base=std::strrchr(bridge_path,'/');if(!base||std::strcmp(base+1,"libstage1_actionbag_identity.so"))return 78;
    for(const char* p=target_text;*p;++p)if(*p<'0'||*p>'9')return 78;
    char* end=nullptr;errno=0;auto target=std::strtoull(target_text,&end,10);if(errno||!end||*end||!target||target>UINT32_MAX||!single_main())return 78;
    const uint64_t entry_pid=getpid(),entry_tid=tid();
    const char* scalar="/system/lib64/platformsdk/libtokensetproc_shared.z.so";
    void* token=dlopen(scalar,RTLD_NOW|RTLD_LOCAL);if(!token)return 81;
    using Get=uint64_t(*)();using Set=int32_t(*)(uint64_t);
    auto get=function<Get>(token,"GetSelfTokenID",scalar);auto set=function<Set>(token,"SetSelfTokenID",scalar);
    if(!get||!set||!single_main())return 82; /* Handles intentionally retained until process exit. */
    uint64_t original=get();if(!original)return 82;bool attempted=true,verified=false;int select_rc=set(target);uint64_t selected=get();
    if(select_rc==0&&selected==target){
        void* bridge=dlopen(bridge_path,RTLD_NOW|RTLD_LOCAL);
        if(bridge){
            auto api_get=function<NS_GetIdentityApi>(bridge,"ns_identity_get_api",bridge_path);NS_IdentityApi api{};
            if(api_get&&api_get(NS_VERSION,sizeof(api),&api)==NS_OK&&ns_header(&api.header,sizeof(api))==NS_OK&&api.query&&function_owner(api.query,bridge_path)){
                NS_Observation o{};o.header={NS_VERSION,sizeof(o),NS_SCOPE,0};verified=api.query(static_cast<uint32_t>(target),&o)==NS_OK&&o.lookup==target&&o.self==selected&&o.granted;
            }
        }
    }
    int restore_rc=-1;bool restored=false;
    if(attempted&&entry_pid==static_cast<uint64_t>(getpid())&&entry_tid==tid()){restore_rc=set(original);restored=restore_rc==0&&get()==original;}
    std::printf("OWN_WINDOW operator_assertion_only=1 select_rc=%d verified=%d restored_entry=%d restore_rc=%d bridge_retained_until_exit=1 worker_parent_restoration=UNKNOWN traffic=NOT_ENTERED\n",select_rc,int(verified),int(restored),restore_rc);
    return verified&&restored?0:88;
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const int control_mode=stage1_control::frontend_control(argc,argv);if(control_mode!=-1)return control_mode;
    observe_tasks("main_entry");observe_symbols("entry_global",RTLD_DEFAULT);
    if(argc==2&&!std::strcmp(argv[1],"--admission-selftest"))return stage1_entry::admission_selftest()?0:105;
    if(argc==4&&!std::strcmp(argv[1],"--admission-check"))return admission_check(argv[2],argv[3],false);
    if(argc==4&&!std::strcmp(argv[1],"--admitted-run"))return admission_check(argv[2],argv[3],true);
    if(argc==2&&!std::strcmp(argv[1],"--selftest"))return selftest();
    if(argc==3&&!std::strcmp(argv[1],"--loader-selftest"))return loader_test(argv[2]);
    if(argc==3&&!std::strcmp(argv[1],"--query-own"))return query_own(argv[2]);
    if(argc==4&&!std::strcmp(argv[1],"--approved-owned-window"))return approved_window(argv[2],argv[3]);
    std::printf("AUTHORIZATION_REQUIRED no_identity_api=1 no_optional_dlopen=1 no_SDK_business=1 cpp=%s\n",cpp_abi);return 78;
}
