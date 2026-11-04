#include "FakePublicSocketLink.hpp"
#include "../probes/PublicSocketRuntimeProbe.hpp"
#include "../probes/PublicSocketRuntimeTrace.hpp"
#include <iostream>
#include <thread>
#include <condition_variable>
#include <map>
#include <stdexcept>
using namespace fastdds_dsoftbus::dsoftbus::runtime_probe;
static void Check(bool x,const char* why){if(!x)throw std::runtime_error(why);}
int main(int argc,char**argv){
 try {
    const std::string mode=argc>1?argv[1]:"pair";
    if(mode=="trace-text"){
        TraceText("peer","x\nPROBE_STAGE forged");DumpTrace();
    } else if(mode=="token-outcome"){
        std::string first;ObserveTokenDiagnostic(first,"original_token",10,20,0);Check(first.empty(),"different original token not failure");
        ObserveTokenDiagnostic(first,"select_token",20,20,0);Check(first.empty(),"selection success");
        ObserveTokenDiagnostic(first,"restore_token",10,30,-1);ObserveTokenDiagnostic(first,"initialization_or_record_exception",0,0,-1);Check(first=="restore_token","first failure preserved");
        Check(std::string(RestorationLabel(0,false))=="NOT_NEEDED"&&std::string(RestorationLabel(1,false))=="FAIL"&&std::string(RestorationLabel(2,true))=="PASS","restoration facts distinguished");
    } else if(mode=="trace"){
        for(int i=0;i<1025;++i)TraceEvent("test",0,0,i,0,0);
        Check(TraceCount()==1024&&TraceOverflow()==1,"bounded trace overflow visible");
    } else if(mode=="records"){
        auto p=MakeRecord("0123456789abcdef",0,1);RecordLedger ledger("0123456789abcdef",1);
        Check(ledger.Accept(p.data(),p.size()),"valid opposite record");
        Check(!ledger.Accept(p.data(),p.size()),"duplicate rejected");
        p[80]^=1;Check(!ledger.Accept(p.data(),p.size()),"body rejected");
        RecordLedger wrong("1123456789abcdef",1);Check(!wrong.Accept(p.data(),p.size()),"wrong run rejected");
        Check(!wrong.Accept(nullptr,256)&&!wrong.Accept(p.data(),255),"invalid length/pointer rejected before access");
        Check(ledger.Count()==1&&ledger.Errors()==2,"unique ledger not SDK byte count");
    } else if(mode=="config") {
        ProbeConfig c;c.run="0123456789abcdef";c.side=0;c.peer_network="model.net.B";
        Check(ValidConfig(c),"valid fixed config");c.side=2;Check(!ValidConfig(c),"wrong side");
        c.side=0;c.run="../unsafe";Check(!ValidConfig(c),"run not path");
        c.run="0123456789abcdef";c.peer_network="";Check(!ValidConfig(c),"no guessed peer");
    } else if(mode=="load") {
        public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
        Check(ProbeLoadCheck()==0,"load enters and returns");
        auto x=public_socket_fake::Observe();Check(x.live==0&&x.sends[0]+x.sends[1]+x.sockets[0]+x.sockets[1]+x.listens[0]+x.listens[1]+x.binds[0]+x.binds[1]+x.shutdowns[0]+x.shutdowns[1]==0,"load no explicit public socket calls");
    } else if(mode=="pair"||mode=="late-record"||mode=="negotiation2"||mode=="delayed-bind") {
        auto trace=std::make_shared<host_dds::Trace>();public_socket_fake::Configure(trace,true);if(mode=="delayed-bind")public_socket_fake::DelayBindCallbacks();
        ProbeResult result[2];std::thread tasks[2];std::mutex gate;std::condition_variable changed;std::map<std::string,int> ready;
        bool injected=false;
        auto barrier=[&](const char* stage){std::unique_lock<std::mutex> lock(gate);++ready[stage];
            if(mode=="negotiation2"&&std::string(stage)=="LISTENER_READY"&&ready[stage]==2)Check(!public_socket_fake::UnsupportedNegotiation2(0),"unsupported callback remains false");
            if(mode=="late-record"&&std::string(stage)=="DELIVERED"&&ready[stage]==2&&!injected){injected=true;public_socket_fake::LateBytes(0);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!TraceHas("RECEIVER_INVALID")&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();Check(TraceHas("RECEIVER_INVALID"),"late invalid really entered original receiver");}
            changed.notify_all();return changed.wait_for(lock,std::chrono::seconds(10),[&]{return ready[stage]==2;});};
        for(int i=0;i<2;++i)tasks[i]=std::thread([&,i]{ProbeConfig c;c.side=i;c.run="0123456789abcdef";c.service_prefix="host.public";c.package="host.public.dds";c.peer_network=i?"model.net.A":"model.net.B";result[i]=RunProbe(c,barrier,[&]{if(mode=="delayed-bind"&&TraceHas("WAIT_ACCEPTED"))public_socket_fake::ReleaseBindCallbacks();public_socket_fake::PumpOne();});});
        for(auto& t:tasks)t.join();
        if(mode=="late-record"){Check(!result[0].success&&result[0].invalid==1&&!result[0].first_failure.empty(),"late invalid must revoke success and preserve final predicate");}
        else for(auto& r:result){Check(r.success,"real shim fake C bounded run");Check(r.kind_is_real,"never mark real Kind Mock");Check(r.default_rejected,"ordinary Build remains unavailable");Check(r.received==5&&r.sent==5&&r.invalid==0,"per direction ledger");Check(r.drained,"original ownership drains");}
        if(mode=="negotiation2")Check(TraceHas("OnNegotiate2_unsupported"),"actual unsupported callback observed");
        Check(public_socket_fake::Observe().live==0,"normal final shutdown");
        Check(TraceHas("OnNegotiate_result")&&TraceHas("OnBind")&&TraceHas("OnBytes")&&TraceHas("SendBytes_return"),"actual shim trace exercised");
        Check(TraceOverflow()==0,"complete bounded trace");
    } else throw std::runtime_error("case");
    std::cout<<"PASS runtime probe host "<<mode<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
