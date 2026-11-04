#include "owned_policy.h"
#include <cstdio>
#include <stdexcept>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
static void need(bool b,const char* text){if(!b)throw std::runtime_error(text);}
struct Script {int observe_rc=OW_OK,identity_rc=OW_OK,reap_rc=OW_OK,kill_rc=OW_OK;bool terminal=false,mismatch=false;int observes=0,identities=0,kills=0,reaps=0;uint32_t killed=0;};
static int observe(void* p,uint32_t pid,OW_Observation* o){auto& s=*static_cast<Script*>(p);++s.observes;*o={s.terminal?pid:0,s.terminal?1:0,0};return s.observe_rc;}
static int identity(void* p,uint32_t pid,OW_Identity* i){auto& s=*static_cast<Script*>(p);++s.identities;*i={pid,10,s.mismatch?99u:42u,1};return s.identity_rc;}
static int signal_one(void* p,uint32_t pid){auto& s=*static_cast<Script*>(p);++s.kills;s.killed=pid;return s.kill_rc;}
static int reap(void* p,uint32_t,int* status){auto& s=*static_cast<Script*>(p);++s.reaps;*status=0;return s.reap_rc;}
static const OW_Ops scripted={observe,identity,signal_one,reap};
static OW_Child child(){OW_Child c{};need(ow_begin(&c,11,10,42,0,100)==OW_OK,"positive creation");return c;}
static void scenarios(){
    {auto c=child();Script s;s.terminal=true;need(ow_step(&c,&scripted,&s,1,0)==OW_REAPED&&s.kills==0&&s.reaps==1,"normal root exit never signals");int n=s.observes;need(ow_step(&c,&scripted,&s,10000,1)==OW_REAPED&&s.observes==n&&s.kills==0,"late cleanup idempotent");}
    {auto c=child();Script s;need(ow_step(&c,&scripted,&s,100,0)==OW_SIGNALLED&&s.kills==1&&s.killed==11,"deadline exact positive PID once");s.terminal=true;need(ow_step(&c,&scripted,&s,101,0)==OW_REAPED&&s.kills==1,"known root reaped only");}
    {auto c=child();Script s;s.observe_rc=OW_INTR;need(ow_step(&c,&scripted,&s,100,0)==OW_ACTIVE&&s.kills==0&&s.identities==0,"EINTR grants no stale signal");s.observe_rc=OW_OK;need(ow_step(&c,&scripted,&s,101,0)==OW_SIGNALLED&&s.kills==1,"fresh evidence after EINTR");}
    for(int kind=0;kind<4;++kind){auto c=child();Script s;if(kind==0)s.observe_rc=OW_ERROR;if(kind==1)s.identity_rc=OW_ERROR;if(kind==2)s.mismatch=true;if(kind==3)s.kill_rc=OW_ERROR;need(ow_step(&c,&scripted,&s,100,0)==OW_UNKNOWN,"wait ECHILD/error/identity/PID reuse/signal error revoke");int n=s.observes,k=s.kills;s.observe_rc=OW_OK;s.mismatch=false;s.terminal=true;need(ow_step(&c,&scripted,&s,101,1)==OW_UNKNOWN&&s.observes==n&&s.kills==k&&s.reaps==0,"revoked late event performs no target operation");}
    {auto c=child();Script s;s.terminal=true;s.reap_rc=OW_ERROR;need(ow_step(&c,&scripted,&s,1,0)==OW_UNKNOWN&&s.kills==0,"reap error no fallback signal");}
    {auto c=child();Script s;s.terminal=true;s.reap_rc=OW_INTR;need(ow_step(&c,&scripted,&s,1,0)==OW_ACTIVE&&s.kills==0,"interrupted reap retains no stale terminal claim");s.reap_rc=OW_OK;need(ow_step(&c,&scripted,&s,2,0)==OW_REAPED&&s.kills==0,"fresh nonconsuming observation then reap");}
    {auto c=child();Script s;s.terminal=true;need(ow_step(&c,&scripted,&s,100,1)==OW_REAPED&&s.kills==0&&c.late_observation,"terminal beats timeout/cancel signal race");}
    {auto c=child();Script s;need(ow_step(&c,&scripted,&s,100,1)==OW_SIGNALLED,"cancel root");need(ow_step(&c,&scripted,&s,2100,1)==OW_UNKNOWN&&s.kills==1,"bounded unproven reap no repeat signal");}
    {auto c=child();Script s;need(ow_step(&c,&scripted,&s,10,1)==OW_SIGNALLED,"early cancel");s.observe_rc=OW_INTR;int n=s.observes;need(ow_step(&c,&scripted,&s,2010,0)==OW_UNKNOWN&&s.observes==n&&s.kills==1,"EINTR cannot extend cleanup");}
    {auto c=child();Script s;s.terminal=true;s.reap_rc=OW_INTR;need(ow_step(&c,&scripted,&s,1,0)==OW_ACTIVE,"terminal timestamp");s.reap_rc=OW_OK;need(ow_step(&c,&scripted,&s,150,0)==OW_REAPED&&!c.late_observation&&s.kills==0,"first terminal timely across reap interruption");}
    {auto c=child();Script s;s.terminal=true;s.reap_rc=OW_INTR;need(ow_step(&c,&scripted,&s,1,0)==OW_ACTIVE,"terminal cleanup start");int n=s.observes;need(ow_step(&c,&scripted,&s,2001,0)==OW_UNKNOWN&&s.observes==n,"terminal cleanup expires before observe");}
    {auto c=child();Script s;s.terminal=true;s.reap_rc=OW_INTR;need(ow_step(&c,&scripted,&s,1,0)==OW_ACTIVE,"terminal stable");s.terminal=false;need(ow_step(&c,&scripted,&s,2,0)==OW_UNKNOWN&&s.kills==0,"terminal cannot become running");}
    {OW_Child c{};Script s;need(ow_begin(&c,11,10,42,INT64_MAX-2100,100)==OW_OK,"near max admission");need(ow_step(&c,&scripted,&s,INT64_MAX-1,0)==OW_SIGNALLED&&c.cleanup_ms==INT64_MAX,"cleanup saturates without overflow");}
    {auto c=child();Script s;need(ow_step(&c,&scripted,&s,2,0)==OW_ACTIVE,"running");need(ow_step(&c,&scripted,&s,1,0)==OW_UNKNOWN&&s.kills==0,"clock rollback revokes");}
    OW_Child bad{};need(ow_begin(&bad,0,10,42,0,100)==OW_ERROR&&ow_begin(&bad,10,10,42,0,100)==OW_ERROR,"invalid PID creation refused");
    std::puts("PASS SCRIPTED_DIRECT_CHILD_POLICY normal_no_signal EINTR_fresh ECHILD_revoke identity_generation_loss late_idempotent root_only_descendants_UNKNOWN no_group_operation");
}
#ifdef _WIN32
struct Real {HANDLE process=nullptr;DWORD pid=0;uint64_t birth=0;int kills=0;};
static uint64_t born(HANDLE h){FILETIME c,e,k,u;if(!GetProcessTimes(h,&c,&e,&k,&u))return 0;return (uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
static int real_observe(void* p,uint32_t pid,OW_Observation* o){auto& s=*static_cast<Real*>(p);if(pid!=s.pid)return OW_ERROR;const auto w=WaitForSingleObject(s.process,0);if(w==WAIT_TIMEOUT){*o={0,0,0};return OW_OK;}DWORD code=0;if(w!=WAIT_OBJECT_0||!GetExitCodeProcess(s.process,&code))return OW_ERROR;*o={pid,1,static_cast<int>(code)};return OW_OK;}
static int real_identity(void* p,uint32_t pid,OW_Identity* i){auto& s=*static_cast<Real*>(p);*i={pid,GetCurrentProcessId(),born(s.process),pid==s.pid&&GetProcessId(s.process)==pid};return OW_OK;}
static int real_signal(void* p,uint32_t pid){auto& s=*static_cast<Real*>(p);if(pid!=s.pid||GetProcessId(s.process)!=pid||born(s.process)!=s.birth)return OW_ERROR;++s.kills;return TerminateProcess(s.process,201)?OW_OK:OW_ERROR;}
static int real_reap(void* p,uint32_t pid,int* status){OW_Observation o{};int r=real_observe(p,pid,&o);if(r)return r;if(!o.terminal)return OW_PENDING;*status=o.raw_status;return OW_OK;}
struct OwnedFixture {
    Real value;bool checked=false,attempted=false;
    ~OwnedFixture(){if(value.process&&!finish())std::fputs("OWN_FIXTURE_CLEANUP_UNPROVEN handle_retained_until_process_exit=1\n",stderr);}
    bool finish()noexcept{
        if(attempted)return checked;attempted=true;if(!value.process)return checked;if(!value.birth||GetProcessId(value.process)!=value.pid||born(value.process)!=value.birth)return false;
        const DWORD state=WaitForSingleObject(value.process,0);if(state==WAIT_TIMEOUT){if(!TerminateProcess(value.process,202)||WaitForSingleObject(value.process,1000)!=WAIT_OBJECT_0)return false;}else if(state!=WAIT_OBJECT_0)return false;
        DWORD code=0;if(!GetExitCodeProcess(value.process,&code)||code==STILL_ACTIVE)return false;if(!CloseHandle(value.process))return false;value.process=nullptr;checked=true;return true;
    }
};
static void owned_fixture(const char* image,bool timeout){
    char cmd[2048];const int len=std::snprintf(cmd,sizeof(cmd),"\"%s\" %s",image,timeout?"--finite-wait":"--exit-zero");need(len>0&&static_cast<size_t>(len)<sizeof(cmd),"fixture command");STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};need(CreateProcessA(image,cmd,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi),"own fixture CreateProcess");OwnedFixture owner;owner.value={pi.hProcess,pi.dwProcessId,born(pi.hProcess),0};need(CloseHandle(pi.hThread),"created thread handle close");Real& s=owner.value;need(s.birth&&GetProcessId(s.process)==s.pid,"created handle birth identity");OW_Child c{};const auto start=GetTickCount64();need(ow_begin(&c,s.pid,GetCurrentProcessId(),s.birth,static_cast<int64_t>(start),timeout?50:1500)==OW_OK,"fixture admission");const OW_Ops ops={real_observe,real_identity,real_signal,real_reap};
    while(c.phase!=OW_REAPED&&c.phase!=OW_UNKNOWN){(void)ow_step(&c,&ops,&s,static_cast<int64_t>(GetTickCount64()),0);if(GetTickCount64()-start>3500)break;Sleep(1);}
    const bool ok=c.phase==OW_REAPED&&c.status_valid&&c.raw_status==(timeout?201:0)&&s.kills==(timeout?1:0);const auto pid=s.pid;const int killed=s.kills;need(owner.finish(),"actual fixture cleanup must be terminal/checked");need(ok,"actual owned handle normal/timeout exact exit");std::printf("PASS REAL_WINDOWS_OWN_CHILD pid=%lu signals=%d raw_exit=%d reaped_root_only=1 Native_PID_semantics_UNPROVEN=1\n",static_cast<unsigned long>(pid),killed,c.raw_status);
}
#endif
int main(int argc,char** argv){try{scenarios();
#ifdef _WIN32
    need(argc==2,"owned fixture path");owned_fixture(argv[1],false);owned_fixture(argv[1],true);
#else
    (void)argc;(void)argv;
#endif
return 0;}catch(const std::exception& e){std::fprintf(stderr,"SUPERVISION_FAIL %s\n",e.what());return 4;}}
