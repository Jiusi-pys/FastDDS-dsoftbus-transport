# R35 bounded serialized bag record and playback acceptance

Evidence snapshot 2026-10-19T22:45:38.910915+00:00; source-history baseline `f38f184de9fa8da099fa0d79ffccf00ffe8a5dfe`. Scope `R35_PHASED_STOCK_BAG` is a separate staged two-process record/closed-bag/play experiment. **Implementation PRESENT; DEVICE_TESTED / CROSS_DEVICE_TESTED / INTEGRATION_TESTED PASS for the three fixed cases below. General ROS2 NOT_VERIFIED.** [Capability matrix](ROS2_CAPABILITY_MATRIX.md), [R34 diagnostic observation](ROS2_SERIALIZED_IMPACT_ACCEPTANCE.md) and [historical ten-String bag acceptance](ROS2_ROSBAG_RECORDPLAY_ACCEPTANCE.md) retain distinct scopes.

## Actual finite result

| Case / direction | Original run / execution nonce | Accepted observation | Original exit / status |
| --- | --- | --- | --- |
| RECORD A publisher to B recorder | 995d57d79617477a / 039d7e732e7b8e05 | Four topics/two definitions/eight CDR rows; SQLite schema4/metadata9; immutable closed bag with one db3 and metadata.yaml, no WAL/SHM. Independent closed-bag audit PASS. | Host0, two children0, PASS |
| DEFAULT B player to A typed/raw reader | f1037096c37e5b3b / b418b18692e0d7bd | Same immutable bag;16 callbacks/eight full CDR matches; no disable-loan flag; independent default-play audit PASS. | Host0, two children0, PASS |
| NO_LOAN B player to A typed/raw reader | 128ed56c0766fce3 / 8013dbfb9dc16f08 | Same immutable bag;16 callbacks/eight full CDR matches; exactly one --disable-loan-message in actual B argv. | Host0, two children0, PASS |

There are four dedicated lanes: typed_event, serialized_event, typed_string and serialized_string, each with indexes1 and2. The fixed rcl_interfaces/msg/ParameterEvent fixture is a nested message; this is not another parameter-service/mutation-event qualification. Requested QoS is RELIABLE/VOLATILE, KEEP_LAST depth10. Source publication records, retained type definitions/type-support bindings, offered QoS/type hashes, SQLite rows and metadata are checked together. Topic-local order is checked; no cross-topic total order or shared device clock is inferred.

| Stored/replayed fixture | Complete bytes / schema consumed / suffix | Fidelity boundary |
| --- | --- | --- |
| Typed ParameterEvent | 140 /140 /0 in the recorded rows | Typed publication and local diagnostic serialization are distinct operations. |
| Serialized ParameterEvent | 148 /140 /8; three nonzero suffix bytes per sample | All original bytes and suffix hashes remain; storage and playback match without trimming, normalization or padding assumptions. |
| Typed/serialized String | 40 /40 /0 | Four controls remain complete and suffix-free. |

Each playback starts paused with all four typed/raw lane counts[0,0] and empty slots. One real Resume service request completes with an empty response fields object{} and strict boolean done=true/cancelled=false/raised=false. Final counts are[2,2] per lane: typed8/raw8/native-full-buffer-decode8,16 callbacks and eight complete stored-buffer comparisons. The final quiet observation is0.25s. Duplicates/extras reject within the full retained observation window; this finite interval does not prove indefinite absence. `effective_can_loan_messages` remains UNOBSERVABLE_WITH_STOCK_CLI in both modes; PASS in two modes cannot reveal recorder-internal take or an effective loan path.

## Environment, build and lifecycle

The registered physical A/B RK3588A OpenHarmony pair uses SDK23/AArch64, project-linked CPython3.12.7, ROS2 Jazzy/rmw_fastrtps_cpp, project FastDDS2.14 and DSoftBus Socket providers. Each phase has two Participants/one peer. A retains the previously qualified explicit outgoing-first backend and B the ordinary backend. No third observer, default reconnect enablement, RMW/native serialization fix or Transport topology change was introduced. Deployment roots are `/data/local/tmp/mdds_dds_<RUN>_A` and `_B`; actual IDs and original supervisor-enveloped SDK argv remain private. A/B below are placeholders and do not identify physical devices publicly.

Build artifact: root-relative `build/r35-serialized-bag/package-a6a967d2c4870802/package`,3240 files/220 AArch64 ELF. DERIVATION.json SHA256 `e2bca52596e04c31e999ac7c42a3f031ce0ed0ba2bd38f111cc2d0eb2f3f5022`. Qualification is **REUSED_HASH_VERIFIED_NOT_REBUILT**. Final review/current Main admissions bind the unchanged package and33 runtime pins. No native compilation or repeated whole-package/compiler897 member audit occurred during final testing/docs sync.

Normal cleanup is PASS per case: two child exits0, four owned child/supervisor PIDs absent, identity restored1 on each board, forced signals0. RECORD uses one guarded normal TERM to close the recorder before collection. DEFAULT and NO_LOAN players exit naturally, with no normal TERM. The record's original precollection CLOSE_UNPROVEN field remains in its original receipt; independent final close admission supplies that record's closure facts. These observations do not prove SDK-global drain or epochs. Native/provider evidence binds the selected real custom-only DSoftBus Socket path (`fake_sdk=0`); per-ROS-payload identity in RTPS/wire bytes is NOT_DIRECTLY_MAPPED.

| Existing timing bound | Original actual run-qualified CONSUMED age from earliest query dispatch |
| --- | --- |
| Dispatch240s, RECORD |99.687160s, PASS |
| Dispatch240s, DEFAULT |97.903390s, PASS |
| Dispatch240s, NO_LOAN |102.450479s, PASS |

The run-qualified CONSUMED receipt, not an earlier plan/reviewer readiness time, supplies these ages. All original16 control budgets remain unchanged: query whole600s, pair300s, RPC30s, query freshness300s, dispatch240s, listener60s, business35s, probe40s, close release50s, exit20s, analysis60s, postquery outer120s/child60s, continuation wrapper140s, review child60s/ledger75s. Standby300s remains unchanged. Business SDK commands are301/320/308 for RECORD/DEFAULT/NO_LOAN, each with its own separate40-command query. Counts are operational receipts, not performance metrics; the query/business/control executions are not combined into a benchmark.

## Test and review evidence

| Stage | Actual qualification / reuse boundary |
| --- | --- |
| Public four-file implementation | PRESENT with unchanged exact hashes listed below; no DDS/RTPS/Writer/Reader/discovery/QoS semantic or Transport-architecture change. |
| Existing public host qualification | PASS308 =63 serialized-bag topcases +229 regression topcases across13 modules +9 qualification +6 private ops +1 historical real-FAIL negative. REUSED_NOT_RERUN with current matching hashes. |
| Existing C4 controller qualification | PASS317 =58 original cases +39 owner-journal +220 falsy-failure cases. Implementer success9, independent success9/injections170/foreign-SID1/value-JSON8/journal-JSON8, retained faults and repeated detector subsets are separate evidence; none is added to317. |
| Publisher/caller and no-loan final coverage | Publisher42/caller9 and actual post-source-review final16 are REUSED_NOT_RERUN. The same no-loan16 is not32. |
| Final agentic-review | PASS, zero findings/zero source fixes;283 assertions/4867 unique pins. Exact RESULT/DELIVERY/SEAL below. |
| Final independent technical tester | Commit gate: PASS for finite R35 technical test scope;452 current paths checked, public290/private5/controller9 plus33 runtime pins and source/user protection. Existing device cases and host suites reused; no new board run. |
| Newly executed after final review | Two readonly unmocked H.checked_qualification checks: real PUBLIC accepted; real CONTROLLER rejected as PUBLIC. Exit0; no checked_plan/prepare/controller/Bridge/SDK/device execution. |
| Documentation sync | Seven owned documents synchronized to these facts; no new device, SDK or test execution. Main separately owns graph/stage/identity/target-remote/FF/precommit and Git publication records. |

The final tester's minimal command actually executed on the Windows host from the repository root was:

```powershell
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:GIT_OPTIONAL_LOCKS = '0'
& 'C:/pixi_ws/.pixi/envs/default/python.exe' -X utf8 -B `
  'build/r35-serialized-bag/final-technical-gate-c4-d01/host_public_admission_boundary.py'
```

Original UTC22:39:41.292240 to22:39:41.632318 on2026-10-19; timeout60s/exit0, stdout636 bytes/stderr0. It inherits the normal user environment with the two recorded overrides. These are two fresh checks, not a fresh308/317 rerun. Native I/O mocked in host fixtures remains host evidence; real device execution is provided by the three original cases, never inferred from host tests.

## Recorded commands and reuse

The following PowerShell template documents the public host entry point and required arguments. It is **NOT_EXECUTED by docs_updater and has no new PASS**. `<NONCE>`, `<FRESH_MAIN_RELEASE>` and A/B IDs are placeholders; archived releases/CONSUMED runs are already consumed and are historical evidence, not new execution authorization. Original Main/tester journals retain the exact actual host argv/environment/exits. Paths resolve from the repository root shown in `$R`.

```powershell
$R = 'C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport'
$W3 = "$R/build/r15-lifecycle/publish-worktree"
$E = "$R/build/r35-serialized-bag"
$Py = 'C:/pixi_ws/.pixi/envs/default/python.exe'
$Hdc = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe'
$Nonce = '<NONCE>'
& $Py -X utf8 -B "$W3/tests/ros2_target/run_serialized_bag.py" run `
  --prepared "$E/prepared-$Nonce/PLAN.json" `
  --release '<FRESH_MAIN_RELEASE>' --hdc $Hdc --output "$E/execution-$Nonce"
```

For source-history reproduction, the actual recorded A/B operation argument vectors follow, rendered with shell quoting from each original A.dispatch.json/B.dispatch.json. They are historical target command templates beneath the original token-first/supervisor ownership and HDC envelope, not standalone launches or new execution receipts. All original business host commands exited0. Public documents include neither physical target IDs nor raw CDR/base64 payloads.

### RECORD 995d57d79617477a

A operation (original child exit0):

```sh
/data/local/tmp/mdds_dds_995d57d79617477a_A/package/run-token-first --script /data/local/tmp/mdds_dds_995d57d79617477a_A/package/scripts/serialized_bag_probe.py --case record_A_to_B --run 995d57d79617477a --record-run 995d57d79617477a --side A --role bag-publisher --finish-control /data/local/tmp/mdds_dds_995d57d79617477a_A/control.PROBE_FINISH --contract /data/local/tmp/mdds_dds_995d57d79617477a_A/r35-contract.json --contract-sha256 3288f809ab2059999df60ccfac79bd6c06476fa619d2b6e9868dffe0d664e290
```

B operation (original child exit0):

```sh
/data/local/tmp/mdds_dds_995d57d79617477a_B/package/run-token-first ros2 bag record --output /data/local/tmp/mdds_dds_995d57d79617477a_B/bag --storage sqlite3 --storage-preset-profile none --compression-mode none --max-cache-size 0 --disable-keyboard-controls --node-name r35bagrec_995d57d79617477a --qos-profile-overrides-path /data/local/tmp/mdds_dds_995d57d79617477a_B/qos.yaml --topics /r35_995d57d79617477a/typed_event /r35_995d57d79617477a/serialized_event /r35_995d57d79617477a/typed_string /r35_995d57d79617477a/serialized_string
```

### DEFAULT f1037096c37e5b3b

A operation (original child exit0):

```sh
/data/local/tmp/mdds_dds_f1037096c37e5b3b_A/package/run-token-first --script /data/local/tmp/mdds_dds_f1037096c37e5b3b_A/package/scripts/serialized_bag_probe.py --case play_default_B_to_A --run f1037096c37e5b3b --record-run 995d57d79617477a --side A --role bag-subscriber --finish-control /data/local/tmp/mdds_dds_f1037096c37e5b3b_A/control.PROBE_FINISH --contract /data/local/tmp/mdds_dds_f1037096c37e5b3b_A/r35-contract.json --contract-sha256 3d8f13aa7bd5ec56385efda3a6c9b9eab6f6f7e6cd9670b877c0293024a7cd35 --resume-control /data/local/tmp/mdds_dds_f1037096c37e5b3b_A/control.R35_RESUME --bag-seal-sha256 bcf9a25ce2e4359abd92ea98aa05c10bab668e379f89bcd86c88ea1cc786d0f2
```

B operation (original child exit0):

```sh
/data/local/tmp/mdds_dds_f1037096c37e5b3b_B/package/run-token-first ros2 bag play --input /data/local/tmp/mdds_dds_f1037096c37e5b3b_B/input/bag sqlite3 --topics /r35_995d57d79617477a/typed_event /r35_995d57d79617477a/serialized_event /r35_995d57d79617477a/typed_string /r35_995d57d79617477a/serialized_string --remap /r35_995d57d79617477a/typed_event:=/r35play_f1037096c37e5b3b/typed_event /r35_995d57d79617477a/serialized_event:=/r35play_f1037096c37e5b3b/serialized_event /r35_995d57d79617477a/typed_string:=/r35play_f1037096c37e5b3b/typed_string /r35_995d57d79617477a/serialized_string:=/r35play_f1037096c37e5b3b/serialized_string __node:=r35bagplayer_f1037096c37e5b3b --start-paused --disable-keyboard-controls --wait-for-all-acked 5000 --qos-profile-overrides-path /data/local/tmp/mdds_dds_f1037096c37e5b3b_B/qos.yaml
```

### NO_LOAN 128ed56c0766fce3

A operation (original child exit0):

```sh
/data/local/tmp/mdds_dds_128ed56c0766fce3_A/package/run-token-first --script /data/local/tmp/mdds_dds_128ed56c0766fce3_A/package/scripts/serialized_bag_probe.py --case play_no_loan_B_to_A --run 128ed56c0766fce3 --record-run 995d57d79617477a --side A --role bag-subscriber --finish-control /data/local/tmp/mdds_dds_128ed56c0766fce3_A/control.PROBE_FINISH --contract /data/local/tmp/mdds_dds_128ed56c0766fce3_A/r35-contract.json --contract-sha256 0b8dc7a8a905ebe56727616149a836fcb6ed535aad7f637f1d3dafc97229dc45 --resume-control /data/local/tmp/mdds_dds_128ed56c0766fce3_A/control.R35_RESUME --bag-seal-sha256 bcf9a25ce2e4359abd92ea98aa05c10bab668e379f89bcd86c88ea1cc786d0f2
```

B operation (original child exit0):

```sh
/data/local/tmp/mdds_dds_128ed56c0766fce3_B/package/run-token-first ros2 bag play --input /data/local/tmp/mdds_dds_128ed56c0766fce3_B/input/bag sqlite3 --topics /r35_995d57d79617477a/typed_event /r35_995d57d79617477a/serialized_event /r35_995d57d79617477a/typed_string /r35_995d57d79617477a/serialized_string --remap /r35_995d57d79617477a/typed_event:=/r35play_128ed56c0766fce3/typed_event /r35_995d57d79617477a/serialized_event:=/r35play_128ed56c0766fce3/serialized_event /r35_995d57d79617477a/typed_string:=/r35play_128ed56c0766fce3/typed_string /r35_995d57d79617477a/serialized_string:=/r35play_128ed56c0766fce3/serialized_string __node:=r35bagplayer_128ed56c0766fce3 --start-paused --disable-keyboard-controls --wait-for-all-acked 5000 --qos-profile-overrides-path /data/local/tmp/mdds_dds_128ed56c0766fce3_B/qos.yaml --disable-loan-message
```

## Immutable evidence navigation

All following paths are root-relative under `build/r35-serialized-bag/`; exact run/results are independently pinned in final technical RESULT and review RESULT. Original logs contain full bytes privately; this public record reports only lengths, counts and hashes.

| Evidence | Original artifact / SHA256 |
| --- | --- |
| Final technical tester | `final-technical-gate-c4-d01/RESULT.json`: b4b61e191bd7b7d3b482687695594fd24c519b7d8e252bb332e0787c60bfcb81 |
| Final tester delivery | `final-technical-gate-c4-d01/DELIVERY.json`: c69e4362e2530d72d2dc67b295e605d75b82bd8043d2afc7363e0e438948cc1e |
| Final tester report / seal | `REPORT.md`:35e662a55d673c9754ec9f895ef081c375ff904003322a3ed3a293e268d44a61; `SEAL.json`:a7370ce7e7953857eedda219c8b85c03bd73d4e4fd3de372407b0f4f867af511 (108 members). |
| Final independent review | `final-stage-review-c4-d01/RESULT.json`:070d8e682a1ed3e51642039b8564b43cb3165ffa3b98ec24b0b697b75b6330ad |
| Review delivery / seal | `DELIVERY.json`:bdf7a51b1067571f14a09e3a36b629666886650e5309a547c69b2fa63f38824a; `SEAL.json`:1ade74f816b199f88b551042abd9038aeb652467282affdd9bd86da01ff218b5 (11 members). |
| Record original RESULT | `execution-039d7e732e7b8e05/RESULT.json`:bc7c5906f3eaa01505fda60c90ce28db02afcc718445c615f23e59fe049a8a00 |
| Default original RESULT | `execution-b418b18692e0d7bd/RESULT.json`:64568cb6d7297296fc1391d9fbfd5cd59b3dabacda3a6dbac4c1de38a89c156c |
| No-loan original RESULT | `execution-8013dbfb9dc16f08/RESULT.json`:1c0dfc1c88556e2b48f134d3bc2053ddba66ca75c11d11c32b0f25a593681919 |
| Record canonical admission | `record-execution-c4-d01/`; `MAIN_C4_RECORD_EXECUTION_ADMISSION_D01.json`:e7a0a4026f18162fc43291bb25b286c9850f87ad702675434d75c2ce6e546ffc;5526 admitted members. |
| Default canonical admission | `default-play-execution-c4-d02/`; `MAIN_C4_DEFAULT_EXECUTION_ADMISSION_D02.json`:739b9b51a91cdbaa8e410a99e41574e882df3600ecd5bda981d2cfa151277aae;2622 admitted members = tester2531 + reviewer91. |
| No-loan canonical admission | `noloan-play-execution-c4-d01/`; `MAIN_C4_NOLOAN_EXECUTION_ADMISSION_D01.json`:d28bdf36c8d56fa9d83213438979be7b159613d2d4d9282c5efdf0c9659cb0f8;2707 admitted members = tester2616 + reviewer91. |
| Independent audits | `closed-bag-c4-record-d01/DELIVERY.json` and `default-play-audit-c4-d02/DELIVERY.json`; exact original pairs reused unchanged through current Main admissions. |
| Original host qualification | `combined-final-c4-d01/PUBLIC_RESULT.json`:0836e318392eebf1bb4b78e04edfff96028f01235f4d0d1776c2d42cb8e8cfbd; `CONTROLLER_RESULT.json`:bee66567d972e4a07fa24ffa38ed44f198097adb89296de3e764d10d5bc364b2. |

| Current source (relative to this worktree) | SHA256 |
| --- | --- |
| integrations/ros2/target/serialized_bag_probe.py | a120aaa1b0d75f10bb202f094deef9c1c60d49a96e884365fd382bb21c57fad5 |
| tests/ros2_target/run_serialized_bag.py | b051a493a1be10b622c6bde0176d5645b3101b046d4db37c9bac3222271f2ea1 |
| tests/ros2_target/serialized_bag_oracle.py | 55bbc97bf8bc00b8c24fb6d211e13deb5e0e0bddcc638038a21163bcf091290f |
| tests/ros2_target/test_serialized_bag.py | 78dc4e95594d495da507165f75d4c933905a52f651a7a34403ce5c72c0a1464e |

## Retained failures and claim limits

DEFAULT D01 execution nonce217a27a5d58ef672 remains **FAIL**: dispatch freshness240.291205s exceeded the unchanged240s bound before deployment. It has no CONSUMED receipt and is not one of the three PASS cases. Its original RESULT SHA256 is279a5e175664b572c4bd9f74dcdae63388967f3ced762061acaec4b8ee65d261. Prior actual C2 RECORD failure, both R33 V1 failures, R34 query1 FAIL and auxiliary overwritten-receipt PARTIAL/time UNKNOWN remain unchanged; later passing runs do not rewrite them. Exploratory metadata displays clipped during documentation inspection are excluded from complete evidence; exact input/source bytes and compact reads supply the record.

The original simultaneous publisher/observer/recorder goal remains BLOCKED for this two-Participant/one-peer bridge configuration. R35 qualifies the narrower separate-phase study and cannot observe recorder-internal take. Strong DDS serializedPayload/wire proof remains **BLOCKED_PROVENANCE_GAP**: the stripped rclpy extension (hash prefix997bb) lacks exact source/compiler/link/unstripped binding; the referenced e4c7 revision is not available as a local Git object and does not bind that installed extension. No ABI root cause is inferred. Native raw callback/storage matches do not establish canonical padding, universal standalone serialization compatibility or a safe RMW repair. Existing rosbag2_py executor delta prevents describing the whole binding as byte-stock; native RMW/DDS/RTPS remains selected without semantic changes.

General serialized compatibility remains OPEN_NOT_VERIFIED. Full ROS2 NOT_VERIFIED; native pub/sub/service/Action/parameter evidence remains bounded as in the central matrix. Broad QoS (transient-local/deadline/lifespan/liveliness/history), other types/storage backends/compression, large messages/concurrent workloads, long soak/leak freedom, arbitrary lifecycle/recovery and default automatic reconnect are not qualified by these cases. Default reconnect is NOT_PROVIDED by this accepted configuration, not declared permanently impossible. SDK epoch/global drain are NOT_PROVEN; direct ROS-message-to-RTPS mapping is NOT_ESTABLISHED. Strict unique-flow remains NOT_PROVIDED/UNTESTED; selected rclpy feedback/status ordering remains OPEN. R22/user lifecycle-QoS work stays STOPPED/excluded. DSoftBus Socket API only; no Session API fallback or upper-layer DDS/RTPS/Writer/Reader/discovery/QoS semantic change is part of this R35 scope.
