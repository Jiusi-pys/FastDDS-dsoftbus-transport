# R32 autonomous Action after one guarded server replacement

Snapshot: 2026-10-18T23:26:38.723843+00:00; W3 HEAD `f14e57c9e5e94558a66a0f028cf99801f37ccd84`.

**Implementation PRESENT; final host PASS; DEVICE_TESTED / CROSS_DEVICE_TESTED PASS for one finite explicitly guarded replacement. Overall full ROS 2 NOT_VERIFIED.** R31 current-stack natural Action AB/BA is published as `f14e57c9e5e94558a66a0f028cf99801f37ccd84` on the existing branch (2026-10-18 22:16:35 UTC exact-SHA CI NOT_RUN). At this 2026-10-18T23:26:38.723843+00:00 pre-commit documentation snapshot, the R32 finite technical tester gate is PASS; Main graph/PRE_COMMIT/commit/push/CI are PENDING. Final publication outcomes are recorded separately in root-relative `build/r32-action-natural-restart/main-natural-restart-publication/POST_PUBLISH.json`; no future SHA or CI success is prefilled.

## Qualified behavior and architectural boundary

One persistent A Node/Context/runtime/Participant/ActionClient performs its own application sequence: initial readiness -> PRE order6 -> its PRE result -> OLD order7 -> readiness false then true -> NEW order8. There is no START_OLD/COMPLETE/END business permission or intermediate EXECUTING success prerequisite. Existing listener/finish/runtime-close controls and **one ARM plus one REPLACE** remain transport/process lifecycle infrastructure. Physical A selects the existing bounded outgoing-first backend with process observation=1; B0/B1 select the ordinary backend. This is autonomous application business over an explicit experimental replacement, not default automatic reconnect or unrestricted natural node lifecycle.

B0 finishes normally, restores its token and both owned PIDs become absent before B1 launches with fresh PID/starttime/root/Participant. A preserves client object545605513200, runtime545600483168, Participant `010f5a582c6ed91f00000000000001c1` and all26 anchors including8 Action endpoints. Retained listener1; native old dial2/accepted3 owners4/7 become dial4/accepted5 owners79/82. SDK send/ingress RTPS pairs match A/B0 and A/B1; this does not prove SDK epoch/drain or map each Action payload directly to an individual RTPS sequence.

| Phase | UUID | Observed business result |
| --- | --- | --- |
| PRE order6 | `f5930ec99f5d70a8347042881381bbf9` | Five exact feedback prefixes, exact SUCCEEDED result, peer application CDR equal. |
| OLD order7 | `b8581b7410fd401894815eeeba75a184` | Six feedbacks and SUCCEEDED **before disappearance**. Ordinary completion, not old-goal recovery or remote cancellation. |
| NEW order8 | `2282a8596565597f9d149ead274ce99d` | Seven exact feedback prefixes and SUCCEEDED after the same client observed unavailable then ready; peer application CDR equal. |

Three goals, zero application retries. Goal/request/result associations are distinct; OLD cannot substitute for NEW. Three typed Goal/Feedback/Result CDR roundtrips,10 imported module origins/hashes and capsule checks per process matched the package. These application witnesses are not proof of every protocol wrapper CDR; CancelGoal capsule/endpoint presence does not prove cancellation execution.

## Environment, package and command record

Windows host: `C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B`; existing OpenHarmony SDK23 hdc. Physical RK3588A A `3e01ff55454d202020104033bf453b00`, token672006291; B `3e01ff55454d202020104433991c3b00`, token672042405. Scenario `98dac942b90719fd`, original paired query `495e2a0f9b739c25`, execution `6dbaf4fd56fdf4e5`. The exact [host start](../../../../build/r32-action-natural-restart/scene-query1-fix2-host/HOST_START.json) / [exit0](../../../../build/r32-action-natural-restart/scene-query1-fix2-host/HOST_EXIT.json) and [query/continuation receipts](../../../../build/r32-action-natural-restart/device1-final-audit/QUERY_AND_HOST.json) bind all argv, cwd, time, exits and streams. This recorded command is not authorization to reuse its consumed scene/query:

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r15-lifecycle\\publish-worktree\\tests\\ros2_target\\run_action_natural_restart.py",
  "run",
  "--prepared",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r32-action-natural-restart\\prepared-6dbaf4fd56fdf4e5\\PLAN.json",
  "--release",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r32-action-natural-restart\\MAIN_SCENE_QUERY1_FIX2_RELEASE.json",
  "--hdc",
  "C:\\Users\\17715\\AppData\\Local\\OpenHarmony\\Sdk\\23\\toolchains\\hdc.exe",
  "--output",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r32-action-natural-restart\\execution-6dbaf4fd56fdf4e5"
]
```

Only four new test-side files: `integrations/ros2/target/action_natural_restart_probe.py` and `tests/ros2_target/{action_natural_restart_oracle,run_action_natural_restart,test_action_natural_restart}.py`. The target script is a finite fixture; production DDS/RTPS/native Transport/rclpy are unchanged. Exact hashes:

| Source | SHA256 |
| --- | --- |
| `integrations/ros2/target/action_natural_restart_probe.py` | `db2d83c4bb85b0d91c4882a7486e14dc4ae9aa5b7947865e6ebe00ea4f3bfe05` |
| `tests/ros2_target/action_natural_restart_oracle.py` | `310e0c89997f0e3aa7df04d369e18f8bfcb7e9a81b32736a6c73cb4f9b7e8811` |
| `tests/ros2_target/run_action_natural_restart.py` | `d0b50def3e3aa878ac3c2bf9071ac9ea1467e8b4e78b68f4a0127c3046955c52` |
| `tests/ros2_target/test_action_natural_restart.py` | `f81971d5a1ba4d43946fd4803aa4fdfeca7efb668df059834e60396a220d2adc` |

Package `build/r32-action-natural-restart/package-9b7afb81a8c7431e/package` has3,237 files/220ELF: original R30/R31 3,236-file current stack with only one new probe and exact launcher manifest delta. DERIVATION SHA256 `8cd2676d97dd233a3479aa15be0605c318c629c057586b85c5403baffb5a4f80`. Current bridge remains `9db2a35c7da2cb98afa2c255ab122179e5a959a9d4a39515b11881ffe98039c1`, using the previously built API23 ON/OFF artifacts and897 dependency hashes: **REUSED_HASH_VERIFIED_NOT_REBUILT**. No fresh native compile or whole-build reproducibility claim. Current source inventory276 SHA256 `703c348bd02e1eb568a15a13d645328778b8bdea84c7a86b88029825f6f6b567` preserves the original272 subset. [R31 acceptance](ROS2_NATURAL_ACTION_ACCEPTANCE.md) explains native source/build lineage; [R30 acceptance](ROS2_ACTION_RESTART_ACCEPTANCE.md) remains its separate coordinated-replacement scope.

## Execution, cleanup and timing

Independent raw audit confirms40 query commands,4 local continuation commands and380 scene commands:356 unique remote exit0 markers and24 successful file transfers. Host scene exit0; three child rc0/finalize0/token restorations, six owned PIDs absent, zero signals. Actual bridge/libsoftbus loaded-map hashes match admitted package/providers; native Socket/Listen/Bind/SendBytes/Shutdown records, fake_sdk0, builtins0, intraprocess0 and data-sharingOFF substantiate the native path. Known optional unique-flow failure diagnostics remain in every process; original-QoS fallback stays on custom Transport, unique-flow NOT_PROVIDED, strict UNTESTED. Startup is not described as error-free.

Scene collection follows process-pair absence: pre-transfer size, successful receive/local bytes and post-transfer remote SHA agree. The collector does not capture a pre-transfer SHA, and none is claimed; paired query collection has its separate before/after SHA contract. The final audit preserves3,253 original raw/query/prepared/execution members and replays actual three-process streams through content, native, continuity and endpoint oracles; their output equals the original runner analysis.

| Bound / origin | Actual observation |
| --- | --- |
| Original query anchor | 2026-10-18 23:11:56.004346 UTC; no freshness reset. |
| Dispatch <240 s; A/B0/B1 launches <=300 s | 62.221290 s;114.962382/115.560743/130.066914 s. |
| Query whole600 s / per RPC30 s | Capture9.477907 s; contracts passed. |
| Four local commands60 s each / outer120 s | Each passed; outer16.920870 s. |
| Business35 s, normal-close-release50 s | 15.531 s /23.625 s from existing listener-release origin. |
| Final exit20 s / analysis60 s | 1.062 s /4.563 s. |
| Probe40 s | Observed NODE_READY-to-FINAL A19.820515/B0 4.750450/B1 10.191922 s; source enforces create_node-return origin, whose exact timestamp is not independently exported. |
| Entire scene host elapsed |107.378420 s including admission/deployment; no outer process kill. |

These are host/control/capture-inclusive measurements, not network latency. One scene attempt, no automatic new query/window or relaxed bound. [Timing](../../../../build/r32-action-natural-restart/device1-final-audit/TIMING.json), [process identities](../../../../build/r32-action-natural-restart/device1-final-audit/PROCESSES.json) and [normal-exit ordering](../../../../build/r32-action-natural-restart/device1-final-audit/NORMAL_EXIT_ORDER.json) retain exact details.

## Initial failures, independent review and retest

- **T01 initial FAIL:** the actual oracle accepted three impossible same-process OLD timelines (feedback before own goal, result before own feedback, client feedback before own send). Same-process causal checks were added; original full-chain counterexamples reject after repair. Legitimate missing/late OLD observations and optional status remain allowed; no cross-board ordering requirement was invented.
- **T02 fix1 FAIL:** admission incorrectly rejected the original query product's exact external SDK23 libc++ provenance as outside ROOT/build. Repair admits only the fixed read-only SDK leaf/hash `d1a368b58e7cb80a7422b8bd344ec9328a43884d62f409af6e9ec161c73a2f4d`; arbitrary SDK paths, wrong hashes and changed leaves reject. No writable root, device permission or network change.
- **Implementation review PASS:** independent source/lifecycle/admission review found no actionable blocker. Its sandbox18-test run preserved17 passes plus a Path.resolve WinError5; the unchanged normal-user suite passed18/18. Review configuration controls used explicit local send/shell stubs, not device traffic.
- **Post-review final HOST PASS:**18 repository tests,3 original chronology counterexamples,3 OLD outcome positives,17 communicate cases using mocked rclpy/native interfaces and real selected Python Future,33 deadline cases,8 execute/cleanup cases, extracted selected server API and3 inherited configuration paths. Actual3,237-file package/220ELF/current bridge/897 dependencies admitted; native not rebuilt. Real final receipt consumption passed; actual expired-route prepare rejected without output or clock patch. Fixtures remain HOST_TESTED.
- **OP01 operational review FAIL:** sealed scene output could drift before capture dispatch. Repair hashes the release helper, rechecks its read-only gate immediately before dispatch and pins prepared/output nonce and release fields.
- **OP02 repair review FAIL:** preparation seal's own hash was not compared before trusting rows. Final repair verifies all six reviewed role hashes including seal before members. The actual capture/gate33 offline controls, including both original drift counterexamples, passed; mocked subprocesses do not constitute device execution. Earlier failed plans/seals remain immutable.
- **Final DEVICE audit PASS:** independent actual-raw replay,18 fresh repository tests and12 negative mutations of copied actual records; one optional transient-status omission accepted as specified. Its own first timeline helper alias collision/exit1 is retained, corrected only in a new audit helper. It is not a device/source failure. The audit made no HDC calls or new native build; original scene is the real execution evidence.

## Compatibility, remaining gaps and next stage

**OPEN — COMPAT_RCLPY_STATUS_BEFORE_GOAL_RESPONSE / UNCOORDINATED_NOT_FIXED.** Selected rclpy local commit `4e607c09d624debc39a4ae5405680c06d87fac35` (`7.1.12-1-g4e607c0`) remains unchanged, without a project client patch or latest-upstream claim. Status before local goal-handle registration may be ignored; acceptance/GetResult can succeed while handle status is UNKNOWN/stale. Result-time callback removal can drop late observed feedback. Intermediate status is observation-only, and no EXECUTING prerequisite is added. Strict PRE/NEW fixture feedback remains required; this successful schedule does not guarantee every schedule or close the compatibility issue.

OLD completed before disappearance; no retained goal recovery, cancellation or resumed execution is claimed. Native replacement remains explicitly guarded. Default automatic reconnect, SDK global callback drain/epochs, leak freedom, repeated/long-duration restarts, failure injection, concurrent goals, broad QoS/types and large-message behavior are not proven here. Dedicated SEDP remains NOT_QUALIFIED; application-to-RTPS direct payload mapping is absent. Existing native C++ Action/service/rosbag acceptance retains its own scope. R22 remains STOPPED. Overall full ROS2 NOT_VERIFIED.

Continue with a separately reviewed bounded repeated/lifecycle or wider-feature acceptance plan based on real boards and existing safe controls. Preserve selected-client observation limitations and stop dependent device actions on anomalous state; do not silently expand native replacement permissions or deadlines to obtain PASS.

## Immutable local evidence

These ignored build paths are local evidence, not promised remote downloads. The device audit SEAL has217 members; the3,253-member original snapshot is a separate scope. Complete argv/cwd/UTC/exit/raw streams, package/native/provider/source pins and failed gate receipts are referenced by the canonical RESULT.

| Receipt | SHA256 |
| --- | --- |
| [device1-final-audit/RESULT.json](../../../../build/r32-action-natural-restart/device1-final-audit/RESULT.json) | `077a77c60a79637b8529b2c3583e2afe90d9fd908efdf0158fdfd367ab1159b1` |
| [device1-final-audit/SEAL.json](../../../../build/r32-action-natural-restart/device1-final-audit/SEAL.json) | `d2df06f13bf75fbd0b1f3e445729e7779116e90b6403dbe0b1caa7d3299a5b0a` |
| [final-tester/RESULT.json](../../../../build/r32-action-natural-restart/final-tester/RESULT.json) | `352c150344d4d819d9c16ac3565eaa1596d698f948814e21074d1c14e445d091` |
| [implementation-review/RESULT.json](../../../../build/r32-action-natural-restart/implementation-review/RESULT.json) | `7fe847bbec3df93fa1ce78ff899a53815aa9ff7b6e4b4c87fa1e97156c6f469e` |
| [initial-tester/RESULT.json](../../../../build/r32-action-natural-restart/initial-tester/RESULT.json) | `bdf9c7a64559f99560c6028836e37ba6695197fc2b43ac9ba2b089a679f7734e` |
| [initial-fix1-tester/RESULT.json](../../../../build/r32-action-natural-restart/initial-fix1-tester/RESULT.json) | `f0dae5abe893266ece9262ca529f869f69a93b8a81b6c0e0ddcfffc42b4f25ec` |
| [initial-fix2-tester/RESULT.json](../../../../build/r32-action-natural-restart/initial-fix2-tester/RESULT.json) | `9529f5c59908a11b1ef246b345bfff81f18927231c6ba8062aad5f83d52fb3e0` |
| [device-plan-review/RESULT.json](../../../../build/r32-action-natural-restart/device-plan-review/RESULT.json) | `3834d02414ead621ca6fbff286db4e236c4f509cf781221ca6fdcecd495310d2` |
| [device-plan-fix1-review/RESULT.json](../../../../build/r32-action-natural-restart/device-plan-fix1-review/RESULT.json) | `2ddb7f21141960aadd5fbfe2020d920e338a4fce75602891267777b378727361` |
| [device-plan-fix2-review/RESULT.json](../../../../build/r32-action-natural-restart/device-plan-fix2-review/RESULT.json) | `eee9b0aac402420da5843ce1128d7a762954738e752428888eff0955e3e174b7` |
| [execution-6dbaf4fd56fdf4e5/RESULT.json](../../../../build/r32-action-natural-restart/execution-6dbaf4fd56fdf4e5/RESULT.json) | `79547a58e8bed8a4c1c26eee32674847dad79d87be7477a961b00711b677b1ec` |
| [final-tester/SEAL.json](../../../../build/r32-action-natural-restart/final-tester/SEAL.json) | `b9f1141f56a42d581b1049498b1c334facccb0a2efd16eb9969522ad1e176662` |
| [implementation-review/SEAL.json](../../../../build/r32-action-natural-restart/implementation-review/SEAL.json) | `c3939d3b1097fc7efb477adaf88fbe84df7a0ea19ee8b332a0be17d90f2459ae` |
| [initial-tester/SEAL.json](../../../../build/r32-action-natural-restart/initial-tester/SEAL.json) | `ec670b7b1e6583806c98e75e854d6903bd4201b2ecaabb06872a81137bbfb2ae` |
| [initial-fix1-tester/SEAL.json](../../../../build/r32-action-natural-restart/initial-fix1-tester/SEAL.json) | `453420121d248eb666cbd1c66e66df89c06782b4d8507d531d94b86e3694821d` |
| [initial-fix2-tester/SEAL.json](../../../../build/r32-action-natural-restart/initial-fix2-tester/SEAL.json) | `4c835ec48feaec9ce46b59e7111168065f3733f75f9cdf3193156082157dc8f2` |
| [device-plan-review/SEAL.json](../../../../build/r32-action-natural-restart/device-plan-review/SEAL.json) | `36a8f086ddf4f438c9bd88d07d8fa1bacdbedfb6706a8e0b93d96ef911fc0611` |
| [device-plan-fix1-review/SEAL.json](../../../../build/r32-action-natural-restart/device-plan-fix1-review/SEAL.json) | `cef581b286a08f0b0b34e62e97f5fb524cb520f5ae1af5b27dd5131c189e858e` |
| [device-plan-fix2-review/SEAL.json](../../../../build/r32-action-natural-restart/device-plan-fix2-review/SEAL.json) | `ececaa9e811c06eac13bf0cb834a220f1f403f2e6d7ba8f0bff25c82e6e39c55` |
| [R31 publication](../../../../build/r31-action-natural/main-current-action-publication/POST_PUBLISH.json) | `3e9dcce55df3979932d3f07bc5e7a4576f524d48a23cf780db4b955a753d3985` |

[Device audit report](../../../../build/r32-action-natural-restart/device1-final-audit/REPORT.md), [final host report](../../../../build/r32-action-natural-restart/final-tester/REPORT.md), [implementation review](../../../../build/r32-action-natural-restart/implementation-review/REPORT.md) and [final operational review](../../../../build/r32-action-natural-restart/device-plan-fix2-review/REPORT.md) distinguish actual device facts from host fixtures and preserve all failure history.
