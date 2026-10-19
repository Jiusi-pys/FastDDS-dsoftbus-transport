# R33 native parameters: functional V2 PASS, strict V1 failures retained

Snapshot 2026-10-19T02:39:39.888997+00:00; baseline HEAD `5585ce7ffde6a1191b1a2b49dd5d2abdfb581484`. **Technical Commit gate PASS and DEVICE/CROSS_DEVICE PASS only for `R33_NATIVE_PARAMETER_FUNCTIONAL_V2_STOCK_RMW`. Overall ROS 2 NOT_VERIFIED.** R32 is published as `5585ce7ffde6a1191b1a2b49dd5d2abdfb581484` (2026-10-18 23:35:56 UTC exact-SHA CI NOT_RUN). At this 2026-10-19T02:39:39.888997+00:00 pre-commit documentation snapshot, the technical tester gate is PASS only for functional V2; Main graph/PRE_COMMIT/commit/push/CI remain PENDING. Actual publication outcomes are recorded separately in root-relative `build/r33-capability-gap/main-functional-v2-publication/POST_PUBLISH.json`; no future SHA or CI success is prefilled.

## Selected direction and meaning of V2

Stock RMW is selected and unchanged. The user-authorized, independently reviewed V2 evidence model separates (1) exact parameter request/response/event/state semantics, (2) native deserialize of the complete public serialize output with equal typed fields, and (3) structural native transport/endpoint/identity/lifecycle proof. It preserves all40 original local bytes, lengths and digests. No production RMW, DDS/RTPS, DSoftBus, generated type or parameter service implementation was changed. No suffix is silently trimmed, normalized or declared valid padding.

This is a new explicit functional acceptance version, not proof that standalone serialization was corrected. The old strict V1 entrypoints and decoder remain. Both original V1 device windows stay FAIL. The strict V1 decoder also raises `Trailing CDR bytes` on the new V2 window; V2 rejects historical V1 raw rows lacking complete native-roundtrip markers rather than retroactively qualifying them. No old result, raw sample or hash was rewritten.

Fixed V1/V2 compose/prepare/run entrypoints bind the selected profile through package, qualification, prepared plan, constructor, release, live business, final analysis and result. V2 uses `compose-functional-v2`, `prepare-functional-v2`, `run-functional-v2`; plans cannot select arbitrary oracle callbacks. Strict cdr/CdrReader/exact remain AST-identical, shared field semantics retain requirements and now reject wrong-role captures. Release schema must be an exact int, not bool/float. Both live and final validation use the selected fixed wrapper.

## Actual device3 outcome

Once-only scenario `c74f911bdf7870ca`, application run `96063cc74887df4c`, execution `c289d704ce533034`, original paired query `3c3a49a1fd648747`. Physical RK3588A A `3e01ff55454d202020104033bf453b00`/token672006291 and B `3e01ff55454d202020104433991c3b00`/token672042405; same admitted Windows Python/OpenHarmony SDK23 environment. No replacement, ARM/REPLACE or business permission was added to this parameter scene.

| Evidence | Bounded result |
| --- | --- |
| Services / calls | Six stock parameter services;14 ordered request/response pairs,nine parameter value types,readonly/dynamic descriptors. Exact typed values, identities, service/role associations and ordering pass. |
| Mutations / rejection |12 mutation events/commits,13 validation callbacks,3 atomic refusals with unchanged-state readbacks,dynamic deletion/final state. No-extra-event observation250ms, not unbounded absence. |
| Discovery / endpoints | Three snapshots precede exact two-node CLIENT_READY; six request/reply role pairs,parameter events and stock type-description endpoints,data-sharingOFF. GetTypeDescription RPC NOT_RUN. |
| Complete local samples |40 actual full native serialize/deserialize roundtrips with equal fields;18 exact consumed lengths,22 preserved suffixes,11 nonzero,suffix range4..72 bytes. All original base64/bytes/lengths/digests retained. |
| Native path | Selected module hashes,13 type capsules per process,owned launcher/libpython,bridge/FastDDS/FastCDR/RMW/Socket providers bound to package. Reparsed native A sends88/ingress110; B sends110/ingress88. Not per-RPC-to-RTPS attribution. |
| Normal cleanup | Both child rc0,normal FINISH/node destroy/shutdown/interpreter finalization0;four owned PIDs absent,both tokens restored,identity mutations0,zero forced signals. |

The independent auditor executed the actual current `ParameterScene.analyze` against preserved real streams/ownership; complete output equals original ANALYSIS, and live BUSINESS equals recomputed content. Seven copied-raw mutations reject.17 native +11 functional repository tests pass. Audit replay is a host reanalysis of a real run, not another device execution.

## Commands, budgets and immutable package

Actual [HOST_START](../../../../build/r33-capability-gap/scene-functional-v2-query3-host/HOST_START.json) / [HOST_EXIT0](../../../../build/r33-capability-gap/scene-functional-v2-query3-host/HOST_EXIT.json) bind the following historical command. This is not permission to reuse a consumed release:

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r15-lifecycle\\publish-worktree\\tests\\ros2_target\\run_parameter_native.py",
  "run-functional-v2",
  "--prepared",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r33-capability-gap\\prepared-c289d704ce533034\\PLAN.json",
  "--release",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r33-capability-gap\\MAIN_SCENE_FUNCTIONAL_V2_QUERY3_RELEASE.json",
  "--hdc",
  "C:\\Users\\17715\\AppData\\Local\\OpenHarmony\\Sdk\\23\\toolchains\\hdc.exe",
  "--output",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r33-capability-gap\\execution-c289d704ce533034"
]
```

40 query host commands/24 exact remote markers,4 reviewed local continuation commands and224 scene host commands/208 exact shell remote markers all return0. The other16 scene commands are successful file transfers, not fabricated remote markers. Original earliest query UTC2026-10-19 02:13:36.839550 was never renewed; one query per board. Exact argv,cwd,UTC,exit/raw streams and hashes are bound by the canonical audit.

| Existing bound | Actual observation |
| --- | --- |
| Dispatch <240 s | Host start72.950 s;consumed dispatch92.798 s from original query. |
| A/B launch <=300 s |119.910 /120.542 s. |
| Business35 s / normal-close-release50 s |1.546 /9.265 s from original business origin. |
| Exit20 s |1.063 s after close. |
| Analysis60 s | Conservative HOST_EXIT-minus-final-absence upper bound12.763 s, including collection/analyze/cleanup; not invented exact analysis CPU time. |
| Probe40 s | TYPE_SUPPORT-to-FINAL spans11.396 /12.738 s; existing source guard unchanged. |
| Whole host wrapper |76.570 s including pre-business qualification/deployment, not a substitute for phase clocks. |

Five files per process were collected only after normal owned absence, using actual pre-transfer stat,receive and post-transfer remote hash against local bytes. No unexecuted pre-transfer scene hash is claimed. Query pre/post remote hashes,exit/token restoration/owned absence are independently verified.

Package `package-9e8884606c97acee` DERIVATION SHA256 `5c4cf15d2d0ea7df51ce9f1658a86a640a5f50213875b78575d2e851c3e44f85`:3,238 files,220 ELF,897 dependencies; original native ON/OFF bridge/archive/compile-command hashes are REUSED_HASH_VERIFIED_NOT_REBUILT. Current bridge remains `9db2a35c7da2cb98afa2c255ab122179e5a959a9d4a39515b11881ffe98039c1`. Only declared test-probe/manifest delta, no native rebuild or reproducibility claim. Current282-source inventory SHA256 `0b4ca39f579a3b74701234f1b0ce87aaaa9b616ee99be846009a465c34bda504` includes six uncommitted candidate files:

| Source | SHA256 |
| --- | --- |
| `tests/ros2_target/run_parameter_native.py` | `dba96f325a4bdf8adee1a713035a04b80a9a9208c2aea45107d862d3f7ebbec8` |
| `integrations/ros2/target/parameter_native_probe.py` | `6eeffdbf3221c6ef115610cdceaef83208399e59bf2fb5a0094a9f49a28c6490` |
| `tests/ros2_target/parameter_native_oracle.py` | `0d84c82928aba02e458f8b988cd8ccfe5b1129d2c575711ee91b8227343371e5` |
| `tests/ros2_target/test_parameter_native.py` | `816e793d394f31da330d1e33ba63eedc3cf6734900e715b2b9505a591beae8fb` |
| `tests/ros2_target/parameter_functional_oracle.py` | `276819400bcbdd631e9cc408d2fe88ea98b8ebd37d054d8d26e1e9c6656c8679` |
| `tests/ros2_target/test_parameter_functional.py` | `c1c15fb5ce7aa8302733e41501ddbe3b21f974e6d7519137dfa3d9b31a935e5f` |

## Provider contract and retained audit failures

The unchanged production consumer `run_actionbag_cli.py:236 ordinary_map_paths`, called by inherited `OrdinaryCliRunner.maps:659`, treats89 external objects as an allowlist. All six saved map stages load80 allowed/hash-matched external objects; all required launcher,owned libpython,FastDDS,FastCDR,both RMW,bridge,token bridge,libc++,SoftBus and rclpy providers are present. Nine allowed optional objects are consistently unloaded; equality with the whole allowlist is not required. The actual inherited consumer was replayed successfully, not merely inferred from documentation.

The auditor's initial equality-to89 assertion failed and remains sealed. Its next cross-import function-object identity assertion also failed; corrected helper compares actual source/sourcefile and invokes the inherited real global consumer. These are audit-helper contract mistakes, not production/provider/device defects. An exec-server disconnect has UNKNOWN/no execution receipt; bounded process checks found no active audit process before one local resumed replay. No device retry was performed. Ops3's sandbox Path.resolve WinError5 and unchanged normal-user recovery remain preserved too.

## Design, implementation review and host qualification

[Design review](../../../../build/r33-capability-gap/acceptance-v2-design-review/REPORT.md) approved the stock-RMW functional model with conditions before implementation. Initial FV2-T01 exposed wrong-role extra REQUEST/RESPONSE/PARAMETER_EVENT acceptance; FV2-T02 exposed boolean release schema comparing equal to1. Both failures remain sealed. Repairs enforce per-role/kernel guards and exact schema type before dispatch/output; independent fix1 initial passes.

Formal review found no actionable blocker and executed28 repository tests plus27 host controls. Post-review final host freshly ran17+11 repository tests,11 original-failure rejection controls,50 parser/semantic/full-byte/immutable-suffix/native-API-stub entries,16 actual workflow deadline cases,eight actual execute lifecycle/release cases,complete synthetic analyze/endpoint cases and27 reviewer controls. Counts overlap and are not a total of distinct tests. Other exact-same-source selected ParameterService14RPC/startup/discovery/packaged-constructor/regression evidence is explicitly retained, not claimed rerun. Host native-I/O/capsule stubs do not establish target serialization.

Actual final qualification consumed its immutable RESULT without a mock; expired routes still reject. Positive full preparation using a labeled host-clock/release fixture did not fabricate new UTC/device authority. Ops3 separately verified the exact fresh plan/helper contracts with25 offline controls; then the new real query3/window and independent raw audit supplied device proof. No unchanged source package or deadline was silently relaxed.

## Compatibility still OPEN and historical V1 failures

**COMPAT_RMW_FASTRTPS_STANDALONE_LENGTH: OPEN.** Local `rclpy.serialize_message` produces a diagnostic reserialization; public deserialize receives the complete original buffer before a marker is emitted. rclpy owns an RAII SerializedMessage and copies exactly RMW's returned length into Python-owned bytes. The selected standalone RMW assigns estimated size to length/capacity; clean dependency HEAD `eb2b49a66b02c209ce836f04b3611e1ab550c134` matches official same-commit source and deployed disassembly. This is not an R33 RMW edit. Separate typed DDS TypeSupport uses actual serializer length. No diagnostic-to-wire correlation establishes these suffixes traversed DSoftBus, wire corruption or wire leakage. Implementation-defined format does not justify arbitrary suffixes or prove correct output; a whole-byte roundtrip is not canonical/minimal serialization proof.

V2 deliberately reports parameter functionality separately from that unresolved compatibility. General serialization/serialized APIs, serialized publication, bytewise determinism and general rosbag interoperability are NOT_VERIFIED. The existing [native SQLite10 String record/play](ROS2_ROSBAG_RECORDPLAY_ACCEPTANCE.md) is its own narrow accepted scope, not proof that these parameter samples or arbitrary message families are safe for serialized publishing/bagging. No selected RMW patch or mandatory fork is planned. This supersedes the earlier undecided proposal wording; original proposal/decision snapshots remain historical and unapplied.

Original device1 FAIL: immediate exact graph assertion failed and the original graph return was not captured. The subsequent bounded readiness repair and device2 self -> UNKNOWN -> exact A/B snapshots cannot reconstruct device1. Original device2 strict FAIL:14 RPC/12-event JSON semantics were observed, but22/40 complete local samples fail exhaustion; first GetParameters.Response680 returned/632 consumed/48 suffix including nonzero bytes, SHA256 `b887d2b05b34e875e05fa62865762c22d09874284cba2d5899ee21ffa4119e55`. Both child exits71/teardown1 and restored tokens/four absent PIDs were failure cleanup, not normal success. Original strict failure is reproducible by the retained read-only `device2-diagnosis/reproduce_strict_failure.py`; this offline original-sample replay is not a new native/device test. Neither failed window supplies V2 normal-close proof.

Default automatic reconnect NOT_PROVIDED, SDK global epoch/drain UNPROVEN, dedicated SEDP NOT_QUALIFIED, no direct payload-to-RTPS mapping. Optional unique-flow failure/original-QoS custom-Transport fallback remains NOT_PROVIDED/strict UNTESTED. Selected rclpy Action early-status/late-feedback compatibility remains OPEN_UNCOORDINATED_NOT_FIXED. Fixed ordered14-RPC acceptance does not qualify broad QoS/concurrency/large messages/restarts/soak or full ROS2; GetTypeDescription is endpoints-only. R22 remains STOPPED.

Next work should separately assess general serialized consumers and bag/API boundaries with pinned full samples and independent tests, then select further bounded real-board coverage for remaining capabilities. It must not label V1 fixed, silently narrow old assertions, or claim all ROS2 complete. Current publication gate is limited to this V2 stage and still requires Main checks.

## Local immutable evidence

Device audit SEAL2,692 members; original snapshot2,172 files (different scopes). Native/package/source and raw protections were verified by the auditor; no new HDC call was made by that audit. Ignored build artifacts are local evidence, not promised remote downloads. Complete detailed commands/providers/clocks and failures are bound by canonical RESULT.

| Record | SHA256 |
| --- | --- |
| [functional-v2-device3-final-audit/RESULT.json](../../../../build/r33-capability-gap/functional-v2-device3-final-audit/RESULT.json) | `cfc8ca5f62841a9d314e6a7cafa5c1f210ea55a9fdb80fad8edb141919f24d18` |
| [functional-v2-device3-final-audit/SEAL.json](../../../../build/r33-capability-gap/functional-v2-device3-final-audit/SEAL.json) | `5d6ca8a9694a5c370fcea5fe66568c2fc1478000ee2223c9451db60afee4d813` |
| [functional-v2-device3-final-audit/REPORT.md](../../../../build/r33-capability-gap/functional-v2-device3-final-audit/REPORT.md) | `e20c6e2e5af6dd94d120515f4cea8081451508db424f4362c6df39d77462bf88` |
| [functional-v2-final/RESULT.json](../../../../build/r33-capability-gap/functional-v2-final/RESULT.json) | `1d50f030dbf1e69efea8eaf40358a0ee05725867b78f9f6af0eebd01f653056d` |
| [functional-v2-final/SEAL.json](../../../../build/r33-capability-gap/functional-v2-final/SEAL.json) | `cee09e86df3f8010f7f2e870ca9b348d66572767b97e69b82b561d66cd111fe0` |
| [functional-v2-review/RESULT.json](../../../../build/r33-capability-gap/functional-v2-review/RESULT.json) | `25a9d7c79df3d932cdb1ee411b153d02531846176d4d74075732237376a1b7cf` |
| [functional-v2-operations3-review/RESULT.json](../../../../build/r33-capability-gap/functional-v2-operations3-review/RESULT.json) | `06cf3f14350428a72b89bd4ada9256ad99f789837a96d5b1d248e36abdb4bca4` |
| [functional-v2-fix1-initial/RESULT.json](../../../../build/r33-capability-gap/functional-v2-fix1-initial/RESULT.json) | `a6e8df578a86d99a808b4940a1b75b51f7ec6057732781a04e415e2940ad0eb3` |
| [device1-failure-audit/RESULT.json](../../../../build/r33-capability-gap/device1-failure-audit/RESULT.json) | `0e7f9e8a1c3184e9c907ab7977435f0a74ff1a32be67adfc32d05e3365e254ec` |
| [device1-failure-audit/SEAL.json](../../../../build/r33-capability-gap/device1-failure-audit/SEAL.json) | `53e5dae4fd9f470de01c7e09b7adbd6991639d511728ed0feb5578682721d994` |
| [device2-failure-audit/RESULT.json](../../../../build/r33-capability-gap/device2-failure-audit/RESULT.json) | `1116f4f1cf3724ac53ee38c760632b299944bcde0e10622a609738da94b40cfa` |
| [device2-failure-audit/SEAL.json](../../../../build/r33-capability-gap/device2-failure-audit/SEAL.json) | `7ebd5f00ee33c36916184e3904f51e697e0819a1a9ed32c96eace16ef3bfc637` |
| [acceptance-v2-design-review/RESULT.json](../../../../build/r33-capability-gap/acceptance-v2-design-review/RESULT.json) | `1a87bbebd6f378e87e0fe78616b16d2ba082627be484a3841e16746818c1ce4a` |
| [functional-v2-initial/RESULT.json](../../../../build/r33-capability-gap/functional-v2-initial/RESULT.json) | `fc88223d808c9da4a5519118c81c6494625e78a8a12f9282dd74abc08fffa890` |

[Complete sample accounting](../../../../build/r33-capability-gap/functional-v2-device3-final-audit/CAPTURE_ACCOUNTING.json), [canonical audit report](../../../../build/r33-capability-gap/functional-v2-device3-final-audit/REPORT.md), [final host report](../../../../build/r33-capability-gap/functional-v2-final/REPORT.md), [formal code review](../../../../build/r33-capability-gap/functional-v2-review/REPORT.md) and [ops3 review](../../../../build/r33-capability-gap/functional-v2-operations3-review/REPORT.md) retain full supporting scope.
