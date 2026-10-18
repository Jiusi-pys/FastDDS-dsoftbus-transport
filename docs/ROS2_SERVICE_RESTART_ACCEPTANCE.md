# R29 bounded true service restart acceptance

## EOF qualification addendum (2026-10-18 18:06 UTC pre-commit snapshot)

Current host runner SHA-256 is
`508ab7ca158a2cff2f2bd95810a95897419ed3def45162f7a5125d790c385e5f`.
The original real Q1 executed
`8c6fea0114614c5d81e70c813bc9c44579ee388a8067511ce1e7c7ba43bf279f`.
Only one terminal LF was removed after staged whitespace checking failed;
the failure and original bytes remain preserved. **The new runner hash was not
device-tested.** The original finite service facts remain bound to the old hash.

Independent initial, review and final stages passed. AST including positions,
all47 recursive code objects and compiled marshal bytes match. Post-review
host execution passed43 repository methods,12 workflow deadline cases,9 runner
methods including execute/self-hash controls, and4 constructor admissions.
The old receipt correctly rejects the current source hash. A new immutable
BUILD_RECEIPT was accepted by checked_bridge and real compose/checked_package;
all3,236 newly derived payload files are byte-identical to the original deployed
package. API23 ON/OFF artifacts and897 dependencies were reused, not rebuilt.
These establish limited evidence inheritance, not a new device run. The original
2,092 raw files and295-member device audit remain unchanged.

Technical tester gate PASS now includes this EOF equivalence qualification.
Documentation Ready for commit YES; Main restage/graph/PRE_COMMIT/publication
remain pending **at this 18:06 UTC snapshot**. Actual final commit/remote/CI is
recorded separately in root-relative
`build/r29-service-restart/main-service-restart-publication/POST_PUBLISH.json`.
Main's current four-source manifest is
`build/r29-service-restart/main-service-restart-publication/IMPLEMENTATION_FILES_EOF.json`;
the original IMPLEMENTATION_FILES.json and source hashes below describe the
original real-device revision. No future manifest or publication result is
assumed before Main writes it. Default reconnect, SDK epoch/drain, action
recovery, soak/leak freedom and full ROS2 remain unqualified.

| EOF evidence (local ignored files) | SHA-256 |
| --- | --- |
| [build/r29-service-restart/eof-final-tester/RESULT.json](../../../../build/r29-service-restart/eof-final-tester/RESULT.json) | `01dc0bbf97a1ffab070f89b66dcd126216b87edc175c37885723c58cbb699b0f` |
| [build/r29-service-restart/eof-final-tester/SEAL.json](../../../../build/r29-service-restart/eof-final-tester/SEAL.json) | `d79c01f7ba1e63bb83a201085bb5662715a371a1f4fcdd3d2fdc2ebaa86b9ab5` |
| [build/r29-service-restart/eof-final-tester/BUILD_RECEIPT.json](../../../../build/r29-service-restart/eof-final-tester/BUILD_RECEIPT.json) | `860b808301a6cfcd78dcca067dce4f10a14c18487556aec1c3119e291e1b050d` |
| [build/r29-service-restart/package-1d3f139c844aed53/DERIVATION.json](../../../../build/r29-service-restart/package-1d3f139c844aed53/DERIVATION.json) | `2a715b6456d5e194e4340eca5aa0b61ab11c28e525b764488386658316454df2` |
| [build/r29-service-restart/eof-final-tester/EQUIVALENCE.json](../../../../build/r29-service-restart/eof-final-tester/EQUIVALENCE.json) | `c7b3b5c0c1afa5a903fd4b37ffe5d7b264d9c2fa84320fe31e1f466d84040ddb` |
| [build/r29-service-restart/eof-final-tester/PACKAGE_EQUIVALENCE.json](../../../../build/r29-service-restart/eof-final-tester/PACKAGE_EQUIVALENCE.json) | `5c62f5ebd098906b610f707a101c7005c3a528264a57a1c3ddcd0b6a6901776b` |
| [build/r29-service-restart/eof-initial-tester/RESULT.json](../../../../build/r29-service-restart/eof-initial-tester/RESULT.json) | `7e0b721d78aaa5e34df7721dbcf335e52734e5045e714b97328f162ebdc2de74` |
| [build/r29-service-restart/eof-review/RESULT.json](../../../../build/r29-service-restart/eof-review/RESULT.json) | `64e0cd84202d90978999c54cad7be67462f720827e0fdc555138ef5459d3297a` |
| [build/r29-service-restart/eof-finalization/FAILURE.json](../../../../build/r29-service-restart/eof-finalization/FAILURE.json) | `2ac77bcf624113bfd645e397891e0f6460d5096d8a17b8f2c2230433cab428cd` |

Snapshot: 2026-10-18 17:51 UTC. **IMPLEMENTED / HOST_TESTED / DEVICE_TESTED /
CROSS_DEVICE_TESTED PASS**, only one explicit normal AddTwoInts server replacement
with one persistent A client and distinct PRE/OLD/NEW requests. Native build
qualification is reused, not a new build. Independent technical tester gate PASS;
documentation Ready for commit YES. Overall ROS 2 NOT VERIFIED.

R29 gate/publication/CI pending below means the 2026-10-18 17:51 UTC pre-commit documentation snapshot. Final commit, remote and CI results are recorded separately at root-relative `build/r29-service-restart/main-service-restart-publication/POST_PUBLISH.json`; no future SHA or CI success is prefilled.

## Actual source and real service behavior

Four new files only: target service_restart_probe.py and tests/ros2_target/
{service_restart_oracle,run_service_restart,test_service_restart}.py. Current
hashes are in IMPLEMENTATION_FILES below. Existing R27 single-reset primitives
are reused; process_restart_cycles is omitted. No RTPS/DDS/RMW semantics, native
safety configuration, Socket ownership limits or default reconnect changes.

The unique real Q1 scenario `1398f5a5c90cbd08`, execution `e3ce620975f5a132`, used
RK3588A A `3e01ff55454d202020104033bf453b00` and B
`3e01ff55454d202020104433991c3b00`. Actual CPython312 aarch64-OHOS imports,
five module origins/hashes, Service/Request/Response capsules and native request/
response serialize-deserialize witnesses precede Node creation. Real rclpy
create_client/create_service use rmw_fastrtps and native DSoftBus Socket; no topic
echo substitutes for service. Selected QoS: RELIABLE/VOLATILE depth10.

Three requests were each issued once, with distinct operands and Future
generations. PRE completes on B0; after B0 pauses, OLD remains pending. B0 exits
normally and restores its token; child/supervisor absence and observed service
disappearance precede B1 launch. B1 has a new run/root/PID/starttime/Participant
GUID. NEW then completes on B1. PRE/NEW each have equal peer request and response
CDR: two completed pairs and nine side payload records (eight completed-pair
copies plus one OLD request), not three successful calls. No automatic retry.

No OLD response was observed on either server or A. Cleanup invokes
remove_pending_request and cancels the local Future; **this is not remote
cancellation**. Python Future addresses are not exported; actual bound source,
distinct call_async generations, operands and CDR establish OLD/NEW accounting.
A retains its client/Node, Runtime `545988765264`, Participant
`010f5a585734e5be00000000000001c1`,20 endpoints and exact service request-writer/
reply-reader GUIDs (`...1503` / `...1404`).

Native old/new observations preserve listener1, retire IDs2/3 with owners4/12,
and admit IDs4/5 with owners43/46. Observed on-shutdown callbacks do not prove
SDK epoch/drain. Real system libsoftbus_client and selected provider mappings
are verified; fake_sdk0, builtin transports/intraprocess disabled and endpoint
data-sharing OFF. Native send/ingress/wire evidence is structural, not direct
one-payload-to-one-RTPS sequence mapping or a system-wide packet capture.

## Timing, lifecycle and commands

All three children, interpreters and finalizers returned0; tokens restored,
six owned PIDs absent, no TERM/KILL. ARM/OLD/REPLACE/POST and finish/close controls
each occur once; optional read-only empty-capture resample was unused. All361
scene host commands returned0/no timeout:337 shell remote markers0 and24 transfers
without a fabricated remote-shell exit claim. Query40 plus4 local commands
passed, preserving the original query timestamp.

Business12.516/35 s; normal close release19.969/50; final exit1.031/20;
analysis acceptance4.125/60. These include HDC control/capture, not network latency.
Scene query age51.9005 s and B1 launch age110.82671 s satisfy separate dispatch240/
construction300 limits. Probe40 is retained; observed NODE_READY-to-FINAL spans
A17.821931791/B04.428557737/B18.308521334 s are below40, without inventing an
unexported create_node-return timestamp.

Windows host used the project Python and SDK23 HDC; actual host argv follows,
from ROOT cwd, exit0. Exact target commands/exits and captured environment are
in QUERY_AND_HOST/COMMAND_AUDIT and stage command records.

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r15-lifecycle\\publish-worktree\\tests\\ros2_target\\run_service_restart.py",
  "run",
  "--prepared",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r29-service-restart\\prepared-e3ce620975f5a132\\PLAN.json",
  "--release",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r29-service-restart\\MAIN_SCENE_R29_QUERY1_RELEASE.json",
  "--hdc",
  "C:\\Users\\17715\\AppData\\Local\\OpenHarmony\\Sdk\\23\\toolchains\\hdc.exe",
  "--output",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r29-service-restart\\execution-e3ce620975f5a132"
]
```

Formal package has3,236 files. Scene logs were collected after owned PID absence:
pre-transfer size, successful receive and local size, then matching remote SHA256.
Scene collection did not take a pre-transfer hash; the independent query did
use matching before/after hashes. The audit preserved2,092 original evidence
files. Consumed release/query files are evidence, not reusable dispatch permission.

## Test, review and preserved failures

Initial host FAIL T01/T02 remains immutable: missing AddTwoInts module/capsule/
deserialize qualification and persistent service writer/reader anchors. These
were acceptance gaps, not observed device communication failures. Fix1 added
actual type support qualification and service-endpoint continuity checks.
Initial fix1 passed; independent implementation review found no blocking findings;
post-review final host freshly passed R29 16/R27 15/R28 12 repository methods,
27 type-support API-shaped cases,13 communicate,12 deadline boundaries,48 strict
oracle cases,9 runner methods,4 constructor/type-order and12 reviewer controls.
Host ABI/capsule fixtures and synthetic CDR records remain host evidence; the
real target imports/service execution were separately qualified by Q1.

API23 ON/OFF bridge/archive/compile-command hashes and897 compiler dependencies
were verified and reused. R29 did not recompile or re-execute these binaries as
host tests. No native compiler was needed for this four-Python-file addition.
The 207-member final-host seal and 295-member device audit bind current sources.

Three independent audit-helper failures remain preserved: audit.py wrongly
required two scene hashes; audit_fix1.py omitted `_EXPERIMENTAL` in a backend
literal; finish.py used the wrong finish-marker prefix. Corrected helpers used
the frozen actual collector/configuration/marker contracts. These were local
audit script errors, not device FAILs, new device runs or production fixes.
An exploratory nonexistent query filename lookup is also retained as a tool
lookup error. Original evidence was not rewritten.

## Capability matrix and next step

| Capability | Qualification |
| --- | --- |
| True service-server normal replacement | Supported within this tested explicit one-replacement AddTwoInts path. |
| General service recovery | Partial: only this type/QoS and finite request sequence; concurrent service load, arbitrary QoS/types and repeated failures remain untested. |
| OLD request cancellation | Local pending removal/Future cancellation only; remote cancellation not implemented or claimed by this experiment. |
| Default automatic reconnect / SDK epoch-drain | Not established / NOT_PROVEN. Existing native safety boundaries unchanged. |
| Action recovery / soak / leak freedom | NOT_TESTED / NOT_TESTED / NOT_PROVEN. |
| Unique flow / strict / dedicated SEDP | NOT_PROVIDED / UNTESTED / NOT_QUALIFIED. Known unique-flow diagnostics remain visible. |

Prior bounded Action, CLI, rosbag and finite pub/sub replacement qualifications
remain valid within their scopes; they do not establish action recovery. R22
STOPPED; full ROS 2 NOT VERIFIED. Next: Main gate/publication, then a separately
reviewed bounded action-recovery or other priority capability test with fresh
device admission; preserve existing35/40/50/20/60 and query300/240 boundaries.

## Sealed local evidence

Local ignored build links are not a promise of remote-download availability.
R28 published HEAD is `65ec59cffdd0ad4b0d9991b713a0a2741522f5e3`; its exact-SHA
CI was NOT_RUN at16:54:36.469429 UTC. R29 publication is separately recorded as
described in the dated snapshot above.

| Local evidence | SHA-256 |
| --- | --- |
| [build/r29-service-restart/device1-final-audit/RESULT.json](../../../../build/r29-service-restart/device1-final-audit/RESULT.json) | `946a4984970c7c31c40b773b494c59d12b00f56e84695158da56118517955ead` |
| [build/r29-service-restart/device1-final-audit/SEAL.json](../../../../build/r29-service-restart/device1-final-audit/SEAL.json) | `51cfe218f7835d6f6fd87871b9b36493252952f41faecd5ff9852327c4320fb6` |
| [build/r29-service-restart/device1-final-audit/APPLICATION_CDR.json](../../../../build/r29-service-restart/device1-final-audit/APPLICATION_CDR.json) | `56e6774c3278a8499fc5caae1219a6b8c68b86dfa24571557fb3ae5452d3a289` |
| [build/r29-service-restart/device1-final-audit/TRANSPORT_AND_CONTROLS.json](../../../../build/r29-service-restart/device1-final-audit/TRANSPORT_AND_CONTROLS.json) | `24e114de59db3fef1609e3f64a39ec67b77489bb424f4d679d546a0a5d90735b` |
| [build/r29-service-restart/device1-final-audit/TIMING.json](../../../../build/r29-service-restart/device1-final-audit/TIMING.json) | `07ec92ca19d7d097aa5d3c62e0bbf0fe8ea6c8a3fce4f6479bd95bd53a36d6c8` |
| [build/r29-service-restart/device1-final-audit/QUERY_AND_HOST.json](../../../../build/r29-service-restart/device1-final-audit/QUERY_AND_HOST.json) | `189bc01bec640b89afe212f305bf3cddbf982c0f4274b38f5c517a62fb94fcde` |
| [build/r29-service-restart/device1-final-audit/COMMAND_AUDIT.json](../../../../build/r29-service-restart/device1-final-audit/COMMAND_AUDIT.json) | `a64a745510974e4acca5c66267d1dda0460202847de2c403e418a466237fa1af` |
| [build/r29-service-restart/device1-final-audit/RAW_HASHES.json](../../../../build/r29-service-restart/device1-final-audit/RAW_HASHES.json) | `cf34f5653003f556e2a4748b936842760f5e85bc80b7065cfda20d0e16392637` |
| [build/r29-service-restart/initial-tester/RESULT.json](../../../../build/r29-service-restart/initial-tester/RESULT.json) | `02569b0e9ca30d7c70d4dd61671b6f5e2d31c5eb2be14c432e082512d93e2e14` |
| [build/r29-service-restart/initial-fix1-tester/RESULT.json](../../../../build/r29-service-restart/initial-fix1-tester/RESULT.json) | `f81a6d2223f92bd3338cf27cba9f11739293c688d61c12021596c97ec549bf03` |
| [build/r29-service-restart/implementation-review/RESULT.json](../../../../build/r29-service-restart/implementation-review/RESULT.json) | `dab29deb5f123d60ba6d325469255b3c9a2f882e9c788e6eb7b5e493a370b3f5` |
| [build/r29-service-restart/final-tester/RESULT.json](../../../../build/r29-service-restart/final-tester/RESULT.json) | `1ab0ac21dc3c4adbbbd167dd5af055ffab4ee750d3ca28ddb7572265e160a71d` |
| [build/r29-service-restart/final-tester/SEAL.json](../../../../build/r29-service-restart/final-tester/SEAL.json) | `208f921119d815696b623342e03a0a097b83466a09ddfcd7c8d9a74c4d493c7d` |
| [build/r29-service-restart/device-plan-review/RESULT.json](../../../../build/r29-service-restart/device-plan-review/RESULT.json) | `920c19d78d2f44a5935d82eee914d19645a4a55e8c71fc5b63529315a58772e6` |
| [build/r29-service-restart/execution-e3ce620975f5a132/RESULT.json](../../../../build/r29-service-restart/execution-e3ce620975f5a132/RESULT.json) | `ea73ea1f75b23ff0cf13c3a23a3cb423f64a19fe58e885a47404c0e23e0deaa1` |
| [build/r29-service-restart/package-ab5881038de7217f/DERIVATION.json](../../../../build/r29-service-restart/package-ab5881038de7217f/DERIVATION.json) | `42788a1723f2880794b53963d5fa7b8d1c4eab9af56e33dd2575489608ad57e1` |
| [build/r29-service-restart/main-service-restart-publication/IMPLEMENTATION_FILES.json](../../../../build/r29-service-restart/main-service-restart-publication/IMPLEMENTATION_FILES.json) | `d0b22f3f7a7f4fa5268a627877940d799e8333fc4455ed81886bd170e7a7a986` |
| [build/r28-repeated-restart/main-repeated-restart-publication/POST_PUBLISH.json](../../../../build/r28-repeated-restart/main-repeated-restart-publication/POST_PUBLISH.json) | `d980fc30e57072e65f666cc9ff8a5ef9dce011989608d8fe8a12439eb2598fb8` |
