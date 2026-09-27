# New Public Socket bounded DDS regression

Status: **`NEW_PUBLIC_SOCKET_BOUNDED_DDS_REGRESSION_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**
Artifact: `.artifacts/new-public-socket-bounded-dds/run-20260927T014310Z`

This package closes seven bounded functional profiles over the current Public
Socket DDS path. It is a functional checkpoint, not a pre-commit, production,
SDK-quiescence, or general Transport qualification gate.

## Executed cases

| Case | Run | Writes / Reader takes | Observed result |
| --- | --- | ---: | --- |
| F256 | `f0dcbb1b81ea2802` | 3 / 3 | 256 KiB samples; 33 fragments each |
| F1M | `116a5436ce25660d` | 3 / 3 | 1 MiB samples; 131 fragments each |
| REL_LOSS | `12527a6dcb7a8bc0` | 3 / 3 | One exact drop; NACK_FRAG and admitted repair |
| BE_LOSS | `139c9e073e453deb` | 3 / 2 | Sample 1 absent in the full post-drop window; 2/3 complete |
| VOL_LATE | `f23f0bbe053f9aa7` | 6 / 3 | Only new samples 4/5/6 after late Reader creation |
| TL_LATE | `29b518965ed7cb94` | 6 / 6 | Historical 1/2/3 retained, then new 4/5/6 |
| KEYED_LAST | `34e7b70353655874` | 10 / 6 | Each key retained historical 3/4, then new 5 |

Each data case used a fresh process and one Participant per board, with private
meta/user locator channels and the case's frozen QoS/resource profile. Seven
additional guarded queries completed. The combined result is 14 completed
windows, 28 board-window guards, 26 valid formal Reader rows, and zero data
retries. Every wrapper, HDC and remote result was zero; normal public DDS
deletion was B→A.

## Evidence and implementation boundary

The observed path is:

```text
public DDS factory
  -> current DSoftBusTransport isolated façade
  -> NativeSendExecution
  -> PublicSocketBackend / public Socket API
  -> remote accepted callback
  -> ProviderAdapter / ServiceRecordBinding
  -> owned-copy / receiver lease
  -> DataReader
```

Fast DDS supplied fragmentation. Actual fragment size was `8016` bytes and the
native record bound remained `8192`; no new transport fragmentation protocol or
larger runtime queue was introduced. The target package had six members and
main SHA-256 `091c259dbc1cec586d21879ea66ce24f18d9898945b5a20382200ba2b9a983ad`.
The R4 source freeze SHA-256 was
`5919183eb491d76b0ad8058e10bce3275bbe88c8f09b327335991c428db89999`.
The source headers, API23 toolchain, platform ELF and firmware ABI remain
separate identities; source-to-firmware/full ABI equivalence is not established.

## Host and review evidence

- Final full Host: `438/438 PASS`, exit 0.
- Affected clean-environment ASan: `169/169 PASS`, exit 0.
- The initial ASan environment timeout remains preserved; clean PATH/module
  configuration resolved startup. This does not claim the initial invocation
  passed.
- Prebuilt Fast DDS dependencies were not all ASan-instrumented; TSan was not run.
- Final independent review recomputed all seven analyzers, 14 load analyses,
  14 window restorations, 28 board guards, 56 raw before/after metadata
  observations, 26 Reader rows, and all 316 source / 1,528 target input hashes.
  No unresolved blocking finding remains for these seven profiles.

## Loss, admission, and close-credit findings

REL_LOSS records a compatible same-run logical chain: selected DATA_FRAG drop,
missing-range NACK_FRAG, admitted repair send, and later ingress/owned/running
observations. These are separate local counters, not a unique physical packet
identity or cross-board latency measurement.

F1M A has a Binding `refused_receive` increase of `111` from MATCH to DATA_DONE.
The individual reasons and record IDs are `UNOBSERVED`; this is a Binding
aggregate, not 111 SDK errors. Six other windows have one B setup
`OnBytesRejected` event, local `BackendStatus::InvalidState`, with the precise
phase also `UNOBSERVED`. These observations limit ingress accounting. They are
not relabeled as capacity drops, harmless outcomes, or proof of loss-free
delivery; the 26 formal Reader chains remain independently valid.

The shared close-credit race was fixed and re-tested. Binding retains the
original owner/namespace while waiting outside gates for the unchanged single
close credit. Provider callback reentry refuses blocking control; precise
destructor self-wait diagnoses the same held table credit, while destruction of
an unrelated native table remains legal. This is a lifecycle safety correction,
not an SDK drain, cancellation, or quiescence guarantee.

`SendBytes` returns were zero where recorded. DDS wait operations are DDS API
operations, not ACK packet counts. DSoftBus `Shutdown` is void; no SDK rc0 is
claimed. Terminal notifications, local adapter rejection, and SDK settlement
remain distinct observations.

## Remaining boundaries

The default production factory remains `NativeBackendUnavailable`. P0,
PF-01 and PF-02 remain `BLOCKED / NOT QUALIFIED`; P4 is unreleased and no
QualificationProfile exists. This package does not qualify arbitrary QoS or
topology, same-process hot rebuild, RAW/mixed/ROS2, endurance or performance,
full `TransportInterface`, complete firmware ABI, SDK internal settlement,
cancellation, quiescence, or production readiness. Historical packages and
their original evidence retain their own scope.

The final report, final review, evidence-summary ledgers, per-window raw logs
and guards are the authoritative records for this checkpoint.
