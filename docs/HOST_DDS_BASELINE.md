# SDK-free Host DDS integration baseline

This versioned baseline records cumulative Host management and integration work after root commit `145d0ee97de5e8728a985550da82f169c640d89e` (historical experiment baseline `2cb5beebde8aa41b01fecf0a5cc41164f133eed5`). It is not production/native qualification. No self commit hash is embedded here.

## Verified bounded scope

- ServiceRecordTable/ServiceRecordBinding composition, repaired sink lifetime and shared registration, execution-context enforcement, callback close request/deferred control close.
- Original NativeSendExecution tickets and ProviderAdapter receive path through owned-copy and original application/receiver leases.
- Explicit Host-injected actual DSoftBusTransport, independent exact-channel Close, safe reopen, whole shutdown. One local/one dial; controls externally serialized; callback synchronous destruction forbidden.
- Actual Fast DDS factory/ReceiverResource, SPDP/SEDP, current remote GUID matches, Reader takes and Reader recreation, both Participant deletion orders, and a mock forwarding-disabled negative control. The test descriptor is local to the test program; no real SDK backend is selected.

The latest actual CONFIG Host receipt is **346/346 PASS**. Its independent ASan execution is **new Participant9/9**, **input-close14/14**, **provider-loop15/15**; these overlap the ordinary suite and are not summed into an independent test total. Prebuilt Fast DDS/Fast CDR/foonathan DLLs were not instrumented. Normal integration scenes associate25 actual Reader takes with same-run Host model GUID/sequence observations. A100ms post-ACK window is bounded observation only.

**D5:** Fast DDS returns a non-null Participant although input Bind is rejected. Public DDS API deletion cleans up that attempted instance; the already live instance retains its verified open state, receiver identity and provider ownership. This is not an all-or-nothing creation guarantee, not a null return, and not proof that every input is Ready when a Participant pointer exists.

## Boundaries

Production/default constructors remain `NativeBackendUnavailable` (`NativeUnavailable` mode). P0/PF-01/PF-02 remain BLOCKED / NOT QUALIFIED; P4 is not released and no QualificationProfile exists. Real DSoftBus SDK provider, new management path on devices, original SDK-event identity, quiescence/cancellation, arbitrary topology/all QoS/full TransportInterface qualification are unverified. Historical device results remain bound to their old experiment sources/packages, not this new Host path. SDK physical settlement and TSan race freedom are not established.

Historical HUMAN_REQUIRED review records are preserved: human merge decisions are not cleared by a local checkpoint commit. This baseline is SDK-free and does not authorize merge, push or release.

## Build and external dependencies

Use Windows VS2022 x64 Release `/MD`, CMake and the existing Fast DDS3.6.2.0 CONFIG install (fixed source commit343f155c36b561db3d9f047a65d866ce0f3da300). Explicitly set `FASTDDS_DSOFTBUS_FASTDDS_MODE=CONFIG`, `fastdds_DIR`, `fastcdr_DIR`, `foonathan_memory_DIR`, `FASTDDS_SOURCE_DIR`, and `DSOFTBUS_SOURCE_DIR`; the source dirs may be declared external checkouts. Enable `BUILD_DSOFTBUS_TRANSPORT_FACADE`, `BUILD_DSOFTBUS_PROVIDER_ADAPTER_PHASE0`, `BUILD_DSOFTBUS_HOST_PROVIDER_LOOP`, `BUILD_TESTING` and, for the complete historical Host regression, `BUILD_FASTDDS_DSOFTBUS_EXPERIMENT`. Keep `FASTDDS_DSOFTBUS_EXPERIMENT_NATIVE=OFF` and `FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP=OFF`. Run CTest with `-C Release`; provide the matching DLL directory on Windows Path and the standard PowerShell module paths for Get-FileHash tests.

`HostProviderLoop` remains default OFF. Debug-to-Release ABI substitution is rejected. The full Host suite includes older reference fixtures; their traffic is not evidence for the strict new Host mock path. The new Participant target does not link old ExperimentalTransport. ASan uses Release `/MD /fsanitize=address /Zi`, link `/DEBUG`, with the VS ASan runtime available; this is not an SDK/whole-dependency sanitizer proof.

The Git tree keeps the recorded Fast-DDS and communication_dsoftbus gitlinks. No SDK binaries, installed libraries, local subrepo modifications or .artifacts content is vendored. An isolated-candidate build receipt declares the actual external source/library locations and hashes. Subrepo local changes are not part of the root checkpoint.

## External local evidence index

The following paths are local evidence outside Git. A clone does **not** contain these raw logs, receipts, builds or failures. SHA256 values identify the referenced files without rewriting old HEAD/source/gate records. The local checkpoint execution receipt is separate and is not another commit.

| Local path | SHA256 |
|---|---|
| `.artifacts/srtc/README.md` | `0bc1c3b161f90bedb96860efa1eca6245dd4db20392b0a3331dcc3e08db6046c` |
| `.artifacts/srtc/COMPOSITION_MAP.md` | `6a64e0014df573db7b17a6c95033a85523dc918dd8be8228bd537beb4e829db2` |
| `.artifacts/srtc-repair/README.md` | `80c7df27ecbd86b5f39ec732f9653eb1ac9d0cabc5532beb592d27966cb61ba9` |
| `.artifacts/srtc-repair/RECEIPTS.md` | `4df08c74961d7fff0539a3f39b85d2cec0232279404f5d9cd4f317cd56eb962a` |
| `.artifacts/srtc-repair/SOURCE_HASHES.txt` | `c2fbddfd9904831f5e8f97c44e74c0242ea946b8ec2723f0ef76f9de8447828d` |
| `.artifacts/srtc-close/README.md` | `e4abb02231cac868f606ae5a117a359507b2095c058da76374249b8812a298e7` |
| `.artifacts/srtc-close/SOURCE_HASHES.txt` | `6f596876226346b3b371556a17524f4f38b2c007ef7cbe83d28bcd766c91b8aa` |
| `.artifacts/native-provider-phase0/run-20260925T190646Z/FINAL_REVIEW.md` | `9698fcd796b4685f67d634e7ba39f0051768f0b2669a417bf83f3174f8a02c75` |
| `.artifacts/host-facade-provider-loop/run-20260925T202407Z/FINAL_REVIEW.md` | `2eeb09789869eb50a45b3bfd6043b5225ede4f036604f2aacc688372cfe3e819` |
| `.artifacts/host-input-close-reopen/run-20260925T213718Z/FINAL_GATE.json` | `e01ec39c9d569b31338a973b5b46fcc9ee94f65f341d0444df7d0ed97dcc3da0` |
| `.artifacts/host-input-close-reopen/run-20260925T213718Z/SOURCE_FREEZE_R3.json` | `d4c3e1a3445da04983393a1ea61f6e1437b10095eb310c6ecaebb2be9127d8dc` |
| `.artifacts/real-participant-host/run-20260926T093712Z/FINAL_GATE.json` | `57de7e12819dc6a7951b7a99504df9103eca64703bd6eed9dc58c73687548a1d` |
| `.artifacts/real-participant-host/run-20260926T093712Z/SOURCE_FREEZE_R2.json` | `1c7fb87449dfdf350f6aad4e3dccd3c63a9ef01df4774514c6c185ccfd1db337` |
| `.artifacts/real-participant-host/run-20260926T093712Z/FINAL_REVIEW.md` | `facbd7d0ca77b0755b77e086bdb45b6be818f94dbe7783df40321ed0222f702b` |
