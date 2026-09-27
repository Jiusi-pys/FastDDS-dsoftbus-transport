# Bounded public Socket DDS baseline

This baseline combines the implementation in A (`new-public-socket-bounded-dds/run-20260927T014310Z`) and B (`public-socket-decision-close/run-20260927T083510Z`). Their historical status/HEAD/package receipts remain unchanged. The current isolated checkpoint (`bounded-dds-stabilization-checkpoint/run-20260927T103431Z`) independently built the frozen candidate and executed the full CONFIG Host suite: `445/445 PASS`, exit 0. No device execution or new ASan run was performed; this records current evidence and does not make an old device or ASan run an execution of this checkpoint.

The current tester receipt is `.artifacts/bounded-dds-stabilization-checkpoint/run-20260927T103431Z/tester-r2/TEST_RESULT.json` (SHA-256 `b44735d37a4bcad5a81518457692bbb0a826c53f0d2fb47539aa09ad292b39b1`), with full receipt SHA-256 `07c7fc5ba4db1f0765cadee8beca900443be8b4eaf5fa24cadca370c8a1e91ed`. The actual configure argv and declared external dependency identities are recorded in `.artifacts/bounded-dds-stabilization-checkpoint/run-20260927T103431Z/DECLARED_DEPENDENCIES.json` (SHA-256 `21aa10087035b45c4aea209e76f79ae0c4483532f14a1850d2a508a488f27f4a`); the build used the isolated candidate source, Fast DDS `343f155c36b561db3d9f047a65d866ce0f3da300`, communication_dsoftbus `5518313ee70e79f788fc4d9f9a9310c8b24827cc`, and declared prebuilt host libraries.

## Included behavior and evidence

A contains seven bounded new-path fixtures and a real CloseBusy responsibility repair: retain the original owner/namespace while waiting outside gates for the single close credit, settle that original responsibility once, retain cleanup errors, and diagnose synchronous self-wait without forbidding unrelated-table destruction. Its deterministic Host tests and reviews remain part of the baseline.

B adds SDK-free value-only ReceiveDecision observations through the existing observer, exact branch/result/known-field accounting, a bounded recorder and negative analyzer tests. Protected state is captured at the actual decision gate and reported after unlocking. B does not change the CloseBusy algorithm, admission predicates, queue capacity, scheduling or retries. Synchronous observation costs time and can extend an armed admission; it is not a zero-cost performance baseline.

| Parameter | Unit / scope |
|---|---|
| 8192 | maximum native transport record bytes in the device profiles |
| 32 | local receive record-count budget |
| 16384 | opt-in decision detail rows per process |
| 5243440 | observed decision-store bytes/process on tested builds, not SDK total memory |

Decision recording defaults OFF (`BUILD_PUBLIC_SOCKET_DECISION_DIAGNOSTICS`). The explicit bounded experimental build does not enable the normal production factory. Default `NativeBackendUnavailable`, P0/PF-01/PF-02 BLOCKED / NOT QUALIFIED and unreleased P4 are unchanged. No QualificationProfile exists.

## Different runs, different accounting

- A verified F256, F1M, REL_LOSS, BE_LOSS, VOL_LATE, TL_LATE and KEYED_LAST in seven fresh-process two-board windows. A's old F1M111 Binding refusals are an aggregate without per-decision causes; the old six adapter InvalidState rows also lack directly observed causes. They are not117 unique lost packets.
- B verified F1M and both small-DDS deletion orders in three fresh-process windows:23 Reader rows,5 DDS wait API operations and12 board guards including its three separate queries. Wait calls are not network ACK counts.
- B's107 new Binding refusals are directly recorded CAPTURE/LOCAL_RECORDS, with local_records32/limit32. Two new adapter refusals are observed Deploying-phase decisions. These observations never fill old111/6 causes, do not count DDS samples, and are not made harmless by eventual Reader success.
- Host barriers verify CloseBusy responsibility retention and settlement; device windows verify ordinary public-DDS deletion. Device activation of CloseBusy was not established.

Historical tests are separately scoped: A full438/438 and affected ASan169/169; B canonical full443/443, final affected Host72/72, affected ASan72/72, analyzer20/20 and profile27/27. B's final discovery445 was not a full445 execution. Prebuilt Fast DDS was not ASan-instrumented; TSan was not run. This checkpoint's isolated full CONFIG Host receipt is local under `.artifacts/bounded-dds-stabilization-checkpoint/run-20260927T103431Z/`; its final numbers are recorded by the independent tester, not prefilled from history. Device execution in this checkpoint is NOT_RUN. The historical B ASan result remains source-equivalent evidence only and is not a new sanitizer run for this checkpoint.

## Build and external dependencies

Use a fresh Release/CONFIG build with MSVC x64 and the declared prebuilt Fast DDS/fastcdr/foonathan packages. Enable the current model/Binding/Host-provider/public-Socket-Fake/DDS-contract targets and `BUILD_PUBLIC_SOCKET_BOUNDED_REGRESSION`; explicitly enable decision diagnostics when testing their output. The complete actual configure argv and dependency hashes are in this checkpoint's DECLARED_DEPENDENCIES receipt. Prebuilt dependencies are Release-only; do not mix an incompatible Debug runtime.

Fast DDS source is fixed at343f155c36b561db3d9f047a65d866ce0f3da300 /3.6.2.0. Public Socket headers used by the verified packages came from authorized communication_dsoftbus5518313ee70e79f788fc4d9f9a9310c8b24827cc, not installed SDK-bundled headers. The root gitlink remains its recorded baseline value; a plain submodule checkout is not silently equivalent to that external authorized header input. SDK23/compiler, source headers, platform libraries and target ELF are distinct identities. No source-to-firmware/full ABI mapping is established.

## Tools and external evidence

Reusable regression and decision analyzers/tests are versioned under `scripts/public_socket` and `tests`. B's `analysis-small-profile` is a historical, package-bound offline derivative: four expected trace-capacity literals select the already-compiled16384/16384 profile instead of the older4096/1024 profile. Its wrapper pins main/source/manifest identities and retains all count, ordering, overflow, Reader, identity and guard checks. It is not a general build/test dependency and remains in B's artifact with hashes in [the evidence index](BOUNDED_DDS_STABILIZATION_EVIDENCE.json).

Raw logs, packages, permission inputs, per-run executors and that historical derivative are external local evidence. A Git clone does not include them or grant device execution permission. This checkpoint does not add device identities to reusable executors or authorize remote disclosure.

SDK internal settlement, cancellation, original-event identity, record continuity, full ABI, arbitrary topology/all QoS, same-process native hot rebuild and production qualification remain unproved. No queue enlargement, control-traffic prioritization, tuning, SDK/provider enablement or new device work is part of this checkpoint.

The current graph disposition retains dynamic and truncation limits: 27 code paths are covered by the persisted graph, while unresolved cross-language and omitted-callee coverage is not treated as graph-safe. This checkpoint versions the cumulative implementation and evidence described above; it introduces no new qualification framework or device claim.
