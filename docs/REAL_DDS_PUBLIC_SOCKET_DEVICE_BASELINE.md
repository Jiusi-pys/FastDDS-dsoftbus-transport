# Real DDS over new Public Socket device baseline

Status: **`REAL_DDS_OVER_NEW_PUBLIC_SOCKET_DEVICE_BASELINE_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**
Artifact: `.artifacts/real-dds-public-socket/run-20260926T225417Z`

This is a bounded two-board small DDS baseline for the new PublicSocketBackend
path. It is separate from earlier Host-only, accepted-only and runtime-probe
packages; their historical scope is preserved.

## Device evidence

- D1 `e6e553762d29ceb2` deletes B then A.
- D2 `c4af736f2b0d35c6` deletes A then B.
- Both windows ran on the two registered RK3588A boards through actual
  `DSoftBusTransport` façade → `NativeSendExecution` →
  `ProviderAdapter`/`PublicSocketBackend` → public Socket API → remote
  accepted/`ProviderAdapter`/`Binding` → `ReceiverResource`/`DataReader`.
  Each direction delivered five samples; the two windows provide 20 complete
  formal sample Writer GUID/SN chains in total.
- No retries were observed. DDS wait API calls are not network ACK-packet
  counts.
- Device guards cover the four windows and eight board-window outcomes. The
  control trace retains the limitation that B records 12 SPDP ingress events
  versus 11 owned/running events with the exact reason unobserved; this is not
  a claim of complete native packet delivery accounting.

## Supporting evidence

- Host full r1: `414/414 PASS`.
- Final affected Host and ASan r3: `161/161 PASS` each.
- Final independent probe Host and ASan r1: `9/9 PASS` each; these totals
  overlap and are not summed.
- Final r3 package hash begins `2c42f3fc...`; source freeze r3 is bound to
  `fb4400...`; target-r3 binds 1,514 inputs and six package members.
- One actual Fake-call QoS parameter RED was caught before device execution:
  numeric `3` (`RTT_LEVEL`) had been used as `MAX_WAIT_TIMEOUT`, and was fixed
  to `1`. Three separate analyzer counterexamples were also recorded. The R2
  package was never deployed.

## Boundaries

This is a small DDS baseline only. It does not qualify hot rebuild, full QoS,
full `TransportInterface`, firmware/SDK ABI equivalence, SDK drain/quiescence,
arbitrary topology, production enablement, P0/PF or P4. Default
`NativeBackendUnavailable` remains unchanged; P0/PF-01/PF-02 remain
`BLOCKED / NOT QUALIFIED`, P4 is not released, and no QualificationProfile or
commit/push authorization follows.
