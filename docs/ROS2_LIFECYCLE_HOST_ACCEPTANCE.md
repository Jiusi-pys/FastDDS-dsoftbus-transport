# Windows Host ROS lifecycle acceptance (R15)

R15 qualifies a bounded remote ROS lifecycle scenario on the published
`default-2145` Windows Host package: Fast DDS 2.14.5, SECURITY OFF / STATISTICS
OFF. Implementation is PRESENT; the fresh standalone build and final independent
Host tests PASS. The tester's `Commit gate: PASS` is limited to this Host
acceptance tooling. DEVICE_TESTED=false, native/cross-device testing remains
BLOCKED, and ROS over native DSoftBus remains NOT VERIFIED. Main's graph,
PRE_COMMIT, commit, push and CI are separate pending receipts at this snapshot.

The baseline is `60d205eed57087ae2d840260aaf7e914745955cf`. Four added files supply
the standalone C++17 consumer, CMake entry, runner and model tests. No Transport,
DDS/RTPS, RMW, shared validator, dependency, security-policy or native source is
changed. The consumer links the installed factory configuration and rejects a
profile other than `default-2145`. See [Host integration](ROS2_HOST_INTEGRATION.md)
and [historical profile qualification](ROS2_HISTORICAL_HOST_PROFILE.md) for the
distinct package boundaries.

## Executed scenario and capability limits

Two OS processes use real ROS services and events. A owns two
`rclcpp_lifecycle::LifecycleNode` instances (`recover` and `fatal`); B calls
`ChangeState`, `GetState`, `GetAvailableTransitions` and subscribes to
`TransitionEvent`. Intra-process delivery is disabled. Files coordinate START,
business release and runtime close only; they do not carry lifecycle requests
or responses. DDS traffic crosses the TCP-backed Host fake DSoftBus Socket seam,
not an OpenHarmony Socket API implementation.

The fixed sequence is:

1. Snapshot `recover` as unconfigured and reject an unknown transition label
   without a state transition.
2. Configure callback returns FAILURE, leaving it unconfigured; the next configure
   succeeds. Activate, deactivate and cleanup succeed.
3. Configure returns ERROR; successful error recovery returns to unconfigured.
   Configure then succeeds again, followed by inactive shutdown to finalized.
4. Snapshot `fatal` as unconfigured; configure returns ERROR and failed error
   recovery takes it to finalized.

The oracle checks all ten change responses, twelve state snapshots, twelve
available-transition snapshots, twenty ordered transition events and eleven
callback receipts, ending with two finalized nodes. It validates exact
run/side/PID, unique fields, local ordinals, callback order, request/state/event
relationships and allowed transition sets. It permits a service response before,
between or after the corresponding asynchronous events while requiring their
order and the following snapshot. This does not correlate transport payloads by
timestamps or local operation numbers. The 250 ms event drain is bounded and
does not prove absence of arbitrarily delayed duplicates.

The tested ROS implementation emits raw transition-event ID zero here; request
IDs and ordered start/goal states are checked separately. Exact-tag rclcpp
28.1.16 and rcl 9.2.9 source were inspected during review, and the oracle was
corrected before execution. Package metadata/tag names do not establish complete
source-to-DLL reproducibility. The unknown-label case avoids treating the
original invalid-ID callback-return concern as a validated behavior.

| Capability | Status | Reason / limit |
| --- | --- | --- |
| Remote lifecycle services and transition events | Supported for the fixed Host scenario | Actual two-process ROS execution, exact response/state/event oracle |
| Callback FAILURE, later SUCCESS, activate/deactivate/cleanup | Supported for the fixed Host sequence | Independent callback receipts and remote state/event checks |
| ERROR recovery success and failure, finalized nodes | Supported for the tested branches | Both recovery outcomes and two finalized states observed |
| Lifecycle feature coverage overall | Partial | One serialized scenario with two nodes; no full lifecycle conformance claim |
| Managed publisher activation gating | Not tested by R15; independent R16 finite Host acceptance is partial | No managed publisher workload in this consumer. See [R16 managed publisher acceptance](ROS2_LIFECYCLE_PUBLISHER_HOST_ACCEPTANCE.md) for the separate two-overload, matched-window evidence and limits |
| Thrown callback exceptions and all shutdown origins | Unverified | Tested return codes and inactive shutdown do not cover those branches |
| Concurrent transitions, restart/reconnect and stress | Unverified | Single-threaded executors and one outstanding service future |
| Security-enabled lifecycle | Unverified | Uses default SECURITY OFF package; R13 does not supply lifecycle evidence |
| Native DSoftBus, RK3588A and cross-device lifecycle | BLOCKED / unverified | No device run; owner permission blocker remains |
| Graceful cleanup after runner mapping failure | Unverified | Synthetic failures exercise fail-closed forced cleanup, not graceful runtime teardown |

## Independent tests, review and final results

The initial independent phase built a fresh consumer, passed eight model test
methods and ran one real Host pair: run `c5dcaa8877b1b07c`, A PID 43788 / B PID
39176, both exit 0, ports 52684/52685 released for exclusive rebind, no forced
kill. It observed the same 10/12/12/20/11 business counts and two finalized nodes.
A admitted all 142 ingress callbacks; B admitted 186 of 196, with ten adapter
refusals retained. This scoped initial PASS is historical evidence, not proof
of the subsequent runner fix.

The initial provenance seal exited 1 for ten CRLF/LF source-hash differences.
`initial-test/SOURCE_PROVENANCE_RESOLUTION.json` preserves that failure and
resolves the source comparison against the original frozen snapshot and HEAD
blob using explicit line-ending equivalence. R13 provenance covers thirteen
source files; this normalized comparison is not used for binaries, dependencies
or final before/after source guards, which require raw SHA-256. The failed seal
was not silently relabeled as a passing command.

Independent agentic review found one blocking P2, R15-C1: the old START predicate
used a cumulative module map and allowed a child already exited with code zero.
Synthetic counterexamples triggered START after disjoint incomplete scans or
with an exited peer. They did not demonstrate a complete false PASS, and no such
event was observed in the initial real run. The fix keeps history separate,
requires all exact paths/hashes in the current iteration and requires both
`poll()` results to be `None` immediately before START. The barrier records that
current snapshot. Independent closure reports CLOSED; sequential observations
are not atomic guarantees of process liveness, module residency or tamper
protection. Mapping exceptions still fail closed through forced cleanup, and
missing/truncated result files must be treated as failure.

Final verification used a new source freeze and fresh MSVC x64 Release `/MD`
C++17 build. Nine model methods passed, including eleven current-barrier
synthetic controls. Another 60 related parser cases passed: receive capture 31,
I/O capture 7, analyzer 15, native analyzer 6 and control files 1. These are
synthetic/offline tests, not additional ROS or native windows. The independent
closure also retained four original reviewer regression cases and rejected
three in-memory mutants. No new shared-core or device regression is claimed for
these additive acceptance files.

The single final real Host window is run `1fc377841d9c9a77`, A PID 77236 / B PID
73444, both exit 0. It passed the full business oracle, two connection joins,
runtime close/pump join and process cleanup. No process was killed and no timeout
occurred; ports 61361/61362 allowed exclusive rebind. At START the current module
snapshot contained twelve selected modules on A and ten on B, and both polls
were `None`. These are selected exact module bindings, not every process mapping.

| Final transport observation | A | B |
| --- | --- | --- |
| Sends / ingress callbacks | 194 / 148 | 148 / 194 |
| Admitted, charged, refunded | 148 / 148 / 148 | 177 / 177 / 177 |
| Adapter refusals | 0 | 17 |
| Send errors | 0 | 0 |
| Closing receive terminations | 2 SOCKET_ERROR / WSA10004 | 2 EOF |

B's seventeen rejected callbacks retain original ingress/socket/epoch/handle/
size identity and decisions: Adapter layer, AdapterPhase branch,
Closing phase, InvalidState status, `bridge_called=0`. They are pre-Bridge
refusals, not charged records. `full_host` reconciliation and endpoint/counter
coverage passed, but completeness does not mean all callbacks were admitted,
zero errors, or known payload causality. Payload causality remains
`NOT_OBSERVED`. Successful lifecycle business does not explain the contents or
necessity of those rejected frames. Exact rows are retained in
`final-test/ADAPTER_REJECTION_DETAILS.json` and the runtime logs.

## Reproduction records and artifacts

Paths beginning `build/` below are relative to the outer project root
`C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`, not W3. Local ignored
evidence is not promised to exist in a fresh clone. Canonical execution records
are under `build/r15-lifecycle/final-test`:

- `configure.intent.json`, `build.intent.json`, `models.intent.json` and
  `regression-*.intent.json` record exact argv, working directory, environment,
  deadline and associated stdout/stderr/result records.
- `runtime-launch.intent.json` records the Python runner launch;
  `runtime/result.json` records both actual child commands, effective environment
  deltas, run, PIDs, modules, input hashes, business results and cleanup.
- `CURRENT_BARRIER_VERIFIED.json`, `ADAPTER_REJECTION_DETAILS.json`, raw runtime
  logs and `RAW_BUILD_MANIFEST.json` retain the supporting observations.

The exact final consumer build was:

```powershell
& 'C:/pixi_ws/.pixi/envs/default/Library/bin/cmake.exe' --build 'C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport/build/r15f/b' --config Release --target ros2_lifecycle_acceptance -- /m:1 /nr:false
```

Configure used snapshot `build/r15f/source/examples/ros2_lifecycle_acceptance`,
Visual Studio 17 2022/x64, `CMAKE_CONFIGURATION_TYPES=Release`,
`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL`, installed Host package
`build/r13pf/dp`, DDS `build/g2a1/host214-install`, Fast CDR package
`C:/pixi_ws/ros2-windows/Lib/cmake/fastcdr`, base ROS and pixi prefixes.
The runtime launcher used Python `C:/pixi_ws/.pixi/envs/default/python.exe -B`
with snapshot `tests/ros2_host/run_lifecycle_acceptance.py`, `--exe` pointing to
the fresh consumer, `--bridge` and `--host-prefix` pointing to `build/r13pf/dp`,
`--fastdds-prefix build/g2a1/host214-install`,
`--ros-prefix build/ros2-target/h3/runtime`, the pixi prefix, and output
`build/r15-lifecycle/final-test/runtime` (all paths absolute in the record).
RMW is `rmw_fastrtps_cpp`, domain 201. The wrapper bound is 80 seconds and the
runner window is 65 seconds; business waits remain bounded with no business
retry. The exact PATH and removed/overridden environment keys are recorded in
the launch/result files; no machine network/security settings were changed.

| Artifact | SHA-256 |
| --- | --- |
| `build/r15f/b/Release/ros2_lifecycle_acceptance.exe` | `3d043acff89d5e73183006d4036a07d23363853ce857a2a650cbe7bbf439ef02` |
| `build/r13pf/dp/bin/fastdds_dsoftbus_ros2_host_bridge.dll` | `08945ea94381ac7264ad07c8c0818c1e717d847284d2402bdabd37c948c87e63` |
| `build/g2a1/host214-install/bin/fastrtps-2.14.dll` | `60408a27139ce59bf9aaa92c080e799d98f1531b7318dfbf0abf7308e7d555a4` |
| `build/ros2-target/h3/runtime/bin/fastcdr-2.2.dll` | `badcea3777c7a6b23961310dc5857e07c064d94af9babe52d2c097d8842d8c71` |
| `build/ros2-target/h3/runtime/bin/rclcpp_lifecycle.dll` | `d21b1acc1b2250a9ecfe63ab2bc3a1667b3bc7e5f0bf2905dcdef7743456efd9` |
| `build/ros2-target/h3/runtime/bin/rcl_lifecycle.dll` | `0f07dc491f687e5bfa50aa04b5b0a1adda96cf1091586fd012c446863fb317f1` |

Full RMW, rclcpp and lifecycle type-support module paths/hashes are in the current
barrier and runtime input records. Final guards cover 706 source files including
the four candidates, 329 dependency records, 45 published package files and three
selected tools (Python/CMake/CTest). That tool guard is not full MSVC/OS
reproducibility. The 72 initial, 99 review and 100 closure output seals remain
unchanged.

| Canonical receipt under `build/r15-lifecycle` | SHA-256 |
| --- | --- |
| `review-fix/SOURCE_FREEZE_REVIEW_FIX.json` | `d52fd6b63580c667f5e09dd7c1fa950dc5f662f1dc151d0bf29c8cbab864a102` |
| `code-review-closure/CLOSURE.json` | `3819ebb6f3396ba3af6c38ed5e90782b6946459f40e95e91b5dba67074a8fdee` |
| `final-test/FINAL_TEST_RESULT.json` | `b75edee851edb6ab2d27c9fa39ebc030398720aa3a7f03d15df3ed6c58061743` |
| `final-test/EVIDENCE_MANIFEST.json` (76 files) | `967ee5b66beb985fcd2d5392fad39de5dd475a6470cfc53f093fc8cbb14ff506` |

## Independent unresolved work

R14's strict 20 writes / 19 takes FAIL and all-callback visibility FAIL (ten
opaque frames per side) are unchanged and not superseded by lifecycle PASS.
Read-only research in `build/r15-lifecycle/visibility-research/REPORT.md`,
independently reviewed in `visibility-review/REPORT.md`, found that existing
metadata can correlate original callbacks, accounting records, Host connections
and a visible writer to its declared publisher. It cannot identify encrypted
inner endpoint/sample contents. The visible refused writer/publisher association
does not prove that its fragment caused the missing application take.

Transport-only observation without keys cannot meet an all-inner-visibility
requirement for encrypted frames. A prospective observer across the security
boundary needs a separately approved architectural exception; it was not
implemented here. No decryption, key access, security-policy change or new R14
window occurred in this work. Lifecycle acceptance does not relax that gate.

The last known owner permission blocker remains
`ohos.permission.DISTRIBUTED_DATASYNC` DENIED on both registered RK3588A boards
at 2026-10-14T21:51:07Z. R15 made no fresh device/permission check, deployment or
permission change. Native SDK communication, device lifecycle and cross-device
acceptance remain BLOCKED. No aggregate ROS 2 completion percentage is inferred.
