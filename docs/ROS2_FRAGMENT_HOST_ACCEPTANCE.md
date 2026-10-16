# R21 typed String Host delivery acceptance

Current evidence: 2026-10-16 16:56 UTC. **Scoped HOST_TESTED PASS:** Reliable
30/30, strict Best Effort 30/30 and R17 serialized regression 4/4, after independent
review and final testing. These are real Windows ROS processes using the unchanged
TCP-backed fake DSoftBus Host seam. Native DSoftBus/cross-device remains BLOCKED.

## Qualified scope

Each reliability window has two processes, one typed std_msgs/msg/String business
publisher/subscription per process, actual QoS checked as volatile KeepLast(10),
intra-process OFF. Each direction sends five samples per phase:

| Phase | String.data bytes | Application schedule |
| --- | ---: | --- |
| small | 1024 | Sequentially confirmed request/reply |
| large | 66000 | Sequentially confirmed request/reply |
| burst | 66000 | Five contiguous publishes before executor dispatch |

All directions/phases require exact IDs 1–5 and full reconstructed payload bytes.
Actual CDR lengths are measured and cross-checked with the peer; real nonzero
publisher GID/publication sequences map received IDs, without assuming sequence
equals ordinal. A missing Best Effort sample is FAIL, not allowed-loss PASS.
Run/PID/phase barriers, current loaded-module hashes, connection/I/O/full receive
capture, charge/refund reconciliation, normal entity/context/runtime close,
pump join, exit 0 and exclusive port rebind are required. Bounds are unchanged:
50s child, 65s runner, 80s outer cleanup. Finite observation is not permanent
non-delivery proof or wire DATA_FRAG/drop-causality evidence.

## Reproduction in the qualified environment

From W3 root, use the existing x64 MSVC/ROS Host environment and pinned prefixes
from the retained configure/build receipts. This is default-2145 Host only;
the CMake target rejects non-Windows. Choose fresh build/output directories and
stop on a failing command; no automatic retry or changed budgets are prescribed.

```powershell
$python = 'C:/pixi_ws/.pixi/envs/default/python.exe'
$cmake = 'C:/pixi_ws/.pixi/envs/default/Library/bin/cmake.exe'
$repo = 'C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport'
$build = "$repo/build/r21h-repro"  # must be unused
& $cmake -S examples/ros2_fragment_acceptance -B $build -G 'Visual Studio 17 2022' -A x64 `
  '-DCMAKE_CONFIGURATION_TYPES=Release' '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL' `
  "-DFastDDSDSoftBusTransportRos2Host_DIR=$repo/build/r13pf/dp/lib/cmake/FastDDSDSoftBusTransportRos2Host" `
  "-Dfastrtps_DIR=$repo/build/g2a1/host214-install/share/fastrtps/cmake" `
  '-Dfastcdr_DIR=C:/pixi_ws/ros2-windows/Lib/cmake/fastcdr' `
  "-DCMAKE_PREFIX_PATH=$repo/build/g2a1/host214-install;C:/pixi_ws/ros2-windows;C:/pixi_ws/.pixi/envs/default/Library"
if ($LASTEXITCODE) { throw 'configure failed' }
& $cmake --build $build --config Release --target ros2_fragment_acceptance -- /m:1 /nr:false
if ($LASTEXITCODE) { throw 'build failed' }

$suites = 'test_fragment_acceptance.py','test_serialized_acceptance.py',
  'test_lifecycle_acceptance.py','test_control_files.py',
  'host_io_capture_tests.py','test_host_receive_capture.py'
foreach ($suite in $suites) {
  & $python -B "tests/ros2_host/$suite" -v
  if ($LASTEXITCODE) { throw "$suite failed" }
}

$common = @('--bridge', "$repo/build/r13pf/dp/bin/fastdds_dsoftbus_ros2_host_bridge.dll",
  '--host-prefix', "$repo/build/r13pf/dp", '--fastdds-prefix', "$repo/build/g2a1/host214-install",
  '--ros-prefix', "$repo/build/ros2-target/h3/runtime", '--pixi-prefix', 'C:/pixi_ws/.pixi/envs/default')
& $python -B tests/ros2_host/run_lifecycle_acceptance.py @common `
  --exe "$build/Release/ros2_fragment_acceptance.exe" --scenario fragment-reliable `
  --output "$repo/build/r21-repro/reliable"
if ($LASTEXITCODE) { throw 'Reliable failed; preserve output and stop' }
& $python -B tests/ros2_host/run_lifecycle_acceptance.py @common `
  --exe "$build/Release/ros2_fragment_acceptance.exe" --scenario fragment-best-effort `
  --output "$repo/build/r21-repro/best-effort"
if ($LASTEXITCODE) { throw 'Best Effort failed; preserve output and stop' }
& $python -B tests/ros2_host/run_lifecycle_acceptance.py @common `
  --exe "$repo/build/r17f/r17/Release/ros2_serialized_acceptance.exe" --scenario serialized `
  --output "$repo/build/r21-repro/serialized"
if ($LASTEXITCODE) { throw 'serialized regression failed' }
```

The six suites contain 68 methods; synthetic controls do not substitute for ROS
execution. Exact canonical argv/cwd/environment, start/end/exit, compiler-read
inputs, module hashes and full raw output live in the original checkout's ignored
`build/r21-host-delivery/{initial-test,review,final-test}/` receipts. The shared
runner retains child command/environment deltas, result.json and both raw logs.
Review independently exercised 21 control methods plus 2 saved-log positive and
32 negative replays; no corrective code changes were required (Tier 2).

## Initial and final evidence

Initial testing freshly configured/built directly from W3 with MSVC x64 Release
/MD. Final testing reused that unchanged artifact after current source/compiler/
dependency/artifact verification; **there was no fresh final compilation**.
Executable SHA256: `f012cedb55398f32ece4f7a3dc7a8dc6d156c992a49d4c66f164625e536b54a1`.
Both initial and final stages passed 68 controls and their three actual windows.
Final windows each ran once:

| Final window | Run | Business result |
| --- | --- | --- |
| Reliable | 123bee1ae572cb14 | 30/30 |
| Best Effort | 9d0aeeeeb21df250 | 30/30, strict no-loss oracle |
| R17 serialized regression | 24f847d65d09efa3 | 4/4 |

All six final processes exit 0, with no timeout/forced kill/orphan, six exclusive
port rebinds and send_errors=0. Original limits remain records64/bytes524288,
local records32/bytes262144, copying8. No actual capacity refusal occurred, so
these windows do not claim to exercise capacity rejection. Final Reliable B has
one pre-Bridge AdapterPhase/InvalidState decision during Closing: layer1/branch9,
decision_phase4/status5/reason0, bridge_called0/capacity_known0, ingress85.
Other final sides/windows have zero non-acceptance rows. Initial Reliable A's
two Closing decisions are separate observations; no values are backfilled.
Existing unique-flow/associated-Reader startup diagnostics remain, not fixed.

The first final tester completed provenance, 68 controls and Reliable; after an
exec-session/software-update infrastructure interruption, a finishing tester
verified Best Effort had not launched, then ran Best Effort and R17 once each
and sealed final guards. A read-only precheck self-match was corrected. No failed
application window was rerun; interruption and raw records remain.

| Local evidence manifest | Files | SHA256 |
| --- | ---: | --- |
| initial-test/EVIDENCE_MANIFEST.json | 156 | 0fd0d0565ea09ab9a83c9707f7c92cdbb65174e818698c0b3bf61d457938cc05 |
| review/EVIDENCE_MANIFEST.json | 21 | 8c36705d38715cd1b15f99f2c8d70e539da10093b7809bf4f5a405b8c0bb1632 |
| final-test/EVIDENCE_MANIFEST.json | 147 | 349e2a740710264430ddbd8bf6a8cb7cde7c8d38cfd2b2aac8881ea3fc63c59a |

Local-only [final report](../../../r21-host-delivery/final-test/REPORT.md) and
[final result](../../../r21-host-delivery/final-test/RESULT.json) support this
snapshot; ignored evidence is not included in remote documentation commits.
The new fixture's first creation preceded named-symbol impact: this remains a
real process deviation, not retroactively compliant. Main preserved/dispositioned
it in `main-publication/PROCESS_DEVIATION_DISPOSITION.json`; full current graph
and PRE_COMMIT remain required, independent of the code review/test PASS.

## Capability boundary

| Capability | Status |
| --- | --- |
| Fixed typed String small/66k/burst, two same-policy QoS windows | Scoped HOST_TESTED PASS |
| Normal close, current-module/capture/refund/port cleanup in those windows | Scoped HOST_TESTED PASS |
| R20 offline String storage + POSIX entry on each RK3588A independently | Earlier bounded DEVICE_TESTED PASS; separate [target preparation](ROS2_ROSBAG_TARGET_PREPARATION.md) |
| Native DSoftBus / real cross-device R21 delivery | BLOCKED; DEVICE_TESTED=false for R21 |
| Arbitrary types/sizes/load, general QoS, wire-fragment/drop causality | NOT VERIFIED; R14 failures remain open |
| R22 reliability offered/requested boundary | PLANNED read-only triage, not implemented/tested |

Native communication still lacks full current project-only token-state and
supported lossless restoration material. No registry retry or permission/security/
identity/network/device change occurred. Same-policy Best Effort success is one
fixed observation, not a general lossless guarantee. Full rosbag CLI/record/replay,
loaning/zero-copy/security/native acceptance are not established by this fixture.

Last confirmed publication is R20 `ab97486d814e58023f7d4f00c8bed7a945852b1a` on
existing origin/codex/flow-encoding-published-20261014; exact-SHA CI at 16:12:11 UTC
was NOT_RUN (zero workflows/checks/status contexts). R21 publication/CI remains
pending Main, with no future commit SHA predicted.
