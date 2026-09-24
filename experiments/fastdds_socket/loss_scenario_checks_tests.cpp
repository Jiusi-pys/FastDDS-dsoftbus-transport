#include "LossScenarioChecks.hpp"
#include <cstdio>
using namespace mdds::experiment;
int main() {
    int failed=0;
    const auto check=[&](bool value,const char* label) { std::printf("%s %s\n",value?"PASS":"FAIL",label); if(!value) ++failed; };
    LossArmProgress arm;
    check(!arm.Completed() && !arm.Armed(),"arm_not_started_is_pending");
    check(arm.TryClaim(),"discovery_can_claim_first_attempt");
    // Deterministically stop at the callback stage before the actual ArmRxDrop
    // result is available, then execute the main-thread readiness observation.
    check(!arm.Completed() && !arm.Armed(),"claimed_attempt_is_still_pending_before_sdk_independent_arm_result");
    check(!arm.TryClaim(),"second_discovery_cannot_take_in_progress_attempt");
    arm.Publish(true);
    check(arm.Completed() && arm.Armed(),"successful_arm_publication_allows_reader_creation");
    LossArmProgress rejected;
    check(rejected.TryClaim(),"rejected_attempt_claimed");
    rejected.Publish(false);
    check(rejected.Completed() && !rejected.Armed(),"completed_rejection_distinct_from_in_progress");
    check(ControlledObservationComplete(true,0,0),"observation_elapsed_without_shutdown_callback");
    check(!ControlledObservationComplete(false,0,0),"observation_undersized_duration_rejected");
    check(!ControlledObservationComplete(true,1,1),"observation_peer_already_shutdown_rejected");
    check(!ControlledObservationComplete(true,0,1),"observation_peer_shutdown_during_window_rejected");
    ScenarioEvidence e;
    e.exclusive_transport=e.participant_discovered=e.endpoint_discovered=e.matched_once=true;
    e.spdp_tx=e.spdp_rx=e.sedp_required_tx=e.sedp_required_rx=e.user_required=e.receiver_returns=e.sdk_send_returns=1;
    e.sample_bytes=262144; e.fragment_observations=15; e.sample_mask=7; e.writes=3;
    e.acknowledged=e.cleanup_observed=e.token_restored=true;
    LossEvidence loss; loss.mode=LossMode::Reliable; loss.matching_qos_verified=loss.armed=true; loss.dropped_messages=1;
    loss.recovered_range_deliveries=1;
    check(LossScenarioComplete(e,loss),"reliable_receiver_complete");
    loss.dropped_messages=0;
    check(LossDataVerdict(e,loss)==LossVerdict::NotExercised,"no_injection_not_exercised");
    loss.dropped_messages=1; loss.armed=false;
    check(LossDataVerdict(e,loss)==LossVerdict::NotExercised,"unarmed_not_exercised");
    loss.armed=true; loss.recovered_range_deliveries=0;
    check(LossDataVerdict(e,loss)==LossVerdict::Fail,"delivery_without_range_correlation_not_pass");
    loss.recovered_range_deliveries=1; e.cleanup_observed=false;
    check(!LossScenarioComplete(e,loss),"normal_close_required"); e.cleanup_observed=true;
    e.watchdog_terminated=true; check(!LossScenarioComplete(e,loss),"watchdog_not_cleanup"); e.watchdog_terminated=false;
    e.publisher=true; loss.ack_called=true;
    check(LossScenarioComplete(e,loss),"reliable_publisher_acknowledged");
    loss.ack_called=false; check(!LossScenarioComplete(e,loss),"reliable_ack_call_required"); loss.ack_called=true;
    loss.matching_qos_verified=false; check(!LossScenarioComplete(e,loss),"actual_matching_qos_required"); loss.matching_qos_verified=true;
    e.publisher=false; e.sample_mask=6; e.acknowledged=false;
    loss.mode=LossMode::BestEffort; loss.ack_called=false; loss.recovered_range_deliveries=0; loss.observation_window_complete=true;
    check(LossScenarioComplete(e,loss),"best_effort_later_samples_exact_no_incomplete_delivery");
    e.sample_mask=7; check(!LossScenarioComplete(e,loss),"best_effort_target_must_not_appear"); e.sample_mask=6;
    e.sample_mask=2; check(!LossScenarioComplete(e,loss),"best_effort_both_later_samples_required"); e.sample_mask=6;
    loss.ack_called=true; check(!LossScenarioComplete(e,loss),"best_effort_ack_not_called"); loss.ack_called=false;
    loss.observation_window_complete=false; check(!LossScenarioComplete(e,loss),"best_effort_observation_window_required"); loss.observation_window_complete=true;
    loss.recovered_range_deliveries=1;
    check(LossDataVerdict(e,loss)==LossVerdict::NotExercised,"best_effort_range_not_missing_invalidates_control");
    loss.recovered_range_deliveries=0; e.publisher=true;
    check(LossScenarioComplete(e,loss),"best_effort_publisher_does_not_use_dds_ack");
    check(!ValidControlledLoss(LossMode::Reliable,262144,false),"disabled_build_rejects_loss");
    check(!ValidControlledLoss(LossMode::BestEffort,1048576,true),"loss_size_requires_256k");
    check(ValidControlledLoss(LossMode::Reliable,262144,true),"explicit_build_and_size_admit_loss");
    std::printf("failed=%d\n",failed);
    return failed?1:0;
}
