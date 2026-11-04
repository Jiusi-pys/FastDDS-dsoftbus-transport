<!-- R38_CURRENT_SCOPED_PASS_PREFIX; the complete prior body below remains byte-preserved historical evidence. -->
# Current follow-up: R38 finite endurance and normal close PASS (2026-10-20)

The new [R38 durable capture acceptance](ROS2_DURABLE_CAPTURE_ACCEPTANCE.md), run `940f4e9c10a29830`, has independent DEVICE / CROSS_DEVICE / INTEGRATION PASS for1801 String roundtrips/7204 complete33-byte CDR stage records, zero loss/duplicates/reordering/foreign payload, both original oracle modes PASS, and spans A1811.239383904/B1811.244691655sec. Both native RuntimeClose0/observation_complete1/public deletion0/interpreter0/device-child0, token restoration/four owned device PIDs absent/zero forces PASS. SDK receipts are696 host0/680 applicable remote0, plus a separate fresh40-SDK query and4 local steps.

Host worker/child actual OS exit0 is observed through exact objects retained live at startup; runner/original CLI Popen and external short-helper exits0 agree. Observer own OS exit stays **UNOBSERVED_NONBUSINESS_AUXILIARY**, with final exact identity absence, complete bounded stable output and closed handles. No long business wrapper was created; old tool24208 exit UNKNOWN and old R38 `efa7336e7ca4b78b` BLOCKED remain unchanged.

R37 run5f28247d2fc3258a remains FAIL/BLOCKED with host exit/cause UNKNOWN; its source-bound native bridge result is not relabelled by the new run. D07's sealed68 Python/0skip and13 Host CTest PASS and the genuine D03 SDK23 native23TU/1037inputs/23objects build are revalidated by final tester D03, not newly rerun/recompiled there;4 post-review HOST controls are separate model-admission/runner evidence. Full ROS2, broad QoS/sizes/concurrency/hours soak, strong per-payload wire lineage, SDK epoch/drain/global leak freedom and R38 restart/recreation remain unverified. Tester Commit gate:PASS is technical scope only; docs synced, Main PRE_COMMIT/all+staged graph/commit/push PENDING, CI NOT_RUN_NO_NEW_COMMIT.

The entire R37 native-close attempt record and all earlier prefixes below remain byte-preserved historical evidence, including their original failures and dated “current” statements. [Current capability matrix](ROS2_CAPABILITY_MATRIX.md).

---

# R37 native observation and normal-close acceptance: FAIL / BLOCKED

Canonical evidence snapshot: 2026-10-20T03:08:59.634827+00:00. **Implementation PRESENT; native BUILDS; HOST_TESTED PASS; DEVICE_TESTED=true and CROSS_DEVICE_TESTED=true for one actual failed attempt; INTEGRATION_NOT_QUALIFIED; VERIFIED=false. Commit gate: BLOCKED. Ready for commit: NO.** Documentation is COMPLETED_FOR_BLOCKED_HANDOFF. No new commit/push exists; CI NOT_RUN_NO_NEW_COMMIT. Detached baseline `e27b7e9d1be7e72944108655a62f9753bfa99448` is historical R35, with no new remote-verification claim. [Current matrix](ROS2_CAPABILITY_MATRIX.md) / [preserved original R36 failure](ROS2_FINITE_ENDURANCE_ACCEPTANCE.md).

## Scope and implementation boundary

The changed bridge aggregates repeated **complete ordered SPDP locator batches** in the existing256 advert slots. Identity includes direction/socket/writer/PID/serialized length/kind/address/port/DATA offset and batch ordering; only observation and packet-sequence IDs are excluded. Retained first/latest observations remain coherent and occurrences checked. A distinct batch must fit atomically or remain fatal. Seven separate invalid-frame/unsupported/parser-capacity/advert-capacity/invalid-selection/selection-capacity/counter-capacity buckets retain strict rejection and conservation. The callback adds no heap allocation, increased cap, timeout, removed assertion, filtered error or weakened public close result. ROS_CLOSE_COMPONENTS separates actual RuntimeClose from observation completion; Flush also checks output errors.

The work remains in native observation/bridge and acceptance tooling. DDS/RTPS/Writer/Reader/discovery/QoS semantics and Transport abstraction are unchanged; the backend uses DSoftBus Socket API. Main/reviewer resolved R37-F1 by binding both Host observation test inputs through prepare, constructor exact membership and immediate pre-dispatch checks: all11 D04 sources/tests are bound. ReviewD02 PASS has no current actionable findings; original reviewD01 FAIL/red counterexample and fixtureD01/D03 failures remain. Review is not a substitute for device execution.

## Source-bound build and package

| Item | Actual evidence |
| --- | --- |
| Previous native reproduction | Provenance tester reproduced old6378ae… bytes from actual23-TU inputs; preserved original observer repetition overflow reproducer. |
| R37 build | Initial independent tester D01 SDK23/AArch64 native bridge;23 translation units,1037 full compiler/link inputs,23 objects,72 repository sources. Configure/build/dependencies/readelf exit0. |
| Artifact | `build/r37-native-close/initial-tester-d01/native-b2/libfastdds_dsoftbus_ros2_target_bridge.so`; SHA-256 `121a7b6dfcb0e6f9858a58aa97a37bbd225c4c73f905a88817bf6642e288511d`. |
| Build binding | `build/r37-native-close/initial-tester-d01/NATIVE_BINDING.json`; SHA-256 `9452113b88fffd58b3d21da29d3dc33f1b0698ccce5fc7f68c31769359c6c836`. |
| Final build status | VALIDATED_REUSE_D01_NOT_NEW_FINAL_SDK_BUILD: all native inputs/objects revalidated; no new final SDK compilation or full Fast DDS rebuild. Fresh final C++ Host build is separate. |
| Candidate package | `build/r37-native-close/package-a5c93a10a7739504/DERIVATION.json`; SHA-256 `e1cb8c3899fcdb644741bfca5b922c27be5d626f67d902bf7900b2b0d4363b19`;3237 members vs parent3236. Among220 ELF members only bridge changes;219 others unchanged;7 C exports/SONAME/NEEDED checked. |
| Source freeze | `build/r37-native-close/IMPLEMENTATION_FREEZE_D04.json`;11 inputs; SHA-256 `ad5ea1351d6da656aaf570a9445eda6f31d14c073db0c0e72c6ea417be0276ff`. |

This closes the native bridge source-to-build binding for this artifact. It does not close the separate stripped-rclpy strong DDS per-payload/wire provenance gap, or requalify all historically accepted cases against this changed shared library.

## Independent Host execution and review

Final `PYTHON_RESULT.json` records46 methods/0 skips:18 endurance +10 native receipt (5 actual build-receipt/5 model) +18 R26. These are Host tests; no model is treated as real DSoftBus communication. Final `CPP_RESULT.json` records a fresh isolated configure/build/CTest of13 cases, all exit0. Additional3 dispatch drift rejections,9 constructor rejections and21 query-review schema negative scenarios are separate controls, not extra Python/CTest method counts. Original environment/profile/WinError5 failures are preserved; a normal-user retry changed no security configuration.

InitialD01/D02 and reviewD01/F1/D03/D04 progression retains each failure. ReviewerD02 `RESULT.json` status PASS_REVIEW_ONLY, F1 CLOSED_CURRENT_HASHES; seal SHA-256 `9707fa864e6ef0e6f4caa99d73b07e32c63702e2c523191fdaaf7ccd5cd09171`. Graph evidence retains caller/branch/walk limits and UNKNOWN dynamic/cross-language edges; it is not exhaustive reachability proof.

## Actual commands and deployment

Evidence paths below are relative to the repository root; executable and repository-root aliases in the displayed commands replace machine-local absolute prefixes only. Exact argv/cwd/environment and exit results remain in the named private receipts. Commands shown here are an execution record, not authorization to replay consumed releases. No private UDID, network identity, payload, configuration or credential value is published.

| Step | Root-relative command/receipt and result |
| --- | --- |
| Initial native configure/build | `initial-tester-d01/native2-configure.json` and `native2-build.json` under `build/r37-native-close/`; configure source `publish-worktree/integrations/ros2/target`, build `initial-tester-d01/native-b2`, SDK23 OHOS toolchain, arm64-v8a/c++_shared/Release, bounded-native-reaccept OFF; both exit0. Complete dependency/toolchain flags remain in receipts. |
| Final Python | `python -X utf8 -B <W>/tests/ros2_target/test_endurance_roundtrip.py -v`; `test_native_close_receipt.py -v` with actual native binding; `test_sustained_roundtrip.py -v`. Exact argv: `final-tester-d01/endurance.intent.json`, `actual-native-receipt.intent.json`, `R26-sustained.intent.json`; all recorded exit0. |
| Final C++ | `cmake -S <W>/tests/ros2_host/locator_observation -B <E>/final-tester-d01/host-normal-repaired -G Ninja -DCMAKE_BUILD_TYPE=Release -DOBSERVATION_SOURCE_ROOT=<W> -DFASTDDS_DSOFTBUS_LEGACY_OBSERVER=OFF`; `cmake --build <E>/final-tester-d01/host-normal-repaired --parallel 2 --verbose`; `ctest --test-dir <E>/final-tester-d01/host-normal-repaired --output-on-failure -V`. `fresh-host-*.intent.json`/receipts retain exact tool paths and make-program flag; configure/build/CTest exit0. |
| Query | Fresh Q2 run88611e7290d1b53d;40 actual successful SDK commands/four local continuations; exact11-field independent review. Preparation `build/r25-native-actionbag/r37-native-close-3f839def17d28c0f-preparation/`; old Q1 remains HOLD and unreplayed. |
| Business | Single actual command below; exact record `build/r37-native-close/final-tester-d01/BUSINESS_COMMAND.json`. Original host status RUNNING retained; terminal exit UNKNOWN.198 recorded business SDK receipts each host exit0 do not establish outer completion. |
| Read-only recovery |11 separately recorded SDK commands; `build/r37-native-close/final-tester-d01/host-session-loss-d01/`; no signal/restart/replay/manual close/cleanup mutation. |

Here `<W>` means `build/r37-native-close/publish-worktree`, `<E>` means `build/r37-native-close`, and `<SDK23-hdc>` means the originally pinned SDK23 executable. The recorded single business invocation, with root-relative path spelling, was:

```text
python -X utf8 -B build/r37-native-close/publish-worktree/tests/ros2_target/run_endurance_roundtrip.py run --prepared build/r37-native-close/prepared-f3df07623afca45c/PLAN.json --release build/r37-native-close/MAIN_BUSINESS_RELEASE_D01.json --hdc <SDK23-hdc> --output build/r37-native-close/execution-818d77fc83a73d2d
```

| Board | Existing HDC target | Deployment |
| --- | --- | --- |
| A requester | `3e01ff55454d202020104033bf453b00` | `/data/local/tmp/mdds_dds_5f28247d2fc3258a_A/package` |
| B responder | `3e01ff55454d202020104433991c3b00` | `/data/local/tmp/mdds_dds_5f28247d2fc3258a_B/package` |

This is SDK23/AArch64 OpenHarmony RK3588A, selected CPython3.12.7/ROS2 Jazzy/rmw_fastrtps_cpp/domain201, two Participants/one peer, public Node String pub/sub echo. Work remains1801 roundtrips, at most1Hz/minimum payload span1800s,33-byte std_msgs/String CDR, RELIABLE/VOLATILE/KEEP_LAST10, one outstanding/no application retry or dedupe. Bounds remain listener60s/business1900s/probe1910s/absolute close1950s/exit20s,5s no-progress guard,4MiB per stream,240s dispatch freshness; no deadline reset/increase. Actual release age130.764801s was within240s.

## Actual payload, observation and failed close

Independent read-only extraction from the original rejected streams finds1801 ordered roundtrips and7204 complete33-byte CDR stage records (A send/B receive/B echo/A receive), all equal per sequence; zero loss/duplicates/reordering. Local spans A1811.577871855s/B1811.581102355s exceed1800s. This is **PASS_PAYLOAD_OBSERVATIONS_ONLY**, not the original oracle's acceptance. The untouched full streams contain a final FAIL with `R36 fixed1910s probe deadline exceeded`; both original `verify_roundtrip_logs(..., final=False)` and `final=True` fail. Strict native capture, both locator checks and both diagnostic checks also FAIL. Their tracebacks remain under final-tester-d01/strict/ and strict-final/. The first private audit failure is retained; no parser/raw filtering converts it to PASS.

| Observation | A | B | Meaning |
| --- | --- | --- | --- |
| Observer captured / faults / retained adverts / selected |1 /0 /4 /31 |1 /0 /4 /27 | Read-only observed values; original capacity overflow not reproduced in this attempt. |
| Aggregation observed / repeated / repeated batches |5130 /5126 /2563 |5130 /5126 /2563 | Two coherent distinct ordered batches per side; conservation checked; all7 fatal counters0. |
| OriginalDeleteParticipant return |0 |0 | Separate underlying delete result. |
| RuntimeClose / observation_complete |2 /1 |2 /1 | Runtime failure remains; completed observation does not mask it. |
| Public Factory_delete_participant / child exit |1 /71 |1 /71 | Required public close/exit gate FAIL. |
| Interpreter execution_rc / teardown_rc |1 /1 |1 /1 | Original failure diagnostics preserved. |
| Restore-token comparison |matches1 /rc0 |matches1 /rc0 | Restored1; original numeric token not logged. |

Both stderr streams include `ROS_RUNTIME_CLOSE_FAILURE reason=CONTROL_MISSING_OR_INVALID`, `FDDST_CLOSE_RESOURCE_COMPLETE_WITH_RUNTIME_ERROR` and `Failed to delete participant`. Original PROBE_FINISH and RuntimeClose normal-close host controls are absent. No later close was injected.

The original host last emitted elapsed388.344s/A385/B385 progress; this host tail is not the board's final work count. Its registered outer/runner processes are absent and its tool session is unavailable. Original `BUSINESS_COMMAND.json` stays unchanged RUNNING, with **original host exit UNKNOWN and capture-loss cause UNKNOWN**. No matching visible Windows Application1000/1001/1002 python/pwsh/hdc event was found in the requested02:30:30–02:33:00UTC interval; absence does not diagnose the cause or prove a normal timeout, human action, worker restart or DSoftBus outage. Canonical original runner RESULT/CLEANUP/ANALYSIS are absent.

Recovery performed11 read-only SDK commands and obtained six original board files (`A.raw.stdout`, `A.raw.stderr`, `A.raw.exit`, `B.raw.stdout`, `B.raw.stderr`, `B.raw.exit`) under `build/r37-native-close/final-tester-d01/host-session-loss-d01/`: remote-before/local/remote-after SHA match. All four exactly owned device PIDs are absent. Signals/release replays/restarts/manual close/cleanup mutations are0. Recovery establishes original bytes/process absence/token restoration; it cannot replace the missing normal-close evidence or retroactively finish the host runner.

## Measurements and limits

A-local logged application echo RTT for1801 pairs: min1.320375ms, median2.310876ms, p952.947292ms, p993.455083ms, max5.919959ms. Board monotonic clocks are not compared across devices. This includes application/probe/runtime costs and is not wire latency or a Transport benchmark.

| Process observations (32 samples each) | A | B |
| --- | --- | --- |
| VmRSS / VmHWM range KiB |82772..82968 |82892..82964 |
| Threads |23 |23 |
| FD range |19..20 |20..20 |
| RSS slope after received60, KiB/min |0.09705448262535026 |0.09705426284228406 |

Finite resource observations do not prove leak freedom. Full ROS2 remains NOT_VERIFIED. Wider QoS/large messages/concurrency/hours-day soak, SDK epochs/global callback drain/leak freedom, process restart/Participant recreation and strong DDS/wire provenance are not established. Default automatic reconnect is NOT_PROVIDED by accepted configuration; third observer unsupported. Loan and recorder-internal take remain UNOBSERVABLE; selected-client status/feedback ordering OPEN. R22 remains STOPPED/excluded. Historical service/Action/parameter/serialized-bag PASS remains limited to its original cases and library.

## Canonical evidence and blocked handoff

All following paths are repository-root relative under `build/r37-native-close/`:

| Record | Role / SHA-256 |
| --- | --- |
| `final-tester-d01/RESULT.json` | Canonical failed final result; `61d382a8fd713644ad25f2a28d10363046dd486c53515b392f3ff0b4a594a967`. |
| `final-tester-d01/REPORT.md`, `DELIVERY.json`, `SEAL.json` | Final execution/report/release; seal `2f67bf2a89742a3398a4ddd8e92116640cb873aad7c016639df3b6d1223b3f14`;468 sealed evidence members reverified by Main. |
| `final-tester-d01/DEVICE_OBSERVATIONS.json` | Failed raw close/observer observations; `041495da97a68be1fb17a6c6b7bce53214a2688f8a23adab259184b9ed5d3d98`. |
| `final-tester-d01/PAYLOAD_CONTENT_RESULT.json` | Separate independent payload extraction only; `4869397522b9a2edc5cd6e698551ffb5169b7d9ede885ef57ee35a0dfcd5d809`. |
| `final-tester-d01/BUSINESS_COMMAND.json`, `business.stdout`, `host-session-loss-d01/` | Unchanged original RUNNING host record, interrupted host tail, six recovered board raw files/read-only receipts; original command SHA `564905162502ab2e1b92df86c1f5f73cefcf7c62ae5b94265574ca246640035a`. |
| `final-tester-d01/HOST_RESULT.json`, `PYTHON_RESULT.json`, `CPP_RESULT.json` | Actual final Host method/case counts and receipts; SDK native build reuse recorded separately. |
| `final-tester-d01/FINAL_PRESERVATION.json` |11 frozen inputs,1037 native inputs/23 objects,3237 package members,10574 historical members/four protected Git trees preserved across final test.844-source/589-derived-cache ledgers cover the completed tester window; they allow this required later documentation sync. |
| `reviewer-d02/RESULT.json`, `MAIN_FINAL_FAILURE_ADMISSION_D01.json` | Independent F1-closed review and Main's authentic failed-evidence admission; neither grants publication. |
| `docs-before-d01/MANIFEST.json`, `docs-updater-d01/` | Seven original document bodies554137 bytes preserved as exact tails; documentation-only blocked handoff checks and seal. |

Original R36 FAIL, reviewD01-F1 FAIL, Main red, fixtureD01/D03 FAIL and private environment failures are retained with their original seals. Implementation/build/Host success and repaired aggregation observations do not turn this failed full acceptance into VERIFIED. The next admissible technical work requires grounded host-supervision diagnosis and a separately fresh reviewed attempt; no new attempt is part of this documentation stage. PRE_COMMIT remains BLOCKED; **Ready for commit:NO**; Git state transitions0.
