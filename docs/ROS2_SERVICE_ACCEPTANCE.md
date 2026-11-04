# ROS 2 Windows Host service acceptance

## R11 diagnostic rerun (2026-10-15 04:41:11 UTC)

Run `69350e7e061047a5` passed the existing bounded service scenario with the new
R11 bridge and `--observe-host-receive`: 14 requests/response calls, 12 accepted
responses, two timeout removals and two recreated clients. Both processes exited
0, two connections matched, and A/B full_host captures reconciled 144/143
callbacks and charges/refunds with zero refusals. Ports were released. This
qualifies diagnostics around the existing PARTIAL Host scenario, not a new full
ROS capability or native communication. The independent RMW late-discard and
device limits remain. See [Host receive observation](HOST_RECEIVE_OBSERVATION.md)
for exact source, binary, review and final execution evidence.

The R10 records and historical plan below are preserved as dated evidence. R11
implemented the diagnostic sensors, but A/04 replay remains blocked by the
unqualified original security-enabled 2.14.7 profile and missing fragment
identity; no historical rerun or root-cause resolution is claimed.


The additive `examples/ros2_service_acceptance` executable and
`tests/ros2_host/run_service_acceptance.py` qualify one bounded **V1-05 partial**
service scenario. Two independent processes use ordinary `rclcpp` AddTwoInts
services over the configured Windows Host opaque-bytes adapter. Builtin
transports and intra-process delivery are disabled. This is real ROS execution
with `rmw_fastrtps_cpp`, but `fake_sdk=1`: it is not native DSoftBus or RK3588A
communication. Transport, DDS/RTPS and QoS implementations are unchanged.

## Success conditions

Each direction submits four outstanding requests before collecting responses;
the server responds in reverse order. The analyzer joins request ID, request
writer GUID, client generation and payload/result values across the two logs.
One further request expires after the application's 250 ms wait; removing its
pending entry returns true, then false. The server invokes the delayed response
when it receives the recovery request. The removed future must throw the exact
`future_errc::broken_promise`, and must never be counted as a successful reply.
This observes the removed future, **not independent RMW receipt/discard of the
late reply**; the wait assertion is not a separate timing trace.

Recovery and a recreated client must each receive the correct result. The
recreated client must have a distinct request-writer identity. An absent service
must report unavailable. Together this means 14 server requests, 14 response
calls, 12 verified accepted responses and two timeout removals, not 14 delivered
responses. Both processes and runtime close must succeed, with complete Host IO
captures, actual module bindings, process termination and exclusive port rebind.
Control files carry barriers only, never service responses.

The installed `rclcpp::Service::send_response` returns void and can log a
response-send timeout without throwing. Review finding `R10-SVC-01` identified
this false-positive path. The fixed runner preserves the ROS message format and
rejects the exact `failed to send response to ... (timeout):` diagnostic in either
stdout or stderr; unrelated warnings remain visible. The exact broken-promise
code is also checked. Final independent testing closed this P2 finding.

## Executed evidence (2026-10-15 UTC)

| Check | Initial | Post-review final |
| --- | --- | --- |
| Fresh MSVC x64 Release configure/build | PASS | PASS |
| One positive real ROS pair | `d558ecfe006ff7cc` | `9444fcc88d3f9a94` |
| Requests / response calls / accepted / timed out | 14 / 14 / 12 / 2 | 14 / 14 / 12 / 2 |
| Processes / driver exit | 0 / 0 / 0 | 0 / 0 / 0 |
| Complete IO records A / B | 995 / 1001 | 983 / 985 |
| Rejected callbacks A / B | 0 / 2, retained | 0 / 0 |
| Analyzer negative mutations | 4 PASS | 4 PASS |
| Timeout-warning mutations | Review exposed acceptance gap | stdout and stderr rejected; unrelated warning preserved |
| Failed-init negative | Controlled exit 4 | Controlled exit 4 |
| Process/port cleanup | PASS | PASS |

The four analyzer mutations and warning injections are synthetic checks, not
additional ROS functionality runs. Final evidence binds six direct source files,
29,470 dependency entries and 120 retained initial-evidence files before/after.
The final executable SHA-256 is
`c998fc89166e0fe0a534d02993d1c45a0dfe13fa14675cc237c79d190cb9fee0`.
Both successful pairs used the R8 W2 final bridge DLL, SHA-256
`94a33c8d29801beab43a6f38538cfd23d265e608e091ae04462a5dda6d5107eb`.
This adds real ROS service evidence for that final DLL; it does not rerun the
original R8 pub/sub scenario or historical A/04 failures.

Three failed initial pairs remain in the evidence. The default ROS log path was
denied; a diagnostic attempt confirmed that failure and the failed-init cleanup
access violation. The harness now uses an owned log directory and avoids factory
teardown before successful initialization. The controlled failed-init negative
exits 4 without access violation, retaining the upstream context-cleanup warning.
The third pair encountered sandbox localhost denial (`Bind_return=-606`, WSA
10013), abnormal exit 3221226505 and incomplete runtime close. The identical
frozen binary/runner then passed in the scoped authorized localhost context.
**Failed-connect cleanup recovery remains unqualified**. No network/security
setting was changed. Negative shutdown/receive records and unique-flow warnings
remain retained; successful business output does not mean every syscall passed.
The initial two rejected callbacks have no precise admission reason in W2.

Implementation: PRESENT. Build: PASS. Scoped real ROS Windows Host integration:
PASS. Independent review and affected final testing: PASS. Full V1-05 service,
action, parameters, server restart, broad QoS, native and cross-device acceptance:
NOT VERIFIED. The frozen capability denominator remains 30; this partial
scenario establishes no aggregate completion count or percentage. Device
permission was last checked DENIED on both boards on 2026-10-14 at 21:51:07Z;
R10 performed no fresh device calls. Native acceptance remains BLOCKED pending
owner-supported deployment/permission recovery. Overall product: NOT VERIFIED.

## Reproduce the standalone scenario

The following uses the exact compatible R8 W2 dependency family (SECURITY=OFF).
Paths are local prerequisites relative to the original workspace, not artifacts
provided by a fresh clone. Preserve the tested Fast DDS factory ABI; a version
number alone does not establish compatibility. See
[Host IO diagnostics](HOST_IO_DIAGNOSTICS.md) for dependency hashes and bridge
build provenance. Use new output directories and the recorded authorized owned
localhost execution context; sandbox denial is a failed run, not a bypassable
business result.

From PowerShell, set `$src` to the qualified checkout and use a fresh `$out`:

```powershell
$root = 'C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport'
$src = "$root/build/r5-host-fixes/publish-worktree"
$out = "$root/build/service-acceptance-repro"
$pixi = 'C:/pixi_ws/.pixi/envs/default'
$bridgePrefix = "$root/build/r8w2final/p"
$dds = "$root/build/g2a1/host214-install"
$ros = "$root/build/ros2-target/h3/runtime"
$rosDev = 'C:/pixi_ws/ros2-windows'
New-Item -ItemType Directory -Path "$out/tmp" -ErrorAction Stop | Out-Null
$env:PATH = "$dds/bin;$bridgePrefix/bin;$ros/bin;$rosDev/bin;$pixi/Library/bin;$pixi;C:/WINDOWS/system32;C:/WINDOWS"
$env:TEMP = "$out/tmp"
$env:TMP = "$out/tmp"
$env:PYTHONPATH = "$rosDev/Lib/site-packages"
$env:AMENT_PREFIX_PATH = $rosDev
$env:PYTHONDONTWRITEBYTECODE = '1'
Remove-Item Env:PYTHONUTF8 -ErrorAction SilentlyContinue
Remove-Item Env:PSModulePath -ErrorAction SilentlyContinue
$env:CMAKE_BUILD_PARALLEL_LEVEL = '1'
$env:MSBUILDDISABLENODEREUSE = '1'
& "$pixi/Library/bin/cmake.exe" -S "$src/examples/ros2_service_acceptance" -B "$out/b" -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_CONFIGURATION_TYPES=Release' '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL' "-DPython3_EXECUTABLE=$pixi/python.exe" "-DFastDDSDSoftBusTransportRos2Host_DIR=$bridgePrefix/lib/cmake/FastDDSDSoftBusTransportRos2Host" "-Dfastrtps_DIR=$dds/share/fastrtps/cmake" "-Dfastcdr_DIR=$rosDev/Lib/cmake/fastcdr" "-DCMAKE_PREFIX_PATH=$bridgePrefix;$dds;$ros;$rosDev;$pixi/Library"
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
& "$pixi/Library/bin/cmake.exe" --build "$out/b" --config Release -- /m:1 /nr:false
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
& "$pixi/python.exe" -B "$src/tests/ros2_host/run_service_acceptance.py" --exe "$out/b/Release/ros2_service_acceptance.exe" --bridge "$bridgePrefix/bin/fastdds_dsoftbus_ros2_host_bridge.dll" --fastdds-prefix $dds --ros-prefix $ros --pixi-prefix $pixi --output "$out/pair"
if ($LASTEXITCODE -ne 0) { throw 'service acceptance failed; retain pair evidence' }
```

The runner requires a nonexistent output directory, creates owned ROS logs,
selects two localhost ports, enforces a 65-second process window and writes
`result.json` plus both raw stdout/stderr files. Inspect those records and perform
the independent module/process/port and source/dependency checks used by the
tester before claiming a qualified reproduction. The recipe itself was checked
against recorded arguments; it was not run as another business pair.

Local canonical records, relative to `$root`:

- `build/r10-feature-window/PLAN.json` and `service-initial-test/INITIAL_TEST_RESULT.json` under that round directory.
- `build/r10-feature-window/service-review/REVIEW.json`, `REVIEW_FIXES.json` and `REVIEW_FINDINGS_CLOSURE.json` (the latter closes the earlier pending retest state).
- `build/r10-feature-window/service-final-test/FINAL_TEST_RESULT.json`, `SOURCE_SNAPSHOT.json`, `ACTUAL_LOADED_MODULES.json`, `INDEPENDENT_SERVICE_CHECK.json`, `PROCESS_PORT_CLEANUP.json`, `REVIEW_FIX_GATE_TESTS.json` and `EVIDENCE_MANIFEST.json`.
- Exact commands, environments, exit codes and timing: `configure.intent.json`, `build.intent.json`, `pair-final.intent.json` and matching result/stdout/stderr records in that final-test directory.
- Raw successful pairs: `build/r10serviceinitial/pair-approved/result.json` and `build/r10servicefinal/pair-final/result.json`. Retained failed attempts and startup negative are linked by the initial/final reports.

The published starting point is `93256290bd9461954c5e72f187ad20cf944350f0`.
The final tester's commit gate covers only the three additive service files.
Documentation readiness is separate from Main's graph and publication gates;
this page does not assign a future commit SHA or claim CI execution.

## Historical A/04 diagnosis: next work is PLANNED

R10 delivered a design review, not new sensors or a historical failure fix. No
A/04 business window was rerun. The existing local handle/epoch and first-refusal
capture cannot uniquely join peer connections or reconstruct every budget
charge/refund. Priorities are:

1. Record connected local/peer endpoint tuples and roles; uniquely match reversed
   tuples within a lifetime. A directed two-process peer-close control must
   qualify the sensor. Reused/ambiguous tuples and unvalidated cross-process
   timing remain UNKNOWN, not causal proof.
2. Retain bounded scalar records for every refusal and lifecycle stage, including
   original ingress identity. Collision-safe record/release matching must reject
   duplicate or missing identities; retain no payload/native ownership.
3. Only if the remaining hypothesis requires exact occupancy, separately review
   an immutable record identity and gate-sequenced budget charge/transition/refund
   journal covering rollback and discard. Out-of-gate callback ordering is not
   exact budget mutation order.

After sensors pass independent test/review/retest, predeclare at most one original
A and one original 04 window with unchanged business conditions. Stop on absent
target failure, ambiguity, incomplete capture or unexpected cleanup failure.
Neither matching a fragment nor a controlled peer-close schedule proves the
historical cause. W1 ancestry remains unqualified and must not be copied wholesale
into W2. The design evidence is
`build/r10-feature-window/diagnostic-design/DESIGN_REVIEW.json` and `PLAN.md`.
