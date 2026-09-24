#include "PerformanceObservation.hpp"
// Reuse the unchanged real transport contract and real Socket callback signatures.
// No duplicate model of transport behaviour and no extra native generation fields.
#define main ExistingTransportContractMain
#include "transport_contract_tests.cpp"
#undef main
#include <limits>
int main(int argc,char** argv) {
    try {
        const std::string mode=argc==2?argv[1]:"default";
        Require(CurrentPerformanceObservation()==nullptr,"default observation opt-out");
        if(mode=="default") { ParserContracts(); TransportContracts(); return 0; }
        Require(EnablePerformanceObservation(),"first pre-runtime opt-in");
        Require(!EnablePerformanceObservation(),"no observer rebind/reset");
        auto* observer=CurrentPerformanceObservation();Require(observer!=nullptr,"installed observation");
        if(mode=="trace"||mode=="measure") {
            observer->SetQuiet(mode=="measure");
            ParserContracts();TransportContracts();
            const auto s=observer->Snapshot();
            Require(s.spans>0&&s.ingress>0&&s.application_rx>0&&s.application_tx>0,"all four real output points observed");
            Require(s.send_calls==1&&s.send_returns==1&&s.send_attempt_bytes==8&&s.send_success_bytes==8,"actual SDK bytes not application bytes");
            Require(s.owned_copy_messages>=3&&s.owned_copy_bytes>=9,"owned-copy byte scope");
            Require((s.suppressed>0)==(mode=="measure"),"mode only controls routine observation");
            Require(!s.counter_overflow,"bounded normal counters");return 0;
        }
        if(mode=="counter") {
            observer->SetQuiet(true);
            observer->Span("SendBytes_enter",1,1,22,0,true,5,0);
            observer->Span("SendBytes_return",1,1,22,0,true,5,0);
            observer->Span("SendBytes_enter",2,1,22,0,true,3,0);
            observer->Span("SendBytes_return",2,1,22,-7,true,3,0);
            observer->Span("SDK_return",3,1,22,22,true,0,0);
            observer->Span("Shutdown_return",4,1,22,0,false,0,0);
            observer->Span("OnBytes_owned_copy",5,1,22,0,true,4,9);
            const auto s=observer->Snapshot();
            Require(s.send_calls==2&&s.send_returns==2&&s.send_attempt_bytes==8,"attempted SDK bytes");
            Require(s.send_success_bytes==5&&s.send_failed_bytes==3&&s.send_failures==1,"failed native return visible");
            Require(s.owned_copy_bytes==4&&s.nonzero_values==2,"Socket handle retained but not SendBytes error");
            PerformanceErrorRecord e;Require(observer->ErrorRecord(0,e)&&e.value==-7&&e.bytes==3,"bounded event evidence");
            Require(!observer->ErrorRecord(127,e),"unpublished slots unknown");return 0;
        }
        if(mode=="overflow") {
            for(unsigned i=0;i<129;++i)observer->Span("SendBytes_return",i,1,22,-1,true,1,0);
            auto s=observer->Snapshot();Require(s.send_failures==129&&s.error_details_stored==128&&s.error_details_overflow==1,"detail loss not zero/extra allocation");
            observer->Span("SendBytes_enter",1000,1,22,0,true,std::numeric_limits<std::size_t>::max(),0);
            observer->Span("SendBytes_enter",1001,1,22,0,true,1,0);
            s=observer->Snapshot();Require(s.counter_overflow&&s.send_attempt_bytes==std::numeric_limits<std::uint64_t>::max(),"byte counter cannot wrap silently");return 0;
        }
        throw std::runtime_error("unknown test mode");
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what());return 1; }
}
