# R30 finite coordinated Action server restart acceptance

Snapshot:2026-10-18 19:28 UTC. **IMPLEMENTED / HOST_TESTED / DEVICE_TESTED /
CROSS_DEVICE_TESTED PASS**, only one explicit normal server replacement under
three permission controls. Independent technical tester gate PASS; documentation
Ready for commit YES. Full ROS2 NOT_VERIFIED; R22 STOPPED.

R30 pending gate/publication/CI statements describe the 2026-10-18 19:28 UTC pre-commit documentation snapshot. Actual commit, remote and CI outcomes are recorded separately in root-relative `build/r30-action-restart/main-action-restart-publication/POST_PUBLISH.json`; no future SHA or CI success is prefilled.

## Actual implementation and device result

Four new files: target action_restart_probe.py and tests/ros2_target/
{run_action_restart,action_restart_oracle,test_action_restart}.py. Existing
native single-reset and ownership rules are reused. No change to rclpy client,
RMW, FastDDS DDS/RTPS, native safety limits, network permissions or default
automatic reconnect. Native API23 ON/OFF binaries and897 compiler dependencies
are REUSED_HASH_VERIFIED, not newly compiled in R30; package3,236 files and
current270-source inventory are bound to the canonical final receipt.

One real scene `d9d3534643659c2e`, execution `f87237d1b67505f3`, ran on RK3588A
A `3e01ff55454d202020104033bf453b00`, B `3e01ff55454d202020104433991c3b00`.
The same A client/Node/Runtime/Participant and26 endpoints, including exact8
Action endpoints, persisted through B0 normal exit and fresh B1 run/root/PID/
starttime/Participant. B0 token restoration and both PID absences precede B1
launch. Real public rclpy Action APIs and selected native DSoftBus path carry
distinct Fibonacci goal UUIDs and business payloads.

| Goal | Actual outcome |
| --- | --- |
| PRE |5 peer-equal feedback CDR records and SUCCEEDED result. |
| OLD | Accepted, then executing with1 feedback; separate GetResult Future remains pending, no observed result; final local Future cancellation only. |
| NEW |7 peer-equal feedback CDR records and SUCCEEDED result from fresh B1. |

OLD is not resumed/recovered or remotely cancelled. New-goal success must not be
called old-goal recovery. All3 interpreters/children exited0, identities restored,
6 owned PIDs absent, no TERM/KILL or identity mutations. Native listener/retired
owner/new-owner and provider-map evidence passes; SDK epoch/drain and a direct
application-payload-to-RTPS sequence bijection are not proven.

Ten selected module origins, target ABI and protocol capsules were checked.
The three native serialize/deserialize witnesses cover **application Goal,
Feedback and Result**, not protocol-wrapper CDR. Capsule presence does not
establish remote CancelGoal execution or cancel-service behavior.

## OPEN compatibility: COMPAT_RCLPY_STATUS_BEFORE_GOAL_RESPONSE

**OPEN / UNCOORDINATED_NOT_FIXED.** The selected local rclpy revision is
`4e607c09d624debc39a4ae5405680c06d87fac35`, describe
`7.1.12-1-g4e607c0`, recorded origin `https://github.com/ros2/rclpy.git`.
This is selected-local-source provenance, not a latest-upstream assertion.
Selected package/checkout action/client.py SHA is
`72b3ebaa3b010349f13996239efd269dcfa6afefd957fc461e851578dc112eaa`.
Git HEAD blob SHA is
`559457ec784f3d186607e37ec37dcbea46b27fce40ced9ae094ac9c0e3400a02`:
raw bytes differ only by608 CRLF/LF conversions; normalized full bytes and AST
including locations match. No project ActionClient patch introduced this behavior.

At client.py:294-309 the selected client registers a goal handle after the
SendGoal response. At343-360 it ignores status for an unregistered UUID. The
preserved actual-selected-client host counterexample delivers EXECUTING before
the response and still obtains public status UNKNOWN(0); reverse order gives2.
It is a compatibility limitation under legal cross-channel ordering, not an
observed Transport fault or a claimed device-frequency measurement.

R30 uses START_OLD only after A's accepted handle and separate GetResult Future
are captured. B0 holds OLD accepted without execution/feedback until that exact
identity-bound permission arrives, then executes once and emits one feedback.
This constructs the test prerequisite; it does not repair uncoordinated status
loss. COMPLETE_PRE and COMPLETE_NEW wait for expected captured feedback before
permitting completion. Feedback/result ordering across independent channels is
legal; the selected client removes its feedback callback after result completion,
so simply waiting longer cannot recover feedback already discarded. The COMPLETE
barriers likewise construct test conditions, not a general ordering solution.

All three files carry permission/identity only, never Fibonacci payload or
result data, but they do coordinate business ordering. Their exact writes were
independently checked against preceding actual peer logs. Natural cross-channel
order, uncoordinated generic Action behavior and default reconnect are not proven.

## Host verification, repairs and retained failures

Initial T01 FAIL: A's feedback/result could be captured before B's post-publish
log append, so the runner invoked the strict paired oracle too early. Fixed
readiness waits for both required captured peer logs within unchanged deadlines;
extra/invalid records still fail the strict oracle. Independent first review
then found R01 early OLD status loss using the selected public client. START_OLD
closed only this experiment's precondition defect; the compatibility item above
remains OPEN. Both old FAIL records and mock/fixture failures remain sealed.

Repair review found no blocking issue in the four-file coordinated repair.
Final host freshly passed R30 17/R29 16/R27 15/R28 12 repository methods,2 T01
capture-lag replays,61 type-support cases,19 communicate cases,3 selected client/
Future and3 selected server cases,2 coordinated R01 orders plus the old uncoordinated
counterexample,12 START_OLD cases,9 runner methods,36 workflow boundaries,
59 strict oracle/control cases,16 reviewer controls,14 package/plan controls,
3 constructor cases and host-ABI rejection before Node creation. Coverage overlaps;
these counts are host/API-shaped/synthetic evidence, not distinct native features.
The real target ABI/import/CDR/Action execution was qualified separately by the scene.

Original postquery local conversion command0 failed with Path.resolve WinError5,
exit1, and remains FAIL. After independent review the same four exact local argv
ran under normal-user access, all exit0 using original query UTC
2026-10-18T19:13:47.863108Z. No repeat query, refreshed anchor or extended deadline.
MAIN_CONTINUATION_OUTCOME is a retrospective tool-session report, not a start time.

Independent device audit replayed actual content/native oracles,17 repository
tests and10 corrupted-copy negatives. Its first negative fixture incorrectly
added unused modules=[] instead of corrupting origins and failed; original
script/exit1/stderr remain. Corrected separate controls reject real origin SHA,
path and module-set corruption without changing production acceptance.

## Commands, timing and lifecycle limits

Actual Windows Python/SDK23 HDC scene command from ROOT cwd (host exit0):

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r15-lifecycle\\publish-worktree\\tests\\ros2_target\\run_action_restart.py",
  "run",
  "--prepared",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r30-action-restart\\prepared-f87237d1b67505f3\\PLAN.json",
  "--release",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r30-action-restart\\MAIN_SCENE_R30_QUERY1_RELEASE.json",
  "--hdc",
  "C:\\Users\\17715\\AppData\\Local\\OpenHarmony\\Sdk\\23\\toolchains\\hdc.exe",
  "--output",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r30-action-restart\\execution-f87237d1b67505f3"
]
```

All40 query and374 scene commands checked;350 scene shell markers remote0 and
24 transfer commands host0 without inventing shell exits. Business14.797/35 s,
normal close22.750/50, final exit1.125/20, analysis4.516/60; host control/capture
is included, so these are not network-latency benchmarks. Original query240/300
bounds and per-process40 remain; NODE_READY follows create_node but its exact
return timestamp is not separately exported. No timing bound was relaxed.

Scene collection follows confirmed absence, prior stat size, successful receive
and local size, then remote post-transfer SHA matching local bytes. No scene
pre-transfer SHA was collected; the query has a separate pre/post hash contract.
All2,163 original captured files plus4 failed-postquery files remain intact.
Exact command argv/cwd/UTC/exit/raw streams are in linked audit/stage records.

## Remaining scope and next step

This is partial Action restart support under the stated explicit coordination.
OLD recovery/remote cancellation, generic uncoordinated Action ordering, broad
QoS/concurrent load, long-duration/soak/leak freedom, SDK epoch/drain and default
automatic reconnect remain unverified. One known unique-flow diagnostic pair per
process is retained: unique-flow NOT_PROVIDED, strict UNTESTED; dedicated SEDP
NOT_QUALIFIED. Earlier native C++ cancel/reject/abort/concurrency and CLI Action
results retain their original scopes, not R30 cancellation qualification.

R29 is published as `b5b8fbcfa7ab1de7dd6cb24297967e1181b8372b`, exact-SHA CI
NOT_RUN at18:10:45.662387 UTC. Next: Main publication gate, then separately review
the open compatibility gap or another bounded requirement without silently
patching the selected library or widening the experiment. Full ROS2 NOT_VERIFIED.

## Sealed local evidence

Ignored local build links are not promised as remote downloads. Consumed
release/route files are records, not reusable dispatch authorization.

| Local evidence | SHA-256 |
| --- | --- |
| [build/r30-action-restart/device1-final-audit/RESULT.json](../../../../build/r30-action-restart/device1-final-audit/RESULT.json) | `c3e725e6ca3de342fbeea1fced2dd55c2a46dd50ff6826d097e716152fa81202` |
| [build/r30-action-restart/device1-final-audit/SEAL.json](../../../../build/r30-action-restart/device1-final-audit/SEAL.json) | `60ccea1ef8984476ee46fa17dfc10b3f87dbc361eb0c97b142bf17e2d92f2b27` |
| [build/r30-action-restart/device1-final-audit/APPLICATION_CDR.json](../../../../build/r30-action-restart/device1-final-audit/APPLICATION_CDR.json) | `287070cf8c90545a9d4d26f3f8fc8496ca8a1656fa9d08a1e52f455473ad5dcb` |
| [build/r30-action-restart/device1-final-audit/COORDINATION_AND_NEGATIVES.json](../../../../build/r30-action-restart/device1-final-audit/COORDINATION_AND_NEGATIVES.json) | `c76b07f4a2a56ae7c2818efbf8e7a78c5cd8e327cf3fe536da6f3d1867e458f2` |
| [build/r30-action-restart/device1-final-audit/QUERY_AND_HOST.json](../../../../build/r30-action-restart/device1-final-audit/QUERY_AND_HOST.json) | `3d1bed57ace8f27f431874718d8c3e39ab58766dd66cacccb93d861f7219f7aa` |
| [build/r30-action-restart/device1-final-audit/TIMING.json](../../../../build/r30-action-restart/device1-final-audit/TIMING.json) | `cef1ed4af19ca6addf7f3658d8a0f632963df0841fc966f8b1ff838f0bae5349` |
| [build/r30-action-restart/device1-final-audit/HELPER_FAILURE.json](../../../../build/r30-action-restart/device1-final-audit/HELPER_FAILURE.json) | `22905bdc0a957ee95a4184b62f83ceb99f6cbd86c0074fe2545ff389cc2fa3f4` |
| [build/r30-action-restart/initial-tester/RESULT.json](../../../../build/r30-action-restart/initial-tester/RESULT.json) | `1bc8d8419fe67c8df55eeacc7681eb389205546134505de6bf799c17cb0e3306` |
| [build/r30-action-restart/implementation-review/RESULT.json](../../../../build/r30-action-restart/implementation-review/RESULT.json) | `d46c99cfcb706805ec4228f5bfca5d822ef65eaef4d2ca865bbc7c7257d4d0f0` |
| [build/r30-action-restart/initial-fix2-tester/RESULT.json](../../../../build/r30-action-restart/initial-fix2-tester/RESULT.json) | `186ba31d1e2f76ad87b96cb868899cffe16b4ff08b6b5b6498adb7fff1a0e351` |
| [build/r30-action-restart/repair-review/RESULT.json](../../../../build/r30-action-restart/repair-review/RESULT.json) | `849107d9228c1f4496959f03f54ddf2a373788534e13b1f9215456ce3e7b7b3d` |
| [build/r30-action-restart/final-tester/RESULT.json](../../../../build/r30-action-restart/final-tester/RESULT.json) | `6a6710ed313c06c01ac93fec87f92747baf060109a50bccac62a945276206a6f` |
| [build/r30-action-restart/final-tester/SEAL.json](../../../../build/r30-action-restart/final-tester/SEAL.json) | `3c97f4de8c08eedd99bad0d9faa859a29c92f34583fe34c9836d506f5f1cb6f4` |
| [build/r30-action-restart/compatibility-status-gap/RESULT.json](../../../../build/r30-action-restart/compatibility-status-gap/RESULT.json) | `79482991c8daf353ca25d3e73b1bcd0db3a81f48443bd5cc39e24e39cbc9e6e0` |
| [build/r30-action-restart/compatibility-status-gap/NORMALIZATION.json](../../../../build/r30-action-restart/compatibility-status-gap/NORMALIZATION.json) | `c9c3fc112cbd1433bc510b4d26bfa2ecab4b8304ec27f171653743b3201a04e0` |
| [build/r30-action-restart/compatibility-status-gap/SEAL.json](../../../../build/r30-action-restart/compatibility-status-gap/SEAL.json) | `77259ae57cac03332aaecc1a11719a7913710fa03310b18f62e03e02c2823ff8` |
| [build/r30-action-restart/implementation-review/STATUS_RACE.json](../../../../build/r30-action-restart/implementation-review/STATUS_RACE.json) | `2dfb0109630fb3b43cb2573bd2b6567aa0aed1b9041f868b0debce4479e5aebf` |
| [build/r30-action-restart/local-resume-review/RESULT.json](../../../../build/r30-action-restart/local-resume-review/RESULT.json) | `b7813252782f5a68e89f6f0d99973a0f4e623d6f09ee202ee9d34d1cbae5f57d` |
| [build/r30-action-restart/execution-f87237d1b67505f3/RESULT.json](../../../../build/r30-action-restart/execution-f87237d1b67505f3/RESULT.json) | `0ccee0106bdf7af6bb8e3890a7145d22bb1ea051d2eb3b817544ee5798fb3506` |
| [build/r30-action-restart/package-1eee6bd720e559ee/DERIVATION.json](../../../../build/r30-action-restart/package-1eee6bd720e559ee/DERIVATION.json) | `ac22627dd7c1597c2e3d52c35e6b85506be83ed2188bd8f86dcc48de201833ea` |
| [build/r30-action-restart/main-action-restart-publication/IMPLEMENTATION_FILES.json](../../../../build/r30-action-restart/main-action-restart-publication/IMPLEMENTATION_FILES.json) | `64f204912b4326c71759e230ce28ea3ed73618979186f8f8d0965dd317f13f47` |
| [build/r29-service-restart/main-service-restart-publication/POST_PUBLISH.json](../../../../build/r29-service-restart/main-service-restart-publication/POST_PUBLISH.json) | `edbcbfc3c5e3b135e3f6138b08a685c04a75c05e31a06fa59940088eebdc4dca` |
