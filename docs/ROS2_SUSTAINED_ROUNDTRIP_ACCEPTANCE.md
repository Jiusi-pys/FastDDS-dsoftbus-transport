# R26 bounded sustained String roundtrip acceptance

> **Current S1/S2 navigation (2026-10-18 13:52 UTC):** S1 remains published as `9cdbc84ed1371d53f4ee53b88dad5b3e5f1612ab`, CI NOT_RUN at its recorded exact-SHA check. The newly authorized [R27 S2 experiment](ROS2_PROCESS_RESTART_ACCEPTANCE.md) now has independent DEVICE/INTEGRATION PASS for one explicit ON normal B process replacement with A remaining alive (PRE5/POST5). Tester gate PASS; Main PRE_COMMIT and R27 publication pending. Default automatic reconnect, repeated/long-duration recovery and SDK epoch/drain remain unqualified. Earlier blocked snapshots below remain historical evidence, not the current S2 verdict.

> **Publication and subsequent stage update (2026-10-18):** This S1 acceptance was published as `9cdbc84ed1371d53f4ee53b88dad5b3e5f1612ab` on existing `origin/codex/flow-encoding-published-20261014`; exact-SHA CI at 11:45:04.427145 UTC was NOT_RUN. The separate [R27 process-restart candidate](ROS2_PROCESS_RESTART_ACCEPTANCE.md) is implemented/built/host-tested but its two real windows are BLOCKED, before accepted PRE/restart ARM/B1 launch. It remains uncommitted; device-dependent actions and normal commit are paused. S1's bounded PASS is unchanged and does not establish restart. The original snapshot below is retained.

Evidence snapshot: 2026-10-18 11:35 UTC. **S1 DEVICE_TESTED and
INTEGRATION_TESTED PASS for 200 application pub/sub roundtrips on two RK3588A
boards. Overall ROS 2 capability remains NOT VERIFIED.** Independent final
tester Commit gate PASS covers four source files only; Main documentation/graph/
PRE_COMMIT and publication gates remain separate. [Current matrix](STATUS.md).

## What actually passed

Run `f31ef86f8ed07cf2` uses one ordinary public Node in each process. A publishes
one numbered String, B receives and echoes it through a publisher, and A receives
the echo before sending the next. This is application pub/sub echo, **not a ROS
service/action**. There are 200 roundtrips, 400 logical publications and 800
side-local payload records (A send, B receive, B echo, A receive). Each sequence's
four 33-byte CDR values is byte-identical; independent decoding confirmed String
content and ordering. No application retry, deduplication or parallel outstanding
request was used. Selected QoS is RELIABLE, VOLATILE, KEEP_LAST depth 10.

The fresh paired query recorded 40 successful commands. Execution recorded 404
successful commands, including 388 zero remote status markers. Both child exit
codes were 0; all four owned child/supervisor PIDs were absent, both identity
tokens were restored, and no signal was sent. Actual native captures/provider
maps bind the selected DSoftBus Socket path; host fixtures are separate evidence.

| Measurement | A | B | Interpretation |
| --- | --- | --- | --- |
| Payload span | 21.267501 s | 21.265880 s | Local clock observations; no cross-board monotonic comparison. |
| Quiet observation | 2.738559 s | 2.740795 s | Exceeds the fixed 2 s post-completion quiet interval. |
| RSS at received 0 / 200 | 82,852 / 82,880 KiB | 82,832 / 82,872 KiB | Growth +28 / +40 KiB over this bounded window; leak freedom NOT_PROVEN. |
| Threads | 24 throughout | 23 throughout | Five samples per side, at 0/50/100/150/200 received. |
| File descriptors | 20 throughout | 20 throughout | Same five samples; not a long-duration stress test. |
| Runtime-close release elapsed | 34.547 s | 35.000 s | Both below the unchanged 50 s normal-close release deadline. |

A's minimum send interval was 100.766 ms (maximum 112.405 ms), consistent with
the maximum 10 Hz request rate. The business acceptance upper bound is 28.438 s,
below the unchanged 35 s deadline. The exact BUSINESS write timestamp was not
stored: validation preceded business maps and the first normal finish, so
28.438 s is a constrained upper bound, not an exact business duration. The probe
bound remains 40 s. These values do not imply a throughput/latency benchmark.

## Environment, build and execution

| Item | Actual selected environment |
| --- | --- |
| Board A | RK3588A OpenHarmony, HDC `3e01ff55454d202020104033bf453b00` |
| Board B | RK3588A OpenHarmony, HDC `3e01ff55454d202020104433991c3b00` |
| Target | SDK23/AArch64; project-linked CPython 3.12.7; ROS 2 Jazzy components; `rmw_fastrtps_cpp`; domain 201 |
| Identity | Existing `mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16`, A/B tokens 672006291 / 672042405 |
| Deployment | `/data/local/tmp/mdds_dds_f31ef86f8ed07cf2_A` and `_B`; pinned `run-token-first` and `actionbag_python` |
| Host | Windows; `C:/pixi_ws/.pixi/envs/default/python.exe`; SDK23 `hdc.exe`; workspace `C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport` |
| Package | `build/r26-sustained-restart/package-4bd68cc7db3b7d31/package`, 3,235 files; adds the probe script to the accepted runtime, changes zero native payloads |

The package reuses the accepted rosbag native artifact with parent runtime
receipt `17bc0f79af03c3d8a3b9dab89412091d344cf949aa8ccb2da91df56e3ec9724c`.
Derivation SHA-256 is
`f50221b60a304fb823539909ecb2ab9aa30493013a7ede9e5d0a318149580532`.
Actual checked-package validation covered all 3,235 files. There was no new full
native compilation, ABI rerun or claim of whole-build reproducibility. No
DDS/RTPS/RMW semantic or NO_REACCEPT protection change was made.

Actual host execution UTC `2026-10-18T11:28:28.734487+00:00` to `2026-10-18T11:29:53.170557+00:00`, exit **0**.
Cwd: `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport`. Exact argv:

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r15-lifecycle\\publish-worktree\\tests\\ros2_target\\run_sustained_roundtrip.py",
  "run",
  "--prepared",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r26-sustained-restart\\prepared-60a99477b1d6a82b\\PLAN.json",
  "--release",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r25-native-actionbag\\sustained-roundtrip-query1-window\\MAIN_ROUNDTRIP_RELEASE.json",
  "--hdc",
  "C:\\Users\\17715\\AppData\\Local\\OpenHarmony\\Sdk\\23\\toolchains\\hdc.exe",
  "--output",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r26-sustained-restart\\execution-60a99477b1d6a82b"
]
```

Actual A dispatch argv:

```json
[
  "/data/local/tmp/mdds_dds_f31ef86f8ed07cf2_A/package/run-token-first",
  "--script",
  "/data/local/tmp/mdds_dds_f31ef86f8ed07cf2_A/package/scripts/sustained_roundtrip_probe.py",
  "--role",
  "requester",
  "--run",
  "f31ef86f8ed07cf2",
  "--side",
  "A",
  "--finish-control",
  "/data/local/tmp/mdds_dds_f31ef86f8ed07cf2_A/control.PROBE_FINISH"
]
```

Actual B dispatch argv:

```json
[
  "/data/local/tmp/mdds_dds_f31ef86f8ed07cf2_B/package/run-token-first",
  "--script",
  "/data/local/tmp/mdds_dds_f31ef86f8ed07cf2_B/package/scripts/sustained_roundtrip_probe.py",
  "--role",
  "responder",
  "--run",
  "f31ef86f8ed07cf2",
  "--side",
  "B",
  "--finish-control",
  "/data/local/tmp/mdds_dds_f31ef86f8ed07cf2_B/control.PROBE_FINISH"
]
```


These are recorded commands for consumed releases, not reusable launch
instructions. Future device runs require fresh route/release admission.

## Tests, review and explicit environment workaround

The initial tester found **SR-01**: the resource oracle accepted regressing
timestamps or samples placed outside their payload/completion chronology. Its
initial gate remains FAIL (14 repository tests passed, independent controls
exposed two failures). The fix validates resource time progression and position;
the failure evidence is retained.

Independent review then reproduced **SR-02**: the runner could finish a slow
content verification after the 35 s deadline and still accept BUSINESS. That
review remains FAIL. The repair checks the same absolute deadline after the
oracle and after receipt writing, before entering CLOSE, and checks paired
checkpoint readiness against the stage deadline. Original 35/40/50 s bounds
were not enlarged. A late receipt cannot grant normal finish/execution PASS.
The independent repair review found no actionable findings, followed by final
host tests and this independent device audit.

Final host executions passed: 18 repository tests, the original SR-02
counterexample, eight independent runner methods and six wrapper methods with
16 scenarios, all exit 0. Wrapper/positive workflow controls explicitly use
host fixtures and mocked remote/child operations. Earlier seven probe and three
cleanup/admission methods remain same-source evidence, not new final executions.
Actual package/prepare checks passed; stale saved routes were rejected by the
real constructor before dispatch. Host readiness alone did not grant device PASS.

**Recorded workaround:** query `29f1c3850a48ed18` actually passed once per board,
with original anchor `2026-10-18T11:26:53.642557+00:00`. Original local postquery
command 0 failed with `PermissionError: WinError 5` resolving `binding.json`
under the Windows sandbox; command and continuation exit codes were 1. That
failure remains `FAIL_PRESERVED`. Main then executed the same four local argv
from `POSTQUERY_PLAN.json` in the normal-user/elevated context: convert,
validate, routes and prepare all exited 0. Recovery receipts are retained in
`query1-local-permission-recovery/`. There was no new device query, route-anchor
renewal, network/security-setting change or replacement of the original failure.
The final audit measured host-start route age 95.091930 s and consume age
98.217712 s. Original freshness checks remained enforced. Device execution
then exited 0; the environment recovery is not mislabeled as an original PASS.

## Capability matrix and next decision

| Scenario/capability | Current status | Reason / next requirement |
| --- | --- | --- |
| S1: 200 ordered String application roundtrips, then normal paired exit | SUPPORTED within the fixed case; DEVICE/INTEGRATION_TESTED PASS | Selected two boards, package, identity, QoS and bounded rate only. |
| S2: B exits/restarts while A stays alive | BLOCKED_CURRENT_DEFAULT_NO_REACCEPT | Current default does not enable the separately gated bounded native reaccept experiment. A new B PID does not reset A's retained connection state. No configuration change is authorized by this result. |
| S3: both exit, then fresh pair starts | NOT_RUN | A separate cold-start case; would not establish recovery of a surviving peer. |
| S4: same-process Participant recreation | NOT_PROVEN | SDK callback epoch/quiescence contract and separate architecture/test plan remain required. |
| Resource stability | PARTIAL observation only | Five samples per side; observed RSS growth, stable sampled threads/FDs. Leak freedom and long-duration behavior NOT_PROVEN. |
| Unique flow / dedicated SEDP | NOT_PROVIDED / NOT_QUALIFIED | One diagnostic pair on each board is retained; strict unique-flow mode UNTESTED. Startup is not claimed error-free. |
| Native payload correlation | PARTIAL | Native send/ingress/provider evidence exists; no direct per-payload-to-RTPS sequence mapping. |
| General QoS/types, concurrency, large/fragmented messages, stress | UNVERIFIED by S1 | No applicable execution here; one outstanding String and selected QoS only. |
| Prior native Action, ordinary CLI and rosbag | Existing bounded PASS retained | Their original source/package/run evidence remains separate. S1 does not expand service/action or rosbag coverage. |
| Overall ROS 2 | NOT VERIFIED | R22 remains STOPPED; general feature/lifecycle gaps remain. |

S2 is a blocker in the **current selected default configuration**, not a claim
that process restart is technically impossible. The smallest next step is an
explicit architecture/experiment decision: either qualify the existing bounded
native replacement experiment or first establish the missing SDK lifecycle
contract. Before any target attempt, prove the exact package compile flag and
explicit gate/order fields, retain A's Participant/GUID/listener, bind B's new
owned PID/starttime and agreed locator/name identity, observe shutdown/admitted
replacement, then resume numbered traffic. Keep lost, duplicate and late samples
and cleanup outcomes separate. If the existing gate cannot express that process
sequence, propose the minimal Transport-only change with graph impact and host/
device regression scope before enabling it. NO_REACCEPT remains unchanged.
An independently scoped S3 cold start can be planned, but cannot substitute for
S2/S4 acceptance. See the local `RESTART_DECISION.md` evidence below.

After this S1 stage is published, work continues on S2 rather than ending at
the short-window PASS: first inspect the actual package compile flag and control
qualification, then pursue a separately reviewed bounded process-replacement
case within existing safety constraints. Prefer real-board acceptance for the
next admitted case; do not expand a generic framework to avoid the missing
device/lifecycle evidence. A blocked qualification remains explicit until resolved.
Route credential freshness (the applicable 240/300 s preparation/admission
limits) is separate from the 35 s business, 40 s probe and 50 s normal-close
bounds. A later long-duration case needs its own reviewed timing contract; none
of these already reviewed limits is silently enlarged by this documentation.

## Evidence and publication

Evidence below resides in local ignored build directories. Source publication
does not upload these artifacts or promise remote downloads. Final audit
rechecked 2,238 original files, current/frozen sources, prior evidence, four
worktrees/indexes and actual raw receipts without HDC replay.

| Local evidence (root-relative) | SHA-256 |
| --- | --- |
| [build/r26-sustained-restart/sustained-probe-sr02-retest/FREEZE.json](../../../../build/r26-sustained-restart/sustained-probe-sr02-retest/FREEZE.json) | `9da97c8882205db8f1038684b4d71a289f1c363084e81928c4c66498c67c9db1` |
| [build/r26-sustained-restart/sustained-probe-sr02-retest/RESULT.json](../../../../build/r26-sustained-restart/sustained-probe-sr02-retest/RESULT.json) | `f00c76180c129830fbb3f98b25c9a89b8c2f070c4f6515c6b4df98a1f47ad61e` |
| [build/r26-sustained-restart/sustained-probe-sr02-code-review/RESULT.json](../../../../build/r26-sustained-restart/sustained-probe-sr02-code-review/RESULT.json) | `404a6c1250da7a45db4e461814364a0db07bbae47b77d20d74758535812b0381` |
| [build/r26-sustained-restart/sustained-probe-final-host/RESULT.json](../../../../build/r26-sustained-restart/sustained-probe-final-host/RESULT.json) | `e62b998fcc6ea8d0251ad2bcb469474571c19b3f3556c4f3ff329a08223abf63` |
| [build/r26-sustained-restart/execution-60a99477b1d6a82b/RESULT.json](../../../../build/r26-sustained-restart/execution-60a99477b1d6a82b/RESULT.json) | `5f475e3ce8c12228b455bde1808d6ff85bda1681e7a6e7e98eec197a97c36409` |
| [build/r26-sustained-restart/query1-roundtrip-host/HOST_EXIT.json](../../../../build/r26-sustained-restart/query1-roundtrip-host/HOST_EXIT.json) | `86cc8a0941688a8d161e8e61460f7e22a646b0bb15e9c2277c1a5f99a82f961b` |
| [build/r26-sustained-restart/package-4bd68cc7db3b7d31/DERIVATION.json](../../../../build/r26-sustained-restart/package-4bd68cc7db3b7d31/DERIVATION.json) | `f50221b60a304fb823539909ecb2ab9aa30493013a7ede9e5d0a318149580532` |
| [build/r26-sustained-restart/sustained-device-final-audit/RESULT.json](../../../../build/r26-sustained-restart/sustained-device-final-audit/RESULT.json) | `da7d7a36e74dee5dee232c4706ded549db942d8fcc92cbe9a76e628a266a586c` |
| [build/r26-sustained-restart/sustained-device-final-audit/SEAL.json](../../../../build/r26-sustained-restart/sustained-device-final-audit/SEAL.json) | `6cf9e4312c9b0df47cc5de41fffd1509a87e0268d4371c76d27400448f4bf724` |
| [build/r26-sustained-restart/RESTART_DECISION.md](../../../../build/r26-sustained-restart/RESTART_DECISION.md) | `ceae0e2a97c2fa78e43df34b0fcd47364f2130debad14ea4e0b9fd3a99e0104b` |
| [build/r26-sustained-restart/BASELINE_AND_MATRIX.json](../../../../build/r26-sustained-restart/BASELINE_AND_MATRIX.json) | `fc68e6a8f508c825a277cb53fd8caf8bac7fe4046e2493e2bf09e61e68848a2c` |
| [build/r25-native-actionbag/main-bag-recordplay-publication/POST_PUBLISH.json](../../../../build/r25-native-actionbag/main-bag-recordplay-publication/POST_PUBLISH.json) | `af9d9b38816859857e1ec7bb1d398d6b177e8a1274b9f1ba557bd8f4cb2d57d8` |
| [build/r26-sustained-restart/sustained-probe-initial-test/SEAL.json](../../../../build/r26-sustained-restart/sustained-probe-initial-test/SEAL.json) | `2bcd4790df94dc244f0e84601c371816ab1007b4f0b27d25a7667ae038ead28b` |
| [build/r26-sustained-restart/sustained-probe-fix1-initial-test/SEAL.json](../../../../build/r26-sustained-restart/sustained-probe-fix1-initial-test/SEAL.json) | `a5fa855ff597ac3ee2776ab196296297f2a69806cf29f06f04e37f0c8f0f160c` |
| [build/r26-sustained-restart/sustained-probe-code-review/SEAL.json](../../../../build/r26-sustained-restart/sustained-probe-code-review/SEAL.json) | `a3e5d64944bdcc173e47159309f713d1fc0a2d54d6cfcbfe3f09c5a3ca4ada51` |
| [build/r26-sustained-restart/sustained-probe-sr02-retest/SEAL.json](../../../../build/r26-sustained-restart/sustained-probe-sr02-retest/SEAL.json) | `a6c0771fa06d5931acbf0d8bd0c504f51794cc82ce3562acc6c1fd06b9f23554` |
| [build/r26-sustained-restart/sustained-probe-sr02-code-review/SEAL.json](../../../../build/r26-sustained-restart/sustained-probe-sr02-code-review/SEAL.json) | `28fbd4354ee3c2e9d3eb5c9676b9593226637d2ab086eed50ea75d36583f0279` |
| [build/r26-sustained-restart/sustained-probe-final-host/SEAL.json](../../../../build/r26-sustained-restart/sustained-probe-final-host/SEAL.json) | `607465898438f72e1043a134f52d3915d1290cbbbe63fa4668fe99b26c4a3df0` |
| [build/r25-native-actionbag/sustained-roundtrip-query1-window/command-0.json](../../../../build/r25-native-actionbag/sustained-roundtrip-query1-window/command-0.json) | `2bda2998eea839d3170edd820184d7dac859761c27e5d1451e692c21a3ebce23` |
| [build/r26-sustained-restart/query1-local-permission-recovery/command-0.json](../../../../build/r26-sustained-restart/query1-local-permission-recovery/command-0.json) | `6b3eb961ad412bd793db480c4d359104004a389479ba51f617d7dcc255817f92` |
| [build/r26-sustained-restart/query1-local-permission-recovery/command-1.json](../../../../build/r26-sustained-restart/query1-local-permission-recovery/command-1.json) | `bae492f42f4f641ea94bc8d37c0040f82608665512c4db72df127d68abe3a070` |
| [build/r26-sustained-restart/query1-local-permission-recovery/command-2.json](../../../../build/r26-sustained-restart/query1-local-permission-recovery/command-2.json) | `ee95138c0a16936925a3915c76bc3dbff9295a73b58aef1014a55b6411c4029d` |
| [build/r26-sustained-restart/query1-local-permission-recovery/command-3.json](../../../../build/r26-sustained-restart/query1-local-permission-recovery/command-3.json) | `8d59ffd3a17bea558b04b30add1044a157a11727685729e57fab20b11403a8a8` |

The prior bounded record/play stage was published as
`6d025da0efd088be46fb59fdadfd18755f5cc31e` on the existing
`origin/codex/flow-encoding-published-20261014`. Exact-SHA CI at
2026-10-18 10:45:07.955187 UTC was NOT_RUN (zero runs/checks/status contexts).
The current S1 four-source stage has tester Commit gate PASS; Main graph change
analysis, PRE_COMMIT, new commit/push and new exact-SHA CI readback remain pending.
