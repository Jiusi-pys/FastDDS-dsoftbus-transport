# Dual-board device evidence

Date: 2026-11-10, updated 2026-11-11

Every count in this document was read from a retained file under `build/`;
where a claim rests on a run record rather than on raw per-iteration logs, the
document says so.

Retained evidence roots:

| Root | Holds |
| --- | --- |
| `build/ssh-device-tester-20261110/` | stock dual-board UDP matrix, device roundtrips, faultlogger, mixed-version originals, `dsb_suite` launch logs |
| `build/crash-rootcause-20261110/` | exit 139 root cause record (`FINDING.md` only) |
| `build/mixed-isolated-20261110/` | mixed-version rerun with isolated library paths, raw per-peer `.out`/`.err` and `summary.log` |
| `build/device-patched-20261110/` | patched 2.14.5 cross-board matrix, 44 per-peer `.out` files, `summary.log`, `FINDING.md` |
| `build/final-tester-20261110/f9/` | the final tester's own independent device re-run of `appendable_added_member` |
| `build/dsb-exec-20261110/` | the executed DSoftBus `dsb_suite` runs: `214-all/` and `36-all/` with `run.log` (88 `DSB_CASE` lines each) and `result.json`, deployment receipts, `hdc_ssh_shim.py`, and `FINDING.md` |
| `build/mixed-default-20261111/` | the mixed-version UDP re-run with the reader at default type validation: raw per-peer `.out`/`.err`, `summary.log`, `deploy-sha.log`, the post-review `confirm-*` re-run, and `FINDING.md` |

## Device path and identity

The runs used Tailscale SSH to reach both KaihongOS RK3588A boards.

| Endpoint | Role | OS | Kernel |
| --- | --- | --- | --- |
| `root@100.97.6.8` | board A | KaihongOS 6.1.0.04 | 6.6.101 |
| `root@100.97.143.74` | board B | KaihongOS 6.1.0.04 | 6.6.101 |

`hdc list targets` was empty, so `$hdc` was not the executed device path; SSH
reached both boards. `DEVICE_TESTED` is true for the SSH executions below and
only for those. The standards-probe matrices below are UDPv4 between the two
boards; the `dsb_suite` runs retained under `build/dsb-exec-20261110/` are
DSoftBus traffic between the same boards, driven by the unmodified
`scripts/release/run_cross_board.py` through an hdc-to-ssh shim.

## Executed scopes

*The `dsb_suite` and mixed-version rows were updated on 2026-11-11; the other
rows record the 2026-11-10 state unchanged.*

| Scope | Status | Evidence and boundary |
| --- | --- | --- |
| OHOS arm64 cross-build of the stock transport, `dsb_suite` and `cft_race`, 2.14.5 | BUILDS | `dsb-build-214.log`, ninja `[31/31] Linking CXX executable dsb_suite`. 31/31 is a build-step count, not a test count |
| Same, 3.6.2 | BUILDS | `dsb-build-36.log`, ninja `[31/31]` |
| Generated C++ roundtrip on device, 2.14.5 and 3.6.2, both boards | DEVICE_TESTED PASS | `roundtrip-{214,36}-{A,B}*.stdout.log` print `XCDR1 and XCDR2 PASS`, with `exit_code=0` in the paired `.command.log` |
| Same-version UDP delivery, `identical_xcdr1` / `identical_xcdr2`, both directions, both versions | DEVICE_TESTED PASS | 8 reader records, `matched: true`, `peer_alive: true`, received 63/62 (2.14.5 A to B), 40/42 (2.14.5 B to A), 40/38 (3.6.2 A to B), 46/39 (3.6.2 B to A) |
| `incompatible_member` rejection with a live peer, both directions, both versions | DEVICE_TESTED PASS | 4 reader records, `matched: false`, `received: 0`, `peer_alive: true`, `passed: true` |
| `appendable_added_member` / `mutable_added_member`, stock, both versions | DEVICE_TESTED FAIL | 8 reader records, `matched: false` with `peer_alive: true`, so a genuine no-match against a live peer, not a missing peer. Retained as pre-patch evidence; closed only by the gated patch, see below |
| Existing-tree DSoftBus `dsb_suite` execution, 2.14.5 and 3.6.2 | DEVICE_TESTED PASS | 70 passed, 0 failed per version over DSoftBus between both boards, per-case `DSB_CASE` lines retained; route-outage scenarios NOT run. See "DSoftBus `dsb_suite`" below and `build/dsb-exec-20261110/FINDING.md` |
| New standards probe over DSoftBus | NOT_RUN | The new probe is UDP-only and has no DSoftBus deployment runner |
| Mixed-version UDP, reader forcing type validation | BLOCKED | See "Mixed-version result"; this remains that configuration's result |
| Mixed-version UDP, reader at default validation | XCDR1 DEVICE_TESTED PASS; XCDR2 FAIL | Both board directions each; see "Mixed-version result" and `build/mixed-default-20261111/FINDING.md` |

The `xml` case is deliberately absent from the stock rows above. Those stock
runs predate the corrected expectation table, so the probe then graded `xml` as
a delivery case and recorded `passed: false` for it on all four stock
direction/version combinations, the observed behaviour being `matched: false`,
`received: 0`, `peer_alive: true`, which is a rejection. The current
`CASE_EXPECTATION` table in `scripts/standards/run_type_baseline.py` declares
`xml` a **rejection** case, and the runs made with the current code, in the
patched matrix below, grade it PASS on that basis. In both gradings the
measured behaviour is the same rejection; what changed is that the product now
asserts it. A green `xml` row is therefore a **documented non-conformance**: it
records that neither version line implements the DDS-XTypes XML data
representation and that the product does not claim that conformance point. It
is **not** an XTypes conformance pass, and "all six cases PASS" must not be
read as XTypes conformance.

## Patched 2.14.5 verification

The final tester's own device re-run (`build/final-tester-20261110/f9/`) made two
gate-off attempts. The first, on domain 190 with the original 12 s reader and 9 s
writer windows, returned `peer_alive: false` on both peers and exit 3 on each:
once SSH startup latency was added the two windows did not overlap enough for
discovery. It is retained as `f9/gateoff-*` rather than deleted. The second
attempt, `f9/gateoff2-*` on domain 191, produced the live-peer no-match recorded
below. This is disclosed for the same reason the equivalent host retry is
disclosed in `BASELINE.md`: a retried attempt that is not shown reads as a run
that always worked.

The gated patch
`patches/fastdds/2.14.5/0001-edp-honor-reader-type-consistency-qos.patch`
was verified on both boards, in both board directions, with the gate off and
on: 22 cross-board case runs, 44 peer processes, no exit 139. Gate on means
`FASTDDS_XTYPES_HONOR_TYPE_CONSISTENCY=1` on both endpoints. From
`build/device-patched-20261110/summary.log` and the per-peer `.out` files:

| Case | Gate off, A to B | Gate on, A to B | Gate off, B to A | Gate on, B to A |
| --- | --- | --- | --- | --- |
| `identical_xcdr1` | PASS | PASS | not repeated | not repeated |
| `identical_xcdr2` | PASS | PASS | PASS | PASS |
| `appendable_added_member` | **FAIL** | **PASS**, 129 received | **FAIL** | **PASS**, 130 received |
| `mutable_added_member` | **FAIL** | **PASS**, 130 received | **FAIL** | **PASS**, 130 received |
| `incompatible_member` | PASS (rejected) | PASS (rejected) | PASS (rejected) | PASS (rejected) |
| `xml` | PASS (asserted rejection) | PASS (asserted rejection) | PASS (asserted rejection) | PASS (asserted rejection) |

Exactly four non-zero received counts exist across the patched gate-on
appendable and mutable runs: one 129 and three 130. Every row reports
`peer_alive: true` and `cleanup_ok: true`.

The final tester re-ran `appendable_added_member` independently on the same
boards (`build/final-tester-20261110/f9/`): gate off with a live peer gave
`matched: false`, `received: 0`, exit 1; gate on gave `matched: true`,
`received: 251`, exit 0. That 251 is that run's own count, from a different run
than the 129/130 above.

The deployed library is the one built with `FASTDDS_PATCH_DIR` set:
`/e/fdds-patched-20261110/2.14.5/BUILD_INFO.txt` records
`fastdds_patch 7fd2fb0429dd...`, and the deployment hashes agree on both boards
(`libfastrtps.so.2.14` `416179dd29e033d9...`, `libfastcdr.so.2`
`113f8ef51c667c29...`, `bin/dds_type_probe` `9602b973ea7f8f22...`). The patch
file in the working tree now hashes to `240a2f8ba37a8825...` because its
explanatory header was rewritten twice after that build; the diff hunks never
changed, and `build/final-fix-20261110/FINDING.md` records the hash history and
the proof that all three versions produce the same `EDP.cpp`. Both hashes are
correct for what they describe, and neither should be "corrected" to the other.

What the gate relaxes, including the facts that it is **process-wide** and that
**extensibility kind is not enforced while it is on**, is disclosed in
[`ARCHITECTURE.md`](ARCHITECTURE.md). This retained 2.14.5 result does not
address the separate bounded A1 3.6.2 patch; see
[`A1_TYPE_ASSIGNABILITY.md`](A1_TYPE_ASSIGNABILITY.md).

## Mixed-version result

*Updated 2026-11-11: a default-validation re-run was executed; see the last
subsection. The two 2026-11-10 subsections stand unchanged for what they
describe.*

### The original four attempts are invalid, and exit 139 is resolved

| Pair | Recorded outcome |
| --- | --- |
| 2.14.5 to 3.6.2, XCDR1/XCDR2 | 2.14.5 endpoint exit 139; peer exit 3 |
| 3.6.2 to 2.14.5, XCDR1/XCDR2 | 2.14.5 endpoint exit 139; peer exit 3 |

Board B faultlogger recorded `cppcrash-23101-1794274018716` and
`cppcrash-23130-1794274027296` in `mixed-fault-B.log`, with the path
`__libc_free -> GetCompleteidObject -> builtinannotation/TypeObjectFactory ->
register_type -> probe run`.

**This was not a Fast DDS defect, and it is not an open crash.**
`mixed-original-selector.json` records the launcher: it selects the library
path with `if($v -eq '214')` while the version passed is `2.14.5`, so the
comparison never matched and the 2.14.5 endpoint was given
`LD_LIBRARY_PATH=.../3.6.2/lib:.../2.14.5/lib`. Both directories ship
`libfastcdr.so.2` under one soname from different builds:

| Path | sha256 | Fast CDR |
| --- | --- | --- |
| `2.14.5/lib/libfastcdr.so.2` | `113f8ef51c667c29...` | 2.2.5 |
| `3.6.2/lib/libfastcdr.so.2` | `d735d942b10df5e6...` | 2.3.7 |

`libfastrtps.so.2.14` exists only under `2.14.5/lib`, so the process ran Fast
DDS 2.14.5 against Fast CDR 2.3.7. `GetCompleteidObject` is the first complex
CDR size calculation during startup, and a wrong computed size corrupts musl
mallocng metadata, which is why the SEGV surfaces in `get_meta` under
`__libc_free`. The `3.6.2/lib` directory was also incomplete, lacking
`libtinyxml2.so.6`, `libssl.so.3`, `libcrypto.so.3` and `libc++_shared.so`,
which is why the launcher appended `2.14.5/lib` at all.

Controls on board B, same unmodified binary:

| Control | Peer | `LD_LIBRARY_PATH` | Iterations | exit 139 |
| --- | --- | --- | --- | --- |
| Solo, own library directory | none | `2.14.5/lib` | 40 and 20 | 0 |
| Same-version pair | 2.14.5 | `2.14.5/lib` | 15 pairs | 0 |
| Solo, original buggy order | **none** | `3.6.2/lib:2.14.5/lib` | 20 | **20** |

The third control is decisive: the crash reproduces 20/20 with no peer process
running at all, so no mixed-version DDS traffic is involved. These iteration
counts come from the run record `build/crash-rootcause-20261110/FINDING.md`;
that directory holds the record only, and the raw per-iteration logs behind the
40/20/15/20 controls were not pulled off the board.

Earlier orchestration and transfer errors were corrected and rerun, with their
original logs retained separately.

### Rerun with isolated library paths, reader forcing validation: BLOCKED

Both version directories were made self-contained and the matrix rerun
(`build/mixed-isolated-20261110/`, raw `.out`/`.err` per peer plus
`summary.log`). No crash occurred in any of the eight processes, and every peer
exited 3. In all four attempts the 3.6.2 endpoint reported `matched: true` and
the 2.14.5 endpoint `matched: false`, and every endpoint reported
`peer_alive: false`, so
no live-peer control was established in either direction.

Mixed-version delivery is therefore **`BLOCKED`, not `FAIL` and not `PASS`**.
The probe declines to grade a case without a live peer. The reason for the
split is known and upstream: 2.14.5 `EDP::checkTypeValidation` refuses when the
reader sets `m_force_type_validation` and the peer supplies no legacy
`TypeIdentifier`/`TypeObject`, while 3.6.2 `EDP::valid_matching` never reads
`TypeConsistencyEnforcementQosPolicy` at all and so accepts. This is a result
about these two endpoint configurations only. It is **not** a verdict that Fast
DDS 2.14.5 and 3.6.2 cannot interoperate; the reader-at-default configuration
it left untested was executed on 2026-11-11 and is recorded next.

### Re-run with the reader at default validation (2026-11-11): XCDR1 PASS, XCDR2 FAIL

`integrations/standards/type_probe.cpp` now reads
`DDS_STANDARDS_FORCE_TYPE_VALIDATION`. With the value exactly `0` the reader
QoS is left untouched at the Fast DDS release default (no assignment at all)
and the probe prints `DDS_STANDARDS_NOTE force_type_validation=0` into the
retained stdout; with the variable absent or any other value it keeps forcing
validation, so all earlier evidence stands for its configuration. Deployment
receipts and probe hashes are in `build/mixed-default-20261111/deploy-sha.log`
and `FINDING.md`; no new digest is cited here.

The matrix ran on domain 203 with the same isolated per-version deployments,
reader env `DDS_STANDARDS_FORCE_TYPE_VALIDATION=0`, writer unchanged. All
eight processes ran; no crash, no exit 139. From the retained reader records:

| Attempt | Writer | Reader | Reader record | Verdict |
| --- | --- | --- | --- | --- |
| `214w_36r_xcdr1` | 2.14.5 at A | 3.6.2 at B | `matched: true`, `received: 514`, `peer_alive: true`, exit 0 | **PASS** |
| `214w_36r_xcdr2` | 2.14.5 at A | 3.6.2 at B | `matched: true`, `received: 0`, `peer_alive: true`, exit 1 | **FAIL**, live peer, no delivery |
| `36w_214r_xcdr1` | 3.6.2 at A | 2.14.5 at B | `matched: true`, `received: 495`, `peer_alive: true`, exit 0 | **PASS** |
| `36w_214r_xcdr2` | 3.6.2 at A | 2.14.5 at B | `DDS_STANDARDS_ERROR sample value mismatch`, exit 2 | **FAIL**, samples decode to wrong values |

The former BLOCKED state is discharged for this configuration: a live-peer
control establishes in both directions, so the probe grades the case. XCDR1
interoperates in both directions, DEVICE_TESTED PASS. XCDR2 is a genuine FAIL
with a live peer in both directions, not BLOCKED, failing differently per
direction; the observation is recorded as what was measured, delivery and
value comparison, not as a conformance diagnosis of either release.

The independent review asked that `=0` perform no assignment instead of
assigning `false`; the probes were rebuilt from the amended source and both
XCDR1 directions re-run on domain 204: PASS again, `received: 513` with the
2.14.5 writer, then `received: 496` the other way, `peer_alive: true`, exit 0
everywhere (`confirm-*` files, `summary.log`). The XCDR2 failures were not
re-run and carry the pre-review binaries' hashes.

Scope: the probe's keyed bounded record, these two releases, UDPv4 between
these boards, stock isolated deployments. It does not cover the patched
2.14.5 library, other types, or DSoftBus.

## DSoftBus `dsb_suite`

*Updated 2026-11-11 with the executed runs of 2026-11-10 evening; the earlier
same-day state is kept below as history.*

`tests/device_suite/compat.hpp`, shared by `dsb_suite` and `cft_race`, is
modified in this change set, so the suite's state matters here.

| Item | Status | Evidence |
| --- | --- | --- |
| `dsb_suite` cross-build, 2.14.5 and 3.6.2 | BUILDS | `dsb-build-214.log`, `dsb-build-36.log`, `build/ssh-device-stock-{214,36}/dsb_suite` |
| Compilation against the modified `compat.hpp` | COMPILED | OHOS SDK 23 clang `-fsyntax-only`, both version branches, 0 errors; see `build/tester-fix-20261110/FINDING.md` |
| `dsb_suite` launched on both boards, 2.14.5 (first attempt, earlier that day) | STARTED | `dsb214-core-{A,B}-*.log` reach `DSB_LISTENING`; `dsb214-core-A-finalpoll.log` also records `DSoftBus domain confirmation expected=151 actual=151 reason=Matched` |
| DSoftBus node enumeration | PARTIAL | `dsb214-node-A.out` `exit=0`; `dsb214-node-B.out` first returned `DSB_ERROR GetLocalNodeDeviceInfo exit=2`, and `dsb214-node-B-retry.log` then succeeded with `EXIT=0` |
| `dsb_suite` executed against the modified `compat.hpp`, 2.14.5, all groups | DEVICE_TESTED PASS | `build/dsb-exec-20261110/214-all/`: 70 passed, 0 failed (35 per board), `DSB_EXIT=0` on both boards, runner exit 0 |
| `dsb_suite` executed against the modified `compat.hpp`, 3.6.2, all groups | DEVICE_TESTED PASS | `build/dsb-exec-20261110/36-all/`: 70 passed, 0 failed (35 per board), `DSB_EXIT=0` on both boards, runner exit 0 |
| Per-case `dsb_suite` results | RETAINED | 88 `DSB_CASE` lines per run's `run.log`, parsed into `result.json`; per board and version, core 3, QoS 15, feature 14, security 2, statistics 1 all PASS |
| Nine same-host-only cases | NOT_APPLICABLE | Recorded by the runner as records, not passes |
| Route-outage scenarios (`--route 1\|2`) | NOT_RUN here | iptables is denied over the SSH channel; route behaviour keeps its earlier evidence and is not re-qualified by these runs |

The suites were rebuilt from this tree (so the binaries correspond to the
modified `compat.hpp`), deployed over the Tailscale SSH channel, and driven
by the unmodified `scripts/release/run_cross_board.py` through
`hdc_ssh_shim.py`, which translates the runner's three hdc verbs to
`ssh`/`ssh cat`; all runner logic is the runner's own. Binary and deployment
hashes are recorded in `build/dsb-exec-20261110/FINDING.md` and its
`board-{pre,post}-deploy-sha.log` receipts; no new digest is cited here.
These runs qualify the standards-branch tree only, and Fast DDS 3.2.5 was
not run for this tree.

History: earlier revisions of this document recorded `core 3/3, feature 14/0
(14 pass, 0 fail)` with `type_lookup_remote_type`, deletion and
`runtime_closed` passing on both boards and both versions. **No retained file
in this working tree supports those counts**, so they were withdrawn, and
they stay withdrawn. The on-device logs of that launch were searched for on
both boards before re-running and no longer exist, so the withdrawn counts
are unrecoverable. The executed runs above supersede the withdrawn claim with
fresh evidence; they do not reinstate it. Until those runs existed,
`dsb_suite` execution was `UNVERIFIED` for this change set:
`IMPLEMENTED != VERIFIED`, and `BUILDS != TESTED`.

## Overall status

The final A1.0 3.6.2 bounded-assignability execution is recorded separately
in [`A1_TYPE_ASSIGNABILITY.md`](A1_TYPE_ASSIGNABILITY.md), with canonical
rebuild, review-resolution, host, and dual-board candidate evidence under
`build/a1-36/`. It does not convert this document's broader product status or
the retained mixed-version and route-fault limits into a full XTypes or
DSoftBus-standards completion claim.

| Dimension | State |
| --- | --- |
| Implementation | PRESENT (UDP standards probe, evidence runner, bounded IDL generation, gated 2.14.5 patch) |
| Build | PASS (host MSVC and OHOS arm64, both Fast DDS versions) |
| Host Test | PASS (93/93 unit, CTest 1/1 per version, three-way gate comparison) |
| RK3588A Test | PASS for the UDPv4 scopes above and for the executed DSoftBus `dsb_suite` on 2.14.5 and 3.6.2 (route-outage scenarios not re-run); DSoftBus-backed standards probe NOT_RUN |
| Cross-device Test | PASS for same-version UDPv4, for the patched 2.14.5 matrix, and for the `dsb_suite` runs between both boards; mixed-version UDPv4 at default reader validation XCDR1 PASS and XCDR2 FAIL; the forced-validation configuration stays BLOCKED |
| Overall | **NOT VERIFIED** for the product scope: Fast DDS 3.6.2 assignability, the complete IDL 4.2 frontend and the C/Java mappings, the DDS-WEB gateway, the DDS-OPCUA gateway and a DSoftBus-backed standards probe are not implemented |

Step 5 documentation sync is complete. The pre-commit gate decision of
AGENTS.md section 11 Step 6 belongs to Main/Orchestrator, not to this document.
