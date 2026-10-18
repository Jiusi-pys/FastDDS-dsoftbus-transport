# R27 bounded process restart acceptance

## Current Q3 final acceptance (2026-10-18 13:52 UTC)

**IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / INTEGRATION_TESTED PASS,
limited to one explicit ON bounded normal B process replacement.** Independent
tester gate PASS; documentation Ready for commit YES. Main graph/PRE_COMMIT,
R27 commit/push and CI remain pending. Working HEAD is S1's published
`9cdbc84ed1371d53f4ee53b88dad5b3e5f1612ab`; its recorded CI is NOT_RUN.
The newly authorized Q3 supersedes the earlier pause for this run only.

Scenario `be433420201e879a`, execution `567473f5c174bed6`, used the same real
RK3588A pair, SDK23 HDC and project-native 3,235-file package. Previously built
ON bridge `4238b102257c7ea23ae88b9182b361617115dc37c008e2e31e04e3ffb53816a8`
and current probe were retained; this was not a whole native rebuild. Latest
host retest verified 896 compiler dependencies and unchanged native artifacts.

A retained its process, Runtime `545392594128`, Participant
`010f5a5860277c2500000000000001c1`, 20 endpoints and listener. B0 exited normally,
restored its token and became absent before B1 launch with new PID/starttime and
Participant GUID. Old/new dual-lane socket observations passed; zero observed
callbacks/receivers do not prove SDK epoch or drain. PRE5 + POST5 = ten String
application echo roundtrips, 20 logical publications and 40 side payload records;
each 32-byte CDR matched across all four copies. This is pub/sub, not a ROS
service/action recovery test, and payloads are not directly mapped to RTPS
sequence numbers.

Business accepted at +18.172 s; last normal close release +25.984 s. Original
business/probe/close limits remain 35/40/50 s; query freshness is a separate
preparation constraint (host-start age 49.411757 s). All 383 scene commands,
40 query commands and four local continuation commands returned zero; actual
host scene exit was zero. Three children rc0, six owned PIDs absent, tokens
restored, no TERM/KILL or identity mutation. This was normal completion.

The intervening read-only diagnostic made 16 shell calls (plus list/version),
all successful, but did not reproduce or resolve the prior HDC root cause.
The explicit workaround permits one scene-shared resample only for a completed
host0, strictly empty stdout/stderr missing-marker read-only poll, within the
original stage deadline. Original unknown capture is preserved; controls are
not retried. Independent review and final mocked-subprocess host tests passed:
15 repository methods, seven methods/21 scenarios and five review controls.
**Q3 did not use the resample.** Earlier probe/runner/native tests are retained
evidence, not falsely claimed as freshly rerun by this host retest.

Both earlier BLOCKED windows and their raw evidence remain unchanged; Q3 does
not retrospectively pass them. The parent/tool-reported host execution-channel
interruption before Q3 did not dispatch a new business run. Local resumed-query3
seal parsing first failed with TypeError (dict mistaken for list), before any
HDC call; the corrected reader preserved original seals and failure evidence.

Default automatic reconnect remains not enabled/unsupported by this qualification;
NO_REACCEPT outside the opt-in experiment is unchanged. SDK epoch/drain,
same-process Participant recreation, repeated failure/recovery, long-duration
load/leak freedom, general types/large messages/concurrency, service/action
recovery and broad QoS remain unverified. Selected String QoS does not qualify
all QoS. Unique-flow NOT_PROVIDED (one known diagnostic pair per instance),
strict UNTESTED; dedicated SEDP NOT_QUALIFIED. Prior bounded native Action,
ordinary CLI and rosbag results remain valid within their original scopes.
Overall ROS 2 NOT VERIFIED; R22 STOPPED. Next: Main publication gate, then a
separate bounded recovery case with fresh admission, preserving these limits.

Actual command below ran from the ROOT cwd recorded in HOST_START; device
commands, targets and exits are in the sealed execution/audit. Consumed release
and route files are evidence, not reusable launch authorization.

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r15-lifecycle\\publish-worktree\\tests\\ros2_target\\run_process_restart.py",
  "run",
  "--prepared",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r27-process-restart\\prepared-567473f5c174bed6\\PLAN.json",
  "--release",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r27-process-restart\\MAIN_SCENE_QUERY3_RELEASE.json",
  "--hdc",
  "C:\\Users\\17715\\AppData\\Local\\OpenHarmony\\Sdk\\23\\toolchains\\hdc.exe",
  "--output",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r27-process-restart\\execution-567473f5c174bed6"
]
```

The canonical device seal has 148 members. Evidence links are local ignored
`build/` files, not a promise of remote download availability.

| Local evidence | SHA-256 |
| --- | --- |
| [build/r27-process-restart/device3-final-audit/RESULT.json](../../../../build/r27-process-restart/device3-final-audit/RESULT.json) | `b42700d0959ec65de81abe925d2b5b93e3af71a6b5f8bb14cbec4c24e17dca2d` |
| [build/r27-process-restart/device3-final-audit/SEAL.json](../../../../build/r27-process-restart/device3-final-audit/SEAL.json) | `0cebe036e620a80b92af63c35efbb2dd6b6218d3c7d4401ff2d18bcd5faba907` |
| [build/r27-process-restart/hdc-fix-final/RESULT.json](../../../../build/r27-process-restart/hdc-fix-final/RESULT.json) | `ce57b173f59c372717a09c47d12d5368469642709c6b05e60683e1f9a2bf40e7` |
| [build/r27-process-restart/hdc-fix-final/SEAL.json](../../../../build/r27-process-restart/hdc-fix-final/SEAL.json) | `294b1117c9bca12b57757b285e0c453516ad5123761ed26b2be3b40253f3496a` |
| [build/r27-process-restart/hdc-fix-review/RESULT.json](../../../../build/r27-process-restart/hdc-fix-review/RESULT.json) | `2d1386c10097f6ba6329aa613fb2f0377751490bb2ef5ccad99ebc3d4110a930` |
| [build/r27-process-restart/hdc-fix-initial/RESULT.json](../../../../build/r27-process-restart/hdc-fix-initial/RESULT.json) | `5cdec048728400ac93699f3961b69e2d7e5b2260dd3ab329ae5bbfa82d0a699f` |
| [build/r27-process-restart/hdc-capture-diagnostic/RESULT.json](../../../../build/r27-process-restart/hdc-capture-diagnostic/RESULT.json) | `783308e31e7540d246d75c047bf5bf65313eca5cfd83f7a1ba2aa7a67e30e1b3` |
| [build/r27-process-restart/resumed-query3-main/INITIAL_CHECK_ERROR.txt](../../../../build/r27-process-restart/resumed-query3-main/INITIAL_CHECK_ERROR.txt) | `a92ecaeaff56e6dde6c22d9f340769e9f47dad47b2db0740631ff6ab59a77813` |
| [build/r27-process-restart/scene-query3-host/HOST_START.json](../../../../build/r27-process-restart/scene-query3-host/HOST_START.json) | `928948d701d0cb23483e03ae36d763e56e19869acf8777c14a06d24f22e327bb` |
| [build/r27-process-restart/scene-query3-host/HOST_EXIT.json](../../../../build/r27-process-restart/scene-query3-host/HOST_EXIT.json) | `e23ef6f61dbe3054366f3bae8682abe9b8b61ec62fec0acf5c17b983d29a23f5` |
| [build/r25-native-actionbag/process-restart-query3-preparation/FINAL_QUERY_RESULT.json](../../../../build/r25-native-actionbag/process-restart-query3-preparation/FINAL_QUERY_RESULT.json) | `baabf1035c1d80fda35c27c1fdd1c861a6beccd7ecfad96adcf943b9cb13224a` |
| [build/r27-process-restart/execution-567473f5c174bed6/RESULT.json](../../../../build/r27-process-restart/execution-567473f5c174bed6/RESULT.json) | `d91c9780007df129353b70aa962d6a77faf83926d463dbc2ce09204200d1d632` |

### Current source pins changed since the historical snapshot

Only runner and its test changed for the capture workaround: runner
`184c66aec7cd7b5ce935be5a1a5104380dda34684beefb68b614e809daa06c84`, test
`a80d82ed96192670f24602e66893ab523474d03d689345e9081dbae29a40e5ac`.
Canonical RESULT above binds all ten current sources; the older table below
remains the ACK-only historical snapshot. No Main PRE_COMMIT PASS is inferred
from the older incomplete graph check; refresh and exact scope checks remain due.

## Historical blocked snapshot (2026-10-18 13:08 UTC)

The following text and failure evidence are preserved byte-for-byte; its stop,
gate and source-table statements describe that earlier snapshot, not Q3.

Evidence snapshot: 2026-10-18 13:08 UTC. **Implementation PRESENT; fresh target
builds and reviewed host tests PASS; process restart DEVICE acceptance BLOCKED;
overall NOT VERIFIED. Ready for commit: NO.** The ten-source candidate remains
UNCOMMITTED. Device-dependent actions are paused following the reported device/
capture anomaly; no third window is authorized by this document. [Current matrix](STATUS.md).

## Intended experiment and actual implementation

The bounded experiment aims to keep A alive, exchange five String echo
roundtrips with B0, observe B0's normal exit and both retired transport lanes,
then admit one B1 replacement and exchange five further roundtrips. It uses the
existing explicitly gated native replacement experiment. The added observation
checks old/new socket ownership and listener continuity; observations do not
establish SDK callback drain or independently authorize native operations.
Default automatic reconnection remains unsupported/not enabled. This candidate
does not turn the bounded experiment into general reconnect support.

Current changes are limited to the ten source/test/build-registration files
listed in the source table below. They include the process probe, runner,
content/native evidence oracles, dual-lane observation predicate, bridge/control
integration and tests. The bridge accepts process observation only for explicitly
selected non-route bounded A. No DDS/RTPS/RMW semantic, network or permission
change was made. Existing NO_REACCEPT protection remains outside the opt-in
experiment; the failed windows did not reach replacement ARM.

Fresh isolated OFF and ON target builds each compiled 23 translation units.
OFF bridge SHA-256:
`614a93c955d8e4fb314a36d198940ae9a12b79a5416e88a5087f0ac2e2673618`;
ON bridge:
`4238b102257c7ea23ae88b9182b361617115dc37c008e2e31e04e3ffb53816a8`.
The initial implementation review found no blocking issues. Post-review host
checks covered 13 repository Python methods, 25 probe scenarios, ten runner
methods, three admission methods, C++ dual-lane observation with 32 negative
snapshots, and 12 lifecycle / 22 reaccept / two mode cases. These host/fake
controls do not prove native process restart.

## First real window: early ACK failure, later capture timeout

Scenario `5180c04d159faad2`, execution directory
`execution-145103fdf161334a`: **BLOCKED**. The fresh paired query's 40 commands
and four local continuation commands passed, once per board. A and B0 exchanged
five String echo roundtrips: 20 side-local payload records, all four copies of
each 32-byte CDR equal. Both subsequently reported `Bounded acknowledgement
failed`; neither emitted PHASE_COMPLETE. There was no accepted PRE receipt,
restart ARM, replacement or POST acceptance. B1 was deployed, never launched.

Both processes had entered CLOSE_READY by about +4.881/+5.037 s. The host's
later A command 0127 started at +34.914 s with only 0.078 s remaining and timed
out around +35.009 s. This is the surfaced tail-budget capture failure, not
proof of offline hardware. The earlier ACK exception became visible only after
gated cleanup, which obscured the business failure from polling. The exact wire
ACK/heartbeat cause was not captured and remains unproven.

There are 353 command receipts (169 A, 167 B0, 17 B1), one host timeout and a
separate A exit-race result later resolved by an ABSENT receipt. Both child
codes were 71; four owned PIDs were absent and both tokens restored. No TERM/
KILL was sent. These are **failure cleanup** observations, not normal completion.
All 5,258 original files remain preserved. There is no independent scene
HOST_START/HOST_EXIT capture: host exit 1 comes from Main's tool report
(session 45853, chunk 48266d), explicitly an attestation rather than a fabricated
file receipt. The audit's own initial helper field-name failure is retained.

## Probe-only ACK correction and retesting

The six-line probe correction changes the ACK cap from 2 s to 5 s, still clamped
by the 40 s process deadline, and emits the original failure before entering
potentially gated cleanup. ACK false remains a failure. Source inspection found
the selected Fast DDS 2.14 default periodic heartbeat is 3 s, and the previously
qualified S1 probe uses a bounded 5 s ACK cap. That supports the tested choice;
it does not prove the failed window's exact ACKNACK/heartbeat chronology.
The business/probe/normal-close bounds remain 35/40/50 s. No heartbeat setting,
reliability behavior, transport semantics, permissions or network was changed.

The repair passed fresh initial tests, independent review and canonical final
host tests: 13 repository methods, 25 probe scenarios, nine actual-communicate
ACK models and six additional review cleanup models. They are explicitly
HOST_FAKE_RCLPY_ONLY. The same native bridge and 896 compiler dependencies were
revalidated; the Python-only change did not require a new C++ rebuild. Current
probe SHA-256 is
`8d1f113b515f71a0ce766526392b0e12abed3c19f7aac7a57cdda60f5b9fa659`.
Old device packages, failures and timestamps remain immutable.

## Second real window: missing remote output marker

Scenario `6124a63b48af7b87`, execution directory
`execution-2fc1080aa99a16a7`: **BLOCKED**. Query 40 and all four original local
continuation commands exited 0. The scene has 199 command receipts: A 91,
B0 91, B1 17. All host command exits are 0, but this does not establish all
remote exits: A command `0050-A.json` returned no output or required marker.

| Failed capture fact | Observed value |
| --- | --- |
| UTC start / end | 2026-10-18 12:59:20.556605 / 12:59:30.733320 |
| Duration / command budget | 10.172 s / 15 s |
| Relative to business origin | +3.152420 to +13.329135 s |
| Host exit / stdout / stderr | 0 / 0 bytes / 0 bytes |
| Expected marker | `R25_RC_60f1b0be7cd282d2` |
| Parsed remote exit | UNKNOWN (`null`), not zero |
| Runner result | `OSError('Remote exit marker missing')`, BLOCKED |

This is an HDC remote-output capture contract failure with underlying cause
unproven. It occurred well before the 35 s business limit; it is neither that
limit expiring nor proof of a board being offline. Do not silently ignore the
missing marker, retry the window, or reinterpret host exit 0 as remote success.

Main also retained an excerpt of the existing host HDC server log for UTC
12:59:18..45 (85,119 bytes). It is auxiliary host evidence only; it cannot map
the selected entries conclusively to A0050 or establish a root cause or command
non-execution. Root cause remains unresolved. This extraction made no HDC call
and changed no service or configuration.

Both actual raw logs contain PRE PHASE_COMPLETE and five successful pre-restart
echo roundtrips (20 identical side payload records, 32-byte CDR); no bounded ACK
failure appears. The last-payload-to-phase observations were 2.584820 s and
2.586876 s. These are raw-prefix facts only: the host never produced PRE.json,
never armed restart, and never launched B1. B1's 17 receipts concern preparation,
not a replacement process. No post-restart traffic was tested.

This window has actual scene HOST_START/HOST_EXIT receipts; host exit is 1.
Both children exited 71 after WINDOW_FAILED cleanup, four owned PIDs were absent,
tokens restored, and no TERM/KILL was sent. This is not normal scenario close.
All 4,646 original files and the first window's 5,258 files remain unchanged.
Each query ran once per board and freshness anchors were not reset.

## Environment, commands and evidence

Both windows used the registered OpenHarmony RK3588A pair: A
`3e01ff55454d202020104033bf453b00`, B
`3e01ff55454d202020104433991c3b00`, the existing
`mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16` identity, SDK23/AArch64 and
project-linked CPython 3.12.7 / `rmw_fastrtps_cpp` runtime. The candidate package
contains 3,235 files with the separately built experimental bridge and selected
probe. Five full remote package hash receipts in window 2 each cover 3,235
members; listener maps bind 192 providers per active instance. Host compiler/
test commands, flags and environments are retained in the linked build/test
receipts. This is not a fresh complete ROS 2 dependency build or general ABI
requalification.

Second-window host UTC `2026-10-18T12:58:25.780680+00:00` to `2026-10-18T12:59:46.227342+00:00`, exit **1**.
Cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport`; exact recorded argv:

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r15-lifecycle\\publish-worktree\\tests\\ros2_target\\run_process_restart.py",
  "run",
  "--prepared",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r27-process-restart\\prepared-2fc1080aa99a16a7\\PLAN.json",
  "--release",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r27-process-restart\\MAIN_SCENE_QUERY2_RELEASE.json",
  "--hdc",
  "C:\\Users\\17715\\AppData\\Local\\OpenHarmony\\Sdk\\23\\toolchains\\hdc.exe",
  "--output",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r27-process-restart\\execution-2fc1080aa99a16a7"
]
```

The first-window command/exit attribution is the linked Main tool attestation,
not an independently captured host receipt. Historical consumed releases/routes
are not reusable launch instructions.


All paths below are local root-relative ignored build evidence. A source push
does not upload these artifacts or promise remote downloads. Auditors made zero
HDC calls and inspected completed original executions without replay.

| Local evidence (root-relative) | SHA-256 |
| --- | --- |
| [build/r27-process-restart/ack-fix-initial/RESULT.json](../../../../build/r27-process-restart/ack-fix-initial/RESULT.json) | `19c745035dd9193d0d379917c11d5b411250ca968116c063abe0a70a08e4b76a` |
| [build/r27-process-restart/ack-fix-initial/SEAL.json](../../../../build/r27-process-restart/ack-fix-initial/SEAL.json) | `4bcf313b33a48b174377abf5d9102b1d495a03a1b499a25d0a182e44ec901755` |
| [build/r27-process-restart/ack-fix-review/RESULT.json](../../../../build/r27-process-restart/ack-fix-review/RESULT.json) | `4fa8e979086ccee645211e53007fb2c933e176ba8384f1bf871682913259abc8` |
| [build/r27-process-restart/ack-fix-review/SEAL.json](../../../../build/r27-process-restart/ack-fix-review/SEAL.json) | `4e91d5424eb6408788e2e1d660a030fa346ff7b1cc11af0e42bc6f32ba1b688f` |
| [build/r27-process-restart/ack-fix-initial/TEN_SOURCES.json](../../../../build/r27-process-restart/ack-fix-initial/TEN_SOURCES.json) | `55187eba832ccf0b20671e25d9f4c1070f6a8f76fe547828fa2499bc923144cd` |
| [build/r27-process-restart/ack-fix-final/SOURCE_INVENTORY.json](../../../../build/r27-process-restart/ack-fix-final/SOURCE_INVENTORY.json) | `1536ad5c6438053aca508f57ede115d7e8f67532fa4958723d001dcbbff79381` |
| [build/r27-process-restart/execution-145103fdf161334a/RESULT.json](../../../../build/r27-process-restart/execution-145103fdf161334a/RESULT.json) | `402d9276631eca022091f88843a60dc89c9145a805446bfc465a1e3f436fe07e` |
| [build/r27-process-restart/device1-failure-audit/MAIN_TOOL_EXECUTION_ATTESTATION.json](../../../../build/r27-process-restart/device1-failure-audit/MAIN_TOOL_EXECUTION_ATTESTATION.json) | `f6db513d4f173b6c9ca93e2cbcf191868dfb89f30818a5723e35845de11e112a` |
| [build/r27-process-restart/execution-2fc1080aa99a16a7/RESULT.json](../../../../build/r27-process-restart/execution-2fc1080aa99a16a7/RESULT.json) | `e4acb9e1e8974733c26129ecafb8d547aa012857a1e2319ada19db7401344e23` |
| [build/r27-process-restart/scene-query2-host/HOST_START.json](../../../../build/r27-process-restart/scene-query2-host/HOST_START.json) | `1642b66d4627c0cf1330eefe307b1db8db4249d9cb12293c28b05089dca2f245` |
| [build/r27-process-restart/scene-query2-host/HOST_EXIT.json](../../../../build/r27-process-restart/scene-query2-host/HOST_EXIT.json) | `2438905287e98c86a880f2523297d2439e2645a3198e4a651a04159864f46c90` |
| [build/r27-process-restart/device2-failure-audit/RESULT.json](../../../../build/r27-process-restart/device2-failure-audit/RESULT.json) | `697c736ee1b87d5db32c21c37be58d2ffd89ed23ba84c42bbd465a95fa5237e7` |
| [build/r27-process-restart/device2-failure-audit/SEAL.json](../../../../build/r27-process-restart/device2-failure-audit/SEAL.json) | `984231919d7d52ce8461c48ef6a6dab1ca3b4235cfdc8d5e1549e84ff57fecd3` |
| [build/r27-process-restart/ack-fix-final/RESULT.json](../../../../build/r27-process-restart/ack-fix-final/RESULT.json) | `56b0f40a2782c3e4e85789e96ee08b6deb3fe0d78c79bcc81b0d85feba1595a8` |
| [build/r27-process-restart/ack-fix-final/SEAL.json](../../../../build/r27-process-restart/ack-fix-final/SEAL.json) | `e1a923d9ccd096215166f6beffcda382ca004cb99d2305016da89a89bdd5214d` |
| [build/r27-process-restart/device1-failure-audit/RESULT.json](../../../../build/r27-process-restart/device1-failure-audit/RESULT.json) | `941dee2fbd1c99bc1afe30c94e06a7d2e866a439b64cef603f8f4ee65901bf34` |
| [build/r27-process-restart/device1-failure-audit/SEAL.json](../../../../build/r27-process-restart/device1-failure-audit/SEAL.json) | `c08cc3e80b0fbaa95f0fade6e802064c7d0e9a19fad8c600bbab9c757854a17e` |
| [build/r27-process-restart/initial-fix1-tester/BUILD_RECEIPT.json](../../../../build/r27-process-restart/initial-fix1-tester/BUILD_RECEIPT.json) | `de61f0f053705d8f90fc93373315589dd7bcae040f07a861e5b82d8ccf3f30a1` |
| [build/r27-process-restart/initial-fix1-tester/RESULT.json](../../../../build/r27-process-restart/initial-fix1-tester/RESULT.json) | `480f1d2cccde3103b8b950a90a21175c4c8165073bcee6244e335434d756b3cb` |
| [build/r27-process-restart/implementation-review/RESULT.json](../../../../build/r27-process-restart/implementation-review/RESULT.json) | `26f03f23a5207fbb188e4dff495465cb5fc4c227c8cd5c74a6296e9d14382a74` |
| [build/r27-process-restart/final-host-tester/RESULT.json](../../../../build/r27-process-restart/final-host-tester/RESULT.json) | `da5d80d955b7a12022c558fb3881480be0f59d3b878321ca70c1dc190ddbc374` |
| [build/r27-process-restart/ACK_WINDOW_DIAGNOSIS.md](../../../../build/r27-process-restart/ACK_WINDOW_DIAGNOSIS.md) | `60076bc69c9230c19c30639227c8fcd9a2ae4b5ea4ed2f341f48449894a471fd` |
| [build/r27-process-restart/main-graph-user/GRAPH_ALL.json](../../../../build/r27-process-restart/main-graph-user/GRAPH_ALL.json) | `5d7ff6440f0cc5e012de347ab89eab1dd7b3a22e66880eb1bd81a3e87a1f5275` |
| [build/r27-process-restart/main-hdc-diagnostic/EXTRACTION.json](../../../../build/r27-process-restart/main-hdc-diagnostic/EXTRACTION.json) | `1868c85d160208b40c4de64e1f7d2fbd5345222a79ca9106ef9cefabcde0dace` |
| [build/r27-process-restart/main-hdc-diagnostic/HDC_SERVER_WINDOW.log](../../../../build/r27-process-restart/main-hdc-diagnostic/HDC_SERVER_WINDOW.log) | `0f124e93f68b7534f9cb24465d7906fd40538b690fbd7acad0b411cd7325db98` |
| [build/r26-sustained-restart/main-sustained-publication/POST_PUBLISH.json](../../../../build/r26-sustained-restart/main-sustained-publication/POST_PUBLISH.json) | `f6c4fadcac4d1c5607fd5cfa48a2ad4cde0b48b74baede6b20df03fb2e4abc4a` |

Current ten-source inventory (candidate bytes, not a completion claim):

| Source | SHA-256 |
| --- | --- |
| `tests/ros2_target/run_process_restart.py` | `9c00f82be9fe765a69102bb202a713022a83131c5a481b9306bef2c5041efabd` |
| `integrations/ros2/target/process_restart_probe.py` | `8d1f113b515f71a0ce766526392b0e12abed3c19f7aac7a57cdda60f5b9fa659` |
| `tests/ros2_target/process_restart_oracle.py` | `96d1fce09046083cefe02834481a5e43dc88cf8b73d190ddce4c9b1385d2d611` |
| `tests/ros2_target/process_restart_native_oracle.py` | `8aa49ae3649e707ab8296567eb7396fa99953b4ba1a32cc21229e0d2e65883c7` |
| `tests/ros2_target/test_process_restart.py` | `f7e634b335bc4e257ec24b990486804ce65b93a2b25c1b49dc07fbf39ab5b0b4` |
| `tests/ros2_target/process_restart_observation_tests.cpp` | `5de84e8636c15011d65d00afbad8e977338427954eb87d186c30b181fca5fa27` |
| `integrations/ros2/BoundedNativeControl.hpp` | `7ef2a0e6e0faee523399b4f1d7e6cada57306ed8bf8e3c30a173ee9ba76c5249` |
| `integrations/ros2/ProcessRestartObservation.hpp` | `317c88f526f3269e2c4874d32b164c7a7729e6527a35deee198b4e11b1a3f87f` |
| `integrations/ros2/target/TargetBridge.cpp` | `d68ca8ec337b1cbdb5f09c165ccf3e26667e13c6e3d24bb8a891874eb7955e47` |
| `CMakeLists.txt` | `c30caf32ba3ceef4bad3fe9309b89b404c30fa82866e9a00b609eff310f73ed1` |

## Current capability matrix and next step

| Capability | Current status and reason |
| --- | --- |
| R27 candidate implementation/build | PRESENT / BUILDS PASS for fresh OFF/ON bridge and host checks. Ten files remain uncommitted. |
| Reviewed ACK repair | HOST_TESTED PASS; model/fixture evidence only. Second device prefix no longer shows the ACK failure, but full scenario is still BLOCKED. |
| Native five-echo pre-restart prefix | Observed on both windows; second has raw PRE phase completion. Prefix evidence does not grant full PRE/restart acceptance. |
| Surviving-A / B restart | DEVICE acceptance BLOCKED; no ARM, no B1 launch, no POST traffic. No successful process replacement demonstrated. |
| Default automatic reconnect | Not enabled/supported by the current default; bounded opt-in experiment remains unqualified. Not a claim of permanent technical impossibility. |
| Normal scenario close | Not demonstrated by these failed windows; failure cleanup/identity restoration is recorded separately. |
| SDK callback epoch/quiescence, same-process Participant recreation | NOT_PROVEN. No upgrade from host or prefix observations. |
| Prior S1 / Action / ordinary CLI / rosbag | Earlier bounded qualifications retain their own evidence; R27 does not invalidate or broaden them. |
| Arbitrary services/actions, general QoS, long-duration/concurrent/large-message behavior | NOT VERIFIED by this stage; no corresponding acceptance. |
| Publication gate | BLOCKED; Ready for commit NO. No normal R27 commit/push; overall ROS 2 NOT VERIFIED. R22 remains STOPPED. |

Next work is paused at the device/capture anomaly. Preserve and diagnose the
missing-output receipt and available host-side capture evidence first. Do not
start a third window or change board/network/security state to manufacture a
pass. Once the blocker is resolved and dependent device work is explicitly
resumed, use a fresh separately reviewed window and original admission/budget
rules to test the actual B0 exit/B1 replacement and numbered post traffic. Any
new source correction requires its own tests/review/retest; no relaxation of
remote-marker validation, freshness or lifecycle gates is implied.

Graph status is not a PRE_COMMIT pass. The refreshed index previously contained
20,344 nodes, 55,401 edges and 363 flows, with global traversal truncation
(1,378 entries / 2,744 callees). `main-graph-user/GRAPH_ALL.json` covered only
four tracked files, including unrelated R22; untracked candidate files were not
complete change coverage. ACK was changed after that index. The earlier sandbox
EPERM graph failure remains under `main-graph/`; the normal-user rerun under
`main-graph-user/` succeeded within its recorded limits. No staged full-scope
graph/PRE_COMMIT is performed while this stage is blocked.

S1 was published as `9cdbc84ed1371d53f4ee53b88dad5b3e5f1612ab` to existing
`origin/codex/flow-encoding-published-20261014`. Exact-SHA CI at
2026-10-18 11:45:04.427145 UTC was NOT_RUN (zero runs/checks/status contexts).
This R27 candidate has no publication or new CI result.
