# Managed lifecycle publisher Host acceptance (R16)

R16 adds bounded acceptance of a real lifecycle-managed publisher on the existing
Windows `default-2145` Host package (Fast DDS 2.14.5, SECURITY OFF / STATISTICS
OFF). Implementation is PRESENT; two fresh standalone builds, independent code
review and final Host tests PASS. The tester's Commit gate PASS applies to this
Host test-tooling stage. Main's graph/PRE_COMMIT, commit, push and CI remain
separate pending receipts at this documentation snapshot. Overall ROS 2 over
native DSoftBus remains NOT VERIFIED; DEVICE_TESTED=false and native/cross-device
acceptance is BLOCKED.

The six-file candidate is based on
`1b8cb4e33f6af8d0ddb163cc221bcf6acc276eee`: four new consumer/analyzer/test files
and two shared runner/test modifications. No Transport, DDS/RTPS, RMW, shared
capture validator, security-policy or dependency source is changed. The original
[R15 lifecycle consumer](ROS2_LIFECYCLE_HOST_ACCEPTANCE.md) and its oracle AST
are unchanged, and the affected shared runner receives a full R15 regression.
The [historical security profile](ROS2_HISTORICAL_HOST_PROFILE.md) is separate
and is not used by R16.

## Scenario and acceptance meaning

A owns one `LifecyclePublisher<std_msgs::msg::String>` registered through a real
LifecycleNode. Configure, activate, deactivate and shutdown callbacks each call
the corresponding base implementation exactly once. There is no direct
publisher activation toggle or replacement publisher. B uses remote
ChangeState/GetState services, Trigger probes, transition-event and sample
subscriptions. These are separate OS processes with intra-process delivery
disabled; files coordinate START/release/close only. Traffic passes through the
TCP-backed Host fake DSoftBus Socket seam, not native OpenHarmony DSoftBus.

After configure, four matched probes run on the same publisher: inactive,
active, inactive after deactivation, and active after reactivation. Each probe
unconditionally invokes both `publish(const Message&)` and
`publish(std::unique_ptr<Message>)`. IDs 1/2 and 5/6 are inactive attempts;
3/4 and 7/8 are active attempts. The final deactivation has no publish attempts
and is not counted as another negative probe. Inactive shutdown then finalizes
the node. ATTEMPT/RETURN receipts mean the void API was called and returned,
not that Transport sent a sample. The moved unique pointer is checked after
return for ownership transfer.

Both endpoints observe and assert reliable, volatile, KeepLast(10) QoS. The
publisher's nonzero GID remains stable across probes and completion; actual
`MessageInfo.publisher_gid` and implementation ID on each delivery must match.
Application attempt IDs are serialized payload fields, not DDS sequence numbers.
The exact final GID is `010f6944c89ac1190000000000001303`, implementation
`rmw_fastrtps_cpp`.

The oracle requires six changes, seven state snapshots, four probes, eight
attempt/return pairs, four exact deliveries, twelve transition events and six
callbacks. It checks schema, run/side/PID, contiguous local records, identity,
payload, transition order and response relationships. Legal asynchronous
response/event/sample order is accepted without comparing process clocks.
Forbidden inactive IDs, duplicates, unknown payloads and wrong GIDs fail the
subscription callback. Active samples must arrive before advancing the script.
The subscription remains spun through final events/state exchanges, a bounded
250 ms drain, release waiting and a final progress call before OBSERVATION_END.

The result is deliberately limited: **the four specified inactive attempts were
not observed at the remote subscriber callback during this matched finite
eight-attempt/four-delivery window**. Source activation guards and successful
progress from the same writer support that observation. They do not establish
indefinite non-delivery, absence from every DDS cache, an RTPS sequence fence,
or absence of queued data after the last spin.

| Capability | Status | Evidence / boundary |
| --- | --- | --- |
| Managed activation with reference and moved unique-pointer publish | Supported in the bounded Host scenario | Real base lifecycle callbacks, eight unconditional attempts, four actual active deliveries |
| Inactive publication suppression | Partial / finite observation | Four specified inactive IDs absent from subscriber callbacks during the matched window only |
| Deactivate/reactivate with stable writer identity | Supported in the tested sequence | Same public GID and actual receive GIDs, two active phases |
| Reliable/volatile/KeepLast(10) | Observed for this scenario | Both endpoints query actual QoS; not a broad QoS conformance result |
| Original lifecycle failure/recovery behavior | Host regression PASS | Full R15 scenario re-executed through modified runner |
| Loaned, serialized and generic publication APIs | Unverified | Only the two named overloads exercised |
| Concurrent publish/transition, restart, reconnect and stress | Unverified | Serialized callbacks and bounded fixed sequence |
| Security-enabled lifecycle publisher | Unverified | Non-security default package only |
| Native DSoftBus / RK3588A / cross-device | BLOCKED, unverified | No observed HDC target in latest discovery; prior owner permission blocker unresolved |

## Independent validation and actual observations

Initial testing passed two fresh builds, seventeen model methods, 24 barrier
controls and 60 related parser tests, plus one R15 and one R16 actual Host
window. Initial runs are `f138b1beed424e32` (A 70196 / B 14300) and
`0cbe181f24334194` (A 35672 / B 61104), all exits 0. Their 100-file manifest and
two-entry appendix remain sealed as initial evidence, not substituted for final
testing.

Independent Tier-2 agentic review reported NO_ACTIONABLE_FINDINGS_FOUND for the
six-file scope. It independently reran seventeen model methods and 24 barrier
controls, reanalyzed the sealed initial windows and rejected four additional
same-count inactive-delivery substitutions while accepting an asynchronous-order
control. Selected installed inline publisher headers were hash-checked and
appear in compiler-read evidence. This is not full ROS source-to-DLL attestation.
No review-driven implementation change was made. Fresh final testing still ran
both affected scenarios.

Final testing passed seventeen methods (ten shared-runner/original methods and
seven new publisher methods), 24 barrier subcontrols and sixty related parser
cases: receive 31, I/O 7, analyzer 15, native analyzer 6 and control files 1.
The 24 controls are subcases, not 24 additional ROS runs. Models are synthetic;
parser replays are offline. Two fresh MSVC x64 Release `/MD` C++17 consumers
were built, followed by exactly one final actual window per scenario:

| Final window | R15 regression | R16 managed publisher |
| --- | --- | --- |
| Run | `4175433b256a6647` | `8925f9c43e172686` |
| A / B PID | 36108 / 67464 | 39624 / 74132 |
| Business | 10 changes, 12 state and 12 available snapshots, 20 events, 11 callbacks, 2 finalized nodes | 6 changes, 7 states, 4 probes, 8 attempts, 4 deliveries, 12 events, 6 callbacks |
| Current START modules A / B | 12 / 10 | 16 / 14 |
| Current START polls | Both None | Both None |
| Exclusive rebind ports | 51538 / 51539 | 50507 / 50508 |
| Exit / forced kill / timeout | Both 0 / none / false | Both 0 / none / false |

The shared runner retains the complete current-map/live-child START barrier,
exact module path/hash binding and final raw input guard. Its scenario choice
is fixed (`lifecycle`, the default, or `managed-publisher`); unknown scenarios
and security environment conflicts fail before child creation. The new scenario
adds mandatory message/service type-support modules and its analyzer hash.
Sequential module scans and polls are observations, not atomic liveness or
runtime tamper protection.

| Final transport observations | R15 A | R15 B | R16 A | R16 B |
| --- | --- | --- | --- | --- |
| Ingress / admitted / adapter rejected | 149 / 149 / 0 | 203 / 185 / 18 | 92 / 78 / 14 | 134 / 134 / 0 |
| Charged / refunded | 149 / 149 | 185 / 185 | 78 / 78 | 134 / 134 |
| Send errors | 0 | 0 | 0 | 0 |
| Receive terminations | 2 WSA10004 | 2 EOF | 2 EOF | 1 WSA10004, 1 EOF |

All rejected callbacks have original identity, entered=returned=1 and Adapter
branch 9 / InvalidState status 5 / Closing phase 4 decisions, with
`bridge_called=0`. They were rejected before the Bridge and did not create
charged receive records. Both scenarios passed two connection joins, strict
`full_host` accounting, runtime close, pump join and port release. Complete
accounting does not mean every ingress was admitted, zero errors, or known
payload causality. Payload causality remains NOT_OBSERVED.

Each process in each final window retains these two DDS error messages (the
complete timestamped lines remain in stderr and the diagnostic receipt):

```text
[RTPS_PARTICIPANT Error] No unicast locators to create unique flows -> Function eprosima::fastrtps::rtps::RTPSParticipantImpl::createAndAssociateReceiverswithEndpoint
[DATA_READER Error] Problem creating associated Reader -> Function eprosima::fastdds::dds::DataReaderImpl::enable
```

The successful business oracle does not erase or dynamically explain those
errors. No zero-error-stream, general unique-flow capability or rejected-frame
payload-cause claim follows from PASS.

## Commands, artifacts and evidence

All `build/...` paths below are relative to the outer project root
`C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`, not W3. Ignored local
receipts are not guaranteed in a fresh clone. Final evidence is in
`build/r16-lifecycle-publisher/final-test`:

- `r15-configure.intent.json`, `r16-configure.intent.json`, corresponding build
  and test intent/result/stdout/stderr files record exact commands and outcomes.
- `r15-RUNTIME_PREDECLARATION.json`, `r16-RUNTIME_PREDECLARATION.json` and
  `*-runtime-launch.intent.json` bind the accepted commands, environment and
  deadlines. Each runtime directory's `result.json` records child commands,
  environment deltas, modules, input hashes and actual results.
- `PUBLISHER_API_OBSERVATIONS.json` retains QoS/GID facts;
  `RUNTIME_CLOSE_AND_REJECTION_DETAILS.json` retains original rejection,
  termination and DDS-error rows. `*-COMPILER_INPUTS.json` retains actual
  compiler reads; `POSTGUARD.json` records before/after checks.

Final sources are frozen at `build/r16f/source`; build directories are
`build/r16f/r15` and `build/r16f/r16`. Configure uses Visual Studio 17 2022/x64,
Release, `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL`, the installed Host package
`build/r13pf/dp`, DDS `build/g2a1/host214-install` and base ROS/pixi packages.
For example, the actual R16 build command is:

```powershell
& 'C:/pixi_ws/.pixi/envs/default/Library/bin/cmake.exe' --build 'C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport/build/r16f/r16' --config Release --target ros2_lifecycle_publisher_acceptance -- /m:1 /nr:false
```

The runtime launch uses `C:/pixi_ws/.pixi/envs/default/python.exe -B`, snapshot
`tests/ros2_host/run_lifecycle_acceptance.py`, the fresh `--exe`, published
`--bridge`/`--host-prefix build/r13pf/dp`,
`--fastdds-prefix build/g2a1/host214-install`,
`--ros-prefix build/ros2-target/h3/runtime`, the pixi prefix and a new output
directory. R16 adds `--scenario managed-publisher`; R15 retains the default.
Exact recorded paths are absolute. RMW is `rmw_fastrtps_cpp`, domain 201; the
wrapper is bounded at 80 seconds and the runner at 65 seconds, with no business
retry. Full PATH, cleared environment keys and control configuration are in the
intent/result records rather than inferred from a user's current shell.

| Artifact | SHA-256 |
| --- | --- |
| `build/r16f/r16/Release/ros2_lifecycle_publisher_acceptance.exe` | `4fad65a060c22255b10a9f827d084b7c77c24cb1d41477457731bd4e56ad7be7` |
| `build/r16f/r15/Release/ros2_lifecycle_acceptance.exe` | `1ca76685a3916d39634c951a54d48f77ca608cac2d4eb1f7ec4f32965f337f11` |
| `build/r13pf/dp/bin/fastdds_dsoftbus_ros2_host_bridge.dll` | `08945ea94381ac7264ad07c8c0818c1e717d847284d2402bdabd37c948c87e63` |
| `build/g2a1/host214-install/bin/fastrtps-2.14.dll` | `60408a27139ce59bf9aaa92c080e799d98f1531b7318dfbf0abf7308e7d555a4` |
| `build/ros2-target/h3/runtime/bin/fastcdr-2.2.dll` | `badcea3777c7a6b23961310dc5857e07c064d94af9babe52d2c097d8842d8c71` |

Exact selected ROS/RMW/lifecycle/std_msgs/std_srvs type-support paths and hashes
are in each current module barrier and runtime inputs. Final raw guards cover
711 source files, 1157 dependency records, 45 published package files and three
selected tools (Python/CMake/CTest); they do not attest the entire MSVC/OS.
The initial 100-file seal plus two appendix entries, eighteen review outputs
and 54 R15 publication-evidence entries remain unchanged.

| Receipt under `build/r16-lifecycle-publisher` | SHA-256 |
| --- | --- |
| `implementation/SOURCE_FREEZE.json` | `c95948044c79b9803e7788cdfa1eb3a8566605050667d22e90973a0aa5b595c4` |
| `code-review/REVIEW.json` | `60d6b70defde3e82b46a0d75e18946c7a64179a5e05f9c6d8ac75fa6ccc5db63` |
| `final-test/FINAL_TEST_RESULT.json` | `69e0e6a578709b079ef7dfc1fc31ec4e6c98fe15fbfef4eadb2efa4e4e6a0864` |
| `final-test/EVIDENCE_MANIFEST.json` (106 entries) | `79768516c8364ab7a352ea88928b6a40e8308bccfd07fcd4b981afcc7cf4f689` |

`final-test/SEAL_RECHECK.json` is one separate supplemental receipt, outside the
106-entry manifest. It confirms those entries and prior seals without changing
them. The old freeze's IMPLEMENTATION_PRESENT_NOT_TESTED and review's pending
final-tester fields are historical snapshots; the canonical final result above
supplies the subsequent execution evidence.

## Device and unresolved acceptance

Main ran read-only `hdc list targets -v` at 2026-10-15T12:24:23.860820Z. Exit was
0, stdout and stderr were empty, and no target entries were observed. This is
not a diagnosis of physical disconnection. No board command or permission
operation followed. The last known permission observation remains
`DISTRIBUTED_DATASYNC` DENIED on both boards at 2026-10-14T21:51:07Z; it was not
rechecked. See `build/r16-lifecycle-publisher/main/HDC_DISCOVERY.json`.

Native/device/cross-device acceptance remains blocked by no observed target and
unresolved owner permission. R14's strict 20 writes / 19 takes FAIL and ten
opaque frames per side FAIL remain unchanged. R16 neither reruns nor supersedes
that secure workload or its payload-visibility limits. No aggregate completion
percentage or complete ROS 2 support claim is made.
