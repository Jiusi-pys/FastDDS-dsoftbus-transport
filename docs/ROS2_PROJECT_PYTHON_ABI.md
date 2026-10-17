# R24 project-linked Python ABI, no Node

> **Publication/current boundary (2026-10-17 20:11 UTC):** R24 remains the published no-Node baseline `d8b0fdfecaa654183c57d4b973cc936234080667`. [R25 native C++ Action](ROS2_NATIVE_ACTION_ACCEPTANCE.md) was separately published as `4794ffb08d2d1cf49e758a0a4627f4aa2c87de92`. The subsequent [ordinary Python/CLI node-discovery stage](ROS2_ORDINARY_CLI_NODE_DISCOVERY_ACCEPTANCE.md) now has build, independent review/post-review testing and real A/B execution evidence: single public Node lifecycle and stock no-daemon node listing pass in both directions. Its Main graph/PRE_COMMIT and publication gates remain pending. This does not broaden the historical R24 ABI claim below. Ordinary topic/Action CLI, bag and full ABI coverage remain UNVERIFIED; earlier permission/publication statements below are dated snapshots.

At 2026-10-17 06:02 UTC, fresh build/package and independent A/B execution pass
for **PROJECT_LINKED_PYTHON_ABI_NO_NODE**. The isolated prefix uses project Fast
DDS 2.14.5/Fast CDR 2.2.5 and project RMW libraries before the first rclpy import.
It reuses the measured rclpy binary candidate and existing CPython 3.12.7, then
checks native imports, exact String serialization/deserialization and CLI
help/resource lookup. These results establish only those executed ABI paths.
Node/daemon lifecycle, full NumPy/ROS ABI and DSoftBus communication remain
unverified. See the [current capability matrix](STATUS.md) for earlier scopes.

## Implementation and dependencies

The four additive files are the [build/package driver](../integrations/ros2/target/project_python.py),
[target CMake hook](../integrations/ros2/target/ProjectPython.cmake),
[no-node probe](../integrations/ros2/target/project_python_probe.py) and
[A/B runner](../tests/ros2_target/run_project_python.py). Transport, DDS and RTPS
semantics are unchanged. The build driver is Windows/CPython-specific: its
process ownership helper uses `_winapi` and Windows Job Objects. Linux/macOS
host execution is not qualified. Host Python runs generators; exact OHOS Python
and NumPy headers/libraries compile target extensions.

Reproduction requires this local prepared input set, not just a fresh Git clone.
`ROOT` below is the original workspace; `W3` is the publication checkout. All
paths in this table are relative to `ROOT` unless noted. Missing inputs must be
prepared and provenance-checked before running; do not fill them from a different
generic ROS installation. Input capture helpers and evidence under `build/` are
local ignored records and are not delivered by links in this document.

| Required input | Exact source / role |
| --- | --- |
| Toolchain selection | `build/ros2-target/r2/BUILD_SELECTION.json`: SDK23 native toolchain, CMake `C:/pixi_ws/.pixi/envs/default/Library/bin/cmake.exe`, host Python `C:/pixi_ws/.pixi/envs/default/python.exe`, Ninja `C:/Users/17715/scoop/shims/ninja.exe`, host generators `C:/pixi_ws/ros2-windows`; selected tool hashes are rechecked. |
| Project providers | `build/g2a1/target214-install` (Fast DDS 2.14.5), `build/ros2-target/r2/ros/install`, `build/r18-action-native/dependency-build/install` (rcl_action), `build/r19baglocal5/bundle`; dependency prefixes in BUILD_SELECTION include Fast CDR 2.2.5, tinyxml2-6, foonathan-0.7.4 and ROS bootstrap. |
| Archived/build sources | `build/ros2-target/r2/src/ros`, `ros2/src/ros2/rosidl_python/rosidl_generator_py`, and `ros2/ohos/patches_full/ros2_rosidl_python.patch` (SHA256 `5778a37e5405d8161e03d73e689c5423ddf27a292c9a313db6efd38161d2ca1b`). The patch applies only to an isolated build source copy. |
| Exact target development inputs | `build/r24-project-python/python-dev/{MANIFEST.json,usr/include/python3.12,usr/lib/libpython3.12.so.1.0,numpy-include}`: 214 CPython and 31 NumPy headers. |
| Target validation/provenance | `build/r24-project-python/python-dev-fix1/{VALIDATION.json,HEADERS_VALIDATION.json,_sysconfigdata__linux_aarch64-linux-ohos.py}` and `python-inputs/A-PYTHON_RUNTIME_ARTIFACT.manifest.json` under the same stage directory. A/B exact-header checks and runtime hashes bind these inputs. |
| Candidate packages | `build/r24-project-python/python-candidates/{INPUT_MANIFEST.json,GENERIC_VALIDATION.json,OVERLAY_VALIDATION.json,generic,overlay}`; 340 generic and 914 overlay candidate files were checked against their release/locked manifests. Only explicit native candidates enter the slice; generated interface support comes from the project build. |
| NumPy/platform observations | `build/r24-project-python/numpy-diagnostic/RESULT.json` (NumPy 1.26.4 and arange `[0,1,2]` observed in preparation); `platform-inputs/ld-musl-aarch64.so.1` under that stage (inspected for symbols, never bundled). |
| Preparation seal | `build/r24-project-python/PREPARATION_RESULT.json` and `PREPARATION_MANIFEST.json` bind 1,657 prepared files; manifest SHA256 `28aefb31f40ee9b64c8c8c6e1bddc78c0c8e9af995945836f3710267eef44f52`. |

The build generates `builtin_interfaces`, `unique_identifier_msgs`,
`service_msgs`, `action_msgs`, `type_description_interfaces`, `rcl_interfaces`,
`lifecycle_msgs` and `std_msgs` Python support; builds rcl_lifecycle and the
upstream rmw_implementation dispatcher; and packages the measured native
dependency closure. Exact SONAME files select project providers. The reused
rclpy extension SHA256 is
`997bb060c4f5508d6facb730a3d11957eb09976d9a3a481b1e0514b9f7155c4f`.
Successful selected-path execution supports this reuse; it is not a general
compatibility claim for arbitrary rclpy builds or APIs.

## Reproduce the build and package

These are the actual final command forms with a fresh output name for reruns.
The canonical completed directory is `$W3/build/r24-project-python/final-runtime`;
preserve it. Both the build output and package directory must be new. Commands
stop on failure; inspect retained receipts rather than overwrite a failed run.

```powershell
$ROOT = 'C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport'
$W3 = Join-Path $ROOT 'build/r15-lifecycle/publish-worktree'
$python = 'C:/pixi_ws/.pixi/envs/default/python.exe'
$hdc = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe'
$runId = 'repro-' + [guid]::NewGuid().ToString('N')
$runtime = Join-Path $W3 ('build/r24-project-python/' + $runId)
$evidence = Join-Path $ROOT ('build/r24-project-python/' + $runId + '-evidence')
$candidates = Join-Path $ROOT 'build/r24-project-python/python-candidates'
$driver = Join-Path $W3 'integrations/ros2/target/project_python.py'
$runner = Join-Path $W3 'tests/ros2_target/run_project_python.py'
Set-Location -LiteralPath $ROOT
& $python -X utf8 -B $driver --inputs-root $ROOT --output $runtime --phase build
if ($LASTEXITCODE -ne 0) { throw 'Build failed; retain receipts' }
& $python -X utf8 -B $driver --inputs-root $ROOT --output $runtime --phase package --python-candidates $candidates
if ($LASTEXITCODE -ne 0) { throw 'Package failed; retain receipts' }
$package = Join-Path $runtime 'package'
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $package 'PACKAGE.json')
```

`BUILD.json` records selected sources, tools' command receipts, development
inputs and installed artifact hashes; `commands/` retains exact build argv,
times, exits and process cleanup results. `PACKAGE.json` records every member,
origin and ELF dependency; `SYMBOL_CLOSURE.json` checks required symbol names.
Symbol closure is a necessary check and does not by itself establish ABI safety.
Build commands are assigned to an unnamed Windows Job before resume; success
requires an empty owned tree. Timeout/interruption/setup/cleanup failures remain
visible, with a separate bounded cleanup wait. `finish-build` exists to preserve
an earlier interrupted build state; the final qualification used a fresh `build`.

## Reproduce the no-node device checks

The existing native runtime must remain at
`/data/python312-rk3588a-verify-c646982b346f/usr`, with libpython SHA256
`47193e17cf471888d1ec325447f8b89f16c022d310a477db9d3d4744eaef191a`.
The platform loader SHA256 is
`d3c3b126a2715ba7d3b6f312776a4b6a55177367ba859970b14033d177eafc86`.
The runner's fixed A/B targets are `3e01ff55454d202020104033bf453b00` and
`3e01ff55454d202020104433991c3b00`. With those boards available, continue from
the variables above, once per board and stop at the first failure:

```powershell
& $python -X utf8 -B $runner --hdc $hdc --side A --package $package --output (Join-Path $evidence 'A')
if ($LASTEXITCODE -ne 0) { throw 'A failed; inspect evidence before continuing' }
& $python -X utf8 -B $runner --hdc $hdc --side B --package $package --output (Join-Path $evidence 'B')
if ($LASTEXITCODE -ne 0) { throw 'B failed; inspect evidence' }
```

The runner validates local manifest membership/hashes, creates a short local
USTAR archive, sends it to a fresh `/data/local/tmp/r24-python.*` directory,
checks the archive and every extracted member, then invokes `sh slice/run` in
four fixed modes: `probe`, `help`, `pkg_prefix`, `interface`. The CLI modes mean
`ros2 --help`, `ros2 pkg prefix rclpy` and
`ros2 interface show std_msgs/msg/String`. No Node or daemon command is run.
The generated entry selects `PYTHONPATH=slice/python`,
`AMENT_PREFIX_PATH=slice`, `LD_LIBRARY_PATH=slice/lib:<existing-runtime>/usr/lib`
and exact libpython before importing rclpy; the generic ROS library prefix is
excluded. It does not replace `/usr/local/bin/ros2`, global Python or shared
generic configuration. Platform libc remains external.

Outputs include `result.json`, `commands.json`, task stdout/stderr and the
package verifier. Remote operations use 60-second limits; HDC command receipts
retain host and remote completion separately. Owned remote scratch and short
local archive directories are cleaned up; canonical host package/evidence stay.
This runner is a bounded qualification tool, not a general ROS installation or
an arbitrary CLI launcher. Failed attempts require diagnosis, not automatic retries.

## Final evidence and retained failures

Canonical package: `$W3/build/r24-project-python/final-runtime/package`.
Its `PACKAGE.json` SHA256 is
`b8cd4ff42c347544a6e7994a78b4c4983024ddf27cd6302a2ee1c35db281762f`.
It describes 2,058 files / 141 AArch64 ELF members; the transmitted archive adds
PACKAGE.json, making 2,059 members. Final archive SHA256 is
`7265dc9a5e377a1d71049c366da91e347b3fc0c79b2e99fef5b6bfde156963db`.
These identify the completed artifacts; a fresh output path creates a new
provenance-bound package and is not assumed to reproduce the same manifest hash.

Final tester `build/r24-project-python/final-test` under ROOT records:

- Fresh 35-command build, all exits 0, 769 AArch64/OHOS compilation units,
  including 98 generated Python units with exact target includes; 2,342
  installed artifact hashes validated.
- A/B sequential actual imports, native NumPy core and exact String CDR:
  `project-python-ABI-中文` produced
  `000100001a00000070726f6a6563742d707974686f6e2d4142492de4b8ade6968700`
  and deserialized unchanged. All three CLI modes passed; package prefix was
  the fresh slice and String interface displayed `string data`.
- All 38 independent device checks passed; task host/remote exits 0 and empty
  stderr. Each board loaded 115 PROJECT_SLICE native files with matching
  package/A/B hashes, including project Fast DDS/Fast CDR/RMW. Native NumPy
  import/origin checks do not qualify its full API.
- Owned scratch/archive removal and bounded process snapshots passed. Generic
  wrapper/environment/bootstrap, exact libpython/platform loader and protected
  root/W1/W2/W3 baseline/R22 files were unchanged. These snapshots are not proof
  of global daemon absence or actual device fault-injection behavior.

`TESTER_RESULT.json`, `REPORT.md`, phase execution receipts, `BUILD_CHECK.json`,
`PACKAGE_CHECK.json`, `DEVICE_CHECK.json`, `FINAL_EVIDENCE_BINDING.json`, raw logs
and `SEAL.json` bind 195 evidence files. Final SHA256_MANIFEST.json SHA256:
`3e03c493489e70b94973eb0593dcac09781fdb7ba82c0eb2570560da17f8f28b`.
All these build/evidence paths are local ignored artifacts, not remote downloads.

| Preserved stage | Actual result and correction |
| --- | --- |
| Preparation | Wrong HDC input paths and empty PyYAML.libs assertion retained; corrected exact retrieval and verified empty directory recorded separately. |
| `initial-test` | BLOCKED: Windows long-path HDC transfer; runner changed to a complete, hashed short-path archive. |
| `initial-fix1` | FAIL: anonymous maps label opened as a file; parser now records anonymous mappings separately while retaining strict file-origin checks. |
| `initial-fix2` | FAIL before String serialization: four installed pure-Python typesupport helper files missing from package; packager now includes them from the exact project build. |
| `initial-fix3` | PASS for no-node package/ABI, before independent review. |
| `review` | Tier 2 P2 `R24-P2-001`: timed-out Windows build descendants survived. Original evidence remains. |
| `review-fix1` | Resolved by assigned-before-resume process-tree ownership; independent focused review passed 23 cases/controls with no actionable findings. |
| `final-test` | Fresh rebuild/package/A/B PASS; ownership passed 21 real-process cases plus sibling survival and stable handles (149 to 149). The tester's initial 74-unit selector omitted 24 generated entrypoint units; retained harness failure corrected to source-path counting of all expected 98, without production changes, rebuild or lowered expectation. |

Build/HOST_TESTED/DEVICE_TESTED and tester Commit gate are PASS only for this
selected no-node scope. No rclpy.init, Context initialization, Node, Participant,
daemon, network exchange or DSoftBus API was exercised. Broader Transport
integration is N/A to these additive tooling changes; native DSoftBus integration
remains BLOCKED / NOT_VERIFIED, and cross-device messages were NOT_TESTED.
Project DATASYNC remained denied at the last 2026-10-16 query; new persistent
identity is NOT_APPROVED / NOT_EXECUTED. Prior whole-registry access was rejected
by automatic approval review and not retried. No security/permission/network
change occurred. R22 Host QoS work remains STOPPED, and historical capability
limits are unchanged. R24 Main graph/PRE_COMMIT, commit/push and CI are PENDING.
