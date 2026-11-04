# ROS 2 rosbag native ABI prerequisite acceptance

Evidence cutoff: 2026-10-18 02:06 UTC. **Build, reviewed host checks and real
two-board no-Node ABI: PASS within this prerequisite scope.** The independent
stage tester reports `Commit gate: PASS`; documentation, Main graph analysis and
PRE_COMMIT are separate gates. This document does not assert a new publication.

No Node, Recorder or Player was created. Native cross-board recording, persisted
SQLite message/CDR/order verification and replay remain **NOT_RUN / UNVERIFIED**.
The device result explicitly records `communication_verified=false` and
`teardown_observed=0` (teardown rc=-1). Interpreter finalization and process exit
do not prove Transport close or SDK callback quiescence. R22 remains STOPPED.

## Scope and source bindings

The five additions provide a native SQLite rosbag overlay builder, static runtime
composer, no-Node probe, finite ABI runner and host tests. They make no changes to
DDS/RTPS/RMW semantics or production Transport behavior. The runner's executable
case is `bag_imports_qos_pair`; record/play is a subsequent implementation stage.

| File | SHA-256 at acceptance |
| --- | --- |
| `integrations/ros2/target/build_actionbag_bag.py` | `38275b921269ac01964c0de2065636879fd2b6bc249a7b691d142cec05f9e46d` |
| `integrations/ros2/target/compose_actionbag_bag_runtime.py` | `2a984e347094db03f80dbd8c20c695d5bf937f922a24fd894b54b5578132e72a` |
| `integrations/ros2/target/bag_runtime_probe.py` | `996eb9a12a7e81a013eb691df44b40a6ed0fc821ee6fb3f1556ded3355deabf6` |
| `tests/ros2_target/run_actionbag_bag.py` | `7825bcb5cb39ff916d5350f3fce2a1b91390fd965f02828b2cd62c8744f98937` |
| `tests/ros2_target/test_actionbag_bag.py` | `4b4a3a5c25c65b1dddff6a0dc3826464027c9442313d7c6399ec27ac4f973185` |

`bag-abi-stage-final-test/SOURCE_BINDINGS.json` additionally binds the three
inherited native/ordinary runner and probe sources, for eight checked sources.
Its SHA-256 is `4cb781cc83caee0a16fbc476fd42e2f7c9cd715f9ab8c2bba0128a33ba824162`.
The seven-source harness freeze is
`b4bc33e1a232da6cd8fb153af3cc94a4516a3cf2c7320f5f37adece005dd0bec`;
the builder has its separate reviewed freeze. Unrelated R22 changes are excluded.

## Build and artifact

Local evidence paths below use these aliases; they are retained local artifacts,
not files promised to be present in a fresh clone:

```text
ROOT = C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport
W3   = ROOT/build/r15-lifecycle/publish-worktree
E    = ROOT/build/r25-native-actionbag
ABI  = E/bag-abi4-window
```

The selected native build is
`W3/build/r25-native-actionbag/bag-fix1-independent-build`: 293 successful build
commands, 491 overlay files, eight rosbag2_py extensions and 123 native closure
members. The post-review test reused the unchanged build after rehashing inputs
and artifacts; it did not claim a second compilation. `BUILD_RESULT.json` has
SHA-256 `b08ff96c47804a42101a1d1bddcddb50fbc0eed58c8db34d9de7e63cd48d6be4`;
`OVERLAY.json` is `fa3e4aaab603b641752f10cec7ad499f321ce1d4fc3e8cf5326ed9f274869d09`.

`whole_build_reproducible=false` remains explicit in the builder review and final
host result. The eight extension modules matched the earlier build, but eight
owned parent-prefix resources and three rebuilt C++ DSOs differed; seven
composition_interfaces object-output paths also differed. The retained root-only
compile-comparison failure was not converted into reproducibility success. These
records qualify the selected hashed build, not a fully reproducible rebuild.

The combined runtime is
`W3/build/r25-native-actionbag/bag-runtime-abi-initial-test-build`: 3,230 members,
3,228 payload files, 217 ELF objects, and 21 RECORDs containing 1,440 rows. The
composer permits the selected base plus overlay, verifies shared bytes and
performs the two recorded ros2bag RECORD share-path relocations. Static ELF and
RECORD validation alone is not runtime or communication qualification.

| Artifact | SHA-256 |
| --- | --- |
| Combined `RUNTIME_PACKAGE.json` | `461f56442cd3a0024fd0248b240ed44aa18d53235fd524ffdae70be1c5a2860d` |
| Combined `package/LAUNCHER_PACKAGE.json` | `92838d2ac2cc682085b54ff997a5364aa1dbc0b9d9fcb3d7ed0f29ea0120c483` |
| Combined `package/COMPOSITION.json` | `51fa7ee24b13ca801dde9230af9a03387e9371c20b01eabf20190edd6f59197e` |

## Review and host evidence

Independent reviews and post-review tests apply to the exact source bytes above.
The builder's `BAG-RUN-01` packaging/SQLite plugin metadata issue was repaired and
retested against the stock parser. Harness findings `BAH-01` (actual producer
gates and descriptor topology) and `BAH-02` (exact HDC binding) were closed in
`bag-abi-harness-fix1-review`; its result is
`aca17a059a9a5ab2b375c05db66e4c93e7f720562bd2914a96390b3795b59c94`.
The composition/probe review found no actionable findings; its result is
`1aa0927a49b58d37f955e6f06def66a56be27eb5c38d9eaccb23426bff393e3c`.

| Final evidence under E | Result SHA-256 | Scope |
| --- | --- | --- |
| `bag-fix1-final-host-test/RESULT.json` | `81ac95e661c3cc50b7ba491b7d6707a92b4662b7412d91f9d60e1498c2dc5cb3` | Builder negative/wheel/plugin/stock-parser checks; unchanged native build reused. |
| `bag-abi-composition-final-test/RESULT.json` | `99f8d664957522ba557a44470976eef6701ec2d519899cafac309e50a53fb686` | Package/static checks, 13 negative controls and host probe/parser fixtures. |
| `bag-abi-harness-fix1-final-test/RESULT.json` | `78647ea64fa9b8095ddc8d2c02d6520c3e3ac0c196b01bef7b71e38d88a4bc0b` | 22 unit tests, 15 independent controls and source/package/Git protection. |
| `bag-abi-stage-final-test/RESULT.json` | `34a7876923cdd07c9b9cd1c694b8efcc20df3a52ed8447a58f8dd0f594f03a7a` | 15,270 checks; rehash and fresh canonical raw-log audit, no HDC. |

Host stock-parser tests use actual parser/plugin code with explicit native-facing
stubs. They do not execute a Recorder/Player or qualify target ABI. Final stage
aggregation reused unchanged post-review results and packages, then independently
replayed the raw audit. Its 61-file seal is
`c66a05d7e596b22920ffee3f768b444f50e3d4c58b670d60d2de6685b6cfce43`.
Session 5630 exited 0. No native rebuild or device replay occurred in aggregation.

## Real RK3588A no-Node execution

Run `daf356bea873e640` used SDK23 HDC and AArch64 CPython 3.12.7. Both devices used
the previously created dedicated identity
`mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16`, normal APL and DATASYNC only.
No new identity, privilege, network or security setting was introduced.

| Side | HDC target | Token | Deployment root | Supervisor / child |
| --- | --- | --- | --- | --- |
| A | `3e01ff55454d202020104033bf453b00` | `672006291` | `/data/local/tmp/mdds_dds_daf356bea873e640_A` | `24343 / 24347` |
| B | `3e01ff55454d202020104433991c3b00` | `672042405` | `/data/local/tmp/mdds_dds_daf356bea873e640_B` | `20290 / 20294` |

The actual host command is recorded in `ABI/ACTUAL_RUN_HOST_START.json`. Using the
aliases above, its argument vector was:

```text
C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B
  W3/tests/ros2_target/run_actionbag_bag.py run
  --prepared ABI/prepared/PLAN.json
  --release ABI/MAIN_RELEASE.json
  --output ABI/execution
  --hdc C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe
```

This is an execution record, not a reusable release command. The consumed run
must not be replayed. Session 95606 exited 0; start was
2026-10-18T01:29:03.183355Z, end 01:29:46.058311Z. Preceding fresh query session
82410 and host continuation session 14636 also exited 0. Query run
`310935d4477e8a0e` has 40 calls and a 415-file seal
`80b95704a1fae21ed88db7efdf95885d6fd1c641f6cdbd52abadc81f7dc9cb1d`.

The 160 actual ABI command records in `ABI/execution/commands/` preserve complete
argv, timeout, host/remote exit, stdout/stderr and transfers. The frozen default
shell bound is 15 seconds; explicitly selected deployment send/recv, tar, full
inventory and raw-log receive calls use 30 seconds. Running-stage calls also obey
remaining stage deadlines. Main entry 240 seconds, route 300 seconds, paired
ready 60 seconds, hold 30 seconds and finish 20 seconds were not increased.

Both boards passed these actual native checks:

- All eight rosbag2_py extensions, rclpy binding and stock parser modules imported
  from the selected origins. Each board mapped 219 paths, including 58 external
  paths within 89 exact allowed providers; owned libpython was the sole provider.
- String and Resume request/response native capsules produced the three expected
  CDR records. Two public native QoS conversions round-tripped fields, durations
  and namespace conventions; these are conversions, not network QoS tests.
- Genuine stock SQLite record parser accepted `none` and `resilient`, defaulted
  to `none`, and rejected an invalid preset with exit 2. Stock play parsing kept
  paused=true and acknowledgement timeout=5000. Parsing never started playback.
- `BAG_ABI_READY -> FINISH_ACCEPTED NORMAL_COMPLETION -> FINAL` completed with
  success=true and node_created=false. Native execution/finalize returned 0,
  identity restore=1 and business status=0. All four exact owned processes had
  original ABSENT receipts; zero signals were sent. Deployment evidence remained.

All 3,230 selected local members, 34 prepared bindings, 16 Git guards and 33
protected files were checked. Full remote inventories matched before and after
cleanup. Original maps are `execution/commands/0066-A.stdout` and
`0079-B.stdout`. Derived `A.imports.maps`/`B.imports.maps` contain Windows CRCRLF
from `write_text`. Audit v4 uses the exact marker-bounded original stdout and
proves the precise LF-to-CRLF write transformation matches the derivative before
calling the unchanged map oracle. It does not strip arbitrary whitespace or
modify evidence.

| Device evidence under ABI | SHA-256 |
| --- | --- |
| `FINAL_TESTER_RESULT.json` | `7b190dc63b2d708f0860629c57cd9fef7176a454a9cc84530a55499af1234f97` |
| `FINAL_TESTER_SEAL.json` (753 files) | `f84893715e6dca0a5c2e1232b84a99ce17b4b873e076109ea7127ea56586679d` |
| `execution/RESULT.json` | `9e5551a6b162370f79760945fab41994dc7329c5b76172bd59a7acc63d65e88f` |
| `independent-audit-v4/RESULT.json` | `f10538779095a8bce69bb6d01bdf7f322cae74384629e750dec9576db89bf9bf` |
| `execution/CLEANUP.json` | `1b9af34237d3a4414a66964fc003d7109ce323bb902ffbc3f462ff2eea756d63` |
| `ACTUAL_RUN_HOST_EXIT.json` | `57668c1a2a903bcd9cf4c1eb83ced60f579d9180e8c2b7698fed49b29316e907` |

## Preserved unsuccessful evidence

The successful run does not rewrite earlier outcomes:

- ABI `7ef377f0726c5e42` remains UNKNOWN_START / BLOCKED_INFRA, null host exit,
  after execution transport disconnected. Later absence observations do not prove
  historical non-execution. The first `awk` reconciliation was invalid because
  awk was unavailable; corrected strict process evidence is retained separately.
- Peer3 ABI `15f3a38c8521f642` remains ENTRY_EXPIRED_NO_LAUNCH, guard exit 1
  before START/subprocess. No entry window was extended or consumed run replayed.
- ABI4 local audit schema precheck and v1/v2/v3 failed: wrong `boards` schema,
  universal 15-second assumption, derivative maps input and missing `./` inventory
  handling. Original scripts, exits and tracebacks remain; source-backed v4
  corrects audit interpretation without changing production or raw receipts.
- Stage aggregation attempt 1 (session 83415) exited 1 on a relative historical
  manifest entry resolved against cwd. Attempt 2 resolved relative entries against
  their manifest parent and passed. This changed only the new aggregation helper.
- Execution disconnects during earlier aggregation and document writing remain
  infrastructure interruptions. The four written documents were checked on resume;
  this acceptance file was absent and was then created. No interrupted write or
  Git transition is retroactively claimed successful.

## Remaining acceptance

[STATUS](STATUS.md) carries the current capability matrix. This stage supports
only the pinned SQLite build and no-Node native ABI. Next work must implement,
review and test real cross-board stock record, immutable closed SQLite/CDR and
message-order validation, and fresh-window playback with matching received
payloads. Those results must include real DDS/DSoftBus communication and normal
Transport lifecycle evidence. Host stubs and earlier R19/R20 local persistence
cannot substitute for that acceptance.

Large/general message types, concurrent record/play, cache/crash recovery,
compression, MCAP, splitting, broader QoS/reconnect/load and full ROS 2 parity
remain UNVERIFIED by this artifact. Prior published Node/topic/Action results keep
their own exact package and run boundaries. Last confirmed publication is Action
documentation commit `a0a4fcdbe0b1e2ae7c4f32abbe0f5c08074f17c3` on
`origin/codex/flow-encoding-published-20261014`; its 2026-10-17 23:41:04.775 UTC
CI observation was NOT_RUN. No new commit SHA, push or CI result is asserted here.
