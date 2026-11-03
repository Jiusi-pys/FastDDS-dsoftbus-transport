#pragma once
#include "ScenarioChecks.hpp"
#include <atomic>
namespace fastdds_dsoftbus::experiment {
enum class LossMode { Disabled, Reliable, BestEffort };
enum class LossVerdict { Pass, Fail, NotExercised };
// The discovery callback claims one attempt, then publishes the completed
// result. The main thread may create its Reader only after that publication.
class LossArmProgress final {
public:
    bool TryClaim() noexcept { return !claimed_.exchange(true); }
    void Publish(bool success) noexcept { armed_.store(success); completed_.store(true); }
    bool Completed() const noexcept { return completed_.load(); }
    bool Armed() const noexcept { return Completed() && armed_.load(); }
private:
    std::atomic<bool> claimed_{false}, completed_{false}, armed_{false};
};
inline bool ControlledObservationComplete(bool elapsed,std::uint64_t shutdown_before,std::uint64_t shutdown_after) noexcept {
    return elapsed && shutdown_before==0 && shutdown_after==0;
}
struct LossEvidence {
    LossMode mode = LossMode::Disabled;
    bool matching_qos_verified = false, ack_called = false, observation_window_complete = false;
    bool armed = false;
    std::uint64_t dropped_messages = 0, recovered_range_deliveries = 0;
};
inline bool ValidControlledLoss(LossMode mode, std::uint32_t sample_bytes, bool build_enabled) noexcept {
    return mode==LossMode::Disabled || ((mode==LossMode::Reliable || mode==LossMode::BestEffort) &&
        build_enabled && sample_bytes==262144);
}
inline LossVerdict LossDataVerdict(const ScenarioEvidence& e, const LossEvidence& loss) noexcept {
    if(loss.mode==LossMode::Disabled) return ScenarioDataComplete(e)?LossVerdict::Pass:LossVerdict::Fail;
    if(!ValidControlledLoss(loss.mode,e.sample_bytes,true)) return LossVerdict::Fail;
    if(!e.publisher && (!loss.armed || loss.dropped_messages==0)) return LossVerdict::NotExercised;
    if(!e.publisher && loss.dropped_messages!=1) return LossVerdict::Fail;
    if(!e.publisher && loss.mode==LossMode::BestEffort && loss.recovered_range_deliveries)
        return LossVerdict::NotExercised; // The selected range did not remain missing.
    // Reuse the unchanged standard path checks; delivery and ACK requirements
    // are evaluated below for this separate controlled-loss experiment.
    auto path=e; path.acknowledged=true; path.sample_mask=SampleMask(e.sample_bytes);
    if(!ScenarioDataComplete(path) || !loss.matching_qos_verified) return LossVerdict::Fail;
    if(loss.mode==LossMode::Reliable) {
        const bool complete=e.publisher ? loss.ack_called && e.acknowledged :
            e.sample_mask==7 && loss.recovered_range_deliveries>0;
        return complete?LossVerdict::Pass:LossVerdict::Fail;
    }
    const bool complete=!loss.ack_called && loss.observation_window_complete &&
        (e.publisher || e.sample_mask==6);
    return complete?LossVerdict::Pass:LossVerdict::Fail;
}
inline bool LossScenarioComplete(const ScenarioEvidence& e, const LossEvidence& loss) noexcept {
    return LossDataVerdict(e,loss)==LossVerdict::Pass && e.cleanup_observed && e.token_restored && !e.watchdog_terminated;
}
}
