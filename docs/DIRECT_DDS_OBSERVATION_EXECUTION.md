# Stock DDS dual-board observation: historical preparation and current status

Current status (2026-11-02): the one authorized actual attempt ran HDC and
predeployment queries, then failed at `readonly.2` with
`REMOTE_BASE_RC_MISSING_OR_NONZERO`. Deployment, Native identity/probe and DDS
traffic were NOT_RUN; the once opportunity is consumed. The resource-receipt
compatibility repair is now PRESENT and HOST_TESTED (56 PASS, zero skips), with
zero open blocking review findings; DEVICE_TESTED=false and Stage1 NOT_VERIFIED.
See [the current compatibility and evidence note](HDC_RESOURCE_RECEIPT_COMPATIBILITY.md).

The remaining sections preserve the historical preparation specification and
its offline test snapshots. Their statements that actual HDC/queries were
NOT_RUN, controls/run were absent, or freeze/first execution was still next apply
to those preparation snapshots, not the current run state. They do not authorize
a retry or supersede the actual FAIL. The frozen private card, active package,
consumed controls and original once record remain unchanged.

## Fixed inputs and objective

Scope: `ONE_NEW_CURRENT_STOCK_DDS_AB_NARROW_OBSERVATION_240S_D01`.
Nonce: `4F81C2A9_DIRECT_DDS_240S_D01`.

Windows user 17715; existing SDK23 official HDC at
`C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe`.
Fixed A/B identifiers are retained only in the private frozen input/command
binding and private execution card; this public document does not publish them.
New device directory on each board:
`/data/local/tmp/mdds_direct_20261103_4f81c2a9`.
New host run: `E:/Codex-FastDDS-directdds-once-20261103_4f81c2a9`.

Reuse the 11 hash-bound Native files, the stock FastDDS 2.14.5 / FastCDR 2.2.5
dependencies and Windows coordinator from the prior reasons package. Unchanged
binaries are referenced and rehashed, not rebuilt or copied into a new backup.
The exact source paths, sizes and SHA256 values are in SOURCE_BINDING_D01.json;
the complete fixed argv registry is COMMANDS_D01.json.

Both boards receive: own_goal_runner, own_goal_launcher, goal_supervisor,
readonly_frontend, and private lib/ copies of libfastdds_dsoftbus_stock_214.so,
libfastrtps.so.2.14, libfoonathan_memory-0.7.4.so, libtinyxml2.so.6,
libfastcdr.so.2, libc++_shared.so, libstage1_actionbag_identity.so.
Only the four newly uploaded programs get mode0700. No system/old project files
are overwritten; test files are retained. No new package, account or profile.

The fixed procedure checks providers/resources, stages and verifies all files,
queries the existing project Native identity, then runs the per-board identity
inheritance/restore probe before traffic. Existing identity:
the exact existing Native name is privately bound, package com.kaihong.mdds.
The two traffic consumers use standard DDS Participant/Topic/Writer/Reader,
each send and receive exactly3 run/side/ordinal-bound payloads. Native event
records and the controller coordinate four releases, DATA_DONE and deletion.
Only the fixed current inputs/receipts may prove delivery/restore/exit.

## Finite actions and resource limits

At most1 self-created foreground HDC server,121 fixed HDC client steps
(115 normal plus6 registered but currently unreachable cancellation-publication
steps), and1 non-HDC
Windows controller:123 possible roots; no automatic command retry. At most3
clients concurrently, with the server and coordinator at most5 live roots.
The Native launcher can attempt identity selection/restore at most8 times
across both probe and traffic windows combined. This activates the existing
identity's full permissions, not a newly reduced permission set.

Total240seconds; work stops at236seconds, final4seconds reserved for cleanup.
After wait_accepted('server') returns with the owned listener accepted, record
the local driver clock and passively observe until at least10seconds after that
timestamp, then begin the one original targets/provider/DDS sequence. The origin
is after wait_accepted's existing short pause, not the underlying listener's
creation timestamp. Each iteration uses the existing pump, notice, resource,
recording and deadline checks and pauses at most0.05seconds. Hard-stop exceptions
propagate; synchronous work can overrun the nominal interval, so completion is
not an exact wall-clock guarantee. This time consumes the existing240seconds
(236seconds work); it resets no deadline and adds no process, query, device-state
loop or retry. It is an experimental query time, not a readiness/handshake
guarantee. This avoids repeating only the previously observed sub-600ms query
window; later success/failure cannot establish cold-start causality or continuous
state over the interval.
Ordinary client15seconds, long launch125seconds, coordinator110seconds;
per original owned handle at most1 stop attempt followed by at most2seconds
wait. Failure/unknown stops new business; no refund/reset of once. Only proved
own original handles may be stopped; no name/tree/other-instance termination.
If an existing HDC or8710 listener is present, stop before creating any process
and request any necessary specific handling separately.

Per root private commit stop1,355,161,600B, working set256MiB; aggregate private
5,420,646,400B and working set1GiB. Start requires commit/physical free8/4GiB;
runtime free below4/2GiB or commit increase above6GiB stops. C and E each need
1GiB disk free. These are sampled stopping thresholds, not hard limits.
Native supervisor retains its current address-space/file-size/time limits;
the1MiB sealed-image file limit exceeds the302104B runner.

Active fixed closure32MiB, run64MiB, combined96MiB; new preparation at most8MiB.
Bounded server capture4MiB, each client/controller64KiB, decoded evidence320KiB;
notice aggregate12MiB; API journal48MiB within the run limit. Runtime grammar
allows only fixed case, control, snapshot and exact HDC cache/log metadata
paths; cache cumulative peak128KiB. Unknown paths/links/identity changes stop.
HDC's own cache/log/key content is never read or hashed by this workflow.

## Explicit new notice policy and residual risks

The old broad server UNKNOWN exception is removed. Only a healthy, complete,
schema-bound **server stdout MONITOR/CLEANUP AUTH_WORD_ONLY** record classified
UNKNOWN/AUTH_CONTEXT_UNKNOWN may be retained while the original DDS observation
continues. Every event is checked: recognized requests/denials, all client
UNKNOWN, ECODE-only/BOTH, partial/oversize records or record failures stop.
This is a new approval boundary, not inherited from the strict State card.

Exact complete public program records are recognized separately: the Native
DIRECT_SUP_CREATED/no_signal_authority record, the fixed-step status sentinel,
and a transfer hash line whose filename exactly matches that fixed hash query.
They contain words such as authority or pair but are not generic authentication
exemptions. Source-controlled numeric/path grammar and full-record boundaries
are required; variants, suffixes, incomplete lines and requests/denials still
stop. The independent DDS evidence parser still checks hashes, remote return
status and Native identity; diagnostic classification cannot accept a step.

Unknown text may hide an unrecognized authentication request or denial. This
exception authorizes observation only: never accepting prompts, new pairing,
permanent trust, new identities/permissions or security/network configuration.
Retained unknown stays UNKNOWN/business_blocked. Even if all DDS payload and
exit checks succeed, overall status remains conditional AUTH_UNKNOWN and exit1;
it is not unconditional permission, ABI or SDK callback-quiescence certification.

HDC may create/update host hdckey/.pub, enumerate/handshake any connected USB
device and automatically change device authentication-flow state. New prompts
are not accepted. Endpoint substitution may misdirect even write/deploy/execute
operations; checks are not atomic endpoint binding. External Jobs can terminate
owned roots; sampled limits can overshoot; synchronous writes can block.
Abnormal Native termination does not guarantee identity restoration, descendant
termination, callback silence, remote completion or trust rollback. Native
created-child receipts have their documented direct-child-only scope.

Rollback: stop new work, then bounded own-handle cleanup. The current workflow
and finish path do not publish the6 registered cancellation steps; remote
cancellation is NOT implemented by this Host cleanup. No system rollback,
token deletion, recursive
deletion or restoration promise. Preserve all new test files and failure logs;
the original project/system state is not overwritten. Exact restore and normal
exit require explicit receipts, not host process exit alone.

## Validation meaning

Offline tests/review qualify composition and failure handling only. Actual board
reachability, current identity grant, ABI/runtime behavior, Socket transport,
both payload directions and Native close/callback silence are NOT_TESTED here.
No ROS2 execution or Transport-above modification is included.

The Native product was built from the existing candidate-d03 source worktree,
not every source or dirty change in the root checkout. A fresh comparison of55
production src/include inputs against the D06 manifest was identical. The
comparison is recorded in
[NATIVE_CANDIDATE_SOURCE_REUSE_D02.json](../build/directdds-composition-20261103-d01/NATIVE_CANDIDATE_SOURCE_REUSE_D02.json).
The root checkout is a different source view; this package does not certify it.
The [reuse record](../build/directdds-observation-package-20261103-d01/prepare/REUSE_VERIFICATION_D01.json)
reports14 fixed external inputs rehashed equal, including11 Native payload files,
with Native_rebuilt=false, binary_copies=0 and actual_HDC_calls=0. These are
existing artifact provenance and hash checks, not new Native execution evidence.

The earlier State sampling attempt consumed its permit with server created and
bound but not resumed, zero state queries and no completed State samples. The
actual attempt remains FAILED. Its counter-shadow defect is fixed separately;
the [counter repair note](HDC_STATE_COUNTER_REPAIR.md) distinguishes the supplied
actual-run summary from the offline TypeError reproduction. Neither is evidence
that a board is offline or that DDS works. No new HDC or device attempt was made
by this offline DDS preparation.

The initial [D01 tester record](../build/directdds-observation-package-20261103-d01/prepare/INITIAL_TEST_RESULT_D01.json)
remains BLOCKED because it did not execute the generated workflow composition.
The subsequent [D02 initial record](../build/directdds-observation-package-20261103-d01/prepare/INITIAL_TEST_RESULT_D02.json)
reports38 PASS (11 DDS composition,10 cleanup,17 notice; no skips). These are
retained historical stages, not substitutes for post-review evidence.

Tier3 Astra [review closure](../build/directdds-observation-package-20261103-d01/prepare/REVIEW_CLOSURE_D01.json)
closes all three P2 findings with zero open blockers: private target/identity
values stay outside this public document, the six unreachable cancellation
steps and Host-only cleanup limit are explicit, and canonical RESULT publication
occurs only after the pending record successfully flushes, fsyncs and closes.
The pending record is not canonical evidence; power-loss durability, directory
fsync and hostile concurrent filesystem mutation are not certified by that fix.

The retained [D01 final tester record](../build/directdds-observation-package-20261103-d01/prepare/FINAL_TEST_RESULT_D01.json)
reports39 PASS (12 DDS composition,10 cleanup,17 notice) before the timing
increment. Its source-binding SHA256 was
`78ef398da6832cf3b9c5318a139c6804040ad83b8f5c528ce4c097b8d7fe1ce4`.
That result and the original review/closure remain historical evidence.

The independent [initial timing test](../build/directdds-observation-package-20261103-d01/prepare/INITIAL_TIMING_TEST_RESULT_D01.json)
passed13 DDS composition tests, exit0, no skips. The subsequent focused Astra
[D02 review closure](../build/directdds-observation-package-20261103-d01/prepare/REVIEW_CLOSURE_D02.json)
found no actionable timing findings and zero open blockers, retained all three
prior closures, and checked the precise post-wait_accepted origin and unchanged
budgets/registry. The fresh independent
[D02 final tester record](../build/directdds-observation-package-20261103-d01/prepare/FINAL_TEST_RESULT_D02.json)
is the canonical execution evidence for the current timing candidate:

| Suite | Tests | Exit | Skips |
| --- | ---: | ---: | ---: |
| DDS composition | 13 | 0 | 0 |
| Cleanup | 10 | 0 | 0 |
| Notice | 17 | 0 | 0 |
| Total | 40 | all0 | 0 |

It records outer-controller exit0, memory compilation without pyc,
16 generated sources plus the input schema matching all17 binding rows, and14
fixed external input sizes/SHA256 rechecked equal. Source, candidate, binding,
protected68 metadata and Git status were unchanged during that test. The current
candidate source-binding SHA256 is
`dd9bae9bc53ba4347ce8a03c94e8c50f9c25eae04f8ffa0a99fd30cd6276d562`.
New controls and the run were absent at the final-test snapshot. This
documentation synchronization follows that test and changes only the public
status/evidence text; final package freeze remains Main's next stage.

The synthetic workflow exercised103 used steps through all four probe/data
windows and the coordinator against the finite123-step registry. FakeClock
checks targets start at least10seconds after the recorded observation origin,
handshake_proved=false, and a first-pump injected hard stop propagates without
query support or simulated delay. The final suite also covers generated
publication fsync failure, successful publication and replay refusal. It does
not execute Native programs, select a real identity,
transfer/deploy files, exercise live DDS/Socket traffic or invoke HDC/WinDLL.
Native SDK callback quiet remains UNKNOWN; worker-parent identity restoration
and remote termination remain unproved. The narrow complete-server
AUTH_WORD_ONLY exception retains UNKNOWN authentication semantics and never
turns that observation into unconditional overall PASS.

The D02 final tester commit_gate=PASS applies only to the local Fake timing
increment, with the prior composition evidence retained. Documentation readiness
is YES for local offline preparation only;
DEVICE_TESTED=false, actual DDS/device execution NOT_RUN and Stage1 NOT_VERIFIED.
Main still owns graph/pre-commit checks, publication, final package freeze/hash
qualification and the private ignored execution card. Exact A/B identifiers and
the existing Native identity name are appended only to that private card at
freeze; this public document retains symbolic descriptions. No preparation,
review, test or documentation result grants an actual execution opportunity.

Approval needed: one execution of this exact fresh package, including its new
deployment directories, four executable modes, existing identity select/restore
attempts, standard DDS traffic and the explicitly limited UNKNOWN observation
risk above. No automatic subsequent attempt is authorized.
