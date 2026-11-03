#pragma once
#include <cstdint>
namespace fastdds_dsoftbus::experiment {
inline constexpr char skip_default_xml_variable[] = "SKIP_DEFAULT_XML_FILE";
// Evidence for the explicitly selected, one-peer engineering experiment only.
// Not a platform qualification or a claim about SDK-internal resource drain.
struct ScenarioEvidence {
    bool publisher = false, exclusive_transport = false;
    bool participant_discovered = false, endpoint_discovered = false, matched_once = false;
    std::uint64_t spdp_tx = 0, spdp_rx = 0, sedp_required_tx = 0, sedp_required_rx = 0;
    std::uint64_t user_required = 0, receiver_returns = 0, sdk_send_returns = 0;
    unsigned writes = 0, sample_mask = 0, invalid_samples = 0;
    std::uint32_t sample_bytes = 0;
    std::uint64_t fragment_observations = 0;
    bool acknowledged = false, cleanup_observed = false, token_restored = false, watchdog_terminated = false;
};
inline bool ValidSampleBytes(std::uint32_t bytes) noexcept {
    return bytes==0 || bytes==262144 || bytes==1048576;
}
inline unsigned SampleCount(std::uint32_t bytes) noexcept { return bytes==0?10u:3u; }
inline unsigned SampleMask(std::uint32_t bytes) noexcept { return (1u<<SampleCount(bytes))-1u; }
inline bool ScenarioDataComplete(const ScenarioEvidence& e) noexcept {
    return ValidSampleBytes(e.sample_bytes) && (!e.sample_bytes || e.fragment_observations) &&
        e.exclusive_transport && e.participant_discovered && e.endpoint_discovered && e.matched_once &&
        e.spdp_tx && e.spdp_rx && e.sedp_required_tx && e.sedp_required_rx && e.user_required &&
        e.receiver_returns && e.sdk_send_returns && !e.invalid_samples &&
        (e.publisher ? e.writes == SampleCount(e.sample_bytes) && e.acknowledged : e.sample_mask == SampleMask(e.sample_bytes));
}
inline bool ScenarioComplete(const ScenarioEvidence& e) noexcept {
    return ScenarioDataComplete(e) && e.cleanup_observed && e.token_restored && !e.watchdog_terminated;
}
}
