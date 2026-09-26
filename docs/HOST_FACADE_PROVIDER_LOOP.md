# Host-only facade / provider loop

Status: **HOST_FACADE_PROVIDER_LOOP_HOST_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED**
(2026-09-26; artifact run `20260925T202407Z`). The final independent Host run
passed focused `16/16` and full CONFIG `323/323`; both opt-in flags remain OFF by
default. This is a bounded Host fixture result, not production Transport or SDK
qualification.

Stated chain: actual DSoftBusTransport -> original SenderResource admission and
NativeSendExecution -> ProviderAdapter -> NativeBackend Mock -> saved weak
callback -> ProviderAdapter -> ServiceRecordBinding -> owned copy / original
application and receiver lease -> test receiver.

Scope: one local and one dial route, manually driven Host fixture. No Participant,
SDK, device, real provider, production backend registration, commit or push.
NativeUnavailable remains the default constructor behavior in both compiled variants.

## Integration design

- A separate opt-in facade library compiles the existing facade sources with
  MDDS_HOST_BACKEND_TESTING, propagated to its consumers. A private tagged
  constructor accepts a small HostFacadeBackend bridge. The normal facade library
  has no macro/injection path. CMake compatible-interface properties reject
  consumers linking both variants with the same C++ class symbols.
- The injected Ready owns the same locator plan/slots, but creates no parallel
  RuntimeSendPath or InputChannels. OpenOutputChannel(s) use NativeSendExecution's
  Attach; OpenInputChannel creates and publishes the existing ProviderAdapter /
  ServiceRecordBinding with the caller's exact receiver.
- HostProviderLoop owns the native owner table, ApplicationAttachments, native
  send execution/registration and adapter. All use the same native table.
  ProviderAdapter exposes a private friend-only NativeSendProvider bridge, which
  checks that exact table. No public raw-socket send or arbitrary foreign
  execution/work adapter method is added.
- PumpSend takes original owned work, admits through the existing native ticket,
  runs it, and destroys invocation/work before decrementing the pump count.
  PumpReceive uses Binding's original NextAdmitted/RunAdmitted. Neither bypasses
  a queue, looks up a future receiver nor constructs a discovery protocol.
- Provider send is a synchronous immutable borrow. Missing result, unavailable
  provider or exception is a provider-contract fault, not a fabricated SDK rc.
  Send priority remains observed in the adapter; no SDK priority mapping is claimed.
- Provider and receiver callback exceptions are contained without bypassing the
  binding. Open identity/allocation failures, duplicate start/ready ownership,
  failed pending cleanup, and the descriptor RX limit were covered by tests-first
  RED/GREEN fixes. The descriptor limit is 64 accepted and 65 rejected. The
  adapter's internal `-1` failure sentinel is not an SDK return code.

## Normal control shutdown

Close the controller's pump/attach admission; BeginRevoke and progress the native
send registration (cancel queued/not-entered work); seal adapter ingress; publish
the sealed boundary; wait for send/receive pumps to settle outside every gate;
then close the binding. Already executing receiver calls retain their original
lease. Queued RX may be dropped by its original detach barrier. SenderResource
handles retained by their caller may retain closed runtime metadata; that is not
reported as released memory or SDK settlement. SDK drain remains Unproven.

No hidden worker is introduced. Init/Open/explicit shutdown and destruction are
external control operations; Observe is also externally serialized because
Initialize/Connect use the existing binding gate. No synchronous shutdown from
mock or receiver callback is allowed. PumpSend and PumpReceive may overlap the control shutdown. A test
condition-variable barrier, not sleep, observes the admission-sealed boundary.
Receiver/native dependencies live until whole shutdown and pump join finish.

## Explicit unsupported operation

Independent CloseInputChannel is NOT implemented by this Host fixture. The
injected facade returns false with an explicit unsupported-operation diagnostic,
does not mark the channel closed and retains the receiver through whole shutdown.
The legacy/production variant keeps its existing behavior. This fixture must not
be bound to a real Participant that could free a receiver after CloseInputChannel.
It is not full TransportInterface conformance or a reusable production backend.

## TDD matrix

1. Default factory remains unavailable in both variants; explicit injection only.
2. Actual facade SenderResource -> owned TX -> native ticket -> mock saved callback
   -> owned RX -> exact original receiver, with both source buffers overwritten.
3. Send before owner/input setup rejects rather than fabricating a ready owner.
4. Independent input-close rejection has no side effects; whole shutdown works.
5. Queued TX revoked before execution never reaches the provider.
6. Paused provider send: shutdown seals admission, rejects new work and waits;
   returned invocation and payload settle before provider shutdown.
7. Paused receiver: shutdown waits for the original lease; no call after return.
8. Late callback after shutdown rejects; provider errors retain truthful result.
9. Runtime/record/byte and adapter counters distinguish drained work from retained
   metadata; raw SDK quiescence is never inferred.
10. Both facade variants cannot be linked into one consumer.

Status: **IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED** for the one-local,
one-route Host fixture. Canonical final evidence is in
`.artifacts/host-facade-provider-loop/run-20260925T202407Z/`; prior 10/317 and
14/321 attempts remain preserved as pre-review evidence. The default-OFF CMake
configuration passed, and the compatible-interface guard rejects consumers that
try to link production and Host-injected facade variants together.

The Host mock is the only provider exercised. Real Fast DDS Participant/SPDP,
SDK provider calls, native/device execution, full `TransportInterface` conformance,
hot rebuild, and fault-cancellation qualification were not run. The explicit
`CloseInputChannel` operation remains unsupported in this fixture: it returns
false with a diagnostic and retains the channel/receiver until whole shutdown;
this is not a production close contract. Start/Open/Attach/control operations
are externally serialized, while send/receive pumps may overlap shutdown. P0,
PF-01/PF-02, P4, `NativeBackendUnavailable`, and QualificationProfile status are
unchanged.

The paragraph above records this package's original unsupported-close scope. The
later bounded [`HOST_INPUT_CHANNEL_CLOSE_REOPEN`](HOST_INPUT_CLOSE_CONTRACT.md)
package exercises independent close/reopen on the frozen R3 Host fixture; it does
not expand this package's evidence or claim SDK/device/full `TransportInterface`
qualification.
