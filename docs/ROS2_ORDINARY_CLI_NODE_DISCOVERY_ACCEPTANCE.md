# R25 ordinary ROS 2 CLI node discovery on RK3588A

> **Publication / successor (2026-10-17 22:34 UTC):** This exact 11-file node-discovery stage was published as `471d344050dccae6cb10396d3432fc9f88a92ff7` to existing `origin/codex/flow-encoding-published-20261014`; 20:30:40 UTC CI was NOT_RUN (zero runs/checks/status contexts). [Later topic/echo evidence](ROS2_ORDINARY_CLI_TOPIC_ACCEPTANCE.md) distinguishes pre-repair topic/pub runs from fresh echo AB/BA after a four Host-file repair. That repair still awaits Main graph/PRE_COMMIT/publication. The dated pre-commit body below is retained unchanged.

At 2026-10-17 20:11 UTC, the independent final execution gate is **PASS for the
11-file Python entry / CLI packaging / bidirectional node-discovery stage**.
Stock `ros2 node list --no-daemon --spin-time 3` found the opposite board's
public rclpy Node in two real DSoftBus Socket windows. This is target execution,
not a Host stub. Overall ROS 2 feature completeness remains **NOT VERIFIED**.
The Main graph/PRE_COMMIT and publication gates are separate and pending at this
pre-commit documentation snapshot.

## Implementation and environment

The additive implementation consists of `actionbag_python_entry.cpp`,
`build_actionbag_python.py`, `build_actionbag_cli.py`, `build_actionbag_types.py`,
`build_actionbag_rosgraph.py`, `build_actionbag_runtime.py` and
`actionbag_runtime_probe.py` under `integrations/ros2/target/`, plus
`run_actionbag_cli.py`, `test_actionbag_cli.py`, `peer_evidence_view.py` and
`test_peer_evidence_view.py` under `tests/ros2_target/`. Exact hashes are in the
final tester's `SOURCE_FREEZE.json`. The types builder is a package input;
its presence does not qualify ordinary Action CLI execution.

The launcher enters Python only after existing token-first identity admission,
returns interpreter execution status, finalizes Python and permits original-token
restoration. Package composition validates exact native providers and generated
resources. The runner checks fresh peer evidence, actual loaded maps/hashes,
public/native close and owned process exit. No DDS, RTPS, RMW or discovery
semantics were changed to make these commands pass. Session API and builtin
UDP/TCP fallback are excluded.

| Environment / artifact | Qualified value |
| --- | --- |
| Checkout baseline | `build/r15-lifecycle/publish-worktree` (W3), `4794ffb08d2d1cf49e758a0a4627f4aa2c87de92` before this stage |
| Host | Windows, `C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B`; SDK23 OHOS AArch64 build inputs |
| Board A | RK3588A / HDC `3e01ff55454d202020104033bf453b00` |
| Board B | RK3588A / HDC `3e01ff55454d202020104433991c3b00` |
| Target Python | CPython 3.12.7, `cpython-312-aarch64-linux-ohos`; existing `/data/python312-rk3588a-verify-c646982b346f/usr` |
| ROS providers | Project Fast DDS 2.14.5 / Fast CDR 2.2.5 and project RMW; real native Socket, fake_sdk=0, builtin=0 |
| Identity | Existing `mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16`, normal APL, DATASYNC only; no identity creation or security/network change in these windows |
| Local package | `W3/build/r25-native-actionbag/runtime-qualified-cli-types-modules/package`: 2,811 members (2,809 payload + 2 bookkeeping), 167 ELF; 89 canonical external providers / 299 edges / 93 names including 4 aliases per board |
| Runtime manifest | `W3/build/r25-native-actionbag/runtime-qualified-cli-types-modules/RUNTIME_PACKAGE.json` SHA256 `c46747034682072be50e8a7ad4ca6bd4129e8846dafd7b7cc4305ff3309bc299` |
| Target deployment | `/data/local/tmp/mdds_dds_<run>_A` and `_B`; package and evidence are isolated per fresh run; diagnostic directories retained |

The final package adds exact qualifications for target `pyexpat`, `_decimal`
and `resource` extension providers observed in the earlier failed CLI maps.
Each file was captured from both boards with pre/post hashes and checked as
ELF64 AArch64 with its exact `PyInit_*` export and `libc.so` dependency. Removing
those three roots/edges leaves the previous 86-provider closure unchanged.
The 2,809 payload files were reused byte-for-byte from the preceding qualified
package; the final composition did not rebuild native binaries. Host builds,
synthetic controls and target runs remain distinct evidence classes.

## Actual commands and outcomes

The recorded package command (exit 0, owned build-process tree empty) was:

```powershell
# ROOT is C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport
# W3 is ROOT/build/r15-lifecycle/publish-worktree; BASE is ROOT/build/r25-native-actionbag
& C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B `
  "$W3/integrations/ros2/target/build_actionbag_runtime.py" `
  --inputs-root $ROOT --selection "$BASE/package-qualified-cli-modules-build/SELECTION.json" `
  --output "$W3/build/r25-native-actionbag/runtime-qualified-cli-types-modules"
```

Each executed host runner used the following form, substituting `ab` or `ba`:

```powershell
& C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B `
  "$W3/tests/ros2_target/run_actionbag_cli.py" run `
  --prepared "$BASE/ordinary-cli-modules-node-graph-ab-window/prepared/PLAN.json" `
  --output "$BASE/ordinary-cli-modules-node-graph-ab-window/execution" `
  --hdc C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe
```

These are historical executed commands, **not reusable release commands**.
The sealed query/plan/run directories must not be replayed or overwritten.
A new run needs fresh reciprocal peer evidence, exact package/provider and
identity bindings, a reviewed plan and a fresh output directory. The unchanged
300-second peer-evidence freshness budget covers dispatch; the current windows
are closed. All exact argv, times, exits and raw output remain in
`ACTUAL_RUN_HOST_START.json`, `ACTUAL_RUN_HOST_EXIT.json` and `execution/`.

The CLI role executed `${P}/run-token-first ros2 node list --no-daemon --spin-time 3`.
The other board executed `${P}/run-token-first --script
${P}/scripts/actionbag_runtime_probe.py --role hold-node --run <run> --side <side>
--finish-control ${W}/control.PROBE_FINISH` with one public Node/context per
process. Here `P` and `W` are the prepared package/control paths in the isolated
run. The CLI used its ordinary upstream implementation; module imports alone
are not evidence of a daemon being started.

| Window | Exact CLI result | Host / recorded remote exits | Lifecycle |
| --- | --- | --- | --- |
| AB `ad29a5b4d0d83169`, session 89396 | A printed `/ab_ad29a5b4d0d83169_B` exactly once | 0; all 231 HDC calls and 215 remote exit receipts were 0 | Both child exits 0; 4 owned PIDs absent; no forced signals |
| BA `becffc43fccc35dc`, session 53857 | B printed `/ab_becffc43fccc35dc_A` exactly once | 0; all 243 HDC calls and 227 remote exit receipts were 0 | Both child exits 0; 4 owned PIDs absent; no forced signals |

Both windows passed public Participant deletion, native RuntimeClose and Python
finalization with return 0, plus original-token restoration. The new three
providers were present with exact hashes in the CLI process at listener and
close checkpoints. This proves process-local closure and exit, not SDK-wide
callback drain, epoch/quiescence or repeated Participant lifetimes in one process.

## Validation and retained evidence

All evidence paths below are relative to `ROOT/build/r25-native-actionbag`.
They are local ignored records; naming them does not upload their logs or make
a clean checkout independently reproducible. Preserve the selected inputs and
receipts; missing input files are a reproduction blocker, not permission to
substitute another ROS/Python deployment.

| Record | SHA256 / result |
| --- | --- |
| `ordinary-cli-graph-stage-final-test/RESULT.json` | `8022f6eed1755b10ae2fe046b754c75e4220617f6b42f32c22c4f08f0085eca7`; Commit gate PASS for exact 11-file execution scope |
| Same directory `SEALED_MANIFEST.json` | `1049e1af6333f300d43fb997e195a549b9640c47b1abb59884d5b192ebf40a9b`; 51 members |
| `ordinary-cli-modules-node-graph-ab-window/FINAL_TESTER_RESULT.json` | `8623191b48eceabe99f8e0d82b1ed7aede26cf9476bc528a54a9a28bdebc8b4c`; PASS |
| AB `FINAL_TESTER_SEAL.json` | `44ce50de714dddd35ef822959d9767d7cff09c6fbbf79180822a6bbfb6abf6a0`; 1,044 members |
| `ordinary-cli-modules-node-graph-ba-window/FINAL_TESTER_RESULT.json` | `69751bbcffa96bb7814323cba8332838e58d3c042755b903537e3a7f3d16f20d`; PASS |
| BA `FINAL_TESTER_SEAL.json` | `f3303bf17bde6eb717757f51a99aeba8d8ae931e75ec49e5e87382f4bab8719f`; 1,091 members |
| `package-qualified-cli-modules-independent-test/RESULT.json` | `322d9847cba91828420b5c90407a7e9f1ec887913a91dff222076853d03fe113`; Host package PASS |

`SOURCE_GATE_CHAINS.json` in the final-gate directory binds independent initial
testing, review and post-review testing for launcher, CLI, generated types,
composer, ordinary runner and peer evidence converter. The final tester
revalidated 23 seals / 15,906 files, the current 11 sources and protected Git
state. It reused immutable execution evidence; it did not claim a fresh rebuild,
new product tests or a further device window. Host synthetic/mock controls are
not counted as native DSoftBus proof.

The earlier CLI overlay's two global preservation assertions remain FAIL in
their original record. The separately reviewed launcherfix2 exact-byte lineage
and current composition supply the accepted disposition; historical results
are not rewritten. The original missing-provider graph window and host
connection/parse/audit failures likewise remain FAIL/BLOCKED as recorded. A
later successful run or later process-absence check does not upgrade them.

## Capability boundary and next acceptance

| Capability | Current status / reason |
| --- | --- |
| Single public Python Node and token/finalize/close path | SUPPORTED within the recorded two-board, single-lifetime scope |
| Stock no-daemon node listing, A→B and B→A | SUPPORTED for these exact graph fixtures; DEVICE_TESTED PASS |
| CLI/types/rosgraph packaging | BUILD / HOST_TESTED PASS; node command target-tested. Package presence is not execution of every command, type or simulation-time path. |
| Optional unique flows | PARTIAL: the known failed first Reader attempt and existing RMW original-QoS retry remain in both boards' logs; unique flows NOT_PROVIDED, strict mode UNVERIFIED |
| Topic graph, ordinary pub/echo, ordinary Action CLI | UNVERIFIED: the next frozen cases have not executed in this stage |
| Application service, QoS matrix, large payloads, concurrency stress, reconnect | NOT VERIFIED BY THIS STAGE; prior dated native/Host evidence keeps its own scope |
| rosbag record/play, rosbag2_py, remote replay | UNVERIFIED; bag builder/package excluded |
| Dedicated SEDP wire oracle; SDK global quiescence | NOT QUALIFIED / NOT PROVEN; node discovery and local close are insufficient |
| Full ROS 2 parity | NOT VERIFIED |

The next planned sequence is topic graph AB/BA, topic pub AB/BA, topic echo
AB/BA, then ordinary Action send-goal AB/BA, each with its own fresh bounded
window. R22 five-file Host QoS work remains STOPPED and excluded. The prior
[native C++ Action stage](ROS2_NATIVE_ACTION_ACCEPTANCE.md) was separately
published as `4794ffb08d2d1cf49e758a0a4627f4aa2c87de92` to existing
`origin/codex/flow-encoding-published-20261014`; its 08:31:44 UTC CI snapshot was
NOT_RUN (zero runs/checks/status contexts). That publication does not imply
this Python/CLI stage is already committed or pushed.
