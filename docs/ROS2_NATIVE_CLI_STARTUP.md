# R23 native ROS 2 CLI startup

> **Current cross-reference (2026-10-17 20:11 UTC):** R24 project-linked no-Node ABI and [R25 native C++ Action](ROS2_NATIVE_ACTION_ACCEPTANCE.md) are published. The subsequent [ordinary project CLI node-discovery stage](ROS2_ORDINARY_CLI_NODE_DISCOVERY_ACCEPTANCE.md) has independently reviewed/tested implementation and real RK3588A AB/BA PASS for single public Node lifecycle and stock `ros2 node list --no-daemon --spin-time 3`. Its Main graph/PRE_COMMIT and publication gates remain pending. Ordinary topic/Action CLI, daemon mode and bag communication remain UNVERIFIED. The R23 generic deployment results below retain their original scope.

As of 2026-10-17 04:42 UTC, the existing generic OpenHarmony deployment passes
bounded CLI startup on two RK3588A boards. This qualifies real Python/native
rclpy imports, help and two resource queries. It does not qualify the project's
DSoftBus-linked CLI, Node/daemon lifecycle, pub/sub, service, action, QoS or
cross-board communication. See the [current capability matrix](STATUS.md).

## Executed scope and artifacts

The additive [probe](../integrations/ros2/target/cli_runtime_probe.py) records
Python/module/native-extension paths and hashes, package metadata, environment
and mapped DSOs without initializing rclpy or a Node. The
[runner](../tests/ros2_target/run_cli_startup.py) checks reviewed file hashes
before sourcing the existing environment, then runs the probe and exactly:

```text
/usr/local/bin/ros2 --help
/usr/local/bin/ros2 pkg prefix rclpy
/usr/local/bin/ros2 interface show std_msgs/msg/String
```

| Item | Observed value |
| --- | --- |
| Board A / B | `3e01ff55454d202020104033bf453b00` / `3e01ff55454d202020104433991c3b00` |
| Python | 3.12.7, AArch64; `/data/python312-rk3588a-verify-c646982b346f/usr/bin/python3.12` |
| ABI / loader flags | `cpython-312-aarch64-linux-ohos` / 258 |
| Existing ROS prefix/profile | `/data/local/tmp/ros2-generic`; `FASTDDS_BUILTIN_TRANSPORTS=UDPv4` |
| Native extension | `Lib/site-packages/rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so`, relative to that ROS prefix; 1,029,952 bytes |
| Extension SHA256 | `997bb060c4f5508d6facb730a3d11957eb09976d9a3a481b1e0514b9f7155c4f` |
| Probe SHA256 | `62989505f05247c36f27672198287ce4ed613a1ab18b3e8619d56fc5ccde97ee` |
| Runner SHA256 | `73eb80118e8b2b91f42673ed493c461bce2eed9aafdccde8046afff03c21c5e2` |

Final independent A/B executions were sequential, without retries. All four
tasks per board returned host 0/remote 0 with empty task stderr. Package prefix
was the generic prefix; String definition contained `string data`. Exact owned
scratch directories were removed and independently confirmed absent; process
snapshots showed no remaining visible owned probe/CLI/Python processes.
Successful snapshots do not prove global daemon absence or timeout descendant
termination. Optional numpy/psutil were located, not functionally tested.

## Reproduction with reviewed inventory

Run from the checkout containing the two scripts, using host Python and HDC.
The runner requires `--inventory`: three existing files per board, in the layout
below. It does not generate trusted inventory or change the shared installation.
To capture a fresh inventory, use a fresh local directory and receive only these
exact existing paths. These commands copy bytes; inspect them before any runner
execution (which sources the environment). A changed deployment requires new
inspection, not automatic acceptance of a newly captured hash.

```powershell
$hdc = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe'
$inventory = Join-Path $env:TEMP ('r23-inventory-' + [guid]::NewGuid().ToString('N'))
$targets = @{ A = '3e01ff55454d202020104033bf453b00'; B = '3e01ff55454d202020104433991c3b00' }
$bootstrap = '/data/local/tmp/ros2-core-config/python-16920f1f6460d75a65be821e3ddac34cdc5f2a01b65e427414c7f7118adc438d/sitecustomize.py'
foreach ($directory in 'existing-entry', 'existing-environment', 'existing-bootstrap') {
    New-Item -ItemType Directory -Path (Join-Path $inventory $directory) | Out-Null
}
foreach ($side in 'A', 'B') {
    & $hdc -t $targets[$side] file recv /usr/local/bin/ros2 "$inventory/existing-entry/$side-ros2"
    if ($LASTEXITCODE -ne 0) { throw 'wrapper capture failed' }
    & $hdc -t $targets[$side] file recv /data/local/tmp/ros2-generic/env.sh "$inventory/existing-environment/$side-env.sh"
    if ($LASTEXITCODE -ne 0) { throw 'environment capture failed' }
    & $hdc -t $targets[$side] file recv $bootstrap "$inventory/existing-bootstrap/$side-sitecustomize.py"
    if ($LASTEXITCODE -ne 0) { throw 'bootstrap capture failed' }
}
Get-ChildItem -LiteralPath $inventory -File -Recurse | Get-FileHash -Algorithm SHA256
Get-ChildItem -LiteralPath $inventory -File -Recurse | ForEach-Object {
    $_.FullName
    Get-Content -LiteralPath $_.FullName -Encoding utf8
}
```

The reviewed A/B files were identical per role: wrapper
`f9a8595287f8cdf467fab8c99d81de46744e4ae2d6954518bf0fa1641628477e`, environment
`371bf4f8575d6f005416b7d94f8c02f18b832f13f04c8903fdbf8d8d479110dc`, bootstrap
`16920f1f6460d75a65be821e3ddac34cdc5f2a01b65e427414c7f7118adc438d` (SHA256).
The wrapper/env select the existing generic release and Python; the bootstrap
sets its OHOS loader behavior. Preserve this deployment. After inspecting the
snapshots and confirming the intended paths/profile, run each board once:

```powershell
python -B tests/ros2_target/run_cli_startup.py --hdc $hdc --inventory $inventory --side A --output "$inventory/run-A"
if ($LASTEXITCODE -ne 0) { throw 'A failed; inspect evidence before continuing' }
python -B tests/ros2_target/run_cli_startup.py --hdc $hdc --inventory $inventory --side B --output "$inventory/run-B"
if ($LASTEXITCODE -ne 0) { throw 'B failed; inspect evidence' }
```

Output directories must not exist. The runner records exact HDC/shell argv,
timestamps, stdout/stderr, host/remote exits, artifact hashes and cleanup. It
stops at the first failure with remote 45-second / host 60-second limits, writes
only a fresh `/data/local/tmp/r23-cli.*` scratch directory on the device, and
removes only its known files and that exact directory. No identity, permission,
registry, global configuration or shared generic-deployment change is involved.

## Evidence and limits

Evidence is local under `build/r23-native-cli` in the original workspace (outside
this publication worktree); these records are ignored and are not uploaded by
documenting their paths. Canonical `final-test/TESTER_RESULT.json`, `REPORT.md`,
`A_execution.json`, `B_execution.json`, per-board `commands.json`, raw outputs,
postchecks and `SEAL.json` bind 34 passing checks and 73 evidence files.
`final-test/SHA256_MANIFEST.json` SHA256 is
`d0ab05a91c446a661a9ae3bf0b2feb7b9d29b960703167aae9815d2fcbc3ab58`.
Formal Tier 2 `review/REVIEW_RESULT.json` has no actionable findings, 14 local
fault controls and a 70-file seal; no implementation changed after review.

The initial `initial-test` help-oracle failure is retained: the reviewed wrapper
uses Python `-c`, so actual help begins `usage: -c`, not the assumed `usage: ros2`.
The corrected oracle additionally requires ROS help identity and core commands;
`initial-fix1` and final testing PASS. Python AST checks PASS; fresh C++ build and
Transport integration are N/A to these additive scripts. No Node or daemon was
started by the test. DEVICE_TESTED=PASS is scoped to CLI/import/resource lookup;
native DSoftBus integration and cross-device communication are NOT_TESTED.
Main's final graph/PRE_COMMIT and R23 publication remain PENDING.

`abi-triage/REPORT.md` and `LOADED_CANDIDATE_COMPARISON.json` show the actual rclpy
ELF has 71 direct DT_NEEDED entries and 578 named undefined dynamic symbols,
including direct Fast CDR and generated Fast RTPS typesupport dependencies.
Import already loads generic Fast DDS 2.14.6/Fast CDR 2.2.7; the project selects
2.14.5/2.2.5. Matching names or Jazzy labels do not prove compatibility.
The next bounded step is an isolated project Python/library prefix selected
before the first rclpy import. Reuse existing CPython and suitable R18 rcl_action
artifacts, complete the measured closure, and determine whether the observed
extension can be reused; a rebuild is not yet proved necessary. No whole-ROS
rebuild or new RMW is implied.

Project DATASYNC remained denied at the last 2026-10-16 query. A new persistent
identity is NOT_APPROVED / NOT_EXECUTED. Earlier whole-registry access was
rejected by automatic approval review and not retried. R23 changes none of these
boundaries. R22 is STOPPED / UNVERIFIED; its five retained uncommitted files and
earlier R18/R20/R21 evidence remain separate from this scope.
