#include "softbus_options_contract.hpp"
namespace mdds_probe {
bool OptionsNativeCallActive(std::int64_t entered, std::int64_t returned, std::int64_t observed) noexcept {
    return entered >= 0 && returned < 0 && observed >= entered;
}
namespace {
bool ValidBoolQuery(const OptionsQueryObservation& query) noexcept {
    return query.returned && query.tid != 0 && query.entered_us >= 0 && query.returned_us >= query.entered_us &&
           query.sdk_rc == 0 && query.reported_size == static_cast<int>(sizeof(bool)) &&
           (query.raw_value == 0 || query.raw_value == 1);
}
}
bool OptionsKnownUnsupported(const OptionsQueryObservation& query) noexcept {
    return ValidBoolQuery(query) && query.raw_value == 0;
}
bool OptionsMaySetNeedAck(const OptionsAdmission& value) noexcept {
    return value.main_tid != 0 && value.initial.tid == value.main_tid && value.set_attempts == 0 &&
           OptionsKnownUnsupported(value.initial) && OptionsKnownUnsupported(value.baseline) &&
           value.baseline.tid != value.main_tid && value.baseline.entered_us >= value.initial.returned_us &&
           OptionsWorkerReturnedWithinBudget(value.baseline) && value.baseline_joined_us >= value.baseline.returned_us &&
           value.baseline_joined_us - value.baseline.entered_us <= options_worker_budget_us;
}
bool OptionsWorkerReturnedWithinBudget(const OptionsQueryObservation& query) noexcept {
    return query.returned && query.tid != 0 && query.entered_us >= 0 && query.returned_us >= query.entered_us &&
           query.returned_us - query.entered_us <= options_worker_budget_us;
}
OptionsVerdict EvaluateOptionsCompletion(const OptionsCompletion& value) noexcept {
    if (value.failure || value.watchdog_terminated || value.shutdown_entered_us < 0 ||
            value.shutdown_returned_us < value.shutdown_entered_us) return OptionsVerdict::Failed;
    const auto& initial = value.admission.initial;
    if (value.admission.set_attempts == 0) {
        return ValidBoolQuery(initial) && initial.raw_value == 1 && initial.tid == value.admission.main_tid &&
               value.set_entered_us < 0 && value.set_returned_us < 0 && value.shutdown_entered_us >= initial.returned_us ?
               OptionsVerdict::NotTriggered : OptionsVerdict::Failed;
    }
    auto admission = value.admission; admission.set_attempts = 0;
    if (value.admission.set_attempts != 1 || !OptionsMaySetNeedAck(admission) ||
            value.set_entered_us < admission.baseline_joined_us || value.set_returned_us < value.set_entered_us ||
            !ValidBoolQuery(value.same_thread) || value.same_thread.tid != admission.main_tid ||
            value.same_thread.entered_us < value.set_returned_us || !ValidBoolQuery(value.cross_thread) ||
            value.cross_thread.tid == admission.main_tid || value.cross_thread.entered_us < value.same_thread.returned_us ||
            !OptionsWorkerReturnedWithinBudget(value.cross_thread) || value.cross_thread_joined_us < value.cross_thread.returned_us ||
            value.cross_thread_joined_us - value.cross_thread.entered_us > options_worker_budget_us ||
            value.shutdown_entered_us < value.cross_thread_joined_us) return OptionsVerdict::Failed;
    return OptionsVerdict::ReturnedObserved;
}
const char* OptionsVerdictName(OptionsVerdict value) noexcept {
    return value == OptionsVerdict::NotTriggered ? "NOT_TRIGGERED" :
           value == OptionsVerdict::ReturnedObserved ? "CROSS_THREAD_RETURN_OBSERVED" : "FAIL";
}
}
