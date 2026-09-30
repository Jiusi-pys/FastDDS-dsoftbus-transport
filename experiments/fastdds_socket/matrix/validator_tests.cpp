#include "PerReaderValidator.hpp"
#include <cstdio>
#include <string>
#include <condition_variable>
#include <chrono>
#include <thread>
using namespace fastdds_dsoftbus::experiment::matrix;
int main(){
    unsigned failures=0,checks=0;
    const auto check=[&](bool ok,const char* name){++checks;std::printf("test=%s result=%s\n",name,ok?"PASS":"FAIL");if(!ok)++failures;};
    const std::string r1(32,'1'),r2(32,'2'),w1(32,'a'),w2(32,'b');
    const ExpectedSamples one{{w1,{1,2}}};
    PerReaderValidator a(r1,one),b(r2,one);
    check(a.Observe(r1,w1,0,1,1,true)&&a.Observe(r1,w1,0,2,2,true)&&a.Complete(one),"positive_complete");
    check(!b.Complete(one),"R1_complete_R2_missing");
    PerReaderValidator c(r1,one),d(r2,one);c.Observe(r1,w1,0,1,1,true);d.Observe(r2,w1,0,2,2,true);
    check(!c.Complete(one)&&!d.Complete(one),"aggregate_union_does_not_complete_individual");
    const ExpectedSamples two{{w1,{1}},{w2,{1}}};PerReaderValidator e(r1,two);
    check(e.Observe(r1,w1,0,1,1,true)&&!e.Complete(two)&&e.Observe(r1,w2,0,1,1,true)&&e.Complete(two),"writers_same_ordinal_distinct");
    PerReaderValidator f(r1,one);check(!f.Observe(r1,w2,0,1,1,true)&&f.Counters().wrong_writer==1,"wrong_writer");
    PerReaderValidator g(r1,one);check(!g.Observe(r2,w1,0,1,1,true)&&g.Counters().wrong_reader==1,"wrong_reader");
    PerReaderValidator h(r1,one);h.Observe(r1,w1,0,1,1,true);check(!h.Observe(r1,w1,0,1,1,true)&&h.Counters().duplicate==1,"duplicate_sequence");
    PerReaderValidator i(r1,one);i.Observe(r1,w1,0,1,1,true);check(!i.Observe(r1,w1,0,2,1,true)&&i.Counters().duplicate==1,"resend_same_ordinal_new_sequence");
    PerReaderValidator j(r1,one);check(!j.Observe(r1,w1,0,1,1,false)&&j.Counters().payload_mismatch==1,"payload_mismatch");
    PerReaderValidator k(r1,one);check(!k.Observe(r1,w1,0,0,1,true)&&k.Counters().invalid_sample==1,"invalid_sequence");
    PerReaderValidator l(r1,one);check(!l.Observe(r1,w1,0,1,3,true)&&l.Counters().unexpected_ordinal==1,"unexpected_ordinal");
    PerReaderValidator m(r1,one);m.Freeze();check(!m.Observe(r1,w1,0,1,1,true)&&m.Counters().after_delete==1,"callback_after_delete");
    PerReaderValidator n(r1,one);check(!n.Observe(r1,w1,0,1,1,true,false)&&n.Counters().invalid_sample==1,"invalid_data");
    PerReaderValidator o(r1,one);o.Observe(r2,w1,0,1,1,true);const auto first=o.Failure();o.Observe(r1,w2,0,1,1,true);check(!first.empty()&&o.Failure()==first,"first_failure_preserved");
    {
        // Same publication helper as the actual DDS Reader callback. Hold the
        // evidence sink deterministically; a phase observer must not see the
        // final sample until that immutable receipt has been published.
        const ExpectedSamples last{{w1,{1}}};PerReaderValidator ledger(r1,last);
        std::mutex gate;std::condition_variable changed;bool entered=false,release=false,published=false,accepted=false;
        std::thread callback([&]{accepted=ledger.PublishObservation(r1,w1,0,1,1,true,true,[&]{
            std::unique_lock<std::mutex> lock(gate);entered=true;changed.notify_all();
            changed.wait(lock,[&]{return release;});published=true;
        });});
        bool reached=false;{std::unique_lock<std::mutex> lock(gate);reached=changed.wait_for(lock,std::chrono::seconds(2),[&]{return entered;});}
        const bool overtaken=ledger.Complete(last);
        {std::lock_guard<std::mutex> lock(gate);release=true;}changed.notify_all();callback.join();
        check(reached&&!overtaken&&published&&accepted&&ledger.Complete(last),"sample_evidence_precedes_visible_completion");
    }
    std::printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
