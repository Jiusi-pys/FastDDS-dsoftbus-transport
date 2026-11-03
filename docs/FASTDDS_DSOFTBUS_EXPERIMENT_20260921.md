# Real Fast DDS × DSoftBus Socket experiment (2026-09-21)

## Current closeout — bounded performance baseline (2026-09-24)

`DDS_BOUNDED_PERFORMANCE_BASELINE` is **`BOUNDED_PERFORMANCE_BASELINE_MEASURED / CHECKPOINT_READY / NOT_COMMITTED`** for the executed `BYTES_NATIVE` scope: two RK3588A boards, six fixed configurations and three independent MEASURE repeats. The formal population contains 3,300 RTT requests; fixed-load Reader delivery contains 6,120 deliveries (RTT full-echo Reader deliveries are outside that fixed-load total). Pooled p50/p99 are RTT_1K `3.219125/4.435375 ms`, RTT_4K `3.226709/4.408250 ms` and RTT_256K `11.085668/14.135335 ms`; fixed-load Reader business goodput is 0.09765625, 0.390625 and 1.00000000 MiB/s respectively. These are executed offered loads, not maximum throughput.

The final data review is PASS with no unresolved blocking findings. Independent post-review host suites are OFF `243/243 PASS` and ON `245/245 PASS`; `49/49` is the historical pre-TRACE-fix full-host stage, while the focused R3 final analyzer is `58/58 PASS`. The independent device receipt is `PASS_WITH_PRESERVED_TRACE_FAILURE`: query, failed TRACE01, successful TRACE02 and M1/M2/M3 provide 12/12 final board guards; TRACE01 remains immutable and is excluded from formal statistics. The focused R3 receipt records PowerShell 4, Python 10 and Node 14 executor checks; the native package has 1,737 inputs and 6 artifacts with unchanged hashes, and executable SHA-256 `5f98207268441debefcfaa57f0396a7be0d1cc2ed9d40e98e49f11211b30a440`.

The evidence remains bounded. `RAW_MEASUREMENT_SAMPLES.csv` contains 19,116 planned-identity rows, not deliveries. SDK internal settlement and exact queue high-water remain `UNOBSERVED`; only the two cumulative M3/A `NO_APPLICATION` rejected records (not four) retain unobserved payload/Writer identity and control-versus-business classification. Each side records two TX rejects; RX prefix overflow is diagnostic truncation and is not treated as business loss. P0/PF-01/PF-02 remain `BLOCKED / NOT QUALIFIED`, production remains `NativeUnavailable`, P4 is unreleased, no QualificationProfile exists, and Git transitions remain prohibited. See [RESULT_TABLES.md](../.artifacts/dds-bounded-performance-20260924/run-20260923T183702Z/analysis/RESULT_TABLES.md), [FINAL_DATA_REVIEW.md](../.artifacts/dds-bounded-performance-20260924/run-20260923T183702Z/review/final-data/FINAL_DATA_REVIEW.md), and the [canonical device receipt](../.artifacts/dds-bounded-performance-20260924/run-20260923T183702Z/device/CANONICAL_INDEPENDENT_DEVICE_ALL_ATTEMPT_RECEIPT.json).

Observed resources are bounded measurements: single-core process CPU per case is `3.293%–12.126%`, Participant lifecycle span is `85.656–86.118 s`, sampled RSS peaks at `29.824 MiB`, and observed VmHWM peaks at `30.133 MiB`. These do not establish a long-term leak result or SDK internal settlement.

## Current matrix closeout — DDS normal functional matrix (2026-09-23)

The current bounded package is `DDS_MULTI_ENDPOINT_AND_LATE_JOINER_FUNCTIONAL_MATRIX`, recorded under [`.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z`](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z). It is **`DDS_NORMAL_FUNCTIONAL_MATRIX_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`** for the tested `BYTES_NATIVE` scope. The 13 fresh two-board cases M1/M2/M3/M4, Q_BE_BE/Q_REL_BE/Q_REL_REL/Q_BE_REL, L1/L2/L3 and I1/I2 passed independent review with 102 original writes, 132 per-Reader deliveries and 132 same-run native associations. The frozen contract is [CASE_MATRIX.json](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/design/CASE_MATRIX.json); execution and ledger evidence are [MATRIX_EXECUTION_RESULTS.json](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/analysis/MATRIX_EXECUTION_RESULTS.json) and [ACTUAL_QOS_MATCH_GRAPH_READER_LEDGERS.json](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/analysis/ACTUAL_QOS_MATCH_GRAPH_READER_LEDGERS.json).

Host post-review suites were OFF `175/175 PASS` and ON `177/177 PASS`. The four scoped native regressions (hot1, 1 MiB fragment, RELIABLE controlled-loss, BEST_EFFORT controlled-loss) passed; one additional Socket-free directory query was recorded. Thus 13 matrix windows + 4 native regression windows + 1 query window = 18 registered windows, of which 17 were actual DDS native windows, with 36/36 complete board guards. Native package selection and hashes are bound by [BUILD_SELECTION.json](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/device-build/BUILD_SELECTION.json). The final independent review is [REVIEW_CLOSURE.md](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/review-final/REVIEW_CLOSURE.md), and the final focused tester receipt is [FINAL_TEST_RECEIPT.md](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/host/independent-fragment-disposition-v2/post-review/FINAL_TEST_RECEIPT.md).

This is a bounded normal-DDS functional result, not production qualification. P0/PF-01/PF-02 remain `BLOCKED / NOT QUALIFIED`, the production default remains `NativeUnavailable`, P4 remains unreleased, no QualificationProfile was generated, and no Git mutation was performed. The prior [restored-package-device delivery](../.artifacts/restored-package-device/run-20260923T075212Z/FINAL_GATE.json) is formally closed and was not reopened. SDK internal settlement remains `UNOBSERVED`; the 27 nonzero `SendBytes` returns and the fragment disposition correction remain explicitly accounted for. L2/L3 late-join evidence does not establish a new application-detached gap or PF-02 old-native-frame isolation. RAW, three-board, mixed transport, ROS 2/RMW, performance and new 100-cycle qualification remain outside this package.

## Historical closeout — DDS keyed history and resource boundaries (2026-09-23)

The separate `DDS_KEYED_HISTORY_AND_RESOURCE_BOUNDARIES` package is **`FINAL_CODE_AND_DEVICE_REVIEW_CLOSED / CHECKPOINT_READY / NOT_COMMITTED`**. Ten bounded `BYTES_NATIVE` keyed native cases (K1, K2, K3, K4_TRUE, K4_FALSE, H1, H2, H3, R2 and R3) passed; R1 is `HOST_ONLY_PASS`. Canonical totals are 62 writes, 128 API data/take observations, 58 unique data identities, 42 taken identities, 3 state records, 58 native data joins and 3 native state joins. H1's four oldest-before-Reader evictions are expected Writer `KEEP_LAST` behavior; H2 repeated reads are not network-duplicate evidence; `valid_data=false` rows were not payload-parsed.

The final independent host receipt is OFF `205/205 PASS`, ON `207/207 PASS`, with 53/53 analyzer tests and 12/12 affected gates passing. Both native packages built with 1,719 inputs and zero independent input mismatches; only OFF was exercised on the two boards. Fourteen registered windows produced 28 board windows and all full guards passed. K1-attempt-01 retains an unobserved outer launcher exit while runner and remote exits are 0, and is excluded from canonical totals; the socket-free query retains 82 per-command zero exits while its aggregate launcher/runner exit remains unobserved. The fresh strict small replay and fresh M2 two-reader replay both passed. See the [keyed docs receipt](../.artifacts/dds-keyed-history-20260923/run-20260923T140748Z/docs/DOCS_SYNC.md).

The evidence is limited to fixed Fast DDS 3.6.2.0, the generated bounded keyed type, `BYTES_NATIVE`, the two existing boards, finite history/resources and the executed cases. Reader `get_key_value` and Writer `lookup_instance` remain unsupported, standalone registration propagation is not implemented, and the broad WriterDataLifecycle documentation warning conflicts with the narrow true/false behavior observed here; no all-policy qualification is claimed. SDK internal settlement remains `UNOBSERVED`; P0/PF-01/PF-02 remain `BLOCKED / NOT QUALIFIED`, production remains `NativeUnavailable`, P4 is unreleased, and RAW, three-board, mixed-transport, ROS 2/RMW, performance, 100-cycle and full-topology qualification remain unexecuted.

> **状态同步（2026-09-22）：** 本文早期 R4、双实例和热重建功能证据均保留为历史/继承证据。当前收尾工作包 `HOT_REBUILD_BASELINE_CLOSEOUT_AND_RX_REJECTION_ACCOUNTING` 状态为 `DONE_WITH_HISTORICAL_LIMITATION / NOT_COMMITTED`；源码封存/恢复及最终 checkpoint 结论以本轮 `FINAL_REPORT.md` / `FINAL_GATE.json` 为准，本文不独立宣称恢复验证 PASS。当前收尾证据见 [DOCS_SYNC.md](../.artifacts/hot-rebuild-closeout-rx-20260922/run-20260922T012640Z/docs/DOCS_SYNC.md)。

## Current closeout — RX admission accounting (2026-09-22)

This closeout reconciles the previous functional package; it does not rerun or requalify the hot-rebuild implementation. The inherited package remains bound to `hot-rebuild-production-integration-20260921/run-20260921T152404Z`: its OFF/ON host suites were 132/132 and 134/134, its 1/10/100 functional windows and 20/20 guards remain historical evidence, and its package hashes are not replaced by this closeout.

The historical 1 MiB window had 217 native ingress records and 201 owned-copy records. The 16 ingress-only records are exact raw observations, but contain no Captured stage, owned-copy, rejection result, payload identity, Writer GUID/SN or generation. They are therefore **not proven 16 rejections or DATA_FRAG losses**; their exact cause remains `HISTORICAL_CAUSE_UNRESOLVED`. The 105 sender DATA_FRAG observations and 89 receiver-return observations use a different unit and cannot be subtracted to identify those 16 records.

The diagnostic delta records the decision-point adapter snapshot with bounded storage and preserves unknown/overflow/incomplete states. It does not infer SDK generation or perform payload reads. Final host validation is OFF `148/148 PASS`, ON `150/150 PASS`, focused `8/8 PASS`, and analyzer `31/31 PASS`. Post-review native OFF/ON builds are `0/0/0`, each with 1,657 inputs and six artifacts; executables are `6352b28f6b7f3d033bb8ecd58f5319eb4479cfe76d5d0ece461ed21487f1c0fc` and `dedfad61e0c26da6e9d7a5e26b60c21ab8db9130a573d4513a5223a8ce8f3a19`.

The fresh 1 MiB window `5923cd595106e8a6` recorded 203 admissions and zero rejects, so the new 1 MiB rejection condition was `NOT_REPRODUCED`; the historical causes remain `HISTORICAL_CAUSE_UNRESOLVED`. The fresh hot-smoke window `98e4d1228566a0a4` recorded 10 same-run sample chains and 10 successful DDS `wait_for_acknowledgments` operations; its 144 decisions were 141 accepted and three `NO_APPLICATION` terminal dispositions, two in the strict gap and one during final cleanup. These new observations do not retroactively explain the historical records and do not prove SDK internal settlement.

The diagnostic review found no actionable Transport defect. P0/PF-01/PF-02 remain `BLOCKED / NOT QUALIFIED`, production remains `NativeUnavailable`, SDK internal settlement remains `UNOBSERVED`, and no QualificationProfile is generated. The local PF contract request is [PLATFORM_CONTRACT_REQUEST.md](../.artifacts/hot-rebuild-closeout-rx-20260922/run-20260922T012640Z/PLATFORM_CONTRACT_REQUEST.md). The final source checkpoint conclusion is recorded in this work package's FINAL_REPORT.md / FINAL_GATE.json; this experiment document does not independently claim restore verification.

## Historical — separate same-name hot-reattach admission boundary

The later same-process, same-display-name hot-reattach work package was recorded at that time as `BLOCKED / NOT_COMMITTED`. At that historical STOP 0, real Participant deletion reached terminal ExperimentalTransport State shutdown while no bridge attached a new Participant facade to a surviving live native InputChannel/table. The current limited live-table app-reattach path closes that historical wiring gap; OLD2/PF-02 and the full-native-record RED remain qualification boundaries.

The prior same-native-service reproducer remains a separate nonblocking RED artifact and is not rerun or promoted into this admission premise. No new build, test, device, SDK or permission operation occurred; native build, device pilot, single cycle and repetition are `NOT_RUN`. Details: [hot reattach final report](../.artifacts/experiment-hot-reattach-20260921/run-20260921T145916Z/FINAL_REPORT.md).

## Final bounded dual-instance correction

The later validator correction was independently re-tested and exercised in five fresh `BYTES_NATIVE` A/B windows. This is a bounded experiment record; it does not qualify the production transport.

| Evidence | Result |
| --- | --- |
| Post-review host suites | OFF `55/55 PASS`; ON `57/57 PASS` |
| Native package builds | OFF and ON bridge/configure/build `0/0/0`; each manifest has 93 inputs |
| Native package hashes | OFF `09b16d92d1445b144af92c93d2cc9f64ce6f2faa3ce33e7d4ef43eb5c850f23c`; ON `5096447dcb0b689640eb6ca1d54c4593f010bfba189b50aa4bb4ce00deed6f55` |
| Fresh device windows | Dual `d4a56718293a4b5c`, small `e0a56718293a4b5c`, fragment `e1a56718293a4b5c`, reliable `e2a56718293a4b5c`, best-effort `e3a56718293a4b5c`: all PASS, HDC/remote exit `0`, no watchdog |

The dual window verified two Participants per board, P1 deletion with P2 retained, P2 writes after the recorded `P1_DELETE_COMPLETE` marker, exact fixture/public GUID and entity-ID readback, stable P2 participant/writer/reader state, and three ACK stages per board. P1 and P2 each produced six and ten normal samples respectively. All 32 normal native chains were paired to the opposite board's successful SDK send using instance, keys, ports, length and hash. A had 61 native `OnBytes` events/61 owned copies; B had 60 events/59 owned copies plus one explicitly accounted closing/no-receiver B2 invocation with no receiver entry. The latter is retained as evidence and is not treated as a successful delivery. The raw discovery/status stream remains available alongside the current full-GUID map and exact matched/sample identity checks.

The four affected regressions are separately evidenced: ten exact small samples; three exact 262144-byte samples with five DATA_FRAG ranges each; one RELIABLE whole-message drop with NACK-associated range recovery and all three samples; and one BEST_EFFORT target sample absent while samples 2/3 remain exact and the user ACK API is `NotCalled`. Existing 1 MiB evidence remains historical and was not rerun in this correction because fragment/type limits were unchanged.

The TDD and review record preserves the original runtime RED (D1, D2, D4, D5, D6, S2, S3, S4; ctest 8 failures), the independent P2 cleanup/action-exception finding and its X1–X4 reproduction, and the corrected GREEN `26/26`. Final reviews are closed for test fidelity, program behavior, device evidence and four affected regressions. The old `device-dual-r2` receipt is historical: it matched P2 and had four overlap rounds plus ACK, but failed the pre-delete raw-count predicate and its label overstated the result; its A/B exit-6 cleanup did not prove P1 deletion/P2 survival. The separate `device-window` attempt failed before B started due to an invocation/path issue; a later post-abort guard pass is recorded under its later run and is not retroactively assigned to that attempt.

The experiment remains `NOT_QUALIFIED`: `P0 BLOCKED / NOT QUALIFIED`, `NativeUnavailable`, PF-01–04, production P4 unauthorized, no qualification profile, no commit/push and `COMMIT_GATE=PROHIBITED` are unchanged. SDK-internal settlement is `UNOBSERVED`; same-run activity overlap is not simultaneous SDK proof. RAW, hot rebuild, more Participant topologies, pressure/fault close, mixed transport, native self-route and three-board testing remain unverified or deferred. The structured evidence ledger is [FINAL_REPORT.md](../.artifacts/experiment-dual-20260921-second-instance-fix/run-20260921T121708Z/FINAL_REPORT.md).

This is a separately authorized, default-OFF feasibility experiment. It is a
limited exception for one real Fast DDS × public DSoftBus Socket path; it does
not reopen the closed PF-01..04 investigations, authorize P4, qualify the
production transport, or permit a commit/push. The original R4 receipt is retained at
[`R4 RESULT.md`](../.artifacts/integration-experiment-20260921/tester/device-reader-r4/RESULT.md).
The subsequent [hardening regression](../.artifacts/experiment-hardening-20260921/tester/device-r4/RESULT.md)
and bounded-fragment results below have separate artifacts and run IDs.

## Scope and isolation

- Two authenticated RK3588A OpenHarmony boards: A publisher and B subscriber;
  one process/Participant per board; ten small generated HelloWorld samples in
  the original/default scenario, or three bounded samples in each large scenario.
- Fast DDS 3.6.2.0 supplies SIMPLE SPDP/SEDP, the user Writer/Reader and
  RELIABLE acknowledgement. The experiment adds no discovery protocol,
  framing, retransmission, or RTPS semantic change.
- The experimental transport is under
  [`experiments/fastdds_socket`](../experiments/fastdds_socket), behind
  `BUILD_FASTDDS_DSOFTBUS_EXPERIMENT=OFF` by default. The native program also
  requires `FASTDDS_DSOFTBUS_EXPERIMENT_NATIVE=ON`, an OHOS build, real Fast DDS CONFIG
  3.6.2.0, and an explicit C/POD token bridge.
- Effective runtime isolation was checked: `use_builtin_transports=false`, the
  sole transport descriptor is the experimental private kind, UDP/TCP/SHM
  descriptors are absent, intraprocess delivery and data sharing are OFF, and
  external XML/profile/discovery-server overrides are rejected. The package
  uses the V1 address codec, fixed
  `com.kaihong.mdds.` + canonical service names, separate client/server role
  records and socket owners, owned TX/RX copies, bounded queues, and separate
  worker/lease/queue/SDK-stop accounting. The TokenBridge keeps the `__h`
  AccessTokenKit ABI separate from the `__n1` Fast DDS app ABI; it is not IPC.
- The static native-app permission policy and existing permission rules were
  unchanged. The controlled run temporarily granted the existing
  `DISTRIBUTED_DATASYNC` permission to the two experiment identities, revoked
  it afterward, and restored the pre-run baseline; fresh guards and revoke
  logs are retained with the R4 evidence.

## Original R4 build and subsequent hardening evidence

The compile/package script is
[`BuildFastDdsSocketExperiment.ps1`](../scripts/BuildFastDdsSocketExperiment.ps1).
The original R4 OHOS package has bridge/configure/build exit codes `0/0/0` and
main executable SHA-256
`4d3182b9c933c054e1df30cfeb1dd277732b63469fdb08395e439e74573ee68b`.
The complete input/package hash manifest is
[`build-manifest.json`](../.artifacts/integration-experiment-20260921/tester/reader-native/build-manifest.json).

The independent Windows CONFIG/Release host run was:

```powershell
ctest --test-dir .artifacts/integration-experiment-20260921/tester/reader-host/build -C Release -R ^experiment\. --output-on-failure
```

Result: `7/7 PASS`, including listener policy, XML control/skip isolation,
scenario checks, transport contract, late-bind close, and native-name checks.
The independent default-OFF/source-probe gate also passed; an opt-in source
build is rejected unless the explicit experiment path is selected.

The subsequent hardening package is bound separately from the original R4
package: its executable SHA-256 is
`4486635012e66a76fce9c9bf9263baf8a11990d6ce720d3a7468b2fb5ac4e064`, and the
independent hardening host run is `16/16 PASS`. The hardening review and tests
cover: terminal Bind state cannot be overwritten by a nominal READY result;
Stop seals work and joins the unique TX caller before local native close;
affirmative negotiation reserves an accepted owner before Bind, while terminal
or never-bound reservations remain closeable; observer PAD/INFO_TS zero-length
headers preserve following submessages; and logical task counters are kept
separate from actual payload lifetime. Payloads use per-message vectors rather
than a pool; SDK memory and internal settlement remain `UNOBSERVED`.

## Original R4 device run and observed path

The exact A/B commands, package directories, peer network IDs, expected token
IDs, run ID, and natural-expiry policy are in
[`commands.json`](../.artifacts/integration-experiment-20260921/tester/device-reader-r4/commands.json).
The packages ran from separate `/data/local/tmp/fastdds_socket_reader_r4_*`
directories with their own `LD_LIBRARY_PATH`; system libraries were not
replaced. HDC and remote exits were zero, `timedOut=false`, and the transition
record proves B started while A's collector was still open.

R4 recorded bidirectional SPDP/SEDP discovery and matching. A logged ten
successful writes and `DDS_ACKNOWLEDGED rc=0`; B received exactly samples 1–10
with `duplicate=0`, `mask=3ff`, and `invalid=0`. The path proof includes the
user sample's sender `SendBytes_return`, receiver owned-copy and Fast DDS
receiver return with equal hash, length, and writer GUID, plus the complete
SPDP/SEDP hash chain.

Both sides reported `sdk_active=0`, `callbacks_active=0`, `leases=0`, zero TX/RX
records and bytes, joined workers, `closed=1`, and `clean_observed=1`. This is
adapter-visible cleanup. `SDKinternalSettlement=UNOBSERVED`; the 4 ms/2 ms stop
observations are measurements from this run, not an SDK timeout bound, and the
remaining process/callback tombstone is not SDK quiescence proof.

## Conditional bounded DATA_FRAG phase

After the hardened R4 run, independent host build/gate/test and both reviews
passed again: host tests were `17/17 PASS`, the default-OFF/source probe passed,
and the OHOS bridge/configure/build exits were `0/0/0`. The fragment package's
main executable SHA-256 is
`b7e20c6004e2be2e38d9cf5dbb3e3b6910309f838d74b853e059adc6705e55ad`; the final
81-input manifest rehash had zero mismatches. The earlier small-message
package (`4d3182…`) and the hardened package (`448663…`) remain preserved as
their own artifact versions.

The generated [ExperimentLargeSample IDL](../experiments/fastdds_socket/large_sample/ExperimentLargeSample.idl)
declares an octet sequence bounded at 1048576 bytes. Its mutable C++ vector is
checked before DDS write and after DDS take for configured size and every
run/index-dependent byte; the declaration alone is not treated as runtime
enforcement. Large-mode DDS history/resource limits are four samples plus one
explicit extra sample. The per-submessage four-byte alignment correction was
added and tested in this fragment artifact, after the hardening artifact.

The default small case used run `c5d6e7f8091a2b3c` and retained the original
ten-sample path. The 256 KiB case used run `d6e7f8091a2b3c4d`; each of three
samples had the actual writer GUID
`010f5a584b52675c0000000000000103`, sequence numbers 1–3, serialized sample
size `262156`, fragment size `59824`, complete range union `1..5`, five
successful sends, and matching sender/copy/receiver hash and length. The 1 MiB
case used run `e7f8091a2b3c4d5e`; each of three samples had writer GUID
`010f5a58195378a00000000000000103`, serialized sample size `1048588`, fragment
size `59824`, complete range union `1..18`, and exact Reader mask `007`. Its
successful SendBytes observations were `29/24/26`; unique range count was 18
per sequence, with repeated-range observations `11/6/8` (79 DATA_FRAG
observations in total). Repeated ranges are observational counts only and do
not identify a retry, NACK, or retransmission cause. The v2 offline analysis
also associates each 256 KiB and 1 MiB group with the actual ReaderSampleInfo
GUID, sequence high/low, index, exact byte count and payload hash; every group
has a counterpart tuple and complete range union.
The v2 checks are per-sequence sorted coverage and identity/range/hash-length
counterpart checks; they do not claim fragment multiplicity beyond the observed
records.

For every DATA_FRAG observation, the observer records the active writer GUID,
sequence high/low, fragment start/count/range, `sample_size`, native message
length, and native hash. The per-submessage GUID handling preserves the prior
INFO_SRC/first-writer meaning; it does not reconstruct, retransmit, or alter
RTPS data. Native RTPS input remains bounded at `60000` bytes, while the
maximum observed native message length in both large runs was `59968` bytes. The
default small case remains the normal ten-sample case. All three bounded
device scenarios recorded `SCENARIO_PASS`, data/ACK/close success, and no
watchdog; adapter task and payload counters reached zero, while SDK memory and
internal settlement stayed `UNOBSERVED`.

The 1 MiB run has an immediate post-revoke full guard pass with restored A/B
baseline hashes `ac52be…` and `5512ae…`. The small and 256 KiB revoke logs
show `verification_permission=-1`, but their complete JSON guards were captured
later, so full guard success is `NOT_OBSERVED_AT_BOUNDARY` for those windows.
Scenario/data/close PASS is therefore reported separately from the fixture
guard boundary. Stop-duration values are observations from these runs, not
fault or SDK timeout bounds.

## Controlled-loss phase

The initial plain ExperimentalTransport.cpp exactly matched the b7e20c build
manifest (`ff75c2ed…`): no duplicated old/new diff statements were present, and
that check did not trigger a source rewrite or repeat of the earlier device
matrix. The real Submit exception defect was separately reproduced: a throwing
post-admission hook caused premature settlement or UINT64 counter underflow.
Queue publication is now irreversible; post-admission diagnostic exceptions
cannot run pre-admission rollback. Both RED cases and subsequent GREEN evidence
are retained under the controlled-loss submit directory.

The final native package executable is
`471317e2a95c3eba0490459d63c455a6033a9118969f3ec60e10a912027f5289`. Final
host validation was `OFF 21/21 PASS` and `ON 23/23 PASS`; the final OHOS
bridge/configure/build exits were `0/0/0`. The drop capability is default-OFF,
requires `FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP=ON` (build script `-EnableRxDrop`) and the
explicit controlled-loss runtime arguments, and does not
alter the ordinary transport path. No SenderResource/registry/iterator
refactor was introduced.

The RELIABLE run (`f8a901b2c3d4e5f6`) established the causal chain from actual
writer discovery and arm completion, through user Reader creation and ordinary
SEDP/matching, to the first write. One whole native RTPS message was then
selected at B's owned-copy/receiver boundary by the actual writer GUID,
sequence 1, fragment 3, and serialized sample size `262156`: native length
`59968`, hash `3eef211bfc4d2ae8`. No receiver invocation occurred for that
drop. Fast DDS later sent one NACK_FRAG request for sequence 1, base fragment 3,
bitmap `80000000`, native length `160`, hash `58e6b3ec82ea1af7`; the request
was received at A, A sent the missing range, B received the new fragment with
the same GUID/SN/range and hash, and ReaderSampleInfo then reported sample 1
exactly before samples 2 and 3 completed and reliable ACK returned. The v2
offline causal analysis reports two positive runs and rejects five mutated
negative chains; the earlier v1 analysis is retained but not used because its
summary counted two prints rather than drop events and its receiver check used
the wrong invocation field. The final analysis checks both logged sender
acceptance and native send entry after control ingress on A's own clock;
the acceptance log is not the exact queue-publication linearization instant.

The BEST_EFFORT run (`a9b012c3d4e5f607`) dropped one selected sequence-1,
fragment-3 message of `59952` native bytes (distinct from RELIABLE's `59968`).
B delivered samples 2 and 3 exactly with mask `006`, did
not deliver Reader sample 1, and the publisher did not call the ACK API
(`ACK=NotCalled`). The selected range was absent from receiver deliveries and
remained absent through the observation window. Both runs had
normal observable cleanup: HDC/remote exits 0, workers joined, SDK-active /
callback / lease / task / payload counters zero, and final guards restored the
baseline. The guards prove the recorded permission/file restoration boundary;
they do not prove SDK-internal physical resource settlement, which remains
`UNOBSERVED`. The BEST_EFFORT 4 s/3 s windows and
`NO_SHUTDOWN_CALLBACK_OBSERVED` are observation results, not physical liveness
or timeout bounds.

The controlled-loss reports distinguish application payload bytes (`262144`),
serialized DDS sample size (`262156`), and selected whole native packet length
(`59968` for RELIABLE, `59952` for BEST_EFFORT). The ordinary read-only RTPS observer and bounded vendor-statistics
metadata remain diagnostic only; they do not disable SDK/Fast DDS features or
create a recovery protocol. No IPC, warmup channel, or control protocol was
used.

All 85 final build inputs match their manifest. Read-only post-run HDC
`sha256sum` receipts match all six deployed package files in each REL/BE A/B
directory. These are explicitly post-run file checks, not invented pre-run
captures. See the execution receipts and evidence review below.

## Status and limits

| Area | Evidence state |
| --- | --- |
| Experiment implementation | PRESENT; production `src/DSoftBusTransport.cpp` and `src/DSoftBusTransportDescriptor.cpp` unchanged |
| OHOS compile/package | PASS, exit `0/0/0` |
| Original R4 host experiment tests | PASS, `7/7` |
| RK3588A cross-device R4 scenario | PASS, HDC/remote exits `0`, no timeout |
| Hardening lifecycle/observer host scope | PASS, independent `16/16` plus scoped reviews |
| Final fragment host scope | PASS, independent `17/17` plus scoped reviews; all 81 manifested inputs match |
| Bounded DATA_FRAG device phase | PASS for small/256 KiB/1 MiB scenarios; guard-boundary limitation recorded above |
| Controlled RELIABLE loss/recovery | PASS, one causal drop → NACK_FRAG → range resend → Reader/ACK chain; bounded experiment only |
| Controlled BEST_EFFORT loss | PASS, selected range absent and ACK not called; bounded experiment only |
| Final controlled-loss host scope | OFF 21/21 and ON 23/23 PASS; reviewed fixes re-tested and final native build 0/0/0 |
| Data and normal-stop gates | PASS, limited to the recorded scenarios |
| Historical fragment-package permission guards | Small/256 KiB NOT_OBSERVED_AT_BOUNDARY; 1 MiB PASS |
| Controlled-loss permission guards | REL and BE immediate full-JSON restoration PASS; REL verified before BE authorization |
| Production P0 qualification | BLOCKED / NOT QUALIFIED; `NativeUnavailable` remains |
| Production P4 native backend | NOT AUTHORIZED; no production qualification profile generated |
| Commit/push gate | PROHIBITED; this report does not authorize Git state changes |

RAW/RAW_ENCRYPTED, hot reconstruction, multiple Participants, sustained
pressure, fault/close matrices, mixed transport, and native self-route remain
`NOT COVERED` and remain required elsewhere. This experiment is not a complete
V1 result and does not replace those acceptance items.

Earlier failures remain part of the record: the first `device-final` package
failed Socket admission with `PERMISSION_DENIED`; `device-admission-r2` was a
sequential-runner infrastructure failure (`PEER_SESSION_NOT_CREATED` followed
by abuse protection); and `device-admission-r3` exposed the parent listener's
`DATA_ON_READERS` masking before the R4 listener-mask correction. These are
preserved as historical evidence and do not reverse the R4 result.

## Evidence index

- [`WORK_PACKAGE.md`](../.artifacts/integration-experiment-20260921/WORK_PACKAGE.md)
- [`IMPLEMENTATION_RECEIPT.md`](../.artifacts/integration-experiment-20260921/transport/IMPLEMENTATION_RECEIPT.md)
- [`result.json`](../.artifacts/integration-experiment-20260921/tester/device-reader-r4/result.json)
- [`transitions.jsonl`](../.artifacts/integration-experiment-20260921/tester/device-reader-r4/transitions.jsonl)
- [`review/`](../.artifacts/integration-experiment-20260921/review/)
- [`hardening review and device receipt`](../.artifacts/experiment-hardening-20260921/)
- [`eight deterministic lifecycle RED cases`](../.artifacts/experiment-hardening-20260921/lifecycle/RED_RECEIPT.md)
- [`observer RED/GREEN evidence`](../.artifacts/experiment-hardening-20260921/observer/RECEIPT.md)
- [`final transport implementation`](../experiments/fastdds_socket/ExperimentalTransport.cpp)
- [`final 81-input consistency check`](../.artifacts/experiment-fragments-20260921/tester/final-host/all-input-hash-consistency.json)
- [`fragment reviews and device receipts`](../.artifacts/experiment-fragments-20260921/)
- [`small-message receipt`](../.artifacts/experiment-fragments-20260921/tester/device-small/RESULT.md)
- [`256 KiB v2 analysis`](../.artifacts/experiment-fragments-20260921/tester/device-256k/fragment-analysis-v2.json), [`1 MiB v2 analysis`](../.artifacts/experiment-fragments-20260921/tester/device-1m/fragment-analysis-v2.json)
- [`controlled-loss v2 analysis`](../.artifacts/experiment-loss-20260921/tester/device-reliable/loss-analysis-v2.json), [`best-effort v2 analysis`](../.artifacts/experiment-loss-20260921/tester/device-best-effort/loss-analysis-v2.json)
- [`controlled-loss v2 checks`](../.artifacts/experiment-loss-20260921/tester/analysis-v2-checks.json), [`loss reviews`](../.artifacts/experiment-loss-20260921/review/)
- [`Submit RED evidence`](../.artifacts/experiment-loss-20260921/submit/RED_RECEIPT.md), [`Submit GREEN evidence`](../.artifacts/experiment-loss-20260921/submit/GREEN_RECEIPT.md)
- [`RELIABLE execution receipt`](../.artifacts/experiment-loss-20260921/tester/device-reliable/RESULT.md), [`BEST_EFFORT execution receipt`](../.artifacts/experiment-loss-20260921/tester/device-best-effort/RESULT.md)
- [`independent causal evidence review`](../.artifacts/experiment-loss-20260921/review/EVIDENCE.md)
- [`post-run package consistency`](../.artifacts/experiment-loss-20260921/tester/postrun-package-consistency.json), [`final 85-input consistency`](../.artifacts/experiment-loss-20260921/tester/final-input-consistency.json)
