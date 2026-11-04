#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "native_files.hpp"
#include "goal_bound.h"
#include "goal_diagnostics.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <cerrno>

using namespace stage1_goal;
static void need(bool v,const char* c){if(!v)throw std::runtime_error(c);}
static uint64_t number(const char* p){need(p&&*p,"ARG_NUMBER");uint64_t n=0;for(;*p;++p){need(*p>='0'&&*p<='9'&&n<=(UINT64_MAX-unsigned(*p-'0'))/10,"ARG_NUMBER");n=n*10+unsigned(*p-'0');}return n;}
int main(int argc,char** argv){
    // A new prepared entry, not an unlock or mutation of the old frontend.
    // No API is called on the default/invalid path. A data record is NOT consent;
    // only the approved task operator may start this exact bound artifact.
    if(argc!=5||std::strcmp(argv[1],"--wait-owned")||(std::strcmp(argv[2],"A")&&std::strcmp(argv[2],"B"))){std::puts("GOAL_AUTHORIZATION_REQUIRED setters=0 business=0");return 78;}
    std::unique_ptr<Scalar> scalar;std::unique_ptr<Files> files;std::unique_ptr<OwnedExecFlow<Scalar>> flow;int state_fd=-1,image_fd=-1,exec_errno=0;bool exec_errno_valid=false;GoalDiagnostic diagnostic;
    try{
        need(!std::getenv("LD_PRELOAD")||!*std::getenv("LD_PRELOAD"),"PRELOAD_FORBIDDEN");need(single_main(),"ENTRY_TASKS");
        const auto budget=number(argv[4]);need(budget>0&&budget<=120000,"BOOT_BUDGET");files=std::make_unique<Files>(argv[3]);const std::string side=argv[2];
        diagnostic.at(GoalPhase::Scalar);scalar=std::make_unique<Scalar>(GOAL_SCALAR_SHA);const auto point=native_point();const auto before=scalar->get(),again=scalar->get();need(before&&before==again&&point.tasks==1,"BOOT_SELF");
        diagnostic.at(GoalPhase::Boot);
        const auto exe=image_path();const auto exe_sha=self_image_hash();const auto wall=std::time(nullptr);need(wall>0,"WALL_CLOCK");
        const auto boot="format=S1GOALBOOT1\nside="+side+"\npid="+std::to_string(point.pid)+"\nparent="+std::to_string(point.parent)+"\nbirth="+std::to_string(point.birth)+"\ncaller="+std::to_string(before)+"\nuid="+std::to_string(point.uid)+"\neuid="+std::to_string(point.euid)+"\ngid="+std::to_string(point.gid)+"\negid="+std::to_string(point.egid)+"\nwall="+std::to_string(wall)+"\nmono="+std::to_string(point.now)+"\nimage="+exe+"\nimage_sha="+exe_sha+"\n";
        files->publish("boot."+side,boot);const auto boot_sha=pure_hash::digest(reinterpret_cast<const uint8_t*>(boot.data()),boot.size());
        diagnostic.at(GoalPhase::Admission);const auto boot_end=Clock::now()+std::chrono::milliseconds(budget);need(files->wait("admission."+side,boot_end),"ADMISSION_DEADLINE");
        stage1_entry::Record record{};char admission_sha[65]{};const char* why=nullptr;
        const auto admission_path=files->root()+"/admission."+side;
        need(stage1_entry::verify_admission(admission_path.c_str(),side.c_str(),exe.c_str(),record,admission_sha,why),"ADMISSION_INPUTS");
        need(!std::strcmp(record.value[stage1_entry::FrontendSha],exe_sha.c_str())&&record.caller==before,"BOOT_ADMISSION_BINDING");
        need(!std::strcmp(record.value[stage1_entry::ConsumerSha],GOAL_APP_SHA)&&!std::strcmp(record.value[stage1_entry::BridgeSha],GOAL_BRIDGE_SHA),"BOUND_COMPONENT_BYTES");
        need(!std::strcmp(record.value[stage1_entry::Intent],"own")||!std::strcmp(record.value[stage1_entry::Intent],"traffic"),"GOAL_INTENT");
        // This process stays sole-main; platform query workers live only in the
        // created/reaped readonly child. No persistent identity is ever changed.
        diagnostic.at(GoalPhase::Query);std::string lookup;const bool lookup_ok=query_owned_child(record,before,std::min<uint64_t>(point.now+budget*1000000,now_ns()+UINT64_C(4000000000)),lookup);
        if(!lookup.empty())files->publish("lookup."+side,lookup);
        need(lookup_ok&&scalar->get()==before&&single_main(),"OWN_LOOKUP_OR_PARENT_CONTEXT");
        const auto lookup_sha=pure_hash::digest(reinterpret_cast<const uint8_t*>(lookup.data()),lookup.size());
        auto admission=bound_admission(record,point,admission_sha,lookup_ok);admission.until=now_ns()+UINT64_C(90000000000);
        diagnostic.at(GoalPhase::Image);image_fd=sealed_image(record.value[stage1_entry::Consumer],GOAL_APP_SHA);
        diagnostic.at(GoalPhase::Admission);need(stage1_entry::admission_current(record,why),"EXPIRED_BEFORE_SELECT");
        diagnostic.at(GoalPhase::Select);flow=std::make_unique<OwnedExecFlow<Scalar>>(*scalar);const bool selected=flow->select(admission);if(flow->result().select_attempted)diagnostic.returned(flow->result().select_rc);need(selected,"OWN_SELECT_FAILED");
        ResumeState state{};state.run=admission.run;state.pid=admission.pid;state.parent=admission.parent;state.birth=admission.birth;state.original=flow->result().original;state.target=admission.target;state.until=admission.until;state.side=admission.side;state.attempted=flow->result().select_attempted?1:0;
        diagnostic.at(GoalPhase::State);std::memcpy(state.image_sha,admission.image_sha,65);std::memcpy(state.admission_sha,admission_sha,65);std::memcpy(state.boot_sha,boot_sha.c_str(),65);std::memcpy(state.lookup_sha,lookup_sha.c_str(),65);state_fd=sealed_state(state);
        const auto fd=std::to_string(state_fd);std::string child=record.value[stage1_entry::Consumer];std::string mode=!std::strcmp(record.value[stage1_entry::Intent],"own")?"--probe":"--data";
        char* args[]={child.data(),const_cast<char*>("--resume-owned"),const_cast<char*>(side.c_str()),argv[3],const_cast<char*>(fd.c_str()),mode.data(),nullptr};
        const auto slash=child.rfind('/');need(slash!=std::string::npos,"CHILD_DIRECTORY");const auto lib="LD_LIBRARY_PATH="+child.substr(0,slash)+"/lib";const auto tmp="TMPDIR="+files->root()+"/tmp";
        char* env[]={const_cast<char*>("PATH=/system/bin:/system/xbin"),const_cast<char*>("LANG=C"),const_cast<char*>(lib.c_str()),const_cast<char*>(tmp.c_str()),nullptr};
        // fexec is the same PID, before any business dependency constructor.
        // Successful propagation is NOT assumed: same data ELF probe is required.
        diagnostic.at(GoalPhase::Exec);const auto exec_rc=fexecve(image_fd,args,env);exec_errno=errno;exec_errno_valid=exec_rc<0;diagnostic.returned(exec_rc);
        flow->exec_returned();std::printf("GOAL_EXEC_RETURNED restored_entry=%d restore_rc=%d SDK_quiet=UNKNOWN\n",int(flow->result().state==FlowState::Restored),flow->result().restore_rc);
        files->publish("launch-failure."+side,"format=S1LAUNCHFAIL1\n"+diagnostic.fields()+"api_errno_valid="+std::to_string(exec_errno_valid)+"\napi_errno="+std::to_string(exec_errno)+"\nrestore_attempted="+std::to_string(flow->result().restore_attempted)+"\nrestore_rc="+std::to_string(flow->result().restore_rc)+"\n");
        close(state_fd);close(image_fd);return 89;
    }catch(const std::exception&){
        bool restored=false;if(flow){try{restored=flow->restore();}catch(...){}}
        if(state_fd>=0)close(state_fd);if(image_fd>=0)close(image_fd);
        if(files){try{files->publish(std::string("launch-failure.")+argv[2],"format=S1LAUNCHFAIL1\n"+diagnostic.fields()+"api_errno_valid="+std::to_string(exec_errno_valid)+"\napi_errno="+std::to_string(exec_errno)+"\nselect_attempted="+std::to_string(flow&&flow->result().select_attempted)+"\nrestore_attempted="+std::to_string(flow&&flow->result().restore_attempted)+"\nrestore_rc="+std::to_string(flow?flow->result().restore_rc:0)+"\nrestored_entry="+std::to_string(restored)+"\n");}catch(...){}}
        std::printf("GOAL_LAUNCH_REFUSED phase=%u native_rc_valid=%d native_rc=%d select_attempted=%d restored_entry=%d no_business_success=1\n",static_cast<unsigned>(diagnostic.phase),int(diagnostic.rc_valid),diagnostic.native_rc,int(flow&&flow->result().select_attempted),int(restored));return 88;
    }
}
