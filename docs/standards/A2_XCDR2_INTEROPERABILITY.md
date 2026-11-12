# A2.0 bounded XCDR2 interoperability

Documentation sync: 2026-11-12

A2.0 is an approved, finite Fast DDS 2.14.5 repair for the recorded mixed
XCDR2 framing divergence. It is not a claim of general DynamicData XCDR2 or
XTypes conformance, and it does not complete the broader standards product.

## Evidence state

| Dimension | A2 state |
| --- | --- |
| Implementation | PRESENT: `patches/fastdds/2.14.5/0002-dynamic-xcdr2-framing.patch`, XCDR2 contract, matrix runner and tests |
| Build | PASS: host SECURITY=OFF and canonical OHOS arm64 SECURITY=ON build/install |
| Host test | PASS: existing runner suite, contract oracle, and gate contract evidence; exact commands and measured counts are in `build/a2-20261112/final-tester/FINDING.md` and `summary.json` |
| RK3588A/device test | PASS: both RK3588A boards, with the secure canonical library and board contracts |
| Cross-device matrix | PASS for the canonical LAN-controlled matrix; the retained pre-LAN overlay failures remain diagnostic evidence |
| Review | RESOLVED: the receipt-identity finding is recorded in `build/a2-20261112/review/REPORT.md` and `RESOLUTION.md` |
| Commit gate | PASS in the final tester record; the repository PRE_COMMIT decision remains Main/Orchestrator-owned |
| Product scope | NOT VERIFIED: full XTypes, A3/A4, IDL 4.2, DDS-WEB, DDS-OPCUA and a DSoftBus-backed standards probe remain outside this milestone |

The authoritative final execution record is
[`FINDING.md`](../../build/a2-20261112/final-tester/FINDING.md), with
machine-readable status in [`summary.json`](../../build/a2-20261112/final-tester/summary.json)
and build identity in [`BUILD_INFO.txt`](../../build/a2-20261112/final-tester/BUILD_INFO.txt).
The approved scope and actual root cause are retained in
[`AUTHORIZATION.md`](../../build/a2-20261112/AUTHORIZATION.md) and
[`ROOT_CAUSE.md`](../../build/a2-20261112/ROOT_CAUSE.md).

## Activation and rollback

The repair is default off. Enable it only with the exact process-start value
`FASTDDS_XCDR2_DYNAMIC_FRAMING=1`, before constructing the affected
`DynamicData` type support. The setting is process-wide and read once; restart
the process to change or roll back. Unset or other values use the stock path.
The existing A1 gate is independent.

The optional LAN SSH configuration is task-local only. It uses an existing
OpenSSH config with strict host-key checking, verifies the file before use, and
records its digest in the final evidence; it does not modify global SSH
configuration. The board addresses and aliases are dated test evidence, not a
permanent deployment guarantee.

## Supported boundary and exclusions

The delivered codec is intentionally bounded to non-inherited, unkeyed,
non-empty flat APPENDABLE DynamicData with continuous member IDs matching
declaration indices and at most 256 members. The usable scalar/string profile
is `uint32` plus 8-bit strings (`TK_STRING8`) with bounds from 1 through 65535, with no
embedded NUL. FINAL and MUTABLE roots, key/optional/MUST_UNDERSTAND and
non-serialized members, collections, and other unsupported kinds are rejected
explicitly. Unknown trailing writer fields may be skipped, while missing local
fields fail; this is not a general schema-evolution or default-initialization
implementation. XCDR1 wire behavior is unchanged. With the new gate enabled,
the extra DHEADER capacity is reserved even for XCDR1/default size-provider
calls because the legacy Writer omits representation there; stock and
gate-off size behavior remains unchanged.

The patch is a Fast DDS patch, not a Fast CDR, Transport, RTPS reliability,
discovery, Writer/Reader or QoS source change. Same-version DSoftBus regression
coverage remains separate; the A2 standards matrix is UDP-based and does not
prove mixed-version XCDR2 over DSoftBus.

## Historical controls retained

The earlier stock and gate-off mixed-version XCDR2 failures remain valid
controls and are not rewritten as passes. The earlier overlay matrix failures
and replay timeouts remain under `build/a2-20261112/final-tester/`; the clean
canonical LAN run is the final acceptance record. Forced type-validation mixed
configuration remains BLOCKED and outside A2. Route-fault injection was not
run because iptables authority was unavailable.

A2 does not alter the prior A1 scope: A1's mixed-version XCDR2 statement is
qualified as stock/off evidence and links here for the later finite repair.
