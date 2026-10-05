# A5/C1 bounded same-route reaccept Host contract

Status: `A5C1_BOUNDED_REACCEPT_HOST_VERIFIED` (bounded Host scope);
`API23_REACCEPT_BUILD_VERIFIED`; device reconnect and production qualification
remain unverified.

Authorization: A5C1_BOUNDED_REACCEPT_HOST_IMPLEMENTATION_AND_LOCAL_COMMIT.
Base commit: bdb1073e18192bce1e904e30517219ddda5e8bc5.
Parent: d670773a8385194c409b09b3d028d44edb01b81e.
Tree: d16ed047615db94d0d30ee616c522c61dfc6048d.

## Scope and model

Explicit Host experiment only, using the existing public Socket Fake/opaque-byte
C boundary and the real owner table, adapter, binding, queues and DDS/ROS entities.
The default and API23 real backend retain NO_REACCEPT. No device operation is
authorized. V1 locator/name contracts, PF/P4 and production defaults are unchanged.

The supported model supplies a never-before-used positive native ID for each new
connection within the retained record/Runtime lifetime. Public callbacks remain
OnNegotiate(id, peer), OnBind(id, peer), OnBytes(id, bytes), OnShutdown(id, reason):
no generation/context is added. Old callbacks carry their original IDs and are
rejected after retirement. IDs already owned, prepared, abandoned or quarantined
cannot be reused. An SDK event first delivered with indistinguishable reused ID
and peer cannot be classified by these arguments; real SDK reuse remains unsupported.

| Requirement | This package |
| --- | --- |
| Controlled normal connection ending, fresh replacement IDs | Host integration target, both endpoint ending orders |
| Peer connection interruption reported through normal shutdown callback | Declared Host model only; no SDK timing/resource assertion |
| Peer process restart / route or permission rediscovery | Not covered; formal requirement remains unmet |
| Same numeric ID reused or ambiguous first native effect | Explicit rejection / unsupported, never an epoch claim |

## Ownership and transitions

1. Original OnShutdown latches the matching immutable owner terminal state and
   stops new lane admission. OnError without a proven terminal notification stays
   failed; it does not authorize replacement or infer native closure.
2. Callbacks never wait for settlement or create replacement sockets. Existing
   external Runtime receive/control pumping advances retirement. A serialized
   control attempt is non-reentrant and final Runtime close wins over renewal.
3. Accepted retirement retains listener/receiver anchor and the opposite outgoing
   direction. Cancel queued RX with their original leases; entered copy/receiver
   work completes before the old child binding is released. Existing native
   operation/callback/receive counters must settle before a new owner is admitted.
4. Outgoing retirement retains its original record entrance and pending exposing
   anchor. In the existing dual assembly the outgoing binding stays Connected:
   it has no published membership or application/receiver group. The listener
   remains the sole receiver anchor; renewal must not publish a Dial receiver.
   Old queued sends retain their original generation and are cancelled/rejected;
   entered sends finish against the original ticket. Fresh Socket/Bind is driven
   by the existing control pump, not an application rebuild call. Reserve a new
   bounded owner before any new Socket exposure; never overwrite the old cell.
5. A negotiation reserves a candidate only. Ownership/close responsibility starts
   at the existing CommitInbound point after matching OnBind. Binding route and
   adapter Active publication then enable RX. Competing candidates cannot replace
   Preparing/Prepared/Committing/Active state. Publication and terminal callbacks
   must be checked under the original gates; a losing candidate rolls back once.
6. Dial replacement reuses that binding's exposing record and namespace lease,
   after the original owner has a terminal public notification and no entered
   native activities. A fresh ReserveDial/Socket/Bind ticket replaces the binding's
   operational lease, never the old owner cell. The successful native Bind ticket
   publishes the new TX generation; no ServiceRecordBinding::Publish or new Dial
   membership/group is introduced. Listener receiver registration is unchanged.
   Failed renewal is retained rather than retried without a new admissible event.
7. NativeOwnerTable retains old IDs, incarnations and SDK-visible failed candidate
   IDs in its existing finite cells. Prepared-only candidates have no Shutdown
   right. Rejections/quarantine never blindly Shutdown unknown IDs. Capacity
   exhaustion rejects before exposure and does not close healthy opposite lanes.
8. Listener close and Runtime close stop all renewal. Only the existing original
   owner/ticket may close listener, dial or accepted handles; a public terminal
   callback may discharge that owner's close responsibility, not another's.

Adapter settlement is not SDK drainage. No tombstone reclamation, second registry,
worker pool, retransmission protocol, Participant rebuild or Runtime restart is
introduced. Finite owner capacity is the lifetime replacement budget; admission
failure is observable and does not promise arbitrary long-running reconnect.

## Implementation and validation sequence

First RED: existing real public-C-API adapter/DDS integration completes ordinary
communication, receives terminal events, retains the same upper entities, then
fails to deliver replacement data under original NO_REACCEPT behavior. Implement
the bounded native predicate/retained candidate IDs, deferred adapter control,
backend Host-only renewal, and original binding record retention. Extend the
existing Host byte seam's finite sockets/accept loop; it supplies no new identity
arguments. Keep the default NO_REACCEPT characterization unchanged.

Then test both endpoint orders; blocked entered send/copy/receiver; old queue
cancellation; duplicate candidates; rollback and close-once; wrong peer/profile/
route; reused and abandoned IDs; valid new events; finite capacity; opposite-lane
survival. Run real DDS and ordinary ROS Host with stable process/context/
Participant/Writer/Reader, resumed Topic data, graph/service, original DDS deletion
and Runtime close. Restore NO_REACCEPT as a compiling functional mutant and check
integration failure; also kill useful old-generation retarget, early settlement,
duplicate-close and candidate-overwrite mutants.

Independent tester owns execution evidence. Run affected ownership/close ASan,
applicable full CONFIG, V1/installed consumer regression, and actual API23 compile/
link with Host mode refused in the default target path. Independent review and
post-review retest precede docs sync and one exact allowlisted local commit.
Historical V1 device PASS remains bound to its original source/package/run.

## 2026-10-05 execution disposition

The bounded Host implementation and its post-review execution evidence are
recorded in [A5C1 bounded reaccept Host acceptance](A5C1_BOUNDED_REACCEPT_HOST_ACCEPTANCE_20261005.md).
The independent tier-2 review reported no blocking source finding. The Host
experiment passed both terminal orders and ordinary ROS control runs while
preserving the same upper entities, resumed Topic traffic, graph/service,
original DDS deletion, Runtime close and public return 0. The default real
backend still rejects reaccept; the real compiled shim rejection test passed.

The acceptance is deliberately limited to the declared opaque Host callback
model and never-used native IDs. It does not establish SDK event epochs,
internal SDK drain, same-ID reuse, peer process restart, route/permission
rediscovery, general reconnect, device execution or production qualification.
The formal reconnect requirement therefore remains incomplete. The finite
owner-table budget and explicit rejection behavior remain part of the contract.

The Host scheduler pauses the existing receive pumps, delivers the normal
terminal callbacks, lets the accepted endpoint retire and settle, then resumes
the existing outgoing control path for one automatic Socket/Bind attempt. The
application never calls a rebuild operation. A failed Dial candidate is retained
and is not blindly retried. This proves the fresh-ID/deferred-control model; it
does not prove recovery from arbitrary peer-readiness races or process restart.

Success labels are conditional on execution: A5C1_BOUNDED_REACCEPT_HOST_VERIFIED,
API23_REACCEPT_BUILD_VERIFIED, LOCAL_COMMITTED, DEVICE_RECONNECT_NOT_VERIFIED,
NOT_PRODUCTION_QUALIFIED. FULL_REQUIREMENTS_COMPLETE, SDK_RECONNECT_QUALIFIED and
PF01/PF02_PASS are not established by this package.
