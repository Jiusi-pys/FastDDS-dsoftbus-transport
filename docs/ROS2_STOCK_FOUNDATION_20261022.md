> Evidence link maintenance (2026-11-05): unavailable historical local files are shown as explicit paths. Original labels, hashes and dated results are retained; see the [local evidence index](LOCAL_EVIDENCE_INDEX.md). Availability does not requalify an old run as current. Historical “byte-preserved” statements describe earlier archival steps; this navigation update changes Markdown bytes and does not assert the old document hash. Original execution results and artifact hashes remain unchanged.

<!-- R39_DOMAIN_CANONICAL_20261024; this current addendum supersedes current-state claims in the preserved dated body below. -->
# Current R39 domain circuits: canonical Host scope (2026-10-24)

**Implementation: PRESENT. Fresh Host configure/build: PASS. Canonical selected Stock Host tests: 56/56 PASS, each registered case executed exactly once. SDK23 product22 build/install/relocated-consumer link: PASS; canonical A/B loader metadata: PASS in the declared shared-C++ host only; strict ABI: UNKNOWN. SDK_COMMUNICATION_TESTED=false; CROSS_DEVICE_COMMUNICATION_TESTED=false; ROS2_INTEGRATION_TESTED=false. Overall: NOT_VERIFIED. Normal Commit gate: FAIL. Ready for commit: NO.**

The independent final batch is domain-canonical-final-d02/TEST_RESULT.json (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-canonical-final-d02/TEST_RESULT.json`), with receipts and manifest (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-canonical-final-d02/MANIFEST.json`) (SHA256 `6e958084407f188c314073943d86d4a1f5c5438d908aacc8d4e92c73cac9d867`). Tested source dictionary: 873 files, SHA256 `8922c3a8f819ed118b44d79e48bee6df9cbda6e73c57f96efb58495936b01ab0`. This remains the canonical Host execution provenance, not a new Host rerun. Current post-product source inventory is877 (pre-doc SHA256 `1fff45b332d69a64db603e24e86f4e157cf7d4ef64c89b8cde8ac62d09036303`); the five product source paths legitimately differ/add to that older Host inventory. This documentation sync changes only five Markdown files, leaving all872 other current files identical to the final877 snapshot.

## Current delivery directory (composite package, 2026-10-24)

Use the current delivery directory (historical local evidence unavailable in this checkout: `../../../r39-stock-product-delivery-20261024-d01`), `build/r39-stock-product-delivery-20261024-d01`, for this artifact set. Independent delivery validation (historical local evidence unavailable in this checkout: `../../recovery-20261024/tester-product-delivery-d01/RESULT.json`) confirms exactly40 files:39 byte-identical files from the preserved earlier package plus the final canonical `bin/stock_transport_loader` (SHA256 `0225a138175d14e10844d18ad357f78ee8ed5b4c540e91070ab05c2d56b30751`). The product library remains SHA256 `91456513c56dcef49b87ec18fd0211d6b15bd7ad3f051118b064d1ac2d0a8883`; this is reused product22 output, not a new product rebuild/full CMake install. Exact source/destination hashes are in the composite package manifest (historical local evidence unavailable in this checkout: `../../recovery-20261024/tester-product-delivery-d01/COMPOSITE_PACKAGE_MANIFEST.json`); stage manifest SHA256 `7d6a10c0f7537226acc5bd6cac8a65fac51a4eb9d10d60e4d1c7d611b51d42d4`.

The old `build/r39-stock-product-fixed-initial-d01/install` remains historical and unchanged: its library is correct but its loader is the earlier C-driver `0dae3d66...`, so that prefix is not the complete current package. The new directory explicitly combines that immutable package with the canonical loader. Its standalone Bootstrap archive was rebuilt from current source during the original product phase despite matching historical hash40f95; product22 compiled Bootstrap directly. A fresh separate installed-consumer configure/one-TU compile+link against the new prefix passed (consumer not executed), with SONAME/`$ORIGIN`/root C++ dependency checks. Platform/vendor dependencies remain external and were not packaged. No product22 rebuild, device run or communication acceptance was added; prior final A/B metadata-only results, original C-only139 FAIL, ABI UNKNOWN, communication/cross-device/ROS2 false and exact SetSelfTokenID PENDING remain unchanged. Normal Commit gate FAIL; Ready for commit:NO.

## Current product package and canonical loader metadata (2026-10-24)

`BUILD_STOCK_TRANSPORT_OHOS_PRODUCT` is implemented, default OFF. It requires the real OHOS toolchain, shared SDK C++ runtime, exact Fast DDS2.14.5/FastCDR2.2.5 and explicit hash/ELF-qualified real Socket provider; this build option does not enable the native quota opt-in. Current Runtime20/Observation/Bootstrap and old tests are unchanged by the five-path product increment (stock CMake, package config, pure-C loader and two consumer files).

Corrected product initial execution (historical local evidence unavailable in this checkout: `../../recovery-20261024/tester-product-fixed-initial-d01/RESULT.json`) and the first product final (historical local evidence unavailable in this checkout: `../../recovery-20261024/tester-product-final-d01/RESULT.json`) built all22 current product TUs including Bootstrap, performed native strict link (`-z defs`, `--no-undefined`, `--no-allow-shlib-undefined`), installed and compiled/linked the relocated installed consumer. Product SHA256 `91456513c56dcef49b87ec18fd0211d6b15bd7ad3f051118b064d1ac2d0a8883`; actual unversioned SONAME `libfastdds_dsoftbus_stock_214.so`, RUNPATH `$ORIGIN`, seven direct dependencies. This advances product build/package/link evidence; the consumer was not executed. The original initial consumer literal-newline failure and absent SONAME/RPATH remain FAIL. Seven intended negative guards passed; unsupported-host guard was NOT_REACHED after an earlier toolchain-control failure, not PASS.

The first product-final pure-C-linked metadata loader on A (PID15157) printed dlclose0 then exited139/SIGSEGV; B was skipped. Read-only teardown diagnosis (historical local evidence unavailable in this checkout: `../../recovery-20261024/developer/product-teardown-diagnosis-d01/REPORT.md`) correlates IPC registry teardown through `_ZdlPv` with three stale SDK allocator GOT entries. This is a loader C++ provider/lifetime failure, not an observed Socket/domain-traffic failure; general SDK unload safety remains UNKNOWN. Old LP02 eight normal exits retained dlopen handles and do not prove current unload safety. A separate same-C-object executable-root SDK C++ control exited0; it is diagnostic support, not the canonical fix or unique-cause proof.

The four-line CMake correction (historical local evidence unavailable in this checkout: `../../recovery-20261024/developer/loader-abi-owner-fix-d01/`) selects ordinary `LINKER_LANGUAGE CXX` for the loader while preserving pure-C compilation, main/dlopen/dlsym/dladdr/dlclose/normal return. This models the declared shared-SDK-C++ consumer host: no forced runtime retention, NODELETE, skipped close, `_Exit`, allocator wrapper or higher-layer change. Actual executable root NEEDED `libc++_shared.so` and `libc.so` was checked. Canonical initial A/B PASS (historical local evidence unavailable in this checkout: `../../recovery-20261024/tester-loader-owner-initial-d01/RESULT.json`) was followed by independent review with no actionable findings (historical local evidence unavailable in this checkout: `../../recovery-20261024/reviewer-loader-owner-d01/REPORT.md`) and then the authoritative post-review final (historical local evidence unavailable in this checkout: `../../recovery-20261024/tester-loader-owner-final-d01/RESULT.json`), manifest SHA256 `b567d2d6acda101a87f7042f14c753ee3ebc643311b416c2e0a9a6ea5140d81f`.

Final execution freshly compiled only the C loader and linked via the standard C++ driver (native0), producing loader SHA256 `0225a138175d14e10844d18ad357f78ee8ed5b4c540e91070ab05c2d56b30751`. Product91456513 and its22 source/flags/generated-rule inputs were revalidated and REUSED, not rebuilt; no new install, consumer execution or Host56 rerun. A PID19232 and B PID27151 each launched exactly once, completed real provider-address/current-product checks, dlclose0 and native exit0; postflight file/system hashes matched and diagnostic processes were absent. Remote metadata-only directory: `/data/local/tmp/fastdds-r39-product-loader-20261024-owner-final-72c71618b56e`. Exact commands, environment, logs and native receipts are in final local receipts (historical local evidence unavailable in this checkout: `../../recovery-20261024/tester-loader-owner-final-d01/RECEIPTS.json`) and board receipts (historical local evidence unavailable in this checkout: `../../recovery-20261024/tester-loader-owner-final-d01/BOARD_RECEIPTS.json`).

`TARGET_LOADER_METADATA_TESTED=true` only in this declared shared-SDK-C++ host contract. `SDK_COMMUNICATION_TESTED=false`, `CROSS_DEVICE_COMMUNICATION_TESTED=false`, `ROS2_INTEGRATION_TESTED=false`; strict ABI remains UNKNOWN. No explicit Runtime/Participant/Socket/BX06/consumer execution, token selection, chmod or security/network configuration change occurred; dependency-initializer side effects are not proven absent. SDK23 reports6.1.0.32 versus board6.1.0.31. dlsym addresses do not prove GOT bindings, C++ object ABI, native close/recreate or SDK drain; static weak gaps169/C++ overlaps464 remain incomplete ABI evidence. Runtime lifetime and global lookup order changed together and before/after maps were not observed; do not infer a unique cause or universal C-only embedding ban. Original C-only native139 remains FAIL. Exact SetSelfTokenID confirmation stays PENDING, untouched. This loader ABI build contract does not fix default native-ID capacity.

Earlier runtime21 compile (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-runtime-sdk23-compile-d01/RESULT.json`) and standalone strict diagnostic link (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-runtime-strict-link-d01/RESULT.json`) remain dated build evidence. The product/canonical loader stages above supersede their old no-product-link/no-loader-execution status only within the stated scopes. Normal project Commit gate remains FAIL; Overall NOT_VERIFIED; Ready for commit:NO.

Generic ordinary/Persistent `MakeDescriptor` with absent expected domain now waits for a usable real public actual-DID callback to bind each view's bounded circuit. Explicit expected domain, including engaged zero, and `MakeStockDescriptor` remain NativeBound; a genuine wrong actual domain becomes sticky Refused and denies actual Sender/provider traffic. Missing/unsupported callbacks remain Await and fail closed. `ScopedTransportBootstrap` still calls MakeStockDescriptor with fixed explicit configuration: automatic natural ROS_DOMAIN_ID operation without extra configuration is not established. Current capacity is two views/two domain circuits per Runtime; this is an implementation bound, not a necessary SDK single-domain restriction or complete ROS2 multi-Participant support.

The older dated sections below are historical evidence. In particular their configured-domain default policy and original failures are not the latest generic policy, and no historical FAIL is retrospectively changed to PASS. R38 board successes retain their original provenance; they are not new R39 communication acceptance.

## Canonical execution boundary

The 56 selected cases cover domain9, Work7, default-domain11, views6, normal5, circuit5, multi3, default9 and process-receive1. Fresh configure/build native exits were zero; every selected test native exit was zero. Children were ordinary Windows processes (AppContainer0, elevated0). Actual `fastrtps-2.14.dll` module path/hash was captured for 55/56 (SHA256 `68ed01951f54dd19907f05821be21341b6384750847845baf8aa3c68dc4660cf`); process-receive has no loader-helper print and is N/A for module-path proof. Selected CMake package files/hashes identify Fast DDS2.14.5 and FastCDR2.2.5; no actual FastCDR runtime module path was captured.

Exact executed argv, cwd, environment allowlist, PIDs, timestamps, native exits and logs are in configure.start.json (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-canonical-final-d02/configure.start.json`), build.start.json (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-canonical-final-d02/build.start.json`), per-case `*.start.json`/`*.receipt.json`/stdout/stderr, registered selection (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-canonical-final-d02/REGISTERED_SELECTION.json`) and artifacts (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-canonical-final-d02/ARTIFACTS.json`). These are executed commands, not examples or a claim that docs_updater ran tests.

The supplemental remaining-regression record (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-remaining-regression-d01/TEST_RESULT.json`) is historical source7109: 44 Stock cases, 43 PASS/one held original Root-predicate FAIL, plus 68 pure-model PASS. Final input revalidation (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-canonical-final-d02/SUPPLEMENTAL_MODEL_REVALIDATION.json`) confirms unchanged model inputs/artifacts/options/header/link identities on current source8922; it is not a new model execution. The models consume 31 headers/20 project-source closure from an existing interface fixture (HEAD `343f155c36b561db3d9f047a65d866ce0f3da300`, version3.6.2.0), zero Fast DDS implementation C++ and no DDS implementation binary. They do not establish Stock DDS integration or a patched DDS binary.

Root CONFIG HostLoop/shared/input-close/provider/public-shim regressions remain BLOCKED: root requires exact3.6.2.0 whereas this Stock project uses mandated2.14 and does not add the root project. No version check was weakened, no3.6 binary fallback was used, and SOURCE_PROBE models are separate. Real SDK/DSoftBus communication, dual-board, ROS2 Jazzy services/actions/QoS/serialization/large-payload/concurrency acceptance remain outside this new execution scope. Preserving upper protocol code is not an automatic feature test.

Public-QoS/ScopedTransportBootstrap still selects the explicit stock descriptor. Foundation SDK23 build evidence below belongs to its earlier frozen source/artifact scope. Canonical Host final did not execute SDK code; the subsequent product22 build/package and canonical target metadata-only loader scope are recorded above. Native domain-circuit communication acceptance remains unverified.

## Remaining required gates

Default quota8 no-handle-failure exhaustion/9th no-SDK-call remains unchanged; opt-in twelve loops require full immutable deployment/name/identity/collision prerequisites and do not solve default capacity. Retained slots/entrances/IDs and void native Shutdown do not establish silence. New exact SDK23 SetSelfTokenID `mdds` profile confirmation is PENDING: prior read-only permission/package/GetSelf evidence is not current identity selection or a board Socket BYTES run. BX06 prepared SDK BYTES package remains HostBuild-only with confirmation card pending. No new identity selection, deployment or board communication acceptance is claimed.

GitNexus cached impact remains stale/UNKNOWN with historical CRITICAL110 not waived. New getter lookup is unresolved; text/source confirmation supports documentation only. No clean-index or detect_changes claim is made; no commit gate is passed. Required root regressions, SDK/native communication, RK3588A/cross-board and full ROS2 validation remain incomplete.

---

Historical dated record (preserved verbatim below):

# R39 stock Fast DDS / public-QoS foundation evidence

Snapshot: 2026-10-22, after independent post-review final testing. **Overall NOT_VERIFIED; normal project Commit gate BLOCKED; Ready for commit: NO.** This phase qualifies only six frozen new files for the stock-build / public-QoS Bootstrap foundation. It does not establish a new native ROS2 runtime or full Jazzy support.

## Implementation and artifact boundary

Candidate `build/r39-ros2-complete-matrix/candidate-d03` is detached at historical R38 publication `123051e6abcc55db0712befd71eca0c7670f681f`. New sources are `integrations/ros2/stock/{TransportBootstrap.hpp,TransportBootstrap.cpp,CMakeLists.txt,build_stock_fastdds.py}` and `tests/ros2_host/{stock_transport/bootstrap_tests.cpp,test_stock_fastdds_build.py}`. They remained byte-frozen throughout initial testing, review and final testing. Documentation synchronization does not change them.

The Bootstrap uses public Fast DDS Factory/profile/default Participant QoS APIs to select a custom Transport descriptor and explicit local/peer locators before Participant creation. Release refuses active Participants or changed defaults and restores the previous defaults after successful public deletion. The tested participant-owned Transport shuts down naturally while the descriptor remains retained. No local Factory implementation, old Bridge/TargetBridge or endpoint-policy hook is in the measured Host input set. Host linkage has eight real imported Factory APIs; the runtime build has23 translation units, one DDS_EXPERIMENT variant, PRIVATE RUNTIME_PROBE and unchanged observation counters. These are bounded measured facts, not a global reachability proof.

The new target entry/package and runner/artifact binding are **NOT_IMPLEMENTED / NOT_TESTED**. Preparation identifies Runtime exposes only PumpSend/PumpReceive/Close, the old BridgeOwner busy-yield cannot be copied directly, and descriptor single-create constraints as **IMPLEMENTATION_GAP** for broader lifecycle behavior. Multiple Contexts/Participant recreation must not be claimed from this single-Participant Host foundation. DDS/RTPS/Writer/Reader/discovery/QoS semantics remain outside the Transport adaptation; only DSoftBus Socket API is allowed.

## Canonical final execution and review

| Evidence | Actual outcome | Practical limit |
| --- | --- | --- |
| Fresh Host configure / VS2022 direct MSBuild | PASS, actual exit0; `/m:1 /nr:false` | Fresh final directory; not an old parallel 600s replay |
| Original Host CTest | 3/3 PASS: lifecycle, defaults-changed, invalid; unchanged25s each | Real Factory/Transport through Fake Socket; three direct repeats add process evidence, not unique coverage |
| Python stock integrity | 4/4 PASS, three negatives and one positive control | Source byte/member/archive checks; not device tests |
| Public configuration helper | Fresh unmodified reviewer helper configure/build, boundaries and XML PASS | Active disabled participant, invalid empty/max0, AlreadyInstalled, defaults restoration, re-registration; descriptor create calls0 |
| Existing SDK23 stock Fast DDS build | Developer archive/configure/build/install actual0; final independent audit PASS |3771 raw-equal upstream files,229 AArch64 objects/VALID deps,2644 classified dependency paths, exact229-object link response; final tester did not rebuild229 TUs |
| Existing SDK23 ELF | `libfastrtps.so.2.14.5`, SONAME `libfastrtps.so.2.14` | SHA256 `817cfc21e99e0e65c43d083c0d7354484311c2c23a9d299c7a5ec742c5233a61`; no target execution |
| Fresh SDK23 Bootstrap | AArch64 static compile PASS, BUILD_TESTING=OFF, single `TransportBootstrap.cpp.o` | SHA256 `40f95a4337790059c353affa8796d94f381aa0845c3b58d65582e24447887e1f`; runtime link and execution NOT_RUN |
| Actual Host DLL load | Toolhelp observed `C:\pixi_ws\ros2-windows\bin\fastrtps-2.14.dll` | SHA256 `6cd266232ace706f5998537432b9ffb4774b4c4b1db462b1bbe8e43dfe387fca`; five API headers raw-equal to stock, full original source/compiler lineage UNKNOWN |
| Independent agentic review | Tier2, zero blocking findings; no post-review source edits | Joined forced-include parser hardening deferred as nonblocking for fixed audited SDK inputs; no generic arbitrary/untrusted configuration qualification |
| Canonical final execution gate | `PASS_FOR_BOUNDED_SIX_FILE_FOUNDATION` | Tester `PASS_FOR_BOUNDED_TEST_EXECUTION_PORTION_ONLY` is not a normal project commit gate |

Lifecycle output includes `PUBLIC_FACTORY_DELETE=0 NATURAL_TRANSPORT_SHUTDOWN=1 DEFAULT_DESCRIPTOR_RETAINED=1`. Frozen assertions establish final Fake Socket live/queue/bytes/invalid counters0. This does not prove global real data-plane resource quiescence. The final checker handle count130->131 leaves an unattributed+1 UNKNOWN; exhaustive system-wide descendant lineage and native SDK quiescence remain UNKNOWN.

The reviewer confirmed the compiler accepts joined `-include<path>` beyond the current parser guard. The fixed SDK environment-propagation counterexample did not demonstrate a current false-PASS route; actual measured commands/dependencies/link inputs were separately audited. Main records the hardening deferral, to be reconsidered before broadening accepted inputs.

## Devices and required acceptance

Main's read-only HDC records identify two USB Connected boards: `3e01ff55454d202020104033bf453b00` and `3e01ff55454d202020104433991c3b00`. Later approved normal-shell reads have nonempty actual exit0 output: Linux6.6.101 aarch64, OpenHarmony-6.1.0.31, and device-tree model `Rockchip RK3588 EVB1 LP4 V10 Board`. Earlier default-sandbox exit0/empty output is retained and does not prove remote execution. Fifteen read-only HDC calls and zero device writes are recorded in this recovery stage.

**New stock DEVICE_TESTED=false; CROSS_DEVICE_TESTED=false; required DSoftBus/ROS2 integration BLOCKED_NOT_RUN.** No new target package was deployed and no new Socket/device/cross-device functional test ran. These required tests are not N/A. New packages, targets and tests need concrete artifact/target admission under the project gates; R38 permits/budgets do not automatically apply. Routine reversible deployment and Socket testing can continue within this session's existing authorization. Actual security, permission, token or credential changes require specific human authorization. Board availability does not close this gate.

The [37-case current-stock capability matrix](ROS2_CAPABILITY_MATRIX.md) is finite acceptance work, not a completion percentage. Seventeen historical scenes need current-artifact rebinding; twenty device runners are missing. All37 lack new-stock acceptance, including explicit security-disabled and currently-not-provided configuration rows. R38's genuine finite1801 String roundtrip/7204 CDR record dual-board PASS and published SHA remain historical results for a patched Fast DDS/bridge artifact, not evidence for this new unmodified upstream path. Historical service/Action/parameters/bag/restart results likewise retain their original artifacts and scopes.

## Retained failures and evidence integrity

| Record | Preserved status |
| --- | --- |
| Old SDK D01 | FAIL: SONAME qualification failed despite prior build commands0 |
| Old SDK D02 | UNKNOWN: partial logs163/231 and165 objects; no terminal build/install outcome or ELF |
| Old Host D01 | UNKNOWN: CompilerId only; no terminal build/executable outcome |
| Host D03; D04-D08 | D03 timeout UNKNOWN; D04-D08 recorded failures retained; later D09 passed |
| Recovery process absence | Does not infer historical exit0 or convert UNKNOWN into PASS |
| Initial evidence after reviewer CTest | Original287-member manifest unchanged;285 match,2 generated Testing files changed,0 missing; original RESULT hash still matches |
| Changed initial generated files | `host-build/Testing/Temporary/CTestCostData.txt` and `LastTest.log`; no reconstruction/restoration/backdating or manifest rewrite |
| Review evidence |410/410 matched in Main/final tester read-only audits |
| Final evidence |436/436 matched in Main and docs-updater read-only audits; sealed final directory must not be reused for test execution |
| Final tester harness | First process audit exit1 counted its own active checker; original evidence retained, fresh D02 actual0; no production-test failure invented |

The older weekly recovery reports predate final testing and the disclosed reviewer side effect. This record and canonical final evidence supply the later facts without rewriting those historical files.

## Commands and local evidence entrances

`R` means the original workspace `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport`, not the nested candidate root. `B=R\build\r39-ros2-complete-matrix`; `REC=B\recovery-20261022`. The following are retained local evidence paths; build artifacts/logs are not claimed as distributed Git files. Read the recorded argv/cwd/PID/creation/exit before any separately authorized new run. Never execute tests inside a sealed output directory.

| Local evidence relative to R | What it binds |
| --- | --- |
| `build/r39-ros2-complete-matrix/recovery-20261022/tester-final-d01/RESULT.json` / `REPORT.md` | Canonical post-review result, bounded scope and blockers |
| `.../tester-final-d01/COMMAND_LEDGER.json` / `OUTER_TOOL_LEDGER.json` | Exact actual configure/MSBuild/CTest/Python/ninja/readelf/helper subprocess argv, cwd, timing, PID/creation, raw stdout/stderr and exits; outer wrapper outcomes separately |
| `.../tester-final-d01/SOURCE_AND_BINARY_INPUT_BINDING.json` / `HOST_ASSERTION_EVIDENCE_MAP.json` | Frozen source and actual binary/loaded DLL / assertion-to-log bindings |
| `.../tester-final-d01/EVIDENCE_MANIFEST.json` |436-member final seal; SHA256 `7fd6904dcffe53e200c594b192f5ed15087ab6c49ce71e11fb57ed3e5878c4b7` |
| `.../developer/FREEZE_HANDOFF_D01.json` | Six source hashes; SHA256 `1fd27527d151d04356917272c770adc099a971cb62fb1330d602988c86be5399` |
| `.../review-d01/REVIEW.json` | Tier2 review; SHA256 `6aeada4f458acbd6347230f8380a150b41ec4f4a639c1280930bebad50d3b494` |
| `.../main-d01/REVIEW_DISPOSITION_D01.json` | Nonblocking parser hardening disposition; no source changes |
| `.../main-d01/INITIAL_EVIDENCE_REVIEW_SIDE_EFFECT.json` | Exact two changed initial generated files and before/current hashes |
| `.../main-d01/FINAL_EVIDENCE_READONLY_AUDIT_D01.json` | Main post-seal436-member audit |
| `.../main-d01/HDC_LIST.json`; `.../main-device-env-d02`, `main-device-env-d03`, `main-device-model-d01` | Actual read-only argv/exits and nonempty device identity evidence |
| `build/r39-ros2-complete-matrix/stock-sdk23-d03/RESULT.json`, `build/compile_commands.json`, `build/CMakeFiles/fastrtps.rsp` | Original stock build command exits, actual compiler commands and link inputs |
| `build/r39-ros2-complete-matrix/developer-baseline-d01/JAZZY_ACCEPTANCE_MATRIX_V1.{json,md}` |37 finite APIs/oracles,17 old scenes,20 missing runners and required new inputs |
| `build/r38-durable-endurance-capture/main-publication-d01/REPORT.md` | Actual R38 publication/gates/CI; supersedes pending-publication wording only for that historical phase |
| `.../docs-sync-d01/RESULT.json` / `REPORT.md` | Documentation hashes, actual Git diff, frozen-source preservation and Ready:NO |

Canonical final RESULT SHA256: `a471cc1238d3772228691587c0f84fc14e1fa983484026a2b69e2083c3f682e2`.

All documentation-stage Git reads use per-command `git -c core.autocrlf=false --no-optional-locks`; no configuration change. Source/test edits, tests/builds/HDC calls and Git state transitions are outside this documentation stage.

## Remaining normal project gates

New target entry/package/current-stock artifact binding, the37 current-stock cases and required physical communication acceptance remain incomplete. Original Host DLL and reused target dependency source/compiler lineage remain unestablished. Full ROS2 and global resource/drain guarantees remain NOT_VERIFIED.

GitNexus candidate alias `FastDDS-r39-stock-candidate-d03` has20961 nodes/57837 edges, but global process extraction is truncated, Release is lower-bound with an unresolved receiver, and43 older UNKNOWN results remain unresolved. Earlier all=0 on six untracked sources is not a clean change check. Main must obtain complete visible all/staged assessment and apply the full12-item PRE_COMMIT gate before any normal implementation commit. Documentation sync alone cannot make Ready:YES.

R39 has no stage/commit/push. Published R38 branch `codex/flow-encoding-published-20261014` remains at `123051e6abcc55db0712befd71eca0c7670f681f` in the recorded read-only check. SSH read-only D02 failed128 with timeout to198.18.0.136:22; authorized API reads remained available. Remote/network/credentials were not changed. Exact-HEAD CI has0 runs/checks/statuses: **NOT_REPORTED, never PASS**.
