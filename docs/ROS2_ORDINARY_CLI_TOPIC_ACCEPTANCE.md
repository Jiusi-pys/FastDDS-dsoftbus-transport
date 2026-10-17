# R25 ordinary topic commands and echo readiness repair

> **Publication / successor (2026-10-17 23:34 UTC):** This four-file echo repair was published as `cb9c54dc6564362b52dc652e55ae9f7b5559749e` to existing `origin/codex/flow-encoding-published-20261014`; 22:49:05 UTC CI was NOT_RUN (zero runs/checks/status contexts). [Ordinary Action CLI AB/BA](ROS2_ORDINARY_CLI_ACTION_ACCEPTANCE.md) subsequently passed fixed positive order=6 goal/feedback/result on unchanged sources/package. That evidence documentation awaits publication; historical pre-commit and Action NOT_RUN statements below describe the earlier echo-stage snapshot.

At 2026-10-17 22:34 UTC, independent final testing reports **execution Commit
gate PASS for four Host Python files**. Fresh stock `ros2 topic echo` AB/BA
windows passed on the real RK3588A pair using the unchanged qualified native
DSoftBus Socket package. Main graph/PRE_COMMIT and publication of this repair
remain pending at this documentation snapshot. Full ROS 2 parity is NOT VERIFIED.

## Problem, change and validation scope

The Host runner reads board snapshots sequentially. A CLI snapshot can already
contain the exact received YAML while the preceding publisher snapshot still
contains only matched=0 records. Previously that intermediate observation
failed immediately. The repaired runner returns not-ready for this precise
live state and continues within the existing deadline. Incorrect/duplicate
YAML, wrong payloads, malformed match records and a final missing positive
match still fail. The original 35-second business, 50-second close and
300-second freshness budgets are unchanged.

Only `tests/ros2_target/run_actionbag_cli.py`, `test_actionbag_cli.py`,
`peer_evidence_view.py` and `test_peer_evidence_view.py` change. The converter
pins the new exact consumer hash; tests cover the readiness transition,
deadline and strict binding failures. The target fixture, native package,
C++ ABI, RMW, DDS/RTPS and DSoftBus API behavior are unchanged. Synthetic
negative metadata is used only by Host tests. Fresh real device metadata was
constructed and validated separately before either actual window.

| Environment / artifact | Evidence boundary |
| --- | --- |
| Checkout | `ROOT/build/r15-lifecycle/publish-worktree` (W3), baseline `471d344050dccae6cb10396d3432fc9f88a92ff7` |
| Host | Windows; `C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B` |
| Targets | RK3588A A `3e01ff55454d202020104033bf453b00`; B `3e01ff55454d202020104433991c3b00` |
| Native runtime | SDK23 OHOS AArch64, CPython 3.12.7, project Fast DDS 2.14.5 / Fast CDR 2.2.5 / project RMW; actual Socket path, fake_sdk=0, builtin=0 |
| Package | `W3/build/r25-native-actionbag/runtime-qualified-cli-types-modules/package`; 2,811 members, 2,809 payload, 167 ELF, 19 RECORD files; 89 canonical external providers |
| Runtime manifest | Output-root `RUNTIME_PACKAGE.json` SHA256 `c46747034682072be50e8a7ad4ca6bd4129e8846dafd7b7cc4305ff3309bc299` |
| Build | Python compile PASS for four files plus unchanged fixture; native package bytes rehashed and reused. New C++ build N/A because no native source, ABI or payload changed. |
| Identity / deployment | Existing normal/DATASYNC-only `mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16`; isolated `/data/local/tmp/mdds_dds_<run>_A` and `_B`; no identity/security/network changes |

`ROOT` denotes `C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`;
`BASE` below is `ROOT/build/r25-native-actionbag`. The earlier native build and
package provenance is in [node-discovery acceptance](ROS2_ORDINARY_CLI_NODE_DISCOVERY_ACCEPTANCE.md).
This repair did not rebuild C++. Host controls are not device transport tests.

## Executed commands and results

The post-review targeted Host command was:

```powershell
& C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B `
  "$W3/tests/ros2_target/test_actionbag_cli.py" `
  OrdinaryCliTests.test_echo_sequential_snapshots `
  OrdinaryCliTests.test_echo_waits_for_evidence_without_extending_deadline -v
```

It passed 2 tests. The full ordinary suite passed 14, native-runner Host control
1, peer-view suite 7 and exact failed-snapshot replay 1. All five command exits
were 0 and owned Host process trees empty. These are fresh post-review Host
executions; the later final-stage audit revalidated their receipts instead of
claiming another run. Exact argv and outputs are in
`ordinary-echo-readiness-fix2-final-test/commands/001-targeted` through
`005-replay` (`.json`, `.stdout`, `.stderr`). Independent review found no
remaining actionable findings in this four-file revision.

Each real echo window used this host-runner form (substitute `ab` or `ba`):

```powershell
& C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B `
  "$W3/tests/ros2_target/run_actionbag_cli.py" run `
  --prepared "$BASE/ordinary-echo-fix2-ab-window/prepared/PLAN.json" `
  --output "$BASE/ordinary-echo-fix2-ab-window/execution" `
  --hdc C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe
```

The actual target CLI command template was:

```sh
${P}/run-token-first ros2 topic echo /actionbag_${run} std_msgs/msg/String --no-daemon --once --timeout 20 --qos-reliability reliable --qos-durability volatile --qos-depth 10
```

The opposite board ran `actionbag_runtime_probe.py --role publisher` through
`run-token-first`, with the prepared run/side/finish-control arguments. There
was one Node/context per process. Acceptance required exactly
`data: payload_<run>` and a positive publisher match **before normal close**;
a timeout exit of 0 alone could not pass.

| Case / run | Actual data direction | Result |
| --- | --- | --- |
| echo AB `c67a4d427026c670`, host session 57123 | B fixture publisher → A stock CLI echo | Host exit 0; exact YAML and positive match before close; PASS |
| echo BA `a039923ab6958e31`, host session 15869 | A fixture publisher → B stock CLI echo | Host exit 0; exact YAML and positive match before close; PASS |

AB/BA name the CLI side, not always the data direction. In BA, raw receipt
sequence `0100 A matched=0 → 0102 B exact YAML → 0104 A matched=1` demonstrates
the repaired observation case before normal close. Both windows report
public delete=0, native RuntimeClose=0, Python finalize=0, original-token
restore=1, child exits 0 and all four owned PIDs absent with no forced signals.
SDK-wide callback drain/quiescence remains NOT PROVEN.

These command lines describe completed executions. Their fresh peer bindings
have expired and their windows are closed: do not replay or overwrite them.
A future run needs a fresh plan, peer query, reviewed real metadata, exact
provider/package bindings and new output paths. Finite deadlines are not
extended to rescue a failed run.

## Earlier topic graph and publication evidence

Four immutable pre-repair windows remain valid only for their original source
and run scope. They were rehashed, **not rerun with the current four-file patch**.
Topic listing used `ros2 topic list --no-daemon --spin-time 3 --show-types` and
required the exact topic/type line. Stock publication used the unique
`std_msgs/msg/String` payload with `--once --wait-matching-subscriptions 1
--max-wait-time-secs 20 --keep-alive 1`, reliable/volatile/depth10, and required
the opposite fixture's exact received data before close.

| Historical window | Run | Bounded result |
| --- | --- | --- |
| topic graph AB | `6df36f0c32ac9ecc` | A listed B's exact topic and type; host exit 0 |
| topic graph BA | `1b011b90d74c44b9` | B listed A's exact topic and type; host exit 0 |
| stock pub AB | `4410961dbfaf28f2` | A published exact String received by B; host exit 0 |
| stock pub BA | `15b560b881233471` | B published exact String received by A; host exit 0 |

The pub AB local preparation receipt originally named the graph BA helper and
hash incorrectly. The original outer tool stdout was not separately captured
to disk. Its addendum is explicitly a transcript-based discrepancy record,
not invented raw evidence; subsequent independent disposition permits limited
reuse of the independently verified actual execution chain. Original receipt,
seal and missing-outer-raw limitation remain intact. See
`ordinary-cli-modules-topic-pub-ab-preparation-receipt-addendum/` and the BA
window's `historical_AB_preparation_receipt_limit`.

## Canonical evidence and limits

All paths in this section are relative to BASE; local ignored evidence and
prepared inputs are not uploaded by these references. A clean clone alone is
insufficient to reproduce the exact target package or deployment. Missing
inputs must be recovered with provenance, not replaced by generic ROS binaries.

| Record | SHA256 |
| --- | --- |
| `ordinary-echo-fix2-stage-final-test/RESULT.json` | `ffbd7e91d8acbfdbfaaf43f6fdfa2b82c8ce54f2838f7a1edbc1a52a2bf2dde9` |
| Same directory `SEALED_MANIFEST.json` (71 members) | `05afacc0e1cc1f8c071e26d68b9351255687969965b1fa505c875fd31c28a003` |
| `ordinary-echo-fix2-ab-window/FINAL_TESTER_RESULT.json` | `5d343adf5fac1d06154f2d41e76373ac26e276522dd0dad5ee0916f93712243f` |
| AB `FINAL_TESTER_SEAL.json` (1,043 members) | `dbc342546e5e985432cbd4d25ac927f0009f9a3f52ff0b95445524276a94f3ff` |
| `ordinary-echo-fix2-ba-window/FINAL_TESTER_RESULT.json` | `dc24a4cfc3641fd92d7e815137dbbc4c1fe4c72ade2f5adbb47f73ba12b87bc8` |
| BA `FINAL_TESTER_SEAL.json` (1,025 members) | `c3391caf41d7ca01d47d440f68a3cb0cf2f25d8cd0603aa0dac4fb81a4d196bd` |

The final tester verified 15 seals / 9,341 files. Its `SOURCE_GATE_CHAINS.json`,
`HOST_REUSE.json`, `BUILD_REUSE.json`, `DEVICE_ABBA.json`,
`HISTORICAL_TOPIC_PUB.json` and `CAPABILITY_MATRIX.json` bind source/review,
Host, package and device evidence. Each actual window retains
`ACTUAL_RUN_HOST_START.json`, raw `execution/A.raw.stdout.log` / `B.raw.stdout.log`,
exit logs, pre-close audits and command receipts. BA additionally retains
`LIVE_SNAPSHOT_PROGRESS_AUDIT.json`.

The original failed BA run and its 838-member seal remain FAIL; the earlier
104-member initial result remains BLOCKED. New success does not upgrade either.
AB's local wrapper/report mistakes and two local final-stage audit exits of 1
are preserved with their corrected successful audits; they are not hidden
product executions. See `TESTER_FAILURE_DISPOSITION.json` for the raw-exit schema
and excluded-file baseline lookup corrections. The exact known optional
unique-flow Reader failure/retry is retained; unique flows NOT_PROVIDED and
strict mode NOT_TESTED. This is not an error-free startup claim.

| Capability | Status / unexecuted boundary |
| --- | --- |
| Fixed String echo AB/BA and process-local close | DEVICE_TESTED PASS for this exact source/package and bounded case |
| Topic graph/pub AB/BA | PARTIAL, original pre-repair real evidence only; no current-runner retest claim |
| Ordinary Action CLI AB/BA | NOT_RUN; next planned cases, separate from prior native C++ Action |
| rosbag record/play / remote replay | NOT_INCLUDED / UNVERIFIED; bag builder excluded |
| Broad QoS, reconnect, large messages, concurrent application stress | UNVERIFIED by this repair; fixed payload and profile do not generalize |
| SDK global quiescence / dedicated SEDP wire oracle | NOT PROVEN / NOT QUALIFIED |
| Full ROS 2 parity | NOT VERIFIED |

R22 five-file work remains STOPPED and excluded. Node-discovery publication
`471d344050dccae6cb10396d3432fc9f88a92ff7` on existing
`origin/codex/flow-encoding-published-20261014` is confirmed by
`main-cli-graph-publication/POST_PUBLISH.json`; its 20:30:40 UTC CI snapshot was
NOT_RUN with zero runs/checks/status contexts, not PASS. This four-file repair
still requires Main's complete pre-commit and publication gates.
