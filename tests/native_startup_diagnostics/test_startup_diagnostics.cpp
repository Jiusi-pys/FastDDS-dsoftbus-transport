#include "startup_diagnostics.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
using namespace stage1_startup;
static void need(bool value){if(!value)throw std::runtime_error("SYNTHETIC_DIAGNOSTIC_ASSERTION");}
int main(){
    unsigned tests=0;
    try {
        {Recorder r;const auto s=r.snapshot();need(s.available==1&&!s.first_reason&&!s.entered_stage);++tests;}
        {const auto s=snapshot_or_unknown(nullptr);need(!s.available&&!s.first_reason);++tests;}
        for(const auto& item:std::array<std::pair<Reason,Stage>,5>{{
          {Reason::GateRegistrationRejected,Stage::GateRegistration},{Reason::DescriptorNull,Stage::DescriptorCreation},
          {Reason::XmlLoadRejected,Stage::XmlLoad},{Reason::XmlModeMismatch,Stage::XmlMode},{Reason::ParticipantNull,Stage::ParticipantCreation}}}){
            Recorder r;r.enter(item.second);
            need(!check(r,false,item.first,item.second));const auto s=r.snapshot();
            need(s.first_reason==std::uint32_t(item.first)&&s.failure_stage==std::uint32_t(item.second));
            ++tests;
        }
        {Recorder r;need(check(r,true,Reason::DescriptorNull,Stage::DescriptorCreation));need(!r.snapshot().first_reason);++tests;}
        {Recorder r;r.enter(Stage::SessionConstruction);r.fail(Reason::UnexpectedException,r.entered());
          r.enter(Stage::ParticipantCreation);r.fail(Reason::ParticipantNull,Stage::ParticipantCreation);
          const auto s=r.snapshot();need(s.first_reason==9&&s.failure_stage==3&&s.entered_stage==12);++tests;}
        {Recorder a,b;a.fail(Reason::LockBusy,Stage::ExecuteGuard);need(!b.snapshot().first_reason);++tests;}
        {Recorder r;r.gate_enter(GateStage::WaitRelease);r.gate_fail(GateReason::WaitFailed,GateStage::WaitRelease);
          r.fail(Reason::ParticipantNull,Stage::ParticipantCreation);auto s=r.snapshot();
          need(s.first_reason==7&&s.gate_reason==3&&s.gate_failure_stage==4);++tests;}
        {Recorder r;std::vector<std::thread> workers;std::atomic<unsigned> bad{0};
          for(unsigned i=1;i<=8;++i)workers.emplace_back([&r,&bad,i]{for(unsigned j=0;j<10000;++j){
            const bool a=(i%2)!=0;r.gate_enter(a?GateStage::EmitEvent:GateStage::WaitRelease);
            r.gate_fail(a?GateReason::EmitFailed:GateReason::WaitFailed,a?GateStage::EmitEvent:GateStage::WaitRelease);
            r.fail(a?Reason::XmlLoadRejected:Reason::ParticipantNull,a?Stage::XmlLoad:Stage::ParticipantCreation);
            const auto s=r.snapshot();if(!((s.gate_reason==2&&s.gate_failure_stage==3)||(s.gate_reason==3&&s.gate_failure_stage==4)))++bad;
            if(!((s.first_reason==5&&s.failure_stage==9)||(s.first_reason==7&&s.failure_stage==12)))++bad;
          }});
          for(auto& w:workers)w.join();need(bad==0);++tests;
        }
        {Recorder r;r.enter(Stage::ParticipantCreation);r.fail(Reason::ParticipantNull,Stage::ParticipantCreation);
          r.gate_enter(GateStage::WaitRelease);r.gate_fail(GateReason::WaitFailed,GateStage::WaitRelease);
          const auto bytes=record(r.snapshot());need(bytes.size()<=512);
          need(bytes=="format=S1GOALCAUSE1\navailable=1\nentered_stage=12\nfirst_reason=7\nfailure_stage=12\ngate_entered_stage=4\ngate_reason=3\ngate_failure_stage=4\n");++tests;}
        {Recorder r;unsigned stored=0,fallback=0;publish(r.snapshot(),[&](const std::string&){++stored;},[&]{++fallback;});
          need(stored==1&&!fallback);++tests;}
        {Recorder r;unsigned fallback=0;
          publish(r.snapshot(),[](const std::string&){throw 1;},[&]{++fallback;throw 2;});
          need(fallback==1);++tests;}
        std::printf("HOST_STARTUP_DIAGNOSTICS_PASS tests=%u SDK_NOT_RUN=1\n",tests);return 0;
    } catch(...) {std::printf("HOST_STARTUP_DIAGNOSTICS_FAIL completed=%u\n",tests);return 1;}
}
