#include "run_protocol.hpp"
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using stage1_run::Clock;using stage1_run::Window;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static NS_Envelope identity(unsigned side){
    NS_Envelope e{};e.run=UINT64_C(0x1122334455667788);e.self_token=100+side;e.side=side;e.domain=151;e.pid=200+side;
    const char* board=side?"3e01ff55454d202020104433991c3b00":"3e01ff55454d202020104033bf453b00";
    std::memcpy(e.board,board,std::strlen(board)+1);
    std::memset(e.executable_sha,'a'+static_cast<int>(side),64);std::memset(e.deployment_sha,'c',64);std::memcpy(e.name_space,"project.test",sizeof("project.test"));return e;
}
static NS_RunEvent event(unsigned side,unsigned kind,unsigned ordinal=0){
    NS_RunEvent e{};e.header={NS_RUN_VERSION,sizeof(e),NS_SCOPE,0};e.envelope=identity(side);e.kind=kind;e.ordinal=ordinal;
    e.socket=17+static_cast<int>(side);e.flags=NS_HOST_FIXTURE_LISTEN;e.writer_matches=e.reader_matches=1;
    const unsigned producer=kind==NS_TAKE?1-side:side;e.writer_guid[0]=static_cast<uint8_t>(producer+1);e.sequence=ordinal;
    const auto p=stage1_run::payload(e.envelope.run,producer,ordinal);std::memcpy(e.payload,p.data(),32);return e;
}
static void ready(Window& w){check(w.emit(event(0,NS_ROOT_LISTENER)),"rootA");check(!w.can_release(NS_CONNECT_RELEASE),"prematureCONNECT");check(w.emit(event(1,NS_ROOT_LISTENER)),"rootB");check(w.can_release(NS_CONNECT_RELEASE),"bothCONNECT");}
static void matched(Window& w){ready(w);check(w.emit(event(1,NS_MATCHED)),"matchB");check(!w.can_release(NS_SEND_RELEASE),"prematureSEND");check(w.emit(event(0,NS_MATCHED)),"matchA");check(w.can_release(NS_SEND_RELEASE),"bothSEND");}
static void samples(Window& w){matched(w);for(unsigned n=1;n<=3;++n){check(w.emit(event(1,NS_WRITE,n)),"writeB");check(w.emit(event(0,NS_TAKE,n)),"takeA");check(w.emit(event(0,NS_WRITE,n)),"writeA");check(w.emit(event(1,NS_TAKE,n)),"takeB");}check(!w.can_release(NS_DELETE_RELEASE),"prematureDELETE");}
static void positive(){
    Window w(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),true);samples(w);
    check(w.emit(event(0,NS_ACK))&&w.emit(event(1,NS_ACK)),"acks");check(w.can_release(NS_DATA_CHECK_RELEASE),"crosslogcheck");
    check(w.emit(event(1,NS_DATA_DONE)),"doneB");check(!w.can_release(NS_DELETE_RELEASE),"oneDONEcannotdelete");check(w.emit(event(0,NS_DATA_DONE)),"doneA");
    check(w.can_release(NS_DELETE_RELEASE),"bothDONEdelete");check(w.emit(event(1,NS_TEARDOWN))&&w.emit(event(0,NS_TEARDOWN)),"teardown");
    const auto p=stage1_run::payload(UINT64_C(0x1122334455667788),0,1);check(p[0]==0x89&&p[31]==0x0f,"knownpayload");
}
static void envelopes(){for(unsigned field=0;field<9;++field){Window w(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),true);auto e=event(0,NS_ROOT_LISTENER);
    switch(field){case 0:++e.envelope.run;break;case 1:++e.envelope.pid;break;case 2:++e.envelope.domain;break;case 3:e.envelope.executable_sha[0]='f';break;case 4:e.envelope.deployment_sha[0]='d';break;case 5:e.envelope.name_space[0]='X';break;case 6:++e.envelope.self_token;break;case 7:e.envelope.board[0]='0';break;default:e.envelope.side=3;}
    check(!w.emit(e)&&!w.can_release(NS_CONNECT_RELEASE),"envelope refuse");}}
static void orders(){for(unsigned kind:{NS_MATCHED,NS_WRITE,NS_TAKE,NS_ACK,NS_DATA_DONE,NS_TEARDOWN}){Window w(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),true);check(!w.emit(event(0,kind,1)),"out of order");}
    Window duplicate(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),true);check(duplicate.emit(event(0,NS_ROOT_LISTENER)),"firstroot");check(!duplicate.emit(event(0,NS_ROOT_LISTENER)),"duplicateroot");
    Window fake(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),false);check(!fake.emit(event(0,NS_ROOT_LISTENER)),"fixture not native");
    Window native(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),false);auto e=event(0,NS_ROOT_LISTENER);e.flags=NS_REAL_ROOT_LISTEN_PUBLISHED;e.listen_rc=-1;check(!native.emit(e),"listen error");
}
static void corrupt(){for(unsigned mode=0;mode<4;++mode){Window w(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),true);matched(w);auto e=event(0,NS_WRITE,1);
    if(mode==0)e.payload[3]^=1;if(mode==1)e.writer_guid[0]=9;if(mode==2)e.sequence=0;if(mode==3)e.ordinal=4;
    check(!w.emit(e),"corrupt sample");}
    Window w(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),true);matched(w);check(w.emit(event(0,NS_WRITE,1)),"firstwrite");check(!w.emit(event(0,NS_WRITE,1)),"duplicatewrite");
    Window seq(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),true);matched(seq);check(seq.emit(event(0,NS_WRITE,1)),"sn1");auto s=event(0,NS_WRITE,2);s.sequence=1;check(!seq.emit(s),"duplicateSN");
}
static void correlation(){Window w(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),true);matched(w);
    for(unsigned n=1;n<=3;++n){check(w.emit(event(0,NS_WRITE,n))&&w.emit(event(1,NS_WRITE,n)),"writes");auto bad=event(0,NS_TAKE,n);bad.sequence+=10;check(w.emit(bad)&&w.emit(event(1,NS_TAKE,n)),"takes");}
    check(w.emit(event(0,NS_ACK))&&w.emit(event(1,NS_ACK)),"ackbad");check(!w.can_release(NS_DATA_CHECK_RELEASE)&&!w.can_release(NS_DELETE_RELEASE),"wrongcrossSN");check(!w.emit(event(0,NS_DATA_DONE)),"donebad");
    Window missing(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),true);matched(missing);check(missing.emit(event(0,NS_WRITE,1)),"onewrite");check(!missing.emit(event(0,NS_ACK)),"missingdata");
}
static void hook_case(bool early_failure){
    Window w(identity(0),identity(1),Clock::now()+std::chrono::seconds(2),true);std::promise<void> entered;auto signal=entered.get_future();std::atomic<int> owners{1};
    auto hook=std::async(std::launch::async,[&]{
        try{auto e=event(0,NS_ROOT_LISTENER);if(early_failure)++e.envelope.run;check(w.emit(e),"hookroot");entered.set_value();}
        catch(...){entered.set_exception(std::current_exception());throw;}
        bool ok=w.wait(NS_CONNECT_RELEASE);check(owners==1,"ownersheldinhook");return ok;
    });
    bool observed_early_error=false;
    try{
        check(signal.wait_for(std::chrono::milliseconds(500))==std::future_status::ready,"boundedhookhandshake");
        signal.get();w.cancel();check(!hook.get(),"cancelwithoutClose");
    }catch(const std::exception& e){
        // Preserve the first failure; always release and consume the worker
        // before any referenced owner/control state leaves scope.
        w.cancel();if(hook.valid()){try{(void)hook.get();}catch(...){}}
        if(!early_failure)throw;
        check(std::string(e.what())=="hookroot"&&std::string(w.error())=="ENVELOPE","exactearlyfailure");observed_early_error=true;
    }catch(...){w.cancel();if(hook.valid()){try{(void)hook.get();}catch(...){}}throw;}
    check(observed_early_error==early_failure,"earlyfailureobserved");check(owners==1,"ownersretained");owners=0;
}
static void cancel_hook(){
    hook_case(false);hook_case(true);
    stage1_run::Progress p;const auto g=p.snapshot();p.notify();check(p.wait(g,Clock::now()+std::chrono::seconds(1)),"notifybeforewait");const auto g2=p.snapshot();p.cancel();check(!p.wait(g2,Clock::now()+std::chrono::seconds(1)),"independentcancel");
}
static void deadline(){Window expired(identity(0),identity(1),Clock::now()-std::chrono::seconds(1),true);check(!expired.emit(event(0,NS_ROOT_LISTENER)),"expired");
    Window missing(identity(0),identity(1),Clock::now()+std::chrono::milliseconds(20),true);check(missing.emit(event(0,NS_ROOT_LISTENER)),"one ready");check(!missing.wait(NS_CONNECT_RELEASE)&&std::string(missing.error())=="DEADLINE","missingpeer timeout");
}
int main(){try{positive();envelopes();orders();corrupt();correlation();cancel_hook();deadline();std::cout<<"PASS protocol stages/envelopes/transcripts/cancel/deadline HOST_FIXTURE_ONLY noDDS noSDK noIdentity\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
