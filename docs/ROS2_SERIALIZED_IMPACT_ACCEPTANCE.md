# R34 stage1: bounded typed/serialized publication and raw callback observation

Snapshot 2026-10-19T06:28:09.313739+00:00; working-tree baseline `bb007f39d3d5224d44ed5bcfa8294a68b337b32e`. Scope `R34_SERIALIZED_IMPACT_TYPED_AND_RAW` is a diagnostic experiment with unchanged stock RMW/native runtime. **DEVICE_TESTED / CROSS_DEVICE_TESTED PASS for this finite A-to-B scope only.** Strong DDS payload-boundary proof remains **BLOCKED_PROVENANCE_GAP**; general serialized compatibility OPEN; R34 bag NOT_TESTED; full ROS2 NOT_VERIFIED. [Central matrix](ROS2_CAPABILITY_MATRIX.md).

## Observed result and attribution

One public rclpy Node per board uses four dedicated topics: typed/serialized ParameterEvent and typed/serialized String,with two distinct indexes per topic. The ParameterEvents are fixed nested-message fixtures,not a new execution of parameter services or automatically emitted mutation events. QoS is RELIABLE/VOLATILE,KEEP_LAST depth10. A makes8 public publish calls; B owns typed and raw subscriptions on each topic and receives16 callbacks. Eight local serialize buffers plus eight raw callback buffers are retained in full. No receiver reserialization,truncation,suffix removal or byte normalization is used.

| Fixture / publication | Local full bytes / schema consumed / suffix | Raw callback full bytes / consumed / suffix | Actual outcome |
| --- | --- | --- | --- |
| ParameterEvent,typed,indexes1 and2 |148 /140 /8 |140 /140 /0 | Typed fields and full native deserialize agree. Local diagnostic serialization and actual typed serialization are separate operations. |
| ParameterEvent,bytes,indexes1 and2 |148 /140 /8 |148 /140 /8 | Full original bytes,complete-buffer hashes and suffix hashes match at source/raw receiver; native decode and typed fields agree. |
| String,typed and bytes,two indexes each |40 /40 /0 |40 /40 /0 | Four clean controls; all fields/native decodes agree. |

The source/raw comparison is reported as observation,not imposed as a required equality or required suffix size. This run's observed suffix nonzero-byte count is0; it does not erase [R33's22 suffix samples/11 nonzero samples](ROS2_NATIVE_PARAMETER_ACCEPTANCE.md),prove safe padding,canonical/minimal serialization or deterministic output. Complete native deserialization receives each original buffer unchanged. The local schema parser reports consumed length separately; successful deserialization does not validate every trailing byte's provenance.

Publisher MessageInfo source timestamp and publication sequence match across each typed/raw callback pair. Reader-specific reception timestamps/sequence counters are retained without requiring equality; unsupported counters remain null. No cross-topic total order or shared clock is inferred. The complete log includes callbacks after provisional completion until explicit final observation cutoff; duplicates/extras reject. The250ms quiet observation is bounded,not unbounded absence.

Accepted origin is **NATIVE_RAW_CALLBACK_BUFFER**: selected public Publisher dispatches message objects to typed publish and bytes to publish_raw; selected Executor forwards native take's tuple to a two-argument callback. The probe records raw callback bytes before complete-buffer native deserialization. Module hashes,PyCapsules and package/provider maps are bound,but full source/build linkage for the stripped rclpy extension remains incomplete. Consequently these observations do not attest DDS serializedPayload length,physical wire bytes,per-payload RTPS mapping or recorder-internal take input. Stock RMW,DDS/RTPS,Transport and DSoftBus Socket behavior were not changed.

## Build,tests,review and original failure

| Stage | Evidence-backed status |
| --- | --- |
| Implementation | PRESENT:four diagnostic probe/oracle/runner/test candidates listed below. |
| Native build | REUSED_HASH_VERIFIED_NOT_REBUILT; no fresh native compilation or whole-build-reproducibility claim. |
| Final host | PASS:81 repository tests (R34 20,R33 native17,functional11,R32 18,R31 15),plus fresh explicit controls. |
| Independent code/operations review | PASS for stage1 after R34-D01 repair; exact fresh operational plan reviewed before execution. |
| Device/integration | PASS for one fresh paired RK3588A A-to-B scene; audit replays preserved raw evidence without another board run. |
| Final technical tester Commit gate | PASS for bounded public typed/bytes publish/native raw callback observation only. |
| Documentation | Synchronized for stage1; Main precommit/publication gates still PENDING at this snapshot. |
| Strong DDS boundary / R34 bag / overall ROS2 | BLOCKED_PROVENANCE_GAP / NOT_TESTED / NOT_VERIFIED. |

Original query1 remains FAIL with zero publications/buffer records: inherited instance string `self.topic` shadowed the new `topic()` helper before measured endpoint creation. Both child exits71/native teardown1 were failure cleanup,despite four PIDs absent/restored tokens. R34-D01 minimally renames that helper and six uses to `lane_topic`,preserving inherited topic and all measurement/deadline/ownership contracts. The test fixture now executes the real constructor chain; host tests additionally cover actual packaged constructors. Oracle and runner did not change in that repair. No stock-RMW/native fix or old-result promotion is claimed.

Independent final host testing runs17 actual source and17 actual packaged constructor-to-communicate controls,seven old/new reviewer controls and other metadata/readiness/type-support/execute/deadline/configuration/admission/package negatives. Counts overlap; they are not an exhaustive unique-test total. Native rclpy/serialization I/O in host controls uses explicit stubs and supplies no board-serialization proof. Final qualification consumes immutable RESULT bytes separately; positive preparation uses labeled historical clock/in-memory fixtures,while actual stale routes reject.

Initial auxiliary history is **PARTIAL**: a first premature support_readiness receipt/output was overwritten and its original times remain **UNKNOWN**. The preserved tool trace is a transcription,not recovered original stderr. Current final exclusive receipts do not retroactively repair that gap. Independent repair review explicitly retained this limit. Original query1's1300 snapshots and failures remain unchanged. Final host aggregation's mistaken expected exit codes and five final-audit helper failures are retained with distinct fresh corrected receipts; they are not silently discarded or product fixes. Canonical final replay equals original ImpactScene.analyze and8 copied-original-data negatives reject.

## Actual target,command and lifecycle

The registered RK3588A OpenHarmony A/B pair uses SDK23/AArch64,project-linked CPython3.12.7,ROS2 Jazzy/rmw_fastrtps_cpp,project FastDDS2.14 and DSoftBus Socket providers. Native selected configuration remains custom-only,with the previously qualified experimental outgoing-first A backend and ordinary B backend; no default reconnect or topology change is implied. Query `6bc41cb2deba743a`,scenario `1e7ff88669a22014`,execution/output `147dc09d418ab3f5`; exact target IDs/deploy roots/native commands and ownership are retained in the local immutable receipts.

Historical host argv from [HOST_START](../../../../build/r34-serialized-impact/scene-raw-query2-host/HOST_START.json),with root-relative arguments below expanded against the workspace,was:

```text
C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B
  build/r15-lifecycle/publish-worktree/tests/ros2_target/run_serialized_impact.py run
  --prepared build/r34-serialized-impact/prepared-147dc09d418ab3f5/PLAN.json
  --release build/r34-serialized-impact/MAIN_SCENE_RAW_QUERY2_RELEASE.json
  --hdc C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe
  --output build/r34-serialized-impact/execution-147dc09d418ab3f5
```

This records a consumed once-only release,not authorization to rerun. All230 scene host commands,40 query host commands and4 continuation commands exited0;214 scene/24 query shell commands contain exactly one remote zero marker,the remainder include successful transfers. [HOST_EXIT0](../../../../build/r34-serialized-impact/scene-raw-query2-host/HOST_EXIT.json) binds outer execution. No safety bound was increased and documentation/audit made no new HDC call.

| Existing bound | Actual observed interval |
| --- | --- |
| Dispatch240s / launch300s | Host start93.852s;A/B launch143.859/144.518s from original query anchor2026-10-19 04:43:52.589775UTC. |
| Business35s / probe40s |1.657s business;3.116/4.661s A/B probe observation. |
| Normal close release50s / final exit20s |9.360s /1.015s. |
| Local analysis60s |13.681s enclosing wall upper bound from initial normal owned-absence to outer completion;not an invented exact monotonic analysis stamp. |

Both children exit0,interpreter execution/finalize/native teardown0,four owned child/supervisor PIDs absent before collection,both identity tokens restored,zero forced signals. Ten complete logs have actual stat/transfer/post-transfer remote hash bindings; no nonexistent pre-transfer hash is claimed. Normal absence and later finally-cleanup absence remain distinct. Six provider snapshots pass the actual inherited ordinary-map contract:observed approved external objects may be a subset of the allowlist. Native capture supplies fake_sdk=0,custom-only participant,balanced SDK operations,structural bidirectional writer sequences,participant deletion/native close. Application payload-to-RTPS mapping remains NOT_DIRECTLY_MAPPED.

## Frozen sources and package

286-source inventory SHA256 `7fe776dc1d8df6984d127335ac56a28219bfb70e64d7fcb871d7b70adb292ac6`. Package `package-6afcb66e9b25f91c` DERIVATION SHA256 `525783854268c9c85960f23ffa7b061e863be7ec17d6a07a47d006cf76eac25f` contains3239 files,220 ELF objects and897 compiler dependencies; complete native payload/closure hashes are retained. Bridge/native artifacts are reused,not rebuilt.

| Diagnostic candidate | SHA256 |
| --- | --- |
| `integrations/ros2/target/serialized_impact_probe.py` | `af98b20d6c1363ee9b7cde2a9be6c2ad695cd31115289bb137e0d3395de5ca0d` |
| `tests/ros2_target/serialized_impact_oracle.py` | `d8fda62f994245aff36dd87c4ddecf8f98742884bb8478dcee406682db560596` |
| `tests/ros2_target/run_serialized_impact.py` | `76048a67b5bdfc661cfd0dc0855e639c8716c24217e1e21589616f8ac477fee0` |
| `tests/ros2_target/test_serialized_impact.py` | `314150d58c1b54de2c7672a04282c90e68ad1452a19d1be36ce8bb813db7b0d2` |

## Bag decision and remaining gaps

**R34 bag remains NOT_TESTED.** [Independent feasibility review](../../../../build/r34-serialized-impact/bag-followup-review/REPORT.md) blocks the original simultaneous A publisher/B raw observer/B stock recorder plan. Current bridge/backend has one peer,two Participant slots and one accepted owner; adding an observer process/node name does not qualify a third native Participant/route. The observer would still be a different subscription from recorder-internal take. No third observer or native topology change is selected.

A separate narrower candidate may use exactly two Participants in a RECORD phase,an independently sealed/read-only closed-SQLite audit,then a fresh two-Participant PLAY phase. It is **NOT_IMPLEMENTED / NOT_TESTED** and does not complete the original phase2 recorder-input goal. Internal recorder input remains unobservable. Stored BLOB/replay bytes could be compared by topic+sample index using full bytes,lengths/hashes/fields,without assuming cross-topic timestamp order. Typed stock serialization is distinct from local diagnostic serialization. Package/type-support presence and source contracts are prerequisites,not ParameterEvent execution proof. Each new phase needs its own design,source/package binding,tests/review,operational admission and normal lifecycle evidence; original safety bounds remain. [Historical ten-String rosbag PASS](ROS2_ROSBAG_RECORDPLAY_ACCEPTANCE.md) remains a separate bounded qualification.

No broad QoS/concurrency/large-message/fragmentation/stress/soak/leak-freedom qualification follows from this stage. Default automatic reconnect NOT_PROVIDED,SDK epoch/global drain NOT_PROVEN,dedicated SEDP NOT_QUALIFIED,unique-flow NOT_PROVIDED/strict UNTESTED and selected rclpy Action early-status/late-feedback compatibility OPEN remain. R22 STOPPED; full ROS2 NOT_VERIFIED.

## Immutable local evidence and publication

These ignored build artifacts are local supporting evidence,not promised remote downloads. Full original buffers remain private; tracked documentation contains only lengths/digests,redacted semantics,limited commands and evidence references. Canonical device seal covers1791 members,including1548 original snapshots;host seal483;bag review seal134. The documentation updater independently rehashed these seals and protected286 sources/22 user paths/four worktree indices without board or source mutations.

| Record | SHA256 |
| --- | --- |
| [d01-device2-final-audit/RESULT.json](../../../../build/r34-serialized-impact/d01-device2-final-audit/RESULT.json) | `578e3bc511c424c83c3d120153ad3100ad9ce56777031d5fb38d7e608f00e387` |
| [d01-device2-final-audit/SEAL.json](../../../../build/r34-serialized-impact/d01-device2-final-audit/SEAL.json) | `8acf6b7fddfdaa6a364ed322d35e231d7ae76e2080460a40e04ebad87728fbae` |
| [d01-final-host/RESULT.json](../../../../build/r34-serialized-impact/d01-final-host/RESULT.json) | `78878238211ab39e530c1a1a96a8c8acc6156bb3f8536795d2e5b3cbeced3a19` |
| [d01-final-host/SEAL.json](../../../../build/r34-serialized-impact/d01-final-host/SEAL.json) | `000481bc71ec5202c7612a387a6badcbab80f7d75ef58b592df17ef40f8e86c9` |
| [d01-code-review/RESULT.json](../../../../build/r34-serialized-impact/d01-code-review/RESULT.json) | `ab86407da81ad58bbe5a343f53eab757d7f0b35fa0c60bc36fa2ac90e82ae936` |
| [bag-followup-review/RESULT.json](../../../../build/r34-serialized-impact/bag-followup-review/RESULT.json) | `4bfe353a27cf8515f77bfd6a65d6f7fdcffe4d48ebf64eca4bf722c94659ad50` |
| [bag-followup-review/SEAL.json](../../../../build/r34-serialized-impact/bag-followup-review/SEAL.json) | `243dcbe1fa60c8e00b4b5c46b0dedb66a86caa1e25aa6a653ae1304c7f96a910` |
| [d01-device2-final-audit/REPORT.md](../../../../build/r34-serialized-impact/d01-device2-final-audit/REPORT.md) | `cf9ed3be0d5841bd6aeda81fd995e444df89c5b5d1043c8d441dbe9fcd8a622c` |
| [d01-device2-final-audit/CONTENT_SUMMARY.json](../../../../build/r34-serialized-impact/d01-device2-final-audit/CONTENT_SUMMARY.json) | `f2ea8c24fe959728520d56c2747b632a718e15faea4bca5f69c2e3c397d07312` |
| [d01-initial-host/RESULT.json](../../../../build/r34-serialized-impact/d01-initial-host/RESULT.json) | `19fa413a5ed275c946cf1da494ce9106dbda89de4a283a9e337c995ebdac2451` |
| [ops-query2-review/RESULT.json](../../../../build/r34-serialized-impact/ops-query2-review/RESULT.json) | `d1d83907b186f585e20cb5c0a03f0b263a871151d987d9ddb3d2ca20f0dcf11d` |
| [device-query1-failure-audit/RESULT.json](../../../../build/r34-serialized-impact/device-query1-failure-audit/RESULT.json) | `9cdf8b27a0023e5660b63cb254aabab97f48b6e6c06174a06458754c775099ae` |

R33 functional V2 is published as `bb007f39d3d5224d44ed5bcfa8294a68b337b32e` (2026-10-19 02:50:54.316289 UTC; exact-SHA CI NOT_RUN, zero runs/checks/statuses), confirmed by root-relative `build/r33-capability-gap/main-functional-v2-publication/POST_PUBLISH.json`. At this 2026-10-19T06:28:09.313739+00:00 pre-commit documentation snapshot, R34 Main graph/PRE_COMMIT/commit/push/CI remain PENDING. Actual later outcomes belong in `build/r34-serialized-impact/main-publication/POST_PUBLISH.json`; no future SHA or PASS is prefilled.

Documentation Ready for commit: YES **only for R34 stage1**. This does not authorize the blocked bag scope or replace Main graph/PRE_COMMIT,commit,push or CI checks.
