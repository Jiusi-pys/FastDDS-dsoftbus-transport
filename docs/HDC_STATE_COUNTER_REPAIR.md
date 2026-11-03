# State resource counter repair

Implementation PRESENT; initial and final host suites PASS (58 tests each, no skips). Independent review found no actionable findings. Actual HDC retest NOT_RUN; `DEVICE_TESTED=false`, DDS NOT_RUN, Stage1 NOT_VERIFIED. These results qualify the host counter repair only.

## Actual attempt and attribution limits

The parent-supplied execution summary reports that the actual State attempt at **2026-11-02 19:49:08 UTC** consumed its single-use opportunity: the owned server was created and bound, but was not resumed. It stopped with `CREATE_OR_BIND_UNKNOWN`; **0 State queries** ran. One owned stop was performed, native exit was 125, and EOF, terminal proof and handle close passed. Whole-tree and remote state remain UNKNOWN. These facts describe a failed observation attempt, not successful State sampling or device communication.

The actual attempt did not provide a traceback identifying the Python exception. This documentation task did not read raw, cache, key or private run logs and does not independently reconstruct that attempt. The supplied summary is explicitly distinguished from the offline execution evidence below.

The retained [RED reproduction](../build/state-counter-repair-20261103-d01/RED_REPRO_D01.json) deterministically exercises the same pre-repair Python counter path at `PRE_RESUME` with WinDLL blocked. It records `samples_type_before=dict`, `TypeError: unsupported operand type(s) for +=: 'dict' and 'int'`, zero trace-observe calls and diagnostic exit 1. This establishes the counter collision and explains a possible same-code pre-resume abort. It does **not** prove that exception occurred in the actual attempt, identify a board fault or establish a device/network cause.

## Maintained change and regression boundary

`GoalWindowsBackend` owns numeric `samples` and `epochs` resource counters. Its `create_and_resume` calls the real parent `observe(root, 'PRE_RESUME')` after creation and before resume; `observe` increments `samples` before invoking the resource trace. Previously `StateBackend.init_state` replaced `samples` with a dictionary.

The change in [state_samples.py](../scripts/hdc_goal/state_samples.py) stores State projections in `state_records`. Outcome publication, summary selection and completion checks use that dictionary. The parent's numeric counter and resource-limit checks remain in place. Public `state_samples` output is unchanged; the patch changes no DDS/RTPS behavior or DSoftBus API.

The new `test_real_base_pre_resume_observe_keeps_numeric_resource_counter` in [test_hdc_state_samples.py](../tests/test_hdc_state_samples.py) preserves a nonzero counter through State initialization and calls `GoalWindowsBackend.observe` directly, bypassing the fixture's overridden observer. With a synthetic trace, it checks `samples` advancing from 7 to 8, `epochs` from 0 to 1, exactly one trace call, an empty `state_records` dictionary and resource journaling.

The fixture checks historical frozen source pins, then compiles and executes **current `source_buffers()` outputs in memory** with `ctypes.WinDLL` forbidden. A frozen-shaped module `__file__` is not evidence that the frozen implementation was executed. The consumed package and permit were not rewritten, requalified or reset. The older [State execution proposal](HDC_STATE_SAMPLES_EXECUTION.md) is retained as its historical preparation snapshot; its pre-execution statements do not describe the consumed attempt's current status.

## Execution evidence

| Suite | Initial tests | Final post-review tests | Final exit | Skips |
| --- | ---: | ---: | ---: | ---: |
| State | 17 | 17 | 0 | 0 |
| Pair | 14 | 14 | 0 | 0 |
| Notice | 17 | 17 | 0 | 0 |
| Cleanup | 10 | 10 | 0 | 0 |
| Total | 58 | 58 | all 0 | 0 |

The independent [initial tester record](../build/state-counter-repair-20261103-d01/INITIAL_TEST_RESULT_D01.json) precedes the [formal agentic review](../build/state-counter-repair-20261103-d01/FORMAL_AGENTIC_REVIEW_D01.json). That Tier 2 review found no blocking or nonblocking actionable findings and separately passed the new regression test (exit 0). The fresh independent [final tester record](../build/state-counter-repair-20261103-d01/FINAL_TEST_RESULT_D01.json) follows the review and is the canonical execution evidence: 58 PASS, two-file memory compilation PASS without pyc, unchanged source hashes and protected68 metadata, native exit 0 and outer-controller exit 0. Its `commit_gate=PASS` is explicitly scoped to **Host counter helper repair only**.

Current source hashes match the initial, review and final records:

| File | SHA256 |
| --- | --- |
| `scripts/hdc_goal/state_samples.py` | `fa6ba3c79a724168a19bf5b08b674ee7e9fa4cea22755feab95369a4d06f1eae` |
| `tests/test_hdc_state_samples.py` | `2cacf9adec2c338c2258c9fd7b2c3da1fde690e725283d8c0a7a56aaf31a64b6` |

## Remaining gates

| Item | Status and limit |
| --- | --- |
| Host counter implementation and tests | PRESENT / HOST_TESTED; initial and final PASS |
| Independent review | PASS for the two-file host repair; no actionable findings |
| Native build / actual HDC / WinDLL / board retest | NOT_RUN; Python memory compilation is the build check for this host-only patch |
| Device / cross-device validation | NOT_RUN; `DEVICE_TESTED=false` |
| Fast DDS / DSoftBus integration | NOT_RUN in this repair; DDS NOT_RUN |
| Stage1 | NOT_VERIFIED |
| Graph coverage | UNKNOWN for the Counterfix symbols; no callers or processes resolved by impact analysis |
| Documentation readiness | Ready for commit: YES for the Counterfix documentation scope only |

Graph-first pre-edit and review attempts could not resolve the Counterfix symbols; scoped source searches supplement that evidence without turning UNKNOWN into low risk. The recorded whole-working-tree [graph change analysis](../build/state-counter-repair-20261103-d01/GRAPH_PRE_COMMIT_D01.json) reports HIGH risk across 59 changed symbols in 26 files and 10 affected processes; it does not list these two Counterfix files. This is a global dirty-tree result, not a scoped all-clear.

Only the two maintained Counterfix files and this new note belong to the proposed source/documentation change. Concurrent Direct DDS files and unrelated existing changes are outside this scope. This documentation task changed neither source/tests nor the old card, frozen history or protected68 inputs. The Main/Orchestrator owns staging, graph/pre-commit decisions and any commit. Documentation readiness does not clear global gates, validate Stage1 or authorize another actual run; any future actual execution needs its applicable fresh qualification and authorization.
