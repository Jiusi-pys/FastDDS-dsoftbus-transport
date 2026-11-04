# Original-handle terminal identity and final acceptance deadlines

Status: 2026-11-03. The host lifecycle fix is IMPLEMENTED and HOST_TESTED.
Python compilation/generated execution and the fresh 51-test final gate PASS;
Native binaries were not rebuilt. This phase performed no new HDC, SDK, board,
permission or identity operation, and no actual raw/key/stream body read or hash.
DSoftBus and dual-board DDS verification for this fix are NOT_RUN;
DEVICE_TESTED=false and overall DDS/Stage1 remains NOT_VERIFIED.

## Observed failure and evidence limits

The [causal analysis](../build/terminal-identity-analysis-20261103-d01/CAUSAL_ANALYSIS_D01.json)
records the consumed D03 sequence: an originally bound process/image pair,
QueryFullProcessImageNameW failure with error5, an immediate original-handle
WaitForSingleObject result258, then revoked operation authority. The same
original handle later became signaled with exit0 and EOF, but the old wrapper
rejected subsequent terminal identity at its authority precondition. That later
identity error was derivative of the earlier revocation.

The [natural sample](../build/terminal-identity-analysis-20261103-d01/NATURAL_SAMPLE_D01.json)
observed real image-query error5 followed by wait258 in 12 of 12 own local child
trials, with later original-handle terminal proof in all 12. No failure was
injected in those trials. Transition latency was not recorded; these samples
do not establish a 50ms bound or the internal Windows cause of error5.
The separate [controlled reproduction](../build/terminal-identity-analysis-20261103-d01/D03_CONTROLLED_REPRO_D01.json)
explicitly injected the image-query error while retaining a real original
process handle and image pin. It reproduces the old authority/observation
ordering, not a natural Windows image-query failure. Neither result proves DDS.

Microsoft's contracts distinguish the observations:

- [QueryFullProcessImageNameW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew)
  returns failure as zero with extended error information. Image-query failure
  alone does not establish that a process object is signaled.
- [WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject)
  distinguishes signaled WAIT_OBJECT_0 from nonsignaled WAIT_TIMEOUT258 and API
  failure. Closing the handle while its wait is pending has undefined behavior.
- [GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)
  supplies creation time from a process handle; this fix checks that native
  birth anchor alongside the original PID and retained executable pin.
- [TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)
  is asynchronous for another process, and completion requires a process-handle
  wait. A terminated process object persists until its open handles are
  released. This contract does not grant a new right to terminate a process.

## Implemented observation and authority boundary

The maintained [lifecycle helper](../scripts/hdc_goal/goal_handle_lifecycle.py)
and generated observation adapter preserve the original creator binding:
original retained process handle, PID, native creation time, previously verified
full live-image path, retained image pin and executable FileID. No PID reopen,
new process factory, cached LIVE-image fallback or permission change is added.
LIVE still requires a successful current full-image query and matching anchors.

Only after ImageUnavailable may a nonsignaled original handle receive one
additional read-only wait in that failure branch, at most 50ms and floor-clamped
to the remaining original deadline. No deadline or less than 1ms remaining adds
no wait. Error5, timeout, API failure, clock regression or elapsed deadline does
not prove termination. Multiple roots can accumulate bounded waits within the
existing deadlines; scheduling and synchronous calls are not hard real time.

An unavailable observation revokes signal/resume authority. Later read-only
terminal observation may succeed only on the same retained handles, with
matching creation/image-pin anchors, positive signaled state and successful
exit-code query. It cannot return LIVE or restore operation authority.
Contradictory identity, replaced/closed handles, changed pins or other invalid
proofs latch failure permanently. A caller requiring LIVE rejects an observed
terminal process while retaining its valid terminal proof for cleanup.
Cleanup does not signal a live root whose operation authority was revoked.

The backend binds the API journal case to the root before identity observation.
Server checks temporarily select the server case and restore the previous
context, preventing another root's identity evidence from being mislabeled.
No process identifiers, image paths or FileID values are published here.

## Final acceptance and unchanged budgets

The [backend](../scripts/hdc_goal/goal_windows_backend.py) and
[DDS generator](../scripts/hdc_goal/prepare_directdds.py) keep the original
work/step/server identity deadlines, including the existing pre-resume bind
deadline. Final business acceptance is rechecked after identity verification,
receipt publication and the final server check. DDS uses its original
work/server deadline; the maintained probe backend separately retains its
original listener deadline. No new LIMITS key or timeout extension is added.
The 123-entry registry and all existing LIMITS remain unchanged.

The exclusive per-root RESULT is a pre-acceptance observation. Its
content_evidence_qualified value is only a candidate; accepted_evidence is
persisted as False and step_acceptance as PENDING when content qualifies.
After publication, the final server/deadline check may permit in-memory
acceptance through the existing global grant/accepted ledger, which remains
authoritative. Acceptance exceptions reset the in-memory acceptance flag to
False/REJECTED without overwriting the immutable PENDING receipt. A content
candidate or terminal exit0 therefore does not independently prove acceptance.

## Independent review and final execution evidence

The retained [initial failure](../build/terminal-identity-analysis-20261103-d01/INITIAL_TEST_RESULT_D01.json)
and [subsequent initial test](../build/terminal-identity-analysis-20261103-d01/INITIAL_TEST_RESULT_D02.json)
remain separate records. Fixture contract corrections retained the original
cleanup expectations. The subsequent Astra review chain is also preserved:

- [D01 full review](../build/terminal-identity-analysis-20261103-d01/REVIEW_RESULT_D01.json)
  found P2 TERMINAL-DEADLINE-01: post-wait verification/publication could cross
  the original deadline before acceptance.
- [D02 limited review](../build/terminal-identity-analysis-20261103-d01/REVIEW_CLOSURE_D02.json)
  retained FAIL: ordinary-step PENDING/deadline handling passed, but generated
  DDS server acceptance referenced the absent server_ready_ms limit.
- [D03 closure](../build/terminal-identity-analysis-20261103-d01/REVIEW_CLOSURE_D03.json)
  closes both findings with zero open blockers. It preserves the separate DDS
  and probe deadline contracts; it does not replace final tester execution.

The fresh independent [final tester](../build/terminal-identity-analysis-20261103-d01/FINAL_TEST_RESULT_D01.json)
ran all three suites after D03 closure, each once:

| Suite | Tests | Exit | Skips |
| --- | ---: | ---: | ---: |
| Handle lifecycle | 27 | 0 | 0 |
| Cleanup | 10 | 0 | 0 |
| DDS composition | 14 | 0 | 0 |
| Total | 51 | all0 | 0 |

The lifecycle suite includes one own harmless Python child, original retained
process/image handles and real Win32 terminal proof, with only the image-query
failure injected. Revoked stop/resume APIs were forbidden, child exit0 and
handle close were checked. The twelve natural samples and separate controlled
script were not rerun by final testing. Other tests exercise synthetic deadline,
publication, exception, ledger and generated-adapter cases.

Current source/test pins, generated 17 source buffers plus inputs, registry and
limits, protected 68 metadata/status, consumed D03 frozen sources/records and
safe old-run metadata were unchanged across final testing. Compilation was in
memory without pyc. The retained snapshot inventory correction changed no
production/test source. No consumed package was assembled, revised or frozen.

| Evidence axis | Current status |
| --- | --- |
| Host lifecycle/acceptance implementation | PRESENT |
| Python compilation/generated execution | PASS |
| Independent final host tests | HOST_TESTED: 51 PASS, zero skips |
| Independent review | D03 closure; zero open blockers |
| Native build | NOT_REBUILT; no Native artifact change |
| New DSoftBus/dual-board DDS tests | NOT_RUN; DEVICE_TESTED=false |
| Overall DDS/Stage1 | NOT_VERIFIED |

The final tester's Commit gate: PASS and documentation Ready for commit: YES
cover this host fix only. Main retains graph/pre-commit disposition and Git
transitions. Scoped graph targets are UNKNOWN/unindexed, not proof of zero
impact or a clean repository-wide graph.

## Consumed D03 attempt and next verification boundary

The [actual result](../build/directdds-df-contract-package-20261103-d01/prepare/ACTUAL_TEST_RESULT_D01.json)
and [outcome](../build/directdds-df-contract-package-20261103-d01/prepare/MAIN_ACTUAL_OUTCOME_D01.json)
record the historical UTC interval 2026-11-03 00:16:24.914313 through
00:16:42.722400, entry exit1, FAIL_OR_HOLD and consumed once. A's 11 files were
uploaded/verified, four programs chmodded, and the read-only existing-identity
query accepted. B's file0 through 3 transfers were accepted; file4 started but
was not accepted. B's full fileset/hash/chmod verification was NOT_RUN.
Probes, identity setters/select-restore and DDS data were NOT_RUN. The original
cleanup uncertainty and failed result are not upgraded by this host repair.

The old board directory `/data/local/tmp/mdds_direct_20261103_c2d7a641` is retained
without cleanup, overwrite or reuse. These are historical accepted-step facts;
present board state has not been rechecked. No remote-descendant termination,
SDK callback silence, identity restoration or current resource state is proved.

The next shortest path is PLANNED: include this fix in a new complete DDS frozen
package, reuse the original 11 bound Native payload artifacts, choose a fresh
nonce and new directories on both boards, then qualify the concrete package and
just-in-time safe admission. Actual execution requires a new specific single-run
approval after that preparation.
Do not return first to connection sampling or reuse the consumed once. This
document grants no HDC, new raw read, signal, board cleanup or DDS execution.
Before new packaging, rollback is limited to reverting the scoped code changes;
it does not alter old evidence, consumed controls or retained board directories.
