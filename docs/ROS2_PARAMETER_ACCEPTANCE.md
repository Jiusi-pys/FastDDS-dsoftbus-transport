# ROS 2 Windows Host parameter acceptance

> **Current native follow-up (2026-10-19T02:39:39.888997+00:00):** [R33 functional V2](ROS2_NATIVE_PARAMETER_ACCEPTANCE.md) passed a fresh bounded RK3588A pair window using unchanged stock RMW, full original native local roundtrip samples and normal close. Both V1 device failures remain FAIL;22/40 samples in the new run still have standalone-length suffixes, so serialization/serialized publish/general bag compatibility remains OPEN. This does not convert the historical Windows fake_sdk=1 seam below into native evidence. [Matrix](ROS2_CAPABILITY_MATRIX.md). At this 2026-10-19T02:39:39.888997+00:00 pre-commit documentation snapshot, the technical tester gate is PASS only for functional V2; Main graph/PRE_COMMIT/commit/push/CI remain PENDING. Actual publication outcomes are recorded separately in root-relative `build/r33-capability-gap/main-functional-v2-publication/POST_PUBLISH.json`; no future SHA or CI success is prefilled.

As of 2026-10-15 03:34:47 UTC, the additive
`examples/ros2_parameter_acceptance` target and
`tests/ros2_host/run_parameter_acceptance.py` have independent initial testing,
agentic review, a repaired acceptance gap and independent final tester PASS for
one bounded **V1-07 PARTIAL** scenario. This is actual `rclcpp` parameter service
and event traffic between two Windows processes using `rmw_fastrtps_cpp` over
the Host opaque-bytes test seam. Builtin transports and intra-process delivery
are disabled. `fake_sdk=1` means native DSoftBus, RK3588A and cross-device
communication are **NOT VERIFIED** by this result.

The accepted scope adds three standalone acceptance files. It changes no DDS,
RTPS, QoS or production Transport behavior. It links the immutable R8 W2 final
bridge, not the concurrent R11 diagnostic working tree. That diagnostic lane
is unfinished and excluded from this stage's verification/publication; no R11
historical A/04 rerun is claimed. Main's overall pre-commit, commit/push and CI
checks remain separate from the tester's scoped commit gate.

## Accepted scenario

A runs `/param_<run>/server`; B uses `rclcpp::AsyncParametersClient`. The initial
`case` namespace contains integer `counter=10`, string `label=initial_<run>`
and read-only integer `readonly=7`, all with fixed types. Control files only
coordinate business completion and teardown; parameter values and events use
the ROS interfaces.

| RPC IDs | Assertion |
| --- | --- |
| 1, 2, 3 | Read exact initial values, describe types/read-only/dynamic-typing flags, list exactly three names |
| 4 | Non-atomic single set `counter=11`, successful result and matching change event |
| 5, 6 | Atomic set `counter=12`, `label=accepted_<run>`, matching event and readback |
| 7, 8 | Reject atomic `counter=99` plus read-only update; readback unchanged |
| 9, 10 | Reject atomic label update plus wrong counter type; readback unchanged |
| 11, 12 | Reject atomic counter update plus `case.undeclared`; original three values unchanged |
| 13, 14 | Reject atomic negative counter plus label update with exact callback reason `negative_counter`; readback unchanged |
| 15, 16 | Recover with atomic `counter=13`, `label=recovered_<run>`; matching event and readback |
| 17 | Final remote list contains exactly the original three names; server independently asserts `!has_parameter("case.undeclared")` |

These are 17 completed remote operations: ten read/metadata operations, three
accepted mutations and four rejected mutations. They are not 17 successful
mutations. Exactly three mutation events must have the expected node identity,
parameter names, types and values, with valid nanoseconds and no deleted fields.
Old values come from preceding remote reads, not old-value fields in events.
The absent parameter service stays unavailable during the 100 ms wait. A 250 ms
drain checks for extra change events within that bounded interval; it does not
exclude arbitrarily late events. Both processes and runtime close succeed,
complete Host IO captures are required, exact loaded module paths/hashes are
checked, and the owned ports must allow exclusive rebind after exit.

## Evidence and review

All `build/...` paths below are local retained evidence under the original
project root `C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`, not
under the nested publication worktree. They are not checked-in deliverables.

| Check | Corrected initial | Post-review final |
| --- | --- | --- |
| Fresh VS 2022 x64 Release `/MD` configure/build exits | 0 / 0 | 0 / 0 |
| Exactly one actual ROS pair | `fe783d3212bfb3cf` | `c9055af03a3dc7e6` |
| UTC business start | 03:18:04.710799 | 03:33:02.358147 |
| UTC business end | 03:18:06.244038 | 03:33:04.470131 |
| Remote operations | 16 | 17 |
| Accepted / rejected mutations | 3 / 4 | 3 / 4 |
| Changed / declaration events observed | 3 / 3 | 3 / 0 |
| Complete capture records A / B | 1002 / 1057 | 1030 / 1085 |
| Host sends A / B | 134 / 153 | 138 / 157 |
| Host ingress A / B | 153 / 134 | 157 / 138 |
| Send errors A / B | 0 / 0 | 0 / 0 |
| Rejected callbacks A / B | 6 / 0 | 6 / 0 |
| Process A / B / driver exits | 0 / 0 / 0 | 0 / 0 / 0 |
| Actual loaded modules per process | 8 | 8 |
| Source/helper and dependency before/after binding | 6 + 29,470, PASS | 6 + 29,470, PASS |
| Synthetic retained-log negative checks | 10 PASS | 19 PASS |
| Process exit and exclusive port rebind | PASS | PASS, ports 51969 / 51970 |

The first fresh attempt failed compilation (configure 0, build 1, zero business
runs) because the expected-value lambda lacked its closing `};`. That failure
and its 22-file evidence manifest remain intact in
`build/r11-observation/parameter-initial-test/`. The corrected initial result
and 55-file manifest are in `parameter-initial-test-corrected/`. They establish
the original 16-call scenario, not the repaired final scenario.

Independent Tier 2 review found `R11-PARAM-01` (P2): reading only the original
three names could miss a residual undeclared parameter after a rejected batch.
The fix adds RPC 17's exact name-set check and the server's explicit absence
assertion, and updates the analyzer to require both. This was an acceptance gap,
not evidence of an installed rclcpp partial-write bug. The focused review closure
found no remaining actionable concern; the subsequent final tester built and
ran the repaired sources. Final negative checks reject an orphan in the final
list, missing/false server absence, missing RPC 17, incorrect values, metadata,
events, identities and operation accounting, plus response-send timeout warnings
on either output stream. An unrelated warning still passes. These 19 checks
mutate retained logs; they are not additional ROS functional runs.

Canonical receipts are `build/r11-observation/parameter-review/REVIEW.json`,
`REVIEW_FIXES.json`, `REVIEW_FIX_CLOSURE.json`, and
`build/r11-observation/parameter-final-test/FINAL_TEST_RESULT.json` (scoped
`Commit gate: PASS`). The final directory also retains `SOURCE_SNAPSHOT.json`,
`EVIDENCE_MANIFEST.json`, `INDEPENDENT_PARAMETER_CHECK.json`,
`ACTUAL_LOADED_MODULES.json`, `ANALYZER_MUTATION_TESTS.json`,
`REVIEW_REGRESSION_CHECKS.json`, `PROCESS_PORT_CLEANUP.json` and command receipts.
The reviewer closure preceded final execution; the final tester supplies the
execution evidence rather than the reviewer inferring a pass.

The final executable is
`build/r11paramfinal/b/Release/ros2_parameter_acceptance.exe`, SHA-256
`60455fbc4d99a9edc81905bb921c82c7227244552406f17c082fbcd19a4774b4`.
The immutable bridge is
`build/r8w2final/p/bin/fastdds_dsoftbus_ros2_host_bridge.dll`, SHA-256
`94a33c8d29801beab43a6f38538cfd23d265e608e091ae04462a5dda6d5107eb`.
Eight actual module observations per process cover the executable, bridge,
Fast DDS, Fast CDR, rclcpp, rmw_fastrtps_cpp, and both rcl_interfaces C++/FastRTPS
type-support modules. Mere dependency availability is not used as load proof.

## Executed commands and environment

The following are the recorded final argument vectors rendered as PowerShell
commands. They describe the completed run, not instructions to overwrite the
existing outputs. A new run needs new snapshot/build/output paths (the runner
requires a fresh output directory). Full argv, cwd, environment, exit code,
stdout and stderr are retained in `parameter-final-test/{configure,build,pair-final}`
`.intent.json`, `.json`, `.stdout` and `.stderr` files.

`configure` (exit 0):

```powershell
& 'C:\pixi_ws\.pixi\envs\default\Library\bin\cmake.exe' '-S' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r11paramfinal\s\examples\ros2_parameter_acceptance' '-B' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r11paramfinal\b' '-G' 'Visual Studio 17 2022' '-A' 'x64' '-DCMAKE_CONFIGURATION_TYPES=Release' '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL' '-DPython3_EXECUTABLE=C:\pixi_ws\.pixi\envs\default\python.exe' '-DFastDDSDSoftBusTransportRos2Host_DIR=C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r8w2final\p\lib\cmake\FastDDSDSoftBusTransportRos2Host' '-Dfastrtps_DIR=C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\g2a1\host214-install\share\fastrtps\cmake' '-Dfastcdr_DIR=C:/pixi_ws/ros2-windows/Lib/cmake/fastcdr' '-DCMAKE_PREFIX_PATH=C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r8w2final\p;C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\g2a1\host214-install;C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\ros2-target\h3\runtime;C:/pixi_ws/ros2-windows;C:/pixi_ws/.pixi/envs/default/Library'
```

`build` (exit 0):

```powershell
& 'C:\pixi_ws\.pixi\envs\default\Library\bin\cmake.exe' '--build' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r11paramfinal\b' '--config' 'Release' '--' '/m:1' '/nr:false'
```

`pair-final` (exit 0):

```powershell
& 'C:\pixi_ws\.pixi\envs\default\python.exe' '-B' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r11paramfinal\s\tests\ros2_host\run_parameter_acceptance.py' '--exe' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r11paramfinal\b\Release\ros2_parameter_acceptance.exe' '--bridge' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r8w2final\p\bin\fastdds_dsoftbus_ros2_host_bridge.dll' '--fastdds-prefix' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\g2a1\host214-install' '--ros-prefix' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\ros2-target\h3\runtime' '--pixi-prefix' 'C:\pixi_ws\.pixi\envs\default' '--output' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r11paramfinal\pair-final'
```

The working directory was `build/r11paramfinal`; source files were frozen
under its `s/` directory. The compiler was Visual Studio 17 2022 x64, Release,
`MultiThreadedDLL`, single-job build with node reuse disabled. Python and CMake
came from `C:/pixi_ws/.pixi/envs/default`. Development headers/packages came from
`C:/pixi_ws/ros2-windows`; runtime ROS DLLs came from
`build/ros2-target/h3/runtime`; compatible Fast DDS came from
`build/g2a1/host214-install`. The recorded PATH orders Fast DDS, immutable bridge,
ROS runtime, ROS development, Pixi and Windows directories. `TEMP`/`TMP` use
`build/r11paramfinal/tmp`; `PYTHONPATH` and `AMENT_PREFIX_PATH` select the ROS
development prefix; `PYTHONUTF8` and `PSModulePath` are unset;
`PYTHONDONTWRITEBYTECODE=1`, `CMAKE_BUILD_PARALLEL_LEVEL=1` and
`MSBUILDDISABLENODEREUSE=1` are set.

The runner creates run-specific A/B configs and log directories, requires
`rmw_fastrtps_cpp`, domain 201, V1 locator and
`HOST_OPAQUE_BYTES_TEST_ONLY`, and enables the already-qualified Host IO capture.
It validates `builtin=0`, `intraprocess=0`, `fake_sdk=1`, runtime closure and pump
join. The actual final localhost pair used scoped approved execution after the
known sandbox connect restriction; no device, firewall, network permission or
security setting was changed.

Independent tester inspection commands (both exit 0) were:

```powershell
C:/pixi_ws/.pixi/envs/default/python.exe -B build/r11-observation/parameter-final-test/inspect_results.py
C:/pixi_ws/.pixi/envs/default/python.exe -B build/r11-observation/parameter-final-test/review_regression.py
```

## Capability boundary and remaining work

| Capability/status | Supported evidence or missing acceptance |
| --- | --- |
| V1-07 parameters: PARTIAL | This exact Windows Host service/read/metadata/mutation/event scenario passed |
| Full V1-07: NOT VERIFIED | Delete/undeclare, richer types/ranges, namespace/multi-node variation and device coverage remain missing |
| Declaration-event replay: NOT VERIFIED | Three declarations observed initially, zero finally; completeness and late-join replay were not acceptance criteria |
| Rollback state: bounded verified assertions | Four rejected batches preserve subsequent reads; final remote list and local absence check exclude a remaining undeclared name; transient internal state is not continuously observed |
| Rejected callback causality: UNRESOLVED | Final A 6 / B 0; immutable R8 bridge lacks precise admission reasons; these counts are retained despite business success |
| Failed startup/connection recovery: NOT VERIFIED here | The positive parameter run does not establish failure-path cleanup |
| Action, broad QoS, reconnect, size/concurrency matrix: NOT VERIFIED by this stage | No corresponding new runs; no promotion from this bounded scenario |
| Native DSoftBus / RK3588A / cross-device: BLOCKED / NOT VERIFIED | No parameter deployment or device run; latest permission observation below |
| Overall product: NOT VERIFIED | Frozen capability denominator remains 30; no completed-row numerator or aggregate percentage is asserted |

No standalone authoritative 30-row capability matrix exists in this publication
worktree. This document records the bounded V1-07 update without importing
unqualified changes from the separate W1 tree. Earlier R10 service/status text
is a historical snapshot: its parameters-NOT-VERIFIED statement predates this
Host-only partial evidence, and its native limitations remain applicable.

The last narrow device permission query was **2026-10-14T21:51:07Z**, DENIED on
both `3e01ff55454d202020104033bf453b00` and
`3e01ff55454d202020104433991c3b00` for `fastdds_dsoftbus_probe` and
`ohos.permission.DISTRIBUTED_DATASYNC`. This stage did not refresh device
presence or permission. Owner-supported deployment/permission recovery followed
by a narrow GRANTED check remains required before native testing. No flashing,
security/network modification or permission bypass was performed.

This stage neither diagnoses historical A/04 failures nor qualifies the concurrent
connection/receive-journal implementation. Publication identity and CI results
must come from Main's later receipts; no new commit, push or CI PASS is inferred
from these local test results.
