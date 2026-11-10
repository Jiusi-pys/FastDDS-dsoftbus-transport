# Dual-board device evidence

Date: 2026-11-10

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

## Device path and identity

The runs used Tailscale SSH to reach both KaihongOS RK3588A boards.

| Endpoint | Role | OS | Kernel |
| --- | --- | --- | --- |
| `root@100.97.6.8` | board A | KaihongOS 6.1.0.04 | 6.6.101 |
| `root@100.97.143.74` | board B | KaihongOS 6.1.0.04 | 6.6.101 |

`hdc list targets` was empty, so `$hdc` was not the executed device path; SSH
reached both boards. `DEVICE_TESTED` is true for the SSH executions below and
only for those. All device traffic below is UDPv4 between the two boards; none
of it is DSoftBus evidence.

## Executed scopes

| Scope | Status | Evidence and boundary |
| --- | --- | --- |
| OHOS arm64 cross-build of the stock transport, `dsb_suite` and `cft_race`, 2.14.5 | BUILDS | `dsb-build-214.log`, ninja `[31/31] Linking CXX executable dsb_suite`. 31/31 is a build-step count, not a test count |
| Same, 3.6.2 | BUILDS | `dsb-build-36.log`, ninja `[31/31]` |
| Generated C++ roundtrip on device, 2.14.5 and 3.6.2, both boards | DEVICE_TESTED PASS | `roundtrip-{214,36}-{A,B}*.stdout.log` print `XCDR1 and XCDR2 PASS`, with `exit_code=0` in the paired `.command.log` |
| Same-version UDP delivery, `identical_xcdr1` / `identical_xcdr2`, both directions, both versions | DEVICE_TESTED PASS | 8 reader records, `matched: true`, `peer_alive: true`, received 63/62 (2.14.5 A to B), 40/42 (2.14.5 B to A), 40/38 (3.6.2 A to B), 46/39 (3.6.2 B to A) |
| `incompatible_member` rejection with a live peer, both directions, both versions | DEVICE_TESTED PASS | 4 reader records, `matched: false`, `received: 0`, `peer_alive: true`, `passed: true` |
| `appendable_added_member` / `mutable_added_member`, stock, both versions | DEVICE_TESTED FAIL | 8 reader records, `matched: false` with `peer_alive: true`, so a genuine no-match against a live peer, not a missing peer. Retained as pre-patch evidence; closed only by the gated patch, see below |
| Existing-tree DSoftBus `dsb_suite` execution | UNVERIFIED in this tree | See "DSoftBus `dsb_suite`" below. Builds exist and the 2.14.5 suite was launched on both boards, but no per-case result line is retained |
| New standards probe over DSoftBus | NOT_RUN | The new probe is UDP-only and has no DSoftBus deployment runner |
| Mixed-version UDP | BLOCKED | See "Mixed-version result" |

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
[`ARCHITECTURE.md`](ARCHITECTURE.md). Fast DDS 3.6.2 is not patched and is not
addressed by this result.

## Mixed-version result

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

### Rerun with isolated library paths: BLOCKED

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
DDS 2.14.5 and 3.6.2 cannot interoperate; a reader leaving
`m_force_type_validation` at its default has not been tested.

## DSoftBus `dsb_suite`

`tests/device_suite/compat.hpp`, shared by `dsb_suite` and `cft_race`, is
modified in this change set, so the suite's state matters here.

| Item | Status | Evidence |
| --- | --- | --- |
| `dsb_suite` cross-build, 2.14.5 and 3.6.2 | BUILDS | `dsb-build-214.log`, `dsb-build-36.log`, `build/ssh-device-stock-{214,36}/dsb_suite` |
| Compilation against the modified `compat.hpp` | COMPILED | OHOS SDK 23 clang `-fsyntax-only`, both version branches, 0 errors; see `build/tester-fix-20261110/FINDING.md` |
| `dsb_suite` launched on both boards, 2.14.5 | STARTED | `dsb214-core-{A,B}-*.log` reach `DSB_LISTENING`; `dsb214-core-A-finalpoll.log` also records `DSoftBus domain confirmation expected=151 actual=151 reason=Matched` |
| DSoftBus node enumeration | PARTIAL | `dsb214-node-A.out` `exit=0`; `dsb214-node-B.out` first returned `DSB_ERROR GetLocalNodeDeviceInfo exit=2`, and `dsb214-node-B-retry.log` then succeeded with `EXIT=0` |
| Per-case `dsb_suite` results | NOT RETAINED | No file in the tree contains a `DSB_CASE` or `DSB_SUMMARY` line; the on-device `work/.../A.log` and `B.log` were not pulled |
| `dsb_suite` executed against the modified `compat.hpp` | NOT_RUN | Needs the full OHOS stock-transport and DSoftBus deployment. `COMPILED != TESTED` |
| `dsb_suite` on 3.6.2 on device | NOT_RUN | Only configure and build logs exist for 3.6.2 |

Earlier revisions of this document recorded `core 3/3, feature 14/0 (14 pass,
0 fail)` with `type_lookup_remote_type`, deletion and `runtime_closed` passing
on both boards and both versions. **No retained file in this working tree
supports those counts**, so they are withdrawn rather than repeated. The retained launch lines record `groups=core` only, so even a recovered result line from this run could not have covered the withdrawn `feature 14/0`; that figure would need a separate feature-group execution. If that
run happened, its result lines are still on the boards or in another worktree;
until they are retained here, `dsb_suite` execution is `UNVERIFIED` for this
change set. `IMPLEMENTED != VERIFIED`, and `BUILDS != TESTED`.

## Overall status

| Dimension | State |
| --- | --- |
| Implementation | PRESENT (UDP standards probe, evidence runner, bounded IDL generation, gated 2.14.5 patch) |
| Build | PASS (host MSVC and OHOS arm64, both Fast DDS versions) |
| Host Test | PASS (65/65 unit, CTest 1/1 per version, three-way gate comparison) |
| RK3588A Test | PASS for the UDPv4 scopes above; `dsb_suite` execution UNVERIFIED; DSoftBus-backed standards probe NOT_RUN |
| Cross-device Test | PASS for same-version UDPv4 and for the patched 2.14.5 matrix in both board directions; mixed-version UDPv4 BLOCKED |
| Overall | **NOT VERIFIED** for the product scope: Fast DDS 3.6.2 assignability, the complete IDL 4.2 frontend and the C/Java mappings, the DDS-WEB gateway, the DDS-OPCUA gateway and a DSoftBus-backed standards probe are not implemented |

Step 5 documentation sync is complete. The pre-commit gate decision of
AGENTS.md section 11 Step 6 belongs to Main/Orchestrator, not to this document.
