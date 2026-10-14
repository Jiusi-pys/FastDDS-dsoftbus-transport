# Offline executor encoding validation

This record covers only `tests/public_socket_executor/flow_tests.py` on base `facca428ab9a15649f44f2bdf216e446c7d56d5d`, branch `codex/flow-encoding-published-20261014`. The tested source SHA256 is `94b0f25df1d7b7ada52e1f7e01795f767da8279402732fe32f43e6598f205bff`. This independent branch does not include or qualify fixes from the separate qualification worktree.

The validator-mutation fixture now reads/writes its Python source explicitly as UTF-8. Three PowerShell subprocess call sites retain captured output as bytes instead of decoding with Python's locale. The 70 assertions, refusal expectations and timeouts are unchanged. No production executor, Transport, permission or protocol behavior changes.

## Evidence

The original baseline failed under default cp936 with a Unicode decoding error and exit 1. Under `PYTHONUTF8=1` it exited 0 but emitted four reader-thread decoding exceptions, so that exit was not accepted as a clean pass. Both original results remain preserved. Git archive produced CRLF bytes rather than the LF blob; exact LF-to-CRLF equivalence was checked explicitly, without changing tests.

Independent initial testing, agentic review (`PASS_SCOPED_W2_FLOW_ENCODING`, no actionable findings), and final testing ran sequentially. Final testing completed on 2026-10-14 at 22:41:32 UTC and reports scoped `result: PASS`, `commit_gate: PASS`. All 22 actual candidate commands exited 0:

| Final command group | Count and scope |
| --- | --- |
| Complete executor suite registered at this base | 16 commands, including the default-locale Flow suite; commands executed directly, not a claimed CTest build/run |
| Additional UTF-8 Flow run | One command; actual `utf8_mode=1`, preferred encoding UTF-8 |
| Direct consumers of Flow | Five commands: `device_bootstrap`, `final_materialization`, `installed_device_contract`, `local_recovery`, `ros_materialization_capture` |

Both Flow modes ran all 21 tests under Python 3.12.3. The default run confirmed `utf8_mode=0` and cp936; the second confirmed `PYTHONUTF8=1`. Neither produced background exceptions or decode errors. In each mode the intended unbound-validator refusal returned 1, its raw diagnostic identified the binding gate, and the forbidden-execution marker remained absent. All four PowerShell invocations per mode retained raw streams and all forbidden markers stayed absent.

The two 659-file source snapshots, working source, review and retained fake fixture were rechecked without drift. The `local_recovery` suite used its required exact-digest historical `fake-hdc.exe`/Python fixture; it was not replaced with an arbitrary stub. Python subprocess audit and fixture inspection found no actual HDC execution. No processes matching the final tester artifact root remained. These observations are not an exhaustive OS process/syscall trace.

Preliminary sandbox attempts, including timeouts/failures, remain in initial evidence and are not relabeled successful. Canonical initial and final executions used the approved outside-sandbox context with the original 90/45/20-second command limits. Review confirmed the intended encoding-only AST differences and unchanged assertions; it did not substitute for execution.

## Reproduction and limits

Evidence paths are relative to the outer checkout `C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`; this code worktree is `build/r5-host-fixes/publish-worktree`.

- `build/r5-host-fixes/publication-assessment/ASSESSMENT.md`: exact-base applicability and independent branch scope, not publication approval.
- `build/r5-host-fixes/publish-initial-test/INITIAL_TEST_RESULT.json` and `PRELIMINARY_ATTEMPTS.json`: baseline counterexamples, initial candidate results and retained sandbox attempts.
- `build/r5-host-fixes/publish-review/REVIEW.json`: independent review and source hash.
- `build/r5-host-fixes/publish-final-test/FINAL_TEST_RESULT.json` and `COMMAND_LEDGER.json`: canonical result and all 22 exact argv, working directories, environments, timeouts and exits.
- In the final-test directory, `SOURCE_BEFORE.json`, `SOURCE_AFTER.json`, `FIXTURE_BINDING.json`, `REVIEW_AND_INITIAL_BINDING.json`, `PROCESS_CLEANUP.json`, `EVIDENCE_MANIFEST.json`, and per-case raw logs/PowerShell binary streams retain bindings and observations.

For reproduction, use the final ledger and its recorded fixture hashes in a fresh output directory; run Flow under both actual locale modes, then all 16 registered cases and the five direct consumers without double-counting default Flow. Inspect background errors and exact refusal/marker evidence as well as exit codes. Do not overwrite canonical receipts. The old version-bound `local_flow` full orchestration was not run and is not implied by these 22 commands. No tests were rerun during documentation synchronization.

These are synthetic offline fixture tests with real local Python/Node/PowerShell execution. They do not establish ROS, DDS, DSoftBus, native Socket, RK3588 or cross-device communication. Device testing is N/A for this test-only encoding change; no native-transport capability is promoted. Main graph/pre-commit and publication checks remain required. This record neither claims a push/CI result nor releases unrelated qualification-branch fixes.
