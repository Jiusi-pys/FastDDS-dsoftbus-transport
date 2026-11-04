# R38 durable capture and finite ROS 2 String endurance acceptance

Evidence snapshot: 2026-10-20T07:55:34.550015+00:00. **Current selected scope PASS; Full ROS2 NOT_VERIFIED.** Final independent tester D03 reports `VERIFIED_CURRENT_SCOPE=true`, `DEVICE_TESTED=true`, `INTEGRATION_TESTED=true` and `Commit gate: PASS` for its technical scope only. Main's final12 pre-commit gates, complete nonpartial/nontruncated all+staged graph checks and Git publication remain PENDING. CI: NOT_RUN_NO_NEW_COMMIT. Detached baseline `e27b7e9d1be7e72944108655a62f9753bfa99448` is historical R35, not a new R38 publication or remote-verification claim.

## Accepted scope and architectural boundary

New run `940f4e9c10a29830` executes ordinary public Node `std_msgs/msg/String` publish/subscribe echo on two real RK3588A OpenHarmony boards through the configured DSoftBus Socket backend. A sends one numbered String; B receives and echoes it; A receives before sending the next. Selected QoS is RELIABLE / VOLATILE / KEEP_LAST depth10,33-byte CDR, two Participants/one peer, one outstanding request, no application retry/deduplication. This is one finite roughly30-minute application echo case; service/Action/parameters/bag, restart, Participant recreation and hours soak are not newly qualified.

Fast DDS DDS/RTPS/Writer/Reader/discovery/QoS semantics and Transport abstraction are unchanged. No Session API or upper-layer workaround is introduced. Native observation aggregates only identical **complete ordered SPDP locator batches** in the original256 slots; direction/socket/writer/PID/serialized length/kind/address/port/DATA offset and batch order remain part of identity. Observation IDs and packet-sequence IDs are excluded from batch equality while coherent first/latest records and occurrence conservation are retained. A distinct batch must fit atomically; all seven fatal buckets remain fatal. Close evidence separates actual RuntimeClose from observation completion and checks output errors.

The added durable capture tooling is Windows x64 and bound to this stage's exact worktree/output namespaces. A finite worker leaves the launcher's permitted breakaway job and owns a new `KILL_ON_JOB_CLOSE` Job for its executor, runner and SDK descendants. Exact PID+creation, source13, Job membership, one-shot unconsumed permit and original route freshness are checked before SDK dispatch. Failed child assignment reaps only the exact just-created unassigned child; assigned descendants remain owned. State is a bounded append-only `STATE.jsonl`, read through the last complete newline; malformed/oversize data fails. It is telemetry, not the acceptance oracle. No assertion, expected original business result, return code, payload scope or original time bound was weakened.

## Actual results and fixed bounds

| Observation | Independent final result |
| --- | --- |
| Complete application roundtrips |1801;7204 complete33-byte CDR stage records; all expected content agrees |
| Loss / duplicate / order / foreign |0 /0 /0 /0 on complete original streams |
| Original oracle modes |`final=False` PASS and `final=True` PASS, independently rerun on the completed real logs |
| Payload spans |A1811.239383904s; B1811.244691655s |
| SPDP aggregation, each board |stored4 / observed4902 / repeated4898; distinct batches2 / repeated batches2449 |
| Native faults / close |All7 fatal counters0; RuntimeClose0; observation_complete1; normal native diagnostics PASS |
| Public / process close |public participant deletion0; Python interpreter/finalization0; A/B device children0 |
| Ownership / tokens |All4 owned device supervisor/child PIDs absent; original tokens restored; directories retained |
| Business SDK receipts |696 actual host receipts all0;680 applicable remote rc receipts all0;16 file transfers explain the count difference |
| Fresh query |Separate new query `673223b0caae7395`:40 SDK calls+4 local steps, actual query/continuation tool exits0; exact11-field review SHA5e54b5a6…; final query seal SHA b3ea3142… |
| Forced paths / timeout |forced-owned0 / forced-unassigned0 / executor-forced false / outer-timeout false; no capture error |
| Capture completeness |1880 complete state rows /747087bytes; business stdout31507bytes, stderr0; bounded streams complete |

SPDP observation counters measure discovery-locator bookkeeping. They are not7204 business CDR records, nor a DDS per-payload/native-wire causal proof. Resource/latency observations remain finite application observations in the unchanged original oracle; they establish neither transport benchmark performance nor global leak freedom.

Original limits remain:1801 roundtrips at most1Hz/minimum1800s payload span; listener60s/business1900s/probe1910s/absolute close1950s/exit20s; capture start10s/outer2350s/stream and journal caps4MiB. The observer's bounded acquisition/observation windows do not renew the original business deadline. R26's older200-roundtrip35s/40s/50s contract remains separate and unchanged.

## Host exit evidence and explicit auxiliary limit

Independent observer77040/creation134369536616389814 retained live exact worker53416/creation134369536617307579 and child73560/creation134369536618178593 process objects at startup (`STILL_ACTIVE=259`). At2026-10-20T07:39:05.618480+00:00 it observed `GetExitCodeProcess=0` for both retained objects and closed both handles without error. Original runner52900 and captured child Popen waits are0; original start CLI Popen wait0 and the external short-helper actual tool exit0 are separately recorded. A declared `TERMINAL.worker_exit=0` alone is not used as an OS-exit proof.

The observer's **own actual OS exit is UNOBSERVED_NONBUSINESS_AUXILIARY**. Its receipt proves its worker/child observations, not its own process exit. Its exact PID+creation is absent (Win32 error87); no owned business/auxiliary process remains active, stderr is empty, complete output is bounded, query handles are closed, and two finite post-termination snapshots have stable receipt/terminal/log hashes. Independent three-pass review explicitly accepted that equivalence only when all business/held-object exit and final absence/stability facts coexist. These facts are present in final D03; the document does not claim every host process had an observed OS exit0. No new long business wrapper was created. Old session24208 remains actualexit UNKNOWN and is not retroactively repaired by this new run.

## Build artifact, target and deployment

| Item | Actual recorded identity |
| --- | --- |
| SDK / target |OpenHarmony SDK23; AArch64 `aarch64-linux-ohos`; bridge built from23 TU/23 objects/1037 compiler+link inputs |
| Host tools |Windows x64; Python `C:\pixi_ws\.pixi\envs\default\python.exe`; stage-local bounded TMP/TEMP/TMPDIR |
| A |RK3588A OpenHarmony HDC `3e01ff55454d202020104033bf453b00`; `/data/local/tmp/mdds_dds_940f4e9c10a29830_A` |
| B |RK3588A OpenHarmony HDC `3e01ff55454d202020104433991c3b00`; `/data/local/tmp/mdds_dds_940f4e9c10a29830_B` |
| HDC executable |`C:\Users\17715\AppData\Local\OpenHarmony\Sdk\23\toolchains\hdc.exe`; SHA `99043a8267aec70e2aa78c056a8d2de59505e1c8bb96ced3c9ac6aad7c834b7c` |
| Native bridge |`build/r37-native-close/r38-native-build-d01/libfastdds_dsoftbus_ros2_target_bridge.so`; SHA `121a7b6dfcb0e6f9858a58aa97a37bbd225c4c73f905a88817bf6642e288511d` |
| Native binding |`build/r37-native-close/r38-native-build-d01/NATIVE_BINDING.json`; SHA-256 `115370fae6cafbe1f4eb83a7bd9d2815d175d189e70c6657d03b59961dedd585` |
| Package derivation |`build/r37-native-close/package-96960d580a051b56/DERIVATION.json`; SHA-256 `62947069c097f6ed71f7062e1c4a8687d84a14d68ff753f81738c6db60e545a3`;3237 members/220 ELFs,219 other ELFs unchanged,7 bridge C exports checked |
| Build qualification |Genuine initial D03 SDK build; configure/build/dependencies/readelf actual exit0. D07/final D03 revalidate inputs/objects/receipts, not a new SDK compilation or full Fast DDS rebuild |
| Selected stack |project-linked CPython3.12.7 / ROS2 Jazzy components / rmw_fastrtps_cpp / domain201; pinned existing token-first launcher and actionbag_python |

Device execution was controlled with the installed `hdc` skill, selected SDK executable and both fixed boards. No flash, port, network, permission or security configuration change is part of this acceptance. The documentation updater executes no SDK command, deployment, replay or business start.

## Recorded commands, exit codes and logs

The following are records of already consumed commands, not instructions or permission to replay. Aliases abbreviate absolute prefixes only: `R` is this repository root, `E=R/build/r38-durable-endurance-capture`, `W=E/publish-worktree`, `N=R/build/r37-native-close`, `PY=C:/pixi_ws/.pixi/envs/default/python.exe`, `HDC=C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe`. Exact complete argv/cwd/environment/timeouts/stdout/stderr hashes remain in the bound receipts below.

```text
cmake --build N/r38-native-build-d01 --parallel 2 --verbose
PY -X utf8 -B R/build/r25-native-actionbag/r38-durable-close-a0431926a6732fea-preparation/CAPTURE_QUERY.py --release-sha256 bfe3f7e5185d1e703ff402bbc1150a588b9c1fa7ca93e688e7b9ac77978981f9
PY -X utf8 -B R/build/r25-native-actionbag/r38-durable-close-a0431926a6732fea-preparation/continue_query.py --query-session 82167
PY -X utf8 -B W/tests/ros2_target/durable_endurance_capture.py start --spec E/CAPTURE_SPEC_D02.json
PY -X utf8 -B W/tests/ros2_target/run_endurance_roundtrip.py run --prepared N/prepared-b77d5e439d866d29/PLAN.json --release N/MAIN_BUSINESS_RELEASE_R38_D02.json --hdc HDC --output N/execution-6159c6e4fc38f244
PY -X utf8 -B E/final-tester-d03/device-d01/strict_acceptance_d03_v2.py
```

Native configure uses `-S W/integrations/ros2/target`, `-B N/r38-native-build-d01`, Ninja, SDK23 `ohos.toolchain.cmake`, `OHOS_ARCH=arm64-v8a`, `OHOS_STL=c++_shared`, Release and explicitly pinned dependency directories/libraries; `configure.json` preserves the full actual argv and environment. Build, `ninja -C ... -t deps` and SDK23 `llvm-readelf -h -d -n ...` each returned0. The final native binding does not imply a fresh D07 compilation.

Query cwd is R; original business runner cwd is **W**, as frozen `durable_endurance_capture.py` records and dispatches. Route age at the one-shot Main permit is92.825s (<240) and dispatch rechecks the same fixed freshness contract; no old permit was replayed. Query session82167 and continuation32006 actually exit0; Main externally observed short-helper tool exit0. Strict local acceptor Popen and outer tool exit0; finalizer session95099 actual exit0 (final tool chunk7a6602). The first private checker failed its newly authored wrong cwd=R guard; only that guard was corrected to source-defined W and the same immutable real logs were rechecked. This was not a business replay or a change to the original business/native/close/oracle/deadline contracts.

| Complete command/target/log receipt, root-relative | SHA-256 |
| --- | --- |
| `build/r37-native-close/r38-native-build-d01/configure.json` | `df297a964ff5e1d2f22a5b6bad884c0056fc4af2544266382dd93faa094cef1d` |
| `build/r37-native-close/r38-native-build-d01/build.json` | `384457d68132beadb29751b76cfaa68cbed17c143be8f39cbadd5e755a40deb8` |
| `build/r37-native-close/r38-native-build-d01/dependencies.json` | `ccdbe969e3e46808b3863c0c169279da9cb87f1478808a72f88aa50650c4f0f2` |
| `build/r37-native-close/r38-native-build-d01/readelf.json` | `49b77738d6c119d12c11a12fbcd25f3a521f9fa66f4cda082f09685f1aef41e4` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/fresh-query.json` | `8efdcebf25a3506cc200b10da6dfc38312c169eb378b58551730960ac2b9f889` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/continue-query.json` | `7e098467434a924529b24a621f06677322a1a8c999a31ed8ac78ca62c614b187` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/SHORT_LAUNCH_INTENT.json` | `c2e880ccd1b5e24904703b803c2ca71c95791e39d6044c8dd58fca2406d95db7` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/durable-start.json` | `af5ce3a87c8dd38c7f95ee7c9fe28ecfcacc3cda954f20c2ce48f20d500b5a2c` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/independent-strict-acceptance-d02.json` | `222855c6e4a20805a0cb124b09426fbf9d65720ca626ff72e53c18fc503bc9e4` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/DEVICE_ENVIRONMENT_AND_DEPLOYMENT.json` | `02f0918ce6cc406a36ddb6293b16c9c3f23dcc8f3ae0f7523a42b336e12307bd` |

Business output: `build/r37-native-close/execution-6159c6e4fc38f244`; durable capture: `build/r38-durable-endurance-capture/capture-a09b0d0c3f662b47`; final independent checker: `build/r38-durable-endurance-capture/final-tester-d03/device-d01/STRICT_ACCEPTANCE_D02.json`. Complete raw A/B stdout/stderr, original RESULT/ANALYSIS/CLEANUP, all actual hdc commands/rc/stdout/stderr, deployment roots, ownership/token observations and capture journal/streams are bound by the final external tree ledgers. Their member counts are2938 business /14 capture /363 query. These local evidence paths may be untracked and are not a claim that private evidence is distributed with Git.

## Independent Host regression, review and provenance

D07 actual Host regression PASS:68 Python methods,0 skips =22 durable capture+18 endurance+18 R26+10 native receipt (5 actual-build receipt and5 model). Fresh isolated CMake configure/build/CTest executes13 C++ cases, all PASS. The canonical F2 **HOST** seal has817 members. Final D03 fully rehashes that prior execution evidence and the genuine D03 native build/package; it does not rerun68/13 or recompile SDK code.

After the private orchestration review, final tester runs four fresh Windows control methods with0 skips and actual command0. The positive control closes the external owner Job, observes the worker/child/observer/runner remain live, then releases the HOSTMODEL runner and externally observes their actual OS exits0. Three negative controls reject wrong spec SHA, observer SHA and acquisition creation-time identity. Win32/production Job behavior is real; admission and runner are HOSTMODEL, with SDK0. Those controls are not DSoftBus communication. CONTROL seal binds394 members. The first initial fixture's early RUNNER_STARTED read/premature cleanup FAIL is preserved; corrected initial controls bind390 members and final controls are separate new execution.

Production D07 source/F1 review is closed on current hashes in reviewer-d02; a fresh independent Tier2 three-pass agentic review covers private orchestration and new query preparation in reviewer-d03, PASS/findings[]/blocking0. Review does not substitute for device testing. Final strict acceptance uses the unchanged original oracles and full real logs. Source13 remains byte-identical through final tester and this documentation change. The exact sources are:

| Source/test | SHA-256 |
| --- | --- |
| [integrations/ros2/target/NativeObservation.hpp](../integrations/ros2/target/NativeObservation.hpp) | `afe1572e579b9d6bb245468bd40c04f7f4c4e15c4715c9ed05430d6d2c67a425` |
| [integrations/ros2/target/NativeObservation.cpp](../integrations/ros2/target/NativeObservation.cpp) | `7c9ac5322e9619d7066c54af3b9bb25950b965b2ab45cfd88138dc1016e19aa2` |
| [integrations/ros2/target/TargetBridge.cpp](../integrations/ros2/target/TargetBridge.cpp) | `f0c66946e2226763c515594dce44f03f4ca55ddbd7d0f93dd0f25d9d8973ad3e` |
| [integrations/ros2/target/native_bridge_receipt.py](../integrations/ros2/target/native_bridge_receipt.py) | `9d1e833b67ff313e1940a1caf1c9ed86dae56b1f93fffa3cdef72c81b5868b7f` |
| [tests/ros2_host/locator_observation_tests.cpp](../tests/ros2_host/locator_observation_tests.cpp) | `7bd25299c84c3e74e5de96fa4242656df79c613f9565fefa10f58c2c32600055` |
| [tests/ros2_host/locator_observation/CMakeLists.txt](../tests/ros2_host/locator_observation/CMakeLists.txt) | `dd6e3643652bfbf4339d921d724bbebc44cbe0dd57e6b1493deaa5a7e723423f` |
| [tests/ros2_target/test_native_close_receipt.py](../tests/ros2_target/test_native_close_receipt.py) | `ffbfe34ca5428d657a79dc20a8b49003bd9a688c821d4d6fe059935b3bd26b2d` |
| [tests/ros2_target/run_endurance_roundtrip.py](../tests/ros2_target/run_endurance_roundtrip.py) | `4e7ed9d29d4ec480a87b54a9814caf1e7c704e883d0d37c1be247969dcdc1ae8` |
| [integrations/ros2/target/endurance_roundtrip_probe.py](../integrations/ros2/target/endurance_roundtrip_probe.py) | `f97f933309bb716560d3c58bd31fb08183767130b032e528d5558e37878b5da3` |
| [tests/ros2_target/endurance_roundtrip_oracle.py](../tests/ros2_target/endurance_roundtrip_oracle.py) | `4ce51467e0d779d2eb0c055a353b67dad2d52b1a28e0cbdbc4f77cbbb68c4811` |
| [tests/ros2_target/test_endurance_roundtrip.py](../tests/ros2_target/test_endurance_roundtrip.py) | `317ce91f57cfbf30ef31a2027eebdc71d2585025659f26cc78d82d063de4aff2` |
| [tests/ros2_target/durable_endurance_capture.py](../tests/ros2_target/durable_endurance_capture.py) | `2930722fdd77c1c0a9ab9be44e4dbb319122af2324421fa7b3a60f6a8d0d7081` |
| [tests/ros2_target/test_durable_endurance_capture.py](../tests/ros2_target/test_durable_endurance_capture.py) | `d8e4bd453db94796949c8373e35a43fdf66046fe9410612620b05462fd308de1` |

## Historical failures retained

R36 and R37 native-close failures remain their original FAIL/BLOCKED records; R37 original host exit/capture-loss cause remains UNKNOWN. The previous R37 docs-updater27-member blocked-handoff seal and845-file source tree are unchanged. R38 initial D01/D02/D03 Windows state replacement failures and D04 shared-delete replacement FAIL remain; append publication was separately implemented/tested. ReviewD01 F1 unassigned-child containment FAIL was fixed in D07. D06's newly added deliberate-failure launcher test incorrectly expected0 instead of actual70; its failure remains, and D07 corrects only that new expectation against the pre-existing production return contract.

Old real R38 run `efa7336e7ca4b78b` has payload/native/close component PASS but its full gate stays **BLOCKED** for missing independent held worker/child OS receipt and old long tool24208 actualexit UNKNOWN; canonical old seal `0ad3e8b7f0dc634aed4c93bfb79e49eef8ce1a0a4890385f4c3938722f8d87ec` binds1016 members. The new run does not repair those missing observations. The private orchestration first fixture FAIL remains in its seal; the final self-authored acceptor's first cwd guard FAIL remains under `final-tester-d03/device-d01/failed-private-acceptor-d01`. The corrected private guard rechecks the same actual immutable data, preserving the original source/oracles/time limits and failure bytes. Read-only graph UNKNOWN, speculative reads and sandbox access-denied observations retain their recorded non-PASS statuses; no security setting was changed to mask them.

## Supported, partial, unavailable and unverified

| Capability / gate | Current status | Evidence and boundary |
| --- | --- | --- |
| Selected public Node String pub/sub echo | SUPPORTED_WITHIN_TESTED_SCOPE / DEVICE+INTEGRATION PASS | New real two-board1801 case, RELIABLE/VOLATILE/KEEP_LAST10, 33-byte CDR, one outstanding/no application retry or dedupe. |
| Native observation / normal close | PASS_CURRENT_FINITE_CASE | Same ordered SPDP batches aggregate within256 slots; per side stored4/observed4902/repeated4898,2 distinct/2449 repeated batches; all7 fatal counters0; RuntimeClose0/observation_complete1/public deletion0/interpreter0/child0. These SPDP counters are not business CDR or wire-causality counts. |
| Durable Host capture | SUPPORTED_WITHIN_WINDOWS_X64_STAGE_NAMESPACE | Exact source13/job/identity/one-shot barrier; fixed finite bounds; worker/child held live259 then actual OS0; runner/CLI/short-helper actual0. Auxiliary observer own OS exit UNOBSERVED, exact identity absent/stable complete logs/closed handles. No general portable launcher qualification. |
| Build / HOST_TESTED | PASS; sealed prior execution REVALIDATED | Genuine D03 SDK23/AArch64 bridge23TU/objects1037inputs; D07 Python68/0skip and fresh Host CTest13/13; final D03 did not rerun these. Four new post-review HOSTMODEL admission/runner controls PASS separately; real Win32 job lifecycle only. |
| Historical service / Action / parameters / serialized bag | PARTIAL; historical bounded PASS retained | Original scopes/libraries remain dated evidence; not requalified against the new bridge hash by R38. |
| DDS/RTPS/QoS / discovery / Reliability / fragmentation / history | PRESERVED_ARCHITECTURAL_BOUNDARY; broad ROS2 coverage NOT_VERIFIED | No upper-layer semantic modification. The selected QoS case does not qualify the complete matrix, large messages or concurrent workloads. |
| Full ROS2 / DDS per-payload and wire causal proof | NOT_VERIFIED / BLOCKED_PROVENANCE_GAP | Exact deployed stripped-rclpy source/compiler/link/unstripped lineage unavailable. Native bridge build lineage and public payload observations do not fill that separate gap; no ABI root cause or impossibility is asserted. |
| Longer / hours soak; broad QoS, message sizes, concurrency | NOT_VERIFIED | One finite roughly30-minute run; no new broad matrix, stress or hours-long run. |
| Restart / repeated recovery / Participant recreation | NOT_TESTED_BY_R38 | Historical bounded replacement evidence is separate; no automatic extension to this artifact. |
| Default automatic reconnect | NOT_PROVIDED_BY_CURRENT_CONFIGURATION | Current accepted configuration does not supply it; general feasibility is not assessed. |
| Third observer | NOT_SUPPORTED_CURRENT_TOPOLOGY | Current scope has two Participants and one peer; no general implementation impossibility claim. |
| Loan / recorder-internal take | UNOBSERVABLE_WITH_CURRENT_INSTRUMENTATION / NOT_VERIFIED | No applicable internal observation in R38; historical NO_LOAN mode is not loan support. |
| Selected-client status / feedback ordering | OPEN / NOT_VERIFIED | R38 echo does not test Action status/feedback. |
| SDK epoch / late-callback drain / global leak freedom | NOT_PROVEN | Finite process/resource observations and normal close do not prove those global properties. |
| R22 | STOPPED / EXCLUDED | No silent resumption or extrapolation. |
| Tester / docs / Main PRE_COMMIT / publication / CI | PASS scoped / SYNCED / PENDING / PENDING / NOT_RUN_NO_NEW_COMMIT | Tester730-member seal is technical scope only; Main12 and final complete all/staged graph checks remain. |


The exact deployed stripped-rclpy source/compiler/link/unstripped lineage gap remains separate from the proven native bridge build. Neither an ABI root cause nor implementation impossibility follows from that missing evidence. Historical service/Action/parameter/bag PASS stays attached to its original artifact and scope. “Unsupported” above is limited to the present topology/configuration; absent instrumentation and unexecuted tests are explicitly different from inability to implement.

## Canonical evidence and remaining Main gates

All paths below are local root-relative evidence; each SHA-256 binds exact bytes. Final tester seal has730 members and binds its RESULT/REPORT, private controls, strict acceptance and raw command evidence; Main independently revalidated all730 at actual exit0 before allowing this documentation stage. It did not authorize commit there (`commit_GO=false`). Main progress writes to F3 were explicitly stopped before sealing and have not resumed.

| Evidence | SHA-256 |
| --- | --- |
| `build/r38-durable-endurance-capture/final-tester-d03/SEAL.json` | `9d8dbe9e15813da1fad73dbc213dfd505d871868e69e24a765d711a3d2f97459` |
| `build/r38-durable-endurance-capture/final-tester-d03/RESULT.json` | `ab28ad155837d8f9cb2b6ea6d7953b57a8233289898ae8bfdbe8a4f73c49b815` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/STRICT_ACCEPTANCE_D02.json` | `aa2ebb2fb82ff91557c0eaa2b0ad05cece219086276d7d6ff81f9355457c6a8e` |
| `build/r38-durable-endurance-capture/MAIN_FINAL_REAL_ADMISSION_D03.json` | `388c86817e6f55026ce3086c74de722d9515274175f250417758f146cbca2d4d` |
| `build/r38-durable-endurance-capture/IMPLEMENTATION_FREEZE_D07.json` | `13ae0a7ecc24090e18938e7b15616e40becac26f133b03eab375fda04e403478` |
| `build/r38-durable-endurance-capture/final-tester-d02/HOST_SEAL.json` | `f66b156fc6e1991a99e29b910e0ebe7c6c8363063dd671b8178a043c0952a90e` |
| `build/r38-durable-endurance-capture/final-tester-d03/CONTROL_SEAL.json` | `75cb5aeea1465c99b01c228fcbacf5d987f4b67313f3d646ca9c5fb3b3212dce` |
| `build/r38-durable-endurance-capture/reviewer-d03/SEAL.json` | `70f3b193881a2872a193059b2450081ab55d5812f8d8954b30a049a2c5e3f773` |
| `build/r38-durable-endurance-capture/reviewer-d02/SEAL.json` | `12bb185c94349ea05800ec91d1344c3546837682cc5f09935a54c09e64efb4fc` |
| `build/r37-native-close/r38-native-build-d01/NATIVE_BINDING.json` | `115370fae6cafbe1f4eb83a7bd9d2815d175d189e70c6657d03b59961dedd585` |
| `build/r37-native-close/package-96960d580a051b56/DERIVATION.json` | `62947069c097f6ed71f7062e1c4a8687d84a14d68ff753f81738c6db60e545a3` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/HOST_OS_EXIT_RECEIPT.json` | `c1f08e3b304493d874bfda4ab58002c8b03a678e74c7c0a8470462753562878d` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/FINAL_EXACT_HOST_IDENTITIES_D02.json` | `350dbd0ec3d73f5e0168e9b7e1251ccd100cafde063b130468d4a9c37936008e` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/FINAL_LOG_AND_RECEIPT_STABILITY.json` | `12ab3edbdc00922d62fad512a4e73ea542dbf4edea942dca5d055368f3db101e` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/EXTERNAL_BUSINESS_TREE_FINAL.json` | `b0fd4719cca690ebebb7ae47ea9e13818d7a82848241116c09e867e3f5aa6684` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/EXTERNAL_CAPTURE_TREE_FINAL.json` | `e88f5ebafa362f48b1842d1f5bca749ec39d965090dd95a8c1228e495f573307` |
| `build/r38-durable-endurance-capture/final-tester-d03/device-d01/EXTERNAL_QUERY_TREE_FINAL.json` | `32eeeca9f501db5ed44cc6e2af8bdfecc1ebc1074aaa825c33a5ffe7202537b8` |
| `build/r38-durable-endurance-capture/MAIN_SHORT_LAUNCH_ACTUAL_D02.json` | `7dd85e24f569ea217a5c5a2bb77de184dee549622c277455b88c2b0a7c1365bb` |
| `build/r38-durable-endurance-capture/MAIN_F3_WRITES_STOPPED_D03.json` | `8748ca6b9dc21a972cb3a394ded7da594c27c9050fd3d1b8b0e8e14a80dd646a` |

Documentation synchronization prepends current evidence to exactly eight existing approved documents and adds this page. Historical bodies, including earlier “current”/FAIL/BLOCKED/UNKNOWN snapshots, are preserved byte-for-byte; the new prefix governs current scope. The updater's own `build/r38-durable-endurance-capture/docs-updater-d01/RESULT.json`, `REPORT.md`, `MANIFEST.json`, `SEAL.json` record the changed-doc hashes, preservation checks and readiness. Readiness means docs are prepared for Main review and staging; it is not Main's PRE_COMMIT verdict.

Before any normal implementation commit, Main must finish the12 AGENTS gates, confirm no unexpected tracked changes, use the documented per-command `core.autocrlf=false` byte-preservation policy for explicit paths, and obtain complete all/staged graph change results with neither partial nor truncated output. The existing800 stat-only raw/index/HEAD-equal M entries are recorded evidence, not800 implementation changes or a Git-clean claim; no global/local Git configuration or index refresh was performed by the updater. Only Main owns stage/commit/amend/push. Actual future commit SHA, remote result and exact-SHA CI result must be recorded after they exist.
