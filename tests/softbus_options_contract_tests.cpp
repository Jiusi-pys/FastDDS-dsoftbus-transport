#include "softbus_options_contract.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace fastdds_dsoftbus_probe;
namespace {
void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
OptionsQueryObservation Query(std::uint64_t tid, std::int64_t at) {
    return {true, tid, at, at + 100, 0, static_cast<int>(sizeof(bool)), 0};
}
OptionsAdmission Admission() {
    return {10, Query(10, 0), Query(11, 200), 400, 0};
}
OptionsCompletion Completed() {
    OptionsCompletion result; result.admission = Admission(); result.admission.set_attempts = 1;
    result.set_entered_us = 500; result.set_returned_us = 600; result.set_sdk_rc = -99;
    result.same_thread = Query(10, 700); result.cross_thread = Query(12, 900);
    result.cross_thread_joined_us = 1100; result.shutdown_entered_us = 1200; result.shutdown_returned_us = 1300;
    return result;
}
}
int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"OPT-01 exact false query permits negative experiment only with independent baseline", [] {
            const auto admission = Admission();
            Require(OptionsKnownUnsupported(admission.initial), "observed canonical false recognized");
            Require(OptionsMaySetNeedAck(admission), "false and successful joined cross-thread baseline admit once");
        }},
        {"OPT-02 true malformed or failed query never authorizes NEED_ACK", [] {
            for (unsigned choice = 0; choice != 7; ++choice) {
                auto admission = Admission();
                if (choice == 0) admission.initial.raw_value = 1;
                if (choice == 1) admission.initial.raw_value = 2;
                if (choice == 2) admission.initial.sdk_rc = -1;
                if (choice == 3) admission.initial.reported_size = 0;
                if (choice == 4) admission.initial.reported_size = sizeof(bool) + 1;
                if (choice == 5) admission.initial.returned = false;
                if (choice == 6) admission.initial.returned_us = -1;
                Require(!OptionsMaySetNeedAck(admission), "native evidence cannot be inferred from false default");
            }
        }},
        {"OPT-03 baseline must actually return join and use another thread", [] {
            for (unsigned choice = 0; choice != 7; ++choice) {
                auto admission = Admission();
                if (choice == 0) admission.baseline.tid = 10;
                if (choice == 1) admission.baseline.tid = 0;
                if (choice == 2) admission.baseline.returned = false;
                if (choice == 3) admission.baseline.sdk_rc = -2;
                if (choice == 4) admission.baseline.raw_value = 1;
                if (choice == 5) admission.baseline_joined_us = -1;
                if (choice == 6) admission.baseline.entered_us = 99;
                Require(!OptionsMaySetNeedAck(admission), "same-thread-only observation cannot falsify recursive lock retention");
            }
        }},
        {"OPT-04 admission consumes one attempt and respects exact worker budget", [] {
            auto admission = Admission(); admission.baseline.returned_us = admission.baseline.entered_us + options_worker_budget_us;
            admission.baseline_joined_us = admission.baseline.returned_us;
            Require(OptionsMaySetNeedAck(admission), "inclusive 1500 ms baseline");
            ++admission.baseline_joined_us;
            Require(!OptionsMaySetNeedAck(admission), "late join cannot establish bounded baseline");
            admission = Admission(); admission.set_attempts = 1;
            Require(!OptionsMaySetNeedAck(admission), "no repeated mutating call");
        }},
        {"OPT-05 SDK error and cross-thread completion are separate observations", [] {
            auto value = Completed();
            Require(EvaluateOptionsCompletion(value) == OptionsVerdict::ReturnedObserved, "expected option error does not erase real cross-thread return evidence");
            value.set_sdk_rc = 0;
            Require(EvaluateOptionsCompletion(value) == OptionsVerdict::ReturnedObserved, "native rc retained without pretending it is worker status");
        }},
        {"OPT-06 watchdog live worker missing cleanup and late join cannot pass", [] {
            for (unsigned choice = 0; choice != 7; ++choice) {
                auto value = Completed();
                if (choice == 0) value.watchdog_terminated = true;
                if (choice == 1) value.cross_thread.returned = false;
                if (choice == 2) value.cross_thread_joined_us = -1;
                if (choice == 3) value.shutdown_returned_us = -1;
                if (choice == 4) value.failure = true;
                if (choice == 5) value.cross_thread_joined_us = value.cross_thread.entered_us + options_worker_budget_us + 1;
                if (choice == 6) value.cross_thread.tid = value.admission.main_tid;
                Require(EvaluateOptionsCompletion(value) == OptionsVerdict::Failed, "incomplete or abandoned native work cannot become orderly return evidence");
            }
        }},
        {"OPT-07 support true is NOT_TRIGGERED only after observed cleanup", [] {
            auto value = Completed(); value.admission.initial.raw_value = 1; value.admission.set_attempts = 0;
            value.set_entered_us = value.set_returned_us = -1;
            Require(EvaluateOptionsCompletion(value) == OptionsVerdict::NotTriggered, "known supported socket does not exercise unsupported branch");
            value.shutdown_returned_us = -1;
            Require(EvaluateOptionsCompletion(value) == OptionsVerdict::Failed, "not-triggered is not permission to omit cleanup");
        }},
        {"OPT-08 returned query interval validity is explicit", [] {
            auto value = Query(20, 10);
            Require(OptionsWorkerReturnedWithinBudget(value), "bounded return interval");
            value.returned_us = value.entered_us + options_worker_budget_us + 1;
            Require(!OptionsWorkerReturnedWithinBudget(value), "actual worker deadline unchanged");
            value.returned_us = value.entered_us - 1;
            Require(!OptionsWorkerReturnedWithinBudget(value), "same monotonic clock ordering required");
        }},
        {"OPT-09 native exit is separate from logging and worker completion", [] {
            Require(OptionsNativeCallActive(100, -1, 1500101), "entered call without observed return remains live at observation");
            Require(!OptionsNativeCallActive(100, 110, 1500101), "completed SDK call cannot be reported live while worker logs");
            Require(!OptionsNativeCallActive(-1, -1, 1500101), "unscheduled worker is not evidence of native blocking");
            Require(!OptionsNativeCallActive(100, -1, 99), "pre-entry observation cannot claim live native invocation");
            auto value = Completed(); value.cross_thread.sdk_rc = -42;
            Require(OptionsWorkerReturnedWithinBudget(value.cross_thread), "error still has a real timely return");
            Require(EvaluateOptionsCompletion(value) == OptionsVerdict::Failed, "failed query is not successful lifecycle experiment");
        }}
    };
    unsigned failures = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failures; std::cout << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    return failures ? 1 : 0;
}
