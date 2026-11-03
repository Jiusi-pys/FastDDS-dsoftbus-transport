# HDC resource receipt compatibility and verification state

Status as of 2026-11-03. This note covers the pure host parser and future source
composition changes in `scripts/hdc_goal/resource_receipt.py` and
`prepare_directdds.py`, with their two test modules. The subsequent authorized
local classification and fresh AWK-free candidate are distinguished below;
neither establishes a successful DDS/device run.

## Latest actual attempt and local df repair

The later [AWK-free actual result](../build/directdds-builtin-package-20261103-d01/prepare/ACTUAL_TEST_RESULT_D01.json)
and [outcome](../build/directdds-builtin-package-20261103-d01/prepare/MAIN_ACTUAL_OUTCOME_D01.json)
for commit `61bec0b` retain FAIL_OR_HOLD, entry exit1: server, targets and provider `readonly.1`
passed; resource `readonly.2` failed with `DF_RECORD_UNKNOWN`. Deployment,
identity selection/restore, probes and all real/cross-device DDS were NOT_RUN.
The once is consumed. Its device evidence covers only the HDC/read-only prefix,
not DDS integration. A later separately approved
[local405-byte actual classification](../build/df-receipt-analysis-20261102-d01/LOCAL405_ACTUAL_TEST_RESULT_D01.json)
completed one405-byte read with original-handle/path checks and close, returning
only enums: complete standard `df -k`, single six-field row, unsigned ASCII
counts, attached percent, no edge padding, OTHER_LEXICAL_ABSOLUTE mount,
candidate ACCEPTED and no unclassified remainder. This confirms the old
two-name whitelist as the captured host receipt incompatibility; actual mount
values and content hashes are not published. The local permit is now consumed,
its operator/INTENT/RESULT exist, and the original actual FAIL is unchanged.
The subsequent
[local df display-path repair](HDC_DF_RECEIPT_CONTRACT.md) is HOST_TESTED: final47
PASS, zero skips, with zero open blocking review findings. It preserves the fixed
`df -k` query, strict receipts and resource thresholds; DEVICE_TESTED=false for
this repair and overall Stage1 NOT_VERIFIED. The
[fresh complete DDS binding](DIRECT_DDS_DF_EXECUTION.md) has initial14 PASS,
zero-blocker independent review and fresh final14 PASS; it reuses the prior47
component tests by exact hashes, without rerunning all47. Its new run/five
controls remain absent at the final-test snapshot, new DDS NOT_RUN and
DEVICE_TESTED=false. Root's later appended final hashes/private binding establish
freeze, not execution authority. A new specific single-run DDS grant is still
required; the405-byte read authorization grants no DDS execution. The sections
below retain the earlier evidence snapshots.

## Earlier actual attempt remains failed

The [actual tester result](../build/directdds-observation-package-20261103-d01/prepare/ACTUAL_TEST_RESULT_D01.json)
and [recorded outcome](../build/directdds-observation-package-20261103-d01/prepare/MAIN_ACTUAL_OUTCOME_D01.json)
record one attempt from 2026-11-02 20:56:45.788589 to 20:56:56.996156 UTC,
entry exit 1. Server, targets and the A-side provider query (`readonly.1`) were
accepted. Resource query `readonly.2` failed its evidence gate with
`REMOTE_BASE_RC_MISSING_OR_NONZERO`, despite local client exit 0. That combined
predicate does not distinguish absent, malformed or nonzero remote evidence and
does not identify the failing subcommand.

Deployment/staging, Native identity selection/restore, probes and all DDS traffic
were NOT_RUN. Overall actual attempt/integration result remains FAIL;
DEVICE_TESTED=false and Stage1 NOT_VERIFIED. The once opportunity was consumed;
no retry has occurred. A separately approved local classification subsequently
read the existing 59-byte body exactly once, with no content logging or hashing.
Its [structured result](../build/resource-receipt-analysis-20261102-d01/LOCAL59_ACTUAL_TEST_RESULT_D01.json)
reports AWK LOOKUP_OR_INACCESSIBLE, base marker NONZERO, root marker MISSING and
no unclassified remainder. This cannot distinguish missing installation, PATH
or permissions, establish wider board state or identify a DDS defect. The local
read permit is also consumed; it grants no additional read or device action.

## Established compatibility defects and narrow repair

Static source inspection and synthetic records establish two compatibility
issues in the old parser. A single CRLF replacement leaves a residual CR for
CRCRLF, so anchored numeric markers reject that otherwise supported framing.
The old resource proof also required root-ENOENT in local stderr and exactly ten
stdout records. Official HDC `AsyncCmd::Popen` merges remote stdout/stderr into
one pipe ([fixed commit 443234c, lines 206-222](https://github.com/openharmony/developtools_hdc/blob/443234c10deea8ba553437cf7365ce5e20970f0d/src/common/async_cmd.cpp#L206)).
Separate host pipes cannot separate descriptors already merged remotely. This
establishes a parser compatibility defect against that public implementation;
it does not attest the installed vendor binary's revision or explain this run.
The [source analysis](../build/resource-receipt-analysis-20261102-d01/ASTRA_ANALYSIS_D01.md)
records the comparison and limits. Public Toybox awk support is conditional;
public defaults do not establish this board's actual command inventory.

The new pure helper accepts only complete strict-UTF-8 LF, CRLF or CRCRLF records
within the existing bound. It rejects bare/extra CR, NUL, other prohibited control
characters and Unicode line separators. TAB remains permitted inside fields.
It requires a unique canonical zero base marker, a unique canonical nonzero
root-stat marker and the full ordered ten-field resource proof. All memory/disk
thresholds, canonical parent path and directory metadata requirements remain.

Only one exact ENOENT diagnostic bound to the expected test root is accepted:
either alone in separate stderr, or in merged stdout exactly between the base
and root markers. Nonzero stat alone does not prove absence. Unknown noise,
missing/nonzero/malformed/duplicate markers, wrong paths, misplaced or repeated
errors, permission failures and existing roots continue to reject. The helper
reports fixed marker states without accepting failed resource evidence.

The generator applies shared complete-record framing to resource/provider/step
receipts and early BEGIN handling, including rejecting incomplete BEGIN records.
Its parser-repair snapshot contained 17 source buffers plus the input schema
(18 inputs) in memory. A subsequent separately assembled AWK-free offline
candidate is recorded in [the current execution note](DIRECT_DDS_BUILTIN_EXECUTION.md);
freeze is pending at this documentation/test snapshot and execution remains
unauthorized. DDS,
restore and transfer evaluators retain exact AST reuse checks. No Fast DDS
Transport, RTPS, discovery, QoS or Writer/Reader behavior changes in this scope.

## Independent execution and review evidence

The [D02 initial tester](../build/resource-receipt-analysis-20261102-d01/INITIAL_TEST_RESULT_D02.json)
passed 56 host tests, zero skips. The subsequent independent
[D02 agentic review](../build/resource-receipt-analysis-20261102-d01/FORMAL_AGENTIC_REVIEW_D02.md)
and [closure](../build/resource-receipt-analysis-20261102-d01/REVIEW_CLOSURE_D02.json)
report zero actionable findings and zero open blockers on the four pinned files.
D02 includes the control-character hardening and supersedes the retained D01
review closure. The reviewer additionally reports 600 pure host adversarial
cases; this is separate from the canonical tester count below.

The fresh [final tester record](../build/resource-receipt-analysis-20261102-d01/FINAL_TEST_RESULT_D01.json),
executed after D02 review from 21:30:43.146982 to 21:31:19.458152 UTC, is canonical
for the local code repair:

| Suite | Tests | Exit | Skips |
| --- | ---: | ---: | ---: |
| Resource receipt | 15 | 0 | 0 |
| DDS composition | 14 | 0 | 0 |
| Cleanup | 10 | 0 | 0 |
| Notice | 17 | 0 | 0 |
| Total | 56 | all 0 | 0 |

The tester used rejecting live-effect/WinDLL stubs, memory compilation without
pyc and synthetic workflow inputs. Its preservation checks report the frozen
33-file closure, original card/manifest/bindings, consumed controls/once and
protected 68 metadata unchanged. Four code pins match review and final
before/after snapshots. No HDC, WinDLL, SDK or device operation was performed by
this repair's host validation. The original frozen package/hash/controls/once
remain the historical consumed package, not an upgraded execution candidate.

| Evidence axis | Current state |
| --- | --- |
| Local compatibility implementation | PRESENT |
| Python source compilation | PASS, in memory; no new Native build |
| Targeted/composition/cleanup/notice host tests | HOST_TESTED: 56 PASS, zero skips |
| Independent review | Completed; zero open blocking findings |
| Actual predeployment query attempt | FAIL, unchanged |
| Live DDS / Native identity / deployment | NOT_RUN |
| DDS device / cross-device verification | NOT VERIFIED; DEVICE_TESTED=false |
| Actual 59-byte local classification | AWK LOOKUP_OR_INACCESSIBLE; base NONZERO; root MISSING; no unclassified remainder |
| Overall DDS functionality / Stage1 | NOT VERIFIED |

Fast DDS/DSoftBus integration and device success are not claimed by these host
checks. The local repair changes parsing/composition only; proving its behavior
on real board output requires a separately qualified and authorized future run.
The final tester's `Commit gate: PASS` applies to the four-file host repair.
Documentation readiness covers those four files and these two public documents
only. Repository-wide graph/pre-commit and Git transitions remain Main's gates;
the inherited all-working-tree graph risk is HIGH and is not cleared by this
scoped result. No workflow result grants execution permission.

## Historical AWK-free preparation and authorization boundary

The [fresh AWK-free full DDS candidate](DIRECT_DDS_BUILTIN_EXECUTION.md) replaces
only the two meminfo AWK scans with `IFS= read -r`, `case` and `printf`; strict
receipts and resource thresholds remain unchanged. Its original initial and
post-review final host suites each passed 64 tests, zero skips. A later test-only
cleanup containment change passed an independent initial8, zero-blocker review
and fresh final8. Current final coverage is 64 unique tests: eight current D03
executions plus 56 unchanged tests retaining D02 final evidence, not a D03 full
64-test rerun. Those newer results cover local query/composition
preparation; the 56-test results above remain evidence for the earlier parser
repair. Host Git sh/Bash is not target OpenHarmony mksh verification.
No connection-sampling detour or actual new HDC, deployment, identity or DDS run
was performed. DEVICE_TESTED=false and overall Stage1 remains NOT_VERIFIED.

The actual FAIL is preserved. A new package, bindings, authorization and explicit
execution opportunity would be required before any later real run; the consumed
opportunities are not refunded by either repair or this documentation update.
At this documentation/test snapshot, publication/freeze is pending. Root's later
appended final hashes and private binding establish freeze, not actual execution
authorization; a new specific single-run approval is required. No precise file
identity, device identifier, existing Native name or
raw path is published here.
