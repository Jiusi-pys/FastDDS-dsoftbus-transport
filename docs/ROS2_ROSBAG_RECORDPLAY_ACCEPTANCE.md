# ROS 2 bounded cross-board rosbag record/play acceptance

## R34 follow-up boundary (2026-10-19T06:28:09.313739+00:00)

The historical ten-String record/play PASS below retains its exact scope. [R34 stage1](ROS2_SERIALIZED_IMPACT_ACCEPTANCE.md) tests typed/bytes publishing and native raw callbacks only; **R34 ParameterEvent/multi-topic bag record/play is NOT_TESTED**. Its raw callback buffers do not reveal recorder-internal take input or DDS serializedPayload/wire boundaries.

The [bag feasibility review](../../../../build/r34-serialized-impact/bag-followup-review/REPORT.md) blocks the original simultaneous publisher/observer/recorder proposal: the current native bridge has one peer/two Participants, and a parallel observer would still be a different subscription from the recorder. Do not add a third observer or silently modify native topology. A separately named narrower candidate could use two-process record,independently audited closed SQLite bag,then two-process play with fresh admission. It is NOT_IMPLEMENTED/NOT_TESTED; internal recorder input remains unobservable,so it cannot be called completion of the original phase2 goal. Package/type-support presence does not prove ParameterEvent storage/replay execution. Stock runtime,old String oracle and safety bounds retain their original contracts.

The following dated acceptance and its original failures remain historical evidence.

> **Publication update (2026-10-18):** This bounded record/play stage was published as `6d025da0efd088be46fb59fdadfd18755f5cc31e` on existing `origin/codex/flow-encoding-published-20261014`; exact-SHA CI at 10:45:07.955187 UTC was NOT_RUN (zero runs/checks/status contexts). The original acceptance snapshot below is retained. Subsequent [R26 sustained application roundtrip evidence](ROS2_SUSTAINED_ROUNDTRIP_ACCEPTANCE.md) is a separate fixed String pub/sub case, not expanded rosbag or service/action qualification.

Evidence snapshot: 2026-10-18 10:33 UTC. **DEVICE_TESTED and
INTEGRATION_TESTED PASS for one ten-String record/play chain on two RK3588A
boards. General rosbag and full ROS 2 support remain PARTIAL / NOT VERIFIED.**
Independent final tester Commit gate PASS covers exactly seven source files.
Main graph analysis, PRE_COMMIT and publication for this stage remain pending.
See [current capability matrix](STATUS.md), [SQLite oracle](ROS2_ROSBAG_SQLITE_ORACLE.md)
and the separate [no-Node ABI prerequisite](ROS2_ROSBAG_ABI_ACCEPTANCE.md).

## Actual path and scope

Record5 `1403cdfc8beb937b` published ten original `std_msgs/msg/String`
messages on A and recorded them with stock `ros2 bag record` on B. Each original
CDR payload is 36 bytes; text is `r25bag_1403cdfc8beb937b_000` through `_009`.
Ten publications were acknowledged. B received one guarded SIGTERM for normal
stock recorder shutdown, both children exited 0, all four owned child/supervisor
PIDs were absent before collection, and both identity tokens were restored.
There were no forced cleanup signals. All 275 recorded commands passed.

The collected closed bag contains only `bag_0.db3` (28,672 bytes) and
`metadata.yaml` (1,272 bytes). Remote pre/post and received hashes agree. The
read-only oracle checked schema, topic, actual offered QoS/type hash/definition,
metadata, ten persisted CDR blobs and order against actual original publication
records. It did not repair, checkpoint, reindex or silently normalize bag bytes.

Play1 `5ca772d545190a98` used that same successful immutable recording on B
and a subscriber on A. The stock player started paused; one actual
`/bagplayer_5ca772d545190a98/resume` service request returned successfully.
Ten received CDR blobs, text and sequence exactly match the original publications.
B returned naturally; both children exited 0, four owned PIDs were absent and
both tokens restored. Playback used zero TERM/KILL/forced signals. All 279
recorded commands passed. Original bag and deployed playback inputs retained
their hashes and lengths before and after. Each window also had its own fresh
40-command paired query and four successful host continuation commands.

The native binding uses `BYTES_NATIVE` / `OHOS_PUBLIC_SOCKET_EXPERIMENTAL`,
listener `fake_sdk=0`, deployed project bridge/Fast DDS and system DSoftBus
providers. The independent audits re-evaluated raw native captures and provider
maps. This is actual DSoftBus Socket-path integration evidence, not a host stub.
It does not directly map every ROS payload or Resume request to an RTPS sequence.

## Environment and target

| Item | Selected environment |
| --- | --- |
| A | RK3588A OpenHarmony, HDC `3e01ff55454d202020104033bf453b00` |
| B | RK3588A OpenHarmony, HDC `3e01ff55454d202020104433991c3b00` |
| Target runtime | SDK23 / AArch64; project-linked CPython 3.12.7; ROS 2 Jazzy components; `rmw_fastrtps_cpp`, project Fast DDS 2.14; ROS domain 201 |
| Identity | Existing `mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16`; A/B tokens 672006291 / 672042405; no identity/security changes in these runs |
| Deployment roots | `/data/local/tmp/mdds_dds_1403cdfc8beb937b_{A,B}` for recording; `/data/local/tmp/mdds_dds_5ca772d545190a98_{A,B}` for playback |
| Artifact | Combined package with 3,234 files and 220 ELF objects; `RUNTIME_PACKAGE.json` SHA-256 `17bc0f79af03c3d8a3b9dab89412091d344cf949aa8ccb2da91df56e3ec9724c` |
| Host | Windows workspace `C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`; Python `C:/pixi_ws/.pixi/envs/default/python.exe`; SDK23 `hdc.exe` |

The composer adds the pinned existing String C++ typesupport roots and probes,
preserves native payload/RECORD bytes and checks combined ELF closure. Existing
native compilation artifacts and retained readelf receipts were reused with hash
provenance. This stage does not claim a complete native rebuild, an ABI rerun or
`whole_build_reproducible=true`. The no-Node ABI evidence remains a prerequisite
with its own narrower scope.

## Changes, host tests and retained failures

The seven-file scope comprises the record/play composer, A-side public single
Node probe, finite host runner, their three test files and the tracked
`bag_sqlite_oracle.py` contract correction. B executes stock rosbag CLI. The runner
requires current producer evidence, fresh paired routes, exact release and owned
process checks; successful recording includes normal close and immutable
collection. Playback admits only successful source recordings and uses one
Resume call in a separate window. No DDS/RTPS/RMW semantics were changed.

Typesupport/cleanup fix2 initial testing, independent review and final testing
precede the target runs. The final host report records 20 composition and 30
runner tests, 24 replayed review controls, 25 cleanup boundary controls and six
existing no-repeat/error controls; overlapping controls are not added into a
fabricated distinct-requirement count. Existing same-source probe and composition
evidence remains bound by the current producer gates. The subsequent oracle
contract change passed 23 oracle tests, 32 runner tests and 19 controls, independent
review and final re-testing. The real consumer accepted the mixed six current
composition/harness initial-review-final producers against the unchanged runtime.
Host checks, generated fixtures and schema-only plans do not supply device PASS.

| Prior attempt | Preserved outcome and correction |
| --- | --- |
| Record2 | FAIL: CRLF in rosidl resource-index entries broke message-definition lookup. The correction normalizes only 12 pinned rosidl resource-index files; message-definition payload bytes remain unchanged. Failure and diagnosis remain preserved. |
| Record3 | FAIL: required String C++ typesupport was missing; failure cleanup also had a defect. The composer now includes pinned C++ typesupport and cleanup ownership/order checks were repaired, independently reviewed and retested. |
| Record4 | Ten real messages reached disk, but the runner was BLOCKED by its host oracle call/metadata contract. It remains BLOCKED and is refused as a playback source. |
| Oracle contract correction | Both runner call sites now invoke the actual `verify_recorded_bag` API. Official empty `custom_data` encoding accepts only explicit null or an empty mapping, while rejecting absent/nonempty/incorrect types; this follows the selected encoder/decoder rather than changing expected results to hide a failure. |
| Record5 then Play1 | New successful windows after fixes; only successful record5 supplied playback input. Earlier FAIL/BLOCKED records were not rewritten or promoted. |

Original BSI-01/BSO-01 offline fixture/type-coercion failures, ABI unknown-start/
expired windows and earlier audit-helper failures also retain their historical
status. This success does not erase those records.

## Capability boundaries

| Capability | Status and reason |
| --- | --- |
| Fixed String pub/sub, SQLite recording and replay | SUPPORTED within this exact A-to-B record then B-to-A replay case; DEVICE/INTEGRATION_TESTED PASS, ten 36-byte CDR payloads in order. |
| Pause/resume service | PARTIAL: one real Resume request/response and subsequent playback passed. Arbitrary services/actions are not qualified by it. |
| QoS and serialization | PARTIAL: selected offered QoS/type support, exact String CDR and acknowledgments passed. General QoS combinations and general types remain UNVERIFIED. |
| Lifecycle/resources | Bounded normal recorder stop, natural player EOF, child exits, owned PID absence and identity restoration passed. SDK epoch/callback quiescence remains NOT_PROVEN; broader lifecycle qualification is not implied. |
| Discovery/native transport | Actual paired route, endpoint and native Socket-path observations passed for these windows. Dedicated SEDP is NOT_QUALIFIED; direct per-payload RTPS mapping is NOT_PROVEN. |
| Unique network flow | NOT_PROVIDED; strict mode UNTESTED. Both diagnostic pairs are retained; startup is not represented as error-free. |
| Other ROS 2 capabilities | Earlier native C++ Action and ordinary node/topic/Action CLI PASS retain their original bounded evidence. This stage neither replaces nor expands them. |
| Reconnect, large/fragmented messages, concurrency/stress, other types, MCAP/compression/split bags | UNVERIFIED by this stage; no applicable execution evidence here. R22 remains STOPPED. |
| Full ROS 2 parity | NOT VERIFIED. This successful fixed chain is partial coverage, not general product qualification. |

## Execution receipts and sealed evidence

Paths in the evidence tables are relative to root `build/r25-native-actionbag/`.
These are local ignored build artifacts; repository publication does not upload
them or promise that they can be downloaded from the remote. Hashes bind the
local receipts. Recorded commands below are historical exact invocations, not
permission to reuse consumed releases or stale routes. A new run requires fresh
preparation/review and the project gates.

### record5: exact observed host argv

UTC `2026-10-18T10:15:10.488752+00:00` to `2026-10-18T10:16:15.329808+00:00`, host exit **0**.
Cwd: `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport`. Receipt SHA-256 `7d0e260af2f9f0a9c813cf4c50d9078a8530b1692087b89296bc2c359f6879bd`.

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r15-lifecycle\\publish-worktree\\tests\\ros2_target\\run_actionbag_bag_recordplay.py",
  "run",
  "--prepared",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r25-native-actionbag\\bag-recordplay-run-058448a48b2ddb0f-prepared\\PLAN.json",
  "--release",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r25-native-actionbag\\bag-recordplay-record5-window\\MAIN_RECORD_RELEASE.json",
  "--output",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r25-native-actionbag\\bag-recordplay-run-058448a48b2ddb0f-execution",
  "--hdc",
  "C:\\Users\\17715\\AppData\\Local\\OpenHarmony\\Sdk\\23\\toolchains\\hdc.exe"
]
```

Actual B dispatch argv (stock CLI through the pinned entry):

```json
[
  "/data/local/tmp/mdds_dds_1403cdfc8beb937b_B/package/run-token-first",
  "ros2",
  "bag",
  "record",
  "--output",
  "/data/local/tmp/mdds_dds_1403cdfc8beb937b_B/bag",
  "--storage",
  "sqlite3",
  "--storage-preset-profile",
  "none",
  "--compression-mode",
  "none",
  "--max-cache-size",
  "0",
  "--disable-keyboard-controls",
  "--node-name",
  "bagrec_1403cdfc8beb937b",
  "--qos-profile-overrides-path",
  "/data/local/tmp/mdds_dds_1403cdfc8beb937b_B/qos.yaml",
  "--topics",
  "/bag_1403cdfc8beb937b"
]
```

### play1: exact observed host argv

UTC `2026-10-18T10:28:52.955680+00:00` to `2026-10-18T10:29:58.807533+00:00`, host exit **0**.
Cwd: `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport`. Receipt SHA-256 `09e1933defad35b128126e0c6c16bd8f0ea9d1f9c901b86941cb181de690d099`.

```json
[
  "C:\\pixi_ws\\.pixi\\envs\\default\\python.exe",
  "-X",
  "utf8",
  "-B",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r15-lifecycle\\publish-worktree\\tests\\ros2_target\\run_actionbag_bag_recordplay.py",
  "run",
  "--prepared",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r25-native-actionbag\\bag-recordplay-run-46e5e9bb4e5fe6c8-prepared\\PLAN.json",
  "--release",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r25-native-actionbag\\bag-recordplay-play1-window\\MAIN_RECORD_RELEASE.json",
  "--output",
  "C:\\Users\\17715\\Documents\\codes\\FastDDS-dsoftbus-Transport\\build\\r25-native-actionbag\\bag-recordplay-run-46e5e9bb4e5fe6c8-execution",
  "--hdc",
  "C:\\Users\\17715\\AppData\\Local\\OpenHarmony\\Sdk\\23\\toolchains\\hdc.exe"
]
```

Actual B dispatch argv (stock CLI through the pinned entry):

```json
[
  "/data/local/tmp/mdds_dds_5ca772d545190a98_B/package/run-token-first",
  "ros2",
  "bag",
  "play",
  "--input",
  "/data/local/tmp/mdds_dds_5ca772d545190a98_B/input/bag",
  "sqlite3",
  "--topics",
  "/bag_1403cdfc8beb937b",
  "--remap",
  "/bag_1403cdfc8beb937b:=/bagplay_5ca772d545190a98",
  "__node:=bagplayer_5ca772d545190a98",
  "--start-paused",
  "--disable-keyboard-controls",
  "--wait-for-all-acked",
  "5000",
  "--qos-profile-overrides-path",
  "/data/local/tmp/mdds_dds_5ca772d545190a98_B/qos.yaml"
]
```


| Local evidence | SHA-256 |
| --- | --- |
| [bag-recordplay-typesupport-cleanup-fix2-initial-test/composition/RESULT.json](../../../../build/r25-native-actionbag/bag-recordplay-typesupport-cleanup-fix2-initial-test/composition/RESULT.json) | `9828c9b0ab61e32855d2bf0ab71cade73e31f85ac72823ece581111e4d378306` |
| [bag-recordplay-typesupport-cleanup-fix2-initial-test/composition/SEAL.json](../../../../build/r25-native-actionbag/bag-recordplay-typesupport-cleanup-fix2-initial-test/composition/SEAL.json) | `7f519c5e65203249f407bf135f487cb1845438a00aad7bda92666fc434cdaf82` |
| [bag-recordplay-typesupport-cleanup-code-review/composition/RESULT.json](../../../../build/r25-native-actionbag/bag-recordplay-typesupport-cleanup-code-review/composition/RESULT.json) | `0a52b446b74bcfec25f3355c6c77720d4354157ecd1571cfaa19a7a205c0ba71` |
| [bag-recordplay-typesupport-cleanup-code-review/composition/SEAL.json](../../../../build/r25-native-actionbag/bag-recordplay-typesupport-cleanup-code-review/composition/SEAL.json) | `38136c1ca159b2eba6dddb685115b2176afff5c7540ff16ba936628545dc7490` |
| [bag-recordplay-typesupport-cleanup-final-test/composition/RESULT.json](../../../../build/r25-native-actionbag/bag-recordplay-typesupport-cleanup-final-test/composition/RESULT.json) | `fee6724446cd88df90b769de1f7f655a5f7a0a991b76e73ccc09cd0568e6e052` |
| [bag-recordplay-typesupport-cleanup-final-test/composition/SEAL.json](../../../../build/r25-native-actionbag/bag-recordplay-typesupport-cleanup-final-test/composition/SEAL.json) | `948256ebc7fa44cc9d858766d24efd19d2b78dfc658f15f7e45827fb1a1d9bd0` |
| [bag-recordplay-oracle-contract-code-review/harness/RESULT.json](../../../../build/r25-native-actionbag/bag-recordplay-oracle-contract-code-review/harness/RESULT.json) | `4df4effed4a1d73dc4f376d6edd30192c2ae0036d83e62238267c9fae20ca903` |
| [bag-recordplay-oracle-contract-code-review/harness/SEAL.json](../../../../build/r25-native-actionbag/bag-recordplay-oracle-contract-code-review/harness/SEAL.json) | `99f0a56497d639ac14d0dc6892bfa3cc04ea366a9d91b331565ec66ffb42c03d` |
| [bag-recordplay-oracle-contract-final-test/harness/RESULT.json](../../../../build/r25-native-actionbag/bag-recordplay-oracle-contract-final-test/harness/RESULT.json) | `e7eb6b9916399e0e4a77b17ce547d486ee2d5e376b35f5242dede6714d75542f` |
| [bag-recordplay-oracle-contract-final-test/harness/SEAL.json](../../../../build/r25-native-actionbag/bag-recordplay-oracle-contract-final-test/harness/SEAL.json) | `10d6e47b0fc90f3e36f8054d0807b5e715f3efd4d9e26999b6119eb8af4acbff` |
| [bag-recordplay-oracle-contract-initial-test/RESULT.json](../../../../build/r25-native-actionbag/bag-recordplay-oracle-contract-initial-test/RESULT.json) | `3da2da3283305fb57febd8159f53ac226a00d52d236029d05ec0105a25acf2b9` |
| [bag-recordplay-oracle-contract-initial-test/SEAL.json](../../../../build/r25-native-actionbag/bag-recordplay-oracle-contract-initial-test/SEAL.json) | `bf3f5f060fd4d59077cce4f2325c1e1205125ba469ce0723d150b7697d50f9a1` |
| [bag-recordplay-record5-device-audit/RESULT.json](../../../../build/r25-native-actionbag/bag-recordplay-record5-device-audit/RESULT.json) | `7cef82e058f1bc4efb2c61ed387949c4dc5f974d86f12c59254432e85e69e12c` |
| [bag-recordplay-record5-device-audit/SEAL.json](../../../../build/r25-native-actionbag/bag-recordplay-record5-device-audit/SEAL.json) | `ec936a60decc43afbdaa5c5748dc917e85dc0ac895a3db0ebca3ae68f344adb4` |
| [bag-recordplay-play1-operational-final-test/RESULT.json](../../../../build/r25-native-actionbag/bag-recordplay-play1-operational-final-test/RESULT.json) | `45556cc761fa480842870372d5818f2dad75b155c3ef2d4d803a2a9e06096174` |
| [bag-recordplay-play1-operational-final-test/SEAL.json](../../../../build/r25-native-actionbag/bag-recordplay-play1-operational-final-test/SEAL.json) | `4edd4db360e3c8e9486ead91e523952222d2596b684de5ebd1f2551670e84a08` |
| [bag-recordplay-run-058448a48b2ddb0f-execution/RESULT.json](../../../../build/r25-native-actionbag/bag-recordplay-run-058448a48b2ddb0f-execution/RESULT.json) | `323f7791c4e818c40b07c07d5999a084db3f19610dc635ee6a87c0652796c2a5` |
| [bag-recordplay-run-46e5e9bb4e5fe6c8-execution/RESULT.json](../../../../build/r25-native-actionbag/bag-recordplay-run-46e5e9bb4e5fe6c8-execution/RESULT.json) | `3d984a72ea2899af926c5548481e82aab6b4a70e03a471b8ae5a8cac1f485c2a` |
| [bag-recordplay-oracle-contract-initial-test/FREEZE.json](../../../../build/r25-native-actionbag/bag-recordplay-oracle-contract-initial-test/FREEZE.json) | `00b9e69486aad413c6d98fc4956a410c7de34704af1b5df206d35a1008ed5ac7` |
| [build/r15-lifecycle/publish-worktree/build/r25-native-actionbag/bag-recordplay-typesupport-cleanup-fix2-initial-test-build/RUNTIME_PACKAGE.json](../../../../build/r15-lifecycle/publish-worktree/build/r25-native-actionbag/bag-recordplay-typesupport-cleanup-fix2-initial-test-build/RUNTIME_PACKAGE.json) | `17bc0f79af03c3d8a3b9dab89412091d344cf949aa8ccb2da91df56e3ec9724c` |
| [bag-recordplay-play1-device-final-audit/RESULT.json](../../../../build/r25-native-actionbag/bag-recordplay-play1-device-final-audit/RESULT.json) | `23f1df5994b31fbafa25a3e230340c36cb6f0c9e5e3da5d0f6fa8c0f35b273fd` |
| [bag-recordplay-play1-device-final-audit/SEAL.json](../../../../build/r25-native-actionbag/bag-recordplay-play1-device-final-audit/SEAL.json) | `c21f86ea9af7498483b362606627357e27a2a03b78ea9fc9c1238a016a17b9a4` |
| [main-bag-sqlite-oracle-publication/POST_PUBLISH.json](../../../../build/r25-native-actionbag/main-bag-sqlite-oracle-publication/POST_PUBLISH.json) | `bc3762373db7c6dac8cabd617fe66f53cb0ca9a07c5565b1347596abc4ceba59` |

| Immutable input | Bytes / SHA-256 |
| --- | --- |
| `bag_0.db3` | 28672 / `d3d7f53eba2d298475def96823369f2824001c07ad87532552f27cd0ea9439e3` |
| `metadata.yaml` | 1272 / `64bd187e24fb5f75bc1bfb7e0a9e52aa1735f979aac885019418f6fd02a566a5` |

Final independent audit at 10:33 UTC rechecked the current producer gates,
record5 audit and play1 operational review/final seals, raw target evidence,
3,288 original evidence files, 22 protected source paths and four worktrees/
indexes. Its own HDC calls were zero: it verified the actual completed device
execution rather than replaying it. Tester Commit gate PASS is restricted to
the exact seven-source stage. Documentation synchronization does not replace
Main's graph analysis or PRE_COMMIT.

Last confirmed publication before this stage is SQLite helper
`ee04d945b993b4e7b4cdc96f9773afa3a18817ff`, normally pushed to existing
`origin/codex/flow-encoding-published-20261014`; exact-SHA CI at
2026-10-18 05:52:42 UTC was NOT_RUN (zero runs/checks/status contexts).
The preceding ABI commit `06e5917abd8906a5d17066b87223d239109c5374` is also
published. This record/play stage has no new commit, push or CI result yet.
