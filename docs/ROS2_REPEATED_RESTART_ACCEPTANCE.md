# R28 finite repeated process restart acceptance

> **Publication-state timestamp:** Any R28 Main gate/publication/CI pending statement in this document describes the **2026-10-18 16:41 UTC pre-commit documentation snapshot**, not a permanent current-state assertion. The finite scene evidence is PASS; Main records the actual commit, remote branch and CI outcome separately in root-relative `build/r28-repeated-restart/main-repeated-restart-publication/POST_PUBLISH.json` when publication completes. No future commit SHA or CI success is prefilled.

Snapshot: 2026-10-18 16:41 UTC. **IMPLEMENTED / BUILDS / HOST_TESTED /
DEVICE_TESTED / CROSS_DEVICE_TESTED PASS**, restricted to two explicitly enabled
normal B process replacements with one continuous A and 15 String pub/sub echo
roundtrips. Independent tester gate PASS; documentation Ready for commit YES.
Main graph/PRE_COMMIT, R28 commit/push and CI remain pending. Overall ROS 2
NOT VERIFIED. Prior R27 is published as `46ef5523fdee66421dc29b23aaeae9e095d8fa0d`;
its recorded exact-SHA CI is NOT_RUN.

## Actual implementation and acceptance

The fourteen-file implementation scope is pinned by IMPLEMENTATION_FILES below;
REVIEW_SCOPE_FIX4 is a historical earlier-source snapshot. The public Socket
backend's native reset limit defaults to 1. Explicit `process_restart_cycles=2`
is accepted only for non-route native bounded A with process observation, with
the experiment enabled. A finite two-round ledger retains old socket/owner
history; third reset/control remains refused. These are Transport/control and
test-harness changes, not DDS/RTPS/RMW semantics changes or default automatic
reconnect. No Session API fallback, network/security changes or history reclaim
is used to obtain this result.

Q4 scenario `5c46bab2fc2d0a61`, execution `d1f37ff8c89e5836`, used real rclpy
String pub/sub on the RK3588A pair: A `3e01ff55454d202020104033bf453b00`, B
`3e01ff55454d202020104433991c3b00`. B0 normal absence precedes B1 launch, and B1
normal absence precedes B2 launch. A keeps one Runtime, one Participant, 20
endpoints and seven continuity anchors. Three phases of five application echo
roundtrips yield 30 logical publications and 60 side payload records; each
exchange has four equal CDR copies. Native SDK/RTPS structural matches qualify
the path; there is no direct per-payload RTPS sequence mapping. This is not ROS
service/action recovery.

Four children and public/interpreter finalizers returned 0; all four restored
tokens, all eight owned PIDs became absent, and no TERM/KILL was used. All 546
scene host commands returned 0 without timeout: 514 shell markers independently
establish remote0, while 32 transfers carry no remote-shell exit claim. Query40
and four exact planned local continuation commands passed. The real 3,236-file
package, provider maps and raw captures are bound by the final audit.

Business acceptance was 34.532/35 s (0.468 s remaining). Probe40, normal-close50,
final-exit20 and analysis60 limits were unchanged; query construction300 and
dispatch240 are separate preparation freshness limits. Close completed within
its original remaining budget. Conservative final-exit/analysis upper bounds
are 10.062/30.448216 s; unavailable exact stage-start timestamps are not invented.
The probe40 code/accepted phase evidence is retained, not a claim that all
process teardown events occurred within 40 s of the first log line.

Three native samples observed owner totals3/5/7. Five A OS samples at PHASE0,
AFTER_B0_EXIT, PHASE1, AFTER_B1_EXIT and PHASE2 retain full before/after owned
identity and original bytes. FDs19/17/19/17/19, threads23 throughout, RSS82872
to83464 KiB (+592) are finite observations; leak freedom is NOT_PROVEN.

## Validation history and preserved failures

Initial host blockers remain sealed: T01 resource oracle accepted invalid
missing/zero/reused/chronologically reversed evidence; T02 rejected accepted-first
replacement after observed retirement; T03 public-close/pump interleaving caused
false refusal; T04 accepted deadline equality/overruns. Repairs were independently
tested, reviewed and retested. Fix4 final host passed R28 11/R27 15 repository
methods, nine actual workflow boundary cases and 14 actual coordinator/backend
API-shape cases. API-shape/stub tests do not execute the board SDK. Native
OFF/ON build evidence is retained separately from host execution.

Q1 FAIL: A startup rejected configuration before successful Node/payload; B0
required TERM/KILL and exited137. Its historical token restoration remains
NOT_PROVEN despite later read-only healthy project identity. D01 admitted only
the existing cycles field into the parser whitelist; semantic restrictions stayed.
Reviewed/retested ON bridge `9db2a35c7da2cb98afa2c255ab122179e5a959a9d4a39515b11881ffe98039c1`
and OFF bridge `0d0bcf462d626581471208079f1a0666728acb08486f4e98002b6df95dec89b6`
were built; the known failure is not retroactively passed.

Q2 PASS_EXPECTED_PRODUCTION_ENTRY_REJECTION: actual packaged launcher -> rclpy
Node -> factory reached the expected domain202 versus201 rejection after cycles
parsing, before Runtime creation by executed error plus source ordering. Child71
was expected; finalize0, token restore and two PID absences passed. This was a
real negative entry test, not successful communication.

Q3 BLOCKED: all15 echoes and native observations existed, but the last OS
identity read had only0.031 s remaining and timed out. No accepted BUSINESS or
NATIVE_REPEAT_COMPLETE existed. A/B2 exited71 during failure cleanup; all tokens
and eight PID absences were verified without signals. Partial content does not
substitute for the missing deadline-qualified scene completion.

D02 reduced each fixed OS sample from three read RPCs to one sequential read
envelope. Before/after PID/starttime/PPID/executable/argv, read failures, exact
remote marker, raw hashes and capture/write deadlines remain enforced. This is
not an atomic snapshot. Independent review found no blocking issue; final host
freshly passed R28 12/R27 15 methods,31 actual-resource host controls,7 local Git
Bash /proc cases and9 workflow boundary cases. Seven unaffected runner methods
retain identical-source initial evidence. Native binaries/897 dependencies were
verified and reused, not rebuilt or rerun in D02. Host sandbox failures remain
preserved; final host used normal-user access. Q4 is the subsequent real success.

Final audit also retained an initial local helper failure using an obsolete
flat-package map verifier; corrected production map-consumer replay passed.
This was a local audit correction, not another device run. Q1 FAIL and Q3
BLOCKED remain unchanged.

## Remaining capabilities and next step

Default automatic reconnect and SDK epoch/drain are not established. The two
cycles do not qualify arbitrary/repeated unbounded recovery, long-duration load,
leak freedom, same-process Participant recreation, simultaneous restarts,
service/action recovery, general message types/large payloads or broad QoS.
Unique flow is NOT_PROVIDED with known diagnostics retained; strict UNTESTED;
dedicated SEDP NOT_QUALIFIED. Earlier bounded native Action, ordinary CLI and
rosbag qualifications remain within their original scopes; R22 STOPPED.
After Main's publication gate, select and independently admit the next bounded
recovery case. Do not silently enlarge reset allowance or the existing deadlines.

## Commands and sealed evidence

Windows host used project Python, SDK23 HDC and OpenHarmony RK3588A targets.
Actual scene host ran from ROOT and exited0 (16:34:08.526072 to16:36:27.048002 UTC):

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r15-lifecycle\\publish-worktree\\tests\\ros2_target\\run_repeated_restart.py",
  "run",
  "--prepared",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r28-repeated-restart\\prepared-d1f37ff8c89e5836\\PLAN.json",
  "--release",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r28-repeated-restart\\MAIN_SCENE_R28_QUERY4_RELEASE.json",
  "--hdc",
  "C:\\Users\\17715\\AppData\\Local\\OpenHarmony\\Sdk\\23\\toolchains\\hdc.exe",
  "--output",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r28-repeated-restart\\execution-d1f37ff8c89e5836"
]
```

Exact build/test/device argv, cwd, exits and raw streams are retained in the
linked stage seals. No extra build or device run was needed for documentation;
262 source pins and the final168-member audit were rechecked. Consumed route/
release files are historical evidence, not launch authorization. Local ignored
build links are not promised to be downloadable from the remote repository.

| Local evidence | SHA-256 |
| --- | --- |
| [build/r28-repeated-restart/device4-final-audit/RESULT.json](../../../../build/r28-repeated-restart/device4-final-audit/RESULT.json) | `e974529499fe121b7c3832ac4b016091c63378ac025d154a310eba9568045b56` |
| [build/r28-repeated-restart/device4-final-audit/SEAL.json](../../../../build/r28-repeated-restart/device4-final-audit/SEAL.json) | `5f7ae7995b8f9ac72004f3a70061efcf3a2727e66ebb2262da12f20cacb9fa8b` |
| [build/r28-repeated-restart/device4-final-audit/TIMING.json](../../../../build/r28-repeated-restart/device4-final-audit/TIMING.json) | `d63a44e565f6871b0cf6201eec1f095e4a235e7094ccfaf60288af0e214de9ea` |
| [build/r28-repeated-restart/device4-final-audit/HOST_QUERY.json](../../../../build/r28-repeated-restart/device4-final-audit/HOST_QUERY.json) | `61b29ad9e1871bb1c4147bceb8ecbba7575709b1c9ea29bec55a98a996f57e4d` |
| [build/r28-repeated-restart/device4-final-audit/ORACLE_REPLAY.json](../../../../build/r28-repeated-restart/device4-final-audit/ORACLE_REPLAY.json) | `0b2bf722cb574872982b850fd5496cd242ebdf5602b602dbd8c1652147929a54` |
| [build/r28-repeated-restart/device4-final-audit/OS_RESOURCE_REPLAY.json](../../../../build/r28-repeated-restart/device4-final-audit/OS_RESOURCE_REPLAY.json) | `5c902b8274f5fdb944e67effbc801f0b263154310d87d62b540227a999df5370` |
| [build/r28-repeated-restart/device4-final-audit/COMMAND_AUDIT.json](../../../../build/r28-repeated-restart/device4-final-audit/COMMAND_AUDIT.json) | `53f42957c19d215a7e06311b6265f64b18888b5662ad56341011b5cc25cd38c3` |
| [build/r28-repeated-restart/initial-fix4-tester/RESULT.json](../../../../build/r28-repeated-restart/initial-fix4-tester/RESULT.json) | `b06654943b369253de2afccc7c31087f15b48ee43e1cf46b6a93cbdd5baf0b4e` |
| [build/r28-repeated-restart/implementation-review/RESULT.json](../../../../build/r28-repeated-restart/implementation-review/RESULT.json) | `99705ea220bc40a16e5a1dc9dec824411c025bed849658576f435592746bbfed` |
| [build/r28-repeated-restart/final-host-tester/RESULT.json](../../../../build/r28-repeated-restart/final-host-tester/RESULT.json) | `c7206325b241452b2a02a5bc5336f9114c601454e983c10ad7e32a99b884520b` |
| [build/r28-repeated-restart/device-fix1-review/RESULT.json](../../../../build/r28-repeated-restart/device-fix1-review/RESULT.json) | `cf8a210ec66dc06f1dd5689698eccfcbacca9b117215dc09b2f206ee541840b7` |
| [build/r28-repeated-restart/device-fix1-final-tester/RESULT.json](../../../../build/r28-repeated-restart/device-fix1-final-tester/RESULT.json) | `11c9b7e8dcc5b6b9d7888f31349755fefdabe9a0d5b61bd27adecf128813245d` |
| [build/r28-repeated-restart/device-fix2-initial-tester/RESULT.json](../../../../build/r28-repeated-restart/device-fix2-initial-tester/RESULT.json) | `d6c13e0509a7c5db2be54fb1f9147b9262a5dafe3a289299d9cd8cd145c492b1` |
| [build/r28-repeated-restart/device-fix2-review/RESULT.json](../../../../build/r28-repeated-restart/device-fix2-review/RESULT.json) | `e3ca6284dae58a54abe2b110e815cd35f25b5f785ba8f5dcb97a8677d683fd29` |
| [build/r28-repeated-restart/device-fix2-final-tester/RESULT.json](../../../../build/r28-repeated-restart/device-fix2-final-tester/RESULT.json) | `3be111f47ed74cae8d5934630ded425a7ddf259a1bfc4d06773541ebdf49f958` |
| [build/r28-repeated-restart/device1-failure-audit/RESULT.json](../../../../build/r28-repeated-restart/device1-failure-audit/RESULT.json) | `02525a89719285a0f64997b317a9be73311d9846475cb56675e363c65ad24b09` |
| [build/r28-repeated-restart/parser-entry-device-audit/RESULT.json](../../../../build/r28-repeated-restart/parser-entry-device-audit/RESULT.json) | `8e3639e927a555d0a03069167f9a4cd0731132ffacf0e0bcab7894b48099b1fc` |
| [build/r28-repeated-restart/device3-failure-audit/RESULT.json](../../../../build/r28-repeated-restart/device3-failure-audit/RESULT.json) | `43c3cfbd4ec9d5c0bcf1ad4800a6e86c6a2e457674d71e6f8de1243f9e63a2cc` |
| [build/r28-repeated-restart/main-repeated-restart-publication/IMPLEMENTATION_FILES.json](../../../../build/r28-repeated-restart/main-repeated-restart-publication/IMPLEMENTATION_FILES.json) | `7ee88a73a21f247c6c51b7d97a743ede4a8c7405d7429798a34f77f05627c49c` |
| [build/r27-process-restart/main-process-restart-publication/POST_PUBLISH.json](../../../../build/r27-process-restart/main-process-restart-publication/POST_PUBLISH.json) | `59b085ae589c32728a3f00db24baf2e42769831d7262792f414fb4bad68fc19c` |
| [build/r28-repeated-restart/initial-tester/RESULT.json](../../../../build/r28-repeated-restart/initial-tester/RESULT.json) | `383c2c1cf213c9e8de21cb718aa95bb035137ef9b2878ea5ed15db26d19770a8` |
| [build/r28-repeated-restart/initial-fix1-tester/RESULT.json](../../../../build/r28-repeated-restart/initial-fix1-tester/RESULT.json) | `f9f64362c4df8a2ef8c70dcfb91f7431b7ddbac47beaed22f83b4c3b0520679e` |
| [build/r28-repeated-restart/initial-fix2-tester/RESULT.json](../../../../build/r28-repeated-restart/initial-fix2-tester/RESULT.json) | `a8fdb3763a3cd6f34ac887741566799497c80abb12de04964871c51a044bd0d2` |
| [build/r28-repeated-restart/initial-fix3-tester/RESULT.json](../../../../build/r28-repeated-restart/initial-fix3-tester/RESULT.json) | `f92c0b619c2aa7f19bb740e3b41654efe4f1ef55fbd15ee8c6e7c719462531a9` |
