# Native provider adapter boundary — Phase 0

Status: **NATIVE_PROVIDER_ADAPTER_BOUNDARY_DESIGNED / CHECKPOINT_READY / NOT_COMMITTED**
(2026-09-26; artifact run `20260925T190646Z`). This is an SDK-free Host design and verification checkpoint. It does
not enable the production backend, call DSoftBus, run a native build, or qualify a
device path.

Scope: SDK-free interface and opt-in Host contract prototype. No production
backend registration, SDK call, device run, or change to DSoftBusTransport.
Baseline: `145d0ee97de5e8728a985550da82f169c640d89e`, with the pre-existing
ServiceRecordBinding/close-contract working tree preserved.

## Target and delivered boundary

The future dependency direction is DSoftBusTransport -> NativeBackend, with
NullBackend, an explicit wrapper around the existing experimental backend, and
a future DSoftBusProviderBackend. This phase does **not** install that dependency
in the facade, wrap/enable the experimental transport, or implement the SDK
provider. Both non-null production integrations remain deferred. The facade's
existing NativeBackendUnavailable path stays byte-for-byte unchanged.

NativeBackend is an SDK-free provider port: typed identity declaration,
create/listen/bind, synchronous send and shutdown. NullBackend returns unavailable
without a socket or an SDK return code. The Host mock is the only non-null
implementation used in this phase. ProviderAdapter is a Host-only composition
prototype that implements the existing ServiceRecordProvider bridge privately;
ServiceRecordBinding is not converted into an SDK adapter.

| Responsibility | Owner |
| --- | --- |
| Service name / deployment address | BindingRequest / directory; not identity |
| Provider-declared record and owner tokens | Distinct typed nonzero model inputs; not SDK generation/context or authentication |
| Fixed record/role entrance and actual model record incarnation | ServiceRecordTable, via ServiceRecordBinding |
| Socket numeric ID and native owner incarnation/tickets | NativeOwnerTable, via ServiceRecordBinding |
| Application/receiver identity, owned receive copy, queue and lease | ApplicationAttachments / InputChannels |
| Provider event boundary, exception containment, availability latch | ProviderAdapter |
| Synchronous send borrow / provider result | NativeBackend send port; future execution must use NativeSendExecution admission/ticket, never direct facade-to-socket send |

The prototype's bounded registration domain rejects duplicate declared record
tokens or service names before touching binding/provider state; it is not a new
native record allocator or continuity proof. One adapter owns one new binding in
Phase 0; join/accepted/reconnect and real backend selection are not implemented.
The domain, native table, attachments and receiver must outlive their adapters.

## Lifecycle and receive contract

1. Validate Host mode, nonempty matching service name, nonzero separately typed
   provider record/owner tokens, and supported new-binding topology.
2. Reserve the bounded declaration entry; construct the existing binding and
   Prepare/Connect. Every create/listen/bind call still crosses its native ticket.
3. Publish the binding, then open adapter ingress. A provider stop latch is
   terminal and prevents late publication from reopening admission.
4. Provider receives only an adapter-owned weak callback handle, never
   Binding::Entrance or a binding/receiver pointer. Bytes enter the adapter,
   capture a live original binding under its gate, then call that binding's
   fixed entrance **outside** the adapter gate. Binding performs record/owner/
   application attribution and the existing owned-copy admission.
5. Delivery uses Binding::NextAdmitted/RunAdmitted; it does not invoke the receiver
   directly. A receiver exception is contained by that actual path, and the
   adapter remains closeable. Provider setup exceptions are caught before the
   legacy noexcept bridge; its internal failure sentinel is not reported as an
   SDK return code.
6. Provider shutdown notification only latches unavailability at the adapter.
   It never synchronously calls binding while Connect holds its gate. New
   receive/publication admission fails closed. The send port is declaration-only
   in this phase and is not executed. Control-context Close seals admission,
   waits for adapter ingress calls, and invokes the existing binding release.
   Already owned work is settled by the original lease/drain rules.
7. Close reports adapter settlement separately from SDK quiescence, which always
   remains **Unproven**. No timeout, stop event or shutdown return proves native
   cancellation or physical resource release. Retired callback handles weakly
   reject rather than adopt a later adapter.

Controls (initialize/create/publish/delivery/close/destruction/Observe) are externally serialized;
provider callbacks may occur during setup, ready operation and close. A close or
destruction from the delivery callback is not an authorized use of this Host
prototype; use the inherited binding close-request contract on a future control
surface. Provider operations and receiver calls never run under the adapter gate.
The pending-stop state has at most one candidate plus a conflict bit. It becomes
stopped only after the candidate is matched or a conflict is observed. A single
foreign candidate is rejected without closing the existing owner. This bounded
Host admission rule is not an SDK drain guarantee.

Important inherited integration constraint: Binding::Connect currently holds its
own gate across provider setup. Pre-publication callbacks therefore terminally
drop at the adapter and stop notifications only set the latch. This does not
establish arbitrary SDK reentrancy support. The future SDK shim still needs fixed
service-record/role trampoline lifetime and an observation basis for record
continuity. The weak Host callback handle is **not** a claim that public SDK
callbacks carry context/generation. PF-02 remains unqualified.

The send port is declared, but Phase 0 does not create an alternate send queue or
execute it from ProviderAdapter. A future send implementation must bind the port
to NativeSendExecution's original owner/deadline/payload ticket. It is not a
verified SDK send/cancel path. Adapter close preserves the first provider shutdown
failure and reports close/adapter settlement separately; repeated Close calls do
not hide that fault.

## Frozen Host matrix

| Case | Expected contract |
| --- | --- |
| P0 | Null port unavailable; real default facade init still fails NativeBackendUnavailable |
| P1 | Mock create -> listen/bind -> publish -> adapter callback -> actual binding owned copy -> receiver -> close; one close responsibility |
| P2 | Throwing receiver callback is contained; subsequent valid delivery and close work; throwing provider setup does not cross noexcept bridge |
| P3 | Provider stop before publish or before binding close refuses new ingress and publication; no claim of SDK drain |
| P4 | Missing record or owner declaration refuses before reservation/exposure/provider calls |
| P5 | Duplicate record (including a different service-name alias) or duplicate service name refuses; existing binding still delivers |
| P6 | Inline setup callback is rejected without deadlock; late callback after adapter destruction rejects |
| P7 | Invalid/foreign socket and invalid buffer rejected; new interface/header carries no DSoftBus SDK include |

Workflow: design review -> tests plus nonfunctional stub -> observed RED -> minimal
implementation -> independent tester -> agentic review -> affected retest -> docs.
Final Phase 0 evidence is in
`.artifacts/native-provider-phase0/run-20260925T190646Z/`: focused `16/16 PASS`,
full CONFIG `307/307 PASS`, and default-OFF configuration verification. The pre-fix
independent 11-case raw log was not retained as a standalone artifact; the
noncanonical 302-test wrapper exit was `UNKNOWN`, while the later main green run
retains its own 11-case log. The invalid-locator fixture failure remains separate
historical evidence. Use new artifact runs
and build directories for later changes. Retain inherited dirty bytes and all
failed evidence. No commit/merge/push or QualificationProfile.

## Evidence status and remaining boundary

The SDK-free adapter boundary is **IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED**
within the bounded Host-only scope. P0 is a characterization pass; P1–P7 are
covered by the final focused and full Host runs. The provider callback is routed
through `ProviderAdapter` into the real `ServiceRecordBinding` attribution and
owned-copy path; it does not enter the binding directly. The public controls are
externally serialized, and the callback handle is a weak Host handle rather than
an SDK-provided context or generation.

The experimental-backend wrapper, future DSoftBus provider, production facade
injection, actual send execution, SDK callback normalization, physical
record/owner continuity, callback quiescence/cancellation, device and DDS runs,
and the publish-stop interleave as a deterministic barrier test remain
**DEFERRED / NOT QUALIFIED**. P0/PF-01/PF-02 and production P4 remain unchanged;
production default remains `NativeBackendUnavailable`.
