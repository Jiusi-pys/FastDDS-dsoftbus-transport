# Stock DDS two-board verification after the AWK dependency repair

This is a fresh, offline-prepared candidate, not permission to run. The previous
240-second attempt failed before deployment at the A resource query. Its once
record and the subsequent 59-byte local classification are consumed and remain
unchanged. That classification identified AWK lookup/access failure, a nonzero
base marker and no root marker; it did not distinguish missing installation,
PATH or permissions. Neither attempt proves DDS traffic or SDK correctness.

## Exact change and unchanged acceptance

The A/B resource commands replace only the initial AWK scan with a subshell using
`IFS= read -r`, `case` and `printf`. It scans the complete kernel text file,
requires exactly one ordered MemTotal and MemAvailable record, rejects missing,
duplicate, inverted and unterminated records, and emits the original two lines.
Read/open/print failures cannot produce a successful base receipt. There are no
missing-value defaults. The remaining df/readlink/stat commands, remote status
markers, strict numeric/unit/record parser and every resource threshold remain
unchanged. Previously reviewed CRLF/CRCRLF and merged-ENOENT receipt compatibility
is included; the consumed package itself is not patched.

OpenHarmony mksh's line-mode read distinguishes EOF=1 from read error=2; this is
an implementation contract, not a guarantee for every POSIX shell. Git sh/Bash
testing does not certify the actual board shell. mksh can discard NUL bytes, so
this is a normal `/proc/meminfo` kernel-text contract, not binary validation.
printf may be an external command: its availability was evidenced by the prior
actual base marker, not assumed from a builtin name. No software is installed.

The private card records exact targets, existing Native identity, file hashes,
argv and entry bootstrap. The isolated Python entry adapter adds only the fixed
verified active directory for sibling imports; it does not load old controls.

## Fixed scope, inputs and finite actions

Scope `ONE_NEW_CURRENT_STOCK_DDS_AB_BUILTIN_MEMINFO_240S_D02`;
nonce `B07E91CA_DIRECT_DDS_240S_D02`.
Host run `E:/Codex-FastDDS-directdds-once-20261103_b07e91ca`.
On each fixed A/B board use the new directory
`/data/local/tmp/mdds_direct_20261103_b07e91ca`; an existing root stops admission.
No old file is overwritten or deleted. Old failures and old controls are retained.

Reuse by rehashing 14 fixed external artifacts, including 11 Native files,
without recompiling or duplicating their archives. Programs: own_goal_runner,
own_goal_launcher, goal_supervisor and readonly_frontend. Libraries: the existing
stock DSoftBus Transport, stock FastDDS 2.14.5/FastCDR 2.2.5, foonathan, tinyxml2,
libc++ and project identity bridge. Only the four new programs get mode0700.
The Native product belongs to the documented candidate-d03 source view, not
every dirty file in the root checkout. Reuse is not new execution evidence.

The original full DDS procedure remains: owned foreground service, ten-second
passive observation, target/provider/resource checks, verified staging, existing
identity query, per-board inheritance/restore probes, then standard DDS
Participant/Topic/Writer/Reader traffic, bidirectional three run/side/ordinal-bound
payloads, READY/release/DATA_DONE, deletion, restore and normal exit receipts.
No B-only or connection-sampling detour, retry, ROS2 or above-Transport change.

At most one self-created HDC service, 121 registered fixed clients (115 normal
and six currently unreachable cancellation steps), and one non-HDC controller:
123 possible roots, at most three concurrent clients/five live roots. The
current workflow does not publish the six cancellation steps; host cleanup
does not implement remote cancellation. Identity selection/restoration attempts
total at most eight across the four probe/data windows. They use only the
privately fixed existing project Native identity, package com.kaihong.mdds,
activating its existing full permission set. No identity/profile/account is
created and no persistent permissions are changed.

Total240 seconds, work236 seconds, final4 reserved for cleanup. Regular clients
15 seconds, long launches125 seconds, controller110 seconds. At most one stop
per original owned handle and a two-second wait. Any pre-existing HDC/8710
listener blocks startup; no takeover or killing unrelated instances.

Private stop threshold per root1,355,161,600B, working set256MiB; aggregate private
5,420,646,400B and working set1GiB. Starting commit/physical free8/4GiB; runtime
free below4/2GiB or commit growth above6GiB stops. C/E each require1GiB free.
These are sampled stops with possible overshoot, not hard limits. Existing Native
AS/file/time limits are retained. Active closure32MiB, run64MiB, combined96MiB;
preparation8MiB/128 files. Server capture4MiB, clients/controller64KiB each,
decoded320KiB, notice12MiB, API journal48MiB, fixed-cache metadata128KiB. No old
raw streams or key/cache contents are read or hashed by this preparation.

## Stop policy, risks and recovery

Only healthy complete server-stdout MONITOR/CLEANUP UNKNOWN records with
AUTH_CONTEXT_UNKNOWN/AUTH_WORD_ONLY may continue under a new explicit grant.
Unknown is retained: it may hide unrecognized authentication meaning. Requests,
denials, all client UNKNOWN, partial/oversize/malformed records and recording
failures stop. Exact public program/step/hash diagnostics still need independent
remote return-code, payload and identity acceptance. Nothing accepts a prompt,
new pairing or permanent trust. Even successful DDS data with retained unknown
is conditional AUTH_UNKNOWN/exit1, never unconditional overall PASS.

Official HDC may create/update host keys, enumerate all connected USB devices
and change automatic device authentication-flow state. Endpoint substitution
can misdirect deployment or execution. External Jobs, sampling overshoot,
synchronous write stalls, abnormal identity restoration, remote descendants,
callback silence and trust rollback remain risks/unknowns. No network/security
configuration change is included. Runtime closure and identity recovery require
explicit receipts; local exit alone proves neither.

Failure stops new work and attempts only bounded cleanup of proved own original
handles. No system rollback, token deletion, remote tree-kill, recursive deletion
or authentication rollback is promised. Retain uploaded test files and evidence.
No actual run, AUTH, admission, fresh proof or once is created by preparation.
The final private card and manifest require a new specific single-run approval.

## Evidence status

The independent [initial tester](../build/directdds-builtin-package-20261103-d01/prepare/INITIAL_TEST_RESULT_D01.json)
passed 64 host tests, zero skips, failures or errors. The subsequent Tier3 Astra
[agentic review closure](../build/directdds-builtin-package-20261103-d01/prepare/REVIEW_CLOSURE_D02.json)
reports no actionable findings and zero open blockers. The original independent
[full 64-test final result](../build/directdds-builtin-package-20261103-d01/prepare/FINAL_TEST_RESULT_D02_PRE_CONTAINMENT.json)
passed all 64 and is preserved byte-for-byte. A subsequent test-only change adds
resolved containment assertions before temporary-directory cleanup. Its separate
[initial eight-test run](../build/directdds-builtin-package-20261103-d01/prepare/INITIAL_CONTAINMENT_TEST_D03.json),
[focused review](../build/directdds-builtin-package-20261103-d01/prepare/REVIEW_CONTAINMENT_D03.json)
(zero blockers) and fresh post-review
[final eight-test run](../build/directdds-builtin-package-20261103-d01/prepare/FINAL_CONTAINMENT_TEST_D03.json)
all pass. The updated canonical
[final result](../build/directdds-builtin-package-20261103-d01/prepare/FINAL_TEST_RESULT_D02.json)
therefore covers 64 unique tests: eight current D03 final executions plus 56
unchanged tests retaining their D02 final evidence. D03 did not rerun all 64.

| Suite | Tests | Exit | Skips | Final evidence |
| --- | ---: | ---: | ---: | --- |
| Local shell resource query | 8 | 0 | 0 | Current D03 |
| DDS composition | 14 | 0 | 0 | Retained D02 |
| Resource receipt | 15 | 0 | 0 | Retained D02 |
| Cleanup | 10 | 0 | 0 | Retained D02 |
| Notice | 17 | 0 | 0 | Retained D02 |
| Unique current coverage | 64 | all 0 | 0 | 8 current + 56 unchanged |

Canonical final-result SHA256:
`4954165bdf3b914158ebc615cde38cdf02d8bbdc3f88a75a53c966391f79ce48`.
Preserved original full 64-test final SHA256:
`c864905bad844ba6b949353f3293900dce3af975836b7c98151b5ee9cb554196`.
The tested source/input binding SHA256 is
`169724adb065eb26abe8fc8585b5762e1670bfa457da3907054e021dadcdb1a3`.
Both original full-test stages retain the MSYS initialization failure (exit1, zero
assertions) and the approved rerun of the unchanged synthetic shell suite
(8 PASS). Git sh/Bash host execution does not certify OpenHarmony mksh.

Final integrity checks matched all 17 generated source files plus the input
schema (18 bound rows), rehashed the 14 fixed external artifacts, and confirmed
the complete 123-entry registry/digest, resource thresholds and limits unchanged.
The active buffers matched current composition; source/test pins, binding,
protected 68 metadata and Git status were unchanged during final testing.
Python compilation was in memory without pyc. Rejecting live-effect stubs and
synthetic driver/backend fixtures exercised the full DDS procedure; they are
not real DDS/DSoftBus integration. No new HDC, WinDLL, SDK, board or identity
operation, or actual raw-stream read/hash was performed by these tests.

| Evidence axis | Current state |
| --- | --- |
| Local query repair / offline composition | IMPLEMENTED; HOST_TESTED: 64 unique PASS (8 current + 56 unchanged) |
| Independent review / fresh final test | PASS; zero open blocking findings |
| Native build | Reused by hash; NOT_REBUILT |
| New HDC / deployment / identity / real DDS | NOT_RUN |
| Target OpenHarmony mksh | NOT_TESTED |
| RK3588A / cross-device DDS | DEVICE_TESTED=false; NOT_VERIFIED |
| Overall DDS functionality / Stage1 | NOT_VERIFIED |

Documentation synchronization follows the final tester and changes evidence text
only. Its `Commit gate: PASS` and documentation `Ready for commit: YES` cover
this local offline scope; Main still owns graph/pre-commit disposition, Git
transitions and source-bound publication/freeze. The inherited repository-wide
graph risk is HIGH, and unavailable scoped graph targets remain UNKNOWN; this
document does not clear those wider changes.

The new run and all five active controls were absent at the final-test snapshot;
actual_execution_authorized=false. At this documentation/final-test snapshot,
final package freeze and the private execution card are pending. Root's later
appended final hashes and private binding establish freeze, not actual execution
authorization. Exact fixed A/B identifiers and the existing Native name are
appended only to that private card by the current builder at freeze. A new
specific single-run approval is required for this fresh full DDS package after
qualification; no old once or local-classification permit is reusable. Prior
actual FAIL remains unchanged, and a local PASS cannot certify the boards.
