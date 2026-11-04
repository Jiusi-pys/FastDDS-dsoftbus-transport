# Public Socket target load and bounded runtime probe

Status: **`TARGET_LOAD_OBSERVED / BOUNDED_PUBLIC_SOCKET_CALLBACK_PATH_OBSERVED / NEW_SHIM_BIDIRECTIONAL_SMALL_BYTES_OBSERVED / CHECKPOINT_READY / NOT_COMMITTED`**
Artifact: `.artifacts/public-socket-runtime/run-20260926T200859Z`

This package is a bounded runtime observation of the current Public Socket shim
on two RK3588A boards. It is separate from the earlier accepted-only Host
integration package and does not rewrite that package's evidence.

## Actual windows

- Load `93a421640af19a22`: both processes entered and exited normally; mapped
  symbols and source/header origins were checked. No grant and no explicit
  five-call Socket sequence were used.
- Guarded live query `7ec64dad0b67367d`: fresh public directory identity and
  separately owned grant/revoke guards passed; this window created no Socket
  resources.
- Socket `d82c4df0d3dccd73`: the only actual data attempt, five 256-byte
  synthetic records in each direction. Each side observed five accepted
  `OnBytes` callbacks, complete payload checks, normal exits and restored
  guards. A second data attempt was not needed or run.

The callback sequence was listener 1, outgoing 2, accepted 3; ordinary
`OnNegotiate` and `OnBind` committed accepted 3, sends used outgoing 2, and
received bytes used accepted 3. A issued explicit void `Shutdown` for accepted,
outgoing and listener. B received typed `OnShutdown` reason 8 for outgoing and
accepted, then explicitly closed listener. These are observed ownership and
callback facts; void return does not prove SDK drain or quiescence.

## Supporting validation

- Independent full Host R3: `407/407 PASS`.
- Final affected Host and ASan sets: `151/151 PASS` each.
- Final runtime Host and ASan probe sets: `10/10 PASS` each; these scopes
  overlap and are not added together.
- Fresh target package probe hash:
  `989bb53c2852cb6c6fb02211fe90c03f05ddcf65cbf0f92f36c58633f1efdea8`.
  Target-r4 binds 1,357 inputs and six package members; both boards used the
  same package and observed the same system `libsoftbus_client.z.so` hash.

## Boundaries

The payloads are synthetic small BYTES probe records, not DDS samples, RTPS
traffic or a new SDK DDS path. The result does not establish complete firmware
ABI equivalence, SDK internal settlement, cancellation deadlines, old physical
event identity, arbitrary topology, performance or production readiness.
Headers came from authorized `communication_dsoftbus` source checkout
`5518313`, not bundled SDK headers. The new opt-in runtime probe does not change
default `NativeBackendUnavailable`; P0/PF-01/PF-02 remain `BLOCKED / NOT
QUALIFIED`, P4 is not released, and no QualificationProfile exists.

No device result from this package is assigned to the earlier accepted-only
package. No DDS Participant was created in the runtime probe.
