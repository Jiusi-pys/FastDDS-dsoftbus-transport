#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <process.h>
#include "contract.h"
typedef struct {uint32_t kind,phase;} Event;
static Event events[64];
static volatile LONG count,phase,overflow,controlled_workers;
static LONG printed;
static DWORD main_thread;
DD_EXPORT uint32_t DD_CALL dd_host_witness(uint32_t kind) {
    LONG n=InterlockedIncrement(&count)-1;
    uint32_t p=(uint32_t)InterlockedCompareExchange(&phase,0,0);
    if(n<0||n>=64){InterlockedExchange(&overflow,1);return p;}
    events[n].kind=kind;events[n].phase=p;
    return p; /* No allocation/IO/lock/API query in loader callback. */
}
static void flush_events(void) {
    LONG n=InterlockedCompareExchange(&count,0,0);
    if(n>64)n=64;
    while(printed<n){printf("EVENT kind=%u phase=%u\n",events[printed].kind,events[printed].phase);++printed;}
    fflush(stdout);
}
static unsigned os_threads(void) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    THREADENTRY32 entry;unsigned n=0;DWORD pid=GetCurrentProcessId();
    if(snapshot==INVALID_HANDLE_VALUE)return 0;
    memset(&entry,0,sizeof(entry));entry.dwSize=(DWORD)sizeof(entry);
    if(Thread32First(snapshot,&entry))do {if(entry.th32OwnerProcessID==pid)++n;}while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);return n;
}
static int entry_ok(void) {return GetCurrentThreadId()==main_thread&&InterlockedCompareExchange(&controlled_workers,0,0)==0;}
static int fixture_begin(int reject) {
    if(!entry_ok())return 0;
    InterlockedExchange(&phase,1);(void)dd_host_witness(DD_SELECTED);
    /* Explicit fixture values only; no OS token/provider can be selected. */
    if(reject)return 0;
    if(strcmp(DD_PACKAGE,"com.kaihong.mdds")||strcmp(DD_ACTIONBAG,"mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16"))return 0;
    InterlockedExchange(&phase,2);(void)dd_host_witness(DD_VERIFIED);return 1;
}
static void fixture_restore(void) {
    if(InterlockedCompareExchange(&phase,0,0)){
        InterlockedExchange(&phase,0);(void)dd_host_witness(DD_RESTORED);
        printf("fixture_restored=1 not_native_restore=1\n");
    }
}
typedef struct {HANDLE ready,stop;} Worker;
static DWORD WINAPI worker(void* arg) {
    Worker* w=(Worker*)arg;
    InterlockedIncrement(&controlled_workers);SetEvent(w->ready);
    (void)WaitForSingleObject(w->stop,INFINITE); /* Main always signals; outer child deadline bounds failures. */
    InterlockedDecrement(&controlled_workers);return 0;
}
static int worker_case(void) {
    Worker w;HANDLE thread;int rejected=0;
    w.ready=CreateEventA(NULL,TRUE,FALSE,NULL);w.stop=CreateEventA(NULL,TRUE,FALSE,NULL);
    if(!w.ready||!w.stop){if(w.ready)CloseHandle(w.ready);if(w.stop)CloseHandle(w.stop);return 99;}
    thread=CreateThread(NULL,0,worker,&w,0,NULL);
    if(!thread){CloseHandle(w.ready);CloseHandle(w.stop);return 99;}
    if(WaitForSingleObject(w.ready,1000)==WAIT_OBJECT_0) rejected=!entry_ok();
    SetEvent(w.stop);
    if(WaitForSingleObject(thread,1000)!=WAIT_OBJECT_0){printf("worker_cleanup_failed=1\n");fflush(stdout);_exit(91);}
    CloseHandle(thread);CloseHandle(w.ready);CloseHandle(w.stop);
    printf("workers_joined=1 controlled_worker_rejected=%d\n",rejected);return rejected?80:99;
}
typedef struct Context {int cancel,cancel_on_open,error_reentry;int32_t nested;DD_Api* api;DD_Request* request;DD_Host* host;} Context;
static int32_t DD_CALL cancelled(void* arg){return ((Context*)arg)->cancel;}
static void DD_CALL event(void* arg,uint32_t kind) {
    Context* c=(Context*)arg;(void)dd_host_witness(kind);
    if(kind==DD_OPEN&&c->cancel_on_open)c->cancel=1;
    if(kind==DD_OPEN&&c->error_reentry){DD_Result r={DD_VERSION,(uint32_t)sizeof(DD_Result),DD_SCOPE,0,DD_COOKIE,0,0,0};c->nested=c->api->run(c->request,c->host,&r);}
    flush_events();
}
static int function_owner(DD_Run run,HMODULE expected) {
    uintptr_t bits=0;HMODULE owner=NULL;
    _Static_assert(sizeof(bits)==sizeof(run),"Host function address representation");
    memcpy(&bits,&run,sizeof(bits));
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)bits,&owner)&&owner==expected;
}
static int checked_reject(DD_Api* api,DD_Request* q,DD_Host* host,DD_Result* r,int32_t expected) {
    DD_Result before=*r;unsigned char output[8],input[6];int32_t rc;
    memcpy(output,q->output,sizeof(output));memcpy(input,q->input,sizeof(input));
    rc=api->run(q,host,r);printf("module_rejection_rc=%d expected=%d\n",rc,expected);
    return rc==expected&&!memcmp(&before,r,sizeof(before))&&!memcmp(output,q->output,sizeof(output))&&!memcmp(input,q->input,sizeof(input));
}
int main(int argc,char** argv) {
    const char* mode;const char* path;HMODULE module=NULL;DD_GetApi get=NULL;DD_Api api;FARPROC symbol;
    int code=99,early;int32_t rc;unsigned char input[8]={'p','r','o','j',0,255,0,0},output[8];
    DD_Request q={DD_VERSION,(uint32_t)sizeof(DD_Request),DD_SCOPE,DD_BORROWED,DD_COOKIE,input,6,output,8,DD_NORMAL,0};
    DD_Result result={DD_VERSION,(uint32_t)sizeof(DD_Result),DD_SCOPE,0,DD_COOKIE,0,0,0};
    DD_Host host={DD_VERSION,(uint32_t)sizeof(DD_Host),DD_SCOPE,0,DD_COOKIE,NULL,cancelled,event};Context ctx={0,0,0,0,&api,&q,&host};
    setvbuf(stdout,NULL,_IONBF,0);main_thread=GetCurrentThreadId();
    printf("scope=HOST_FIXTURE_ONLY real_identity_calls=0 real_provider=NOT_IMPLEMENTED\n");
    printf("entry_scope=HOST_CONTROLLED_WORKERS os_thread_count=%u not_OH_single_task=1\n",os_threads());
    if(argc>=2&&!strcmp(argv[1],"--real")){printf("REAL_IDENTITY_UNAVAILABLE workers_joined=1\n");return 78;}
    if(argc!=4||strcmp(argv[1],"--fixture")){printf("DEFAULT_DENIED workers_joined=1\n");return 77;}
    mode=argv[2];path=argv[3];host.context=&ctx;memset(output,0xcc,sizeof(output));
    {
        const char* allowed[]={"worker_reject","identity_error","normal","early_load","missing_dll","missing_export","api_rejections","foreign_api","request_size","request_rejections","result_size","short_output","overlap","cancel","module_error","hard_exit"};
        size_t i;int known=0;
        for(i=0;i<sizeof(allowed)/sizeof(allowed[0]);++i)if(!strcmp(mode,allowed[i]))known=1;
        if(!known){printf("UNKNOWN_FIXTURE_DENIED workers_joined=1\n");return 77;}
    }
    if(!strcmp(mode,"worker_reject"))return worker_case();
    if(!strcmp(mode,"identity_error")){(void)fixture_begin(1);code=79;goto cleanup;}
    early=!strcmp(mode,"early_load");
    if(!early&&!fixture_begin(0)){code=79;goto cleanup;}
    if(strlen(path)<3||path[1]!=':'||(path[2]!='/'&&path[2]!='\\')){code=51;goto cleanup;}
    module=LoadLibraryExA(path,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module){printf("load_error=%lu\n",GetLastError());code=51;goto cleanup;}
    (void)dd_host_witness(DD_LOADED);
    if(early&&!fixture_begin(0)){code=79;goto cleanup;}
    symbol=GetProcAddress(module,!strcmp(mode,"foreign_api")?"dd_get_api_foreign":"dd_get_api");
    if(!symbol){code=52;goto cleanup;}
    memcpy(&get,&symbol,sizeof(get));
    if(!strcmp(mode,"api_rejections")){
        DD_Api before;memset(&api,0xa5,sizeof(api));before=api;
        rc=get(2,(uint32_t)sizeof(api),&api);
        if(rc!=DD_ABI||memcmp(&api,&before,sizeof(api))){code=99;goto cleanup;}
        rc=get(DD_VERSION,(uint32_t)sizeof(api)-1,&api);
        code=rc==DD_SIZE&&!memcmp(&api,&before,sizeof(api))?53:99;goto cleanup;
    }
    rc=get(DD_VERSION,(uint32_t)sizeof(api),&api);printf("get_api_rc=%d\n",rc);
    if(rc!=DD_OK){code=53;goto cleanup;}
    if(api.version!=DD_VERSION||api.size!=sizeof(api)||api.scope!=DD_SCOPE||api.reserved||!api.run||!function_owner(api.run,module)){code=55;goto cleanup;}
    (void)dd_host_witness(DD_API);
    if(!strcmp(mode,"request_rejections")){
        DD_Request bad=q;int valid;
        bad.ownership=0;valid=checked_reject(&api,&bad,&host,&result,DD_OWNER);
        bad=q;bad.cookie++;valid=valid&&checked_reject(&api,&bad,&host,&result,DD_OWNER);
        bad=q;bad.scope=0;valid=valid&&checked_reject(&api,&bad,&host,&result,DD_BAD_SCOPE);
        code=valid?54:99;(void)dd_host_witness(DD_RETURNED);goto cleanup;
    }
    if(!strcmp(mode,"request_size")){q.size--;code=checked_reject(&api,&q,&host,&result,DD_SIZE)?54:99;}
    else if(!strcmp(mode,"result_size")){result.size--;code=checked_reject(&api,&q,&host,&result,DD_SIZE)?54:99;}
    else if(!strcmp(mode,"short_output")){q.output_capacity=5;code=checked_reject(&api,&q,&host,&result,DD_CAPACITY)?54:99;}
    else if(!strcmp(mode,"overlap")){q.output=input;code=checked_reject(&api,&q,&host,&result,DD_ALIAS)?54:99;}
    else {
        if(!strcmp(mode,"cancel"))ctx.cancel_on_open=1;
        if(!strcmp(mode,"module_error")){q.scenario=DD_FAIL_STEP;ctx.error_reentry=1;}
        if(!strcmp(mode,"hard_exit"))q.scenario=DD_HARD_EXIT;
        rc=api.run(&q,&host,&result);printf("module_rc=%d closed=%u bytes=%llu nested_rc=%d\n",rc,result.closed,(unsigned long long)result.output_bytes,ctx.nested);
        if(!result.closed||result.status!=rc){printf("lifecycle_unproven_no_restore=1\n");flush_events();_exit(93);}
        else if(!strcmp(mode,"cancel"))code=rc==DD_CANCEL&&result.output_bytes==0?43:99;
        else if(!strcmp(mode,"module_error"))code=rc==DD_APP_ERROR&&ctx.nested==DD_BUSY&&result.output_bytes==0?42:99;
        else code=rc==DD_OK&&result.output_bytes==6&&!memcmp(input,output,6)?0:99;
    }
    (void)dd_host_witness(DD_RETURNED);
cleanup:
    if(module){if(!FreeLibrary(module)){printf("unload_failed_no_restore=1\n");flush_events();_exit(92);}else (void)dd_host_witness(DD_UNLOADED);}
    fixture_restore();flush_events();if(overflow)code=99;
    printf("workers_joined=1 exit=%d scope=HOST_FIXTURE_ONLY\n",code);return code;
}
