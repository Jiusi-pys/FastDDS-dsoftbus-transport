# A1.0 authorization checkpoint and A1 bounded type assignability

Authorization checkpoint: 2026-11-11
Documentation sync: 2026-11-12

The A1.0 label records the approved architecture checkpoint; this document
records the resulting bounded A1 implementation milestone from
[`STANDARDS_REMAINING_SCOPE_PLAN.md`](../STANDARDS_REMAINING_SCOPE_PLAN.md).
It does not mark B, C, D, or full XTypes Complete as complete. The earlier
mixed-version XCDR2 failure stated below is the stock/gate-off result; the
later finite A2 repair is documented in
[`A2_XCDR2_INTEROPERABILITY.md`](A2_XCDR2_INTEROPERABILITY.md).

## Delivered boundary

The versioned out-of-tree patch
`patches/fastdds/3.6.2/0001-edp-type-assignability.patch` adds a
default-off, process-wide opt-in for the exact environment value
`FASTDDS_XTYPES_HONOR_TYPE_CONSISTENCY=1`. Unset and other values retain stock
matching decisions. The patch does not modify Transport or use DSoftBus APIs.
The user approved this specific A1.0 exception on 2026-11-11; that approval is
not a general authorization for further Fast DDS changes.

The supported subset is deliberately bounded: non-inherited flat structs with
at most 256 members in each non-equivalent definition; corresponding ID/name
or name-hash members; equal extensibility; final and appendable
ordering/member-set rules; required, key, and non-optional writer-only
widening checks; and equivalent member types or same-width bounded string
identifiers. Unsupported or non-equivalent cases fail closed. Mutable
optional/key-flag differences remain conservatively rejected. The patch does
not claim numeric coercion, alias expansion, recursive evolution, unions,
differing collection bounds, or small/large string conversions, and is not a
complete XTypes implementation.

## Evidence state

| Dimension | A1 state |
| --- | --- |
| Implementation | PRESENT for the bounded 3.6.2 patch, probe, runner, and contracts |
| Build | PASS for the final host and OHOS rebuild/install |
| Host test | PASS for the final Python, contract, gate-on, and generated-roundtrip evidence |
| RK3588A/device test | PASS for the final gate-on UDP and DSoftBus candidate runs on both boards |
| Review | RESOLVED; no new actionable findings in `build/a1-36/review/RESOLUTION.md` |
| Final tester commit-gate evidence | PASS in the final tester record; this is not the overall project PRE_COMMIT decision |
| Product scope | NOT VERIFIED; broader XTypes and product milestones remain outside A1 |

Authoritative execution details, commands, exit codes, deployment roots, and
artifact identities are retained in
[`FINDING.md`](../../build/a1-36/final-tester/FINDING.md),
[`BUILD_INFO.txt`](../../build/a1-36/final-tester/BUILD_INFO.txt), and
[`summary.json`](../../build/a1-36/final-tester/summary.json). Review records
are [`REPORT.md`](../../build/a1-36/review/REPORT.md) and
[`RESOLUTION.md`](../../build/a1-36/review/RESOLUTION.md).

## Operational and verification limits

Set the gate before DDS threads start. It is sampled once per process, so it
also affects every reader in that process. Unsetting the variable and using
the stock library is the rollback path; because the gate is read once, restart
the process after changing or unsetting it. The final tester did not repeat the
2.14.5 compatibility probe, device gate-off matrix, invalid-gate control, or
DSoftBus route fault-injection after the review fix; the final record explains
why those retained results remain bounded historical evidence. The retained
stock/gate-off mixed-version XCDR2 result remains a failure, and
forced-validation mixed-version runs remain blocked. A2's separately gated
Fast DDS 2.14.5 framing repair has its own evidence and does not expand A1's
assignability scope. The new standards probe remains UDP-only rather than a
DSoftBus-backed standards transport.
