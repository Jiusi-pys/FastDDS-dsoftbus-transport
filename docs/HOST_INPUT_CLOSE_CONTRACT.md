# Host input close/reopen contract (R3 host-verified checkpoint)

Scope: explicit Host-injected actual facade, one exact configured local locator
and one dial route. No SDK/device/Participant enablement or production default change.
Fixed upstream: Fast DDS `343f155c36b561db3d9f047a65d866ce0f3da300`.

**Caller contract.** TransportInterface.hpp requires a definitive channel state
and safe Close during Receive on another thread. ReceiverResource.cpp constructs
via OpenInputChannel; its Cleanup lambda calls CloseInputChannel **without using
the bool result**. disable() runs Cleanup before waiting its own callbacks and
marking them disabled; its destructor body is empty (despite the older header
comment). NetworkFactory::BuildReceiverResources checks IsInputChannelOpen before
constructing a resource. SupportsLocator checks kind plus DoInputLocatorsMatch.
Tests use an actual TransportReceiverInterface object and the same close-then-
destroy ordering; they do not claim to instantiate the internal ReceiverResource.

```text
Host loop: NativeOwnerTable + NativeSendExecution registration -> TX tickets
    |                   (retained throughout local input close)
    + ProviderAdapter / same provider handle / ServiceRecordBinding
         + native owner + record/role membership + route (retained)
         + ApplicationGroup -> claim + receiver binding -> original receive queue
                              (detached/drained; recreated on successful reopen)
```

| Operation/state | Frozen result |
| --- | --- |
| Initial / gap / closing / whole shutdown | IsInputChannelOpen=false |
| Open known local, nonnull receiver | true only after real claim/receiver publication |
| Open same receiver while already open | true, no replacement or new generation |
| Open different receiver while already open | false, original remains callable |
| Close known local | true after RX capture/copy/queued/executing work settles and old callable references are removed |
| Close already closed / before first open / after shutdown, known local | true, idempotent, no native close |
| Unknown locator or different kind | false, no effect on legitimate channel |
| Reopen known local | same locator/provider/owner/TX registration; fresh real receiver identity tuple, transactional rollback on failure |
| DoInputLocatorsMatch | exact supported local equality; not device-only or kind-only equality; no live mapping after whole shutdown |

Close first seals RX admission; binding removes the sole registration under its
gate (shared group with >1 members is refused unchanged). The sole-member decision
uses the same group gate as joins. RequestDetach cancels queued records without a
pump, then AwaitDetached crosses the old input lease barrier. A binding ingress
counter spans capture through complete Admit return, because AwaitDetached alone
does not wait copying_activity. Close waits that counter and any old RX pump, not
TX, before returning. Old work cannot re-query a new registration. Waits, record
destruction, provider calls and receiver calls stay outside ownership gates.

TX and native owner stay usable in the gap; echo/other new RX in that gap receives
CHANNEL_CLOSED/NO_APPLICATION terminal disposition before owned-copy. No future
queue exists. Reopen claims a new application/receiver registration on the same
existing attachments and queue; native identity need not change. New RX after
reopen uses that new lease, including legitimate calls of the retained provider
callback handle. This says nothing about pre-adapter old physical SDK events.

Controls Init/Open/Close/shutdown/destruction remain externally serialized;
IsOpen observations may run during closing and become false at its start. Open
while a close is in progress is outside the supported control contract, not a
promise of arbitrary concurrent control. Provider callback and RX/TX pumps may
overlap local close. Callback-context synchronous close/shutdown remains forbidden.

Reopen failure leaves RX closed with no claim/half-registration, permits retry on
the same object/local, preserves provider fault/cleanup evidence and never revives
a terminated TX generation. A provider stop or whole shutdown refuses reopen.
Whole shutdown still freezes TX, joins pumps and closes the native owner once.

Verification: I1–I14 tests through facade + adapter + binding; real Captured/OwnedCopy
observer barriers; receiver-address reuse with identity tuples; isolated behavioral
mutants; Release and available ASan; independent tester/review. The R3 source freeze
hashes match and the independent corrected-environment focused receipt is 32/32 PASS
(14 new input-close cases, 16 existing provider-loop cases, and the two tests that
failed only under the first run's omitted `PSModulePath`). A later independent
canonical Python-wait receipt `tester/canonical-full-r3/RECEIPT.json` records
exit 0 and its `ctest.stdout.raw.log` records CTest `337/337 PASS` in 119.84
seconds. The earlier `final-build-all-r3` 118.69-second run remains preserved
as non-canonical, and the earlier uninterrupted full Release run remains preserved as
335/337 with exit 8 and its environment cause; it is historical raw evidence,
not the final full-target result. The final mutation index reports the isolated
R3 control 14/14 PASS and nine required plus one additional effective mutant killed;
the earlier invalid/ineffective M03 attempts remain historical.

The review-found callback Close deadlock is closed: the pre-lock callback-context
guard was observed RED by the two-stage timeout, fixed, and re-tested; deleting the
guard recreates the bounded deadlock. Independent close can destroy the old actual
`TransportReceiverInterface` after RX capture/copy/queued/executing work settles,
drop gap RX while retaining TX/native ownership, and safely reopen on the same local
locator with a fresh receiver/application lease. The observation path uses the
still-owned `ApplicationAttachments` ledger after group removal; a default-zero
binding-local projection is not treated as total settlement.

Current status: **HOST_INPUT_CHANNEL_CLOSE_REOPEN_HOST_VERIFIED /
CHECKPOINT_READY / NOT_COMMITTED** for the bounded one-local, one-dial Host fixture.
No test uses a failed bool to justify retaining an old receiver. Debug is blocked by
the repository's explicit Release-only ABI guard; TSan was not run and race freedom
is not established. Real `ReceiverResource` instantiation, real Participant/SDK
provider, full `TransportInterface` qualification, native/device and cross-device
behavior remain unverified. P0/PF-01/PF-02, P4, `NativeBackendUnavailable`, and
QualificationProfile boundaries are unchanged.
