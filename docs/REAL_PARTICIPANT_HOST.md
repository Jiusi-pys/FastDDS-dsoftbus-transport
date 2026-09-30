# Real Fast DDS Participant over Host provider

Work package: **`REAL_FASTDDS_PARTICIPANT_OVER_HOST_NATIVE_BACKEND`**<br>
Status: **`REAL_FASTDDS_PARTICIPANT_HOST_INTEGRATION_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**<br>
Artifact run: `.artifacts/real-participant-host/run-20260926T093712Z`

This is a historical checkpoint recorded before the public naming migration. Its
`MddsKeyedSample` and other legacy identifiers describe that original source and
package only; they are not current `fastdds_dsoftbus` package or ABI evidence.

This is a bounded Host integration checkpoint on Fast DDS 3.6.2.0 commit
`343f155c36b561db3d9f047a65d866ce0f3da300`, using the current test-only
descriptor factory and the existing Host facade. It uses one local locator and
one dial route, two Participants in one process, RELIABLE/VOLATILE QoS,
`MddsKeyedSample` with a fixed 32-byte payload, `maxMessageSize=8192`, builtin
transports disabled, intraprocess disabled, and DataSharing disabled. The old
`ExperimentalTransport` target is not linked.

## Actual path and cases

The exercised path is:

```text
public DDS factory
  -> test-only descriptor
  -> current Host facade
  -> SenderResource / NativeSendExecution / ProviderAdapter / Binding
  -> actual ReceiverResource
  -> DataReader take
```

The mock backend forwards borrowed bytes synchronously to the peer callback;
the existing adapter performs the owned RX copy. The parser is observation-only
and does not route, respond, or fabricate RTPS/data delivery.

Normal `D1+D2+D3` runs are five samples in each direction, followed by B-first
with a recreated B Reader taking A samples 6–10, and A-first with the opposite
deletion order. The normal scenarios produced 15 and 10 actual DataReader takes;
the recreated Reader has a new GUID while the Participant, Writer and input
resource remain unchanged. Whole Participant/entity deletion leaves the two
idempotent input-close calls to the upstream cleanup order and records one
provider close. A 100 ms post-ACK observation found no extra valid sample; this
is a bounded observation, not proof of permanent absence.

D0 records the ordinary default factory unavailable path. D5 records the actual
public API result: B creation returns a **non-null Participant** while input
setup Bind is rejected; the returned B Participant is deleted through the public
API, A remains live and unchanged, and no half receiver or ticket remains. It
must not be documented as `create_participant=nullptr`.

The disabled-link negative creates both Participants and performs actual model
send attempts, but forwarding is disabled; it has no remote match or data take
within the bounded window. The pump-fault and late-extra cases are audit
counterexamples that detect converted failure and post-ACK extra data; they are
not normal data-delivery evidence.

## Verification evidence

- Independent final-r2 focused set: `9/9 PASS`, each exit 0.
- Independent final-r2 full Release build/configure: exit 0; CTest `346/346
  PASS`, exit 0, subprocess duration 179.434 seconds.
- ASan-r2: new Participant cases `9/9 PASS`, prior input-close `14/14 PASS`,
  prior provider-loop `15/15 PASS`; instrumented project targets imported the
  ASan runtime. The positive UAF control reports the expected heap-use-after-
  free and exits 1; the safe control exits 0.
- Both review findings reached RED, were fixed, and passed independent final
  retest: returned pump failures/cleanup errors and close status are checked;
  the bounded late-extra observation is checked for both current Readers.
- `PATH_EVIDENCE.json` binds the normal focused logs to same-run model-send,
  forward, RX-captured, owned and running observations, with exact Writer GUID,
  sequence and Reader ordinal sets. This is Host/model evidence, not physical
  SendBytes or SDK packet causality.

## Boundaries

This checkpoint does not qualify the SDK provider or ABI, native DSoftBus
execution, device or cross-device behavior, production P4, P0/PF qualification,
or a no-race guarantee. Prebuilt Fast DDS, Fast CDR and foonathan DLLs were not
ASan-instrumented. No SDK/device/native qualification or QualificationProfile is
claimed. P0/PF-01/PF-02 remain **`BLOCKED / NOT QUALIFIED`**; production P4 is
not released, default `NativeBackendUnavailable` remains active, and no-Git
write restrictions remain unchanged.
