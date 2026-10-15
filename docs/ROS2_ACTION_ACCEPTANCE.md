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


## R18 current evidence: Action acceptance WITH APPLICATION READINESS PRECONDITION

Snapshot: 2026-10-15 after independent post-review final testing; candidate base
`1fc72b77452daa80010d0edd578b315c264ba044`. All preceding sections are the preserved
R12 historical record. This section does not reclassify that record or the R18
original no-barrier failure. Overall R18 normal commit gate remains **BLOCKED**;
documentation synchronization does not grant it.

### Implementation and boundary

The four-file candidate comprises the Action example CMake/main and the Action
runner analyzer/model tests. CMake selects the existing Host package on Windows
or Target package on OHOS; OHOS uses the existing business-main startup bridge.
The new application-level `example_interfaces/srv/Trigger` handshake binds the
run ID and accepted UUIDs to PAIR (orders 6/40) and TAIL (order 5). The client has
accepted handles before requesting readiness; the server requires the expected
executing goals and no prior feedback, grants each phase once, and then permits
its timer to emit feedback. Negative readiness latches scenario failure; pending
client service requests are removed on the exception path. Existing bounded
waits and Action feedback/cancel/result/accounting requirements remain enforced.
The analyzer permits feedback while the client awaits the readiness reply; it
does not invent a cross-process timestamp order or require reply-before-feedback.
A successful Trigger reply cannot substitute for Action feedback.

This is an **acceptance precondition**, not a production Action, RMW, DDS/RTPS or
Transport repair. Ordinary ROS applications may legally publish feedback before
the receiver registers its goal handle; the observed typed-client scheduling
risk remains. The compared registration/discard/readiness source blocks in Host
rclcpp 28.1.16 and the bound native 28.1.22 inputs correspond, but this is neither
complete binary equivalence nor native runtime validation. See the sealed
`readiness-design-review/REPORT.md` and `mechanism-review/REPORT.md` under
`build/r18-action-native/` for these narrow source/behavior boundaries.

### Preserved failure, mechanism, initial and final stages

Evidence paths below are relative to the original repository root, not the W3
checkout. All earlier seals remain unchanged.

| Stage | Actual execution and conclusion |
| --- | --- |
| Original native consumer preparation | Original configure FAIL (39-file seal) is retained; the separate corrected binding plan enabled build/static checks, not native execution. |
| Original no-barrier Action | `consumer-initial-fix1-test/INITIAL_TEST_RESULT.json`: run `eb7af94cc32edb89`, A 63240/B 56964 both exit 4, `bounded_wait_expired`, no BUSINESS_DONE; runner/wrapper exit 1. One business window, no kill/timeout, exclusive rebind succeeds. FAIL remains authoritative; cause **NOT_PROVEN**. The extra Python-cache guard failure and subsequent attributed cleanup are separately recorded, not erased. |
| Controlled mechanism | `mechanism-test/INITIAL_TEST_RESULT.json`: `22cabb3705443452`, A 72896/B 67076 exit 0; two publications/takes, three feedback dispatches including one explicit diagnostic redispatch. The same retained opaque object gave no callback before registration and one afterward; register-first control gave one callback. **PASS_MECHANISM_ONLY**, not proof of the original failure cause. One A-side adapter rejection and EOF/WSA10004 remain recorded. |
| Readiness plan setup | `readiness-initial-test/` BLOCKED before execution because a proposed map output addressed an old sealed directory. FIX1 changes output paths only; the original map is preserved. The initial tester's separate ament-path classifier error is retained in `INVALID_PREDECLARATION_PATH_CHECK.json`; exact path classification corrected the collector, without a build/window retry. |
| Independent readiness initial FIX1 | `readiness-initial-fix1-test/INITIAL_TEST_RESULT.json`: fresh native and Host configure/build each once, 70 model methods plus 17 synthetic controls, one run `569c03a07f4302fe`, A 56616/B 53764 exit 0, 5 requests/4 accepted/1 rejected/2 succeeded/1 canceled/1 aborted. PASS_WITH_APPLICATION_READINESS_PRECONDITION; one A-side adapter rejection remains. Native BUILDS/static only. |
| Formal code review | `code-review/REVIEW.json`: NO_BLOCKING_FINDINGS_IN_SCOPED_CANDIDATE; no source changes required. Review executed 31 Action models, 26 offline cases and the original-oracle projection; these are distinct checks, not extra live windows. |
| Canonical independent final | `readiness-final-test/FINAL_TEST_RESULT.json`: **PASS_WITH_APPLICATION_READINESS_PRECONDITION**, verified artifact reuse, zero new configure/build, fresh 70 models and 17 synthetic controls, exactly one final window `fe87b82ecd794c7e`. It is a new readiness scenario, not a retry/reclassification of the old no-barrier window. |

Final model count is 31 Action + 31 receive + 7 I/O + 1 control = **70 methods**;
**17 synthetic mutation concepts** are counted separately. They use a new
readiness-positive fixture. The old ROS-legal early-feedback ordering violates
this added precondition; rejecting it does not make that ROS behavior illegal.
No broad core, lifecycle, serialized or native regression is inferred from these
models.

### Canonical final live observations

A PID **54800** and B PID **79508** both exit **0**. Five requests yield four
accepted goals and one rejected goal; terminals are two SUCCEEDED, one CANCELED
and one ABORTED, including targeted cancellation while the normal goal remains
active and successful follow-up work. PAIR and TAIL each have one request, grant
and reply. Feedback passes the observed-subsequence oracle with the mandatory
cancel prefix. Status-topic publication is **not validated**.

Twelve selected runtime modules per side were checked by the canonical driver;
independent live mapping checks cover thirteen including the executable. These
are **cumulative observations, not a pre-START module barrier**. Both ports
**55602/55603** permit simultaneous exclusive rebind; no forced kill or timeout
occurred. The actual final result is `build/r18ready-final1/pair-once/result.json`
(SHA256 `0f067d052cc65f4cde6d7c9acb4afec6552a4f1976e3ae259aec7198c117551a`).

| Final process | Sends | Ingress/admitted/charged/refunded | Adapter rejections | Retained I/O termination observations |
| --- | ---: | --- | ---: | --- |
| A 54800 | 132 | 121 / 121 / 121 / 121 | 0 | Two EOF at receive-prefix boundary |
| B 79508 | 121 | 132 / 132 / 132 / 132 | 0 | Two socket errors WSA10004 at receive-prefix boundary |

Strict `full_host` capture and counter reconciliation pass. No send errors or
matched stderr error lines were reported in this final window; the above I/O
terminations remain present. Zero adapter rejections in this window is not a
zero-error or whole-frame delivery claim. Payload-to-RTPS/business causality is
**NOT_OBSERVED**; no attribution of all frames or explanation of the original
failure follows from the counters.

### Reproducible commands, artifacts and seal anchors

Host environment: Windows, MSVC x64 Release `/MD`, C++17; bound Fast DDS 2.14.5
SECURITY OFF / STATISTICS OFF, existing R11 Host bridge and h3 ROS runtime.
`FakeSocketTcp` carries opaque bytes between real ROS/DDS processes on the Host;
it is not native OpenHarmony DSoftBus. Native build uses SDK23, Ninja,
`arm64-v8a`, Release, `c++_shared`, the selected Target install and exact package
bindings. Cross-compilation and ELF checks do not execute those binaries.

Full exact `argv`, working directory, captured environment and exit codes are
preserved in `readiness-initial-fix1-test/{host,native}-{configure,build}.json`
and `readiness-final-test/{model-action,model-receive,model-io,model-control,mutants,pair}.json`.
The documentation seal's `docs-sync/COMMAND_RECEIPTS.json` copies those fields
without shortening native CMake arguments. All ten commands exit 0. Final model
commands are `C:/pixi_ws/.pixi/envs/default/python.exe -B -m unittest discover -s
<root>/build/r18ready-final1/s/tests/ros2_host -p <pattern> -v`, with patterns
`test_action_acceptance.py`, `test_host_receive_capture.py`, `host_io_capture_tests.py`
and `test_control_files.py`. Synthetic controls execute the sealed `mutants.py`.
The actual final pair invocation (paths rooted at the original checkout) is:

```text
C:/pixi_ws/.pixi/envs/default/python.exe -B <root>/build/r18ready-final1/s/tests/ros2_host/run_action_acceptance.py --exe <root>/build/r18ready1/host/Release/ros2_action_acceptance.exe --bridge <root>/build/r11diagfinalfix/p/bin/fastdds_dsoftbus_ros2_host_bridge.dll --fastdds-prefix <root>/build/g2a1/host214-install --ros-prefix <root>/build/ros2-target/h3/runtime --pixi-prefix C:/pixi_ws/.pixi/envs/default --output <root>/build/r18ready-final1/pair-once
```

The pair working directory is `<root>/build/r18ready-final1`; PATH begins with
the bound Fast DDS, Host bridge and h3 runtime bins, then installed ROS/Pixi bins.
`PYTHONDONTWRITEBYTECODE=1`, `CMAKE_BUILD_PARALLEL_LEVEL=1` and
`MSBUILDDISABLENODEREUSE=1` are captured. These are executed receipts, not a claim
that the abbreviated command alone recreates all external dependencies.

`ARTIFACT_REUSE.json` binds the initial build command receipts, selected source,
compile/link dependencies and runtime modules. Native executable
`build/r18ready1/native/ros2_action_acceptance` SHA256 is
`fe938244ae64fae4201c04c9e1cd947b6eacf37ea4b2f2e8ac0d72e44a46c0a5`.
Its ELF/interpreter/NEEDED/startup-business connection and actual compiler inputs
were inspected; the historical identity bridge is audit-traced, not freshly
runtime-qualified. Final execution reuses the following identical A/B Host
bindings; full absolute paths and sizes are in `INDEPENDENT_RUNTIME_CHECK.json`:

| Module | SHA256 |
| --- | --- |
| `ros2_action_acceptance.exe` | `4168bdf3ffba2917b45cf820ec9c38614c02a55cb781dc2974766aa9584ca7d5` |
| `fastdds_dsoftbus_ros2_host_bridge.dll` | `cef2b894dd397b46f0edb819f8bedc93df6cea326d8645a0006fd57e1440c572` |
| `fastrtps-2.14.dll` | `60408a27139ce59bf9aaa92c080e799d98f1531b7318dfbf0abf7308e7d555a4` |
| `fastcdr-2.2.dll` | `badcea3777c7a6b23961310dc5857e07c064d94af9babe52d2c097d8842d8c71` |
| `rclcpp.dll` | `d06e9dbdc7c152d76716c582f8d0eb1284e7279ca21e7d9e225d87ef9f958673` |
| `rclcpp_action.dll` | `9a0f108f07a18141b546a62ca9d7122ceadf8f90684c77ec03d966ea1babdb17` |
| `rcl_action.dll` | `c496ec9288b8480b2918c21deee96314f56c86133177df640665b0a704df29ef` |
| `rmw_fastrtps_cpp.dll` | `9f500042d4dbcf697fcf22c9eae9b07130839e301b5a417c701e219115395a6e` |
| `rmw_fastrtps_shared_cpp.dll` | `19715630ddeb38c17c9aca78b3df08ad8475f28ffce27bdf78580daa51430c49` |
| `example_interfaces__rosidl_typesupport_cpp.dll` | `3b9ab51c35b55fefccd55601ffb9fc827a066c7144ef6428b41e84b5712110c8` |
| `example_interfaces__rosidl_typesupport_fastrtps_cpp.dll` | `bdda5f4fec12c0dae6482d9b254c5afe24d5e7eb3b25635a42e2972447ff9f7c` |
| `action_msgs__rosidl_typesupport_cpp.dll` | `b9cc266d1ea070735cfb8fe9748930a6c28ba9d4f96ad4f0df44f87ac2da902c` |
| `action_msgs__rosidl_typesupport_fastrtps_cpp.dll` | `e4d7129b550853d6c76a686bef6fc4a1d91e46a135d29c128481e23c3ab5267a` |

| Evidence under `build/r18-action-native/` | SHA256 |
| --- | --- |
| `readiness-implementation/SOURCE_FREEZE.json` (3 readiness files + separately bound preexisting CMake) | `74c70071da566238427da2e5dab61e2bcfbd028553b3181418951232809ee1ed` |
| `readiness-initial-fix1-test/INITIAL_TEST_RESULT.json` | `917a0cfca80120ce70f8a4ad38581f4e1cd79bdfbceac12eb86671a1ecff923a` |
| `readiness-initial-fix1-test/EVIDENCE_MANIFEST.json` (237 files) | `23c88c4e87d21fe1849432f817469f344439b104586b08e9ecd6e8c55d657b45` |
| `code-review/REVIEW.json` | `9e5d7a627d5a1d65d41124028db33d1f02ce43493e944a76d0d31e3f470b4808` |
| `code-review/EVIDENCE_MANIFEST.json` (2314 inputs, 14 outputs) | `30fe779abdddd3358a1c2d45b37a44fd76bb3cb5fe5eade8ce7cc037cebaf0e1` |
| `readiness-final-test/FINAL_TEST_RESULT.json` (canonical execution verdict) | `f3801cea755215356ac97d9120c49e96e12ee29b017903633e07debafc9ad9a8` |
| `readiness-final-test/EVIDENCE_MANIFEST.json` (98 files) | `5a5c30d905249c5ea5a61335eafcce6d13b3fe3c5ec0344601af721959b3a24a` |
| Original no-barrier `consumer-initial-fix1-test/EVIDENCE_MANIFEST.json` (212 files) | `c7bc5850fd8ad1dd4a346eb1a5ff4d899d23ba91177f3f6bf4a266231dba445a` |
| `mechanism-test/EVIDENCE_MANIFEST.json` (116 files) | `9aecac788716bb4cd5660d8707407c03cd8aed921c83cde5f45d0d9c4a8e9e6e` |

### Current capability and gate matrix

| Capability / gate | Status and reason |
| --- | --- |
| Specified Action acceptance WITH APPLICATION READINESS PRECONDITION | **PARTIAL / HOST_TESTED PASS**: real ROS/DDS five-goal scenario, two readiness phases and cleanup, on the opaque-byte Host fake Socket seam only. |
| Ordinary unbarriered Action feedback scheduling | **NOT VERIFIED; original run FAIL**. Legal early-feedback risk remains, original cause NOT_PROVEN. No production fix or universal delivery promise. |
| Controlled typed-client dispatch mechanism | **HOST_TESTED PASS_MECHANISM_ONLY**; explicit diagnostic redispatch is not an application delivery guarantee. |
| Target dependency/bridge/Action executable | **BUILDS / static PASS**, actual loader/native communication **NOT RUN**. Source correspondence does not prove native 28.1.22 behavior. |
| Status topic, arbitrary Action/QoS, large/fragmented goals, reconnect, secure Action | **UNVERIFIED** in this phase; no test or full feature claim. |
| RK3588A and cross-device R18 Action | **BLOCKED; DEVICE_TESTED=false**. Complete project-only permission-record snapshot and lossless restoration route remain prerequisites. |
| Historical V1 cross-board pub/sub/service | Separate real-device evidence exists for its historical bound artifacts; it does not qualify R18 Action. |
| R14 visibility | Prior strict 20 writes/19 takes and ten opaque frames per side remain **FAIL**; R18 does not alter that conclusion. |
| R19 rosbag preparation | Only three pinned standalone vendor libraries have **BUILDS/static PASS**. Independent vendor review has no blocking findings for that scope; R19-V1 remains **OPEN**. Wrappers/imported targets, plugin, consumer, record/replay/runtime and device are **UNVERIFIED**. |
| Documentation sync | Current evidence recorded; its own consistency gate is separate from normal implementation commit eligibility. |
| R18 normal commit gate / Main graph PRE_COMMIT / commit / push / CI | **BLOCKED / not granted** for normal commit; Main checks and publication receipts remain pending. No commit/push/CI success is claimed. |
| Overall ROS 2 over native DSoftBus | **NOT VERIFIED**; no aggregate completion percentage. |

The user already conditionally authorized the existing `fastdds_dsoftbus_probe`
identity's temporary `ohos.permission.DISTRIBUTED_DATASYNC` window on two boards,
APL_NORMAL, followed by restoration. Execution remains blocked because complete
original project-only attributes and a supported lossless restore operation are
unestablished. No grant/revoke/token switch/reload or security configuration
write was performed. `PERMISSION_AUTHORIZATION_STATE.md` and
`PERMISSION_RECOVERY_REQUIRED_INPUT.md` distinguish authorization from that
remaining prerequisite; broad native-token-registry access was previously
rejected and is not an alternative. Main's R18 read-only board/platform records
observed both boards and matching selected SoftBus DSOs, not Action execution or
current permission qualification. Historical DENIED evidence and V1 permissions
activity are not a complete current project record.

R19 library evidence is separately sealed at
`build/r19-rosbag-preparation/vendor-library-fix1-test/RESULT.json` SHA256
`6c057a50eb3aab5ba818b4313887242018e9a23c8a143964115a9c9981b9b9ca`;
`vendor-library-review/RESULT.json` SHA256
`9cce6bd138da41ed77ad8b64df6a57004d268ffc307953740288449163cc00f7`.
It supplies build/static inputs for future downstream work, not rosbag runtime
acceptance. Earlier vendor/collector failures remain in their separate seals.

R19 post-review final static recheck: `vendor-library-final-test/RESULT.json`
SHA256 `96820ee89aa63109fa010dd41a8375f8b230147cdaf940fd3eaacf86b3453f7f`,
92-file manifest `e540ebd0316c2f68f71befdc0111a9b7e0733f8ab1adf08e65399e067a18f0d2`.
PASS_STATIC_ONLY; three fresh readelf commands exit 0, zero new configure/build/
install. The open downstream/runtime/device limits above are unchanged.


## R18 bounded source/build/Host stage: prospective publication qualification

This later scope receipt supplements the preceding broader R18 assessment;
it does not overwrite that assessment or its BLOCKED gate. Main's
`build/r18-action-native/PUBLICATION_SCOPE_DECISION.json` defines
**R18_SOURCE_BUILD_HOST_STAGE**: OHOS Action acceptance target build enablement
plus Windows Action acceptance WITH APPLICATION READINESS PRECONDITION. The
candidate remains the same four source/test paths and these two documentation
paths; no production Transport, RMW or identity implementation was changed.

Independent technical scope review found the six-file boundary coherent and no
new blocking defect; a Host-only split was not required. The OHOS CMake wiring
makes existing native startup/identity paths reachable from this example, and
the readiness handshake changes application scheduling on either platform if
executed. Therefore this stage does **not** claim absence of native runtime
effects. It claims the exact bound target builds and the exact Windows scenario
passes; native execution remains a separate required qualification.

The independent scope tester sealed **Commit gate:
PASS_FOR_EXACT_SOURCE_BUILD_HOST_STAGE** at 2026-10-15T18:12:27Z. It rechecked
31,000 unique current paths with zero drift and accepted the unchanged
source/build/artifact/input bindings. It reused the recorded post-review
70 model methods, 17 synthetic controls and final Host run
`fe87b82ecd794c7e`; this scope assessment performed **zero new model executions,
zero builds and zero business windows**. Existing actual build/run commands,
module hashes and counts in the preceding section remain the execution evidence.
A new scope judgment is not a second build or a repeated live success.

| Exact scoped claim / obligation | Current status |
| --- | --- |
| Selected SDK23 OHOS Action consumer configuration, compile/link and static ELF/startup-symbol checks | **BUILDS / static PASS**, bound artifact reuse; target was not executed. |
| Windows five-goal Action and two Trigger readiness phases | **HOST_TESTED PASS_WITH_APPLICATION_READINESS_PRECONDITION**, real ROS/DDS over the opaque-byte fake Socket seam. |
| Scoped independent tester release evidence | **PASS_FOR_EXACT_SOURCE_BUILD_HOST_STAGE**; applies only to the two claims above and the unchanged four source/test files. |
| Scoped documentation readiness | **Ready for commit: YES for R18_SOURCE_BUILD_HOST_STAGE only**, subject to Main's remaining graph, exact staged identity and applicable PRE_COMMIT decision. This is not commit authorization. |
| Native loader/symbol resolution and missing-identity rejection | **RequiredForNativeQualification — NOT_RUN/BLOCKED**. |
| Native identity selection, startup and restoration | **RequiredForNativeQualification — NOT_RUN/BLOCKED**. |
| Actual DSoftBus Socket operation and native Action acceptance | **RequiredForNativeQualification — NOT_RUN/BLOCKED**. |
| Cross-board discovery, Action/readiness and lifecycle | **RequiredForNativeQualification — NOT_RUN/BLOCKED**. |
| Original broader R18 gate / native product qualification | **BLOCKED / NOT VERIFIED**, unchanged. DEVICE_TESTED=false. |
| Main graph/PRE_COMMIT, commit/push and CI | Pending separate Main receipts at this documentation snapshot; no success preclaimed. |

Native obligations are **not N/A** and have not been waived because execution
was unavailable. A build claim can be supported while runtime qualification
remains required and unfulfilled. The original no-barrier run remains FAIL,
cause NOT_PROVEN; the controlled mechanism experiment remains
PASS_MECHANISM_ONLY. This publication scope does not repair production feedback
scheduling or establish ordinary unbarriered Action support. It does not expand
QoS, status-topic, fragmentation/security coverage, or change the R14 failures.

The separate loader-readiness diagnostic stopped at its only HDC discovery
at 17:55 UTC: exit 0, empty stdout/stderr, no observed target entries; zero remote
commands, uploads or native executions. Result: BLOCKED_DEVICE_DISCOVERY_EMPTY.
That observation does not establish physical disconnection or a successful
loader test. The project-only original token-record and lossless-restoration
prerequisite remains; no permission grant/revoke, token switch or security
configuration change is inferred or authorized by this publication stage.

All receipts below are under `build/r18-action-native/` at the original checkout.
They supplement rather than rewrite `MAIN_GATE_R18_READINESS_20261015.json` and
`docs-sync/RESULT.json`. Those original broader judgments retain their recorded
NO/BLOCKED values; new documentation evidence is separate in
`publication-scope-docs/`.

| Scope evidence | SHA256 |
| --- | --- |
| `PUBLICATION_SCOPE_DECISION.json` | `ae37c1d674df06a1651063d6c7e6b3ede6493f94c52041aa8322466b90024f78` |
| `publication-scope-review/RESULT.json` | `212e8e7cdafa17b883bef0f6787f2ec6492ce71ca8e2761cc090b0896869b4f4` |
| `publication-scope-audit/g2-n1-supplement/RESULT.json` | `15aec3364f5980c0e626c278947d27e39212295c259c275fc3662d1e8ef17c24` |
| `publication-scope-test-gate/RESULT.json` | `4ffa2d84f7a45006ee18df33a1cfee0be1915ab13f30049b4b36003ae4011f7a` |
| `publication-scope-test-gate/EVIDENCE_MANIFEST.json` (20 entries) | `fca510e0f2dfe57c795f42a2157f7cc6ac9190b781cd09b1ac9451afbe3547b7` |
| `loader-readiness-test/RESULT.json` | `b10cab0958638d5bccabb00ac9d317c377027c44a6c7d08b12c4bd628f61af64` |
| `loader-readiness-test/EVIDENCE_MANIFEST.json` (15 entries) | `8e592bc80946bb43c0b28eed190e9eeb46157bb84350178111f87b9ababc26f5` |


## 2026-10-15 update: publication and bounded native loader evidence

This dated update supersedes only the pending publication, loader diagnostic
and R19 wrapper statuses in the preceding historical snapshots. Their original
FAIL/BLOCKED receipts remain unchanged. Overall native ROS 2 qualification is
still **NOT VERIFIED**.

R18_SOURCE_BUILD_HOST_STAGE was committed and pushed as
`48c01f2597bf16db1ed35fbd79cc5c32826f0ab8` to the existing origin branch
`codex/flow-encoding-published-20261014`. Main's PRE_COMMIT passed 45 checks;
the six-file boundary was verified after publication. Graph risk remained HIGH.
At 18:27:04 UTC GitHub reported zero workflow runs, check runs and commit-status
contexts for this SHA: **CI NOT_RUN at that observation**, not PASS. The workflow
inventory endpoint was rejected by the connector; zero workflows is not claimed.

The subsequent reviewed loader diagnostic executed the bound native Action
executable once on each RK3588A board using SDK23/AArch64 inputs:

| Board | HDC target | Native exit / HDC exit | Result |
| --- | --- | --- | --- |
| A | `3e01ff55454d202020104033bf453b00` | 3 / 0 | PASS_NATIVE_MAIN_LOAD_AND_MISSING_IDENTITY_REJECTION_ONLY |
| B | `3e01ff55454d202020104433991c3b00` | 3 / 0 | PASS_NATIVE_MAIN_LOAD_AND_MISSING_IDENTITY_REJECTION_ONLY |

Executable SHA256:
`fe938244ae64fae4201c04c9e1cd947b6eacf37ea4b2f2e8ac0d72e44a46c0a5`.
Each run used an empty inherited environment, an explicit nonempty required-config
path to an owned absent file, and absent token/admission inputs. stdout was empty;
stderr was exactly `ROS_IDENTITY_REJECTED missing_or_invalid_external_input`
followed by newline. The current `IdentityStartup.cpp` rejects these inputs before
constructing Operations/TokenFirstEntry or entering Action business code. The
required-config file was not read or written on this path.

Both boards passed 104 artifact hashes before and after execution. Following
quiescence, 106 exact owned names were removed and empty directories removed;
separate absence checks passed. There were no timeouts, forced kills, automatic
retries or residual remote resources. The 103-member main dependency closure
excludes the separately deployed 104th target bridge: its loading is **not
established**. Uncalled/lazy symbols and every initializer path are not qualified;
dependency constructors are not asserted to be side-effect-free.

The original 17:55 empty-discovery result remains BLOCKED. The same fixed HDC
binary later observed both boards in the normal host environment; this supports
an environment visibility explanation without proving the unique historical
cause. A preflight sentinel substring parser mistakenly matched its own shell
argv. Its original STOP and an unexecuted draft authored before graph impact are
preserved, including that workflow deviation. The final full-line parser and
five offline controls were reviewed; neither native command nor its byte-exact
oracle was relaxed. Independent evidence review found no blocking finding and
required no additional device execution for this limited claim.

| Current capability | Supported extent and remaining obligation |
| --- | --- |
| Native main load and missing-identity rejection | **SUPPORTED for this diagnostic / DEVICE_TESTED PASS** on both boards. |
| R18 five-goal Action with application readiness | **PARTIAL / HOST_TESTED PASS** on the Windows opaque-byte fake Socket seam; native Action remains unverified. |
| Ordinary unbarriered Action feedback | **UNVERIFIED**, original run FAIL and cause NOT_PROVEN; no production race fix. |
| Accepted identity selection/startup/restoration | **UNVERIFIED / BLOCKED** by complete project-only original record and supported lossless restoration prerequisites. |
| Target bridge execution, actual Socket traffic, cross-board Action/discovery/reconnect/lifecycle | **UNVERIFIED / NOT_RUN/BLOCKED** for current R18 artifacts; native Action DEVICE_TESTED=false. |
| Status-topic behavior, arbitrary Action/QoS, large/fragmented goals and security | **UNVERIFIED** in this acceptance stage. |
| Session API fallback | **NOT SUPPORTED by project policy**; Socket API remains mandatory. |
| R19 vendor wrappers | Scoped configuration contract now passes; see [R19 preparation](ROS2_ROSBAG_TARGET_PREPARATION.md). This is not rosbag runtime support. |

No project permission grant/revoke, token switch, reload or security write was
performed. Conditional permission authorization does not supply the missing
complete project-only record and lossless restore route. Current DATASYNC state
was **NOT_QUERIED**; historical DENIED is not a current observation. The rejected
broad token-registry access route is not an alternative. The original unbarriered
FAIL, mechanism-only result and R14 failures retain their previous meaning.

The following evidence is local to the original checkout's ignored
`build/r18-action-native/` directory, not bundled in the remote Git commit.
Command/environment/stdout/stderr/exit receipts are in these sealed directories.

| Receipt | SHA256 |
| --- | --- |
| `loader-readiness-test-recovered/RESULT.json` | `9217623d5cd834566d0d861040cd37a29a43bf055ccce169c6a343ad2201dba0` |
| `loader-readiness-test-recovered/EVIDENCE_MANIFEST.json` (1082 files) | `75a685d22b7525b62ba32f5c87b6bfa63739669a79f1d531d1dcddaac5191814` |
| `loader-readiness-recovered-review/RESULT.json` | `68a4d58dc3ae1ced7c8c652b8074af9f1341702350ef3ba95c90682ed5e854bf` |

Publication receipts are `main-publication/PUBLICATION_RESULT.json`,
`PRE_COMMIT.json` and `GITHUB_PUBLICATION_CHECK.json`. They record the earlier
publication snapshot; later loader evidence does not rewrite its historical
DEVICE_TESTED=false statement into an Action communication PASS.
