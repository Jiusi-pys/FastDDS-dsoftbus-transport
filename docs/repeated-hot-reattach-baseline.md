# Repeated hot-reattach baseline

This cumulative checkpoint is relative to `7f40798eb551c0ff89bc956367c1b31ba663855f` (tree `6948c5e27a105e0329f286121b53ec8fe19b8677`). It includes runtime admission-pin invariant protection, selected retiring-view observations, actual repeated P teardown/creation with persistent Q, bounded detailed-frame sampling, formal cycle/sample/lifecycle analysis, and controller/collection/query receipt fixes. It is not DIAGNOSTIC_ONLY or DOCS_ONLY. The final N100 stage itself made no further C++ change or target rebuild.

## Finite device evidence (historical, not rerun for this commit)

| Window | Order / cycles | Run | Actual aggregate writes / takes | Per-side final creates / deletes |
| --- | --- | --- | --- | --- |
| N10 | P first, 10 rebuilds | df9319fdf22ab3d9 | 300 / 300 | P 11/11, Q 1/1 |
| N100 | Q first, 100 rebuilds | eab10c4b776d792c | 3000 / 3000 | P 101/101, Q 1/1 |

Query100 run: `12ceebf2b741302e`. These are two fresh-process windows: not both orders at100, and not one continuous110-cycle process. Both have independently accepted lifecycle/sample/control/loading/exit/recovery receipts.

Common runtime SOURCE390: `8628e57951767cf235b00d6ca836050bfca35eeeb3547e768452d50b6c7c2641`. Common main package: `590bfc9ecbb04c6c0ebfb2c95a6fdd397326a30e2ef67cc2a2348b89cfff2bc3`. N10 executor28: `16342f6a604f810ded4a2cc4f5860e27fec5624819a4a1814b8833cd29f9ed27`; N100 executor28: `f850a31b545d92a67f7c3aad664156286b3d057a30899f1e7c7332b1323570de`. Equal runtime/package does not mean equal executor identities.

N100 r14 contract: `9dc9b83886deac5c62ebddd3f76977c3e6ad3e3a9925fbacbb454c96898c2545`. Independent N100 review: `d9e2a4ae1c449c7a6b943ef66a12557de8706697bcd2f138cd8572aba5bbd6ac`.

## Interpretation boundaries

- A C53 Revoked/NotEntered event is a local pre-SDK cancellation of the selected retiring P. Absent sdk_rc is not zero and is not SDK cancellation success.
- B's Deploying SPDP ingress refusal precedes Binding. It is separate from SendBytes errors and does not prove all refusals harmless or all ingress delivered.
- detailed_cycles=1: every formal sample and required lifecycle receipt is retained; native frame detail is sampled, not a complete per-frame chain.
- Stage resource maxima are not continuous high-water marks. Final zero means the reported local active counters, not all allocator memory, SDK physical resources or long-term leak freedom.
- P0/PF/P4 and default NativeBackendUnavailable remain unchanged. SDK epoch/drain/cancellation remain UNPROVEN. QualificationProfile was not generated; there is no production or arbitrary-fault qualification.

## Cumulative provenance and preserved failures

The admission-pin guard is real runtime code. The earlier no-capture-pin mutation removed acquisition but retained release, so it demonstrated an underflow/hang, not double settlement. An invalid removal-of-reset mutation survived; the genuine duplicate decrement and later fail-fast guard have their own receipts. T-B generated text exercises the actual parser, while real-emitter replay is a separate evidence layer. Old failed N10s, the r12 consumed-query postrun failure, and partially unknown historical SIGTERM causality remain recorded; later success does not rewrite them.

Under local artifact root `.artifacts/public-socket-shared-reattach/run-20260927T113439Z/`, retained entry points are `claude-r9/FINAL_REPORT.md`, `codex-resume/FINAL_REPORT.md`, `offline-c6/FINAL_REPORT.md`, `observed-retry/FINAL_REPORT.md`, `query-replay/FINAL_REPORT.md`, and `final-n100/FINAL_REPORT.md` plus its evidence index/review. These artifacts and external SDK/DSOs/builds do not come with a clone. This document is an index, not a complete evidence backup.

The new versioned task templates and explicit private-configuration wiring are separate delivery identities. Materialization does not create authorization. They are verified locally; historical boards did not run those new paths. Current README/PLAN additions are post-run documentation, not bytes retroactively included in SOURCE390.

## Local checkpoint validation

The isolated candidate's standard CONFIG Host result is **523/523 PASS** (valid r1
CONFIG evidence reused at its original source correspondence). Actual Host100 replay is
**25 PASS** and target-core W2 replay is **5 PASS**. The final tool-closure tester ran
11 commands with **Flow21 PASS**; its scope is local checkpoint closure, not device or
production acceptance. The 17 materialized task members plus 2 exact external members
are bound and protected; this 17+2 closure is a local promotion path, not a historical
device execution path.

The internal 9 historical RealText cases remain `SKIP`, and DLL allocator injection was
not triggered. Initial invalid-environment logs remain preserved. The old ASan unique
220-case result is reused only at its original revision and instrumentation boundary;
it is not a new ASan run and does not cover uninstrumented external dependencies.

No device operation occurred in this checkpoint (`HDC calls=0`). N10/N100 finite
evidence remains limited to its separately identified historical windows; historical
failures and their boundaries are unchanged. Default `NativeBackendUnavailable`,
P0/PF/P4 and production restrictions remain in force. The candidate review closed
CP-1, CP-2, CP-3 and the external formal-analyzer dependency finding with no open
actionable findings. This document records evidence; it does not authorize commit,
merge, device execution, or production enablement.
