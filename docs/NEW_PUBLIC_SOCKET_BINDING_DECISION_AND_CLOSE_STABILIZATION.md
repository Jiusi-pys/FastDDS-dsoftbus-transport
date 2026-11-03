# Public Socket binding decision and close stabilization

Date: 2026-09-27
Artifact run: `20260927T083510Z`
Base HEAD: `6943b6c84ca2093f433ece2c81ed4f966354b936`

## Status

`CLOSED_WITH_HISTORICAL_LIMITATION / CHECKPOINT_READY / NOT_COMMITTED`

The scoped functional gate and independent review are PASS. This closes the bounded source, host, analyzer, package-audit and three-window device observation work package. It does not approve a Git commit, SDK qualification, release, production readiness or full Transport qualification.

## Evidence

- Source freeze: `698a06787e5c33380e4bc5b93c0a076a6c9eca84f8853046248de5a2137e4240`.
- Target package main ELF: `db1b6ef915aa820bbcf4392fe0840751da30dbffa6b9cb187f0379fd982aa65d`.
- F1M: three 1 MiB Reader samples, 131 fragments per sample, delete B then A; A had 107 `CAPTURE/LOCAL_RECORDS` binding refusals and B had one `ADAPTER_PHASE` rejection.
- D1: five Reader samples per direction, delete B then A; no binding refusal or adapter rejection.
- D2: five Reader samples per direction, delete A then B; B had one `ADAPTER_PHASE` rejection and no binding refusal.
- All six decision scopes were `COMPLETE_DECISION_ACCOUNTING` with overflow 0. There were 23 Reader rows, 5 DDS wait API operations, and 0 data retries. These are distinct observations and do not establish a packet-loss count.
- Canonical Host `443/443`, affected Host `72/72`, affected ASan `72/72`, analyzer `20/20`, bounded profile `27/27`, target build and static audit: PASS.
- Device query/data guards: 12 PASS across the three query and three data windows.

## Preserved limitations

The 111 value is an aggregate of binding admission refusal decisions; individual event details and causes were not recorded. The six adapter rows remain separate local `InvalidState` observations. No new observation is assigned to either historical set. There is no evidence for 107+2 physical drops, harmlessness, or delivery inference. Device runs show ordinary close/deletion behavior but do not establish that the CloseBusy branch was physically hit. SDK settlement, cancellation, callback quiescence, full Transport qualification, performance sufficiency, complete firmware ABI, and QualificationProfile remain unverified. QualificationProfile was not generated. The prebuilt Fast DDS used by ASan was not fully instrumented; TSan was not run. Final discovery reported 445 tests, but a complete 445-test execution was not claimed. Target execution and device validation were skipped in the independent static audit; the separate device evidence is retained as the runtime record.

The added `ReceiveDecision` is SDK-free and value-only. Values are captured at the actual decision points; protected state is snapshotted under its applicable gate. The existing observer is invoked after unlocking and does not look up current mappings. Diagnostics default OFF, with 16384 rows per process, reported storage of 5243440 bytes, reason aggregates and overflow counters. This instrumentation has non-zero overhead and is not SDK drain evidence. The CloseBusy algorithm is unchanged; inherited original owner/namespace and single-credit retention were verified by deterministic Host tests, while no device CloseBusy hit is claimed. Local raw artifacts and the package-specific profile helper are task-local and are not automatically included by a Git clone.

Retained process failures include the initial loader failure, wrong route preparation, the old analyzer's 4096/1024 capacity mismatch, projection/audit failures, an unnecessary full rerun, and the corrected false yield-cutoff alarm. The bounded profile derivative changed only four expected capacity literals to 16384; it did not change runtime, package, source behavior, or device execution.

## Gates

P0/PF-01/PF-02 remain `BLOCKED / NOT QUALIFIED`; P4 is not released; production default is `NativeBackendUnavailable`. There is no QualificationProfile. No HDC, commit, push or merge action is authorized by this document.

## Canonical evidence

The authoritative evidence is under `.artifacts/public-socket-decision-close/run-20260927T083510Z/`. The write manifest and documentation synchronization receipt identify the exact inputs and write allowlist.
