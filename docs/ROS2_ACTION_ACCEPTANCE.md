# ROS 2 Windows Host Action acceptance

As of 2026-10-15 05:34:11 UTC, independent final testing supports **V1-06
PARTIAL: Windows Host Action acceptance**. A fresh build and one actual pair
of ordinary ROS processes passed the bounded scenario below after independent
agentic review and its acceptance-oracle correction. This is actual
`rclcpp_action` / `rmw_fastrtps_cpp` traffic over the Host opaque-bytes seam,
with builtin transports and intra-process delivery disabled. `fake_sdk=1`
means this result does not verify native DSoftBus or RK3588A communication.

Four additive files provide the standalone Windows-only C++17 example,
runner and parser tests: `examples/ros2_action_acceptance/{CMakeLists.txt,main.cpp}`
and `tests/ros2_host/{run_action_acceptance.py,test_action_acceptance.py}`.
They change no production Transport, DDS/RTPS or QoS behavior. The immutable
qualified R11 bridge is reused without rebuilding it. Tester `Commit gate:
PASS` is scoped to those four files; Main's graph/pre-commit/publication and
CI gates remain separate. Overall product status remains **NOT VERIFIED**.

## Scenario and assertions

A hosts `/action_<run>/fibonacci`; B uses the ordinary ROS Action client and
`example_interfaces::action::Fibonacci`. Files only coordinate process
lifecycle and teardown; goals, feedback, cancellation and results use ROS.

| Order | Response and exact terminal result |
| --- | --- |
| 6 | Accepted, SUCCEEDED, `[0,1,1,2,3,5]` |
| 40 | Accepted concurrently with order 6, targeted cancel, CANCELED, `[0,1]` |
| -1 | Rejected, null client goal handle, no terminal result |
| 0 | Accepted, ABORTED, empty sequence |
| 5 | Accepted after rejection/abort, SUCCEEDED, `[0,1,1,2,3]` |

The exact totals are five requests, four accepted, one rejected, two succeeded,
one canceled, one aborted and one targeted cancellation. Each accepted goal
has a distinct nonzero 16-byte UUID joined across both processes, feedback,
cancel response and wrapped result. A observes the rejected request's UUID,
but B's installed API exposes only a null handle; the rejected response is
correlated by its unique request/order, without inventing a client UUID.

The server's own event sequence proves two executing handles and order 6 still
active during cancellation of order 40. No cross-process clock comparison is
used. B must observe order 40 feedback `[0,1]` before requesting cancellation.
The response must contain `return_code == ERROR_NONE` and exactly the target
UUID in `goals_canceling`. A must observe `is_canceling()` before calling
`canceled()`, and B must receive the CANCELED wrapped result. Cancellation
acceptance alone is insufficient. Order 6 then completes normally.

Feedback checks accept legal observed increasing Fibonacci-prefix subsequences;
they do not require every normal feedback sample. Every B feedback record must
follow its own SENT, but may precede the application's RESPONSE record because
callbacks can execute in the same `spin_some()`. The mandatory cancel-target
feedback is separately required. Terminal results and UUIDs are checked exactly;
Action status-topic delivery is not observed by a dedicated subscription.

Single-threaded executors retain goal handles and result futures. A timer owns
server progress; no detached worker is used. The cancel target is intentionally
held, so this is controlled concurrent cancellation, not a natural cancel/result
race. All waits are bounded (50-second application bound, 65-second runner
supervision); both sides must report four terminal goals and zero pending work.
B drains for 250 ms and stops callbacks before teardown. This finite drain does
not prove absence of arbitrarily late feedback. Runtime close, pump join,
process exit and exclusive port rebind are checked.

## Independent evidence

All `build/...` references below are retained local evidence relative to the
original project root `C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`,
not the nested publication worktree and not checked-in artifacts.

| Check | Corrected initial | Post-review final |
| --- | --- | --- |
| Fresh VS2022 x64 Release `/MD` configure/build exits | 0 / 0 | 0 / 0 |
| One actual ROS pair | `453b1b29e46b26ea` | `c6a401090f2ecab8` |
| A / B process IDs | 7412 / 63388 | 65020 / 76848 |
| A / B / driver exits | 0 / 0 / 0 | 0 / 0 / 0 |
| Requests / accepted / rejected | 5 / 4 / 1 | 5 / 4 / 1 |
| Succeeded / canceled / aborted | 2 / 1 / 1 | 2 / 1 / 1 |
| Original callback ingress A / B | 100 / 112 | 102 / 112 |
| Charged and refunded A / B | 98 / 112 | 102 / 112 |
| Adapter-rejected callbacks A / B | 2 / 0 | 0 / 0 |
| Capacity refusals / send errors | 0 / 0 | 0 / 0 |
| Strict `full_host` reconciliation | PASS both | PASS both |
| Synthetic Action / receive / IO / control cases | 18 / 31 / 7 / 1 | 22 / 31 / 7 / 1 |
| Mutants of that actual run, all rejected | 15 | 17 |
| Exclusive port rebind | 57671 / 57672, PASS | 50922 / 50923, PASS |

Final execution ran once as predeclared, 2026-10-15 05:30:12.641455 through
05:30:14.813017 UTC, without timeout. The 61 model/helper cases are synthetic
validation, not 61 real ROS or native runs. The final 17 retained-log mutations
reject wrong UUID/result/cancel identity or cardinality/code, invalid feedback,
missing/duplicate terminal records, accepting the rejected goal, missing
cancel feedback/concurrency, inactive normal goal, pending teardown and feedback
before SENT. Positive logs pass before and after mutations. The separate replay
of 15 sealed initial mutants and two sealed reviewer mutants is not another
17 final functional tests or an additional actual ROS run.

Strict `full_host` captures join independently recorded original callback
ordinal/socket/epoch/handle/size, validated connection endpoints, post-close
adapter counters and immutable scope/record ownership. Final A has 1,428 receive
events, 204 decisions and 714 stages; B has 1,568 events, 224 decisions and 784
stages. All admitted charges are refunded. These scalar joins establish Host
accounting coverage, not payload causality or native SDK behavior.

The runner binds twelve actually loaded modules per process, including Action
libraries and type support. An independent live `memory_maps` watcher additionally
binds `rmw_fastrtps_shared_cpp` to its exact loaded path and stable before/after
file hash; it is not inferred from PATH. Final checks match all 29,471 dependency
files, twelve source/helper files and seven selected tool files before and after.
This does not freeze the entire Windows/MSVC installation or establish
source-to-binary reproducibility.

Canonical records are:

- `build/r12-action/final-test/FINAL_TEST_RESULT.json` and its 133-file
  `EVIDENCE_MANIFEST.json`: final scoped PASS, command results and limitations.
- The same directory's `INDEPENDENT_ACTUAL_CHECK.json`,
  `INDEPENDENT_LIVE_RMW_BINDING.json`, `ACTUAL_BUSINESS_MUTATIONS.json`,
  `SEALED_PRIOR_LOG_REPLAY.json`, `FINAL_DIAGNOSTIC_DISPOSITION.json`,
  `SOURCE_SNAPSHOT.json` and `FINAL_SOURCE_AND_GIT_GUARD.json`.
- `build/r12-action/initial-test-build-fixed/INITIAL_TEST_RESULT.json` and its
  138-file manifest: corrected initial execution, kept separate from final.
- `build/r12-action/review/{REVIEW.json,REVIEW_FIX_CLOSURE.json}` and
  `build/r12-action/implementation/SOURCE_FREEZE_REVIEW_FIX.json`.

## Review and retained diagnostics

Independent Tier 2 review found P2 `R12-A1`: the original oracle could accept
normal/recovery feedback placed before its own SENT. The fix requires that
local causal ordering, retaining legal feedback between SENT and RESPONSE and
the observed-subsequence boundary. Four added models, both sealed counterexamples
and original/early-feedback positives passed focused independent closure. The
final tester then rebuilt the frozen source and executed a fresh actual pair.

The first initial build failed because the installed `ServerGoalHandle` lacked
the assumed `SharedPtr` alias. `std::shared_ptr<ServerHandle>` repaired that
compile error. Its failed command/output remain in
`build/r12-action/initial-test/INITIAL_TEST_FAILURE.json`. Tester preparation
errors, including path/encoding/quoting/import corrections, remain in the
corresponding `PREPARATION_NOTE.json`; they were corrected in ignored evidence
helpers, without changing application deadlines or retrying the final ROS run.

Both final processes retain two startup errors: `No unicast locators to create
unique flows` and `Problem creating associated Reader`. Build-referenced RMW
source has an explicit optional unique-flow fallback; actual module binding,
repeated graph-reader hooks and successful context/business execution support
that interpretation. Exact dynamic QoS arguments, returned reader pointer and
candidate-port failure were not observed. This is a supported inference, not
direct retry instrumentation. Strict/exclusive unique-flow capability remains
**UNVERIFIED**; no QoS behavior was changed to hide these diagnostics.

Initial A callback ordinals 99/100 were rejected before bridge entry at
AdapterPhase Closing/InvalidState. Their phase is observed, but their payload
identity and business meaning remain UNKNOWN. Final zero refusals neither erase
nor explain those initial callbacks. Neither run establishes payload causality.

## Artifacts, commands and environment

Final executable: `build/r12actfinal/b/Release/ros2_action_acceptance.exe`, SHA-256
`a771b50283739892f1c13e924ebba15c7a4ff6e4239bc0bd10799b0e2eba19a5`.
Reused bridge: `build/r11diagfinalfix/p/bin/fastdds_dsoftbus_ros2_host_bridge.dll`,
SHA-256 `cef2b894dd397b46f0edb819f8bedc93df6cea326d8645a0006fd57e1440c572`.
Shared RMW DLL SHA-256:
`19715630ddeb38c17c9aca78b3df08ad8475f28ffce27bdf78580daa51430c49`.

The final profile is Fast DDS **2.14.5, SECURITY OFF, STATISTICS OFF** from
`build/g2a1/host214-install`, ROS runtime `build/ros2-target/h3/runtime`, ROS
development prefix `C:/pixi_ws/ros2-windows`, Python/CMake from
`C:/pixi_ws/.pixi/envs/default`, VS2022 x64 Release `/MD`, single build job and
MSBuild node reuse disabled. The frozen source is `build/r12actfinal/s` and cwd
is `build/r12actfinal`. Domain 201, `rmw_fastrtps_cpp`, V1 locators and
`HOST_OPAQUE_BYTES_TEST_ONLY` are required; `builtin=0`, `intraprocess=0`,
`fake_sdk=1`, Host IO and receive observation are validated.

Exact executed argv, cwd, environment, timeout, exit code, stdout and stderr are
retained in `build/r12-action/final-test/{configure,build,pair-final}`
`.intent.json`, `.json`, `.stdout` and `.stderr`. They record completed commands;
a new execution must use new snapshot/build/output paths. The runner rejects
an existing output directory. In addition to those three exit-0 commands, the
`model-action`, `model-receive`, `model-io`, `model-control` and
`independent-actual-check` receipts preserve the validation commands/results.

The recorded argument vectors rendered as PowerShell are:

`configure` (exit 0):

```powershell
& 'C:\pixi_ws\.pixi\envs\default\Library\bin\cmake.exe' '-S' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r12actfinal\s\examples\ros2_action_acceptance' '-B' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r12actfinal\b' '-G' 'Visual Studio 17 2022' '-A' 'x64' '-DCMAKE_CONFIGURATION_TYPES=Release' '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL' '-DPython3_EXECUTABLE=C:\pixi_ws\.pixi\envs\default\python.exe' '-DFastDDSDSoftBusTransportRos2Host_DIR=C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r11diagfinalfix\p\lib\cmake\FastDDSDSoftBusTransportRos2Host' '-Dfastrtps_DIR=C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\g2a1\host214-install\share\fastrtps\cmake' '-Dfastcdr_DIR=C:/pixi_ws/ros2-windows/Lib/cmake/fastcdr' '-DCMAKE_PREFIX_PATH=C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r11diagfinalfix\p;C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\g2a1\host214-install;C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\ros2-target\h3\runtime;C:/pixi_ws/ros2-windows;C:/pixi_ws/.pixi/envs/default/Library'
```

`build` (exit 0):

```powershell
& 'C:\pixi_ws\.pixi\envs\default\Library\bin\cmake.exe' '--build' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r12actfinal\b' '--config' 'Release' '--' '/m:1' '/nr:false'
```

`pair-final` (exit 0):

```powershell
& 'C:\pixi_ws\.pixi\envs\default\python.exe' '-B' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r12actfinal\s\tests\ros2_host\run_action_acceptance.py' '--exe' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r12actfinal\b\Release\ros2_action_acceptance.exe' '--bridge' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r11diagfinalfix\p\bin\fastdds_dsoftbus_ros2_host_bridge.dll' '--fastdds-prefix' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\g2a1\host214-install' '--ros-prefix' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\ros2-target\h3\runtime' '--pixi-prefix' 'C:\pixi_ws\.pixi\envs\default' '--output' 'C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r12actfinal\pair-final'
```

`PYTHONDONTWRITEBYTECODE=1`, `CMAKE_BUILD_PARALLEL_LEVEL=1` and
`MSBUILDDISABLENODEREUSE=1` are set; `PYTHONUTF8` is unset. `TEMP`/`TMP` use the
scoped `build/r12actfinal/tmp`; ROS Python/ament prefixes and PATH are explicit.
The Windows PowerShell module paths are preserved. Actual localhost execution
used scoped approved execution; no firewall, device security, network permission
or board configuration was changed.

## Capability and remaining gaps

| Capability or gate | Status and boundary |
| --- | --- |
| Implementation / build | PRESENT / PASS for four additive acceptance files |
| V1-06 goal/reject/feedback/result/cancel/concurrent ownership | PARTIAL: exact Windows Host scenario above passes |
| Independent review / post-review final tests | PASS, R12-A1 closed and repaired source retested |
| Action status delivery, replay and result retention/expiry | UNVERIFIED |
| Client/server restart, reconnect, cancel-all, timestamp cancel, natural cancel/result race | UNVERIFIED |
| Broad Action QoS, serialization/type matrix, large messages and load/concurrency matrix | UNVERIFIED beyond this Fibonacci scenario and two overlapping goals |
| Strict unique-flow capability / payload causality | UNVERIFIED / NOT OBSERVED |
| New core/31-case Host regression and native SDK compile | N/A to this additive acceptance-only change; unchanged R11 bridge reused, not rerun or newly claimed |
| Native DSoftBus / RK3588A / cross-device | BLOCKED / NOT VERIFIED overall; no calls or deployment this phase, DEVICE_TESTED=false |
| Original 2.14.7 SECURITY ON historical qualification | NOT VERIFIED by this 2.14.5 OFF run; separate planned profile work |
| Main graph/pre-commit, commit/push and CI | Pending separate Main receipts at documentation sync |
| Overall product | NOT VERIFIED |

There is no authoritative 30-row capability matrix in this publication worktree.
The retained reference is `build/takeover-baseline-20261014/docs/CAPABILITY_MATRIX.json`,
version `v1-draft-20261014`, SHA-256
`533c65cd1d141704a87e8c1b7d1c5c5e13f5aad9f57083375719a592b4ba288d`.
V1-06 is its Action row. This document records only that bounded partial update;
it does not import other stale rows, approve the draft denominator of 30, assign
completed points or assert an aggregate percentage. Earlier service/parameter
pages retain their historical scope; their Action-not-verified statements predate
this limited Host result.

The last narrow permission observation was 2026-10-14T21:51:07Z: DENIED for
`fastdds_dsoftbus_probe` / `ohos.permission.DISTRIBUTED_DATASYNC` on boards
`3e01ff55454d202020104033bf453b00` and `3e01ff55454d202020104433991c3b00`.
Presence/permission was not refreshed in this phase. Owner-supported deployment
and permission recovery plus a narrow GRANTED check remain prerequisites to
native testing. No Host success substitutes for that missing acceptance.
