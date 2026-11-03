#include "native_ingress.hpp"
#include <cstdio>
#include <stdexcept>
#include <functional>
#include <cstring>
using namespace stage1_control;
static void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static void reject(const std::function<void()>& f){bool refused=false;try{f();}catch(const std::exception&){refused=true;}check(refused,"invalid input accepted");}
static Deployment input(){Deployment d;d.fields={"S1HANDOFF1","NATIVE_PREPARE_ONLY","0123456789abcdef","10","/data/local/tmp/own/front",std::string(64,'a'),"/data/local/tmp/own/consumer.so",std::string(64,'b'),"stage1","151",NS_PACKAGE,"com.kaihong.mdds.own","1000","11","3e01ff55454d202020104033bf453b00","31","ownA","netA","1","initA","12","3e01ff55454d202020104433991c3b00","32","ownB","netB","2","initB"};return d;}
static void contracts(){
    auto d=input();const auto text=d.encode_native();auto copy=Deployment::parse_native(text);check(copy.fields==d.fields,"canonical native roundtrip");reject([&]{Deployment::parse(text);});
    auto end=Clock::now()+std::chrono::seconds(1);auto q=native_bound_request(d,0,11,10,std::string(64,'c'),std::string(64,'a'),end);
    check(q.local.pid==11&&q.peer.pid==12&&q.deployment.domain==151&&q.local.run==UINT64_C(0x0123456789abcdef)&&q.deployment.slot==1&&q.deployment.external_approval==0,"native complete binding without approval");
    reject([&]{native_bound_request(d,0,12,10,std::string(64,'c'),std::string(64,'a'),end);});reject([&]{native_bound_request(d,0,11,9,std::string(64,'c'),std::string(64,'a'),end);});reject([&]{native_bound_request(d,0,11,10,std::string(64,'c'),std::string(64,'d'),end);});
    reject([&]{native_bound_request(d,0,11,10,std::string(64,'c'),std::string(64,'a'),Clock::now());});
    for(const char* path:{"relative","/a/../b","/a/./b","/a//b","/a/..","/a/.","/a\\b"}){auto bad=d;bad.fields[DModule]=path;reject([&]{bad.encode_native();});}
    reject([&]{Deployment::parse_native(text+"extra=x\n");});reject([&]{Deployment::parse_native(text.substr(0,text.size()-1));});
    auto bad=d;bad.fields[DParent]="18446744073709551616";reject([&]{bad.encode_native();});bad=d;bad.fields[DBudget]="0";reject([&]{bad.encode_native();});
    check(native_fd_number("3")==3&&native_fd_number("2147483647")==2147483647,"FD bounds");
    for(const char* s:{"","0","2","03","-1","+3","3x","2147483648"," 3","3\n"})reject([&]{native_fd_number(s);});
    check(native_file_size(8192,8192,true)==8192&&native_file_size(16*1048576,16*1048576,true)==16*1048576,"exact allocation sample bounds");
    for(int64_t bytes:{INT64_C(-1),INT64_C(0),INT64_C(8193),INT64_MAX})reject([&]{native_file_size(bytes,8192,true);});reject([&]{native_file_size(1,8192,false);});
    auto host=d;host.fields[DPurpose]="HOST_NONBUSINESS_FIXTURE";host.fields[DImage]="E:/own/front.exe";host.fields[DModule]="E:/own/fixture.dll";check(Deployment::parse(host.encode()).fields==host.fields,"unchanged Host purpose");reject([&]{host.encode_native();});
    std::puts("PASS NATIVE_CANONICAL_BINDING_AND_FD_ARGUMENTS Hostlogic_only no_POSIX_syscalls=1");
}
static void journal(){
    using J=RestorationJournal;using E=J::Event;using S=J::State;
    {J j(11,11,J::Evidence::HostFake);for(auto invalid:{-1,999})check(j.record(static_cast<E>(invalid),11,11)==NS_CONTEXT&&j.size()==0&&j.state()==S::Empty,"unknown event must not append or mutate");}
    auto capture=[](J& j){check(j.record(E::Capture,11,11,100)==NS_OK,"capture");check(j.record(E::SelectAttempt,11,11,200)==NS_OK,"attempt");};
    for(auto evidence:{J::Evidence::HostFake,J::Evidence::NativeUnverified}){
        J j(11,11,evidence);capture(j);check(j.record(E::SelectReadback,11,11,200)==NS_OK,"selected");check(j.record(E::Construct,11,11)==NS_OK,"construct after selection");check(j.record(E::Cancel,11,11)==NS_OK,"cancel");check(j.record(E::LastUse,11,11,0,0,true)==NS_OK,"fixture last use");check(j.record(E::RestoreAttempt,11,11,100)==NS_OK,"restore observation");check(j.record(E::RestoreReadback,11,11,100)==NS_OK,"entry readback");check(j.state()==(evidence==J::Evidence::HostFake?S::FakeComplete:S::EntryReadbackOnly),"scope distinction");check(!j.native_restoration_verified(),"Native effect must remain unknown");check(j.record(E::Capture,11,11,100)==NS_CONTEXT,"one run terminal");
    }
    {J j(11,11,J::Evidence::HostFake);check(j.record(E::Construct,11,11)==NS_STATE,"no early constructors");capture(j);check(j.record(E::SelectReadback,11,12,200)==NS_CONTEXT,"same entry TID");check(j.record(E::SelectReadback,12,11,200)==NS_CONTEXT,"same entry PID");check(j.record(E::SelectReadback,11,11,100,-1)==NS_OK,"failed selection record");check(j.state()==S::SelectFailed,"failed selection");check(j.record(E::RestoreAttempt,11,11,100)==NS_OK,"failed attempt restoration remains explicit");check(j.record(E::RestoreReadback,11,11,200,-1)==NS_NOT_READY&&j.state()==S::Uncertain,"restore failure retained");}
    {J j(11,11,J::Evidence::NativeUnverified);capture(j);check(j.record(E::SelectReadback,11,11,200)==NS_OK,"selected2");check(j.record(E::Construct,11,11)==NS_OK,"construct2");check(j.record(E::RestoreAttempt,11,11,100)==NS_NOT_READY&&j.state()==S::Uncertain,"no restore while owners unresolved");}
    {J j(11,11,J::Evidence::HostFake);capture(j);check(j.record(E::SelectReadback,11,11,200)==NS_OK,"selected3");check(j.record(E::Cancel,11,11)==NS_OK,"cancel prebusiness");check(j.record(E::LastUse,11,11,0,0,false)==NS_NOT_READY,"unknown last use");}
    std::puts("PASS RESTORATION_JOURNAL_FAKE_EFFECTS_ONLY no_identity_APIs=1 no_RAII_or_SIGKILL_exec_guarantee=1");
}
int main(int argc,char** argv){if(argc>=2&&!std::strcmp(argv[1],"--native-fd-prepare"))return native_fd_prepare(argc,argv);try{contracts();journal();return 0;}catch(const std::exception& e){std::fprintf(stderr,"NATIVE_CONTRACT_FAIL %s\n",e.what());return 4;}}
