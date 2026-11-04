<!-- R38_CURRENT_SCOPED_PASS_PREFIX; the complete prior body below remains byte-preserved historical evidence. -->
# Current follow-up: R38 finite endurance and normal close PASS (2026-10-20)

The new [R38 durable capture acceptance](ROS2_DURABLE_CAPTURE_ACCEPTANCE.md), run `940f4e9c10a29830`, has independent DEVICE / CROSS_DEVICE / INTEGRATION PASS for1801 String roundtrips/7204 complete33-byte CDR stage records, zero loss/duplicates/reordering/foreign payload, both original oracle modes PASS, and spans A1811.239383904/B1811.244691655sec. Both native RuntimeClose0/observation_complete1/public deletion0/interpreter0/device-child0, token restoration/four owned device PIDs absent/zero forces PASS. SDK receipts are696 host0/680 applicable remote0, plus a separate fresh40-SDK query and4 local steps.

Host worker/child actual OS exit0 is observed through exact objects retained live at startup; runner/original CLI Popen and external short-helper exits0 agree. Observer own OS exit stays **UNOBSERVED_NONBUSINESS_AUXILIARY**, with final exact identity absence, complete bounded stable output and closed handles. No long business wrapper was created; old tool24208 exit UNKNOWN and old R38 `efa7336e7ca4b78b` BLOCKED remain unchanged.

R36 and R37 original failures remain FAIL; this is a separately admitted new run of the unchanged1801/at-most1Hz/minspan1800 finite contract. D07's sealed68 Python/0skip and13 Host CTest PASS and the genuine D03 SDK23 native23TU/1037inputs/23objects build are revalidated by final tester D03, not newly rerun/recompiled there;4 post-review HOST controls are separate model-admission/runner evidence. Full ROS2, broad QoS/sizes/concurrency/hours soak, strong per-payload wire lineage, SDK epoch/drain/global leak freedom and R38 restart/recreation remain unverified. Tester Commit gate:PASS is technical scope only; docs synced, Main PRE_COMMIT/all+staged graph/commit/push PENDING, CI NOT_RUN_NO_NEW_COMMIT.

The entire R36 finite endurance record and all earlier prefixes below remain byte-preserved historical evidence, including their original failures and dated “current” statements. [Current capability matrix](ROS2_CAPABILITY_MATRIX.md).

---

<!-- R37_BLOCKED_CURRENT_PREFIX; entire original R36 failed acceptance below is byte-preserved. -->
# Current R37 follow-up: FAIL / BLOCKED (2026-10-20T03:08:59.634827+00:00)

The current [R37 native observation/close acceptance](ROS2_NATIVE_CLOSE_ACCEPTANCE.md) records the source-bound SDK23 bridge repair/build and one real execution of the original finite1801/1Hz/minimum1800s case, run5f28247d2fc3258a. **Implementation PRESENT; native BUILDS; HOST_TESTED PASS (46 Python methods/0 SKIP; fresh13 C++ cases separately); DEVICE_TESTED/CROSS_DEVICE_TESTED=true with failed acceptance; INTEGRATION_NOT_QUALIFIED; VERIFIED=false; Commit gate: BLOCKED; Ready for commit: NO.** Final native qualification is validated D01 build reuse, not a new final SDK build or full Fast DDS rebuild.

Independent read-only extraction finds1801 roundtrips/7204 matching full33-byte CDR records, zero loss/duplicates/reordering, spans A1811.577871855s/B1811.581102355s. Both observers captured1/faults0/adverts4 and all7 fatal counters0, so this attempt did not reproduce R36's capacity overflow. The original complete streams still contain fixed1910s probe-deadline FAIL; original oracle final=False/True and strict native/diagnostic checks all FAIL. Both OriginalDeleteParticipant0/RuntimeClose2/observation_complete1/public deletion1/child71; missing original PROBE_FINISH/normal-close control and CONTROL_MISSING_OR_INVALID remain failures. Host capture stopped at385; original host exit and cause UNKNOWN, BUSINESS_COMMAND RUNNING bytes retained. Four owned PIDs absent/token restored/zero forced cleanup do not establish accepted close.

Full ROS2 NOT_VERIFIED and prior provenance/QoS/large-message/concurrency/SDK-drain/leak/reconnect gaps remain. No new commit/push; CI NOT_RUN_NO_NEW_COMMIT. This documentation is COMPLETED_FOR_BLOCKED_HANDOFF only. The entire R36 record below, including its original failures, build limits and earlier “current” wording, is a historical snapshot; R37 does not rewrite it. [Current matrix](ROS2_CAPABILITY_MATRIX.md).

---

# R36 finite String endurance acceptance: FAIL / BLOCKED

Evidence snapshot: 2026-10-20T00:34:50.480663+00:00. **Implementation PRESENT; HOST PASS; DEVICE_TESTED=true and cross_device_tested=true with FAIL; INTEGRATION_NOT_QUALIFIED; Overall NOT_VERIFIED.** This is a blocked-handoff record. The final independent tester reports `status=FAIL`, `Commit gate: BLOCKED`; documentation synchronization is COMPLETE_FOR_BLOCKED_HANDOFF and **Ready for commit: NO**. No R36 commit/push exists; CI is NOT_RUN_NO_NEW_COMMIT.

## Exact scope and implementation

Case `endurance_1801_ABA_30MIN`, run `f048d4650806d520`, is ordinary public Node pub/sub String echo on two real OpenHarmony RK3588A boards. A sends one numbered request, B receives and echoes it, A receives before sending the next request. This uses `std_msgs/msg/String`,33-byte CDR, selected RELIABLE/VOLATILE/KEEP_LAST depth10, two Participants/one peer, exactly one outstanding request and no application retry/deduplication. It is application echo; service/Action/restart/Participant recreation were NOT_TESTED by this case.

| Repository source | Exact tested SHA-256 |
| --- | --- |
| [integrations/ros2/target/endurance_roundtrip_probe.py](../integrations/ros2/target/endurance_roundtrip_probe.py) | `f97f933309bb716560d3c58bd31fb08183767130b032e528d5558e37878b5da3` |
| [tests/ros2_target/run_endurance_roundtrip.py](../tests/ros2_target/run_endurance_roundtrip.py) | `cc656cabf349a16f26b14563f63c28b2744cfd71138d5d9e96d8120954a33556` |
| [tests/ros2_target/endurance_roundtrip_oracle.py](../tests/ros2_target/endurance_roundtrip_oracle.py) | `4ce51467e0d779d2eb0c055a353b67dad2d52b1a28e0cbdbc4f77cbbb68c4811` |
| [tests/ros2_target/test_endurance_roundtrip.py](../tests/ros2_target/test_endurance_roundtrip.py) | `60fc3b63b9d2a2ce64a2238fa1150c1e822f30c0de04fca843e7b620dfe28bcd` |

All four sources have remained byte-identical since initial testing, independent review and final testing. Existing R26 sources and35s/40s/50s limits remain unchanged. No DDS/RTPS/Writer/Reader/discovery/QoS semantic or Transport abstraction change was made. The selected backend retains DSoftBus Socket API; no Session API, provisioning, permission or SDK change was introduced.

## Environment and native artifact boundary

| Item | Recorded environment |
| --- | --- |
| A | RK3588A OpenHarmony; HDC `3e01ff55454d202020104033bf453b00` |
| B | RK3588A OpenHarmony; HDC `3e01ff55454d202020104433991c3b00` |
| Target | SDK23/AArch64, project-linked CPython3.12.7/ROS2 Jazzy components/rmw_fastrtps_cpp/domain201 |
| Deployment | `/data/local/tmp/mdds_dds_f048d4650806d520_A/package` and `_B/package`; pinned `run-token-first` and `actionbag_python` |
| Package | Root-relative `build/r36-bounded-endurance/package-527b209a8be429f1/package`;3236 members/220 native ELFs |
| Parent | Accepted R26 `build/r26-sustained-restart/package-4bd68cc7db3b7d31/DERIVATION.json`; SHA `f50221b60a304fb823539909ecb2ab9aa30493013a7ede9e5d0a318149580532` |
| R36 derivation | `build/r36-bounded-endurance/package-527b209a8be429f1/DERIVATION.json`; SHA `9bb64dacaa3c9f5703a6c54d0d75d5217b0961df589f1c5ab56b598aaedc43fa` |
| Native qualification | BUILD_QUALIFIED_REUSED_NATIVE_NOT_REBUILT_FOR_R36; zero native-file changes; added endurance probe and updated package inventory only |

The native C++ was not rebuilt for R36. Reused bytes qualify artifact identity, not current-source reproducibility or a new full build. The final tester did not recompose the package. The isolated checkout HEAD is historical R35 `e27b7e9d1be7e72944108655a62f9753bfa99448` (detached), not an R36 commit or a fresh verification of the remote. Public documentation records HDC target IDs only; private UDIDs/network identity/payload/configuration/credentials stay in local evidence.

## Fixed independent timing contract

| Guard | Fixed value / meaning |
| --- | --- |
| Work |1801 requests/echoes; at most1Hz; minimum first-to-last send span1800s |
| Listener |60s listener readiness bound |
| Business |1900s absolute deadline |
| Probe |1910s local bound;5s no-progress guard |
| Normal close |1950s absolute deadline from stored business_origin; no reset after business |
| Exit |20s owned child exit bound |
| Host observation |15s polling; resources at received0/every60/final1801 |
| Logs |4MiB (4194304 bytes) per stream cap |

`execution-d167d58f041d66a6/CLOCK.json` records origin `BEFORE_FIRST_LISTENER_RELEASE`, business_origin948669.828, business_deadline950569.828, normal_close_release_deadline950619.828. The same origin is used for the business and absolute close bounds. These R36 constants do not enlarge historical R26's35s business/40s probe/50s close guards. Outer process capture timeout2350s is separate from these device/application deadlines; it did not trigger.

## Stage gates, query attempts and host evidence

Initial tester executed syntax/import,14 new HOST tests,18 R26 regression tests and15 independent boundary methods:47 PASS. Independent agentic-review (risk tier2) found zero actionable findings and made zero source fixes; five additional review controls passed. The initial graph limitations remained explicitly UNKNOWN where no callers resolved; text confirmation did not reclassify them as LOW. Final tester reran47 HOST methods after review and executed5 Q2 contract methods containing27 individual controls:52 methods PASS. These HOST controls do not substitute for device evidence.

Q1, root-relative `build/r25-native-actionbag/r36-endurance-24978de86e3072dc-preparation`, had40 successful native query calls/frozen audit PASS but its local converter exited1 (`KeyError run_id`). Its newly produced review metadata lacked `run_id`, `assessment`, `peer_plan_sha256`, `supplementary_commands_sha256` required by the current consumer. It remains FAIL at local conversion; all sealed original bytes are retained. No business ran from Q1. This is an evidence-producer/consumer schema mismatch, not a device permission or RTPS failure.

Q2 used a distinct fresh preparation `build/r25-native-actionbag/r36-endurance-4d1dc2bd86707997-preparation`, query run `da074c997a87d74e`. Its independently reviewed metadata has the complete current11-field contract. Eight independent reviewer controls and the final tester's27 controls passed. Its actual40 query calls and full local convert/validate/routes/prepare4-step continuation passed. Full converter validation is separately retained in `final-tester-d01/Q2_FULL_CONVERTER_RESULT.json`; the schema-only result does not itself prove full conversion. Business consumed the fresh query at age68.903s, under240s. Q2 neither modifies nor replays Q1 and does not retrospectively mark Q1 PASS. Main D02 query release and separate business release/GO each admitted only their own exact attempt; the business authorization was consumed once.

Initial compose and final checked-package each retained an original sandbox WinError5/exit1. The identical ordinary-user file-read/copy/check retry succeeded with no ACL change, native replacement or new device permission. These environment failures remain recorded. A first host-only failed-device audit also exited1 when unchanged strict diagnostic parsing rejected the new failed-delete stderr; its later host audit records that rejection as a FAIL component and preserves the original attempt. It does not turn the device result into PASS.

All stage receipts, actual argv/cwd/environment/UTC/exit codes and raw streams remain local under root-relative `build/r36-bounded-endurance/initial-tester-d01`, `reviewer-d01`, `reviewer-query-d02`, `final-tester-d01`. The final tester's `COMMANDS.json` is the exact host-command ledger, including original failures and the inline syntax/full-converter invocations. The final HOST commands below all used cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r36-bounded-endurance\publish-worktree`; their complete environment and timestamps are in that ledger.

`host_endurance_14`: exit 0. Outputs: root-relative `build/r36-bounded-endurance/final-tester-d01/host_endurance_14.stdout` and `host_endurance_14.stderr`.

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r36-bounded-endurance\\publish-worktree\\tests\\ros2_target\\test_endurance_roundtrip.py"
]
```

`host_r26_regression_18`: exit 0. Outputs: root-relative `build/r36-bounded-endurance/final-tester-d01/host_r26_regression_18.stdout` and `host_r26_regression_18.stderr`.

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r36-bounded-endurance\\publish-worktree\\tests\\ros2_target\\test_sustained_roundtrip.py"
]
```

`independent_controls_15`: exit 0. Outputs: root-relative `build/r36-bounded-endurance/final-tester-d01/independent_controls_15.stdout` and `independent_controls_15.stderr`.

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r36-bounded-endurance\\initial-tester-d01\\controls.py"
]
```

`q2_current_review_contract`: exit 0. Outputs: root-relative `build/r36-bounded-endurance/final-tester-d01/q2_current_review_contract.stdout` and `q2_current_review_contract.stderr`.

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r36-bounded-endurance\\final-tester-d01\\q2_review_contract.py"
]
```

## Actual device dispatch and outcome

One actual business command ran from UTC `2026-10-19T23:48:29.680644+00:00` to `2026-10-20T00:19:52.192689+00:00`, outer exit **1**, elapsed1882.5s, outer_timeout=false. Exact argv (historical consumed release; not replayable authorization):

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r36-bounded-endurance\\publish-worktree\\tests\\ros2_target\\run_endurance_roundtrip.py",
  "run",
  "--prepared",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r36-bounded-endurance\\prepared-23633ef5be47ab73\\PLAN.json",
  "--release",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r36-bounded-endurance\\MAIN_BUSINESS_RELEASE_D02.json",
  "--hdc",
  "C:\\Users\\17715\\AppData\\Local\\OpenHarmony\\Sdk\\23\\toolchains\\hdc.exe",
  "--output",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r36-bounded-endurance\\execution-d167d58f041d66a6"
]
```

Exact environment overrides:

```json
{
  "PYTHONDONTWRITEBYTECODE": "1",
  "PYTHONUTF8": "1",
  "TMP": "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r36-bounded-endurance\\final-tester-d01\\tmp",
  "TEMP": "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r36-bounded-endurance\\final-tester-d01\\tmp",
  "TMPDIR": "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r36-bounded-endurance\\final-tester-d01\\tmp",
  "GIT_OPTIONAL_LOCKS": "0"
}
```

Output root is root-relative `build/r36-bounded-endurance/execution-d167d58f041d66a6`. `final-tester-d01/BUSINESS_COMMAND.json` binds command, cwd, prepared/release/runner hashes, UTC/rc and streams; `BUSINESS_TOOL_RECEIPT.json` records terminal tool status. Host capture is `final-tester-d01/business.stdout` (31489 bytes) and `business.stderr` (0 bytes). The host stderr size does not describe either device stderr stream.

The executed699 runner commands retain argv/cwd/time/rc and raw stdout/stderr under the execution directory's `commands/`;2948 original execution files are preserved. There are683 zero remote status markers, not an assertion that all raw output is error-free. Per-side `A.raw.stdout.log`, `A.raw.stderr.log`, `A.raw.exit.log` and corresponding B streams remain unchanged. Provider/maps observations and immutable package identity are separate from the failed strict native qualification. The original runner stops at A's exit71; final independent audit also checks B's exit71 rather than treating the first failure as the entire result.

## Content-only component and finite observations

Independent oracle/content reparse is **PASS_CONTENT_ONLY**:1801 sequences,7204 payload events; A-send/B-receive/B-echo/A-receive have byte-identical33-byte CDR at each sequence. Application payload loss, duplication, foreign/out-of-order values and application payload errors are0. This zero is restricted to payload accounting; native/teardown/diagnostic errors are present.

| Measurement | A | B |
| --- | --- | --- |
| Local payload span(s) |1811.524594267 |1811.528684017 |
| Actual resource samples |32 |32 |
| Initial/final RSS(KiB) |82724 /82928 |82896 /82972 |
| Initial/final HWM(KiB) |82724 /82928 |82900 /82972 |
| Threads(range) |23..23 |23..23 |
| FD(range) |19..20 |20..20 |
| Post-warmup RSS slope(KiB/min) |0.34377727997945684 |0.12133251447637902 |

The fit excludes only received0;31 samples per side remain, starting at received60. Resources describe the whole process, including runtime and probe. RSS/HWM ranges, samples and fit arithmetic are preserved in `final-tester-d01/DEVICE_AUDIT_RESULT.json` and `INDEPENDENT_CONTENT_RESOURCE_AUDIT.json`. A positive small finite slope or PID disappearance cannot prove leak freedom: **resource_leak_freedom=NOT_PROVEN**.

A-local logged application echo RTT over1801 pairs: min1.256792ms,median2.267125ms,p95 = 2.884 ms, p99 = 3.329667 ms,max6.268792ms. These are send-to-echo observations on A's local clock, including application/runtime scheduling; they are not wire latency, a Transport benchmark or a cross-device monotonic-clock subtraction. The first-to-last spans use each process's own clock.

## Required normal close and diagnostics: FAIL

| Required observation | A | B |
| --- | --- | --- |
| RuntimeClose first failure |seq86/call85,INTEGRATION_CLOSE_RESULT2,CLOSE_RETURN |seq86/call85,INTEGRATION_CLOSE_RESULT2,CLOSE_RETURN |
| Factory_delete_participant |seq87/call83,PUBLIC_RETURN_CODE1,POST_DELETE_MAPPING |seq87/call83,PUBLIC_RETURN_CODE1,POST_DELETE_MAPPING |
| Python finalize / teardown |python_rc0 /teardown_rc1 |python_rc0 /teardown_rc1 |
| Owned child exit |71 (required0) |71 (required0) |
| Runtime-close release elapsed(s) |1830.0310000000754 |1830.4220000000205 |
| Token restored |1 |1 |

Both original device stderr streams contain `FDDST_CLOSE_RESOURCE_COMPLETE_WITH_RUNTIME_ERROR` and `Failed to delete participant`. The original unmodified strict native-capture parser and strict diagnostics both reject the run. These errors are additional to the historical optional unique-flow Reader diagnostic pair; they must not be classified as that pair or removed from “all raw errors” accounting. `strict_native_qualification=FAIL`, `strict_diagnostics=FAIL_ADDITIONAL_RUNTIME_STDERR`, `normal_close=FAIL`, integration result `FAIL_REQUIRED_CLOSE_EXIT`, `communication_verified=false`.

All four owned child/supervisor PIDs were observed absent, forced signals=0 and both tokens restored1; cleanup directories/evidence are retained. This proves those cleanup observations only and does not change native normal close or required child exit to PASS. Exact raw context/line locations are retained in `final-tester-d01/A.TEARDOWN_RAW_CONTEXT.txt`, `B.TEARDOWN_RAW_CONTEXT.txt`, `TEARDOWN_RAW_CONTEXT_LOCATIONS.json`; strict rejections are in `STRICT_NATIVE_REPARSE.json` and `DIAGNOSTICS_REPARSE.json`.

## Read-only current-source inference and next required work

Main's preserved `CLOSE_*` graph/context/query records and `MAIN_BLOCKED_ADMISSION_D01.json` trace the current source. [NativeObservation.hpp](../integrations/ros2/target/NativeObservation.hpp) has a256-entry advert array; [NativeObservation.cpp](../integrations/ros2/target/NativeObservation.cpp) increments faults on capacity exhaustion and Flush requires `!locators.faults`; [TargetBridge.cpp](../integrations/ros2/target/TargetBridge.cpp) maps unsuccessful Flush to ABI2. Actual `ROS_V1_OBSERVATION` rows have captured1/adverts256/faults2322 on both boards, selected31(A)/27(B); native summary senderror/overflow/malformed counts are0.

Those rows meet a **sufficient failure condition in the current source**. This is a host-source inference, not proof of the unique root cause. R36 did not compile this C++; its reused native ELF's precise source/build binding remains separate. `CloseLocal.failed` was not independently emitted, so a concurrent close failure cannot be ruled out. Neither zero native summary senderror/overflow/malformed nor successful content permits treating observation faults or close failure as harmless.

The next stage must independently review source-bound native observation capacity/close contracts and a new build, retain split reasons/build provenance, and rerun the original complete dual-board acceptance with initial tests/review/final tests. No silent cap increase, changed return mapping, removed assertion, deadline extension or error filtering is an accepted remedy. After this failure no device rerun, source/native edit, new build, SDK call or permission change occurred; the failure and9-source snapshot are frozen.

## Evidence, scope limits and blocked delivery

Canonical root-relative local evidence:

- `build/r36-bounded-endurance/final-tester-d01/RESULT.json`: SHA `375d9f43c915d289df155ce9cf7c6789e1646ba623bf950c662c0318584c6a4f`.
- `build/r36-bounded-endurance/final-tester-d01/DELIVERY.json`: SHA `aeab0de3b659e36afe91cf9b006705383c07a322a9f64e7c7935f3746f284686`.
- `build/r36-bounded-endurance/final-tester-d01/SEAL.json`: SHA `fe9637ed2509607917db582f4ef2e188bc314536c6edd3faf9e62b856e9777cb`;219 private members plus2948 external raw execution files were checked by Main.
- `build/r36-bounded-endurance/MAIN_BLOCKED_ADMISSION_D01.json`: Main's host-only failure admission; status BLOCKED, no replay.
- `build/r36-bounded-endurance/failed-source-snapshot-d01/SOURCE_MANIFEST.json`:9 frozen source hashes; manifest SHA `00ab2491f7b72c0087816132b884ca7a48003524651a8501fe870d140a80d5b8`.
- `build/r36-bounded-endurance/docs-blocked-d01`: this documentation's original-body snapshots/hash proofs, seven-document inventory, protected-tree comparisons, RESULT/REPORT/DELIVERY/SEAL; blocked handoff only.

These paths are relative to the original repository root; the independent checkout is nested under that root. Local private evidence is not an uploaded deliverable. No UDID/private payload/credential is copied into public documentation.

Full ROS2 remains NOT_VERIFIED; strong DDS serializedPayload/wire and per-payload causal proof remain BLOCKED_PROVENANCE_GAP; loan/recorder-internal take UNOBSERVABLE; SDK epoch/global callback drain NOT_PROVEN. General serialized compatibility and selected rclpy status/feedback ordering remain OPEN. Large messages, broad QoS, concurrency, longer soak and leak freedom remain unverified. Default automatic reconnect is NOT_PROVIDED by the accepted configuration; a third observer is unsupported by the current two-Participant/one-peer configuration. R22 stays STOPPED/excluded.

[R26's200-roundtrip bounded PASS](ROS2_SUSTAINED_ROUNDTRIP_ACCEPTANCE.md) and [R35's three bounded bag PASS cases](ROS2_SERIALIZED_BAG_ACCEPTANCE.md) retain their historical scope. R36 adds a failed finite endurance attempt, not a downgrade of those independent historical results or an endurance acceptance. [Current capability matrix](ROS2_CAPABILITY_MATRIX.md).

The required final device gate and PRE_COMMIT are **BLOCKED**, so no normal implementation commit is permitted. Documentation is synchronized for the blocked handoff only: **Ready for commit:NO**. No new R36 commit/push/CI is claimed; CI NOT_RUN_NO_NEW_COMMIT. No future SHA, PASS or remote result is prefilled.
