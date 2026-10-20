<!-- R38_CURRENT_SCOPED_PASS_PREFIX; the complete prior body below remains byte-preserved historical evidence. -->
# Current capability evidence: R38 selected finite case PASS (2026-10-20)

[R38 complete acceptance and evidence](ROS2_DURABLE_CAPTURE_ACCEPTANCE.md) is the current snapshot. **Full ROS2 remains NOT_VERIFIED.** New run `940f4e9c10a29830` has1801 roundtrips/7204 complete33B CDR records, both unchanged oracle modes PASS, zero loss/duplicates/reordering/foreign content, spans A1811.239383904/B1811.244691655sec. DEVICE_TESTED and INTEGRATION_TESTED apply only to this case. Historical matrices and failure verdicts below retain their original dated scopes; they are not new native-hash qualifications.

| Capability / gate | Current status | Evidence and boundary |
| --- | --- | --- |
| Selected public Node String pub/sub echo | SUPPORTED_WITHIN_TESTED_SCOPE / DEVICE+INTEGRATION PASS | New real two-board1801 case, RELIABLE/VOLATILE/KEEP_LAST10, 33-byte CDR, one outstanding/no application retry or dedupe. |
| Native observation / normal close | PASS_CURRENT_FINITE_CASE | Same ordered SPDP batches aggregate within256 slots; per side stored4/observed4902/repeated4898,2 distinct/2449 repeated batches; all7 fatal counters0; RuntimeClose0/observation_complete1/public deletion0/interpreter0/child0. These SPDP counters are not business CDR or wire-causality counts. |
| Durable Host capture | SUPPORTED_WITHIN_WINDOWS_X64_STAGE_NAMESPACE | Exact source13/job/identity/one-shot barrier; fixed finite bounds; worker/child held live259 then actual OS0; runner/CLI/short-helper actual0. Auxiliary observer own OS exit UNOBSERVED, exact identity absent/stable complete logs/closed handles. No general portable launcher qualification. |
| Build / HOST_TESTED | PASS; sealed prior execution REVALIDATED | Genuine D03 SDK23/AArch64 bridge23TU/objects1037inputs; D07 Python68/0skip and fresh Host CTest13/13; final D03 did not rerun these. Four new post-review HOSTMODEL admission/runner controls PASS separately; real Win32 job lifecycle only. |
| Historical service / Action / parameters / serialized bag | PARTIAL; historical bounded PASS retained | Original scopes/libraries remain dated evidence; not requalified against the new bridge hash by R38. |
| DDS/RTPS/QoS / discovery / Reliability / fragmentation / history | PRESERVED_ARCHITECTURAL_BOUNDARY; broad ROS2 coverage NOT_VERIFIED | No upper-layer semantic modification. The selected QoS case does not qualify the complete matrix, large messages or concurrent workloads. |
| Full ROS2 / DDS per-payload and wire causal proof | NOT_VERIFIED / BLOCKED_PROVENANCE_GAP | Exact deployed stripped-rclpy source/compiler/link/unstripped lineage unavailable. Native bridge build lineage and public payload observations do not fill that separate gap; no ABI root cause or impossibility is asserted. |
| Longer / hours soak; broad QoS, message sizes, concurrency | NOT_VERIFIED | One finite roughly30-minute run; no new broad matrix, stress or hours-long run. |
| Restart / repeated recovery / Participant recreation | NOT_TESTED_BY_R38 | Historical bounded replacement evidence is separate; no automatic extension to this artifact. |
| Default automatic reconnect | NOT_PROVIDED_BY_CURRENT_CONFIGURATION | Current accepted configuration does not supply it; general feasibility is not assessed. |
| Third observer | NOT_SUPPORTED_CURRENT_TOPOLOGY | Current scope has two Participants and one peer; no general implementation impossibility claim. |
| Loan / recorder-internal take | UNOBSERVABLE_WITH_CURRENT_INSTRUMENTATION / NOT_VERIFIED | No applicable internal observation in R38; historical NO_LOAN mode is not loan support. |
| Selected-client status / feedback ordering | OPEN / NOT_VERIFIED | R38 echo does not test Action status/feedback. |
| SDK epoch / late-callback drain / global leak freedom | NOT_PROVEN | Finite process/resource observations and normal close do not prove those global properties. |
| R22 | STOPPED / EXCLUDED | No silent resumption or extrapolation. |
| Tester / docs / Main PRE_COMMIT / publication / CI | PASS scoped / SYNCED / PENDING / PENDING / NOT_RUN_NO_NEW_COMMIT | Tester730-member seal is technical scope only; Main12 and final complete all/staged graph checks remain. |


---

<!-- R37_BLOCKED_CURRENT_PREFIX; original matrices below are byte-preserved historical snapshots. -->
# Current capability evidence: R37 FAIL / BLOCKED (2026-10-20T03:08:59.634827+00:00)

[R37 native build, exact recorded commands and failed acceptance](ROS2_NATIVE_CLOSE_ACCEPTANCE.md) is current. All R36/R35/R26 “current” matrices below retain their original dated scope and failures. Historical service/Action/parameter/serialized-bag results do not automatically qualify the changed R37 library.

| Dimension | Current R37 qualification | Execution evidence and limit |
| --- | --- | --- |
| Implementation | PRESENT | Exact ordered-batch aggregation in original256 slots, seven fatal buckets, strict close-component reporting and11-source binding. |
| Native build / BUILDS | PASS_BUILD_ONLY; final VALIDATED_REUSE_D01_NOT_NEW_FINAL_SDK_BUILD | SDK23/AArch64;23 TU/1037 inputs/23 objects/72 repo sources; new bridge SHA121a7b6d…;3237 package members,219/220 other ELFs unchanged; no full Fast DDS rebuild. |
| HOST_TESTED | PASS |46 Python methods (18 endurance +10 receipt [5 actual/5 model] +18 R26),0 SKIP; fresh13 C++ CTest cases separately;3 dispatch +9 constructor negatives and21 schema scenarios are distinct controls. |
| DEVICE_TESTED / CROSS_DEVICE_TESTED | true / true; ACTUAL_EXECUTED_FAIL | One real dual-RK3588A run5f28247d2fc3258a; strict complete acceptance and normal close FAIL. |
| Payload component | PASS_PAYLOAD_OBSERVATIONS_ONLY | Independent read-only extraction:1801 roundtrips/7204 full33-byte CDR matches, zero loss/duplicates/reordering; spans A1811.577871855s/B1811.581102355s. Original oracle final=False/True both FAIL on unfiltered full streams. |
| Observer component | Capacity overflow not reproduced in this attempt | Both captured1/faults0/adverts4; selected A31/B27; each observed5130/repeated5126/repeated_batches2563; all7 fatal counters0, coherent/conserved. Does not establish normal close or SDK drain. |
| Native/public close | FAIL | Both OriginalDeleteParticipant0, RuntimeClose2/observation_complete1, public deletion1, child71; CONTROL_MISSING_OR_INVALID; original finish/close control absent. |
| Host supervision | BLOCKED; exit/cause UNKNOWN | Capture stops A385/B385; original BUSINESS_COMMAND RUNNING retained, no invented terminal status. Fixed1910s probe-deadline FAIL remains in both full streams. |
| Process/token recovery | Read-only observations only |11 recorded SDK reads;6 raw files before/local/after SHA match; four owned PIDs absent; token matches1/rc0 both; signals/replay/restart/manual close/cleanup mutation0. |
| INTEGRATION_TESTED / VERIFIED | ACTUAL_LIMITED_DUAL_BOARD_PAYLOAD_PASS_CLOSE_FAIL / false | Required full endurance-and-normal-close acceptance FAIL; overall NOT_VERIFIED. |
| Resource / RTT | Finite application observations only |32 samples/side;23 threads; A RSS82772..82968KiB/B82892..82964KiB; FD A19..20/B20; A-local median2.310876ms,p952.947292ms,p993.455083ms,max5.919959ms. Not leak proof or Transport benchmark. |
| Full ROS2 / strong DDS | NOT_VERIFIED / BLOCKED_PROVENANCE_GAP | New native bridge source/build provenance is established; stripped-rclpy strong DDS payload/wire causal gap remains separate. |
| QoS / sizes / load | PARTIAL / wider scope NOT_VERIFIED | RELIABLE/VOLATILE/KEEP_LAST10,33-byte String, one outstanding, no application retry/dedupe; broad QoS/large messages/concurrency/longer soak not covered. |
| SDK epoch / global callback drain / leak freedom | NOT_PROVEN | Recovery, observation completion and process absence do not establish these properties. |
| Restart / Participant recreation / reconnect | R37 NOT_TESTED / default reconnect NOT_PROVIDED | Historical bounded replacement records retain their own scope. |
| Loan / recorder-internal take / client ordering | UNOBSERVABLE / UNOBSERVABLE / OPEN | R37 adds no corresponding evidence; third observer unsupported with two Participants/one peer. |
| R22 / retained failures | STOPPED/excluded / preserved | R36 FAIL, reviewD01-F1, Main red, fixtureD01/D03 and private environment failures retain original bytes. |
| Documentation / commit / CI | COMPLETED_FOR_BLOCKED_HANDOFF / BLOCKED / NOT_RUN_NO_NEW_COMMIT | Ready for commit:NO; no new commit/push. e27b7e9 is historical R35, not a newly verified remote. |

---

<!-- R36_BLOCKED_CURRENT_PREFIX; entire original R35 matrix below is byte-preserved. -->
# Current capability evidence: R36 blocked handoff (2026-10-20T00:34:50.480663+00:00)

R36 [finite endurance acceptance and exact commands](ROS2_FINITE_ENDURANCE_ACCEPTANCE.md) records one real dual-RK3588A execution, not an accepted finite endurance case. The following table is the current R36 status; the entire R35 table below remains a dated 2026-10-19 historical matrix. Its original bounded PASS claims remain valid for their original cases.

| Dimension | Current R36 qualification | Evidence and limit |
| --- | --- | --- |
| Implementation | PRESENT | Four new probe/runner/oracle/test sources; unchanged since initial testing/review. |
| Native build | BUILD_QUALIFIED_REUSED_NATIVE | 3236 package members/220 unchanged R26 ELFs; no R36 C++ rebuild. |
| HOST | PASS | 52 methods:14 new/18 R26/15 independent/5 Q2 schema (27 controls); synthetic/model controls are separate from device observations. |
| Device / cross-device | DEVICE_EXECUTED_FAIL / CROSS_DEVICE_EXECUTED_FAIL | Run f048d4650806d520; both children exit71, normal native close FAIL. DEVICE_TESTED=true describes execution, not success. |
| Integration / overall | INTEGRATION_NOT_QUALIFIED / NOT_VERIFIED | Content-only1801 roundtrips/7204 full33-byte CDR stage matches PASS; strict native/diagnostic and required close/exit FAIL. |
| Process resources | Finite observations only; leak freedom NOT_PROVEN | 32 samples/side,23 threads,FD A19..20/B20; whole-process runtime/probe costs included. |
| RTT | A-local application echo observation only | 1801 pairs; median2.267125ms,p95 = 2.884 ms, p99 = 3.329667 ms; not wire latency or a Transport benchmark. |
| Full ROS2 / strong DDS | NOT_VERIFIED / BLOCKED_PROVENANCE_GAP | No per-payload native-to-DDS/wire causal proof; existing provenance gap remains. |
| Loan / recorder-internal take | UNOBSERVABLE | R36 does not add loan or recorder-internal observations. |
| QoS / message / load coverage | PARTIAL / broader coverage NOT_VERIFIED | Selected RELIABLE/VOLATILE/KEEP_LAST10;33-byte String,single outstanding,no application retry/dedupe; large messages/concurrency/longer soak not covered. |
| Restart / default automatic reconnect | R36 NOT_TESTED / NOT_PROVIDED by accepted configuration | Historical guarded replacements retain their original evidence; R36 does not qualify automatic recovery. |
| SDK epoch / global callback drain | NOT_PROVEN | Process absence/token restoration do not prove SDK-wide guarantees. |
| Topology | Two Participants/one peer | Third observer unsupported by current configuration. |
| R35 serialized bag | Historical bounded PASS unchanged | RECORD995d57d79617477a/DEFAULTf1037096c37e5b3b/NO_LOAN128ed56c0766fce3; independent original scopes. |
| R22 | STOPPED / excluded | Protected user lifecycle/QoS work is outside this task. |
| Publication / commit | BLOCKED; Ready for commit:NO | No R36 new commit/push; CI NOT_RUN_NO_NEW_COMMIT. Baseline e27b7e9 is historical R35, not fresh remote verification. |

Both RuntimeClose returns are2 and both public deletion returns1; the additional resource-complete-with-runtime-error/failed-delete diagnostics remain failures, outside the old optional unique-flow diagnostic pair. Current-source advert-capacity analysis gives a sufficient failure-condition inference, not a confirmed sole root cause or a source-to-reused-ELF build binding. A separate native observation/close review/build and complete dual-board retest are required. No post-failure source edit/device rerun or cap/timeout/assertion/return-code workaround occurred.

---

<!-- HISTORICAL R35 MATRIX: original full body follows without byte changes. -->

# ROS 2 capability matrix: bounded evidence and open gaps

Snapshot 2026-10-19T22:45:38.910915+00:00; R35 source-history baseline `f38f184de9fa8da099fa0d79ffccf00ffe8a5dfe`. **Full ROS2 NOT_VERIFIED; no completion percentage.** The [R35 acceptance](ROS2_SERIALIZED_BAG_ACCEPTANCE.md) adds only three bounded serialized-bag cases. Selected native RMW remains unchanged; the existing rosbag2_py executor delta means the entire binding is not byte-stock. Build qualification is REUSED_HASH_VERIFIED_NOT_REBUILT. Current documentation follows the final independent review and final technical tester; publication evidence belongs to Main's separate records.

| Capability | Qualification | Evidence and practical limit |
| --- | --- | --- |
| Native pub/sub | Bounded SUPPORTED; general coverage PARTIAL | [R26](ROS2_SUSTAINED_ROUNDTRIP_ACCEPTANCE.md):200 String echo roundtrips/about21.267s, not arbitrary messages/large-message/concurrent stress/hours-day soak. |
| Native service | PARTIAL | [R29](ROS2_SERVICE_RESTART_ACCEPTANCE.md): finite AddTwoInts PRE/NEW and guarded normal server replacement; OLD local disposal is not remote cancellation. EOF equivalence was not a new device run. |
| Native Action | PARTIAL | [C++](ROS2_NATIVE_ACTION_ACCEPTANCE.md) selected feedback/cancel/reject/abort/concurrent cases; [R31](ROS2_NATURAL_ACTION_ACCEPTANCE.md) fixed natural AB/BA; [R32](ROS2_NATURAL_ACTION_RESTART_ACCEPTANCE.md) PRE/NEW after guarded replacement. OLD completed before disappearance, not recovered. |
| Native parameter services/events | Functional V2 bounded DEVICE/CROSS_DEVICE PASS; general coverage PARTIAL | [R33 V2](ROS2_NATIVE_PARAMETER_ACCEPTANCE.md):14 ordered RPCs,nine types,3 atomic refusals/readbacks,12 events,40 full-byte native local roundtrips,normal close. No-extra-event window250ms; GetTypeDescription RPC not tested. V1 device1/device2 remain FAIL. |
| Standalone serialization / serialized API / serialized publication | PARTIAL bounded observation; OPEN_COMPATIBILITY / general use NOT_VERIFIED | [R34 stage1](ROS2_SERIALIZED_IMPACT_ACCEPTANCE.md):8 typed/bytes publish calls,16 typed/raw callbacks,16 full buffers; ParameterEvent local148/typed raw140/bytes raw148,full bytes match on serialized lanes. All fields/native decode agree. R33 retains22/40 suffix samples,11 nonzero. No stock-RMW repair,canonical length/determinism or general compatibility proof. |
| Native SQLite rosbag | PARTIAL; R35 bounded DEVICE/CROSS_DEVICE PASS for RECORD/DEFAULT/NO_LOAN | [R35](ROS2_SERIALIZED_BAG_ACCEPTANCE.md): four topics/two fixed types/eight stored CDR rows; each play16 typed/raw callbacks/eight full-buffer matches/one actual Resume. Historical String10 PASS stays separate. R34 bag NOT_TESTED remains historical; original third-observer/recorder-internal goal remains BLOCKED. |
| Rosbag loan-path comparison | Two fixed modes PASS; effective capability UNOBSERVABLE | DEFAULT has no disable-loan flag; NO_LOAN has exactly one --disable-loan-message in actual argv. This does not establish whether the middleware provided loaned messages or which internal take path ran. |
| QoS combinations | PARTIAL; full matrix UNVERIFIED | Selected RELIABLE/VOLATILE and historical small BEST_EFFORT only. Current transient-local/deadline/lifespan/liveliness/history combinations and broad interactions unaccepted. Protected QoS dirt is not qualification evidence. |
| Repeated normal startup/replacement | PARTIAL, finite guarded | [R27](ROS2_PROCESS_RESTART_ACCEPTANCE.md)/[R28](ROS2_REPEATED_RESTART_ACCEPTANCE.md) one/two B replacements with live A; service/Action separately bounded. Not arbitrary in-process Participant lifecycle/failure recovery. |
| Default automatic reconnect | NOT_PROVIDED by accepted configuration | Explicit opt-in guarded replacement retained; neither a default feature nor a claim of permanent technical impossibility. |
| Long duration / leak freedom | UNVERIFIED / NOT_PROVEN | Short resource samples, no hours/day soak or leak-freedom proof. |
| Strict unique network flow | NOT_PROVIDED / strict UNTESTED | Optional attempt failures and original-QoS custom-Transport fallback retained. |
| SDK global drain / epochs | UNPROVEN | Observed callbacks,PID absence and cleanup do not establish global SDK guarantees. |
| Selected rclpy status/feedback ordering | OPEN_UNCOORDINATED_NOT_FIXED | Status-before-handle registration and result-time feedback callback removal persist; fixed-scene success does not close arbitrary-schedule gaps. |
| Direct application-to-RTPS / strong DDS payload boundary / dedicated SEDP | NOT_ESTABLISHED / BLOCKED_PROVENANCE_GAP / NOT_QUALIFIED | R34 measures native raw callback bytes without reserialization. Stripped rclpy extension lacks complete source/build binding; structural native/RTPS evidence does not identify DDS serializedPayload/wire boundaries or per-message mapping. |
| R22 lane | STOPPED / excluded | User lifecycle/QoS edits protected; no acceptance claim added. |
| Recorder internal take | UNOBSERVABLE | Separate raw subscriptions and stored/playback buffers cannot attest recorder-internal input; original simultaneous observer proposal remains blocked. |

R35 stores/replays the complete original CDR bytes: serialized ParameterEvent148/consumed140/suffix8 with three nonzero bytes per sample; String40/suffix0. This is bounded mode/type/sample fidelity, not canonical/minimal padding, deterministic standalone serialization or a safe-RMW-repair conclusion. COMPAT_RMW_FASTRTPS_STANDALONE_LENGTH and general compatibility remain OPEN. R33 V1 failures, R34 query1 FAIL/initial auxiliary PARTIAL/times UNKNOWN, prior C2 RECORD failure and DEFAULT D01 predeployment expiry FAIL/no CONSUMED remain retained. The passing DEFAULT D02 does not promote D01 to PASS.

The bridge qualifies two Participants/one peer; its current configuration does not support a third observer. R35 uses separate record and play phases and does not observe recorder-internal take. Default automatic reconnect is NOT_PROVIDED by this accepted configuration, not declared permanently impossible. Broad QoS/transient-local/deadline/lifespan/liveliness/history combinations, large messages, concurrency, hours/day soak and leak freedom remain UNVERIFIED. SDK epoch/global drain are NOT_PROVEN; unique-flow NOT_PROVIDED/strict UNTESTED and selected rclpy feedback/status ordering OPEN remain.

Strong DDS stays BLOCKED_PROVENANCE_GAP: the stripped rclpy extension (hash prefix997bb) lacks exact source/compiler/link/unstripped lineage; the referenced e4c7 revision is not available as a local Git object and does not bind that installed extension. This is a provenance gap, not an asserted ABI root cause. R35 adds no DDS/RTPS/Writer/Reader/discovery/QoS semantic or Transport-architecture change and uses DSoftBus Socket API only. R22/user lifecycle-QoS work remains STOPPED/excluded.
