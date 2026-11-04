# R25 stock ordinary Action CLI on RK3588A

> **R31 current-stack follow-up (2026-10-18T22:06:34.620832+00:00):** [Current natural Action AB/BA acceptance](ROS2_NATURAL_ACTION_ACCEPTANCE.md) now qualifies the unchanged R30 3,236-file package/`9db2a35c` bridge in two finite windows. The 2,811-file ordinary package/`6378aeae` bridge and prior runs below remain historical comparison evidence. Both runs have five order=6 feedbacks and success without business permission controls; physical A remains explicitly bounded/outgoing-first, B ordinary, with lifecycle infrastructure. Selected-rclpy compatibility stays OPEN; no natural-restart/default-reconnect/old-goal qualification follows. R30 coordinated replacement is published as `a8945d4520af23f476ac93f9c376668b4d397481` on the existing branch (2026-10-18 19:36:28 UTC exact-SHA CI NOT_RUN). At this 2026-10-18T22:06:34.620832+00:00 pre-commit documentation snapshot, the R31 finite technical tester gate is PASS; Main graph/PRE_COMMIT/commit/push/CI are pending. Actual publication results are recorded separately in root-relative `build/r31-action-natural/main-current-action-publication/POST_PUBLISH.json`; no future SHA or CI success is prefilled.

At 2026-10-17 23:34 UTC, independent final testing supports **bounded ordinary
Action CLI AB/BA PASS**: one stock `ros2 action send_goal` Fibonacci order=6
request per direction, with accepted UUID, five ordered feedback prefixes,
exact result and SUCCEEDED on the real RK3588A pair. This documentation-only
stage reuses unchanged published implementation and package bytes. Main graph,
PRE_COMMIT and publication gates remain pending at this snapshot; full ROS 2
feature parity is NOT VERIFIED.

## Environment and provenance

| Item | Actual qualified value |
| --- | --- |
| Checkout | `ROOT/build/r15-lifecycle/publish-worktree` (W3), HEAD `cb9c54dc6564362b52dc652e55ae9f7b5559749e` |
| Host | Windows; `C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B`; HDC `C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe` |
| Board A / B | RK3588A `3e01ff55454d202020104033bf453b00` / `3e01ff55454d202020104433991c3b00` |
| Native runtime | SDK23 OHOS AArch64, CPython 3.12.7, project Fast DDS 2.14.5 / Fast CDR 2.2.5 / project RMW |
| Package | `W3/build/r25-native-actionbag/runtime-qualified-cli-types-modules/package`: 2,811 members, 2,809 payload, 167 ELF, 19 RECORD files, 89 canonical external providers |
| Runtime manifest | Output-root `RUNTIME_PACKAGE.json` SHA256 `c46747034682072be50e8a7ad4ca6bd4129e8846dafd7b7cc4305ff3309bc299` |
| Admission | Existing normal/DATASYNC-only identity `mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16`; token-first launcher and original-token restoration |
| Transport evidence | Real DSoftBus public Socket path, fake SDK absent, builtin Transport disabled; selected project/native providers checked by actual maps and hashes |
| Deployment | Per-run `/data/local/tmp/mdds_dds_<run>_A` and `_B`, with exact package/control paths below |

`ROOT` is `C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`; `BASE`
is `ROOT/build/r25-native-actionbag`. The CLI uses its stock command, while the
opposite process runs the already published public-rclpy `action-server`
fixture in `actionbag_runtime_probe.py`. Each process has one Node/context.
No source, native ABI/provider, DDS/RTPS, RMW or discovery change was made for
this stage. No identity provisioning, permission/network change, Session API
or builtin transport fallback supplied these results.

All package bytes were rehashed. Existing native build and post-review Host
evidence were reused because the four published Host files and target fixture
are unchanged. New C++ build, Python compile and product-suite runs are N/A to
this documentation-only stage; they are not reported as newly executed PASS.
Prior Host tests (targeted 2, ordinary 14, native-runner Host control 1, peer 7,
raw replay) remain Host evidence, separate from the two actual device windows.
See [node-discovery build provenance](ROS2_ORDINARY_CLI_NODE_DISCOVERY_ACCEPTANCE.md)
and [echo repair](ROS2_ORDINARY_CLI_TOPIC_ACCEPTANCE.md).

## Actual execution commands

The Windows host launched `run_actionbag_cli.py run` twice, in ROOT, with the
following exact argument forms. `BASE` and `W3` expand to the paths above.

```powershell
& C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B "$W3/tests/ros2_target/run_actionbag_cli.py" run --prepared "$BASE/ordinary-action-ab-window/prepared/PLAN.json" --output "$BASE/ordinary-action-ab-window/execution" --hdc C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe
& C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B "$W3/tests/ros2_target/run_actionbag_cli.py" run --prepared "$BASE/ordinary-action-ba-window/prepared/PLAN.json" --output "$BASE/ordinary-action-ba-window/execution" --hdc C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe
```

These are completed-run records, not replay instructions. Both peer bindings
have expired and both windows are closed. Future execution needs new reviewed
plans, fresh reciprocal peer evidence and new output/deployment paths; sealed
runs must not be overwritten or reused as a live release. Original 35-second
business, 50-second close and 300-second freshness budgets were not expanded.

The actual target argv below are rendered with shell quoting for readability.
`run-token-first` execs the selected `actionbag_python` executable; the original
argv and expected exec argv are both recorded in `EXACT_EXECUTION_COMMANDS.json`.

### AB target commands

Board A:

```sh
/data/local/tmp/mdds_dds_b1faf61dce12a76c_A/package/run-token-first ros2 action send_goal /fibonacci_b1faf61dce12a76c example_interfaces/action/Fibonacci '{order: 6}' --feedback --timeout 20
```

Board B:

```sh
/data/local/tmp/mdds_dds_b1faf61dce12a76c_B/package/run-token-first --script /data/local/tmp/mdds_dds_b1faf61dce12a76c_B/package/scripts/actionbag_runtime_probe.py --role action-server --run b1faf61dce12a76c --side B --finish-control /data/local/tmp/mdds_dds_b1faf61dce12a76c_B/control.PROBE_FINISH
```


### BA target commands

Board A:

```sh
/data/local/tmp/mdds_dds_264eae7d7766297b_A/package/run-token-first --script /data/local/tmp/mdds_dds_264eae7d7766297b_A/package/scripts/actionbag_runtime_probe.py --role action-server --run 264eae7d7766297b --side A --finish-control /data/local/tmp/mdds_dds_264eae7d7766297b_A/control.PROBE_FINISH
```

Board B:

```sh
/data/local/tmp/mdds_dds_264eae7d7766297b_B/package/run-token-first ros2 action send_goal /fibonacci_264eae7d7766297b example_interfaces/action/Fibonacci '{order: 6}' --feedback --timeout 20
```

## Observed business and lifecycle

| Window | Direction / host session | Actual result |
| --- | --- | --- |
| AB `b1faf61dce12a76c` | A stock CLI → B public-rclpy server; session 55102 | UUID `9e549246781743e6bdb2ade7e9460739`, accepted order=6, SUCCEEDED; host exit 0 |
| BA `264eae7d7766297b` | B stock CLI → A public-rclpy server; session 92988 | UUID `6241d84c27bc4762a7f51ac8fd65c8ed`, accepted order=6, SUCCEEDED; host exit 0 |

Both CLI outputs contain these five ordered feedback sequences:
`[0,1,1]`, `[0,1,1,2]`, `[0,1,1,2,3]`, `[0,1,1,2,3,5]`,
`[0,1,1,2,3,5,8]`. Final result is exactly `[0,1,1,2,3,5,8]`.
The pre-close oracle correlates the nonzero goal UUID and paired server records,
not merely command exit 0. This fixture's order/result contract is distinct
from the older bespoke native C++ fixture; its historical result is not reused.

AB recorded 225 HDC commands / 209 remote receipts, BA 229 / 213. Every
recorded command/remote exit passed the corresponding audit. Both windows
completed normal public Participant deletion, native RuntimeClose and Python
finalization with return 0, restored the original token (1), and observed child
exit 0. All four known owned PIDs were absent per window; no forced signals.
Process-local close/join and exit do not establish SDK-wide callback drain,
epoch/quiescence or repeated Participant lifetimes in one process.

Native-path/SPDP observations and exact loaded providers support real transport
use. They do not directly map an Action UUID to RTPS sequence numbers or supply
a dedicated SEDP wire oracle. The known optional unique-flow Reader failure
and existing original-QoS retry remain in the logs; unique flows were not
provided and strict mode was not tested. Startup was not error-free.

## Evidence and retained errors

Paths below are relative to BASE. These ignored local evidence files and
prepared inputs are not uploaded merely by referencing them. Reproduction
requires their provenance and the exact target environment; a clean clone
alone is insufficient and missing inputs must not be replaced by unrelated
ROS or Python providers.

| Record | SHA256 |
| --- | --- |
| `ordinary-action-cli-stage-final-test/RESULT.json` (Final scoped execution gate PASS) | `bf389683490fde1708585dc3f15caa1312205710b8050d4ab26d6d0561dd0378` |
| `ordinary-action-cli-stage-final-test/SEALED_MANIFEST.json` (50-member final seal) | `34bc9a6b57c11dcaa28ea4ada1ee4ca9e56458ecec73d59c168b17243f46c23a` |
| `ordinary-action-ab-window/FINAL_TESTER_RESULT.json` (AB PASS) | `a4623f9913b17d146e9acf49b161cc2a28dce7b36386bb84db2539b38964121e` |
| `ordinary-action-ab-window/FINAL_TESTER_SEAL.json` (1,026-member AB seal) | `54828a53ebfc107372abcce3ce94ce2a2a7aec709838bc5f31c2f815af1d1092` |
| `ordinary-action-ba-window/FINAL_TESTER_RESULT.json` (BA PASS) | `a156f998d90676e24898dc9b860a8a3ba7f6a386a2e9617ef98909d1f79da522` |
| `ordinary-action-ba-window/FINAL_TESTER_SEAL.json` (1,054-member BA seal) | `ebc1b76077b0e802364762628097bc6931b09f60fbd8f1cdd75a50b445a4e555` |

The final tester reverified ten prior seals containing 4,076 members and the
current source/package protection. `EXACT_EXECUTION_COMMANDS.json`,
`DEVICE_ABBA_AUDIT.json`, `SOURCE_AND_BUILD_REUSE.json`, `CAPABILITY_MATRIX.json`
and `SEAL_AUDIT.json` preserve command, source, build and capability boundaries.
Each actual window includes `ACTUAL_RUN_HOST_START.json`,
`ACTUAL_RUN_HOST_EXIT.json`, `execution/A.raw.stdout.log`, `B.raw.stdout.log`,
raw exit logs, `ACTION_BUSINESS_AUDIT.json` and `PRE_CLOSE_BUSINESS_AUDIT.json`.
The final audit replays the original raw pre-close/final observations through
the unchanged oracle; it does not reexecute either device case.

BA's first local pre-dispatch wrapper used the wrong `run_id` key and failed
before Popen. The corrected wrapper launched the device case once. A subsequent
local raw audit copied AB's 225/209 counts and failed against BA's actual
229/213. A new inventory-derived audit verified that the difference is exactly
two log and two exit observations; control releases, launches and the original
deadlines are unchanged. The failed audit and stderr remain preserved, not
rewritten. AB also retains a preparation-report count erratum: 28 existing
protected files plus one newly bound exclusion equals 29, not 33; the machine
baseline and original seal never changed. `PRESERVED_FAILURES.json` references
these dispositions. Earlier failed/blocked runs are not upgraded by later PASS.

## Capability boundary

| Capability | Current status / remaining gap |
| --- | --- |
| Stock Action CLI successful goal, feedback, result AB/BA | SUPPORTED for one fixed order=6 goal per direction; DEVICE_TESTED PASS |
| Native C++ Action cancel/reject/abort/concurrent goals | Prior separate bounded evidence remains applicable only to that fixture |
| Ordinary CLI cancel/reject/abort/preemption/concurrent goals | UNVERIFIED; no applicable ordinary CLI case executed here |
| General services / Action status-topic behavior | Not separately qualified; successful Action is not universal service acceptance |
| QoS breadth, large messages, reconnect, stress | UNVERIFIED by this small fixed-payload stage |
| rosbag record/play / remote replay | Excluded / UNVERIFIED; bag builder not included |
| SDK global quiescence / strict unique flow | NOT PROVEN / NOT TESTED |
| Dedicated SEDP / direct Action UUID-to-RTPS mapping | NOT QUALIFIED / NOT DIRECTLY MAPPED |
| Full ROS 2 parity | NOT VERIFIED |

R22 five-file work remains STOPPED; it and the bag builder are excluded.
Last confirmed publication is the echo repair
`cb9c54dc6564362b52dc652e55ae9f7b5559749e` on existing
`origin/codex/flow-encoding-published-20261014`, confirmed by
`main-echo-fix2-publication/POST_PUBLISH.json`. Its 22:49:05 UTC CI snapshot was
NOT_RUN (zero runs/checks/status contexts), not PASS. This Action documentation
still needs Main's complete graph/PRE_COMMIT and publication gates.
