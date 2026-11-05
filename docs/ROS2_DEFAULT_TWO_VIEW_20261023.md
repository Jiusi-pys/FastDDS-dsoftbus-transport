> Evidence link maintenance (2026-11-05): unavailable historical local files are shown as explicit paths. Original labels, hashes and dated results are retained; see the [local evidence index](LOCAL_EVIDENCE_INDEX.md). Availability does not requalify an old run as current. Historical “byte-preserved” statements describe earlier archival steps; this navigation update changes Markdown bytes and does not assert the old document hash. Original execution results and artifact hashes remain unchanged.

<!-- R39_DOMAIN_CANONICAL_20261024; this current addendum supersedes current-state claims in the preserved dated body below. -->
# Current R39 domain circuits: canonical Host scope (2026-10-24)

**Implementation: PRESENT. Fresh Host configure/build: PASS. Canonical selected Stock Host tests: 56/56 PASS, each registered case executed exactly once. SDK23 product22 build/install/relocated-consumer link: PASS; canonical A/B loader metadata: PASS in the declared shared-C++ host only; strict ABI: UNKNOWN. SDK_COMMUNICATION_TESTED=false; CROSS_DEVICE_COMMUNICATION_TESTED=false; ROS2_INTEGRATION_TESTED=false. Overall: NOT_VERIFIED. Normal Commit gate: FAIL. Ready for commit: NO.**

The independent final batch is domain-canonical-final-d02/TEST_RESULT.json (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-canonical-final-d02/TEST_RESULT.json`), with receipts and manifest (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-canonical-final-d02/MANIFEST.json`) (SHA256 `6e958084407f188c314073943d86d4a1f5c5438d908aacc8d4e92c73cac9d867`). Tested source dictionary: 873 files, SHA256 `8922c3a8f819ed118b44d79e48bee6df9cbda6e73c57f96efb58495936b01ab0`. This remains the canonical Host execution provenance, not a new Host rerun. Current post-product source inventory is877 (pre-doc SHA256 `1fff45b332d69a64db603e24e86f4e157cf7d4ef64c89b8cde8ac62d09036303`); the five product source paths legitimately differ/add to that older Host inventory. This documentation sync changes only five Markdown files, leaving all872 other current files identical to the final877 snapshot.

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

## Input lifecycle repair and review disposition

Original independent review (historical local evidence unavailable in this checkout: `../../recovery-20261023/reviewer-domain-nativebound-d01/REPORT.md`) found P1 receiver publication versus CloseInput and P2 missing receive lease on same-facade reopen. The unmodified independent repro (historical local evidence unavailable in this checkout: `../../recovery-20261023/domain-input-repro-initial-d01/`) remains an actual FAIL: Open/callback reported success/Ready but replacement/app count stayed zero despite a fresh forwarded marker81.

Current HostLoop captures receiver/incarnation under the binding transaction; CloseInput retires under synchronization then releases locks before controls/drain; reopening an already bound circuit attaches a fresh lease. Repair review (historical local evidence unavailable in this checkout: `../../recovery-20261023/reviewer-domain-input-fix-d01/REPORT.md`) found no additional actionable issue. Targeted9 PASS and canonical56 PASS include the unchanged repro with marker81 exactly once at the new receiver, old receiver stable and app lease1; twelve input cycles101..123 retain stable native IDs/count3 and retire old admissions. These cycles reuse the same facade/native anchor, not full native destruction/recreation. Reentry observes actual AppDetaching, successful same-DID callback, refused callback-context close and eventual close completion. The exact P1 post-Attach/pre-publication runtime schedule is NOT_EXECUTED; source transaction repair and these tests do not prove formal race freedom.

The proposed PumpSendImpl change was withdrawn and its entire original function restored. A target view can TX before its first RX only with the tested root-Await sibling-listener/provider condition (native peer90); the original whole-Runtime no-OpenTX/provider-null gate remains.

## Work contract migration and read-only observation

The observation/Work independent review (historical local evidence unavailable in this checkout: `../../recovery-20261023/reviewer-domain-work-observation-d01/REPORT.md`) reports no actionable findings after seven Work PASS; its recommended test-only failed-close coverage was then added before the fresh canonical56 batch. This final evidence supersedes pre-review execution for current acceptance. The original three review reports and developer/Main decisions remain separate immutable evidence.

A separate bounded circuit DTO, nonvirtual getter and ProbeAccess bridge expose values only. Old HostLoopObservation, BoundedConnectionSnapshot and old getters remain unchanged. New consumers link rebuilt archives; SDK ABI compatibility is not established. References/status are pinned under root gate and sampled afterward. Adapter details are available only for closed roots with successful status/cleanup, drained adapters and no observer fault. Active/failed roots return unavailable adapter details while native usage is independent. Observation groups are non-atomic and Available does not imply business Ready.

The new explicit-zero failed Socket(-7654)/close0 test records rootClosed1, Available/count1/domain0, native present/adapter unavailable, terminal-adapters false; native owners1, retainedIDs0, NQ0, FailedRetained1, callbacks0. This is represented domain0 metadata and a retained no-handle owner, not domain0 DDS traffic, complete resource clearance or SDK quiescence. Observer-fault/no-circuit branches remain unexecuted.

Work diagnostics preserve the old actual failures: before DID, live2/listens1 per side/Bind0/both Await conflicts with old full6. Five ordinary tests now perform full6 after real confirm_raw151, keeping generic descriptors, Await Sender denial and other checks; unsupported mode retains its original prewarm full6. Cleanup applies the exact original used-entry predicate (SHA256 `2c427f1c408a405aa3f58478ea0049ee97e8314099dbe28ef2188503db7148ba`) to all three actual circuit records and Root.listener. All64 original check calls and helper remain verbatim; unused Root.binding/accepted explicitly require Empty/uncreated/no resources, and no allowEmpty relaxation is used for used entries. Circuit.listener is a metadata anchor, not another physical SDK listener. NativeUsage adds active-permit/callback-zero checks.

Circuit retained IDs2/NQ2 are included in aggregate Root3/NQ3, not five resources. Released owner references leave identity fields unavailable/default0; owner_closed while present means close_intent, not proof of live/dead owner or SDK silence. Natural FactoryA shutdown3/callback0 versus heldA shutdown2/terminal-callback1 both finish with Fake live/queued/bytes/invalid0; this is not all-native/callback-resources-zero or SDK-drain proof. See startup migration ledger (historical local evidence unavailable in this checkout: `../../recovery-20261023/developer/domain-work-startup-migration-d01/`), cleanup ledger (historical local evidence unavailable in this checkout: `../../recovery-20261023/developer/domain-work-cleanup-migration-d01/`) and observer increment (historical local evidence unavailable in this checkout: `../../recovery-20261023/developer/domain-circuit-observation-d01/`).

The original runtime_views/T2Domains source bodies remain unchanged (domain body621bytes; original hashes `7e17d6f6e5cef87cccbb1c3a3765a6dd1857313b08d6593b6177544123769d1c` / `a1266107a4b5582fe5a5c2ee16139e5a5e395bef2ff5700a1e9b2ae73fa856fc`) and now PASS. This is distinct from migrated NativeBound fixtures and revised Work contracts. Old raw/mismatch151/mismatch152 and print-only diagnostic FAILs remain archived FAILs; they are not retroactive original-baseline PASS.

## Remaining required gates

Default quota8 no-handle-failure exhaustion/9th no-SDK-call remains unchanged; opt-in twelve loops require full immutable deployment/name/identity/collision prerequisites and do not solve default capacity. Retained slots/entrances/IDs and void native Shutdown do not establish silence. New exact SDK23 SetSelfTokenID `mdds` profile confirmation is PENDING: prior read-only permission/package/GetSelf evidence is not current identity selection or a board Socket BYTES run. BX06 prepared SDK BYTES package remains HostBuild-only with confirmation card pending. No new identity selection, deployment or board communication acceptance is claimed.

GitNexus cached impact remains stale/UNKNOWN with historical CRITICAL110 not waived. New getter lookup is unresolved; text/source confirmation supports documentation only. No clean-index or detect_changes claim is made; no commit gate is passed. Required root regressions, SDK/native communication, RK3588A/cross-board and full ROS2 validation remain incomplete.

---

Historical dated record (preserved verbatim below):

# DP：默认及 observer Runtime 双视图阶段记录（2026-10-23）

**当前双视图有限 Host Fake 范围 PASS；Overall FAIL / NOT_VERIFIED；正常 Commit gate FAIL；Ready for commit: NO；DEVICE_TESTED=false；CROSS_DEVICE_TESTED=false。** 用户目标仍是“我需要完整实现 ros2 功能，不改变 transport 以上”；10月23日继续推进当前Windows项目的授权未改变Transport及以下边界。此报告不宣布完整ROS2完成，不把拒绝业务或fail-closed当成功功能。

## 当前实现及生命周期合同

精确八路径 DP+compat 增量为+277/-40，`main-default-review-artifact-d01/INCREMENTAL.patch` SHA `888693c44205f0113fb0cf0ccbab79ff8993c160721996c611fd6b553425d68c`，不是整个53项Git脏树。四个生产路径是 `include/fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp`、`HostProviderLoop.hpp`、`src/ExperimentalRuntime.cpp`、`HostProviderLoop.cpp`；另有stock CMake、`tests/experimental_runtime_tests.cpp`、新增`default_participant_tests.cpp`和`runtime_views_tests.cpp`。源码867条字典SHA `19b57267f4fd2a8e20edecd471f9c35116766b0d8a579fa176ca8ba14bb167a7`，与DF01原始列表逐路径哈希一致；两种JSON序列化原始文件哈希不同，不能混称同一字节文件。

normal/observer Runtime只签发1个descriptor，重复MakeDescriptor拒绝；同一descriptor可创建固定2个active或pending factory views。先准备metadata，实际input才打开SDK；facade完整构造后、原owned指针返回前提交publication。未发布的失败预留可退款，未使用seed在从未发布时可保留；发布后最后view/reservation终止normal lifetime，不能重新打开。metadata Start实际失败是terminal；唯一stop owner由gate转换选举，其他关闭者等待结果，不先stop再返回Busy。observer exception/reentry的sticky fault保持可见。explicit PersistentNativeAnchor可签发多个兼容descriptor，共用同2slots并保留native pin；legacy reaccept/node-route exclusive边界未放宽。

独立receiver/egress leases使关闭一个view不关闭其sibling。预算仍8records/16entrances；max2/onepeer不是一般多Participant/多peer支持。same-Runtime local edges仍NOT_PROVIDED，不称all-to-all完备。未修改DDS/RTPS/RMW/rcl语义，不截获stock Factory，不调用项目Session API。

## 三分原始基线与断言迁移

原测试完整大小是**19389bytes**，SHA `7651b974cc09c2591eae7d98686c7622baac58f47096df9164c4c74418c0c0a5`，不是7651bytes。必须分开以下结果：

| 测试与库组合 | 实际结果 |
| --- | --- |
| DI01原字节测试+首个dual-view/precompat候选库 |nativeOS1，真实历史FAIL，非R38；DI01组件187/188失败记录保留 |
| DI02及DF01原字节测试+当前compat库 |nativeOS2，ACTUAL_FAIL_ORIGINAL_ORACLE_CONTRACT_CONFLICT；原return1条件已通过，return2中具体OR operand未instrument，UNKNOWN；不得计PASS |
| 新版加强双视图Baseline+当前compat库 |nativeOS0，当前契约PASS；DI02/DF01组件188/188明确使用这一版本 |

`ASSERTION_CHANGE_LEDGER`、DI02与DF01的`ASSERTION_AUDIT`/`BASELINE_THREE_WAY_RESULTS`及独立review的`ASSERTION_INDEPENDENT_CHECK`逐项绑定源码和真实退出。早期audit的NOT_YET_EXECUTED时间点已被后来实际OS2回执覆盖，不能继续声称没有执行原control。

| 断言/源码项 | 保留或变更及原因 |
| --- | --- |
| descriptor签发 |原`if(!da||!db||a.MakeDescriptor(descriptor(0)))return 1;`逐字保留；生产恢复重复签发拒绝，两view通过原descriptor获得 |
| facade容量 |旧第二facade拒绝改为RAII持有两facade成功、第三拒绝；匹配用户所需双视图，固定上限不增加，异常返回指针也不会泄漏 |
| preinit生命周期 |新增两侧在未init而持有borrowed facades时Close均Busy，证明预留计容量和寿命 |
| init |原两线程init及成功检查保留；不是用预计算状态替换Transport初始化 |
| shim provenance |原fake_sdk必须false检查保留；走真实shim但链接显式Fake C符号，不等于device SDK |
| 首pair close |改为首pair关闭后第二facades仍使Runtime Busy；避免误关sibling |
| 最后close与清理 |第二pair关闭后原最终Close及NoResidue检查保留；新增原descriptor不能reopen，强化终止合同 |
| 新Borrowed fixture |撤回新引入的“重复兼容descriptor必须成功”期待，改同一原descriptor供两views；保留reverse init、第三拒绝、独立close及终止检查 |
| 旧函数tail和顶部说明 |从assembly-hook注释至EOF原函数tail全部byte-identical，SHA `12e8c9bdc7d93c83e1690f3d4501e32319995a44cfec9fe7d6d3b149314d7092`；顶部注释仅更新新容量描述 |

新版完整测试SHA `205a3853076a0a56276926797b9f9b40ad1128c63c04d47e7331a88146bf7608`。这不是删除失败以掩盖问题：原文件control及其OS1/OS2失败保留；新contract通过与旧contract冲突分别报告。

## 独立审核与最终执行

DI02 INITIAL完成后，一名独立GPT-6.1 Sol reviewer进行了Tier3三pass审核，无validatedP0–P3；保留残余风险，无review代码修复。随后DF01 FINAL复测实际工作树冻结源码。无finding不代表正确性证明或发布授权。

| 最终范围 | 结果 |
| --- | --- |
| 新default/observer cases |9/9 OS0：borrowed、allocation、cross-close、pending-refund、unique-stop、observer-throw/reentry、start-failure、observed |
| currentbaseline / independent extra |均OS0；后者覆盖empty-seed并发2winner/第三拒绝/last-close race与A关闭后pending成功，Transport级无SDK调用 |
| component |188/188 CTest0，使用新版契约；有限3.6接口组件，不是完整3.6 DDS库 |
| stock |38/39 CTest8，仅原`stock_transport.views.domains` OS1 |
| 原control |OS2真实FAIL，不混进通过数 |
| 全部functional children |239=237OS0+原domainOS1+原controlOS2；实际ordinary AppContainer0/Elevated0，Popen.wait/GetExitCodeProcess记录一致 |

normal和observed各完成同topic四remote crosspairs/八方向，精确remoteGUID+payload共20takes(8/4/8)：初始8、独立关闭A0后4、替代后8；replacement writerGUID变化，native计数保持。normal `default.allocation` 的24次实际facade bad_alloc发生在unused seed、live_views==0/noSDK阶段，验证未发布预留退款（DF01 `native/default-allocation`）。独立的Persistent `multi.allocation` 回归在DF01 fullstock `native/h029` 实际执行，24次失败后仍保留1个既有view，后续primary/新增pair继续精确traffic；不能称24次注入成功，也不精确宣称首个MakeDescriptor fail0的allocation site已被定位。正向Runtime关闭后仍有3个RetainedNotQuiescent IDs，不是SDK资源全清零。metadata-only控制的free8/created0/nativecalls0是另一scope。

原domain621bytes块SHA `a1266107a4b5582fe5a5c2ee16139e5a5e395bef2ff5700a1e9b2ae73fa856fc`完全保留，首对独立Runtime151/152在第二views之前已match，不被生命周期限制遮蔽；显式selected151 guard不是自动ROS_DOMAIN_ID映射。新的`developer/default-domain-plan-d01`仅下一阶段只读计划，NOT_IMPLEMENTED/NOT_RUN，不算当前修复。

## 构建、重放环境与观测限制

DI02初测有fresh当前Host和SDK构建；DF01没有编译/链接，将25份同SHA exe复制到`build/r39-dp-tf01`后在新cwd/log重放。observer与ordinary有各自一致ABI库和测试TU；宏ABI/actual CL inputs只做DI02证据hash-validated reuse，没有制造最终新CL记录。stock为真实Fast DDS2.14.5/FastCDR2.2.5，底层仍Host Fake public Socket；不能与有限3.6接口组件混称。

**RAW_ENV_NOT_IDENTICAL_227**必须单列：12/239 raw environment完全相同；227个CTest子进程PATH少一轮精确重复的stock/ROS2/pixi目录triplet。239/239非PATH变量、参数、timeout、二进制hash均同；只为比较搜索顺序，移除精确重复目录并保留第一次出现后，239组有序目录相同。原始receipt与环境未重写，不能说raw env全同，也不能用PATH等价代替外部DLL采样。快速进程外部module miss仍UNKNOWN；self OS path检查与外部DLL filehash证据分开。

preflight曾把历史PI01的1522与当前DI02的1501 binding误alias，修复后两个独立seal均核验；seal helper误查新CL后改按DI02 ABI reuse；rawPATH全等比较断言失败按上述限定诊断。失败目录与原始输出保留，无功能oracle修改或重跑，不将辅助失败包装成代码失败或删除。材料性限制在CASE_REPLAY_DIAGNOSTIC/COMPARISON和OBSERVER_ABI_REUSE中可追溯。

SDK DI02 fresh静态21TU/21AArch64objects/21archive members/708inputs，archive SHA `87eb80571b3f0f2fd3e67adcc270d767aaa273bb25c4b757fa612aa58c3865c8`。DF01为NOT_REEXECUTED_VALIDATED_REUSE，无新SDK编译、共享link、targetentry、GOT/ABI或device资格。旧UNKNOWN构建结论不改PASS。

## 命令、产物与原始证据

下列路径均相对仓库：D=`build/r39-ros2-complete-matrix/recovery-20261023`，F=`D/df01-default-final-d01`。命令是已封存实际执行记录，本docs阶段未执行测试。精确argv/cwd/timeout/environment见各intent，实际OS退出/普通token见其RESULT与PROCESS_AUDIT；完整targeted和CTest环境另引用`D/docs-updater-default-d01/ACTUAL_COMMANDS_AND_ENV.json`。新doc的目录链接使用仓库中的原始记录，不能以显示的PATH名称推断实际DLL。

- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-dp-tf01\c\experimental_runtime_tests.exe`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\df01-default-final-d01\work\current-baseline`; intent `F/native/current-baseline/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-dp-tf01\h\stock_transport_default_participant_tests.exe cross-close`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\df01-default-final-d01\work\default-cross-close`; intent `F/native/default-cross-close/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-dp-tf01\x\default_extra.exe`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\df01-default-final-d01\work\independent-default`; intent `F/native/independent-default/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-dp-tf01\h\stock_transport_default_observed_tests.exe observed`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\df01-default-final-d01\work\observed`; intent `F/native/observed/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-dp-tf01\x\c018_original.exe`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\df01-default-final-d01\work\original-control`; intent `F/native/original-control/intent.json`.
- `C:\pixi_ws\.pixi\envs\default\Library\bin\ctest.exe --test-dir C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\df01-default-final-d01\ctest-c -C Release --output-on-failure -V`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\df01-default-final-d01\ctest-c`; intent `F/ctest-c/ctest.intent.json`.
- `C:\pixi_ws\.pixi\envs\default\Library\bin\ctest.exe --test-dir C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\df01-default-final-d01\ctest-h -C Release --output-on-failure -V`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\df01-default-final-d01\ctest-h`; intent `F/ctest-h/ctest.intent.json`.

- DF01最终RESULT (historical local evidence unavailable in this checkout: `../../recovery-20261023/df01-default-final-d01/RESULT.json`) SHA `51fbfc884184ac780f20ae0f430ed46aab1e49ccc32663b643b4e7dfa9666318`；REPORT (historical local evidence unavailable in this checkout: `../../recovery-20261023/df01-default-final-d01/REPORT.md`) SHA `087f03caf75e81cdb9ba316af80af93c972e192843096d5d897bd2c476cd1886`；release1411成员 SHA `ddc99f8a1e95e1577dd500198f3c9fbc1cb762cde4539f39dede0f7c3c3d034f`；REPLAY25产物SHA `186adbc08d8cb892d4a5c2d35c3572d965ed7a901e2b8e9b1f4e16fee3f32a06`。
- 原control及三分结果 (historical local evidence unavailable in this checkout: `../../recovery-20261023/df01-default-final-d01/BASELINE_THREE_WAY_RESULTS.json`)、ASSERTION_AUDIT、ORIGINAL_CONTROL_SOURCE/RESULT、DEFAULT_EXECUTION_LEDGER、CASE_REPLAY_COMPARISON/DIAGNOSTIC、OBSERVER_ABI_REUSE、SDK_REUSE分别约束失败、断言、路径和复用范围。
- DI02初测 (historical local evidence unavailable in this checkout: `../../recovery-20261023/di02-default-initial-d01/RESULT.json`)，当前release1501/BUILD_host/BUILD_SDK及原DI01失败独立保留；独立review (historical local evidence unavailable in this checkout: `../../recovery-20261023/review-default-r01/RESULT.json`) SHA `650c676a8896fbeee1025ab4f415d89c918eccc4610abf3723cdc773e7c86951`，manifest83 SHA `7b0f86f1c4bf2011a7eb9e756126dfac0b42e4b37741055500a1c07b05438deb`；Main交接审计 (historical local evidence unavailable in this checkout: `../../recovery-20261023/main-default-docs-handoff-d01/RESULT.json`)核1411+25+83+18及867source。
- 最新HDC availability (historical local evidence unavailable in this checkout: `../../recovery-20261023/main-default-board-availability-d03/RESULT.json`) SHA `8197a3325202518c5d162113392be7a357704fe2c937065e6725849aa75339e9`，manifest18 SHA `8ef6ec4d497a5337bc8192bc118fcdb67b9d2048fa79278f0129496065d419bd`；2026-10-23 19:48:36UTC实际ordinary0/0，三条host命令exit0，两板nonce+aarch64：A3e01ff55454d202020104033bf453b00、B3e01ff55454d202020104433991c3b00。仅USB shell可用性，不是通信或独立远端exit测量。
- 本阶段文档校验 (historical local evidence unavailable in this checkout: `../../recovery-20261023/docs-updater-default-d01/RESULT.json`)保存source/Git前后、原README/matrix bytes、三docs差异/后缀、UTF8/相对链接核验与release。旧docs封存不改写。

## 未完成的验收与提交状态

当前37finite Jazzy双板通信accepted0，DEVICEfalse/CROSSfalse。旧R38patched1801 PASS及旧loader-only不提供本次stock资格；QoS/service/action/bag等当前stock设备验收仍未完成。security build OFF，max2/onepeer及flow/topology限制保留，自动ROS_DOMAIN_ID缺失。默认nohandle8次负返回耗尽原FAIL是历史保留，DP final未重跑该原probe；defaultfalse的独立opt-in same12/distinct12仅历史Host结果，须整个部署（含freshRuntime）native-name→完整identity/domain/peer/package/QoS/config映射不变，无globalregistry保证。8/16预算不扩，SDKdrain/name cleanup/nativefullclose-recreate和latecallback quiescence仍UNKNOWN。

GitNexus cache stale、UNKNOWN/lowerbound/globaltruncation及历史CRITICAL110/99direct未解决，无clean graph gate；无提交故不声称precommit clean。W仍detached123051e6abcc55db0712befd71eca0c7670f681f；Root仍f1914c38d60f77cc620404b9563da87f73336838、branch claude/dsoftbus-transport-20260924，origin git@github.com:Jiusi-pys/FastDDS-dsoftbus-transport.git，用户授权发布ref codex/flow-encoding-published-20261014只是授权范围，不是本轮推送结果。无R39stage/commit/push/newSHA/CI。Root189用户改动/原diffcheck2保留，W53/staged0/diffcheck0为写前状态。文档同步仅三份指定文档，生产/测试不变，正常gate仍FAIL，Ready for commit:NO。
