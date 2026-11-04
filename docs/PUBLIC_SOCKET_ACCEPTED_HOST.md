# Public Socket accepted Host integration

Status: **`PUBLIC_SOCKET_ACCEPTED_HOST_CONTRACT_VERIFIED / REAL_DDS_OVER_PUBLIC_SOCKET_FAKE_VERIFIED / SOURCE_HEADER_TARGET_LINK_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**
Artifact: `.artifacts/public-socket-accepted/run-20260926T174047Z`

This checkpoint covers the bounded dual Host topology with one listener,
one outgoing route and at most one committed accepted owner per endpoint. The
actual Host path uses `PublicSocketBackend`, Fake public Socket C symbols,
typed callbacks, the existing owner/adapter/binding/receiver paths and the
public Fast DDS Participant/Writer/Reader fixture. It is a Host Fake C API
integration, not SDK runtime or device qualification.

## Evidence

- Independent Release focused set: `80/80 PASS`; full suite: `396/396 PASS`,
  exit 0, no timeout.
- Independent ASan affected set: `139/139 PASS`, with external prebuilt DDS
  DLLs not instrumented. The `396` and `139` totals overlap and must not be
  summed.
- Five new actual `PublicSocketBackend` tests are included; the older nine
  DDS tests remain Mock regression coverage and are not relabeled as Public
  Socket tests.
- E4 exercises an extra accepted negotiation against the existing live pair;
  the extra candidate is refused and cleaned up, then the two existing
  Participants perform 10 DDS takes. This is capacity refusal on a live pair,
  not Participant creation failure or a null-create result.
- M1–M9 effective mutations are killed. The initial M6 operator did not apply
  and is excluded; its corrected continuation records the actual M6 kill. The
  earlier M7 build failure and other invalid attempts remain preserved and are
  excluded from the kill count.
- Freeze-r2 contains 21 matching source hashes. Target-r2 contains 727 input
  files and 7 output hashes; API23 AArch64 compile/static-link evidence uses
  authorized public headers from checkout `5518313` and the declared historical
  DSO.

## Contract details recorded

F1 preserves both the first operational failure and cleanup status. F2 keeps
per-role terminal state and strict TX ownership after role stop. F3 cancels
accepted RX through the existing allocation-free `TryTake`/destruction path.
Preparation reserves an original owner, commit publishes that same owner, and
rollback does not invent a second callback slot or native identity.

The ordinary BYTES path skips `Negotiate2`; unsupported negotiation/access-info
routes remain fail-closed. Bounded topology is one listener/outgoing/accepted
per endpoint with accepted-only business RX, no reconnect or RAW claim, and no
new owner registry or queue. The old shim's historical scope remains recorded
in `PUBLIC_SOCKET_SHIM.md`.

## Boundaries

No SDK bundled header, firmware ABI equivalence, loader guarantee, SDK internal
drain/quiescence, device, cross-device, or native runtime evidence is claimed.
The target was not loaded, executed or deployed. Production remains
`NativeBackendUnavailable`; P0/PF-01/PF-02 remain `BLOCKED / NOT QUALIFIED`,
P4 is not released, and no QualificationProfile is created.
