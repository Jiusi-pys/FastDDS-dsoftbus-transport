#pragma once
#include <cstdint>

namespace fastdds_dsoftbus_probe {
inline constexpr std::int64_t options_worker_budget_us = 1500000;

struct OptionsQueryObservation {
    bool returned = false;
    std::uint64_t tid = 0;
    std::int64_t entered_us = -1, returned_us = -1;
    int sdk_rc = -1, reported_size = -1, raw_value = -1;
};
struct OptionsAdmission {
    std::uint64_t main_tid = 0;
    OptionsQueryObservation initial, baseline;
    std::int64_t baseline_joined_us = -1;
    unsigned set_attempts = 0;
};

// A successful API rc alone is insufficient: the bool's reported size and raw
// representation must be valid, and the observed capability must be false.
bool OptionsKnownUnsupported(const OptionsQueryObservation&) noexcept;
bool OptionsMaySetNeedAck(const OptionsAdmission&) noexcept;
bool OptionsWorkerReturnedWithinBudget(const OptionsQueryObservation&) noexcept;
bool OptionsNativeCallActive(std::int64_t entered_us, std::int64_t returned_us, std::int64_t observed_us) noexcept;

struct OptionsCompletion {
    OptionsAdmission admission;
    std::int64_t set_entered_us = -1, set_returned_us = -1;
    int set_sdk_rc = 0;
    OptionsQueryObservation same_thread, cross_thread;
    std::int64_t cross_thread_joined_us = -1, shutdown_entered_us = -1, shutdown_returned_us = -1;
    bool failure = false, watchdog_terminated = false;
};
// Even ReturnedObserved means only this diagnostic's observed calls completed.
// It is never a production option/cancellation/quiescence qualification.
enum class OptionsVerdict { NotTriggered, ReturnedObserved, Failed };
OptionsVerdict EvaluateOptionsCompletion(const OptionsCompletion&) noexcept;
const char* OptionsVerdictName(OptionsVerdict) noexcept;
}
