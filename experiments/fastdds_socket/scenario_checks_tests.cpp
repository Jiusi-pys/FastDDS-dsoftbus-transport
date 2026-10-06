#include "ScenarioChecks.hpp"
#include <cstdio>
#include <stdexcept>
using namespace fastdds_dsoftbus::experiment;
static void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
int main() {
    try {
        ScenarioEvidence e;
        e.publisher = true; e.exclusive_transport = true;
        e.participant_discovered = e.endpoint_discovered = e.matched_once = true;
        e.spdp_tx = e.spdp_rx = e.sedp_required_tx = e.sedp_required_rx = e.user_required = 1;
        e.receiver_returns = e.sdk_send_returns = 1;
        e.writes = 10; e.acknowledged = true;
        e.cleanup_observed = e.token_restored = true;
        Require(ScenarioDataComplete(e) && ScenarioComplete(e), "complete real-path evidence admitted");
        e.cleanup_observed = false;
        Require(ScenarioDataComplete(e) && !ScenarioComplete(e), "samples cannot hide close failure");
        e.cleanup_observed = true; e.watchdog_terminated = true;
        Require(!ScenarioComplete(e), "watchdog is never successful cleanup");
        e.watchdog_terminated = false; e.spdp_rx = 0;
        Require(!ScenarioDataComplete(e), "raw SDK echo is not DDS discovery");
        e.spdp_rx = 1; e.sedp_required_rx = 0;
        Require(!ScenarioDataComplete(e), "SPDP alone is not endpoint discovery");
        e.sedp_required_rx = 1; e.exclusive_transport = false;
        Require(!ScenarioDataComplete(e), "other transport path cannot count");
        e.exclusive_transport = true; e.publisher = false; e.sample_mask = 0x3ff;
        Require(ScenarioComplete(e), "reader has every exact expected sample");
        e.sample_mask = 0x1ff;
        Require(!ScenarioComplete(e), "missing sample rejected");
        e.sample_mask = 0x3ff; e.invalid_samples = 1;
        Require(!ScenarioComplete(e), "wrong identity/content or duplicates rejected");
        e.invalid_samples = 0; e.token_restored = false;
        Require(!ScenarioComplete(e), "restore failure cannot report scenario success");
        e.token_restored=true;
        for(const auto bytes:{262144u,1048576u}) {
            e.sample_bytes=bytes;e.publisher=true;e.writes=3;e.fragment_observations=1;
            Require(ScenarioComplete(e),"bounded large publisher requires three acknowledged samples and DATA_FRAG");
            e.publisher=false;e.sample_mask=7;
            Require(ScenarioComplete(e),"bounded large reader requires all three exact samples and DATA_FRAG");
            e.fragment_observations=0;
            Require(!ScenarioComplete(e),"large sample success without actual DATA_FRAG is insufficient");
            e.fragment_observations=1;e.sample_mask=3;
            Require(!ScenarioComplete(e),"large missing sample cannot pass");
            e.sample_mask=7;e.cleanup_observed=false;
            Require(ScenarioDataComplete(e)&&!ScenarioComplete(e),"large sample path cannot hide close failure");
            e.cleanup_observed=true;e.watchdog_terminated=true;
            Require(!ScenarioComplete(e),"large watchdog never passes");
            e.watchdog_terminated=false;
        }
        e.sample_bytes=1048577;e.publisher=true;e.writes=10;e.sample_mask=0x3ff;
        Require(!ScenarioComplete(e),"unsupported sample size cannot reuse legacy success predicate");
        std::puts("PASS scenario evidence: discovery/data/cleanup are distinct; bypass/watchdog rejected");
        return 0;
    } catch (const std::exception& e) { std::printf("FAIL %s\n", e.what()); return 1; }
}
