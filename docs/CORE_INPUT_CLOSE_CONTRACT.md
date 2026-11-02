# Core input close contract and bounded evidence

The current diagnosis is **NO_CONFIRMED_VALID_USE_BUG**. Production implementation is unchanged; no normal-behavior fix is claimed. Supported external close behavior has bounded host evidence. This is not comprehensive proof for arbitrary external/custom receivers, public-factory runtime, SDK callbacks or device communication.

## Caller contract

[Facade input-control precondition](../include/fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp#L65) and [InputChannels close precondition](../include/fastdds_dsoftbus/dsoftbus/InputChannels.hpp#L116) already prohibit Open/Close/shutdown/destruction on **any synchronous receiver callback stack**, including another channel. Objects must remain alive throughout member calls. External control must let callbacks return instead of synchronously reentering control from them.

[Receiver delivery](../src/InputChannels.cpp#L100) retains its in-flight lease through `OnDataReceived`; [Close](../src/InputChannels.cpp#L217) captures the registration incarnation, waits for its leases to drain, clears the receiver and erases by entry identity. The [facade core path](../src/DSoftBusTransport.cpp#L248) performs synchronous close under its control lock. A callback closing itself would wait on its own lease; callback reentry while external close holds control can form a cycle. Those are prohibited caller patterns, not new supported-use regression requirements. The explicit HostBackend route has a [defensive rejection before its outer lock](../src/DSoftBusTransport.cpp#L229); that defense does not extend the pure-core contract into a reentrancy promise.

The standard Fast DDS receiver holds its mutex through synchronous message processing and takes the same mutex for unregistering. Its transport requirement is safe Close while Receive runs on **another thread**, not same-stack shutdown. Official v2.14.5 [TransportInterface](https://raw.githubusercontent.com/eProsima/Fast-DDS/v2.14.5/include/fastdds/rtps/transport/TransportInterface.h#L160) and [ReceiverResource](https://raw.githubusercontent.com/eProsima/Fast-DDS/v2.14.5/src/cpp/rtps/network/ReceiverResource.cpp) were compared statically; no2.14 execution or binary compatibility certification is claimed.

## Execution reachability and results

The probe uses the private friend-injected HostModelTag core/model with a generation source, linked through installed CONFIG3.6.2.0, without HostBackend. The [public constructor](../src/DSoftBusTransport.cpp#L13) selects NativeUnavailable; the [descriptor factory](../src/DSoftBusTransportDescriptor.cpp#L103) calls that constructor, and [init](../src/DSoftBusTransport.cpp#L116) rejects it. Private model construction requires generations. The explicit Socket experiment/backend route is distinct. This source inspection does not establish a legal default-production deadlock or execute the public factory/native SDK path.

Canonical `build/core-input-close-contract-20261102-d01/FINAL_TEST_RESULT_D02.json` records:

| Case | Classification | Actual result |
| --- | --- | --- |
| normal-repeat-reopen | Legal control | Windows process exit 0; old ticket remains closed after reopen |
| external-close-drains | Legal control | Windows process exit 0; close observed pending until another thread's callback returned |
| callback-exception | Legal control | Windows process exit 0; callback failure releases in-flight lease before close |
| forbidden-self-close | Prohibited-contract diagnostic | own watchdog process exit91 after about2 seconds, exact synchronization marker |
| forbidden-close-cycle | Prohibited-contract diagnostic | own watchdog process exit92 after about2 seconds, exact cycle synchronization markers |

Thus three legal controls passed and two forbidden diagnostic oracles were met; these are not five supported-function passes. Parent emergency kills:0. Astra formal turn01a19b71-adc7-75c1-be97-00dc30a4bc0b and supplement01a19b73-af85-7c22-8614-0632b5fbae25 concluded no confirmed valid-use bug and recommended no production change (`REVIEW_CLOSURE_D01.json`).

## Evidence limits and retention

`PLAN_D01.json` and `CONTRACT_BASIS_D01.json` bind source HEAD `a7152c60dfc4fd21b54ba571f8d5480f687c9f64` and fixture/source pins. Probe EXE SHA256 is `d8cb54dfe62e4595afb281c62c3575a9d6a88d3b26e97e90bc1727c4199de437`. Initial configure exited0; the original build exit remains UNKNOWN after the GBK capture failure (`INITIAL_TEST_RESULT_D01.json`, `RECOVERY_D01.json`). Bytes-capture recovery records a separate incremental build exit0 that only relinked the EXE (`INITIAL_TEST_RESULT_D02.json`). Final D01 was BLOCKED before execution by a generated link-tracking hash; `DEPENDENCY_RECONCILIATION_D01.json` explains and binds that relink metadata without pretending the unavailable old tracking contents were diffed. Fresh final D02 then executed without rebuilding. Failures and blocked evidence remain preserved.

Artifacts remain local in ignored `build/core-input-close-contract-20261102-d01/` and `E:/Codex-FastDDS-core-input-close-20261102-d01`; about14MiB is below the128MiB budget. They are diagnostic retention, not a new execution package or reusable blanket host proof. Final evidence records unchanged source/fixture pins and protected68 baseline metadata. This documentation step reran no tests and changed no production implementation, fixture or baseline file.

SDK physical callback quiescence, drain and cancellation remain unknown; this component lease-drain evidence cannot establish them. No HDC/device/real-output/SDK execution or persistent configuration/security/network changes occurred. The HDC vendor path remains BLOCKED_MISSING_VENDOR_PROVENANCE with no further reads. DEVICE_TESTED=false; Stage1 NOT_VERIFIED; Stage2 HELD. Documentation readiness covers this scoped evidence note only; Main owns publication after its scoped gate, and this document is not a commit/push receipt.
