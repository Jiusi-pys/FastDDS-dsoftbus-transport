# Standards baseline evidence

Date: 2026-11-10, updated 2026-11-11; documentation sync: 2026-11-12

Branch: `codex/dds-standards-product`

This worktree delivers a UDP baseline probe and bounded IDL generation with
version-bound C++ roundtrip tests, plus gated upstream patches for Fast DDS
2.14.5 (including the bounded A2 XCDR2 framing repair) and the bounded A1 3.6.2 assignability subset. The complete capability
status is listed in the architecture table and
[`A1_TYPE_ASSIGNABILITY.md`](A1_TYPE_ASSIGNABILITY.md).
This document does not claim DDS-XTypes Complete, full IDL 4.2 language
coverage, DDS-WEB, DDS-OPCUA, or DSoftBus integration for the new UDP-only
standards probe. Existing-tree DSoftBus `dsb_suite` state is recorded in
[`DEVICE_EVIDENCE_20261110.md`](DEVICE_EVIDENCE_20261110.md).

## Evidence roots

| Root | Holds |
| --- | --- |
| `build/final-tester-20261110/` | the Step 4 final tester's own runs: `f8-stock-gateoff`, `f8-patched-gateoff`, `f8-patched-gateon`, `f8-patched-gateon-retry`, the `f9` device re-run, the `f6` shadowing and `f7` line-ending fixtures, and the synthetic runner fixtures under `fake/` |
| `build/review-fix-gate-off/`, `build/review-fix-gate-on/` | the post-review host A/B pair, `schema_version: 2`, each declaring `gate_variable`, `gate_value`, `gate_on` and `case_expectations` |
| `build/prepatch-214/`, `build/patched-gate-off/`, `build/patched-gate-on/` | the host three-way comparison; summarised in `build/patched-214/FINDING.md` |
| `build/device-patched-20261110/` | the 22-run cross-board matrix for the patched library |
| `build/ssh-device-tester-20261110/` | stock dual-board UDP matrix, device roundtrips, mixed-version originals, faultlogger |
| `build/crash-rootcause-20261110/`, `build/mixed-isolated-20261110/` | exit 139 root cause and the isolated-path rerun |
| `build/final-tester/` | an **earlier** pass. Its `unit.log` records `Ran 12 tests` from the then-current suite and is not the current unit-test count |
| `build/dsb-exec-20261110/` | the executed DSoftBus `dsb_suite` runs: `214-all/` and `36-all/` run directories with `run.log` (88 `DSB_CASE` lines each) and `result.json`, deployment receipts, and `FINDING.md` |
| `build/mixed-default-20261111/` | the mixed-version UDP re-run with the reader at default type validation: raw per-peer `.out`/`.err`, `summary.log`, `deploy-sha.log`, the post-review `confirm-*` re-run, and `FINDING.md` |
| `build/a1-36/final-tester/` | canonical final A1 rebuild, host/device execution, and commit-gate evidence |
| `build/a2-20261112/final-tester/` | canonical final A2 host, secure dual-board, LAN matrix, DSoftBus regression, and commit-gate evidence |
| `build/a1-36/review/` | independent A1 review and P2 resolution records |

Fix-pass and review records: `build/tester-fix-20261110/FINDING.md`,
`build/fix-pass2-20261110/FINDING.md`,
`build/review-fix-20261110/FINDING.md`,
`build/final-fix-20261110/FINDING.md`,
`build/tester-20261111/FINDING.md`,
`build/review-fix-20261111/FINDING.md`,
`build/final-tester-20261111/FINDING.md`.

## Requirements and platform boundary

`scripts/standards/run_type_baseline.py` needs **Python 3.11 or newer**
(`hashlib.file_digest`).

Every `results.json` written by the **current** runner records `gate_variable`,
`gate_value` and `gate_on`, at the root and on each peer record, and carries
`schema_version: 2`. Many retained files predate the field, 25 of them at the
time of writing. The three that this document cites as the host three-way
comparison are `build/prepatch-214/`, `build/patched-gate-off/` and
`build/patched-gate-on/`: those are `schema_version: 1`, contain none of those
keys, and their gate state is established by the surrounding records rather than
by the file itself. Where a self-evidencing file is needed, use
`build/review-fix-gate-off/` and `build/review-fix-gate-on/`, or
`build/confirm-20261110/`. Read these as **the configuration the runner was
launched with**, not as confirmation that the loaded library honoured the gate:
the runner reports its own environment, it never sets the variable itself, and
the probe does not read or echo it. What actually proves the gate took effect is
the A/B verdict difference between a gate-off and a gate-on run of the same
binaries.

The runner targets Windows. It injects `--writer-runtime-dir` /
`--reader-runtime-dir` through `PATH`, which is what governs DLL resolution on
Windows. On POSIX, `.so` resolution is governed by `LD_LIBRARY_PATH` and RPATH,
so `PATH` alone would not make the loader use those directories. The `.so`
handling in the shadowing guard exists to **audit** a directory set, including a
device deployment staged on this host, not to launch peers from it. Device peers
are started by hand over ssh with an explicit `LD_LIBRARY_PATH` and are outside
this script; their records must treat exit 139 as a crash explicitly, which the
device evidence documents do.

The runner owns the `CASE_EXPECTATION` table and refuses a probe whose declared
expectation disagrees with it, so the probe cannot grade itself. `CONTROL_CASE`
is `identical_xcdr2`, looked up by name, and every rejection case is reported
`BLOCKED` unless that control delivered in the same run.

## Reproduce the host baseline

The CMake entry selects one exact Fast DDS release with
`DDS_STANDARDS_FASTDDS=2.14.5` or `3.6.2`. It requires the matching Fast DDS
and Fast CDR CMake packages.

Generate the bounded fixture with the matching Fast DDS-Gen release. The
generator command verifies the JAR hash and version, stores `generation.json`,
and records stdout/stderr for each stage.

```powershell
python scripts/standards/generate_types.py `
  --java "E:/DevEco Studio/jbr/bin/java.exe" `
  --generator "<Fast DDS-Gen 3.3.1 or 4.3.0 jar>" `
  --fastdds 2.14.5 `
  --generator-sha256 "<verified jar hash>" `
  --out build/generated-214

cmake -S integrations/standards -B build/standards-214 `
  '-DDDS_STANDARDS_FASTDDS=2.14.5' `
  "-DDDS_STANDARDS_GENERATED_DIR=$PWD/build/generated-214" `
  -Dfastrtps_DIR="<Fast DDS 2.14.5>/share/fastrtps/cmake" `
  -Dfastcdr_DIR="<Fast CDR 2.2.5>/lib/cmake/fastcdr"
cmake --build build/standards-214 --config Release
ctest --test-dir build/standards-214 -C Release --output-on-failure
```

Use `'-DDDS_STANDARDS_FASTDDS=3.6.2'`, `fastdds_DIR`, Fast CDR 2.3.7, and
`build/generated-36` for the second release. The checked-in IDL fixture is
`integrations/standards/idl/BoundedRecord.idl`. It contains a keyed bounded
structure, a bounded string, and a bounded byte sequence.

Before running the Windows executables, prepend the selected Fast DDS, Fast CDR,
OpenSSL, and other runtime `bin` directories to `PATH`. CMake configure can
complete without this, while the executable and CTest run require the matching
DLL set. Use a new `--out` directory for every baseline invocation because the
runner refuses to overwrite an existing evidence directory.

The UDP probe starts one independent writer and one independent reader through
`scripts/standards/run_type_baseline.py`. It records the executable hash,
runtime library paths, command, raw stdout/stderr, peer discovery control,
cleanup result, termination description, and the machine-readable result line.
A rejection case requires the parallel control topic to deliver successfully. A
reader without a writer returns exit code 3 and is recorded as `BLOCKED`.

```powershell
python scripts/standards/run_type_baseline.py `
  --writer build/standards-36/Release/dds_type_probe.exe `
  --reader build/standards-36/Release/dds_type_probe.exe `
  --writer-version 3.6.2 --reader-version 3.6.2 `
  --writer-runtime-dir "<Fast DDS 3.6.2>/bin" `
  --reader-runtime-dir "<Fast DDS 3.6.2>/bin" `
  --out build/baseline-36-new
```

## Measured results

| Evidence | 2.14.5 | 3.6.2 | State |
| --- | --- | --- | --- |
| Fast DDS-Gen generation with verified version/hash | PASS | PASS | Implemented; `build/final-tester/final-generated-{214,36}/generation.json` record `generator_version` 3.3.1 and 4.3.0 with `PASS` and the verified generator/java/IDL hashes |
| Host generated C++ roundtrip | CTest 1/1 PASS | CTest 1/1 PASS | HOST_TESTED; `ctest-214.log`, `ctest-36.log` |
| OpenHarmony arm64 `dds_type_probe` and roundtrip builds | PASS | PASS | BUILDS; the roundtrip also ran on both boards (DEVICE_TESTED PASS) |
| Same-type XCDR1 and XCDR2 UDP delivery, both directions | PASS | PASS | DEVICE_TESTED on each same-version pair |
| Incompatible-member rejection with live peer control, both directions | PASS | PASS | DEVICE_TESTED |
| Appendable added-member case, stock | FAIL | FAIL | Retained stock baseline; genuine no-match against a live peer |
| Mutable added-member case, stock | FAIL | FAIL | Retained stock baseline; genuine no-match against a live peer |
| Appendable added-member case, patched 2.14.5, gate on | PASS | **not addressed by this 2.14.5 row** | DEVICE_TESTED, 129 received A to B and 130 received B to A; the separate A1 3.6.2 subset is documented independently |
| Mutable added-member case, patched 2.14.5, gate on | PASS | **not addressed by this 2.14.5 row** | DEVICE_TESTED, 130 received in each board direction; the separate A1 3.6.2 subset is documented independently |
| XML data representation case | asserted **rejection** | asserted **rejection** | **Documented non-conformance, NOT a conformance pass**; see below |
| Reader with no writer | exit 3 / BLOCKED | exit 3 / BLOCKED | Evidence guard |
| Python evidence and failure-path tests | 93/93 PASS | 93/93 PASS | UNIT_TESTED; version-independent, measured today |
| Mixed-version UDP, original four attempts | INVALID | INVALID | Exit 139 was a Fast CDR ABI collision created by the launcher, not a Fast DDS defect; see `build/crash-rootcause-20261110/FINDING.md` |
| Mixed-version UDP, isolated library paths, reader forcing type validation | BLOCKED | BLOCKED | No crash in any of 8 processes; no live-peer control established in either direction for that configuration, which remains its result; see `build/mixed-isolated-20261110/FINDING.md` |
| Mixed-version UDP XCDR1, reader at default validation (`DDS_STANDARDS_FORCE_TYPE_VALIDATION=0`) | PASS | PASS | DEVICE_TESTED with a live peer in both board directions: `received: 514` (2.14.5 writer, 3.6.2 reader), `received: 495` (reverse); post-review confirm re-run `received: 513`, `received: 496`; see `build/mixed-default-20261111/FINDING.md` |
| Mixed-version UDP XCDR2, reader at default validation | FAIL | FAIL | Genuine FAIL with a live peer in both directions: the matched 3.6.2 reader records `received: 0`; the 2.14.5 reader receives samples that decode to wrong values (sample value mismatch); see `build/mixed-default-20261111/FINDING.md` |
| Existing-tree DSoftBus `dsb_suite` execution | PASS | PASS | DEVICE_TESTED 2026-11-10 over DSoftBus between both boards: 70 passed, 0 failed per version (35 per board), `DSB_EXIT=0` on both boards, 88 `DSB_CASE` lines retained per `run.log` plus `result.json`; nine same-host-only cases NOT_APPLICABLE (recorded, not passes); route-outage scenarios NOT run; see `build/dsb-exec-20261110/FINDING.md` |
| New standards probe over DSoftBus | NOT_RUN | NOT_RUN | The probe is UDP-only and has no DSoftBus deployment runner; A1 candidate DSoftBus evidence is linked from `A1_TYPE_ASSIGNABILITY.md` |
| HDC target enumeration | N/A | N/A | `hdc list targets` was empty; SSH/Tailscale was the executed device path |

The unit-test count is the one measured in this working tree:

```
python -m pytest tests/standards_runner_tests.py -q   ->   93 passed
```

It must be re-measured, not copied from another document, whenever this file is
edited. Three consecutive tester passes found a stale count here.

## Host three-way comparison

Same probe source, same host, from `build/prepatch-214/`,
`build/patched-gate-off/` and `build/patched-gate-on/`:

| Case | Stock 2.14.5 | Patched, gate off | Patched, gate on |
| --- | --- | --- | --- |
| `identical_xcdr2` | PASS | PASS | PASS |
| `identical_xcdr1` | PASS | PASS | PASS |
| `appendable_added_member` | **FAIL** | **FAIL** | **PASS** |
| `mutable_added_member` | **FAIL** | **FAIL** | **PASS** |
| `incompatible_member` | PASS (rejected) | PASS (rejected) | PASS (rejected) |
| `xml` | PASS (asserted rejection) | PASS (asserted rejection) | PASS (asserted rejection) |
| Overall | FAIL (exit 1) | FAIL (exit 1) | PASS (exit 0) |

## Reserved UDP port ranges, not a flake

Several runs recorded in this directory returned `BLOCKED` on every case with
`peer_alive: false` on both peers and a valid probe result line from each. Two
earlier passes described this as a host flake caused by a lingering participant
from a run on the same domain. **Both parts of that explanation were wrong**,
and it is recorded here as a correction rather than quietly replaced.

The affected runs were on different domains from their predecessors
(the BLOCKED run was `f8-patched-gateon` on 226; the run before it,
`f8-patched-gateoff`, was on 222 and was not BLOCKED), so no preceding run
shared the domain. The confirmation pass reproduced the identical signature
**deterministically** - 226 twice, 228 twice, 231 twice, 232 once - with no
preceding run on those domains, while 219, 222, 225 and 230 succeeded on every attempt.

The cause is the host's reserved UDP port ranges. Fast DDS derives the RTPS
metatraffic port as `7400 + 250 * domain`, and for exactly the failing domains
that port falls inside a Windows excluded UDP range:

| Domain | Metatraffic port | Reserved range | Result |
| --- | --- | --- | --- |
| 219 | 62150 | free | PASS |
| 222 | 62900 | free | PASS |
| 225 | 63650 | free | PASS |
| 226 | 63900 | 63898-63997 | BLOCKED |
| 228 | 64400 | 64398-64497 | BLOCKED |
| 230 | 64900 | free | PASS |
| 231 | 65150 | 65105-65204 | BLOCKED |
| 232 | 65400 | 65316-65415 | BLOCKED |

Eight domains, eight-for-eight correlation, verified against
`netsh int ipv4 show excludedportrange protocol=udp` (21 ranges on this host).

Consequences for anyone reproducing this baseline:

- The failure is reproducible, not intermittent. Retrying the same domain will
  not help; choose a different one.
- Check the host's reserved ranges before changing any code. Every
  `results.json` now records `domain` and `metatraffic_port` so this can be
  checked after the fact.
- `--domain` is capped at 232 by the runner (Fast DDS domain limit), and 231
  and 232 are both reserved on this host, so the top of the range is unusable
  here. The host baseline runs used 219, 222 and 230.
- The probe's `peer_alive` control is what converted this into `BLOCKED`
  rather than a false `FAIL`. An environment fault was reported as an
  environment fault, which is the evidence guard behaving correctly.

## The patch, the gate, and what they do not cover

The stock appendable and mutable failures are retained as pre-patch evidence
and are not converted into passes by the runner. They are closed only on
Fast DDS 2.14.5 carrying
`patches/fastdds/2.14.5/0001-edp-honor-reader-type-consistency-qos.patch`
with `FASTDDS_XTYPES_HONOR_TYPE_CONSISTENCY=1`; with the variable unset the
patched library reproduces the stock failures case for case, which is how the
patch is shown not to change existing behaviour **when the gate is off**.

With the gate **on** the relaxation is wider than appendable and mutable
assignability, and two properties must be read before enabling it:

- it is **process-wide, not per-reader** — every reader in the process has its
  `TypeConsistencyEnforcementQosPolicy` honoured, including readers the
  operator did not intend, such as `rmw_fastrtps` readers in the same process;
- **extensibility kind is not enforced** while it is on, so a reader declaring
  a FINAL struct can match a wider writer, which DDS-XTypes 1.3 forbids. This
  is upstream behaviour, not introduced by the patch, but it is reachable only
  with the gate on.

Both are set out in full in [`ARCHITECTURE.md`](ARCHITECTURE.md). The A1
3.6.2 patch is bounded and separately documented in
[`A1_TYPE_ASSIGNABILITY.md`](A1_TYPE_ASSIGNABILITY.md); it does not claim
full XTypes assignability or alter this retained 2.14.5 comparison.

Patch file identity — all three versions differ only in the explanatory header
and produce the same `EDP.cpp`:

| Version | sha256 |
| --- | --- |
| As built and deployed (`/e/fdds-patched-20261110/2.14.5/BUILD_INFO.txt`) | `7fd2fb0429ddaa30c7b7b8ff5b6594681201f7d0c1ce3a920343390d09c3bba9` |
| After the gate-scope disclosure | `0c581ee6304cedae94808c284cf3614744fe0c1ca5194ce8d1eb726de76a90e9` |
| Current working-tree file, measured | `240a2f8ba37a8825878fd0e1b9b55df2ef58efcebd3b4398f5863911252de503` |

The as-built hash and the current hash are both correct for what they describe;
neither is an error to be corrected to the other.
`build/final-fix-20261110/FINDING.md` holds the proof that the re-applied source
is content-identical to the tree the deployed library was built from.

## The `xml` case is a non-conformance, not a conformance

The `xml` case asserts the **rejection** that both version lines actually
perform. Neither implements an XML data representation, so a green `xml` row is
a **documented non-conformance**: it records that the product does not claim the
DDS-XTypes XML data representation conformance point. "All six cases PASS" must
not be read as XTypes conformance — with the gate on, one of those six passes is
a recorded non-conformance. A future XML implementation turns the case back into
a delivery assertion.

The baseline demonstrates that the probe can distinguish successful delivery,
a live-peer rejection, and an unavailable peer. It does not demonstrate full
XTypes conformance.

## Shared header: compiled, then executed

*Updated 2026-11-11.*

`tests/device_suite/compat.hpp` is shared with `dsb_suite` and `cft_race`, and
both version branches of it are modified in this change set. At the 2026-11-10
gate the state was COMPILED only: all five compilations passed with zero
errors, `cft_race` was fully linked on the host (MSVC 19.44 x64 Release), with
`-fsyntax-only` checks under OHOS SDK 23 clang for `cft_race` and `dsb_suite`
on both Fast DDS branches, and `dsb_suite` had **not** been executed against
the modified header.

That gap is now closed. After the gate, `dsb_suite` was rebuilt from this tree
(so the binaries correspond to the modified `compat.hpp`), deployed to both
boards over the Tailscale SSH channel, and executed over DSoftBus between them
by the unmodified `scripts/release/run_cross_board.py` driven through an
hdc-to-ssh shim: 70 passed, 0 failed on 2.14.5 and again on 3.6.2 (35 per
board per version), `DSB_EXIT=0` on both boards, with 88 `DSB_CASE` lines
retained in each run's `run.log` plus the parsed `result.json`. The nine
same-host-only cases are recorded `NOT_APPLICABLE`, which the runner counts as
a record, not a pass. Route-outage scenarios were NOT run (iptables is denied
over the SSH channel) and keep their earlier evidence; Fast DDS 3.2.5 was not
run for this tree. See `build/dsb-exec-20261110/FINDING.md`. `cft_race`
remains compiled and host-linked only, not executed: `COMPILED != TESTED`
still applies to it.

## What is not implemented

Gated XTypes patches are delivered for Fast DDS 2.14.5 and for the bounded A1
Fast DDS 3.6.2 subset, both default off.
The original product plan covers considerably more than this worktree delivers.
Not started:

- full Fast DDS 3.6.2/XTypes Complete assignability beyond the A1 bounded subset;
- the complete IDL 4.2 frontend and the C and Java language mappings;
- the DDS-WEB gateway;
- the DDS-OPCUA gateway;
- a DSoftBus-backed standards probe (the probe is UDP-only and has no DSoftBus
  deployment runner).

## A2.0 mixed-version XCDR2 repair

The finite A2 repair is present and its final independent tester reports PASS.
The canonical clean LAN-controlled matrix passed all scheduled validations with
no blocked cases; exact case results, commands, hashes and platform details are
in [`build/a2-20261112/final-tester/FINDING.md`](../../build/a2-20261112/final-tester/FINDING.md),
[`summary.json`](../../build/a2-20261112/final-tester/summary.json), and
[`BUILD_INFO.txt`](../../build/a2-20261112/final-tester/BUILD_INFO.txt).
The secure OHOS build is the accepted device build. The earlier overlay and
replay failures remain retained diagnostics and are not retroactively PASS.

The repair is default off and enabled only by the exact
`FASTDDS_XCDR2_DYNAMIC_FRAMING=1` value before type-support construction.
Restart is required to change or roll back the process-wide, read-once gate.
Its supported profile and exclusions are bounded; this result is not full
DynamicData XCDR2 or XTypes conformance, and does not qualify mixed-version
XCDR2 over DSoftBus.

## Historical mixed-version UDP controls: XCDR1 PASS, stock/off XCDR2 FAIL; forced validation stays BLOCKED

*Updated 2026-11-11; retained as historical stock/off evidence after A2.*

The earlier mixed-version exit 139 is **resolved and was not a Fast DDS
defect**. The launcher recorded in
`build/ssh-device-tester-20261110/mixed-original-selector.json` compares the
Fast DDS version with `if($v -eq '214')` while passing `2.14.5`, so the
comparison never matched and the 2.14.5 endpoint was given
`LD_LIBRARY_PATH=.../3.6.2/lib:.../2.14.5/lib`. Both directories ship
`libfastcdr.so.2` under one soname from different builds, 2.2.5 and 2.3.7, so
Fast DDS 2.14.5 loaded Fast CDR 2.3.7 and died in the first complex CDR size
calculation at startup, inside `GetCompleteidObject`. Reproduced 20/20 with the
buggy search order and **no peer process running at all**, against 0/20 for the
same binary with its own library directory alone; the same-version pair passed
15/15. The faultlogger records `cppcrash-23101-1794274018716` and
`cppcrash-23130-1794274027296` are retained as the original evidence. Those
iteration counts come from the run record
`build/crash-rootcause-20261110/FINDING.md`; the raw per-iteration logs behind
them were not pulled off the board.

With isolated library paths and the probe's reader still forcing type
validation, the 2026-11-10 rerun produced no crash in any of eight processes
but established no live-peer control in either direction: the 3.6.2 endpoint
reported `matched: true`, the 2.14.5 endpoint `matched: false`, and the probe
declined to grade the case. That configuration was and remains `BLOCKED`:
2.14.5 `EDP::checkTypeValidation` refuses when the reader sets
`m_force_type_validation` and the peer supplies no legacy
`TypeIdentifier`/`TypeObject`, while 3.6.2 `EDP::valid_matching` never reads
`TypeConsistencyEnforcementQosPolicy`. The verdict stands unchanged for its
own configuration. See `build/mixed-isolated-20261110/FINDING.md`.

The configuration that record named as untested — a reader leaving
`m_force_type_validation` at its default — was executed on 2026-11-11. The
probe now reads `DDS_STANDARDS_FORCE_TYPE_VALIDATION`: with the value exactly
`0` the reader QoS is left untouched at the Fast DDS release default (no
assignment at all), and with the variable absent or any other value it keeps
forcing validation, so all earlier evidence stands for its configuration.
With `=0` on the reader, domain 203, a live peer establishes in both board
directions and the probe grades the case instead of declining:

- **XCDR1 interoperates**: DEVICE_TESTED PASS in both directions,
  `received: 514` with the 2.14.5 writer and 3.6.2 reader, `received: 495`
  the other way, `peer_alive: true`, exit 0. The post-review probe rebuild
  re-ran both XCDR1 directions on domain 204: PASS again, `received: 513`
  then `received: 496`.
- **XCDR2 does not interoperate**: genuine FAIL with a live peer, not
  BLOCKED, in both directions. The matched 3.6.2 reader of a 2.14.5 writer
  records `received: 0`, and the 2.14.5 reader of a 3.6.2 writer receives
  samples that decode to wrong values (`sample value mismatch`, exit 2).
  The observation is recorded at the level of what was measured, delivery
  and value comparison, not as a conformance diagnosis of either release.

Scope: the probe's keyed bounded record, these two releases, UDPv4 between
these boards, stock isolated deployments. It does not cover the patched
2.14.5 library, other types, or DSoftBus. Raw per-peer `.out`/`.err`,
`summary.log` and deployment receipts are retained under
`build/mixed-default-20261111/`; the hashes are in its `FINDING.md` and
`deploy-sha.log`.

## Overall status

| Dimension | State |
| --- | --- |
| Implementation | PRESENT — version-bound baseline probe, evidence runner, bounded IDL generation path, generated C++ roundtrip, OpenHarmony arm64 build path, gated 2.14.5 patches and bounded A1 3.6.2 patch |
| Build | PASS — host MSVC and OHOS arm64, both Fast DDS releases |
| Host Test | PASS — 93/93 unit tests, CTest 1/1 per release, three-way gate comparison reproduced by the final tester |
| RK3588A Test | PASS for the UDPv4 scopes, the patched matrix, and the executed DSoftBus `dsb_suite` on 2.14.5 and 3.6.2 against the modified header (route-outage scenarios not re-run); DSoftBus-backed standards probe NOT_RUN |
| Cross-device Test | PASS for same-version UDPv4, the A1/A2 gated matrices, and the `dsb_suite` runs between both boards; historical stock/off mixed-version UDPv4 XCDR1 PASS and XCDR2 FAIL; forced-validation configuration stays BLOCKED |
| Overall | **NOT VERIFIED** — A2 is evidenced within its bounded profile, but full XTypes, A3/A4, IDL 4.2, DDS-WEB, DDS-OPCUA and a DSoftBus-backed standards probe are not implemented |

`SKIPPED != PASS`, `BLOCKED != PASS`, `IMPLEMENTED != VERIFIED`,
`BUILDS != TESTED`, `HOST_TESTED != DEVICE_TESTED`, `COMPILED != TESTED`.

SSH dual-board logs, hashes, roundtrips, UDP matrix and faultlogger evidence are
retained under `build/ssh-device-tester-20261110/`; see
[`DEVICE_EVIDENCE_20261110.md`](DEVICE_EVIDENCE_20261110.md).
