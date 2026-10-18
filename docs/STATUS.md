# Project status

## Current bounded rosbag record/play (2026-10-18 10:33 UTC)

**DEVICE_TESTED / INTEGRATION_TESTED PASS for one fixed ten-String chain.**
Record5 `1403cdfc8beb937b` recorded A-to-B; play1 `5ca772d545190a98`
replayed B-to-A, with one actual Resume response and all ten original 36-byte
CDR payloads received byte-for-byte in order. [Commands, failures and seals](ROS2_ROSBAG_RECORDPLAY_ACCEPTANCE.md).

| Capability | Current status and reason |
| --- | --- |
| String pub/sub, stock SQLite record/play | SUPPORTED within this fixed two-board chain; 10 publications/acks, closed SQLite/metadata/CDR verification, then 10 exact received payloads. Actual native Socket-path evidence, not host stubs. |
| Resume service and selected QoS/type support | PARTIAL: one real Resume call/response, selected QoS and String CDR passed. Arbitrary services/actions, types and QoS combinations remain unverified here. |
| Lifecycle/resources | Bounded normal recorder SIGTERM (one), natural player return, both child exit codes 0, four owned PIDs absent and both tokens restored per window. Playback signals=0. SDK epoch/quiescence NOT_PROVEN. |
| Discovery/native transport | Paired native path and provider bindings observed. Dedicated SEDP NOT_QUALIFIED; no direct per-payload/Resume-to-RTPS mapping. Unique flow NOT_PROVIDED; strict mode UNTESTED; diagnostics retained. |
| Build / host verification | Existing native artifacts reused; combined package 3,234 files/220 ELF. Current source initial/review/final host gates passed. No fresh full native rebuild or ABI rerun; whole-build reproducibility is not claimed. |
| Earlier Action/node/topic scopes | Prior native C++ Action and ordinary CLI PASS retain their existing bounded evidence, unchanged by this stage. |
| General rosbag / full ROS 2 | PARTIAL / NOT VERIFIED: general types/QoS, reconnect, large/fragmented messages, concurrent/stress and other storage modes not covered. R22 STOPPED. |

Record2/3 FAIL and record4 BLOCKED remain preserved; only record5 was admitted
as playback source. Latest prior publication is `ee04d945b993b4e7b4cdc96f9773afa3a18817ff`
on existing `origin/codex/flow-encoding-published-20261014`; CI at 05:52:42 UTC
was NOT_RUN. ABI `06e5917abd8906a5d17066b87223d239109c5374` is also published.
Final tester Commit gate PASS covers exactly seven new/changed source files.
Main graph/PRE_COMMIT/publication remain pending for record/play. Historical
sections below retain their original dates, NOT_RUN states and publication scope.

## Current offline rosbag SQLite/CDR helper (2026-10-18 05:45 UTC)

**HOST_TESTED PASS for a standalone offline helper; real record/play NOT_RUN.**
The no-Node ABI prerequisite was published as
`06e5917abd8906a5d17066b87223d239109c5374` to existing
`origin/codex/flow-encoding-published-20261014`. Exact-SHA CI at
2026-10-18 05:23:02 UTC: NOT_RUN (zero runs/checks/status contexts).
[Oracle contract, commands and evidence](ROS2_ROSBAG_SQLITE_ORACLE.md).

| Capability | Current status and evidence boundary |
| --- | --- |
| Closed SQLite/CDR content oracle | SUPPORTED within the fixed ten-String offline contract; implementation PRESENT, HOST_TESTED PASS: 23 tests and 63 independent controls. Inputs are host-generated fixtures, not recorder-produced bags. |
| Scalar metadata/QoS fidelity and resource release | HOST_TESTED PASS; 45 independent type-corruption controls rejected; all 63 controls preserve input files/expectations and release fixture roots normally. BSI-01/BSO-01 resolved with failed evidence retained. |
| Native build and real no-Node ABI | Prior published bounded PASS retained; not rerun for this helper. Existing reproducibility and Transport-close limitations remain unchanged. |
| Recorder close, device collection, native reader, replay and received payloads | UNVERIFIED, NOT_RUN; helper not connected to the record/play runner. Native compilation and device/integration tests are N/A for the standalone Python helper, not PASS for the system. |
| Device availability | Read-only observation at 05:42:32 UTC: both registered RK3588A devices USB Connected. Sandbox empty listings did not establish offline status. No new communication evidence. |
| Broader ROS 2 / QoS / reconnect | Overall NOT VERIFIED; R22 STOPPED. No DDS/RTPS/RMW/Transport semantic changes. |

Independent final tester Commit gate PASS is restricted to the two-file helper.
Main graph/PRE_COMMIT/publication remain pending for this stage. Historical ABI
and earlier sections below retain their original dates and publication state.

## Current rosbag native ABI prerequisite (2026-10-18 02:06 UTC)

**Build/composition and real no-Node ABI PASS on both RK3588A boards.**
Run `daf356bea873e640` passed native imports, three fixed CDR checks, two native
QoS conversions and actual stock record/play parser checks. It created no Node,
Recorder or Player. `communication_verified=false`; record/play NOT_RUN.
[Commands, provenance, failures and limits](ROS2_ROSBAG_ABI_ACCEPTANCE.md).

| Capability | Current status and evidence boundary |
| --- | --- |
| Native rosbag build and combined package | SUPPORTED within the pinned SDK23/AArch64/CPython 3.12.7 package; BUILDS PASS. Existing 293-command native build and exact package were revalidated, not rebuilt during final aggregation; `whole_build_reproducible=false`, with earlier output/resource/DSO differences retained. |
| rosbag2_py ABI / selected type support | SUPPORTED within this no-Node probe on A/B: eight native extensions, String and Resume request/response CDR, exact package origins/providers. DEVICE_TESTED PASS for ABI only. |
| QoS conversion / CLI parsing | PARTIAL: two native conversion roundtrips and stock SQLite presets/options parse correctly. Network QoS interoperability and actual record/play behavior UNVERIFIED. |
| Native cross-board record / persisted SQLite oracle / replay | UNVERIFIED, NOT_RUN; not executable cases in the current finite ABI runner. Earlier R19/R20 offline local persistence remains separate. |
| Lifecycle / resources | Interpreter execution/finalize=0, identity restored=1, four owned processes absent, signals=0. `teardown_observed=0`, rc=-1; Transport close and SDK callback quiescence not established by ABI success. |
| Node/topic/Action communication | Prior bounded published evidence retains its exact source/package/run scope. The combined rosbag package has no new ROS communication qualification. |
| Larger/general types, concurrent record/play, cache/crash recovery, compression, MCAP, split bags | UNVERIFIED by this stage; the selected artifact scope is SQLite and does not establish these capabilities. |
| Broader QoS/reconnect/load and full ROS 2 parity | UNVERIFIED; R22 STOPPED. No DDS/RTPS/RMW/Transport semantic changes. |

Independent stage aggregation rehashed the 753-file target seal and the reviewed,
post-review-tested source/package bytes, then replayed the canonical raw-log audit
without HDC. Tester Commit gate PASS covers only five new files. Documentation is
synchronized here; Main graph/PRE_COMMIT/publication remain pending. This is not
full-project VERIFIED. Original UNKNOWN_START, entry-expired and local audit
failures remain preserved, including the aggregation helper's failed first attempt.

Last confirmed publication is ordinary Action evidence
`a0a4fcdbe0b1e2ae7c4f32abbe0f5c08074f17c3` on existing
`origin/codex/flow-encoding-published-20261014`; exact-SHA CI at
2026-10-17 23:41:04.775 UTC was NOT_RUN (zero runs/checks/status contexts).
Earlier sections below retain their historical publication and capability state.


## Current ordinary Action CLI evidence (2026-10-17 23:34 UTC)

**Stock `ros2 action send_goal` AB/BA PASS for one positive Fibonacci order=6
goal per direction on the real RK3588A pair.** Each CLI received an accepted
nonzero UUID, five ordered feedback prefixes, exact result `[0,1,1,2,3,5,8]`
and SUCCEEDED, correlated with the opposite public rclpy server before close.
[Actual commands, artifacts, raw evidence and limits](ROS2_ORDINARY_CLI_ACTION_ACCEPTANCE.md).

| Capability | Status and evidence boundary |
| --- | --- |
| Ordinary Action CLI AB/BA | SUPPORTED within two fresh fixed order=6 windows; DEVICE_TESTED PASS. One Node/context and one successful goal per process. |
| Native C++ Action | Earlier independently published bounded cancellation/rejection/abort/concurrent-goal evidence remains separate; it does not qualify those ordinary CLI behaviors. |
| Native path / local lifecycle | Actual Socket, project RMW/DDS and exact providers verified. Public delete/RuntimeClose/finalize=0, restore=1, all four owned PIDs absent and signals=0 in each window. SDK global callback quiescence NOT PROVEN. |
| Python/node/topic/echo | Prior dated evidence retains its exact source/run scope, including old-runner topic/pub limitation. No fresh topic or general service test is claimed by Action success. |
| Ordinary CLI cancel/reject/abort/preemption/concurrent goals | UNVERIFIED: not executed in the two positive-goal windows. Status-topic behavior is not separately qualified. |
| Serialization / QoS / size / recovery | Fixed Fibonacci payload only. Broader QoS, large messages, reconnect and stress remain UNVERIFIED; R22 remains STOPPED. |
| Optional unique flows / wire attribution | Unique flows NOT_PROVIDED; known optional retry retained, strict mode NOT_TESTED. Dedicated SEDP and direct Action UUID-to-RTPS mapping not qualified. |
| rosbag / complete ROS 2 parity | Bag excluded / UNVERIFIED; full ROS 2 parity NOT VERIFIED. |

This stage changes documentation only. Published source bytes, post-review Host
tests and qualified native build/package were revalidated; no new compile,
product suite or native rebuild is claimed. Independent final execution Commit
gate PASS applies to this fixed Action evidence; Main documentation/graph/
PRE_COMMIT/publication remain separate. Original failures and local errata are
retained. R22 five files and bag builder remain excluded.

Last confirmed publication is echo repair
`cb9c54dc6564362b52dc652e55ae9f7b5559749e` on existing
`origin/codex/flow-encoding-published-20261014`; its 22:49:05 UTC CI snapshot is
NOT_RUN (zero runs/checks/status contexts), not PASS. Historical sections below
retain their earlier publication and capability state.


## Earlier ordinary topic/echo evidence (2026-10-17 22:34 UTC)

**Fresh stock topic echo AB/BA PASS on the real RK3588A pair after a four-file
Host readiness repair.** In BA, successive snapshots show publisher matched=0,
then the CLI's exact YAML, then publisher matched=1 before normal close. The
repair waits only for incomplete live evidence within the original deadline;
incorrect data, malformed matches and final missing matches still fail.
[Commands, artifacts and retained evidence](ROS2_ORDINARY_CLI_TOPIC_ACCEPTANCE.md).

| Capability | Status and evidence boundary |
| --- | --- |
| Python entry / single Node / stock no-daemon node discovery | Previously DEVICE_TESTED PASS and published as `471d344050dccae6cb10396d3432fc9f88a92ff7`; selected single-lifetime scope only. |
| Topic graph AB/BA and stock topic pub AB/BA | PARTIAL: original real pre-repair source/run evidence reverified. Not rerun with this four-file patch. Historical pub AB preparation receipt discrepancy retains its independent disposition and missing outer-raw limitation. |
| Stock topic echo AB/BA | SUPPORTED for exact String payload, reliable/volatile/depth10, one Node/context per process, no-daemon/once/timeout20. Fresh AB `c67a4d427026c670` and BA `a039923ab6958e31` PASS. |
| Host echo readiness / consumer binding | HOST_TESTED and used in fresh target windows. Synthetic negative fixtures remain Host-only; actual device metadata was independently constructed and checked. |
| Native package/build | Reused unchanged qualified package: 2,811 members / 167 ELF. Python compile PASS; new C++ build N/A for this four Host-file change. |
| Public/native close and owned process exit | PARTIAL: both windows delete/close/finalize return 0, restore=1, all four owned PIDs absent, signals=0. SDK global callback drain/quiescence NOT PROVEN. |
| Native C++ Action | Earlier bounded published evidence unchanged. Ordinary Action CLI AB/BA are NOT_RUN, not inferred from topic results. |
| Optional unique flows | PARTIAL: known failed first Reader attempt/retry retained; unique flows NOT_PROVIDED, strict mode NOT_TESTED. |
| QoS breadth, reconnect, large messages, concurrent applications | UNVERIFIED beyond prior separately dated scopes. Fixed String delivery is not general load/recovery acceptance; R22 STOPPED. |
| rosbag / complete ROS 2 parity | Bag NOT_INCLUDED / UNVERIFIED; full ROS 2 parity NOT VERIFIED. Dedicated SEDP wire qualification remains absent. |

Final execution Commit gate PASS applies only to the four changed Host files.
Independent final tester reverified 15 seals / 9,341 files and the current
source freeze; old echo FAIL/BLOCKED windows and local audit errors remain
unchanged. Main documentation/graph/PRE_COMMIT/publication are separate gates.
R22 five files and bag builder are excluded. Last confirmed publication is
`471d344050dccae6cb10396d3432fc9f88a92ff7` on existing
`origin/codex/flow-encoding-published-20261014`; its 20:30:40 UTC CI snapshot is
NOT_RUN (zero runs/checks/status contexts), not CI PASS. Earlier sections below
retain their historical publication and capability states.


## Earlier ordinary Python/CLI node-discovery evidence (2026-10-17 20:11 UTC)

**Bounded real RK3588A AB/BA node-discovery PASS.** Stock
`ros2 node list --no-daemon --spin-time 3` found the opposite public rclpy Node
in runs `ad29a5b4d0d83169` and `becffc43fccc35dc`. Both host runs and all recorded
remote exits were 0; public/native close, Python finalization, token restoration
and all four owned-PID absence checks passed per window without forced signals.
This uses the exact qualified 89-provider package and real DSoftBus Socket
with fake_sdk=0 / builtin=0. [Commands, artifacts and sealed evidence](ROS2_ORDINARY_CLI_NODE_DISCOVERY_ACCEPTANCE.md).

| Capability | Status and evidence boundary |
| --- | --- |
| Owned project Python entry / single public Node | SUPPORTED within tested single-lifetime scope: target no-Node entry cases, single-Node window and AB/BA lifecycle. Full Python/NumPy/ROS ABI is UNVERIFIED. |
| Ordinary no-daemon node listing | SUPPORTED for the exact A-to-B and B-to-A fixtures; DEVICE_TESTED PASS. Dedicated SEDP wire attribution and general reconnect remain UNVERIFIED. |
| CLI/types/rosgraph package | BUILD and HOST_TESTED PASS; stock node command target-tested. Presence of other CLI commands or generated Action types is not their runtime acceptance. |
| Native C++ Action | PARTIAL, separately published: bounded goal/feedback/cancel/reject/abort/concurrency evidence below remains applicable. Ordinary Action CLI is UNVERIFIED. |
| Topic graph, ordinary pub/echo, services and QoS | NOT VERIFIED BY THIS STAGE. Topic graph/pub/echo are next frozen cases; prior dated native/Host results keep their own scope. R22 remains STOPPED. |
| Serialization / sizes / load / recovery | Fixed CDR and prior native Action evidence retain their scopes; large native payloads, broad types, stress, reconnect and failure recovery are not qualified by node listing. |
| Resource lifecycle | PARTIAL: process-local public/native close, finalization, restoration and exit pass. SDK global callback drain/quiescence and same-process repeated Participant lifetimes remain NOT PROVEN. |
| Unique network flows | PARTIAL: known optional first-Reader failure and existing RMW original-QoS retry are retained. Unique flows NOT_PROVIDED; strict mode NOT_TESTED. |
| rosbag record/play / remote replay | UNVERIFIED; bag builder/package excluded from this stage. Earlier local SQLite evidence is unchanged. |
| Unsupported choices / overall status | Session API, builtin fallback and provider substitution excluded. Daemon mode is not qualified. Full ROS 2 parity: NOT VERIFIED. |

The independent final tester reports execution Commit gate PASS for the exact
11 source files; six component initial/review/post-review chains and the actual
AB/BA runs were revalidated. Main graph/PRE_COMMIT and publication for this
stage remain pending. R22 five files, bag and unrelated worktrees are excluded.
Original failed/blocked windows and the two historical CLI preservation FAIL
assertions remain intact; their separately reviewed launcherfix2 disposition
does not relabel old evidence as PASS.

Last confirmed publication is native C++ Action
`4794ffb08d2d1cf49e758a0a4627f4aa2c87de92` on existing
`origin/codex/flow-encoding-published-20261014`; its 08:31:44 UTC CI snapshot is
NOT_RUN (zero runs/checks/status contexts). Dated sections below retain the
state known at their time; they are superseded only within the scope above.


## Earlier R25 native Action evidence (2026-10-17 08:25 UTC)

**Bounded native C++ Action business/path PASS on both RK3588A boards, with
a recorded optional unique-flow retry.** Run `484675d3f084d757` used fresh peer
run `fe7c9c0cb93d52df`, the new dedicated normal/DATASYNC-only identity and
actual project Fast DDS/RMW with native SoftBus. Five requests produced four
acceptances, one rejection, two successes, one cancellation and one abort.
Both children restored tokens and exited 0 without forced signals.

Both A and B logged `No unicast locators to create unique flows` and
`Problem creating associated Reader`; independent post-run review traced the
existing RMW's OPTIONALLY_REQUIRED retry to the original QoS on the same
Participant/custom Transport. Unique flows were not provided; STRICTLY_REQUIRED
was not tested. This is neither builtin fallback nor an error-free startup.
The sealed tester report remains unchanged. The 08:25 UTC additive final tester
supplement restores scoped PASS after the temporary log-triage hold; Main's full
graph/PRE_COMMIT/publication gate is separate. See
[native Action artifacts, commands, limitations and evidence](ROS2_NATIVE_ACTION_ACCEPTANCE.md).

| Capability | Status and evidence boundary |
| --- | --- |
| Fixed native identity and peer directory | SUPPORTED for the exact R25 identity on A/B: native/normal APL, DATASYNC only, token restoration, one local/online directory query per fresh run, SDK buffer release and private local UDID export. Old probe identity remains DENIED. |
| Native C++ Action | PARTIAL: five requests, four accepted, one rejected; two succeeded, one canceled, one aborted; UUID correlation, feedback and concurrent server goals pass in one actual paired window with application readiness barriers. The recorded optional unique-flow failure recovers through the existing RMW original-QoS retry; it is not an error-free startup. |
| Native transport path | SUPPORTED for that bounded run: actual project RMW/Fast DDS maps and real SoftBus provider, fake_sdk=0, builtin/intraprocess=0, DataSharing OFF; bidirectional SDK send/ingress and writer/sequence matches. This is not production qualification. |
| Discovery | PARTIAL: actual V1 metatraffic/user locators in the same SPDP DATA and peer ingress pass; successful Action functionally exercises endpoint discovery. No dedicated SEDP wire oracle, general graph or reconnect test in R25. |
| Pub/sub and service | PARTIAL: Action feedback/results and PAIR/TAIL Trigger readiness service work in this scenario. Prior dated general pub/sub/service results retain their own scope; R25 adds no general CLI topic/service acceptance. |
| Serialization and message size | PARTIAL: fixed Fibonacci Action payloads in R25 and fixed String CDR on A/B in R24. R21 small/66k/burst String remains Host TCP-backed fake-DSoftBus evidence, not native large-message acceptance. |
| Unique network flow endpoints | PARTIAL: this OPTIONALLY_REQUIRED attempt does not provide unique flows; the existing RMW retries with original QoS on the same custom Transport. STRICTLY_REQUIRED is UNVERIFIED / not executed, not a claim of permanent incompatibility. |
| QoS and failure recovery | UNVERIFIED beyond the scenario's existing profile. No all-QoS, status-topic, unbarriered Action, reconnect, packet-loss or crash-recovery claim. R22 remains STOPPED / UNVERIFIED. |
| Resource lifecycle | PARTIAL: public Participant deletion, pump join, local runtime close, original token restore, child exit 0 and owned Action directory cleanup pass without forced signals. SDK global callback drain/epoch/quiescence and same-process repeated Participant lifetimes are not established here. |
| Project Python ABI | SUPPORTED only for R24 no-Node imports, fixed String CDR and help/resource queries. Full ABI and NumPy API coverage remain UNVERIFIED. |
| Project Python Node / ordinary CLI Action | UNVERIFIED on target. Separate token-first Python launcher repair has Host evidence/review only; CLI overlay CLI-01 repair passed independent Host retesting and awaits formal review; Action Python types have build evidence only. These ongoing changes are excluded from this native 14-file stage. |
| rosbag | PARTIAL only for earlier R19/R20 offline local String SQLite persistence. Ordinary record/play over native DSoftBus, rosbag2_py and remote replay remain UNVERIFIED. |
| Unsupported execution choices in this stage | Session API, builtin UDP/TCP fallback and generic ROS provider substitution are excluded. Daemon mode and multiple successful Participant lifetimes per process are not qualified. |

The new identity readiness PASS supersedes the earlier blanket permission
block only for this exact identity and scenario. Old probe DENIED and earlier
failed/blocked runs remain historical facts. No registry-wide access occurred.
R25 publication is PENDING Main's full gate. Last confirmed publication is
R24 `d8b0fdfecaa654183c57d4b973cc936234080667` on existing
`origin/codex/flow-encoding-published-20261014`; 06:12:24 UTC CI NOT_RUN
(zero runs/checks/status contexts). Earlier dated snapshots below are retained.

## Earlier R24 capability overview (2026-10-17 06:02 UTC)

**PROJECT_LINKED_PYTHON_ABI_NO_NODE: PASS on both RK3588A boards.** The isolated
slice selects project Fast DDS 2.14.5/Fast CDR 2.2.5 and project RMW libraries
before importing the measured rclpy candidate. Actual imports, exact String CDR
and three CLI commands pass. This is bounded ABI execution evidence, not full
ROS ABI qualification or native DSoftBus communication acceptance.

| Capability | Current evidence and boundary |
| --- | --- |
| Project-linked CPython/rclpy/native imports | SUPPORTED, bounded: unchanged CPython 3.12.7, reused exact rclpy candidate, actual native NumPy core and 115 PROJECT_SLICE loaded native files per board with matching A/B/package hashes. Full NumPy APIs remain UNVERIFIED. |
| String serialization/deserialization | SUPPORTED for fixed `project-python-ABI-中文`: exact independent CDR bytes and unchanged round-trip on A/B, without a Node or Participant. Other types/ABI paths remain UNVERIFIED. |
| Isolated CLI help/resource queries | SUPPORTED: help, `pkg prefix rclpy` resolves to the fresh slice, `interface show std_msgs/msg/String` passes. Global Python and generic wrapper remain unchanged; R23 generic CLI evidence remains separately scoped. |
| CLI Node/daemon; native DSoftBus and cross-board communication | UNVERIFIED / BLOCKED. No rclpy.init, Context initialization, Node, Participant, daemon, DSoftBus call or message exchange in R24. |
| Pub/sub, large messages and bursts | R21 earlier bounded Host PASS: typed String small/66k/burst, Reliable 30/30 and Best Effort 30/30 over TCP fake DSoftBus. General types/sizes/load/concurrency remain UNVERIFIED; R14 failures remain open. |
| Service, action, discovery/graph | Earlier dated evidence retains its exact scope; [Host integration](ROS2_HOST_INTEGRATION.md) and [R18 action acceptance](ROS2_ACTION_ACCEPTANCE.md) are not upgraded by R24's imports. No current project Python Node/CLI service/action/discovery acceptance. |
| QoS and reconnect | Existing bounded QoS results unchanged; broader QoS, reconnect and failure/recovery acceptance remain UNVERIFIED. R22 Host QoS expansion is STOPPED / UNVERIFIED with five files retained uncommitted. |
| Resource lifecycle | R24 owned build-process cleanup and exact scratch/archive removal PASS; bounded snapshots show no remaining visible owned processes. This does not establish global daemon absence, SDK drain/quiescence or concurrent DDS resource lifecycle. Earlier lifecycle limits remain. |
| Offline persistence / POSIX entry | R20 earlier local String persistence and entry PASS unchanged; no recording/replay or cross-board persistence claim added. |

Implementation PRESENT; fresh build PASS (35 commands, 769 AArch64 compile
units including 98 generated Python units, 2,342 installed artifacts); package
PASS (2,058 recorded files, 141 ELF members). Formal Tier 2 review found one P2
build-timeout process-ownership defect; it was fixed, focused review passed 23
cases/controls, then final tester rebuilt and re-ran A/B. Final ownership checks
passed 21 cases plus sibling-survival and handle-stability controls. All 38
device checks passed, task host/remote exits were 0 and task stderr was empty.
Build, HOST_TESTED and DEVICE_TESTED are PASS only for this no-node scope;
broader Transport integration is N/A to these four additive Python/CMake tools.
DSoftBus integration remains BLOCKED / NOT_VERIFIED. Tester Commit gate PASS
is scoped to R24; original failures and the tester counting correction remain.

The next unresolved integration boundary is project Python Node/CLI lifecycle
with the required identity and native communication validation. The successful
candidate reuse does not require a new RMW, prove all rclpy APIs, or authorize
permission work. Project DATASYNC remained denied at the last 2026-10-16 query;
new persistent identity is NOT_APPROVED / NOT_EXECUTED. Prior whole-registry
access was rejected by automatic approval review and not retried. No security,
permission, network or global installation change occurred in R24.

Last confirmed publication is R23 `9d5d70c5e8c90fb3d33c2a34c5ed8132d1ce293c` on
existing `origin/codex/flow-encoding-published-20261014`; its CI is NOT_RUN
(zero runs/checks/status contexts). R24 Main graph/PRE_COMMIT, commit, push and
CI remain PENDING. See [R24 reproduction and sealed evidence](ROS2_PROJECT_PYTHON_ABI.md).
Local ignored build inputs/evidence are not uploaded by these documentation
references. Dated snapshots below retain their historical scope and status.

## Earlier R23 capability overview (2026-10-17 04:42 UTC)

**Native CLI startup is SUPPORTED within the existing generic UDPv4 deployment
on both RK3588A boards. Project-linked Python and native DSoftBus communication
remain UNVERIFIED / BLOCKED.**

| Capability | Current evidence and boundary |
| --- | --- |
| Generic native Python/CLI entry and imports | SUPPORTED, bounded: real CPython 3.12.7/AArch64 OHOS, native rclpy extension, CLI help on A/B; no Node initialization or daemon command. |
| Generic CLI resource queries | SUPPORTED, bounded: `ros2 pkg prefix rclpy` and `ros2 interface show std_msgs/msg/String`, independently on A/B. |
| Project-linked Python/rclpy/CLI | UNVERIFIED: generic Fast DDS 2.14.6 / Fast CDR 2.2.7 differs from project 2.14.5 / 2.2.5. Loaded-library provenance does not qualify substituting the project stack. |
| Project DSoftBus CLI Node/daemon, pub/sub, service, action, QoS, cross-board delivery | UNVERIFIED / BLOCKED; R23 creates no Node, starts no daemon and exchanges no messages. Earlier R18/R20/R21 results retain their exact separate scopes. |
| R21 typed String Host delivery | Earlier bounded PASS: Reliable 30/30 and Best Effort 30/30 over the TCP fake DSoftBus seam; not native communication. R14 old failures remain open. |
| R20 POSIX entry/local String persistence; R18 action evidence | Earlier bounded results unchanged; see their dated records and [target preparation](ROS2_ROSBAG_TARGET_PREPARATION.md) / [action acceptance](ROS2_ACTION_ACCEPTANCE.md). No broader claim follows from R23. |
| R22 Host QoS boundary expansion | STOPPED / UNVERIFIED by user correction. Five uncommitted files retained unchanged; no further Host expansion is underway. |

Implementation PRESENT; Python AST checks PASS; fresh C++ build N/A (two additive
Python scripts, no Transport/DDS/RTPS changes). Initial help-oracle failure is
retained; corrected initial testing PASS, independent Tier 2 agentic review
completed with no findings and 14 local fault controls, then final independent
testing PASS with 34 checks. Fresh A/B probe/help/two-query tasks all returned
host 0/remote 0 with empty task stderr. Owned scratch cleanup and process
snapshots passed; those snapshots do not establish global daemon absence.
DEVICE_TESTED=PASS only for this CLI/import/resource scope. DSoftBus integration
and cross-device communication are NOT_TESTED; overall native ROS 2/DSoftBus
remains NOT VERIFIED. Tester Commit gate PASS is R23 scoped.

The next small step is an isolated project Python/library prefix selected before
the first rclpy import, reusing the existing CPython and suitable R18 rcl_action
artifacts. Complete the measured dependency closure and determine extension
compatibility there; neither compatibility nor a mandatory rclpy rebuild is
established. No new RMW or broad ROS rebuild is justified by this evidence.

Project DATASYNC remained denied at the last 2026-10-16 query. A new persistent
identity is NOT_APPROVED / NOT_EXECUTED; prior whole-registry access was rejected
by automatic approval review and not retried. No security/global changes occurred.
Last confirmed publication before R23 is R21
`ed7e26c413d0f5a7553f69b0df7aad58d65908a3` on the existing
`origin/codex/flow-encoding-published-20261014`. R23 publication and Main's final
graph/PRE_COMMIT gates are PENDING; no R23 SHA or CI success is predicted.

See [R23 reproduction, exact artifacts and local evidence seals](ROS2_NATIVE_CLI_STARTUP.md).
Evidence under the original workspace's ignored `build/r23-native-cli` is local,
not uploaded by these documentation references. Dated snapshots below retain
their historical scope and publication state.

## Earlier R21 capability overview (2026-10-16 16:56 UTC)

**Scoped Windows Host delivery PASS:** typed String small/66k/burst in Reliable
30/30 and strict Best Effort 30/30; R17 serialized regression 4/4. These are
actual ROS processes over the TCP-backed fake DSoftBus Host seam.

| Completed / current | Remaining / next |
| --- | --- |
| 68 controls, independent Tier 2 review with no code findings/fixes, final three actual windows once each, all six exit0/no forced kill/orphans/six port rebinds | Native/cross-device remains BLOCKED; general types/sizes/load/QoS and fragment/drop causality remain NOT VERIFIED. R14 old failures are not closed. |
| Initial fresh MSVC x64 /MD build; final reuses source/compiler/dependency/artifact-verified outputs, no final recompile | R22 reliability offered/requested boundary is PLANNED read-only triage, not implemented/tested. |
| R20 POSIX entry + local String storage on A/B retains earlier bounded device PASS | Those independent local runs do not establish native cross-device delivery. |

Original queue limits are unchanged; no capacity refusal occurred. Final Reliable
B has one AdapterPhase/InvalidState pre-Bridge decision during Closing; initial
Reliable A's two decisions remain a separate observation. Full charge/refund and
capture passed; business success does not mean zero rejected callbacks. Final
testing completed across an infrastructure interruption without repeating a
completed case: first tester ran controls/Reliable, finisher Best Effort/R17.
The fixture impact-after-creation omission is preserved as a real process
deviation with Main disposition, not a retroactive pre-edit PASS.

See [R21 acceptance, commands and matrix](ROS2_FRAGMENT_HOST_ACCEPTANCE.md).
Tester Commit gate PASS is Host scoped; Main graph/PRE_COMMIT/publication remain.
Last confirmed publication is R20 `ab97486d814e58023f7d4f00c8bed7a945852b1a`,
existing origin/codex/flow-encoding-published-20261014, exact-SHA 16:12:11 UTC CI
NOT_RUN (zero runs/checks/status contexts). No R21 commit/CI is predicted.
Missing project-only token-state/lossless restore material still blocks native
communication; no registry retry/security action. Overall native ROS 2/DSoftBus
NOT VERIFIED. No new major direction decision is required for this Host stage.

## Earlier R20 capability snapshot (2026-10-16 15:54 UTC)

**Generated `bundle/run` now starts the offline String fixture directly from an
unrelated cwd on each RK3588A board, including a relocated path with spaces.**

- Completed: fresh target compile/link/readelf/package; exact four-message
  SQLite raw/typed persistence, definition/metadata/time/order checks and cleanup;
  both review path defects fixed and verified on A/B.
- Current boundary: declared executable/resource entry, unchanged argv/cwd/child
  exit, only AMENT_PREFIX_PATH/LD_LIBRARY_PATH replaced. Actual ROS fixture
  qualification is still offline String; A/B are independent local runs.
- Remaining: full ROS setup/CLI/record/replay/general types/QoS/concurrency/cache/
  crash/native cross-device remain NOT VERIFIED. R21 bounded Host delivery
  diagnostic is PLANNED, not implemented.
- Major decisions: use generated run with no manual prefix exports, preserve
  Transport/Socket-only boundaries and keep native permission work separate.

Final post-review tester Commit gate PASS is POSIX-entry/local-String scoped:
20 legacy controls PASS; 15 entry methods yield **11 PASS and 4 Windows SKIP**.
Fresh r19baglocal5 build at 15:49 UTC passed; direct A/B entry returned HDC 0 /
remote 0 with fds=0/tasks=2 after all three close/destruction phases and PID/hash
checks PASS. Actual physical LF/sibling/alias cases rejected before any child;
explicit-shell and `./` leading-dash cases preserved exact values/exit. Earlier
failures and final sandbox infrastructure failure remain; unchanged normal-user
retry PASS does not rewrite them. DEVICE_TESTED applies to this bounded entry +
offline persistence; overall native ROS 2/DSoftBus remains NOT VERIFIED.

Last published P023 is `e13696ed6a5d185ae29e3cf1ddf5a574cf27f4c5` on existing
origin/codex/flow-encoding-published-20261014; exact-SHA CI 14:53:27 UTC NOT_RUN
(zero workflows/checks/status contexts). R20 graph/PRE_COMMIT/publication/CI
remain Main gates. Missing full current project-only token-state/lossless restore
material still blocks native communication; no registry retry or security action.
See [current preparation/evidence](ROS2_ROSBAG_TARGET_PREPARATION.md) and
[direct entry usage](../tests/ros2_host/rosbag_string/README.md). Historical dated
snapshots below retain their own scope and publication state.

## Earlier R19 offline capability snapshot (2026-10-16 14:38 UTC)

**Offline String persistence now passes on each RK3588A board independently:**
actual SQLite write-close-reopen, exact raw CDR and separate typed readback.

- Completed: storage/SQLite/cpp artifact preparation, local consumer/bundle,
  20 host controls, fresh local A/B runs with four fixed messages, original
  definition/metadata/time/order checks and resource release.
- Current boundary: direct rosbag2 Writer/Reader, sqlite3/CDR, cache disabled;
  two independent local runs do not establish communication between boards.
- Remaining: general CLI/record/replay/types/sizes/concurrency/crash recovery
  and native ROS 2/DSoftBus cross-board acceptance remain unverified.
- Major decisions: retain the offline/native separation, original ROS/Fast DDS
  semantics and Socket-only policy. No new major direction decision is needed.
  Native communication still requires full current project-only token-state
  and supported lossless restoration material.

Initial testing passed; formal Tier 2 review found no actionable findings and
required no fixes; final independent tester Commit gate PASS is scoped to offline
String persistence. Original actual 14:19:01 UTC compile/link/readelf/package
artifacts in build/r19baglocal3 were reused unchanged by explicit Main decision,
with full current source/header/link/resource/provenance/artifact checks before
and after; final testing did not recompile. All 20 host controls reran, and A/B
device runs were fresh. Host controls are mocked ELF/pure byte checks; actual
device execution supplies the local persistence evidence.

At 14:37:29 UTC board A and 14:37:41 UTC board B each returned HDC 0/remote 0.
Four exact raw CDR rows and nanosecond receive timestamps/order, String.msg
definition, topic/type/CDR/count/file metadata, distinct typed Reader reopen and
independent SQLite integrity_check=ok passed. Both 28,672-byte databases share
SHA256 `ec0dc919934f7e66ba67f975d44067757e2e0fda88158d1755013cec7cc618f6`.
All three close/destruction phases report bag_fds=0/task_count=2 with baseline
thread IDs restored; all six supervised PIDs were absent afterward. Deployment
and retrieved-file hashes passed. Device evidence is local-only; no native
transport or identity operation occurred.

Implementation/build/local integration PASS; DEVICE_TESTED=PASS for this exact
offline fixture on A/B. Native DSoftBus INTEGRATION_TESTED=false, cross-device
NOT_EXECUTED; overall ROS 2 over native DSoftBus NOT VERIFIED. ROS_DISTRO was
unset (warning retained), optional ros_distro/type_description_hash are empty,
and offered_qos_profiles=[]. No endpoint QoS, independent send-time persistence,
full CLI/record/replay, large/concurrent/cache/crash-durability or general ABI
claim is made. Upstream rosbag gmock tests remain NOT_RUN. Earlier CRLF-index,
missing-libyaml packaging and missing-package.xml device failures remain FAIL;
review/final harness failures are retained separately.

Last confirmed publication remains cpp static-stage commit
`57311e0f45aae5bdaae3b4578ef917d2e3682c27` on existing
`origin/codex/flow-encoding-published-20261014`; exact-SHA CI at 13:42:47 UTC was
NOT_RUN (zero workflows/checks/status contexts). Main graph/PRE_COMMIT and this
stage's publication/CI remain separate pending receipts. Earlier whole-registry
access was rejected by automatic approval review and was not retried; no new
security change occurred. See [current target preparation](ROS2_ROSBAG_TARGET_PREPARATION.md)
and [fixture usage](../tests/ros2_host/rosbag_string/README.md) for bounded
reproduction, exact device receipts and initial/review/final evidence seals.

## Earlier R19 static-stage capability snapshot (2026-10-16 13:29 UTC)

**Storage, the SQLite plugin and rosbag2_cpp have qualified target build/install
artifacts. Local String write/read and native ROS 2/DSoftBus cross-board
recording remain unverified.**

- Completed: rosbag2_storage original objects, DSO and installation; complete
  original SQLite plugin and rosbag2_cpp 0.26.11 cross-builds/static installs.
  Storage's individual receipts do not establish its complete Ninja package run.
- Current next stage: implement/package a minimal local C++ std_msgs/msg/String
  consumer with explicit sqlite3/CDR, then verify actual board write-close-reopen,
  raw/typed count/topic/type/format/order/receive-time/payload/definition checks
  and resource release. This consumer is still source-only planning.
- Remaining: actual plugin loading and runtime ABI, CLI/record/replay, then
  native ROS 2/DSoftBus and cross-board acceptance. No new major direction
  decision is needed; preserve original package sources/metadata, the Transport
  boundary, Socket-only policy and legacy transport packager behavior.

Canonical rosbag2_cpp final **ROSBAG2_CPP_0.26.11_BUILT_STATICALLY_INSTALLED**
has tester Commit gate PASS for BUILDS/static installation only. Initial testing
passed; independent Tier 2 agentic review found no actionable findings and
requested no implementation changes; post-review final testing passed. Main's
graph/PRE_COMMIT/publication gates remain separate. **HOST_TESTED=false,
HOST_RUNTIME=false, DEVICE_TESTED=false, INTEGRATION_TESTED=false; overall
NOT VERIFIED.** Upstream gmock tests remain NOT_RUN under the authorized
BUILD_TESTING=OFF artifact scope. Device/runtime are N/A to this static stage,
but required for subsequent runtime claims.

The unchanged original cpp target has 20 source entries/19 unique TUs. Full
Ninja all executed 19 compiles and one shared link; original installation made
54 exact-byte copies. Final tree: 176 files/247 members/71 directories.
Built/installed DSOs are distinct files with identical 1,151,400 bytes, SHA256
`63d04394abffea6176b7a2b7f88dea7cd8fed578d06b30f458bccf0ab92e27c4`,
AArch64 ELF64 ET_DYN, SONAME librosbag2_cpp.so, 79 NEEDED and no RPATH/RUNPATH.
This does not qualify physical loading, provider ABI or runtime closure.
The five original configure/build/build-readelf/install/installed-readelf
commands (PIDs 40932/60496/60592/26556/75768) each ran once, exit 0. No target
command was repeated to correct an audit.

Final tester PID 44840 at 13:27:18.887705–13:27:53.105293 UTC exited 0 with
empty stderr, refreshing 7,763 finite inputs, 718 W3 tracked raw files plus
two gitlinks, target membership, 13 seals, 17 read-only Git commands, 54 copies,
five original receipt/chronology joins and final-compatible install/ELF audits.
All 134 negative controls and symmetric positive comparison passed. Broader
unrelated historical guards remain sealed provenance, not freshly retraversed.
Original 18-versus-17 configure-child, class_loader physical-alias and tuple/list
audit STOPs remain historical; new source-backed audit versions changed no
source/tool/environment/configuration flags. Final tester PID 2464's W3-relative
path harness STOP and narrow V2 correction are also preserved. No failure was
retroactively marked PASS.

SDK23/.32 versus board .31, Windows absolute metadata/POSIX setup/relocation,
transitive constructors and plugin loading remain unqualified. The inspected
local APIs need no Node/Participant; unexpected Transport/identity activity
must stop the future run. Independent SQLite send-time persistence remains
source-unsupported, runtime-unmeasured. Source-only acceptance/bundle notes
do not establish implementation or device execution.

Normal-user read-only HDC at 12:10:08 UTC observed both known USB boards
Connected, client exit 0 and empty stderr, after an empty sandbox discovery.
No deployment/runtime/permission operation occurred; connectivity is not
DEVICE_TESTED. Native communication remains BLOCKED only on the full current
project-specific token record and supported lossless restoration material.
Earlier whole-registry automatic approval rejection was not retried; no new
security write occurred. Windows fake TCP/stubs remain separate evidence.

Last confirmed publication is SQLite documentation commit
`a946e9e5e3e8e6d2442f3ac81ac00da23f8595ad` on existing
`origin/codex/flow-encoding-published-20261014` (11:57:42 UTC commit receipt).
Its 11:59:02 UTC exact-SHA CI snapshot is NOT_RUN: zero workflows/checks/status
contexts. No cpp commit or future CI result is predicted. See
[target preparation](ROS2_ROSBAG_TARGET_PREPARATION.md) for exact phase times,
current capability matrix, canonical initial/review/final seals and retained
historical evidence. Local ignored artifacts/evidence are not remotely included.

## Earlier R19 SQLite plugin original-package static qualification (2026-10-16 11:42 UTC pre-publication snapshot)

Final **SQLITE_PLUGIN_PACKAGE_BUILT_INSTALLED_STATICALLY_QUALIFIED** has
bounded Commit gate PASS for the original rosbag2_storage_sqlite3 0.26.11
package build/static install, with **STOP_BEFORE_CONSUMER_RUNTIME_OR_DEVICE**.
All three original C++ TUs, Python two-copy/setup.cfg/egg_info/compileall actions
were retained for OHOS SDK23 arm64-v8a/c++_shared. Configure/Ninja/build-readelf/
install/installed-readelf each ran once, exit 0 without timeout (PIDs
71444/58952/73712/43516/65388). Seven Ninja actions gave 97/139 build files/
members; original installation's 23 operations/33 copies plus manifest/pyc
gave 132 files/194 members/62 directories. Built/installed DSO bytes match:
879,024 bytes, AArch64 ET_DYN, expected SONAME, 80 NEEDED, no RPATH/RUNPATH.

FIX2 binds both original metadata generators in function and root scopes,
correcting the old 18-child candidate to exact 19 without changing upstream
source. FIX1 remains BLOCKED. Initial parser STOPs and alias corrections stay
historical. Formal Tier 2 agentic review found no actionable issue or required
code change. Final tester PID 50752 at 11:33:01-11:36:14 UTC exited 0, empty
stderr/no timeout: historical receipts/snapshots/canonical audits, fresh live
install/installed-ELF and immutable configure/build checks passed. Complete
guards matched 26,278 protected records, 41 seals, four Git states, old SQLite
79/119 and storage 138/194. No target command or historical full-stage API was
rerun. Three final harness platform/argv/tuple-JSON failures remain retained
with narrow tooling fixes; they were not package-command failures.

Only **BUILDS/static_install PASS** is qualified. HOST_TESTED/HOST_RUNTIME,
DEVICE_TESTED and INTEGRATION_TESTED remain false; overall **NOT VERIFIED**.
Pre-authorized BUILD_TESTING=OFF leaves upstream unit/gmock/lint NOT_RUN.
Windows Lib/lib native alias does not qualify case-sensitive board layout;
host Python 3.12.3 fresh pyc static checks do not qualify board Python/CLI.
Compileall child exit is NOT_CAPTURED. Original host hooks/absolute metadata,
POSIX setup/relocation, target ABI/loader and transitive constructor gaps
remain; 80 NEEDED is not runtime closure. No plugin load or raw/typed SQLite
write-close-reopen-read ran. Independent send-timestamp persistence remains
source-unsupported and runtime-untested.

Next is the complete original rosbag2_cpp package, then a fixed String local
SQLite consumer. Source-only readiness found 13 existing dependency Configs
and 20 original source entries/19 unique TUs; no cpp build/runtime has run.
Native ROS 2/Actions/cross-board still need the complete project-specific
fastdds_dsoftbus_probe original token record and supported lossless restore.
Existing temporary authorization remains conditional; whole-registry automatic
approval rejection was not retried. No permission/security change or new device
test occurred. Host fake-TCP/stubs do not prove real DSoftBus communication.
The separate 09:34:08 UTC two-board USB/platform/space observations were
read-only: seven HDC client exits 0/empty stderr, independent remote exit
uncaptured, no deployment/runtime/permission query or change. They do not
establish current connectivity or device acceptance.

Last confirmed publication: `a8afcba30f27a358172f24b19c6fbc465ce327fc` on existing
`origin/codex/flow-encoding-published-20261014`; its 09:30:56.023975 UTC CI was
NOT_RUN in `storage-install-main-publication`. Main's later graph/PRE_COMMIT/
publication receipt owns this stage's commit and CI. See
[target preparation](ROS2_ROSBAG_TARGET_PREPARATION.md) for the current matrix,
artifact/evidence hashes and retained historical snapshots.

## Earlier R19 original installation qualification (2026-10-16 09:25 UTC pre-publication snapshot)

Final **ORIGINAL_STORAGE_INSTALL_AND_STATIC_QUALIFICATION** has bounded Commit
gate PASS, with **STOP_BEFORE_CONSUMER_OR_RUNTIME**. The original install script
ran once at 09:11:59 UTC (PID 54060), exit 0 and empty stderr. Its 42 exact
copies, manifest and 16 directories give 138 files/194 members; the old 95 files
and installed DSO bytes are unchanged. All 65 trace events, 20 INSTALL operations
and the sole final WRITE passed. Formal review found no actionable findings;
final replay/fresh guards at 09:21:01–09:21:57 UTC passed with no failure or
reinstall, including 18,572 file records and all four protected Git states.

POSIX setup remains incomplete: no local_setup.sh, original bat plus empty dsv,
and absolute Windows parent prefixes. Separate shell hooks do not constitute
a target setup entry. This does not prove the library is uncallable. Complete
Ninja package build, consumer/loader, SQLite/cpp/local I/O, runtime and device
checks remain NOT_RUN. Native project-only permission recovery/restore remains
BLOCKED; earlier failures and the whole-registry approval rejection are retained.
No new board test or permission change occurred. Overall NOT VERIFIED.

Last confirmed publication: `069aa7a64188172d3c1b160d9ffc15cf212452e8` on existing
`origin/codex/flow-encoding-published-20261014`, with 08:34:30.710397 UTC CI NOT_RUN
in `storage-link-main-publication`. Main's later receipt records this stage's
publication/CI. See [target preparation](ROS2_ROSBAG_TARGET_PREPARATION.md) for
capabilities, limits and canonical hashes; earlier dated sections preserve history.

## Earlier R19 storage DSO static qualification (2026-10-16 08:27 UTC pre-publication snapshot)

Final **ORIGINAL_STORAGE_DSO_LINKED_AND_STATICALLY_INSPECTED** has bounded
Commit gate PASS with **STOP_BEFORE_INSTALL**. The original link ran once at
08:04:21 UTC and static readelf once at 08:04:54–08:04:55 UTC, both exit 0
with empty stderr. All 97 actual inputs, 181 response tokens and 121 trace
lines were qualified. The 985,840-byte AArch64 ET_DYN has the expected SONAME,
78 statically explained NEEDED entries, no RPATH/RUNPATH and 252 named dynamic
imports. This is not proof of loader, ABI or runtime compatibility.

Formal post-test review found no blocking issue. Final verification at
08:20:48–08:24:51 UTC replayed evidence and refreshed all guards without another
toolchain execution. The three planned rsp/DSO/tar outputs give 95 files/135
members; old 92 files are unchanged. Fresh 18,507 file records, 173 memberships,
29 seals, 215 absences and four Git states passed. The archive parser preparation
failure, reviewer read/syntax errors and final Windows audit-argv collector
exit 1 remain recorded with narrow fixes; none was a real link failure.
Protection remains sampled, not continuous filesystem/process/OS closure.

Complete Ninja package build, installation, SQLite/cpp/local I/O, plugin
runtime and device tests remain NOT_RUN. Next is a separately reviewed install
oracle derived from the real DSO/generated scripts. Native project-only
permission recovery and lossless restore remain BLOCKED; the whole-registry
automatic-approval rejection was not retried. SQLite's independent send-timestamp
source limitation and SDK/board compatibility gaps remain. Overall NOT VERIFIED.

The last confirmed publication is `0e493f839fec66b4c85a1fd22559b7dbade17661`
on existing `origin/codex/flow-encoding-published-20261014`; its 07:25:32 UTC
CI snapshot was NOT_RUN, recorded in `storage-objects-main-publication`.
Main's subsequent publication receipt records this stage's commit/push and CI.
See [target preparation](ROS2_ROSBAG_TARGET_PREPARATION.md) for exact artifact
and evidence hashes, current capabilities and retained historical snapshots.

## Earlier R19 seven storage objects (2026-10-16 07:19 UTC pre-publication snapshot)

Final **ORIGINAL_GENERATED_7_OBJECTS_COMPILED_AND_STATICALLY_CHECKED** has
bounded Commit gate PASS, with **STOP_BEFORE_DSO_LINK**. Seven original expanded
Ninja compiler argv ran directly and serially at 06:52:51–06:53:17 UTC, once
each, all exit 0. The original flags remain unchanged; every stdout is empty
and each job has the exact newly authorized unused `--gcc-toolchain` warning.
Actual depfiles select 846 distinct dependencies, and all seven outputs pass
bounded AArch64 ET_REL checks. This does not establish a complete Ninja package
BUILDS or HOST_TESTED result.

Formal post-test review found no blocking finding or required source fix.
Final testing at 07:11:59–07:13:06 UTC replayed the unchanged objects/dependencies
and refreshed guards, with no new compile/configure/Ninja/link/readelf/install/
device command or final failure. The old 78 files remain unchanged; fourteen
object/depfile additions give 92 files/132 members. Fresh 18,383 file records,
173 memberships, 24 seals, 154 absences and full four-tree Git guards passed.
These are per-job terminal snapshots and before/after protection, not realtime
filesystem or complete child-process/OS closure.

The original private-header pretest P2 BLOCKED record remains; FIX1 added its
exact binding, actually consumed by object-05. The general `ilog2.hpp` search
case remains UNKNOWN despite non-consumption in all seven jobs. The failed
relative-root preparation probe, original collector sources, precise pre-run
V2 Git-warning/full-diff correction, two reviewer representation errors and
all earlier configuration failures remain retained in local evidence.

**Final DSO link, actual final link inputs, install/RPATH, plugin runtime,
local Writer/Reader and device acceptance remain NOT_RUN.** SDK .32/board .31
and generator 4.6.7/target runtime 4.6.9 compatibility remain unverified; bare
`-lrt` final selection is untested. Next is a separate bounded final DSO link
and installation oracle, then SQLite/cpp/local I/O. Native communication remains
BLOCKED by the missing complete project-only recovery record and lossless
restore route; the automatic approval rejection of the whole-registry route
was not retried. DATASYNC remains NOT_QUERIED and permissions unchanged.
Overall R19/native ROS 2 remains NOT VERIFIED.

The preceding stage was published as `b94fa6f6375c541b3170e7fc73fea1633a5ac1cb`
on existing `origin/codex/flow-encoding-published-20261014`; its 06:02:41 UTC
CI observation was NOT_RUN (zero workflows/checks/status contexts), recorded
in `storage-configure-main-publication`. Main's subsequent publication receipt
records this stage's graph/PRE_COMMIT, commit/push and CI outcome. See
[target preparation](ROS2_ROSBAG_TARGET_PREPARATION.md) for the capability matrix
and exact local evidence hashes. Earlier dated sections retain their history.

## Earlier R19 storage configuration evidence (2026-10-16 05:53 UTC snapshot)

Independent final testing reports **CONFIGURE_INPUTS_AND_ID_ABI_STATIC_QUALIFIED**,
with mandatory terminal **STOP_BEFORE_ORDINARY_OBJECTS**. The one FIX2A
configure ran 05:19:53–05:19:55 UTC, exit 0, followed by two ABI dependency
queries, one Ninja command inventory and the input audit. Post-test review
found no actionable findings. Final testing at 05:49–05:51 UTC replayed the
unchanged evidence and refreshed guards; it ran no new configure or compiler.
Seven ordinary objects, the storage DSO, install, readelf and runtime/device
tests remain NOT_RUN. This is not storage BUILDS or HOST_TESTED.

The 4,129 bound inputs include actual SDK 6.1.0.32 metadata and the selected
unversioned RMW library. Both ament generated inputs are real outputs.
The 59 source-bound dev diagnostics and 17 explicit child calls were audited;
independent child stdout/exit remains UNOBSERVED. Original storage1's missing
generator failure, storage2's new helper diagnostics/unbound RMW STOP, FIX2's
exact Python-argument P2 and all collector failures remain preserved. FIX2A
corrects only five contract path strings; it does not relax parser matching.

Host rosidl generator 4.6.7 versus target runtime 4.6.9, and SDK 6.1.0.32 versus
observed boards 6.1.0.31, have no compatibility qualification. Future `-lrt`
has a known export source but no proven linker selection; FindFastRTPS cache
hints prove neither API/ABI nor version compatibility. Next is separately
gated ordinary compilation, then link/install and SQLite/cpp/local I/O.
SQLite source does not persist an independent send timestamp; runtime
reproduction remains unrun. Overall R19/native ROS 2 remains NOT VERIFIED.

Both boards were observed online during the 03:36–03:37 UTC read-only resource
baseline; no storage artifact was deployed or tested. DATASYNC was not queried
and permissions were not changed. Native communication remains BLOCKED by the
missing complete project-only recovery record and lossless restore route;
automatic approval review's whole-registry rejection was not retried.

At this snapshot the preceding stage was published as
`7e5dd899f34a6b019ead41b328c79263e41de6bf` on the existing
`origin/codex/flow-encoding-published-20261014`; its 03:32:38 UTC CI snapshot
was NOT_RUN. This stage's subsequent graph/PRE_COMMIT, publication and CI
outcome belong to Main's publication receipt, not this pre-publication snapshot.
See [target preparation](ROS2_ROSBAG_TARGET_PREPARATION.md) for current scope
and evidence; earlier dated sections retain their historical state.

## R19 fresh pluginlib configuration (2026-10-16 03:23 UTC)

The new root-scope **ament → pluginlib → TinyXML2** configuration has final
**PASS_FOR_BOUNDED_FRESH_PLUGINLIB_CONFIGURATION_EVIDENCE_ONLY**. TinyXML2
is absent before pluginlib; the original vendor/module, CONFIG adapter and
imported targets then load in the required order. The sole FIX1 configure ran
at 02:58:26 UTC, exit 0. Independent post-test review required no source fix;
final read-only replay at 03:20:35–03:21:40 UTC passed, with no new configure,
compiler, link, install, readelf or device execution.

The original missing-dl STOP and pretest coverage gap remain unchanged. FIX1
adds one exact SDK CMAKE_LIBRARY_PATH to the isolated harness and proves the
unchanged rcutils helper's real lookup and complete two-library legacy export;
the modern interface remains literal `dl`. The selected `libdl.a` is an
eight-byte empty archive, not runtime loader code. This closes the named
harness search-context P2 only. Collector failures and the broader Python/DLL/
OS-observation limits remain in the evidence; no warning was waived.

Next is the original seven-source rosbag2_storage build with its own full
compiler/link/install qualification, then SQLite/cpp and local Writer/Reader
acceptance. Those steps, runtime plugin loading and native ROS/DSoftBus remain
**UNVERIFIED**; R19-PT1 stays **OPEN overall**. Local component preparation can
proceed without token writes, subject to unverified transitive DSO behavior.
The latest HDC snapshot (02:47:09 UTC) showed both boards USB Connected only.
DATASYNC remains NOT_QUERIED, no permissions changed, and native communication
still needs a complete current project-only record and lossless restore route.
The rejected whole-token-registry route was not retried.

The preceding consumer/wiring documentation stage was pushed as
`53a13dfc5af752c03538a990bb0ae253238cfdd7` to the existing
`origin/codex/flow-encoding-published-20261014`; its 02:23:05 UTC CI snapshot
was NOT_RUN. This update is not yet committed/pushed; Main's graph/PRE_COMMIT
and publication gates remain pending. See [target preparation and evidence](ROS2_ROSBAG_TARGET_PREPARATION.md).
Earlier dated sections preserve their then-current scope and failures.

## R19 static consumers and host-aware ament wiring (2026-10-16 02:06 UTC)

Independent post-review final testing qualifies **13 static consumer cases
(4 positive, 9 expected negative)**, five ordinary AArch64 object/link pairs
and 15 metadata-only installed files. Seven fresh readelf commands passed;
final testing did not reconfigure, rebuild, reinstall or execute a target.
The installed-pluginlib case preloads TinyXML2. It does **not** establish
pluginlib-first discovery, actual plugin loading or ClassLoader lifecycle.
The plugin XML deliberately names `not_a_runtime_plugin`.

A private ament prefix helper changes only `NOT WIN32` to
`NOT CMAKE_HOST_WIN32`; the installed host helper is unchanged. Four isolated
script cases passed final testing (the non-Windows branch is a Windows-hosted
model, not Linux testing). The private Config shim then passed two positive
real-ament dummy configurations and one exact-version rejection case, each
executed once with LANGUAGES NONE. Independent post-test review found no
required source fix; final testing completely replayed their unchanged raw
records without reconfiguration. This qualifies the measured Windows-host /
OHOS-target prefix and directory-scope behavior, not a ROS package build.
Full Python-import/native-DLL/OS-I/O closure remains NOT_PROVEN.

B and pluginlib A originally omitted C ABI warning classification. The exact
15 existing C jobs now have a reviewed retrospective decision and independent
initial/final replay. This supplements current static assurance; it does not
make the earlier gates compliant at execution time. Old STOP results,
withdrawn CASE PASS labels, collector failures and uncertain loaded-source
bytes from the failed overlapping collector remain preserved. Class_loader
had a different original contract and receives only its named assurance
supplement. No general or future warning exemption follows.

R19-PT1 remains **OPEN**; ROS 2 over native DSoftBus remains **NOT VERIFIED**.
Next is a separate fresh pluginlib-first LANGUAGES NONE acceptance, then the
unchanged seven-source rosbag2_storage build with complete new input bindings.
Storage, SQLite plugin loading and local Writer/Reader are still unbuilt or
unverified in R19. Source inspection supports local component tests without
native-token writes; their transitive runtime behavior still needs testing.
Native cross-board communication independently remains BLOCKED by the missing
complete current project-only token record and supported lossless restore
route. DATASYNC is NOT_QUERIED; no security writes occurred. Automatic approval
review's full-token-registry rejection was not retried. The last HDC visibility
snapshot was 2026-10-15 23:35:14 UTC, both boards USB Connected, not a new device
test. R14's original strict failures and R18 Action's bounded Host/fake-Socket
scope remain unchanged.

The preceding documentation stage was published as
`770d3e8b75b6bd7168acbc7a23f175ed7e51b7b1` to
`origin/codex/flow-encoding-published-20261014`; its 23:33:09 UTC CI snapshot
was **NOT_RUN** (zero workflows/checks/status contexts). This two-document
update is not yet committed/pushed. Final tester gates cover only static
consumer evidence and bounded wiring evidence; Main graph/PRE_COMMIT and
publication gates remain separate. See [the current capability matrix and
local evidence ledger](ROS2_ROSBAG_TARGET_PREPARATION.md). Earlier dated
sections below are historical snapshots, including then-unexecuted consumers
and original A diagnostic conclusions.

## R19 pluginlib target build and static installation (2026-10-15 23:19 UTC)

The fresh SDK23 AArch64 `c++_shared` `pluginlib-sdk-closure-fix1` build has
**PASS_POST_REVIEW_FINAL_STATIC_PLUGINLIB_A**. The successful continuation
ran seven authorized commands (all exit 0), including one ordinary compilation,
one link and one install. Independent agentic review found no blocking finding
or required source fix. Final testing replayed the immutable raw evidence and
ran two fresh readelf inspections of built/installed `list_plugins` (both exit
0, outputs identical to the initial inspection); it did not rebuild, reinstall
or execute the target. All 29 installed files and 15 directories match the
source/generated-byte contract. **BUILDS=true; HOST_TESTED=false,
DEVICE_TESTED=false, INTEGRATION_TESTED=false, VERIFIED=false** for runtime or
transport behavior. R19-PT1 remains **OPEN overall**.

pluginlib is an INTERFACE target plus `list_plugins`, not `libpluginlib.so`;
no real test-plugin DSO was built or loaded. The original `pluginlib-next-test`
A_BLOCKED result remains immutable. The reviewed continuation corrected the
export expectation to CMake 3.28.3's original imported-target spelling:
source/build ends in `tinyxml2::tinyxml2`, generated/installed export in
`tinyxml2`. Exact target identity/location/hash checks were retained. Collector
failures and the classified known gcc-toolchain warning remain recorded.
See [R19 target preparation](ROS2_ROSBAG_TARGET_PREPARATION.md) for the exact
artifact, input closure, correction boundaries and local evidence hashes.

class_loader's earlier static qualification remains valid. The planned four
positive/nine negative consumer cases, pluginlib-first directory scope, real
plugin loading/unloading, fixed String SQLite Writer/Reader and all DDS
recording behavior remain **UNVERIFIED / NOT_RUN**. Compiling `list_plugins`
does not establish general TinyXML2 6 alias/API compatibility or a full ROS 2
runtime result.

The preceding class_loader documentation stage was committed and pushed as
`5e96a3aba1af8185a05ed195f114128557344ac8` to
`origin/codex/flow-encoding-published-20261014`. Its 21:47:50 UTC CI observation
had zero workflow/check runs/status contexts: **CI NOT_RUN**. Main's 23:18:07 UTC
identity precheck found that same remote SHA. This pluginlib documentation
stage has not been committed or pushed; Main graph/staging/PRE_COMMIT and
publication/CI gates remain pending. Final tester gate is
**PASS_FOR_DOCS_ONLY_PLUGINLIB_STATIC_EVIDENCE**. Referenced raw evidence is
local ignored build content and is not included in the remote repository.

At 21:51:22 UTC HDC listed both RK3588A boards USB Connected; that establishes
visibility only, not new device testing. Native permission restoration remains
**BLOCKED** pending a complete current project-only token record and supported
lossless restore route, despite conditional authorization. Current DATASYNC is
**NOT_QUERIED**. Automatic approval review's full-token-registry rejection was
not retried; no security changes occurred. R18's bounded native-load/rejection
and historical cross-board evidence retain their original limits. Overall ROS 2
over native DSoftBus remains **NOT VERIFIED**. Earlier dated sections below are
historical snapshots, including their then-unexecuted pluginlib status.

## R19 class_loader target build and static installation (2026-10-15 21:37 UTC)

The fresh SDK23 AArch64 `c++_shared` build `class_loader-sdk-closure-fix1`
has **CLASS_LOADER_TARGET_INPUT_BUILD_INSTALL_FINAL_STATIC_PASS** after initial
execution, independent agentic review with no actionable findings, and fresh
final static checks. Final testing reused the unchanged successful build and
ran one new `llvm-readelf -h -d` (exit 0); it performed zero new configure,
compile, link or install commands. Four objects, three actual link archives,
112 outputs and 27 installed files are bound. Public exports are exactly
`console_bridge;rcpputils`. **R19-PT1 is closed only for this named fresh
class_loader build/static-install subset; R19-PT1 remains OPEN overall.**

For this scope BUILDS=true; HOST_TESTED=false, DEVICE_TESTED=false,
INTEGRATION_TESTED=false and VERIFIED=false. pluginlib, consumer probes,
rosbag I/O, native ROS activation and device/cross-board execution were not run.
The older class_loader implicit-header/link-input BLOCKED result, first
configure setup failure, collector corrections and review/final checker
failures remain preserved. Complete canonical argument comparisons use
44/48/53 tokens. See [R19 target preparation](ROS2_ROSBAG_TARGET_PREPARATION.md)
for precise scope, artifact identity, retained failures and sealed evidence.

The preceding documentation stage was published as
`473312f2ab3c6bf9edcad65e4504c82226f41ee3` on
`origin/codex/flow-encoding-published-20261014`. Its 20:30:59 UTC snapshot
observed zero workflow/check runs/status contexts: **CI NOT_RUN**. Main's
precheck receipt at 21:34:47 UTC records the same remote SHA. This update
does not claim a subsequent commit, push or CI result. Final tester gate is
**PASS_FOR_DOCS_ONLY_CLASS_LOADER_STATIC_EVIDENCE**; Main's graph, staging,
pre-commit and publication gates remain separate.

Native permission restoration remains **BLOCKED**: conditional permission
authorization exists, but a complete current project-only native-token record
and supported lossless restore route remain unavailable for either board.
No grant/revoke/token switch/reload/security write occurred; current DATASYNC
state is **NOT_QUERIED**. The full-token-registry route was rejected by
automatic approval review and was not retried. R18's two-board native-main
load/missing-identity rejection PASS remains a separate diagnostic claim,
not communication acceptance. Overall ROS 2 over native DSoftBus remains
**NOT VERIFIED**. Earlier dated sections retain their historical scope.

## Windows Host connection/receive diagnostics (2026-10-15 04:41:11 UTC)

R11 independent final testing qualifies the 22-file Host diagnostic candidate
after both P2 review findings were repaired: fresh build/install, 31 CTest,
60 Python cases and an installed C++17 consumer PASS. Macro-off core first ran
131/132 with one original 90-second timeout; one original-limit rerun passed in
30.04 seconds. Concurrent work and sandbox context both changed; cause remains
UNATTRIBUTED. SDK23 ARM64 compile/link/ELF PASS is compile-only.

Actual Windows TCP connection fixtures and bounded receive occupancy/release
passed. The latter observed six callbacks, five charges/refunds, one LocalBytes
refusal and an explicit accepted resend. A fresh two-process ROS service run
passed 14 requests/response calls, 12 accepted responses, two timeout removals
and two recreations, with strict full_host callback/connection/lifecycle joins.
All 23 focused strict mutations were rejected. Retained compiler/timeout failures and negative-control forced cleanup
remain explicit in the evidence.

See [Host connection and receive observation](HOST_RECEIVE_OBSERVATION.md).
Tester Commit gate PASS and documentation readiness are scoped to this diagnostic
change; Main graph/pre-commit/publication/CI gates remain separate. Parameter
stage `41cdcd13a791eb1e32b9a02f0fac8363f3d5def5` was already published separately.
V1-05/V1-07 remain PARTIAL Host; denominator 30 and unknown aggregate completion
are unchanged. No new full ROS feature is qualified. Historical A/04 was not
rerun: current security-OFF 2.14.5 is not a qualified substitute for original
security-ON 2.14.7, and DATA_FRAG identity is absent. Real callback overtaking
and reconnect budget competition remain unverified. DEVICE_TESTED=false; both
boards' permission is last-known DENIED at 2026-10-14T21:51:07Z, not refreshed.
Native/cross-device acceptance remains BLOCKED and overall NOT VERIFIED.
Earlier sections, including parameter evidence and R10 plans, remain historical.


## Windows Host ROS parameter acceptance (2026-10-15 03:34:47 UTC)

The additive parameter acceptance target passed an independent fresh final build
and one actual ROS Windows Host pair after review repair: 17 completed remote
operations, three accepted mutations, four rejected batches with readback, and
three change events. RPC 17 verifies the exact final name set; the server also
asserts that the rejected undeclared parameter is absent. Complete captures are
A 1030 / B 1085; both processes and driver exit 0. A's six rejected callbacks
remain unexplained (B zero). Final declaration events were zero, so replay is
not verified. The original failed compile and corrected 16-call initial run are
preserved separately; 19 final synthetic log checks are not business runs.

V1-07 is PARTIAL for this bounded Host scenario; full parameters, action and
native/cross-device acceptance remain NOT VERIFIED. The 30-row denominator is
unchanged, with no aggregate completion percentage. Testing used the immutable
R8 W2 bridge, not the unfinished concurrent R11 diagnostics. This stage performs
no historical A/04 rerun and makes no diagnostic qualification claim. Device
permission remains last-known DENIED (2026-10-14T21:51:07Z), not freshly queried.
See [ROS parameter acceptance](ROS2_PARAMETER_ACCEPTANCE.md) for scenario,
commands, review/final evidence, hashes and remaining gaps. Tester commit gate
PASS is scoped to these additive parameter files; Main's documentation, graph,
pre-commit, publication and CI gates remain separate. Earlier sections below,
including the R10 statement about parameters, retain their historical scope.

## Windows Host ROS service acceptance (2026-10-15)

The additive service acceptance target passed independent initial and post-review
fresh builds and one real ROS Windows Host pair at each stage: 14 server response
calls, 12 verified accepted responses and two explicitly removed timeout requests.
The scope covers four outstanding requests per direction, reverse replies,
request/writer/value correlation, 250 ms application wait, recovery, client
recreation and absent service. Final captures are complete (A 983/B 985), with
zero rejected callbacks; initial B's two rejected callbacks remain in evidence.
The exact response-send timeout warning now fails the gate, and the removed
future must report the exact broken-promise code. Independent RMW late-response
discard was not observed. Final testing used the R8 W2 final bridge DLL; this is
new service evidence, not a rerun of the earlier pub/sub or historical A/04 cases.

V1-05 remains PARTIAL; full service/action/parameters and native/cross-device
acceptance remain NOT VERIFIED. The frozen 30-row capability denominator is
unchanged, with no aggregate completion percentage. Failed startup/connect runs
are retained; failed-connect abnormal teardown remains unqualified. Device
permission was last known DENIED, with no fresh device/security changes. The
parallel A/04 diagnostic lane produced a design only: peer endpoint identity and
collision-safe refusal/release/budget evidence remain PLANNED. See
[ROS service acceptance](ROS2_SERVICE_ACCEPTANCE.md) for exact scope, commands,
initial/final evidence, review closure, retained failures and diagnostic priorities.
Main's pre-commit/publication gate remains separate.

## Windows Host IO diagnostic scope (2026-10-15)

The independently reviewed, default-off Windows Host Socket diagnostic patch
has a fresh post-review build/install and scoped final tester PASS: 27 CTest,
29 Python checks, 7 actual DLL invalid-configuration cases and 4 synthetic
runner replays. The one initial ordinary ROS Host reliable pair delivered
10 writes/10 takes with complete A/B captures; B recorded one rejected
callback. Final testing revalidated those same-source logs; the final DLL did
not rerun the business pair. Native/RK3588A/cross-device tests are N/A for this
Windows-only patch; broader product acceptance remains BLOCKED/unverified.
Historical A/04 failures and device permission/deployment gates remain open.
See [Host IO diagnostics](HOST_IO_DIAGNOSTICS.md) for usage, exact local evidence,
retained failures and limitations. Main's pre-commit/publish gate is separate.

Historical sections in this file retain the exact names, counts, hashes, protocol
markers and permission identifiers from their original runs. They are labeled by
their work-package dates and artifact paths; they do not describe the current
`FastDDSDSoftBusTransport` package unless a section explicitly says so.

> **历史累计实验基线（2026-09-27）**：A 的七场景与真实 CloseBusy 责任修复，加上 B 的 opt-in 决策诊断及稳定化验证。旧 111/6 未知与新 107/2 已观察原因分别保留。
> 范围、容量单位、依赖与证据见 [bounded DDS stabilization baseline](BOUNDED_DDS_STABILIZATION_BASELINE.md)。该历史 checkpoint 的 isolated Host CONFIG suite 为 `445/445 PASS`；设备未运行，未进行新的 ASan。下列工作包状态是历史快照，不代表当前 Git 提交状态或生产放行。

## 前序 checkpoint：G2A1_V1_LOCATOR_INTEGRATION（2026-10-04；Host/build acceptance snapshot）

本工作包的当前状态为：**`V1_ADDRESS_CODEC_PRODUCTION_PATH_IMPLEMENTED / V1_ADDRESS_CODEC_HOST_INTEGRATION_VERIFIED / ROS214_AND_API23_TARGET_BUILD_VERIFIED / PRE_COMMIT_GATE_PASS / READY_FOR_COMMIT / DEVICE_V1_NOT_RUN / NOT_PRODUCTION_QUALIFIED`**。生产路径已接入 V1：`LocatorCodec` 是 V1 的兼容薄封装，`DSoftBusTransportDescriptor::ConfigureV1` 在发布前生成并验证有界身份目录、碰撞和服务名，`ValidateV1` 要求同一完整 locator 用于 metatraffic/user advertisement；运行时 descriptor 在一次性创建前再次验证。`EndpointDirectory`、完整 locator selection、Host bridge、TargetBridge 和 public socket DDS probe 均使用该路径。未进行 HDC、设备或跨设备操作；本工作包不实现 re-accept，PF/P4 和生产资格保持原状态。

V1 合同记录如下：私有 kind `0x40000000`；地址键为 SHA-256 前 16 字节，输入为 ASCII `MDDS-V1-DEVICE`（含 NUL）、namespace 长度 `u16BE`、namespace bytes、domain `u32BE`、UDID 长度 `u16BE`、UDID bytes；namespace 为 1..116 个不含 NUL 的不透明字节，UDID 为 1..64 个不含 NUL 的不透明字节。port 高 8 位为 profile，低 24 位为 `slot + 1`；profile 1/2/3 结构合法，当前 native pipeline 只支持 `BytesNative` profile 1，slot 为 0..`0x00fffffe`，零低位、非法 profile 和溢出拒绝。独立 fixture 的 synthetic `alpha/0x01020304/udid-1` 向量为：`4d4444532d56312d444556494345000005616c706861010203040006756469642d31` → `c3797784976ed68cef55b2f0f22e8cc4`。

最终 tester evidence：Release full CONFIG `552/552 PASS`（`build/g2a1/final-test/final-config552-final*`，`-j2`，672.56 s）；受影响 ASan `208/208 PASS`（`-j2`，预构建 Fast DDS/Fast CDR/foonathan DLL 未插桩，TSan 未运行）；安装并外部 relocated consumer `PASS`；Host Fast DDS 2.14.5 与 API23 TargetBridge clean-first build/link/static gate `PASS`；`Host214 CTest 14/14 PASS`。ROS 双进程 paired-close `20 writes / 20 takes PASS`（`ros-dual-final`）；failure-close harness 本身 exit 0，所监控 child processes 按预期各 exit 4；forward-0 pair 返回 1，属于保留的负向 ledger（A/B 均 exit 4，writes/takes 0，sends 15，ingress 0，active pumps 0），不能当作普通成功。两项原始 death test 在 common working directory 下因共享 `srtc_close_child_output.txt` 竞争而失败；最终测试采用每个测试唯一工作目录并发运行，断言未改变，也没有 source fix。`experiment.matrix_control_receipts` 的旧高并行失败仍单独保留，原因未解释。

独立 review 已清除当前生产路径的阻塞 finding；receipt 为 `build/g2a1/REVIEW.md`。Debug 配置只完成静态审计，未运行；最终 probe identity migration 已检查。当前仍未证明：设备 V1 执行、跨设备运行、TSan、所有并发 race、SDK 内部 drain/cancellation/quiescence、re-accept，以及生产 qualification。A5/C1 的既有 table/route lifetime 限制和 `NO_REACCEPT` 不变。旧 `93e2dd1` 的过程记录不回填本工作包 receipt；本节只描述本次 named tester/reviewer evidence。

## 当前工作包：V1_LOCATOR_DEVICE_ACCEPTANCE（2026-10-05；final raw acceptance）

本工作包状态为：**`V1_LOCATOR_DEVICE_VERIFIED / ROS2_V1_DUAL_BOARD_VERIFIED / COMMIT_GATE_PASS / NOT_PRODUCTION_QUALIFIED`**。最终独立 raw acceptance 使用 run ID `e72e18d67cbc5b6d`、base Git HEAD `d670773a8385194c409b09b3d028d44edb01b81e`，并绑定 reviewed working-tree native-name/capture/config revision；device binaries 由 `build/v1c26/bound-build` source/manifest receipt 绑定，不表示 device execution 时已有新 commit。运行是 iteration 26、`windows/data-16`；不可变 evidence index SHA-256 为 `aa295d6c5376bccacccaf16d41afd5f783f0cc65dd0b29ed10fd05f1e87066fe`。最终 tester receipt 为 `build/finalpostreview/FINAL_TEST_GATE.json`，Commit Gate=`PASS`；raw acceptance receipt 为 iteration-26 `INDEPENDENT_V1_RAW_ACCEPTANCE.json`，index 为 `V1_RAW_EVIDENCE_INDEX.json`。原始 `build/v1-device-20261004/final-gate/FINAL_GATE.json` 是历史 blocked preflight receipt，保持 `FAIL`，不作为 final-pass evidence。此前只读 preflight 的 policy denial 仍保留为早期 gate evidence，不回写为本次 Socket/device 失败。

最终窗口完成两板双向 ROS 2 V1 业务：两种 QoS 合计 `20/20`，graph、两个跨板 service exchange、原 Participant delete、Runtime close、public return 0 和正常进程退出均通过；SDK-correlated `20` takes 与 SPDP `SendBytes`/ingress 各板 `14/14` 完整。SPDP 实际 PID `0x0031/0x0032` 均保留完整 kind、16 字节 address、profile 1、slot 0、port `0x01000001`；选中账本为 A=`95`、B=`68`。109 成员的新 package、live origins 和 normal restore 均通过。两板 policy current hashes 与保存值一致，未发生平台 policy change，未重复 SDK query。

保留边界：A 侧 19 个 `InvalidState` callback、B 侧 0 个以及 startup cleanup flag 0 按原始收据记录；这不构成 cleanup-tail、zero-loss、SDK epoch 或 drain 证明，SDK epoch/drain 仍未证明。原始 iteration-23 `windows/query-16` cleanup failure 与 consumed claim 保留；iteration-26 `windows/data-16` normal restore 通过，仅追加的 recovery 12-command 收据通过。旧 29-command read-only preflight 和 2 policy receives 不被事后改写为 Socket/device failure。A5/C1 `NO_REACCEPT`、其他设计限制、PF/P4 和生产资格不变；正式 reconnect 仍使 `FULL_REQUIREMENTS_COMPLETE` 不成立。

构建/辅助证据：fresh target package `bound=109/source=101/compiled=55`；local SDK-bundled Ninja 1.13.2 与 SDK 1.12 的 MAX_PATH 问题已解决，SDK 未修改；local final Python `6/6/6/26/14` 与 executor `16`，Host 23 ASan `5`，prior naming `27`/ASan `37`/ROS Host 均通过。help-only token-display-redaction helper 已 review，targeted tests 与新的 API23 helper 编译通过但 `UNUSED_NOT_DEPLOYED`；原 executed helper C++ 保留匹配 frozen SHA。无 QualificationProfile、新设计、性能 100 轮或 SDK/RMW/platform policy/firmware 修改；本包确有 implementation/configuration/executor/test changes。

## 历史 gate：V1_LOCATOR_DEVICE_ACCEPTANCE（2026-10-04；只读 preflight）

本工作包状态为：**`BLOCKED / STATIC_SOURCE_ALIGNED_POLICY_DENIAL / COMMIT_GATE_FAIL`**。任务允许在 unchanged-policy gate 通过后执行必要的 query、部署、恢复和条件性业务动作；实际执行在只读 preflight 阶段停止，因为 mandatory policy gate 失败，后续动作因此 `NOT_EXECUTED`。named initial tester 完成 29 条 HDC preflight 命令和 2 次固定 policy receive；两板 policy hash 一致（`f98b0770c1da57a0a8939d38fdd16386dfcfabb801d9413e18ed781cc217d12e`），目录查询、权限 mutation、部署和业务窗口均为 0。冻结 PRIVATE_CONFIG tuple 已与 A/B 实际 `ros.conf` package/prefix 匹配；具体身份值不写入文档。

独立 reviewer 检查了全部 policy regex 及 `Socket → CreateSocket → ServerIpcCreateSessionServer → CreateSessionServerInner → CheckTransPermission → CheckPermissionEntry` 路径，确认 POSIX regex no-match native enforcing denial：无已安装规则可匹配 canonical `md1` 加小写 hex/dot service-name family。这是 **STATIC_SOURCE_ALIGNED_POLICY_DENIAL**，不是观察到的 Socket rc；没有执行 `GetNodeKeyInfo`、Socket application 或业务窗口。reviewer 高置信度判定 `BLOCKED` 并停止，因为解除需要平台 policy 修改或 service-contract revision，均超出本次授权且被禁止。该历史 preflight tester raw audit 为 `build/v1-device-20261004/final-gate/FINAL_GATE.json`，Commit Gate=`FAIL`。

因此该历史 preflight 不标记 `V1_LOCATOR_DEVICE_VERIFIED` 或 `ROS2_V1_DUAL_BOARD_VERIFIED`。G2/A1 的实现、Host、installed-consumer 和 API23 target compile/link PASS 证据保持不变，但在该 preflight 时实际 V1 device runtime、SPDP/selection、ROS `20/20`、graph/service/delete/runtime close/native current window 均为 `NOT_RUN / BLOCKED`。后续 final raw acceptance 已在上方单独记录。A5/C1 `NO_REACCEPT`、其他设计限制、PF/P4 和生产资格保持原状；若 formal reconnect 是要求，`FULL_REQUIREMENTS_COMPLETE` 不成立。无 codec、RMW、SDK、policy、implementation source 或 executor 修改；项目文档增量保持未提交。详见 [V1 locator device acceptance](V1_LOCATOR_DEVICE_ACCEPTANCE_20261004.md)。

## 历史 checkpoint：AUDIT_FIX_BASELINE（2026-10-04；本地 checkpoint，未 push）

状态（本提交的标签；文档随该提交入库，提交号以 `git log` 为准，不在文中自引用）：**`AUDIT_FIX_BASELINE_COMMITTED / NOT_PUSHED / NOT_PRODUCTION_QUALIFIED`**。它只是对全源审计和独立复核所发现缺陷的一次限定修复，**不是**全部目标完成，也**不是** `REACCEPT_SUPPORTED`、`V1_CODEC_VERIFIED` 或当前修订的设备验证。此前成功的 ROS 设备窗口仍绑定其原始源码和 package；本修订的运行时改动（包括下面的 ROS2 bridge 改动）没有在该设备窗口上执行，不能当作新修订的执行证据。

已修复（每项均有 Host 回归，见括号内 CTest 名；`N4-CLOSED-ACCEPTED-NO-REACCEPT` 是 `dsoftbus.native_owner_model` 内的一个 group）：

| 项 | 修复 | 回归 |
| --- | --- | --- |
| B1 | `ServiceRecordBinding::Prepare` 不再解引用空 `socket_call`；同一 plan 上已有 Ready/Listening owner 时第二个 binding 以 `OwnerUnavailable` 拒绝，不共享 close owner | `service_record_binding.second-binding-same-plan` |
| B2 | joiner 不再加入 `Unanchored` record（与 `Publish` 的 `Revoked` 一致，在取得 namespace/native owner 之前拒绝） | `service_record_binding.joiner-refuses-unanchored` |
| B3 | 非 deferred accepted 路径上，`ObserveInbound` **新建**的 Rejected close-pending owner 现在由该 binding 接管，经唯一 close 路径 Shutdown 一次；若 id 已被存活 owner 持有而 peer 不一致，table 返回 `ForeignIdCollision` 且不返回 owner，因此不会被 Shutdown。被拒 owner cell 按既有有界设计继续保留（见下方限制） | `service_record_binding.rejected-accepted-closed` |
| C2 | 双拓扑 `ReopenInput` 回滚关闭 receiver 所在的 listener binding，而不是 outgoing binding | `public_socket_accepted.reopen-rollback-dual`（用真实 ProviderAdapter/binding 在 listener 的 `OpenReceiver` 内注入 provider stop；旧的错 binding 实现会使该断言失败，已变异验证） |
| C3 | `OpenRuntime` 发布 adapter 前在 gate 内复核 `stopping/closed`，输掉与 `Shutdown` 竞争的 Open 被拒绝且其 adapter 被关闭一次 | `host_provider_loop.open-racing-shutdown`（旧代码返回 `Opened`，已变异验证） |
| C5 | `Observe`/`CloseAccepted` 在 gate 内取指针快照后再使用；`Observe` 对共享运行时先判断不可变的 `shared`，再读可能被 `StartShared` 移走的 registration 句柄（独立复核发现并已改） | `host_provider_loop.observe-lifetime`：**只是功能性的寿命/所有权不变量检查**——它在修复前和修复后的读取方式下都通过（已用把读取改回修复前形式的变异验证），所以快照写法本身**没有**被它单独回归保护；也**不是**数据竞争证明，TSan 未运行 |
| A2 | `init(…, max_msg_size_no_frag)` 按固定的 Fast DDS 3.6.2.0 与 2.14.5 `UDP/TCPTransportInterface::init` 规则：0 表示默认 `s_maximumMessageSize`（65500），非 0 替换该上限，传输的 `maxMessageSize` 不得超过有效上限。旧测试期望是错误的合同假设，不是需保留的产品行为。**后果**：ROS2 两个 bridge 的 descriptor 为 65536（>65500）且关闭 builtin transport，默认 `no_frag=0` 在新规则下会被拒绝；因此 `integrations/ros2/Bridge.cpp` 与 `integrations/ros2/target/TargetBridge.cpp` 现在把 `qos.transport().max_msg_size_no_frag` 设为 descriptor 的 `maxMessageSize`（2.14.5 中该字段在 builtin 关闭时只影响 `init()` 的上限校验）。Host `Bridge.cpp` 在 2.14.5 头文件下做了 `cl /Zs` 语法检查；`TargetBridge.cpp` 含 `<link.h>`/`<unistd.h>`，需要 OHOS 交叉工具链，本机**未编译**；两者均未运行 | `transport_facade`（边界值 63/64/65/0、65500/65501，以及 bridge 使用的 65536：`init(nullptr,0)` 拒绝、`init(nullptr,65536)` 接受） |
| A3 | `OpenOutputChannel` 对其他 transport kind 的 locator 不再覆盖 `last_error` | `transport_facade` |
| G4 | 新增默认 `NativeBackend::MaxMessageSize()`（0 = 后端**未声明**上限，不是“无限”也不是容量声明）；`StartRuntime` 仅在非 0 且小于 descriptor 的 `maxMessageSize` 时拒绝启动。因此一个转发包装器若忘记覆盖该虚函数，检查会静默跳过（“未声明”的设计后果，不是容量声明） | `host_provider_loop.backend-size-bound / -above / -unspecified`（低于/等于/高于/未声明；`<` 改 `<=`、去掉 `!=0` 保护两个变异均使其失败） |

已知限制（独立复核提出，已核实，未扩大本 checkpoint 范围）：

- C3 的 lost-race 分支调用 `settle_pending()`，其清理结果写入 `failed_open_cleanup`；`ShutdownRuntime` 现在在 gate 内取该值的快照（消除无锁读）。但在快照之后才出现的清理失败**不会**并入 `Shutdown()` 已锁定的 `close` 结果，只能通过 `Observe().failed_open_cleanup` 看到；`Shutdown()` 也可能在输家的 `Initialize` 已创建 provider socket、`settle_pending()` 尚未关闭它时先返回 true。没有为此加入“等待进行中的 Open”机制。
- `Observe()` 的行为变化：`Start` 失败后，native/receive/tx 现在报告为空（此前可能显示部分构建状态）。
- B3 只覆盖 table 返回新建 Rejected owner cell 的情形。table 已满（`AdmissionSaturated`/`IdentityExhausted`，会 seal）时 `ObserveInbound` 不返回 owner，组合层没有可认领的 close 责任，因此不会 Shutdown 该 socket，停止 ingress 由调用方负责（`stop_ingress_required`）。该情形未被组合层测试覆盖。另外，`ObserveInbound` 对“同一 id、同一 peer、存活的 Accepted owner”按设计幂等地返回该 owner 的 lease（`native_owner_table_tests` 已固定），非 deferred 路径的 `Prepare` 会采纳它，释放该 binding 时会 Shutdown 该 id——这是既有语义，不在本修复范围。生产路径的 accepted binding 一律使用 `defer_accepted_ownership`（`ProviderAdapter.cpp:206`），只有测试会走非 deferred 路径。
- C5 之外仍有受“控制调用外部串行化”合同约束的无锁读（例如 `OpenRuntime` 重开分支对 `s.adapter` 的读取）；它们既有，未改动。
- 取得 `Shutdown` 的 `stopping` 之后不会再有对 `s.adapter` 的写（`Open` 只在 `!stopping` 时在 gate 内发布），因此 `ShutdownRuntime` 中无锁读 `s.adapter` 是安全的；这依赖 C3 的修复。

**A5/C1（未修复，保持 fail-closed）。** 一个 `NativeOwnerTable` 对一条 route 在整个 table 生命周期内只允许一个 Accepted owner：`FindPlan(Accepted, route)` 对已 `close_intent` 的退休 owner 仍返回该 owner（`src/NativeOwnerTable.cpp:191-199`），因此 `PrepareInbound`（`:677`）与 `ObserveInbound`（`:737`）此后都以 `DuplicateConnection` 拒绝同一 route 的新 accepted socket；创建过外部 socket 的 owner cell 不回收（`:580`）。结果是：对端断开后重新拨入、或 dual 拓扑 `CloseInput` 后 `ReopenInput` 再 accept，在同一 table 内**不受支持**。`N4-CLOSED-ACCEPTED-NO-REACCEPT` 只是对这一行为的特征测试，不是支持声明。本 checkpoint 没有回收墓碑、复用 owner cell 或引入新 epoch；任何显式重连需求保持**未满足**，不是被豁免。重新接受需要单独设计，并需要 SDK 对 socket id 复用的保证。

**G2/A1（该历史 checkpoint 时未满足）。** 生产路径（facade、send、input、ROS2 bridge）仍使用旧 `LocatorCodec`（`include/fastdds_dsoftbus/dsoftbus/EndpointDirectory.hpp:24-25`：kind `0x40000000`，`port == 1`，原始 16 字节 id），不是冻结的 V1 地址合同；`v1::AddressCodec`（`include/fastdds_dsoftbus/dsoftbus/V1Address.hpp`、`src/V1Address.cpp`）只在单元测试和实验里使用，且二者声称同一个 kind。唯一的现行要求在 [FASTDDS_DSOFTBUS_TRANSPORT_V1_PLAN.md](FASTDDS_DSOFTBUS_TRANSPORT_V1_PLAN.md) 第 28 行（locator 使用规范服务名、稳定的 namespace/domain/UDID 摘要和 profile/slot 编码）和第 30 行（“地址编码固定为”：私有非负 kind；`address[16]` 为规范 `{namespace, domain, UDID}` 的 SHA-256 截断键，碰撞必须拒绝歧义路由；`port` 高 8 位编码 profile，低 24 位编码 `slot + 1`，零值和非法组合拒绝；同一 Participant 的 metatraffic/user traffic 公告同一 DSoftBus locator；profile 不兼容的 locator 在选择阶段排除）；第 41 行要求自定义 locator 选择按完整地址比较。实现时以这些行为准，不使用历史寻址提案，也不自行发明字节布局。该历史观察已由上面的 G2A1 当前 checkpoint 更新；原文作为当时 evidence 保留。

其余设计问题保持各自已记录的处置，未宣布关闭：G3（发送 deadline 与准入预算已有；同步 SDK 进入后的返回界和 Close/drain 仍未证明有界；未确立需要改 descriptor ABI，超时后不得直接释放资源）、G1（每目标一份拷贝、按 N 份计费与 N 份实际内存一致；共享块是待实现优化）、A6（多目标全有或全无接纳是已有测试固定的明确合同）、B4（重复 Socket 请求可返回已有 ID，盲目 Shutdown 可能误关旧 owner；条件性缺口，需要平台身份保证）、A4（路由在 init 时冻结，`DeviceDirectory` 未接入）。来源：[独立审核报告](../.artifacts/review-full-20261004/FINAL_REVIEW.md)与本次全源审计。未跟踪的 `src/service_record_binding_tests.cpp` 是未参与构建的旧副本，`communication_dsoftbus` gitlink 为本地脏状态，二者均已保留、未纳入本次提交。

ROS2 Host 交付（审核 P2）：新增唯一入口 `integrations/ros2/prepare_fastdds_source.py`，按固定顺序执行完整五步补丁链并校验六个头文件；`UPSTREAM.json` 的头文件 hash 以 CRLF 折叠为 LF 后计算。该入口对每个文件做精确的行尾映射（与 `target/rebuild.py` 的 `exact_eol` 同一规则：只接受内容相同或仅换行符不同，其余一律拒绝），并为每次转换打印 `EOL_MAPPED`；对 `git archive`（`core.autocrlf=false`）出来的 11 个原始文件中的 7 个需要这种映射。`check_patch_delivery.py` 现在除重放链外还直接调用 `prepare()`：用真实 `git archive` 的字节作输入，断言映射清单、与直接应用链所得的树逐字节一致、重复准备被拒绝、首个前提文件漂移被拒绝且树未被改动；并独立固定五个步骤名与六个头文件，使被丢弃的步骤会失败（已变异验证）。成功输出 `PATCH_DELIVERY_PASS`。从全新 `git archive` 运行单一入口得 `FASTDDS_SOURCE_PREPARED steps=5 headers=6`，六个头文件的准备树/元数据/仓库副本 hash 三方一致，补丁后的 `DomainParticipantFactory.cpp` 的 `cl /Zs` 语法编译通过；移除 `ResidualDiagnostics.hpp` 或 `NativePrefix.hpp` 后分别以 C1083 失败。语法编译只证明补丁后的 factory 文件可解析，不是设备或完整目标执行。已知缺口：该检查不比较链末状态 hash（元数据没有记录终态 hash），只比较与直接应用链的一致性。

测试记录（`原生 VS2022 x64 Release/CONFIG`，非 MSYS）：

- 原始高并行完整运行（`ctest -j 4`）：544/546，失败 `dsoftbus.host_provider_loop.backend-size-bound`（本次新增测试的夹具错误，已修正）与 `experiment.matrix_control_receipts`。该日志保留。
- 之后 `ctest -j 2`：546/546（541 个既有 + 5 个当时新增）。
- 之后又追加 4 个 CTest，注册总数 550（541 + 9）。一次针对 550 的完整运行因独立复核发现的问题需要改源码而被中止并保留为 `ABORTED`（不计入任何总数）。
- 最终源码上的完整运行：**550/550 通过**（`ctest -j 2`，32 个逻辑核，原生 VS2022 x64 Release/CONFIG，非 MSYS；JUnit failures=0 skipped=0 disabled=0，用时 631 秒）。这是全部 550 个注册 CTest 在最终源码上的结果，JUnit 中没有任何 skipped 用例。
- ASan（`/fsanitize=address /Zi`，ownership/rollback/close/host loop/accepted 等受影响套件，`ctest -j 2`，160 个 CTest；最终源码上重跑为 160/160 通过，JUnit failures=0 skipped=0）：通过。预构建的 Fast DDS、Fast CDR 和 foonathan 依赖**未插桩**；**TSan 未运行**——MSVC 没有 TSan，WSL 有 gcc/libtsan 但没有 Linux 版 Fast DDS，无法构建 `HostProviderLoop`。一次 ASan 运行因我的 harness 在 `cmd /c` 中让 `%PATH%` 在 `vcvars64.bat` 之前展开而全部超时，该日志另存为 `INVALID-harness-path` 且不计数。
- 安装包消费者（`tests/install_consumer_test.py`，对本修订新构建的 package）：`INSTALL_RELOCATION_EXTERNAL_HEADERS_DEFAULT_AND_HOST_DDS_PASS`。2.14.5 变体：20 个运行时 TU 的 `cl /Zs` 语法检查无错误；Host `Bridge.cpp` 同样；`TargetBridge.cpp` 未编译（见 A2）。这些只是语法/链接级检查。
- `experiment.matrix_control_receipts`：**未解决、未复现**。它是 `RUN_SERIAL`；其 UDP 端口（31000–31140，`reference_control_tests.cjs` 以 `31000+n*20` 固定分配）没有其他测试使用，也不在 OS 排除范围内，输出目录由 `mkdtemp` 唯一。失败的是第一个子进程（`wrong_run`，需要在测试内置 5 秒 kill 之前自行退出）；保留的目录时间戳显示它从启动到退出被记录经过了约 14 秒，而 kill 定时器是 5 秒——kill 没有及时生效或事件循环被饿死，原因未确定；其 stdout 是块缓冲，被 kill 后日志为空，无法判断卡在何处。单独运行时，`wrong_run` 的子进程约 0.17 秒自行退出（从未执行过的新拷贝为 0.17–0.29 秒，因此首次执行的杀毒扫描不是已证实的原因），整个测试单独运行约 1.8 秒通过。因此不能声称降低并行度修复了它；测试、超时与断言均未改动，高并行下的结果仍作为未解决项记录。

## 历史 checkpoint：BOUNDED_DDS_STABILIZATION（2026-09-27；artifact run `20260927T103431Z`）

该历史 checkpoint 的独立 tester 在冻结候选树上完成 Release/CONFIG Host build 与完整 CTest：`445/445 PASS`、exit 0，commit gate `PASS`。配置显式开启 bounded regression 与 decision diagnostics；生产默认仍为 `NativeBackendUnavailable`，诊断默认 OFF。没有 HDC/SDK/device execution，也没有新的 ASan；B 的 `72/72` ASan 结果仍是历史、预构建 Fast DDS 未完整插桩的 source-equivalent evidence。该状态仅记录历史 checkpoint evidence，不声明当前 package 已提交、SDK qualification、release 或 production readiness。

## 当前工作包：PUBLIC_SOCKET_BINDING_DECISION_AND_CLOSE_STABILIZATION（2026-09-27；artifact run `20260927T083510Z`）

本工作包达到 **`CLOSED_WITH_HISTORICAL_LIMITATION / CHECKPOINT_READY / NOT_COMMITTED`**。
最终独立 review 的 scoped functional gate 与 code gate 为 `PASS`，无未解决的 scoped actionable blocker；这不是 HumanRelease、commit、SDK qualification、production readiness 或 full Transport qualification approval。当前源冻结 SHA-256 为 `698a06787e5c33380e4bc5b93c0a076a6c9eca84f8853046248de5a2137e4240`，独立目标包主程序 SHA-256 为 `db1b6ef915aa820bbcf4392fe0840751da30dbffa6b9cb187f0379fd982aa65d`。

三次 guarded query 与三次双板数据窗口均 PASS：F1M（每个 1 MiB sample 131 fragments，delete B then A）、D1（每方向 5 samples，delete B then A）和 D2（每方向 5 samples，delete A then B）。F1M A 侧实际观察到 107 个 `CAPTURE/LOCAL_RECORDS` binding refusals，B 侧有 1 个 `ADAPTER_PHASE` rejection；D1 无 refusal/rejection，D2 B 有 1 个 `ADAPTER_PHASE` rejection。所有六个 board decision scopes 均为 `COMPLETE_DECISION_ACCOUNTING`、overflow 0；23 个 Reader rows、5 个 DDS wait API operations 和 0 次 data retry 是分离计数。没有 107+2 physical-drop、harmless 或 delivery inference。另行保留的历史限制是：A 工作包旧 F1M 的 111 次是 Binding 接纳拒绝的聚合计数，逐次原因未记录；旧六条 adapter `InvalidState` 的直接原因也未观察。两组历史计数不能相加为 117 条唯一丢失，亦不由本轮 107/2 的原因回填。

新增的是 SDK-free value-only `ReceiveDecision`。在真实决策点捕获值；受锁保护的状态在对应 gate 内取得快照。现有 observer 在解锁后接收该快照，不重新查询当前 map。diagnostic 默认 OFF，每进程容量 16384 rows（reported storage 5243440 bytes），带 reason aggregates 与 overflow counters。该 instrumentation 有实际开销，并非 zero-overhead，且不构成 SDK drain 证明。CloseBusy algorithm 未改变；继承的 original owner/namespace 与 single close credit retention 已由 Host deterministic tests 验证，设备未声称 hit CloseBusy。QualificationProfile 未生成；local raw artifacts 与 package-specific profile helper 不会因 Git clone 自动进入仓库。

验证记录：canonical Host `443/443 PASS`，affected Host `72/72 PASS`，affected ASan retry `72/72 PASS`，analyzer `20/20 PASS`，bounded profile `27/27 PASS`，目标包 build 与 static audit `PASS`。最终发现测试数为 445，但未执行完整 445 suite；ASan 所用预构建 Fast DDS 未插桩，TSan 未执行。初始 ASan loader failure、错误 route preparation、错误旧 analyzer capacity profile、projection/audit failures、无必要的 full rerun 与 false yield cutoff alarm 均保留为历史过程证据。设备普通 deletion/close 通过，但没有证明设备实际命中 CloseBusy 分支；SDK settlement/cancellation/quiescence、full Transport qualification、性能、完整 firmware ABI 与 QualificationProfile 仍未验证。P0/PF-01/PF-02 为 `BLOCKED / NOT QUALIFIED`，P4 未放行，生产默认仍为 `NativeBackendUnavailable`。详见 [decision and close stabilization record](NEW_PUBLIC_SOCKET_BINDING_DECISION_AND_CLOSE_STABILIZATION.md)。

## 当前工作包：NEW_PUBLIC_SOCKET_BOUNDED_DDS_REGRESSION（2026-09-27；artifact run 20260927T014310Z）

本工作包达到 **`NEW_PUBLIC_SOCKET_BOUNDED_DDS_REGRESSION_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**。七个独立 fresh-process、双 RK3588A A→B DDS 数据窗口和七个 fresh guarded query 均通过；合计 14 个完成窗口、28 个 board-window guards、26 个正式 Reader rows，数据重试为 0。最终 Host `438/438 PASS`，affected ASan `169/169 PASS`；ASan 依赖中的预构建 Fast DDS 并非全部 instrumented，TSan 未执行。初始 ASan 环境 timeout 已保留；清洁 PATH/module 配置后受影响套件通过。

| Case | 实际写入 / Reader takes | 结果与边界 |
| --- | ---: | --- |
| F256 | 3 / 3 | 3×256 KiB；每样本 33 fragments |
| F1M | 3 / 3 | 3×1 MiB；每样本 131 fragments |
| REL_LOSS | 3 / 3 | 一次精确 drop；NACK_FRAG admitted repair |
| BE_LOSS | 3 / 2 | 完整 post-drop 观察中 sample 1 absent；2/3 complete |
| VOL_LATE | 6 / 3 | 仅 late Reader 之后的新 4/5/6 |
| TL_LATE | 6 / 6 | 保留 1/2/3 后接收新 4/5/6 |
| KEYED_LAST | 10 / 6 | 每个 key 的历史 3/4 后接收新 5 |

实际链路为 public DDS factory → 当前 DSoftBusTransport isolated façade → 原有
`NativeSendExecution` → `PublicSocketBackend` public Socket API → remote accepted
callback → `ProviderAdapter`/`ServiceRecordBinding` → 原有 owned-copy、receiver
lease 和实际 `DataReader`。Fast DDS 使用现有 fragmentation；fragment size 为
`8016`，native record bound 为 `8192`，均未改变。REL repair 的记录是同一运行内
分开的本地逻辑观察（drop → NACK_FRAG → repair → later ingress/owned/running），
不是跨板时钟或唯一物理 packet identity。

F1M A 侧 `Binding refused_receive` 在 MATCH→DATA_DONE 增加 `111`；这是 Binding
aggregate，不是 111 个 SDK error，个体原因和 record identity 为 `UNOBSERVED`。
另有六个 B 侧 setup `OnBytesRejected`，值为本地 `BackendStatus::InvalidState`，
phase 为 `UNOBSERVED`。这些事实不能被改写为 SDK 错误、假定容量拒绝、无害事件或
all-ingress loss-free；26 条正式 Reader chains 仍按各自 case 成功完成。DDS wait
API 是 DDS 操作而非网络 ACK packet 计数；DSoftBus `Shutdown` 为 void，不能记录为
SDK rc0。没有 SDK drain/cancel/quiescence 证据。

本轮实际修复了 shared close-credit race：Binding 保留原 owner/namespace，在 gate
外等待未改变的单一 close credit（credit 未增加）；provider callback reentry 拒绝
阻塞控制，精确 destructor self-wait 诊断同一 table credit，其他 table 的合法销毁
仍可进行。该修复不构成 SDK cancellation 或 physical drain 保证。

状态边界保持不变：P0/PF-01/PF-02 为 `BLOCKED / NOT QUALIFIED`，生产默认仍为
`NativeBackendUnavailable`，P4 未放行，无 QualificationProfile；未覆盖任意
QoS/topology、same-process hot rebuild、RAW/mixed/ROS2、性能/耐久、full
`TransportInterface`、完整 firmware ABI、SDK settlement/cancellation/quiescence，
也未建立生产资格。Final independent review 已关闭七个 tested profiles 的阻塞发现，
但它不是 pre-commit、生产或更广 Transport qualification gate。

详见 [New Public Socket bounded DDS regression](NEW_PUBLIC_SOCKET_BOUNDED_DDS_REGRESSION.md)
及 artifact `.artifacts/new-public-socket-bounded-dds/run-20260927T014310Z/`；本节
不改写后续历史工作包记录。

## 当前 PRE-COMMIT SNAPSHOT：LOCAL_NATIVE_DDS_BASELINE_COMMIT（2026-09-27；artifact run 20260927T005155Z）

本 pre-commit snapshot 达到 **`LOCAL_NATIVE_DDS_BASELINE_HOST_VERIFIED / CHECKPOINT_READY`**，范围是用户明确授权的当前分支一次本地 commit。没有写入未来 commit hash；merge、push、device、SDK、release 和 QualificationProfile 仍未授权。候选冻结 345 个文件前后 SHA-256 一致；最终 CONFIG Release 全量 CTest 为 `420/420 PASS`、exit 0、144.58 秒。首轮 `418/420` 的两个 `Get-FileHash` PowerShell module-path 环境失败保留，恢复模块路径后完整重跑通过；`LastTest.log` 后被 discovery 覆盖，未宣称重建 raw transcript。final affected helper gate 为 `3/3 PASS`。

候选 review 无 unresolved blocking finding。R3 ASan/device evidence 按匹配 source bytes 复用，不是本 checkpoint 新执行；published code/default 仍 `NativeBackendUnavailable`，P0/PF-01/PF-02 为 `BLOCKED / NOT QUALIFIED`，P4 未放行，SPDP/SDK drain/full `TransportInterface` 仍受限。7 个 helper 与 3 个 analyzer tests 的 provenance 已收据化；实际授权 header checkout `5518313...` 与未变更 repository gitlink `dd6037...` 明确区分。旧工作包 `NOT_COMMITTED` 段落保留为历史，不回写全局状态。

详见 [Public Socket baseline build](PUBLIC_SOCKET_BASELINE_BUILD.md)、[Public Socket evidence index](PUBLIC_SOCKET_EVIDENCE_INDEX.json) 与本包 artifact receipt。Main 仍需完成实际 index/mode/blob/tree、graph/allowlist 和 staged-tree review 技术门。

## 当前工作包：REAL_DDS_OVER_NEW_PUBLIC_SOCKET_DEVICE_BASELINE（2026-09-27；artifact run 20260926T225417Z）

本工作包达到 **`REAL_DDS_OVER_NEW_PUBLIC_SOCKET_DEVICE_BASELINE_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**。D1 `e6e553762d29ceb2` 为 delete B→A，D2 `c4af736f2b0d35c6` 为 delete A→B；两窗口均在两块 RK3588A 上通过实际 `DSoftBusTransport façade → NativeSendExecution → ProviderAdapter/PublicSocketBackend → public Socket API → remote accepted/ProviderAdapter/Binding → ReceiverResource/DataReader`，每方向 5 条，合计 20 条正式样本 GUID/SN 链。无重试；DDS wait API 不是 ACK packet 计数。

Host full r1 `414/414 PASS`；final affected Host/ASan r3 各 `161/161 PASS`；final probe Host/ASan 各 `9/9 PASS`，这些集合有重叠且不相加。四窗口、八个 board-window guards 通过。B control trace 的 12 条 SPDP ingress 与 11 条 owned/running 之间的精确原因保持 `UNOBSERVED`，不能据此宣称所有 native packet delivery。设备前捕获的是一次实际 Fake-call QoS 参数 RED（误把 numeric 3 的 RTT_LEVEL 用作 MAX_WAIT_TIMEOUT，修复为 1），另有三个独立 analyzer counterexamples；R2 从未部署。

final r3 package `2c42f3fc...`、source freeze `fb4400...`、target-r3 1,514 inputs/6 members 均按 receipt 绑定。该结论仅是 small DDS baseline：hot rebuild、full QoS、full `TransportInterface`、firmware/SDK ABI、SDK drain/quiescence、arbitrary topology 与生产 enablement 仍未验证。默认 `NativeBackendUnavailable`、P0/PF-01/PF-02 `BLOCKED / NOT QUALIFIED`、P4 未放行、无 QualificationProfile；不修改旧 runtime/Host package 的设备归属。

详见 [Real DDS Public Socket device baseline](REAL_DDS_PUBLIC_SOCKET_DEVICE_BASELINE.md) 与本包 artifact receipt。

## 当前工作包：PUBLIC_SOCKET_TARGET_LOAD_AND_BOUNDED_RUNTIME_PROBE（2026-09-27；artifact run 20260926T200859Z）

本工作包记录 **`TARGET_LOAD_OBSERVED / BOUNDED_PUBLIC_SOCKET_CALLBACK_PATH_OBSERVED / NEW_SHIM_BIDIRECTIONAL_SMALL_BYTES_OBSERVED / CHECKPOINT_READY / NOT_COMMITTED`**。L `93a421640af19a22` 无 grant 且完成实际 load-origin/symbol/map 检查；guarded query `7ec64dad0b67367d` 完成独立 route identity 与 grant/revoke guard；唯一实际数据窗口 S `d82c4df0d3dccd73` 双板每方向发送/接收 5 条完整 256-byte 合成记录，正常退出并恢复 guards。第二次数据尝试未执行且不需要。

S 窗口实际走新 shim 的 ticket → adapter/Binding/receiver 路径：listener=1、outgoing=2、accepted=3；A 对 accepted/outgoing/listener 各显式调用 void Shutdown，B 对 outgoing/accepted 观察到 typed OnShutdown reason 8 后仅显式关闭 listener。A3 void close 与 B2 typed reason/ listener close 是实际角色差异，不是 SDK drain 证明。普通 `Negotiate2`/`OnError` 未观察到。

独立 Host R3 `407/407 PASS`；最终受影响 Host `151/151`、ASan `151/151`；运行时 Host `10/10`、ASan `10/10`，这些集合有重叠，不相加。目标 fresh probe `989bb...`，target-r4 绑定 1,357 inputs/6 package members；headers 来自授权 source checkout 而非 SDK bundled headers。S 是合成小 BYTES probe，非 DDS/RTPS/Participant，不能升级为新 SDK DDS、firmware ABI、drain/quiescence、性能或 PF 资格。

默认 `NativeBackendUnavailable`、P0/PF-01/PF-02 `BLOCKED / NOT QUALIFIED`、P4 未放行、无 QualificationProfile。此包的双板观察不回写为旧 accepted-only 包的设备证据；无 commit/push/device expansion。

详见 [Public Socket runtime probe](PUBLIC_SOCKET_RUNTIME_PROBE.md) 与当前 artifact receipt。

## 当前工作包：PUBLIC_SOCKET_ACCEPTED_AND_REAL_DDS_HOST_INTEGRATION（2026-09-27；artifact run 20260926T174047Z）

本工作包达到 **`PUBLIC_SOCKET_ACCEPTED_HOST_CONTRACT_VERIFIED / REAL_DDS_OVER_PUBLIC_SOCKET_FAKE_VERIFIED / SOURCE_HEADER_TARGET_LINK_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**。范围是双 Host endpoint、每侧 one listener/outgoing/accepted、Fake public Socket C API、现有 owner/adapter/binding/receiver 路径与真实公开 Fast DDS DDS fixture；旧 9 个 DDS 用例仅作 Mock regression，新 5 个用例覆盖 PublicSocketBackend。

独立 Release focused `80/80 PASS`、full `396/396 PASS`、exit 0；ASan affected `139/139 PASS`，外部预编译 DDS DLL 未插桩，396 与 139 不相加。E4 在现有 live pair 上拒绝额外 accepted negotiation，清理未交接 candidate 后继续 10 次 DDS take；不是 Participant 创建失败，不能写成 null result。F1 保留首个运行错误与 cleanup status，F2 保留 role terminal state 与严格 TX 归属，F3 使用无分配 `TryTake`/destruction 排空 accepted RX。M1–M9 有效变异均 killed，早期无效 operator/build failure 历史保留。

target-r2 为 727 inputs/7 outputs，API23 AArch64 编译/静态链接使用授权公共 headers 与历史 DSO；未加载、未执行、未部署。`Negotiate2` 普通 BYTES 路径跳过，unsupported access-info/special routes fail-closed；边界为 bounded topology、accepted-only business RX、无 reconnect/RAW/general topology。生产 `NativeBackendUnavailable`、P0/PF-01/PF-02 `BLOCKED / NOT QUALIFIED`、P4 未放行、无 QualificationProfile；SDK/device/native runtime/drain/quiescence/firmware ABI 仍未验证。详见 [Public Socket accepted Host](PUBLIC_SOCKET_ACCEPTED_HOST.md) 与本包 artifact receipt。

## 当前工作包：DSOFTBUS_PUBLIC_SOCKET_SHIM_COMPILE_AND_CONTRACT（2026-09-27；artifact run 20260926T160850Z）

本工作包达到 **`DSOFTBUS_PUBLIC_SOCKET_SHIM_IMPLEMENTED / TARGET_SDK_BUILD_VERIFIED / HOST_SHIM_CONTRACT_TESTED / CHECKPOINT_READY / NOT_COMMITTED`**。新增 shim 只负责把现有 `NativeBackend` 接到公共 `Socket`、`Listen`、`Bind`、`SendBytes`、`Shutdown` 声明；默认生产工厂仍不可用，真实 `DSoftBusProvider` kind 仍被拒绝。25 个 shim CTest 经过实际 shim、ProviderAdapter、ServiceRecordBinding、owned-copy、receiver lease 和测试 receiver；另有 17 个 ProviderAdapter CTest，合计 Release `42/42`，受影响核心另为 `73/73`。ASan 同样是 shim/provider `42/42`，并包含既有 Host Participant Mock 回归 `9/9` 与 input-close `14/14`。该 9/9 fixture 没有替换为新 PublicSocketBackend/FakeSDK，因此不构成新 shim 到真实 DDS 的端到端证明；这些结果也不是设备或 SDK runtime 结果。

API23 AArch64 目标使用 `native6.1.0.32` clang `15.0.4`，配置/编译和五个公共符号静态审计通过，目标未执行。声明头文件来自授权的 `communication_dsoftbus` 公共源码 checkout（HEAD `5518313ee70e79f788fc4d9f9a9310c8b24827cc`），而不是安装 SDK bundled headers；source-to-firmware ABI、loader、SDK 内部 drain/quiescence 和最后使用仍 `NOT_ESTABLISHED/UNPROVEN`。freeze-r1 的完整 `372/372` 运行和之后发现的 `/EHs-c-`/C4530 异常展开问题均保留；改为 `/EHsc-` 后，r3 先重跑受影响 Release `42/42 + 73/73` 与 ASan `42/42 + 9/9 + 14/14`，随后又在冻结源码上独立完成最终 full-r3 `372/372 PASS`（build/CTest exit 0，124.517 秒，13/13 source-freeze 无漂移，无 C4530 或 `/EHs-` 覆盖）。这不是把 r1 结果改名。回调使用由原 slot/role 导出的 shim 专用 trampoline。Listen accepted/reconnect ownership 仍 `DEFERRED`。`NativeBackendUnavailable`、P0/PF-01/PF-02、生产 P4 和 QualificationProfile 状态不变。完整范围见 [Public Socket shim](PUBLIC_SOCKET_SHIM.md) 和本包 artifact receipt。

## 当前工作包：REAL_FASTDDS_PARTICIPANT_OVER_HOST_NATIVE_BACKEND（2026-09-26；artifact run 20260926T093712Z）

本工作包达到 **`REAL_FASTDDS_PARTICIPANT_HOST_INTEGRATION_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**。限定范围是 Fast DDS 公开 factory → test-only descriptor → 当前 Host facade → `SenderResource`/`NativeSendExecution`/`ProviderAdapter`/`Binding` → 真实 `ReceiverResource` → `DataReader` 的 one-local、one-dial、双 Participant 单进程 Host 链路；未链接旧 `ExperimentalTransport`。配置固定为 RELIABLE/VOLATILE、`MddsKeyedSample` 32-byte payload、`maxMessageSize=8192`、builtin transports/intraprocess/DataSharing 关闭。

独立 final-r2 focused 为 `9/9 PASS`，全量 Release build/configure/CTest 为 `346/346 PASS`、exit 0（subprocess 179.434 秒）。ASan-r2 新 Participant `9/9`、既有 input-close `14/14`、provider-loop `15/15` 均 PASS；预编译 Fast DDS/fastcdr/foonathan DLL 未 instrument。D1+D2+D3 normal-b-first 产生 15 次实际 `DataReader::take`，D4 normal-a-first 产生 10 次；重建 Reader 使用新 GUID 并取得 A 的 6–10，原 Participant/Writer/input resource 保持。D5 的真实 API 结果为 **non-null Participant** 且 input setup Bind rejected；public delete 清理失败 B，A 保持不变，不能改写为 null。

两项 review P2 均经过 RED → fix → independent retest：pump 转换错误/cleanup error 与 close status 被检查；post-ACK 100ms 窗口能检测 late extra，但不代表永久 absence。disabled-link 为真实 model sends、无 forwarding、无匹配/数据的负向审计；pump-fault 与 late-extra 是审计 counterexample，不计入正常数据证据。`PATH_EVIDENCE.json` 绑定同一 run 的 model send/forward、RX capture/owned/running、GUID/SN/ordinal 链；这是 Host/model 观察，不是 SendBytes/SDK 物理因果证明。

真实 SDK/provider ABI、native/device/cross-device、真实取消、竞争自由仍未验证。P0/PF-01/PF-02 保持 `BLOCKED / NOT QUALIFIED`，生产 P4 未放行，无 QualificationProfile；默认 `NativeBackendUnavailable` 与 no-Git 状态不变。详见 [Real Participant Host](REAL_PARTICIPANT_HOST.md) 与本包 artifact receipt。

## 当前工作包：HOST_INPUT_CHANNEL_CLOSE_REOPEN（2026-09-26；artifact run 20260925T213718Z）

本工作包达到 **`HOST_INPUT_CHANNEL_CLOSE_REOPEN_HOST_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**，范围限定为冻结 R3 源码上的 one-local、one-dial Host facade。独立 corrected-environment receipt 为 `32/32 PASS`（14 个 input-close、16 个 provider-loop，以及首轮因遗漏标准 `PSModulePath` 失败的 2 项复跑）；canonical 独立全目标 Release receipt [`tester/canonical-full-r3/RECEIPT.json`](../.artifacts/host-input-close-reopen/run-20260925T213718Z/tester/canonical-full-r3/RECEIPT.json) 由 Python 直接等待 `returncode=0`，完整 CTest 为 `337/337 PASS`，原始日志为 [`ctest.stdout.raw.log`](../.artifacts/host-input-close-reopen/run-20260925T213718Z/tester/canonical-full-r3/ctest.stdout.raw.log)，119.84 秒。早期 `final-build-all-r3` 的 118.69 秒记录保留为非 canonical；原始 uninterrupted full Release run 的 `335/337 PASS`、exit 8 及其环境失败仍保留为历史原始证据。另有 Release ASan input-close `14/14 PASS`、provider-loop `15/15 PASS`，variants `1/1 PASS` 为构建/default gate。

独立 Close 已验证 receiver 可在返回后销毁，queued/copy/executing RX 结算后 gap RX 丢弃，TX/native owner 保持可用，并可在同一 local locator 安全 reopen 为新的 receiver/application lease。review 发现的 callback Close 与外部 Close 死锁已由真实 RED、锁前 TLS 拒绝修复及复测闭合；删除该 guard 的 m10 会重现有界超时。观测使用 group removal 后仍拥有的 `ApplicationAttachments` ledger，未把 binding-local default-zero projection 当作总账本结算。

Debug CONFIG 受仓库 Release-only ABI guard 阻断；TSan 未执行，不能宣称竞争自由。未执行真实 `ReceiverResource` 内部类、真实 Participant/SDK provider、完整 `TransportInterface`、native/device/cross-device。P0/PF-01/PF-02、P4、`NativeBackendUnavailable` 与 QualificationProfile 状态不变。完整范围见 [Host input close/reopen contract](HOST_INPUT_CLOSE_CONTRACT.md) 及 [.artifacts receipt](../.artifacts/host-input-close-reopen/run-20260925T213718Z/FINAL_REVIEW.md)。

## 当前工作包：HOST_FACADE_PROVIDER_LOOP（2026-09-26；artifact run 20260925T202407Z）

本工作包达到 **`HOST_FACADE_PROVIDER_LOOP_HOST_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**。
限定的 Host 注入 facade 已通过原 `SenderResource` owned-admission、
`NativeSendExecution`、Mock provider saved callback、`ProviderAdapter`、
`ServiceRecordBinding` owned-copy/original receiver lease 的闭环。最终独立 fresh
证据为 focused `16/16 PASS`、完整 CONFIG `323/323 PASS`；默认 OFF 选项通过，
production 与 Host facade 变体的混合链接由 CMake guard 拒绝。

本轮修复并复测了 identity/allocation 异常、duplicate start/ready 误关首个 owner、
pending cleanup 丢失、descriptor RX limit（64 接受、65 拒绝）等问题。Shutdown 会封存
admission、取消尚未运行的工作、等待 send/receive pump 和 receiver lease 结算后再关闭
binding；`CloseInputChannel` 在该手动 fixture 中明确返回 false 并保留 channel/receiver
至 whole shutdown。启动、Open、Attach、控制操作（含 Observe）要求外部串行；send/receive
pump 可与 shutdown 重叠，但禁止 callback 同步 Shutdown。

本轮只执行 SDK-free Mock provider Host fixture：没有真实 Participant/SPDP、SDK provider、
native/device、完整 `TransportInterface`、hot rebuild 或故障取消验证。默认
`NativeBackendUnavailable`、P0/PF-01/PF-02、P4 和 QualificationProfile 状态不变。
详见 [Host facade/provider loop](HOST_FACADE_PROVIDER_LOOP.md) 与
[最终复核](../.artifacts/host-facade-provider-loop/run-20260925T202407Z/FINAL_REVIEW.md)。

## 当前工作包：NATIVE_PROVIDER_ADAPTER_DESIGN_PHASE0（2026-09-26；artifact run 20260925T190646Z）

本工作包达到 **`NATIVE_PROVIDER_ADAPTER_BOUNDARY_DESIGNED / CHECKPOINT_READY / NOT_COMMITTED`**。
新增的 `NativeBackend`/`NullBackend` 与 `ProviderAdapter` 保持 SDK-free；Host mock
provider 经过真实 `ServiceRecordBinding` 的归属、owned-copy 和 receiver lease 路径。
最终独立证据为 focused `16/16 PASS`、完整 CONFIG `307/307 PASS`，并验证默认
配置保持关闭。P0 是 Host characterization；P1–P7 已由修复后的测试覆盖。

本阶段没有接入 production facade、ExperimentalBackend wrapper 或真实 DSoftBus
provider；没有 SDK 调用、native/device/DDS 测试，也没有改变
`DSoftBusTransport::init()` 的 `NativeBackendUnavailable` 默认行为。控制操作要求
外部串行；Initialize/Connect 持有既有 gate 时，公开 Observe 也要求外部串行。pending-stop
最多保留一个候选和 conflict 位，只有候选匹配或出现冲突后才锁定 stopped；单一 foreign
候选会被拒绝且不关闭已有 owner。Close 保留首个 provider
shutdown failure，但不宣称 SDK drain。callback 使用 Host weak handle，不能代表
SDK callback 自带 generation/context。P0、PF-01/PF-02、P4 和 QualificationProfile
状态不变。

证据：[Phase 0 收据](../.artifacts/native-provider-phase0/run-20260925T190646Z/FINAL_REVIEW.md)、
[测试结果](../.artifacts/native-provider-phase0/run-20260925T190646Z/tester/TEST_RESULT.json)。

## 当前工作包：CALLBACK_CONTEXT_DESTROY_CONTRACT（2026-09-25）

`CALLBACK_CONTEXT_DESTROY_CONTRACT` 达到 **`CALLBACK_DESTROY_CONTRACT_HOST_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**（限定 Host 范围，本轮按要求不提交）。本轮关闭组合层最后一项未闭合的寿命合同——**在回调执行上下文内销毁 binding 会自等待**（释放必须等待本帧持有的 receiver in-flight 许可），采用方案 A：新增 `ServiceRecordBinding::CloseRequest()`，它是唯一合法于 ingress/投递回调内的控制入口，只记录关闭意图（相位转 `Closing`）并立即返回，从不 drain、释放或等待；新增 `DeferredClose()` 由所有者在其后的控制上下文完成关闭，走与 `Abandon()` 相同的 drain+释放路径；`BeginClose()`/`Abandon()`/`DeferredClose()` 在回调内一律以 `WrongState` 明确拒绝而非等待；从回调内销毁则 fail-fast（带指向 `CloseRequest()` 的诊断后 abort），不再死锁。sink 登记、native owner lease、receiver lease 的 drain-before-free、claim 恰好 detach 一次这四条前提不变（关闭请求与同步路径共用同一个 `ReleaseLocalLocked()`）。RED：改动前回调内无任何入口可记录关闭意图（`status=19 expected=0`）、回调内销毁在有界 20s 下挂起（rc=124）。新增 11 个 case（CLOSE-C1…C7、C2b 完成守卫、无关 binding 与非末位 sibling 两个合法形状、禁止形状的跨进程 case），组合 case **32 个**；Release/Debug/ASan **32/32**、本配置 **86/86**、仓库标准 CONFIG host Release **290/290**；本轮 11 个变异体全部被对应用例杀死。合同与执行状态见 [PLAN.md 第 20 节](../PLAN.md)，收据与 RED 证据见 [.artifacts/srtc-close/](../.artifacts/srtc-close/)。上一轮 SRT-MEM（寿命/并发/投递上下文与测试判据修复）见 [PLAN.md 第 19 节](../PLAN.md) 与 [.artifacts/srtc-repair/](../.artifacts/srtc-repair/)。真实 SDK provider、设备与 DDS 证据仍 `NOT_RUN`；`DSoftBusTransport::init` 仍 `NativeBackendUnavailable`；P0/PF/P4 与 QualificationProfile 不变。

本轮**未提交、未合并、未推送**：起始提交 `145d0ee97de5e8728a985550da82f169c640d89e`。真实 SDK provider、目标 ABI 等价、callback 排空、设备/双板、DDS SPDP/SEDP/样本、同名 Participant 热重建与 RMW 仍 `NOT_RUN`；P0、PF-01/PF-02、P4 与 QualificationProfile 状态不变。

## 历史工作包：SRT-1 共享 service-record 表（2026-09-24）

`SRT_SHARED_SERVICE_RECORD_TABLE` 达到 **`IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；DEVICE_TESTED = NO`**。本切片实现已批准共享表设计的**有界模型**（新组件 `include/mdds/dsoftbus/ServiceRecordTable.hpp`、`src/ServiceRecordTable.cpp`、`tests/service_record_table_tests.cpp`），SDK-free：不调用 SDK、不依赖 Fast DDS、不建线程，不接线生产 backend。

已执行 host 证据：15 个契约 case 在 Release `15/15 PASS`、Debug `15/15 PASS`；standalone 配置全量 `43/43 PASS`；仓库标准 CONFIG host Release 全量 `258/258 PASS`（需要既有 `.artifacts/install/host/bin` 在 PATH，这是已记录的环境边界，不是本切片引入）；`/W4` 构建与 header 自包含编译无告警；多轮 case × 20–25 次重复运行（含并发 case）无失败。RED 为 stub 下 `15/15` 行为失败，且 RED/GREEN 绑定同一 test/header 字节。独立复核已跑三轮（`multi-review`：6 传感并行 + 独立证伪 + 裁决），三轮均无 blocking finding，gate 停在 `HUMAN_REQUIRED`（R2 承载路径要求人工 owner 批准合并，这是用户侧决定；其余理由均已修复）；全部候选的处置、文件哈希（取自暂存字节）、RED/GREEN 日志、测试预期更正与变异测试（三处变异在修复前存活、修复后被杀）见 [.artifacts/srt1/README.md](../.artifacts/srt1/README.md)。

合同覆盖：同活表重复注册不增 slot；单 owner 退出不退休仍服务的表；跨 role 保活与空 role 保留固定入口；连续性未知不按名字/id 复用、也不覆盖可能仍活的表；真实销毁后同名新实例取得新 slot/incarnation/入口；slot 耗尽只在需要新 incarnation 时、暴露前拒绝，tombstone 永不回收；归属在单一线性化点捕获且旧归属不重新归属；adapter 内部门不包围 sink，sink 可重入、异常被吸收并计数；有界计数与分配失败拒绝后无部分状态。

边界：`probes/softbus_ingress_contract.cpp` 的 11 个单 owner 用例保持不变，不被本切片替换或提升为共享表证据。真实 SDK record 创建/销毁观察、target 入口指针等价、SDK 内 ingress 前事件身份、callback 排空/静默、设备与双板、DDS SPDP/SEDP/样本、同名 Participant 热重建与 RMW 仍 `NOT_RUN`。生产 `init()` 继续 `NativeBackendUnavailable`；P0、PF-01/PF-02 仍 `BLOCKED / NOT QUALIFIED`，P4 未授权，无 QualificationProfile —— 本切片不改变这些结论。

## 历史已关闭工作包：DDS bounded performance baseline（2026-09-24）

`DDS_BOUNDED_PERFORMANCE_BASELINE` 已达到 **`BOUNDED_PERFORMANCE_BASELINE_MEASURED / CHECKPOINT_READY / NOT_COMMITTED`**。这是限定 `BYTES_NATIVE`、两块 RK3588A、6 个固定配置、3 个独立 MEASURE 重复的性能测量基线，不是生产资格或最大吞吐量声明。正式 RTT 共 3,300 次；固定负载 Reader delivery 共 6,120 次（RTT full-echo 的 Reader delivery 不计入该固定负载合计）。合并 p50/p99 为 RTT_1K `3.219125/4.435375 ms`、RTT_4K `3.226709/4.408250 ms`、RTT_256K `11.085668/14.135335 ms`。固定负载 Reader business goodput 为 1K `0.09765625 MiB/s`、4K `0.390625 MiB/s`、256K `1.00000000 MiB/s`。

最终 data review 为 PASS 且无未解决 blocker；独立 host post-review 为 OFF `243/243 PASS`、ON `245/245 PASS`；`49/49` 是 TRACE 修复前的历史 full-host stage，focused R3 的最终 analyzer 为 `58/58 PASS`。独立双板 receipt 为 `PASS_WITH_PRESERVED_TRACE_FAILURE`：query、失败 TRACE01、成功 TRACE02、M1/M2/M3 共 12/12 final board guards PASS；TRACE01 的失败、完整 charge 和 `RESERVED_UNKNOWN` ledger 保留，并排除正式统计。Native package 为 1,737 inputs、6 artifacts，focused R3 executor checks 为 PowerShell 4、Python 10、Node 14，package/input hashes 保持一致；native executable SHA-256 为 `5f98207268441debefcfaa57f0396a7be0d1cc2ed9d40e98e49f11211b30a440`。

测量限制已保留：19,116 行 CSV 是 planned-identity rows，不是 deliveries；SDK internal settlement、exact queue HWM 为 `UNOBSERVED`。仅 M3/A 的两个累计 `NO_APPLICATION` rejected records（不是四个）保留 payload/Writer identity 与 control-versus-business classification 未观测状态；每侧 TX reject 为 2，RX prefix overflow 只说明诊断细节受限，不证明业务丢失。该状态支持 `IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED` 的限定性能基线证据，不升级为生产 `VERIFIED`。P0/PF-01/PF-02 仍 `BLOCKED / NOT QUALIFIED`，生产默认 `NativeUnavailable`，P4 未发布，无 QualificationProfile；禁止 stage/stash/commit/amend/push。完整结果见 [RESULT_TABLES.md](../.artifacts/dds-bounded-performance-20260924/run-20260923T183702Z/analysis/RESULT_TABLES.md)、[FINAL_DATA_REVIEW.md](../.artifacts/dds-bounded-performance-20260924/run-20260923T183702Z/review/final-data/FINAL_DATA_REVIEW.md) 与 [device receipt](../.artifacts/dds-bounded-performance-20260924/run-20260923T183702Z/device/CANONICAL_INDEPENDENT_DEVICE_ALL_ATTEMPT_RECEIPT.json)。

资源观察为单进程 single-core CPU 每 case `3.293%–12.126%`、Participant 生命周期跨度 `85.656–86.118 s`、采样 RSS 最大 `29.824 MiB`、observed VmHWM 最大 `30.133 MiB`；这些是边界观察，不是长期泄漏或 SDK internal settlement 证明。

## 历史已关闭工作包：DDS multi-endpoint and late-joiner functional matrix（2026-09-23）

`DDS_MULTI_ENDPOINT_AND_LATE_JOINER_FUNCTIONAL_MATRIX` 已达到 **`DDS_NORMAL_FUNCTIONAL_MATRIX_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**，现作为历史已关闭工作包保留。13 个限定 `BYTES_NATIVE` 双板窗口全部通过独立复核：M1/M2/M3/M4 多端点与 Reader 删除、Q_BE_BE/Q_REL_BE/Q_REL_REL/Q_BE_REL、L1/L2/L3 晚加入以及 I1/I2 Topic/Domain 隔离。canonical matrix SHA-256 为 `3f51d3a4c5031e0b016d328497b1bae47b4d06082862b7229557fd3c6f4a8259`；结果为 102 次写入、132 条独立 Reader 交付/native 关联，详见 [matrix execution results](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/analysis/MATRIX_EXECUTION_RESULTS.json) 与 [final review closure](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/review-final/REVIEW_CLOSURE.md)。

独立 host 最终为 OFF `175/175 PASS`、ON `177/177 PASS`。四个受影响回归（hot1、1 MiB fragment、RELIABLE controlled-loss、BEST_EFFORT controlled-loss）均通过；另有 1 个 Socket-free directory query，本轮共 13 个矩阵窗口 + 4 个 native 回归窗口 + 1 个 query 窗口 = 18 个注册窗口，其中 17 个为实际 DDS native 窗口，36/36 板端 full guards。选定 native OFF/ON 包分别由 [BUILD_SELECTION.json](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/device-build/BUILD_SELECTION.json) 绑定，每包 1,693 inputs、6 artifacts。实际 QoS、匹配图和逐 Reader 账本见 [ACTUAL_QOS_MATCH_GRAPH_READER_LEDGERS.json](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/analysis/ACTUAL_QOS_MATCH_GRAPH_READER_LEDGERS.json)。

本状态仍不构成生产资格：P0、PF-01/PF-02 为 `BLOCKED / NOT QUALIFIED`，生产默认 `NativeUnavailable`，P4 未放行，无 QualificationProfile，SDK internal settlement 为 `UNOBSERVED`，禁止 commit/push。旧 [restored-package-device 交付线](../.artifacts/restored-package-device/run-20260923T075212Z/FINAL_GATE.json) 已正式关闭，本轮未重开。L2/L3 的晚加入证据未形成新的 no-application gap 或 PF-02 物理旧帧隔离证明；RAW、三板、mixed transport、ROS 2/RMW、性能及新的 100 轮资格均未执行。上一轮 RX closeout 以下内容保留为历史记录。

## 历史已关闭工作包：DDS keyed history and resource boundaries（2026-09-23）

`DDS_KEYED_HISTORY_AND_RESOURCE_BOUNDARIES` 已达到 **`FINAL_CODE_AND_DEVICE_REVIEW_CLOSED / CHECKPOINT_READY / NOT_COMMITTED`**。独立最终 review 报告 `FINAL_CODE_AND_DEVICE_REVIEW_CLOSED`，授权范围内无未解决 blocker。K1、K2、K3、K4_TRUE、K4_FALSE、H1、H2、H3、R2、R3 十个 keyed native cases 均 `PASS`；R1 为 `HOST_ONLY_PASS / DEVICE_NOT_APPLICABLE`。canonical totals 为 62 writes、128 API data/take observations、58 unique data identities、42 taken identities、3 instance-state records、58 native data joins 和 3 native state joins；58 data + 3 state native associations 是本轮证据支持的范围。

最终 host receipt 为 OFF `205/205 PASS`、ON `207/207 PASS`，53/53 analyzer tests 与 12/12 post-R4 affected gates PASS；OFF/ON native packages 均 build PASS，每包 1,719 inputs、独立检查零 mismatch，设备仅执行 OFF package。14 registered windows（13 DDS attempts + 1 Socket-free directory query）对应 28 board windows，168 individual guard JSONs 全部 verified，28/28 board full guards PASS。K1-attempt-01 仅 outer launcher exit 为 `UNOBSERVED`，runner/remote exits 为 `0`，不计入 canonical totals；query 保留 82 per-command exit files all zero，但 aggregate launcher/runner exit 不推断为 PASS。Fresh small replay `87c0d1ef21b9257c` 和 fresh M2 `50ebee5e4a4e215a` 均通过其限定审计。

本轮观察仅适用于 fixed Fast DDS 3.6.2.0、generated bounded keyed type、`BYTES_NATIVE`、两块现有板卡、有限 history/resources 和已执行 cases。H1 的 4 个 oldest-before-Reader evictions 是 Writer `KEEP_LAST` 预期行为；H2 repeated reads 不等于 network duplicates；`valid_data=false` 未解析 payload。Reader `get_key_value` 与 Writer `lookup_instance` unsupported，standalone registration propagation 未实现；WriterDataLifecycle broad documentation warning 与 true/false narrow observed behavior 存在冲突，不能宣称全策略合格。SDK internal settlement 仍 `UNOBSERVED`。

资格边界保持不变：P0、PF-01/PF-02 为 `BLOCKED / NOT QUALIFIED`，生产默认 `NativeUnavailable`，P4 `NOT_RELEASED`，无 QualificationProfile；RAW、三板、mixed transport、ROS 2/RMW、性能、100 cycles 及全拓扑均未执行。本轮没有 Transport/core/FastDDS/SDK implementation changes；Git state 为 `NONE / NOT_COMMITTED`。完整证据、文件哈希和 checkpoint-ready 范围见 [keyed docs receipt](../.artifacts/dds-keyed-history-20260923/run-20260923T140748Z/docs/DOCS_SYNC.md)。

## Historical — RX accounting closeout（2026-09-22）

`HOT_REBUILD_BASELINE_CLOSEOUT_AND_RX_REJECTION_ACCOUNTING` 当前为 **DONE_WITH_HISTORICAL_LIMITATION / NOT_COMMITTED**。本轮证据与分析已完成；源码封存/恢复及最终 checkpoint 结论以本轮 `FINAL_REPORT.md` / `FINAL_GATE.json` 为准，本状态页不独立宣称恢复验证 PASS。

上一工作包的限定功能证据仍唯一定位为 `.artifacts/hot-rebuild-production-integration-20260921/run-20260921T152404Z/`：focused `23/23 PASS`，fresh OFF `132/132 PASS`（实验 93），ON `134/134 PASS`（实验 95），native build `0/0/0`，各 manifest 1,655 inputs、6 artifacts，20/20 final guards；其失败 attempt 与 1/10/100 结果均保持历史归属。本轮新证据统一定位为 `.artifacts/hot-rebuild-closeout-rx-20260922/run-20260922T012640Z/`：最终 host OFF `148/148`、ON `150/150`、analyzer `31/31`，post-review native OFF/ON build `0/0/0`，每包 1,657 inputs、6 artifacts，主程序 SHA-256 为 `6352b28f6b7f3d033bb8ecd58f5319eb4479cfe76d5d0ece461ed21487f1c0fc` / `dedfad61e0c26da6e9d7a5e26b60c21ab8db9130a573d4513a5223a8ce8f3a19`。

实现接线是 Participant facade → application/receiver/logical-token attachment → shared ProcessRuntime/ProcessCoordinator/ApplicationAttachments/InputChannels；每个 endpoint 具有独立 native State/listener/table/owner/worker。P1 detach 仅排空自身旧 lease、TX、RX 和 last-payload，保留 live native table；Q 不受影响；全部 application detach 后才 Finalize native stop/join/tombstone。TX 仍是原有有界 owned-vector 队列与 application 联合 drain，RX 是一次 copy 加原 lease；默认 setup=1，双 endpoint 显式 setup=2，预算为 `2/2/1`（setup/send/close）。

本 checkpoint 的资格边界未改变：P0、PF-01/PF-02 为 `BLOCKED / NOT QUALIFIED`；生产默认 `NativeUnavailable`，P4 未放行，无 QualificationProfile，commit/push 为 `USER_PROHIBITED`。OLD2 物理旧帧隔离、SDK internal settlement、RAW/三板/mixed/self-route/压力故障 close 未验证；full-native destroy/rebuild 是保留的历史 RED。1 MiB functional coverage PASS 但原 all-send predicate 明确 `FAIL`，16 条 active pre-Captured ingress 的具体接收前拒绝原因 `UNOBSERVED`；旧 listener terminal map 不保证数值为零，不能写成旧 current-map 日志必为零。

本轮 RX closeout 将历史 16 条限定为 217 条 native ingress 中的 16 条 ingress-only 记录：没有 Captured、owned-copy、长度/hash、Writer GUID/SN 或拒绝结果，具体原因 `HISTORICAL_CAUSE_UNRESOLVED`。新诊断记录在决定点捕获原始 adapter 状态和原因，固定容量为每 State 1,024 条、最大保留 8 个 service incarnation；未知字段、overflow 或 incomplete snapshot 不得被分析器归类为 harmless。新 1 MiB 窗口 `5923cd595106e8a6` 为 203 admissions / 0 rejects（`NOT_REPRODUCED`），hot-smoke `98e4d1228566a0a4` 为 144 decisions、141 accepted、3 `NO_APPLICATION`（严格 hot gap 2、final cleanup 1）。这些新窗口不倒填历史 16 条，也不证明 SDK 内部结算。

本轮诊断为 `DIAGNOSTIC_ONLY`；未发现需要修复的 Transport admission/lifecycle 缺陷。PF-01/PF-02 的外部契约材料见 [.artifacts/hot-rebuild-closeout-rx-20260922/run-20260922T012640Z/PLATFORM_CONTRACT_REQUEST.md](../.artifacts/hot-rebuild-closeout-rx-20260922/run-20260922T012640Z/PLATFORM_CONTRACT_REQUEST.md)。源码封存/恢复及最终 checkpoint 结论由本轮 `FINAL_REPORT.md` / `FINAL_GATE.json` 统一记录；不生成 QualificationProfile，不启用生产 Native backend，不执行 stage/commit/amend/push。

## Historical — same-name hot-reattach admission: blocked at STOP 0 (2026-09-21)

The requested same-process, same-display-name Participant hot-reattach pilot is `BLOCKED / NOT_COMMITTED`, not a completed pilot. The corrected admission treats OLD1 as a detach/drain barrier; OLD2/PF-02 and the full-native-record RED are nonblocking for a normal pilot. The actual first gate is `PHASE0_EXISTING_REAL_DDS_LIVE_INPUTCHANNEL_REATTACH_PATH`: real Participant deletion reaches `ExperimentalTransport::shutdown` and terminally stops that State, while the experiment has no existing bridge attaching a new Participant facade to a surviving live native InputChannel/table. This is a present-code integration/lifetime-binding gap, not a platform impossibility or a same-display-name/all-channel claim.

The earlier same-native-service reproducer remains a separate, nonblocking RED artifact and is not the admission premise. No new build, test, device, SDK or permission operation occurred in this packet. Prior host suites remain historical; current native build, device pilot, single cycle and repetition are `NOT_RUN`.

P0 remains `BLOCKED / NOT QUALIFIED`; PF-01/PF-02, production `NativeUnavailable`, P4 unauthorized, no QualificationProfile and no commit/push remain unchanged. The prior dual package’s OFF/ON `55/57`, five device windows and `DONE` bounded status remain historical evidence of that prior package only. See [hot final report](../.artifacts/experiment-hot-rebuild-20260921/run-20260921T141304Z/FINAL_REPORT.md).

## Historical — 双实例验证器修正后的最终有限实验（2026-09-21）

独立 tester receipt [FINAL_RESULT.md](../.artifacts/experiment-dual-20260921-second-instance-fix/run-20260921T121708Z/tester/FINAL_RESULT.md) 记录五个新鲜 `BYTES_NATIVE` 双板窗口全部通过：双 Participant/P1 删除后 P2 存活与继续写入、默认小消息、256 KiB 分片、RELIABLE 单次 whole-message drop→NACK_FRAG 关联恢复、BEST_EFFORT 目标 sample 缺失。每个窗口均 wrapper/两板 remote exit `0`、无 host timeout/watchdog；两板均有 pre-grant Granted、最终 Revoked、byte/metadata/ownership/snapshot/process guards。

验证器修正后的 host 结果为 OFF `55/55 PASS`、ON `57/57 PASS`，review 后 native OFF/ON bridge/configure/build 为 `0/0/0`；两个 manifest 各 93 个输入，主程序 SHA-256 分别为 `09b16d92…f23c`、`5096447d…6f55`。双实例 raw 复核确认 P1/P2 两组各 6/10 个正常样本、P2 身份/状态/租约/回调槽连续性、P1 完成标记后的写入顺序，以及所有 32 条正常 native chain 的 opposite-side SDK 发送配对。B 侧额外的 closing/no-receiver ingress 已单独计入，不被静默丢弃；SDK 内部 settlement 仍 `UNOBSERVED`。

这只是 default-OFF 的受限实验结果，不是生产资格或发布结论。`P0=BLOCKED / NOT QUALIFIED`、`NativeUnavailable`、PF-01–04、生产 P4 未授权、`no QualificationProfile`、`COMMIT_GATE=PROHIBITED` 和未提交状态保持不变。RAW、热重建、多 Participant、强压力/故障 close、混合 transport、native self-route、1 MiB 本轮未重跑且三板测试继续 DEFERRED；旧 dual-r2 的 predicate 失败、window 启动/abort 缺口及其后 post-abort 重跑均保留历史边界。详见 [实验报告](FASTDDS_DSOFTBUS_EXPERIMENT_20260921.md) 与 [最终报告](../.artifacts/experiment-dual-20260921-second-instance-fix/run-20260921T121708Z/FINAL_REPORT.md)。

## 当前证据：独立有限例外与历史收口（2026-09-21）

### 独立有限例外：真实 Fast DDS × DSoftBus R4（2026-09-21）

经用户明确授权的 default-OFF 实验已在两块 RK3588A 上完成限定 BYTES 场景；详见 [实验报告](FASTDDS_DSOFTBUS_EXPERIMENT_20260921.md) 及其 [R4 receipt](../.artifacts/integration-experiment-20260921/tester/device-reader-r4/RESULT.md)。OHOS bridge/configure/build 为 `0/0/0`，Windows CONFIG/Release 为 `7/7 PASS`，A/B 的真实 SPDP/SEDP、Writer/Reader、10 个样本、RELIABLE acknowledgement 和正常 close 均有原始命令、日志、哈希与权限撤销证据；限定场景 `SCENARIO_GATE=PASS`。

生命周期/observer hardening 由独立 host `16/16 PASS`、review 和新包 `448663…` 的双板复测闭合；后续 fragments host `17/17 PASS`、OHOS `0/0/0`，256 KiB 与 1 MiB 各三样本 DATA_FRAG 均完成数据/ACK/Reader/close/no-watchdog 观察。1 MiB 79 条 DATA_FRAG 观察中每序列 18 个唯一范围，重复范围不解释为 NACK 或具体重试原因；小/256 KiB 的完整 guard 在撤权边界 `NOT_OBSERVED_AT_BOUNDARY`，1 MiB 即时撤权 guard 恢复 baseline。细节、GUID/SN/range/sample-size/native hash 记录和 package 绑定见 [实验报告](FASTDDS_DSOFTBUS_EXPERIMENT_20260921.md)。

Controlled-loss 独立验证也已完成：最终 OFF/ON host `21/21`、`23/23 PASS`，OHOS `0/0/0`。RELIABLE v2 记录实际单次 whole-message drop、NACK_FRAG 缺失范围请求、重发后的 ReaderSampleInfo/ACK 因果链；BEST_EFFORT 记录目标 sample 缺失、后续 mask `006` 和 `ACK=NotCalled`。两种运行均有正常 observable cleanup 与 fresh full guard 恢复；SDK internal settlement 仍 `UNOBSERVED`。详见实验报告及 `.artifacts/experiment-loss-20260921/tester/analysis-v2-checks.json`。

该结果是独立工作包的有限例外，不是生产 P4 放行或 P0 资格化：`P0=BLOCKED / NOT QUALIFIED`、`NativeUnavailable`、`COMMIT_GATE=PROHIBITED` 保持不变，SDK internal settlement 仍 `UNOBSERVED`。RAW 两种、热重建、多 Participant、强压力/故障 close、混合 transport 和 native self-route 均保留 `NOT COVERED`。

### 前一工作包：可行性收敛（历史）

该工作包状态为 `CLOSED_WITH_PLATFORM_DEPENDENCIES`（前一可行性收敛工作包已关闭，平台问题未解除）；平台资格仍为 `P0 BLOCKED / NOT QUALIFIED`；生产入口保持 `NativeUnavailable`，该工作包不生成 QualificationProfile、不进入 P4 native backend、不 commit/push。该工作包 build/host/device 均 `NOT_RERUN`；历史 D 交付窗口 `64/64` 与 V4 `1/64` 不拼接为压力资格，`NEED_ACK` 的 `NotCalled` 保留。只有具体新机制、平台契约、可判别 trace 或明确用户决策才重开对应问题；同一阻塞不会自动启动新一轮全面探针。详见 [该工作包 review](../.artifacts/closure-20260921/review/REVIEW.md) 与 [只读核查](../.artifacts/closure-20260921/verification/RESULT.md)。

平台问题的版本/profile/config、缺失证据和解除条件统一维护于 [P0 实施与验证报告](P0_IMPLEMENTATION_20260920.md) 的“平台问题清单与解除条件（本轮收口）”。callback incarnation slot 按已批准的实际 service record 实例 + role 及 process-lifetime tombstone 规则处理；它不是 Participant 配额，也不解决 numeric ID、SDK 内部事件归属或 native 阻塞。P1/P2/P3 主机诊断历史结果保留，本轮关闭后不自动续跑。

历史 final guard 保留 `Passed=false / NON_PROJECT_CHANGED`。前轮核查确认 device-exit 授权前与 V4 撤权后的 A/B 快照逐字节一致；前轮 A/D 的独立权限窗口也各有恢复收据，当时 owned DATASYNC 为空、manager 为 `DENIED`。该次只读收口没有操作或重新查询设备权限，不把历史 readback 写成新的设备观测；范围记录见 P0 报告。本次 R4 使用独立权限窗口，其授权、撤销和恢复证据见上方实验报告。

# 历史证据快照（2026-09-20）

本轮最终独立 tester 的 canonical receipt 为 [`.artifacts/implementation-20260920/p0-cancel-schedule-final-device/RESULT.md`](../.artifacts/implementation-20260920/p0-cancel-schedule-final-device/RESULT.md)，详细记录见 [P0_IMPLEMENTATION_20260920.md](P0_IMPLEMENTATION_20260920.md)。诊断探针源码和构建脚本存在，最终 Host Debug/Release 各 18/18 CTest PASS，取消探针内部 CANCEL-01..19 均 PASS；这不等于生产 Transport 或 Fast DDS 集成通过。

| 范围 | 当前状态 | 证据边界 |
| --- | --- | --- |
| Profile/identity 诊断探针 | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED（限定） | 三种 profile、身份可用性、32/255/4096/16384/60000 B 精确记录在双板 B→A→B 通过；不证明 DDS 或加密效果 |
| Cancellation 诊断探针 | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED（结果受限） | BYTES 的 1 秒诊断配置 FAIL；RAW 与 RAW_ENCRYPTED 未观察到选定取消区间，标记 NOT_EXERCISED；不证明所有有限时限均不可能 |
| P0 平台资格 | BLOCKED / NOT QUALIFIED | SDK 发送/关闭总时限、短返回、迟到 callback tombstone/代际防护、adapter I/O/worker/receiver 排空、资源容量和完整权限/拓扑资格尚无充分证据 |
| Fast DDS DSoftBus production backend | NOT IMPLEMENTED / NativeUnavailable | 当前改动是 probes/helpers/build wiring，不是生产 Transport；SPDP/SEDP、Writer/Reader、可靠性、分片和 RMW 未运行 |
| P1-P6、三板测试 | PLANNED / DEFERRED | 依照冻结方案推进；三板 A→B/C 及故障隔离明确留待后续 |
| PRE_COMMIT | FAIL | P0 未资格化，Ready for commit = NO；不允许据此提交或发布 |

该段所指的 2026-09-20 权限快照属于历史记录；2026-09-21 final guard 现记录为 `Passed=false / NON_PROJECT_CHANGED`，因此不能继续称为完整 guard PASS。当前只确认 owned DATASYNC 已撤销、同 token manager readback 为 `DENIED`；完整 baseline 差异仍按 P0 报告记录。未修改 DSoftBus、UDP/TCP/SHM 或 Fast DDS DDS/RTPS 语义。历史状态记录从下一节开始保留，不得覆盖本快照。

## 最新证据同步（2026-09-14 UTC）

Fast DDS 主机库已完成当前准备构建：`.artifacts/build/fastdds-logs/HOST_BUILD_RESULT_20260914.md` 记录实际 237/237 编译和 install exit 0；该构建使用已记录的 UTF-8/兼容性编译选项，失败日志仍保留。此证据只支持 `BUILDS`，不支持 Fast DDS Transport、DDS 集成或上游测试通过。

S3-D2 RuntimeSendPath / DSoftBusSenderResource 发送资源桥接已完成限定范围实现，并由独立 final tester 和 code review 验证：`.artifacts/verification/runtime-send-final-20260914T0527Z/RESULT.md` 报告 direct Debug/Release 各 27 组、root Debug/Release 各 17/17、public consumer、SDK23 arm64 static build 均 PASS；review `.artifacts/review/20260914T052640Z-runtime-send-d2-code/REVIEW.md` 无 actionable finding。该结果仅覆盖发送计划、当前 `send_lambda_` 桥接、批量队列接纳、释放/关闭契约及消费者构建，不覆盖 native backend、DSoftBus SDK Send 或设备 Transport。

token-first 双板 Socket record 实验在有界范围 PASS：`.artifacts/diagnostics/token-first/20260914T051101677Z-execute/` 中 A/B 各方向均完成 32、255、4096、16384、60000 字节五条记录的字节/长度/计数校验；A Listen、B 同步 Bind、发送和关闭观察均有证据。`.artifacts/verification/20260914T045328Z-token-first-device/MAIN_AUDIT.md` 与 `final-readonly-20260914T0514Z/RESULT.md` 证明随后两板权限均撤销、身份记录保留且原始 JSON 哈希恢复。结果仍明确 `quiescence=NOT_VERIFIED`、DDS 未运行、Transport 未实现；不把旧 G1/G6 或此前 exit=6 尝试自动改写为本轮结果。

PLAN 15.2 thread-token diagnostic is IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED within its bounded scope. Independent two-board evidence is `.artifacts/diagnostics/thread-token/20260914T035011891Z-tester-device/`, with result review `.artifacts/review/20260914T035632Z-thread-token-device-results/REVIEW.md`; runner result is `.artifacts/verification/thread-token-runner-final-20260914T034800Z/result.txt` (retained label, actual UTC 03:48:30–03:48:31). Board A observed old worker controlled A historical worker unchanged while the main thread used controlled A token reference; B observed controlled B historical worker unchanged while the main thread used controlled B token reference. Both main tokens were restored, exact diagnostic PIDs disappeared, and persistent JSON/stat/label/guards were unchanged. This does not identify an actual DSoftBus callback token or prove the historical Socket failure root cause.

## 最新证据更正（2026-09-14）

S3-C `InputChannels` 已在 OOM 修复后由独立 final tester 重新验证：`.artifacts/verification/facade-host-final-20260914T075048953Z/RESULT.md` 报告 root CONFIG 20/20、SOURCE Debug/Release 各 19/19、facade repeat 20 次、FCD 21/21、IC-12 及 public consumer 均 PASS；状态为 `IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED`（限定范围），不扩展为 device-tested 或完整 Transport verified。较早的 14/14 结果保留为历史证据。

Descriptor facade 已实现并完成限定范围 host/SDK ABI 验证；native backend/connection manager 仍未实现。此前 S2 跨设备 exit=6 尝试及其审计限制保留为历史证据；本轮 token-first 有界 record 实验另有独立 PASS 证据，不能互相覆盖。CommSvc 旧线程 token 拒绝仍只是待审线索，不能宣称根因或修复；整体仍 `NOT VERIFIED`（完整 Transport 发布未就绪）；N1 限定执行门已 PASS，其提交门槛单独核对。

状态依据最终权限生命周期 tester run `20260914T005000Z-permission-lifecycle-final`、S3-A final `20260914T011000Z-s3a-final` 和 Locator wire final `locator-wire-final-20260914T013535Z`；S1 设备证据仍独立来自 `20260913T155515Z-final`。旧 Socket positive 尝试的审计结论见 `20260914T012000Z-record-positive-second/audit-current/TEST_RESULT.md`；RecordPairHarness 是独立的新 host/simulated helper，不能混同。

| Scope | Status | Evidence / boundary |
| --- | --- | --- |
| S0 baseline and plan | RECORDED / PARTIAL | `PLAN.md`、源码/SDK/设备基线已登记；完整 ABI/产品集成基线仍未完成 |
| S1 Fast DDS interface probe | PASS | Host CTest 中接口契约为 1 个 CTest（内部 IF-01..04）；OHOS arm64 build；A/B 设备 IF-01..04 各 4/4 |
| S1 DSoftBus symbol probe | PASS | A/B required and optional symbols present |
| S2 Socket capability check | PASS（本机已授权限定范围） / NOT RUN（跨设备） | 无授权历史对照 `20260914T000333Z-socket-recheck` 记录权限拒绝；当前 A/B 已授权 probe 均成功创建 Socket 并 Shutdown 一次；不覆盖跨设备通信 |
| S2 Bind/Listen/SendBytes/Shutdown contract | 有界双板 record PASS；quiescence 未验证 | token-first 实验 A Listen、B 同步 Bind 及双向五种记录通过；发送 ≤3 ms、关闭观察 202 ms。SDK callback 排空、并发取消/重连和持续负载仍未验证 |
| S2-P native 身份/最小权限探针 | DEVICE_TESTED / PASS（限定范围） | A/B 同 ID regrant → 独立 Socket 创建/Shutdown 一次 → revoke 闭环 PASS；服务 GRANTED→DENIED、self 还原、JSON guard、外部记录及 provider hashes 保持；不等于 Transport 通信通过 |
| PLAN 15.2 thread-token diagnostic | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED（限定范围） | 两板受控短命进程测量 PASS；旧线程保持原 token、新线程观察目标 token、主线程恢复；不含 DSoftBus callback 身份或 Socket 根因证明 |
| S2 record/route probe | IMPLEMENTED / HOST_TESTED / DEVICE_TESTED / PASS（有界） | token-first 独立执行与 Main audit：A/B 各方向五条完整记录通过，真实 PID 收尾并独立复核；权限已撤销、原始 hash 恢复。仅为诊断包络，不等于生产 Transport 或 callback quiescence |
| S3-A EndpointDirectory | BUILDS / HOST_TESTED（cross build） | `include/mdds/dsoftbus/EndpointDirectory.hpp`、`src/EndpointDirectory.cpp`；最终 Debug/Release consumer/library-only/cross 共 13 组 PASS，证据 `20260914T011000Z-s3a-final`；当前不标 DEVICE_TESTED 或完整目录 VERIFIED |
| Locator wire characterization | BUILDS / DEVICE_TESTED / REVIEWED（限定范围） | 真实 Fast DDS 3.6.2.0 Locator wire A/B 各 4 组 PASS，最终证据 `locator-wire-final-20260914T013535Z`，review `.artifacts/review/20260914T013118Z-wire/REVIEW.md`；不含 Socket/DDS discovery |
| RecordPairHarness | IMPLEMENTED / HOST_TESTED / REVIEWED（限定范围） | final host/simulated 测试通过，closure review 完成；无实际 SDK records 成功证据 |
| Record timing fix | BUILDS / HOST_TESTED / DEVICE negative TESTED（限定范围） | final `.artifacts/verification/record-timing-final-20260914T014906Z/`：Debug/Release 各 21 组、平台交叉构建通过；A/B 拒权模式真实退出 6、非法参数退出 64，符合负例预期。`BindReturned` 是 checkpoint，不等于物理 API 返回瞬间 |
| S3-B SendQueue / S3-D1 batch | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；NOT DEVICE_TESTED | final `.artifacts/verification/20260914T041229981Z-send-batch-final/`：direct Debug/Release 各 26 组、root Debug/Release 各 14/14、consumer、SDK23 arm64 static build PASS；review `.artifacts/review/20260914T040359Z-send-batch/REVIEW.md` 无 blocking。仅覆盖队列原子接纳与记录所有权移交，不含 SDK Send/Transport |
| S3-C InputChannels | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；NOT DEVICE_TESTED | OOM 修复后的 final `.artifacts/verification/facade-host-final-20260914T075048953Z/RESULT.md`：root CONFIG 20/20、SOURCE Debug/Release 各 19/19、FCD 21/21、IC-12、重复运行与 public consumer PASS；review `.artifacts/review/20260914T-facade-code/REVIEW.md` 无 actionable finding；不含 SDK callback 或完整 Transport |
| S3-D2 RuntimeSendPath / SenderResource | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED；NOT DEVICE_TESTED | final `.artifacts/verification/runtime-send-final-20260914T0527Z/RESULT.md`：direct D/R 各 27、root D/R 各 17/17、public consumer、SDK23 arm64 static build PASS；review `.artifacts/review/20260914T052640Z-runtime-send-d2-code/REVIEW.md` 无 actionable finding；不含 native backend、SDK Send 或 DDS/RMW |
| S3 real Fast DDS binding | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED（限定范围） | frozen eight-file slice：host SOURCE D/R 各 17/17、source consumer 1/1、CONFIG matrix 32/32、CONFIG root 17/17、flat 2/2、nested 4 组、poison/absent SOURCE 17/17；SDK23 arm64 consumer/S1 与两板 ABI/model PASS，wrong-architecture rejection PASS。证据 `.artifacts/verification/real-binding-final-20260914T064920994Z/RESULT.md`、`.artifacts/verification/real-binding-sdk-final-20260914T065331423Z/RESULT.md`、review `.artifacts/review/20260914T-binding-fixes/REVIEW.md`。预加载依赖须为 `IMPORTED_GLOBAL`，Release 映射仅未设置或单一 `RELEASE`；bare-PATH、长路径和初始部署失败保留为边界。仅为配置/ABI 绑定，不含 DSoftBus Socket、DDS discovery、同名 Participant 热重建或 RMW；该历史切片不证明完整 Transport 发布就绪；N1 提交门槛单独记录 |
| S3-LC Socket lifecycle/callback close | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（LC runner 限定范围；NOT DEVICE_TESTED） | 独立 final tester `.artifacts/verification/lc-runner-final-20260914T120200Z/RESULT.md`：harness 80/80、runner 42/42、validator 130/130、兼容性 16/16、LC validator 169/169、RecordPairHarness 10/10、syntax 4/4，以及 Runner 私有 real-fd/process diagnostic 均 PASS；review closure `.artifacts/review/20260914T-lc-runner-code/CLOSURE.md`。修复覆盖已观测 collector exit-before-probe race；full log overlap、SDK quiescence、真实板端 Socket/Transport 仍 NOT_RUN，不能宣称完整 Transport 或 VERIFIED |
| S3-LC pure launch validator | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（纯校验器限定范围；NOT DEVICE_TESTED） | final tester `.artifacts/verification/lc-launch-validator-final-20260914T074210082Z/RESULT.md`：record v1 130/130、compatibility 16/16、LC 169/169、harness 10/10、syntax 6/6 PASS，冻结文件哈希 MATCH；review `.artifacts/review/20260914T-lc-validator-code/REVIEW.md` 无 actionable finding。校验器不读设备、不改权限、不认证证据真实性；LC runner 的后续 final evidence 见 `lc-runner-final-20260914T120200Z/RESULT.md`，但设备启动未执行 |
| S3 real Transport facade | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围；NOT VERIFIED） | Descriptor/Transport factory、真实接口与 selectors 已实现；native backend 缺失时 production `init()` 固定失败并报告 `NativeBackendUnavailable`，私有 HostModel 不可作为生产 fallback。final host `.artifacts/verification/facade-host-final-20260914T075048953Z/RESULT.md`、SDK/双板 ABI `.artifacts/verification/facade-sdk-final-20260914T075114524Z/RESULT.md`、review `.artifacts/review/20260914T-facade-code/REVIEW.md`（无 actionable finding）。未证明 native communication、DDS discovery、同名 Participant hot rebuild 或 RMW |
| S4 SPDP | NOT RUN | 无 Fast DDS Participant integration |
| S5 SEDP/user data | NOT RUN | 无 Writer/Reader、匹配或样本证据 |
| S6 fault/reliability | NOT RUN | 无真实 Transport |
| S7 RMW | NOT RUN | RMW 版本和注入边界尚未锁定 |
| ApplicationAttachments root wiring | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；NOT DEVICE_TESTED | `.artifacts/verification/attachments-wiring-final-20260914T093400Z/RESULT.md`：选项 7/7、SOURCE Debug/Release 22/22、CONFIG Release facade ON 23/23、public consumer、binding/facade 矩阵和 SDK23 arm64 link PASS；接线 review `.artifacts/review/20260914T-attachments-wiring-code/REVIEW.md`（SHA `89C726AF62F1B001523C2221B9BBE70DF74DC56927D544C67984900517912418`）无 blocking。仅证明 root 构建接线，不含 HDC、SDK runtime、DDS 或热重建 |
| N1 stable native owner | IMPLEMENTED / BUILDS / HOST_TESTED / SDK23 BUILD/LINK PASS / REVIEWED（模型/owner slice 限定范围） | N1 completion final `.artifacts/verification/n1-completion-final-20260914T1455Z/RESULT.md`：候选树 87/87 字节一致、SOURCE Debug/Release 各 23/23、CONFIG 24/24、root options 9/9、attachment 7/7、consumer 1/1、standalone、20 次重复、SDK23 link/readelf PASS；root wiring 目标仅在 fresh cache 中按 `BUILD_DSOFTBUS_DIRECTORY` 默认，测试仅随 `BUILD_TESTING=ON` 注册。review 无 actionable finding。无 SDK runtime、DSoftBus、DDS 或热重建证据 |
| N2 native send execution | IMPLEMENTED / BUILDS / HOST_TESTED / SDK23 ABI LINK/ELF TESTED / REVIEWED（SDK-free injected synchronous model 限定范围） | [N2 completion](N2_COMPLETION.md)；final receipt SHA `3AC3B952B424D3EDFF1A16D584463ABA89F369D9E0A37527316B32D3955612F7`；SOURCE Debug/Release 各 24/24、CONFIG 25/25、20×10 repeat、N1 9/9、attachments 7/7、binding 32/32、facade 4/4、SDK23 AArch64 link/readelf PASS；review SHA `227E9479AC4CA318B1B89BD51AE66D554ED47F69819DE2B13E1CAB8CA6D660D7` 无 actionable finding。真实 Socket/SDK Send、DDS/RTPS、设备/跨设备、RMW、同名 Participant 热重建 NOT RUN；scoped model gate PASS，不等于完整 Transport 或普通项目 commit |
| Overall | NOT VERIFIED | frozen N3 lifecycle model is IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED in its bounded host scope; native backend remains `NativeBackendUnavailable`; real DSoftBus provider/callback quiescence, device/cross-device runtime, same-name DDS Participant hot rebuild, DDS SPDP/SEDP/user data/RMW remain NOT_RUN; full Transport release NOT VERIFIED |

## S1 上轮 review and re-test

Independent Tier-2 review 发现唯一 P2：“positive Socket Shutdown 分支未测试”。该问题已补测并由最终 tester 重新验证；这些是 S1 历史证据，不改变后续跨设备记录尚未执行的事实。无授权 Socket 失败的最新设备证据见 `.artifacts/diagnostics/20260914T000333Z-socket-recheck/`，其中同 PID 服务日志明确记录 `DISTRIBUTED_DATASYNC` 权限拒绝；当前已授权本机 Socket 生命周期则由 S2-P final run 单独证明。

## Remaining gates

ApplicationAttachments 的设计 R1（unique InputChannels/attachment 封装）与代码 R1（Close waiter 等待迟到 Opening）是两个不同问题，均已在各自限定范围通过 review/test；root wiring final 也已通过。64 次仅为模型 application attachment 周期，不是 DDS Participant 或真实同名热重建；本 slice 未执行 HDC、SDK runtime、DSoftBus Socket、DDS 或 RMW。native backend、实际 DDS/SPDP/SEDP/样本、同名 Participant 热重建和 RMW 仍 NOT_RUN。

LC runner 最终独立 tester 已通过其 host/private real-fd/process 诊断范围，但没有设备执行；N1 completion 的候选树、host/source/consumer、SDK23 ABI 和 root wiring 范围通过，完整记录见 [N1_COMPLETION.md](N1_COMPLETION.md) 与 `.artifacts/verification/n1-completion-final-20260914T1455Z/RESULT.md`。N2 限定模型的实现、review 与 final tester 已完成，完整记录见 [N2_COMPLETION.md](N2_COMPLETION.md)；root CTest 使用进程内 machine `PSModulePath` 与绝对 host DLL `bin` 前缀。该段是 N2 完成文档的原始提交前快照：当时 N2 尚未提交；随后 N2 已提交为 `def10ea376cb3bf9921c1c7ea74c92b1da9e04e8`，N3-1 root wiring 已提交为 `ddc08c55aee78414015d194f2c976da690948f12`。原始模块查找/相对 DLL 路径失败、早期 invalid-hash 诊断均保留为历史证据，不计为 PASS。真实 Socket、DDS、设备和同名 Participant 热重建仍 NOT RUN。

- N1 独立 final tester 的适用执行结果为 PASS；N1 精确提交已完成。N2 文档后的最终图 receipt 与 Git 检查仍由 Main 执行，不能仅由 tester PASS 宣称整个 working tree clean。
- N1 completion 已完成候选 87 路径、18 个 disjoint graph batch 的限定范围 reconcile；完整 Transport 的发布 gate 仍未通过，不能把历史 gate 失败改写为当前 N1 tester 失败。
- token-first 双板 records 在五种长度的有界范围通过，但 SDK 回调排空、并发取消/重连、持续负载、DDS discovery、用户数据和 RMW 仍未验证。S3-A/B/C/D2 与 facade/输入核心仅在各自限定范围通过；Descriptor facade 已实现，但 native backend/完整 Transport 仍未实现。
- GitNexus 当前有 N1 candidate closure 的限定范围 reconciliation，最终文档树的 post-edit graph receipt 由 Main 在提交前单独核对；这不构成完整 Transport 的运行验证。
- 不得将历史缺少原始记录的 G1/G6 与本轮 IF/SOCK 测试自动对应。

## N3-1 根构建接线证据（2026-09-15 UTC）

N3-1 已在限定范围完成：既有 N2 `NativeSendExecution.cpp` 通过根 `CMakeLists.txt` 进入所属生产静态目标，根测试复用该目标，未增加 shadow source list；生产 backend 仍保持 fail-closed。最终独立 tester 见 `.artifacts/n3-execution/wiring-final/RESULT.md`：root/source-subproject 10/10，`SOURCE_PROBE` Debug/Release 各 24/24，CONFIG Release 25/25，既有 N1/attachments/binding/facade 9/7/32/4，外部消费者 1/1/2，SDK23 arm64 link/ELF PASS。review 唯一 P2（artifact parent）已修复，并由 clean-parent 10/10 复测。

该切片不是 N3 生命周期完成：真实 DSoftBus provider/callback/线程、Socket 通信、DDS SPDP/SEDP/样本、设备/跨设备、同进程同名 Participant 热重建及 RMW 均未执行。N3 application bundle、pinned receive/receiver lease、copying/admitted drain 和联合 Detach 仍为 PLANNED；本记录只支持根接线的限定范围 IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED，不支持完整 Transport VERIFIED。

## N3 frozen lifecycle model 证据（2026-09-15 UTC）

冻结的 N3 SDK-free 组合模型已在当前工作树实现并通过 review 后最终 tester：根 SOURCE_PROBE Debug/Release 各 26/26；直接组件 Coordinator 13、Process receive 8、N2 17、ApplicationAttachments 9、InputChannels 16、Runtime 30、N1 11（两种配置均通过）；CONFIG facade 21 + Coordinator 13；N1 consumer 9/9、N2 consumer 10/10；SDK23 两个 Coordinator/Process ELF 均为 ELF64 AArch64。最终证据为 `.artifacts/n3-execution/full-final-test/RESULT.md`，review 原报告与闭环分别为 `.artifacts/n3-execution/full-review/REVIEW.md`、`CLOSURE.md`。

已记录的硬契约包括：P0 记录不可改投 P1；native identity constraint 独立于 callback lease；Revoke 在线性化点后禁止新成功准入且排空全部 admitted invocation；最终接收执行取得原 receiver 的真实 use lease；Detach 等待 application copying；协调门只做短状态转换、等待/provider/receiver/cleanup 在锁外；同 local 的 P1 必须等 P0 Detached。测试覆盖事务回滚、代际/路由校验、Binding 有界停驻、copying 与 dispatch drain、发送 admitted drain、receiver 替换、重复 Detach、保留配额及 80 轮同 local 模型重建。

本状态仅支持 N3 frozen host model 的 IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）及 SDK23 LINK/ELF TESTED。补充 manifest `.artifacts/n3-execution/full-final-test/COMPLETE_N3_MANIFEST_RECEIPT.md` 对完整 27 路径做了补充区间的前后哈希一致性检查，并补测 N2/Process/Coordinator 的 Debug/Release 与 SDK23 三个 ELF；新增六个路径没有原始 before-hash，不能追溯宣称其更早时点未变。真实 DSoftBus provider/callback bridge/生产线程、Socket callback-ID reuse/quiescence、板端权限和运行、DDS SPDP/SEDP/样本/可靠性、RMW，以及实际同进程同名 DDS Participant 热重建均 NOT_RUN；模型的 same-local rebuild 不得升级为实际 same-name Participant PASS。production `init()` 在 backend 未接入时继续 `NativeBackendUnavailable`，本项目自身 install/export package 不属于 N3。

初测中 AA/options/binding 矩阵未在 test-only review 修复后重跑；最终 tester 明确保留其前序 PASS 为历史证据，未冒充 post-change 新执行。初始失败缓存和历史失败记录继续保留，不改写为当前最终结果。

## S2-P final evidence

最终 tester 证据见 [.artifacts/verification/20260914T005000Z-permission-lifecycle-final](../.artifacts/verification/20260914T005000Z-permission-lifecycle-final)：Host CTest 10/10 PASS（权限流程 10 groups、快照守卫 158 checks）；官方平台 clang 路径下 OHOS probe 构建/链接 PASS。A/B 均完成同一已归属 native token 的 regrant、服务 GRANTED 验证、独立 Socket 创建并 Shutdown 恰好一次、revoke 后服务 DENIED 与 self 还原。A token `controlled A token reference`、B token `controlled B token reference`；最终项目记录保留为空权限身份，origin/transition JSON guard、外部记录结构及 provider hashes 均 PASS。

既有 `com.kaihong.mdds` allowlist 未被替换；本次仅使用用户授权的新 `fastdds_dsoftbus_probe` 身份，最终保留空权限记录。权限 lifecycle review `.artifacts/review/20260914T004622Z-permission-lifecycle/REVIEW.md` 无 blocking finding。该证据只覆盖本机 Socket 生命周期和权限闭环，不覆盖 Listen/Bind/SendBytes、跨设备记录、SPDP/SEDP、用户数据或 RMW。该历史权限切片不证明完整 Transport 就绪；N1 限定提交门槛另见 N1_COMPLETION.md。


## Windows Host ROS Action acceptance (2026-10-15 05:34:11 UTC)

R12 adds a bounded ordinary ROS Action acceptance executable and oracle. After
independent review repaired feedback-before-SENT acceptance, the final fresh
VS2022 x64 Release /MD build, 61 synthetic model/helper cases and one actual
Windows Host pair `c6a401090f2ecab8` passed: five requests, four accepted, one
rejected, two succeeded, one canceled and one aborted. UUID/result ownership,
actual feedback before targeted cancellation, exact cancel response and two
concurrent executing goals are checked. Both processes exit 0 and ports
50922/50923 allow exclusive rebind. Strict full_host reconciliation covers
A 102/B 112 callbacks and equal charges/refunds, with zero final adapter/capacity
refusals or send errors. Seventeen final actual-log mutants are rejected;
synthetic replays are separate from actual ROS execution.

V1-06 is PARTIAL for this Windows Host scenario only. Startup Reader errors on
both sides remain recorded; OPTIONAL unique-flow fallback is a supported
inference, not direct dynamic parameter/pointer proof. Strict unique-flow
capability remains UNVERIFIED. Initial A's two Closing/InvalidState pre-bridge
refusals retain UNKNOWN payload causality; final zeros do not erase them.
See [ROS Action acceptance](ROS2_ACTION_ACCEPTANCE.md) for exact scenarios,
review/final receipts, artifact hashes, command records and remaining gaps.

The unchanged R11 bridge uses Fast DDS 2.14.5 SECURITY OFF / STATISTICS OFF;
this is no original 2.14.7 SECURITY ON historical qualification. Full Action
QoS/status delivery/replay/result retention/restart/reconnect/cancel-all/time
cancel/natural cancellation race/large-message acceptance remain UNVERIFIED.
Native SDK/core regressions are not newly claimed for these additive files.
DEVICE_TESTED=false; native/cross-device acceptance remains BLOCKED and overall
NOT VERIFIED. The last device permission observation remains DENIED on both
boards at 2026-10-14T21:51:07Z, not refreshed. The 30-row reference remains a
draft, with no approved aggregate or completion percentage. Tester Commit gate
PASS and documentation readiness are scoped; Main graph/pre-commit/publication
and CI receipts remain separate. Earlier sections are preserved verbatim as
historical snapshots.


## Historical Windows Host profile P1 (2026-10-15)

R13 adds the explicit `historical-2147-security-statistics` selector while
preserving `default-2145`. The historical package binds original Fast DDS
2.14.7 SECURITY ON / STATISTICS ON, Fast CDR 2.2.8 compile inputs and a separately
declared original runtime CDR DLL; MSVC x64 Release /MD is required. Before
factory QoS access, the Bridge checks the actual factory module, hook ABI and
security mode. The historical descriptor uses 65535 bytes; the default and SDK
envelope remain 65536. No DDS/RTPS/QoS semantic or native-source change is made.
The package is prefix-bound, not relocatable; its checks do not prove the full
statistics ABI or runtime protection against same-path byte replacement.

Independent review's R13-A1 Windows junction install-guard finding is CLOSED.
The final independent tester reports SCOPE_QUALIFIED_FINAL_HOST_P1_PASS and
Commit gate PASS: fresh default/historical builds and installs, 37 CTests each,
60 parser tests, 16 synthetic module controls, four matcher models, 13 actual
project-entry configurations with synthetic packages, real dependency/header/
installed-package negatives, and a separate 3.6.2.0 core regression (21 groups).
These categories are separate; the core run is not historical ABI evidence.

One actual historical ROS pair (A 54928 / B 38840, both exit 0) established
entity matching/service readiness with zero business samples/requests/callbacks.
Live modules were captured on both sides. Strict full_host reconciliation
passed with A 96 / B 94 ingress callbacks, equal admissions/charges/refunds and
zero final refusals. One actual direct-DDS duplicate-ID failed-construction
probe (68416, exit 0) verified exact failure-frame retention and cleanup with
zero SDK entries. It does not prove Listen or ROS-node-construction failure.
Initial FIX3's two Closing refusals retain unknown payload causality.

Canonical evidence is build/r13-profile/final-test-port-qualified. The older
final-test result remains BLOCKED: Windows excluded its planned ports before
any ROS/DDS children started. Main accepted new ports after an exclusive-bind
diagnostic, changing only ports/output paths; sources, binaries, policy,
timeouts and oracles stayed unchanged. Both runtime scenarios then ran once.
No network permission/settings were changed. Earlier initial FAIL rounds and
fixture preparation errors remain preserved. See
[historical Host profile qualification](ROS2_HISTORICAL_HOST_PROFILE.md) for
configuration, capability matrix, exact command records, hashes and limits.

Implementation PRESENT; build PASS; HOST_TESTED and INTEGRATION_TESTED apply
only to bounded real ROS/DDS over the Host opaque-byte simulated lower layer.
DEVICE_TESTED=false; native/cross-device BLOCKED; overall NOT VERIFIED. Last
DISTRIBUTED_DATASYNC observation remains DENIED on both boards at
2026-10-14T21:51:07Z, not refreshed. Original A/04, DATA_FRAG, historical business
workloads and broad QoS/reconnect acceptance remain NOT_RUN or UNVERIFIED.
SECURITY ON is not a payload-encryption claim. Main graph/PRE_COMMIT,
commit/push and CI remain separate pending receipts at this documentation
snapshot. Earlier sections are retained verbatim; no aggregate percentage is
claimed.


## Windows Host lifecycle acceptance (R15, 2026-10-15)

R15 adds a standalone ordinary ROS lifecycle acceptance consumer and strict
runner/oracle on the existing default-2145 SECURITY OFF / STATISTICS OFF Host
package. Two OS processes exercise real ChangeState/GetState/
GetAvailableTransitions services and TransitionEvent delivery, including callback
FAILURE then SUCCESS, configure/activate/deactivate/cleanup, ERROR recovery
success/failure, inactive shutdown and two finalized nodes. The lower layer is
the TCP-backed Host fake DSoftBus Socket seam; no Transport/DDS/RTPS/RMW/native
implementation or policy is changed.

Independent review's P2 R15-C1 is CLOSED: START now requires a complete current
module snapshot and both children still live (poll None), rather than historical
module accumulation. This sequential observation is not an atomic liveness or
tamper guarantee. Final fresh MSVC x64 Release /MD C++17 build, nine model
methods (including eleven synthetic barrier controls), sixty related parser
cases and one final real Host window passed. Canonical receipt is
build/r15-lifecycle/final-test/FINAL_TEST_RESULT.json; tester Commit gate PASS
is scoped to Host acceptance tooling.

Final run 1fc377841d9c9a77: A 77236 / B 73444 exit 0, ten ChangeState responses,
twelve state and twelve available-transition snapshots, twenty transition
events, eleven callbacks and two finalized nodes. Current START module snapshots
contain A twelve / B ten selected exact module bindings, both polls None.
No forced kill or timeout; ports 61361/61362 allow exclusive rebind. A admits,
charges and refunds all 148 ingress callbacks. B has 194 ingress, 177 admitted/
charged/refunded and seventeen Adapter Closing/InvalidState pre-Bridge refusals.
A retains two WSA10004 receive terminations and B two EOFs; both have zero send
errors. Complete full_host accounting does not mean zero refusals/errors or
known payload causality, which remains NOT_OBSERVED.

The initial Host PASS and CRLF/LF provenance-seal failure/resolution remain
preserved, along with all review/final evidence. See
[ROS lifecycle Host acceptance](ROS2_LIFECYCLE_HOST_ACCEPTANCE.md) for exact
scenario, commands, module/artifact hashes, test categories and capability
matrix. Managed publisher gating, thrown-exception transitions, all shutdown
origins, concurrent transitions, restart/reconnect, security-enabled lifecycle
and native/cross-device lifecycle remain UNVERIFIED or BLOCKED.

R14's strict 20 writes / 19 takes FAIL and ten opaque frames per side FAIL remain
unchanged. Independently reviewed metadata research can associate callbacks,
accounting and visible publishers, not encrypted inner endpoint/sample contents
or the missing-take cause. A cross-security-layer observer would require a
separately approved architecture exception and has not been implemented.
DEVICE_TESTED=false; last known owner permission blocker remains DENIED on both
boards at 2026-10-14T21:51:07Z, without a new check. ROS over native DSoftBus is
NOT VERIFIED. Main graph/PRE_COMMIT, commit/push and CI are pending separate
receipts at this snapshot. No aggregate completion percentage is claimed.


## Managed lifecycle publisher Host acceptance (R16, 2026-10-15)

R16 adds a standalone managed-publisher consumer and analyzer and extends the
existing runner with a fixed managed-publisher scenario. The default R15 oracle
and consumer remain unchanged and received full regression. Both use the existing
default-2145 SECURITY OFF / STATISTICS OFF Windows Host fake Socket seam; no
Transport/DDS/RTPS/RMW/security-policy or dependency source is changed.

Independent review reports NO_ACTIONABLE_FINDINGS_FOUND. Final independent tests
passed two fresh MSVC x64 Release /MD C++17 builds, seventeen model methods
(including twenty-four barrier subcontrols), sixty related parser cases and one
actual final window per scenario. R15 regression 4175433b256a6647 passed its
10 changes / 12 states / 12 available snapshots / 20 events / 11 callbacks.
R16 run 8925f9c43e172686 (A 39624 / B 74132) passed six changes, seven states,
four probes, eight publish attempts, four deliveries, twelve events and six
callbacks. Both publish overloads (const reference and moved unique pointer)
were called in inactive and active phases. Actual QoS is reliable, volatile,
KeepLast(10); stable publisher/received GID is
010f6944c89ac1190000000000001303, implementation rmw_fastrtps_cpp.

The four specified inactive attempts were not observed in remote subscriber
callbacks during the matched finite window. This is not indefinite non-delivery,
all-cache absence or an RTPS sequence fence. Both windows exited 0 without
forced kill/timeout and all ports allowed exclusive rebind. Strict full_host
reconciliation passed while retaining real pre-Bridge Closing/InvalidState
adapter refusals: R15 A 149/149/0 and B 203/185/18; R16 A 92/78/14 and B
134/134/0 (ingress/admitted/rejected). Callback identity and entered/returned
records remain intact; payload causality is NOT_OBSERVED. Each process retains
two DDS Error lines. Closing I/O records include R15 A two WSA10004 / B two EOF,
and R16 A two EOF / B one WSA10004 plus one EOF. PASS is not zero-error proof.

See [managed lifecycle publisher Host acceptance](ROS2_LIFECYCLE_PUBLISHER_HOST_ACCEPTANCE.md)
for the capability matrix, exact commands/module and artifact hashes, canonical
final 106-entry seal plus separate recheck receipt, and preserved initial/review/
R15 publication evidence. Tester Commit gate PASS is limited to this Host
acceptance-tooling stage. Main graph/PRE_COMMIT, commit/push and CI remain
separate pending receipts at this snapshot.

Main's read-only hdc list targets -v at 2026-10-15T12:24:23.860820Z exited 0 with
empty stdout/stderr and no observed target entries; this is not a physical
connection diagnosis. No board or permission operation occurred. The prior
2026-10-14T21:51:07Z DENIED permission observation was not rechecked.
DEVICE_TESTED=false; native/cross-device BLOCKED; ROS 2 over native DSoftBus
remains NOT VERIFIED. Security-enabled publication, loaned/serialized/generic
APIs, concurrent transition/publication, restart/reconnect remain UNVERIFIED.
R14 strict 20 writes / 19 takes and ten opaque frames per side remain FAIL.
No aggregate completion percentage is claimed.


## Typed serialized publication Host acceptance (R17 FIX1, 2026-10-15)

R17 adds a standalone typed serialized consumer/oracle and fixed serialized
scenario in the shared runner. The existing default-2145 SECURITY OFF /
STATISTICS OFF Host package is unchanged. Two sequential request/reply exchanges
use the SerializedMessage wrapper then raw rcl_serialized_message_t publish
overload in both directions, with 17/4096-byte bodies and exact full application
payload checks. Each process has one application publisher/subscription, plus
ROS auxiliary endpoints. The TCP-backed Host fake Socket seam is not native
OpenHarmony DSoftBus; no production Transport/DDS/RTPS/RMW change is present.

The original R17 run c5d261aa51080285 remains FAIL: both children exited 4,
A serialized_bounds and B bounded_wait_expired, zero serialization/attempt/
return/receive business records. Numeric bounds and the exact failed conjunct
were NOT_CAPTURED; later values cannot be backfilled. FIX1 follows the pinned
rmw_fastrtps_cpp 8.4.3 source assigning both exposed length/capacity fields to
data_length. Before remains 8192; after must equal length with full application
payload bytes < length <=8192. This is an implementation-specific field contract,
not physical heap shrinking or a general ROS capacity promise.

Independent FIX1 initial testing passed; agentic review found no actionable
findings. Final independent testing passed three fresh MSVC x64 Release /MD
C++17 builds, twenty-five model methods (including 36 barrier subcontrols),
sixty parser cases and one final actual window per R15/R16/R17 scenario.
R17 final f9333cf6b0dc4fd7 (A 24196 / B 53532) passed two exchanges and four each
serializations/attempts/returns/typed receives. New numeric diagnostics measured
A 70/4149 and B 68/4147 after-capacity=length, before 8192. Both exited 0, no kill or
timeout, ports 65508/65510 allow exclusive rebind; current modules are 10/10 with
both polls None. R15/R16 regressions also passed all original business oracles.

Strict full_host/counter reconciliation retains real shutdown observations:
R15 B has 5 adapter Closing/InvalidState pre-Bridge rejections; R16 A has 14;
R17 has 0. Final R17 A 52 / B 53 ingress are all admitted/charged/refunded. Each final
process retains two DDS Error lines; EOF/WSA10004 remain recorded. Complete
accounting and business PASS do not imply zero errors, payload causality, or an
explanation for earlier failures. Payload causality remains NOT_OBSERVED.

See [typed serialized Host acceptance](ROS2_SERIALIZED_HOST_ACCEPTANCE.md) for
exact commands/environment, artifact/module hashes, initial FAIL/FIX1/final
receipts and capability matrix. Generic dynamic-loader prerequisites were
identified in planning, not as an observed runtime failure. Loan capability was
not measured; DataSharing OFF does not prove all publisher loans unsupported.
Only the inspected exact-version GenericSubscription handle_loaned_message is
not implemented; generic serialized delivery is a distinct unverified path.
Lifecycle-serialized APIs, arbitrary sizes/fragmentation, zero-copy, security,
concurrency and restart/reconnect remain UNVERIFIED.

Main's read-only hdc list targets -v at 14:12 UTC exited 0 with empty output and
no target entries, not a physical-disconnection diagnosis. No board/permission
operation occurred; the last DENIED permission evidence is still 2026-10-14 and was
not rechecked. DEVICE_TESTED=false; native/cross-device BLOCKED; overall ROS2
over native DSoftBus NOT VERIFIED. R14's 20 writes/19 takes and ten opaque frames
per side remain FAIL. Tester Commit gate PASS is Host-scoped; Main graph/
PRE_COMMIT, commit/push and CI remain pending separate receipts at this snapshot.
No aggregate completion percentage is claimed.


## R18 Action acceptance with application readiness precondition (2026-10-15)

R18's post-review final result is PASS_WITH_APPLICATION_READINESS_PRECONDITION.
The example CMake supports the selected Windows Host/OHOS Target packages; its
application-only Trigger handshake binds PAIR and TAIL to run/accepted UUIDs.
This controls acceptance scheduling, not production Action/Transport semantics.
Ordinary ROS-legal early-feedback risk remains. The original no-barrier run
eb7af94cc32edb89 stays FAIL (both exit 4, bounded_wait_expired); cause NOT_PROVEN.
The separate mechanism run stays PASS_MECHANISM_ONLY, not proof of that cause.

Readiness initial FIX1 passed fresh native/Host builds, 70 model methods and 17
synthetic controls. Original configure/map-path/classifier failures are retained.
Formal review found no blocking findings and required no source change. Final
validation reused hash-bound artifacts with zero new configure/build, reran 70
models plus 17 synthetic controls, then executed one new readiness window:
fe87b82ecd794c7e, A54800/B79508 exit 0; five requests, four accepted, one rejected,
two succeeded, one canceled, one aborted, two readiness requests/grants/replies.
Canonical 12 and independent 13 runtime modules per process were cumulatively
observed, not checked by a pre-START module barrier. No kill/timeout; ports
55602/55603 rebound simultaneously. A sends132/ingress121 and B sends121/ingress132,
all ingress admitted/charged/refunded, zero adapter rejections in this final
window. A two EOF and B two WSA10004 I/O terminations remain recorded. Complete
accounting does not establish all-frame payload causality (NOT_OBSERVED).

See [Action acceptance and R18 capability matrix](ROS2_ACTION_ACCEPTANCE.md#r18-current-evidence-action-acceptance-with-application-readiness-precondition)
for exact command/environment receipts, artifact/module hashes, preserved history
and limits. Canonical final result SHA256:
f3801cea755215356ac97d9120c49e96e12ee29b017903633e07debafc9ad9a8;
98-file manifest SHA256:
5a5c30d905249c5ea5a61335eafcce6d13b3fe3c5ec0344601af721959b3a24a.

Native dependency/bridge/consumer builds and static inspection pass; native
loader/runtime, RK3588A Action and cross-device Action were not executed.
Host28.1.16/native28.1.22 relevant source correspondence is not native proof.
The temporary project permission window is conditionally authorized, but a
complete project-only original record and lossless restoration route remain
unavailable. No grant/revoke/token switch/reload/security write occurred. The
R18 read-only board/platform observations do not establish permission or Action
qualification. Historical V1 real cross-board pub/sub/service evidence remains
separate and valid for its own artifacts; it does not qualify this change.

R19's three standalone vendor libraries have BUILDS/static PASS and scoped
independent review without blocking findings; R19-V1 remains OPEN. Wrappers,
plugin, consumer, runtime record/replay and device are unverified. R14 strict
20/19 and opaque-frame failures remain unchanged. DEVICE_TESTED=false for R18;
native/cross-device BLOCKED; overall ROS 2 over native DSoftBus NOT VERIFIED.
Documentation may be ready as a truthful record, but R18 normal Ready for commit
is NO: required device/cross-device and applicable Main gates remain unmet.
Main graph/PRE_COMMIT, commit/push and CI receipts are pending, not preclaimed.


## R18_SOURCE_BUILD_HOST_STAGE scoped publication readiness (2026-10-15)

A new prospective Main scope decision and independent technical/policy review
retain the exact six-file candidate as OHOS Action consumer build enablement
plus Windows readiness-constrained Action acceptance. No source split or
production Transport/RMW/identity change occurred. Existing native startup is
newly reachable from the example; absence of native runtime impact is not claimed.

Independent tester Commit gate is PASS_FOR_EXACT_SOURCE_BUILD_HOST_STAGE:
31,000 unique paths rechecked with zero drift; the recorded native build/static
artifacts and canonical post-review Windows evidence remain bound. The scope
assessment reused 70 model methods, 17 synthetic controls and run
fe87b82ecd794c7e; new model/build/business executions are zero. This is not a
second runtime success. Tester result SHA256:
4ffa2d84f7a45006ee18df33a1cfee0be1915ab13f30049b4b36003ae4011f7a;
20-entry manifest SHA256:
fca510e0f2dfe57c795f42a2157f7cc6ac9190b781cd09b1ac9451afbe3547b7.

Scoped documentation Ready for commit: YES only for R18_SOURCE_BUILD_HOST_STAGE.
Main graph, staged-identity and applicable PRE_COMMIT remain separate pending
gates; no commit/push/CI success is claimed. This supplements the preceding
broader NO/BLOCKED snapshot without rewriting it. See the appended
[Action scope matrix](ROS2_ACTION_ACCEPTANCE.md#r18-bounded-sourcebuildhost-stage-prospective-publication-qualification)
and separate publication-scope-docs receipt.

Native loader/symbol resolution, missing-identity rejection, identity selection/
restoration, actual DSoftBus Socket/Action and cross-board discovery/readiness/
lifecycle remain RequiredForNativeQualification: NOT_RUN/BLOCKED, not N/A.
The 17:55 loader diagnostic stopped after one HDC discovery with exit 0 and empty
output: zero remote commands/deployment/runtime. This is not proof of a physical
board disconnection. Complete project-only permission-record recovery remains
unestablished; no security write occurred. DEVICE_TESTED=false; overall native
ROS 2 qualification remains NOT VERIFIED. Original no-barrier FAIL/cause
NOT_PROVEN, PASS_MECHANISM_ONLY and R14 failures remain unchanged.


## 2026-10-15 current bounded update: R18 published, loader diagnostic and R19 preparation

This update supplements the earlier dated snapshots. R18_SOURCE_BUILD_HOST_STAGE
was committed and pushed as `48c01f2597bf16db1ed35fbd79cc5c32826f0ab8` to existing
origin branch `codex/flow-encoding-published-20261014`. Main PRE_COMMIT passed
45 checks; graph HIGH risk was retained. At 18:27:04 UTC the SHA had zero GitHub
workflow runs/check runs/status contexts: **CI NOT_RUN**, not PASS. No later CI
state is inferred. Local receipts: `build/r18-action-native/main-publication/`.

Each RK3588A subsequently ran the native main once and returned expected exit 3
with exact missing-identity stderr (HDC exit 0). Both passed 104 before/after
artifact hashes, quiescence, 106 exact-file cleanup and absence checks. This is
**DEVICE_TESTED PASS only for native main load and missing-identity rejection**;
it is not Action, target-bridge loading, accepted identity, Socket or cross-board
communication evidence. The independent recovered-evidence review accepted that
limited claim and required no repeat window. See the
[Action update and current capability matrix](ROS2_ACTION_ACCEPTANCE.md#2026-10-15-update-publication-and-bounded-native-loader-evidence).

The original empty HDC discovery remains recorded. Later normal-environment HDC
discovery recovered both boards without establishing its unique historical cause.
The sentinel fixture failure, corrected full-line parser and unexecuted
pre-impact draft deviation are preserved in the recovered diagnostic evidence.

R19's yaml-cpp, console_bridge and SQLite standalone target libraries have
**BUILDS / static PASS** after independent review and final recheck. Wrapper
canonical final is **PASS_SCOPED_CONFIGURATION_CONTRACT_FINAL**: three positive
and sixteen negative configuration receipts were independently replayed, 4799
current bindings passed, with zero new configure/build/install/target/device
execution in that final check. **R19-V1 is CLOSED only for the frozen four-prefix
and actual imported-target identity contract**. The 99-file aggregate is a bound
input for later work; vendor opt/loader hooks, full relocatability, setup-only
runtime and rosbag read/write are not established.

R19-N1 is **CLOSED_FOR_DESIGN** after the next-dependency FIX1 review. Actual
TinyXML2 alias/class_loader/pluginlib implementation preparation is in progress;
its compilation, probes and runtime are not yet qualified. The next planned
acceptance is fixed `std_msgs/msg/String` typed Writer/Reader with SQLite and no
compression. See [R19 target preparation and capability matrix](ROS2_ROSBAG_TARGET_PREPARATION.md)
for exact boundaries and evidence hashes. Local ignored build evidence is not
included in the remote repository by these documentation links.

Actual native Action/Socket/cross-board tests remain **NOT_RUN/BLOCKED**. Temporary
permission authorization remains conditional on a complete current project-only
original record and a supported lossless restoration route; neither is available.
No grant/revoke/token switch/reload/security write occurred. Current permission
state was NOT_QUERIED. Original unbarriered Action FAIL/cause NOT_PROVEN,
mechanism-only PASS and R14 failures remain unchanged. Overall ROS 2 over native
DSoftBus remains **NOT VERIFIED**; this documentation sync is not a product gate.
