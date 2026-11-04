<!-- R39_DOMAIN_CANONICAL_20261024; this current addendum supersedes current-state claims in the preserved dated body below. -->
# Current R39 domain circuits: canonical Host scope (2026-10-24)

**Implementation: PRESENT. Fresh Host configure/build: PASS. Canonical selected Stock Host tests: 56/56 PASS, each registered case executed exactly once. SDK23 product22 build/install/relocated-consumer link: PASS; canonical A/B loader metadata: PASS in the declared shared-C++ host only; strict ABI: UNKNOWN. SDK_COMMUNICATION_TESTED=false; CROSS_DEVICE_COMMUNICATION_TESTED=false; ROS2_INTEGRATION_TESTED=false. Overall: NOT_VERIFIED. Normal Commit gate: FAIL. Ready for commit: NO.**

The independent final batch is [domain-canonical-final-d02/TEST_RESULT.json](../../recovery-20261023/domain-canonical-final-d02/TEST_RESULT.json), with [receipts and manifest](../../recovery-20261023/domain-canonical-final-d02/MANIFEST.json) (SHA256 `6e958084407f188c314073943d86d4a1f5c5438d908aacc8d4e92c73cac9d867`). Tested source dictionary: 873 files, SHA256 `8922c3a8f819ed118b44d79e48bee6df9cbda6e73c57f96efb58495936b01ab0`. This remains the canonical Host execution provenance, not a new Host rerun. Current post-product source inventory is877 (pre-doc SHA256 `1fff45b332d69a64db603e24e86f4e157cf7d4ef64c89b8cde8ac62d09036303`); the five product source paths legitimately differ/add to that older Host inventory. This documentation sync changes only five Markdown files, leaving all872 other current files identical to the final877 snapshot.

## Current product package and canonical loader metadata (2026-10-24)

`BUILD_STOCK_TRANSPORT_OHOS_PRODUCT` is implemented, default OFF. It requires the real OHOS toolchain, shared SDK C++ runtime, exact Fast DDS2.14.5/FastCDR2.2.5 and explicit hash/ELF-qualified real Socket provider; this build option does not enable the native quota opt-in. Current Runtime20/Observation/Bootstrap and old tests are unchanged by the five-path product increment (stock CMake, package config, pure-C loader and two consumer files).

[Corrected product initial execution](../../recovery-20261024/tester-product-fixed-initial-d01/RESULT.json) and the [first product final](../../recovery-20261024/tester-product-final-d01/RESULT.json) built all22 current product TUs including Bootstrap, performed native strict link (`-z defs`, `--no-undefined`, `--no-allow-shlib-undefined`), installed and compiled/linked the relocated installed consumer. Product SHA256 `91456513c56dcef49b87ec18fd0211d6b15bd7ad3f051118b064d1ac2d0a8883`; actual unversioned SONAME `libfastdds_dsoftbus_stock_214.so`, RUNPATH `$ORIGIN`, seven direct dependencies. This advances product build/package/link evidence; the consumer was not executed. The original initial consumer literal-newline failure and absent SONAME/RPATH remain FAIL. Seven intended negative guards passed; unsupported-host guard was NOT_REACHED after an earlier toolchain-control failure, not PASS.

The first product-final pure-C-linked metadata loader on A (PID15157) printed dlclose0 then exited139/SIGSEGV; B was skipped. [Read-only teardown diagnosis](../../recovery-20261024/developer/product-teardown-diagnosis-d01/REPORT.md) correlates IPC registry teardown through `_ZdlPv` with three stale SDK allocator GOT entries. This is a loader C++ provider/lifetime failure, not an observed Socket/domain-traffic failure; general SDK unload safety remains UNKNOWN. Old LP02 eight normal exits retained dlopen handles and do not prove current unload safety. A separate same-C-object executable-root SDK C++ control exited0; it is diagnostic support, not the canonical fix or unique-cause proof.

The [four-line CMake correction](../../recovery-20261024/developer/loader-abi-owner-fix-d01/) selects ordinary `LINKER_LANGUAGE CXX` for the loader while preserving pure-C compilation, main/dlopen/dlsym/dladdr/dlclose/normal return. This models the declared shared-SDK-C++ consumer host: no forced runtime retention, NODELETE, skipped close, `_Exit`, allocator wrapper or higher-layer change. Actual executable root NEEDED `libc++_shared.so` and `libc.so` was checked. [Canonical initial A/B PASS](../../recovery-20261024/tester-loader-owner-initial-d01/RESULT.json) was followed by [independent review with no actionable findings](../../recovery-20261024/reviewer-loader-owner-d01/REPORT.md) and then the authoritative [post-review final](../../recovery-20261024/tester-loader-owner-final-d01/RESULT.json), manifest SHA256 `b567d2d6acda101a87f7042f14c753ee3ebc643311b416c2e0a9a6ea5140d81f`.

Final execution freshly compiled only the C loader and linked via the standard C++ driver (native0), producing loader SHA256 `0225a138175d14e10844d18ad357f78ee8ed5b4c540e91070ab05c2d56b30751`. Product91456513 and its22 source/flags/generated-rule inputs were revalidated and REUSED, not rebuilt; no new install, consumer execution or Host56 rerun. A PID19232 and B PID27151 each launched exactly once, completed real provider-address/current-product checks, dlclose0 and native exit0; postflight file/system hashes matched and diagnostic processes were absent. Remote metadata-only directory: `/data/local/tmp/fastdds-r39-product-loader-20261024-owner-final-72c71618b56e`. Exact commands, environment, logs and native receipts are in [final local receipts](../../recovery-20261024/tester-loader-owner-final-d01/RECEIPTS.json) and [board receipts](../../recovery-20261024/tester-loader-owner-final-d01/BOARD_RECEIPTS.json).

`TARGET_LOADER_METADATA_TESTED=true` only in this declared shared-SDK-C++ host contract. `SDK_COMMUNICATION_TESTED=false`, `CROSS_DEVICE_COMMUNICATION_TESTED=false`, `ROS2_INTEGRATION_TESTED=false`; strict ABI remains UNKNOWN. No explicit Runtime/Participant/Socket/BX06/consumer execution, token selection, chmod or security/network configuration change occurred; dependency-initializer side effects are not proven absent. SDK23 reports6.1.0.32 versus board6.1.0.31. dlsym addresses do not prove GOT bindings, C++ object ABI, native close/recreate or SDK drain; static weak gaps169/C++ overlaps464 remain incomplete ABI evidence. Runtime lifetime and global lookup order changed together and before/after maps were not observed; do not infer a unique cause or universal C-only embedding ban. Original C-only native139 remains FAIL. Exact SetSelfTokenID confirmation stays PENDING, untouched. This loader ABI build contract does not fix default native-ID capacity.

Earlier [runtime21 compile](../../recovery-20261023/domain-runtime-sdk23-compile-d01/RESULT.json) and [standalone strict diagnostic link](../../recovery-20261023/domain-runtime-strict-link-d01/RESULT.json) remain dated build evidence. The product/canonical loader stages above supersede their old no-product-link/no-loader-execution status only within the stated scopes. Normal project Commit gate remains FAIL; Overall NOT_VERIFIED; Ready for commit:NO.

Generic ordinary/Persistent `MakeDescriptor` with absent expected domain now waits for a usable real public actual-DID callback to bind each view's bounded circuit. Explicit expected domain, including engaged zero, and `MakeStockDescriptor` remain NativeBound; a genuine wrong actual domain becomes sticky Refused and denies actual Sender/provider traffic. Missing/unsupported callbacks remain Await and fail closed. `ScopedTransportBootstrap` still calls MakeStockDescriptor with fixed explicit configuration: automatic natural ROS_DOMAIN_ID operation without extra configuration is not established. Current capacity is two views/two domain circuits per Runtime; this is an implementation bound, not a necessary SDK single-domain restriction or complete ROS2 multi-Participant support.

The older dated sections below are historical evidence. In particular their configured-domain default policy and original failures are not the latest generic policy, and no historical FAIL is retrospectively changed to PASS. R38 board successes retain their original provenance; they are not new R39 communication acceptance.

## Input lifecycle repair and review disposition

[Original independent review](../../recovery-20261023/reviewer-domain-nativebound-d01/REPORT.md) found P1 receiver publication versus CloseInput and P2 missing receive lease on same-facade reopen. The [unmodified independent repro](../../recovery-20261023/domain-input-repro-initial-d01/) remains an actual FAIL: Open/callback reported success/Ready but replacement/app count stayed zero despite a fresh forwarded marker81.

Current HostLoop captures receiver/incarnation under the binding transaction; CloseInput retires under synchronization then releases locks before controls/drain; reopening an already bound circuit attaches a fresh lease. [Repair review](../../recovery-20261023/reviewer-domain-input-fix-d01/REPORT.md) found no additional actionable issue. Targeted9 PASS and canonical56 PASS include the unchanged repro with marker81 exactly once at the new receiver, old receiver stable and app lease1; twelve input cycles101..123 retain stable native IDs/count3 and retire old admissions. These cycles reuse the same facade/native anchor, not full native destruction/recreation. Reentry observes actual AppDetaching, successful same-DID callback, refused callback-context close and eventual close completion. The exact P1 post-Attach/pre-publication runtime schedule is NOT_EXECUTED; source transaction repair and these tests do not prove formal race freedom.

The proposed PumpSendImpl change was withdrawn and its entire original function restored. A target view can TX before its first RX only with the tested root-Await sibling-listener/provider condition (native peer90); the original whole-Runtime no-OpenTX/provider-null gate remains.

## Work contract migration and read-only observation

The [observation/Work independent review](../../recovery-20261023/reviewer-domain-work-observation-d01/REPORT.md) reports no actionable findings after seven Work PASS; its recommended test-only failed-close coverage was then added before the fresh canonical56 batch. This final evidence supersedes pre-review execution for current acceptance. The original three review reports and developer/Main decisions remain separate immutable evidence.

A separate bounded circuit DTO, nonvirtual getter and ProbeAccess bridge expose values only. Old HostLoopObservation, BoundedConnectionSnapshot and old getters remain unchanged. New consumers link rebuilt archives; SDK ABI compatibility is not established. References/status are pinned under root gate and sampled afterward. Adapter details are available only for closed roots with successful status/cleanup, drained adapters and no observer fault. Active/failed roots return unavailable adapter details while native usage is independent. Observation groups are non-atomic and Available does not imply business Ready.

The new explicit-zero failed Socket(-7654)/close0 test records rootClosed1, Available/count1/domain0, native present/adapter unavailable, terminal-adapters false; native owners1, retainedIDs0, NQ0, FailedRetained1, callbacks0. This is represented domain0 metadata and a retained no-handle owner, not domain0 DDS traffic, complete resource clearance or SDK quiescence. Observer-fault/no-circuit branches remain unexecuted.

Work diagnostics preserve the old actual failures: before DID, live2/listens1 per side/Bind0/both Await conflicts with old full6. Five ordinary tests now perform full6 after real confirm_raw151, keeping generic descriptors, Await Sender denial and other checks; unsupported mode retains its original prewarm full6. Cleanup applies the exact original used-entry predicate (SHA256 `2c427f1c408a405aa3f58478ea0049ee97e8314099dbe28ef2188503db7148ba`) to all three actual circuit records and Root.listener. All64 original check calls and helper remain verbatim; unused Root.binding/accepted explicitly require Empty/uncreated/no resources, and no allowEmpty relaxation is used for used entries. Circuit.listener is a metadata anchor, not another physical SDK listener. NativeUsage adds active-permit/callback-zero checks.

Circuit retained IDs2/NQ2 are included in aggregate Root3/NQ3, not five resources. Released owner references leave identity fields unavailable/default0; owner_closed while present means close_intent, not proof of live/dead owner or SDK silence. Natural FactoryA shutdown3/callback0 versus heldA shutdown2/terminal-callback1 both finish with Fake live/queued/bytes/invalid0; this is not all-native/callback-resources-zero or SDK-drain proof. See [startup migration ledger](../../recovery-20261023/developer/domain-work-startup-migration-d01/), [cleanup ledger](../../recovery-20261023/developer/domain-work-cleanup-migration-d01/) and [observer increment](../../recovery-20261023/developer/domain-circuit-observation-d01/).

## Canonical execution boundary

The 56 selected cases cover domain9, Work7, default-domain11, views6, normal5, circuit5, multi3, default9 and process-receive1. Fresh configure/build native exits were zero; every selected test native exit was zero. Children were ordinary Windows processes (AppContainer0, elevated0). Actual `fastrtps-2.14.dll` module path/hash was captured for 55/56 (SHA256 `68ed01951f54dd19907f05821be21341b6384750847845baf8aa3c68dc4660cf`); process-receive has no loader-helper print and is N/A for module-path proof. Selected CMake package files/hashes identify Fast DDS2.14.5 and FastCDR2.2.5; no actual FastCDR runtime module path was captured.

Exact executed argv, cwd, environment allowlist, PIDs, timestamps, native exits and logs are in [configure.start.json](../../recovery-20261023/domain-canonical-final-d02/configure.start.json), [build.start.json](../../recovery-20261023/domain-canonical-final-d02/build.start.json), per-case `*.start.json`/`*.receipt.json`/stdout/stderr, [registered selection](../../recovery-20261023/domain-canonical-final-d02/REGISTERED_SELECTION.json) and [artifacts](../../recovery-20261023/domain-canonical-final-d02/ARTIFACTS.json). These are executed commands, not examples or a claim that docs_updater ran tests.

The supplemental [remaining-regression record](../../recovery-20261023/domain-remaining-regression-d01/TEST_RESULT.json) is historical source7109: 44 Stock cases, 43 PASS/one held original Root-predicate FAIL, plus 68 pure-model PASS. [Final input revalidation](../../recovery-20261023/domain-canonical-final-d02/SUPPLEMENTAL_MODEL_REVALIDATION.json) confirms unchanged model inputs/artifacts/options/header/link identities on current source8922; it is not a new model execution. The models consume 31 headers/20 project-source closure from an existing interface fixture (HEAD `343f155c36b561db3d9f047a65d866ce0f3da300`, version3.6.2.0), zero Fast DDS implementation C++ and no DDS implementation binary. They do not establish Stock DDS integration or a patched DDS binary.

Root CONFIG HostLoop/shared/input-close/provider/public-shim regressions remain BLOCKED: root requires exact3.6.2.0 whereas this Stock project uses mandated2.14 and does not add the root project. No version check was weakened, no3.6 binary fallback was used, and SOURCE_PROBE models are separate. Real SDK/DSoftBus communication, dual-board, ROS2 Jazzy services/actions/QoS/serialization/large-payload/concurrency acceptance remain outside this new execution scope. Preserving upper protocol code is not an automatic feature test.

## Remaining required gates

Default quota8 no-handle-failure exhaustion/9th no-SDK-call remains unchanged; opt-in twelve loops require full immutable deployment/name/identity/collision prerequisites and do not solve default capacity. Retained slots/entrances/IDs and void native Shutdown do not establish silence. New exact SDK23 SetSelfTokenID `mdds` profile confirmation is PENDING: prior read-only permission/package/GetSelf evidence is not current identity selection or a board Socket BYTES run. BX06 prepared SDK BYTES package remains HostBuild-only with confirmation card pending. No new identity selection, deployment or board communication acceptance is claimed.

GitNexus cached impact remains stale/UNKNOWN with historical CRITICAL110 not waived. New getter lookup is unresolved; text/source confirmation supports documentation only. No clean-index or detect_changes claim is made; no commit gate is passed. Required root regressions, SDK/native communication, RK3588A/cross-board and full ROS2 validation remain incomplete.

---

Historical dated record (preserved verbatim below):

# DG02：默认 Runtime domain guard 阶段记录（2026-10-23）

**Implementation PRESENT；Build HOST PASS；Host有限范围PASS；RK3588A/CROSS_DEVICE_TESTED=false；Overall NOT_VERIFIED；正常Commit gate FAIL；Ready for commit:NO。** 本阶段继续“完整实现ros2功能，不改变transport以上”的目标，仅同步本轮证据，不宣布完整ROS2完成。

## 实现合同和安全默认范围

精确增量见 `main-default-domain-review-artifact-d01/INCREMENTAL.patch` 和 SCOPE.md 的七路径；生产仅 `src/ExperimentalRuntime.cpp` 与 `ExperimentalRuntime.hpp` 合同，其余为测试/CMake适配。写前869条source字典SHA `c7f102ee8d53b4ce9e9633c89ee71b8529375cfd64519ff138b27b6f293b3d04`，与DG02 final列表映射相同。

MakeDescriptor在ValidateV1及原native binding通过后，从单一已配置native domain primitive填缺省expected_domain_id；domain0仍为engaged，不因0当缺省；显式冲突拒绝。同一canonical同时用于Start和descriptor，normal、observer、Persistent及legacy Runtime均进入guard。facade只能等待后续真实public domain callback；Factory/init/native-ready成功不等于domainReady。callbackless/empty/unsupported peers仍Await，mismatch为stickyRefused，不把缺省expected当actual确认。standalone低层descriptor及opaqueBootstrap不在Runtime安全默认保证内。仍保留normal单次MakeDescriptor签发、固定双view与onepeer；没有自动ROS_DOMAIN_ID/DDS Context/native-domain选择，也未证明same-Runtime local edges。

## 断言迁移与保留的失败

独立账本 `ASSERTION_CHANGE_LEDGER.json` 与DG01 ASSERTION_AUDIT区分以下变动；旧账本中的implementation-time pending/NOT_YET_EXECUTED不是最终执行结论，原control后来已实际执行且FAIL。

| 用例 | 改动及保留范围 |
| --- | --- |
| domain_scope_tests::DomainTransport |仅两条旧行替换，先实际SenderResource拒绝Await，再由raw driver显式literal151调用public hook；原payload/marker/order/身份/receiver/held/Close/cached sender/Refused/cleanup正向检查保留 |
| runtime_work_tests |原行全部按序保留；插入实际Await sender拒绝和driver-selected151 hook后继续原coalesced payload/order/budget/thread/inflight/close/late-ID oracle，Factory侧真实callback未代替 |
| HostSocket RX fixture |原行全部按序保留；同样新增Await sender拒绝及明确raw151 callback，原六帧及LOCAL_BYTES262144/4records、顺序、交付/释放/exports/close检查保留 |
| 新default_domain target |新增assembly/domain0/冲突、allocation、raw及真实stock正负边界，不删除原失败oracle |

原raw top和递归7个旧helper字节绑定当前library实际OS1，精确FAIL为`scoped legacy send changed`/Await；不能称原baseline全PASS。原whole runtime_views/default_participant测试未改，T2Domains621B/a126 SHA `a1266107a4b5582fe5a5c2ee16139e5a5e395bef2ff5700a1e9b2ae73fa856fc`完整保留。DG执行顺序是跨domain WaitNone通过→native151/actual151 group0实际2writes/2takes→fixednative151/actual152正向reader matchFAIL。DP原首pair跨域matchFAIL属于旧实现历史，DI01原baseline/precompatOS1、DI02原baseline/currentOS2及加强契约OS0也是独立历史，不被本轮覆盖或冒称重跑。

## 独立最终测试与真实构建范围

DG01 INITIAL→独立Tier3三pass审核/no validatedP0–P3→DG02 FINAL已顺序完成且source未变；review不是正确性证明或提交授权。DG02复制27份相同哈希exe到新 `build/r39-dg-tf01`，无新compile/link，在新cwd/registry/log真正执行257个functional children=255OS0+原rawOS1+原整domainOS1，全部真实AppContainer0/Elevated0及Popen.wait/GetExitCodeProcess一致。

| 执行范围 | 结果及资格 |
| --- | --- |
| targeted / RX |19targeted实际OS0；RX单独实际OS0 |
| component |188/188 CTest0；c实际540inputs/84obj/root3.6读取32，仅有限3.6接口组件 |
| stock |47/48 CTest实际exit8；唯一原full domainOS1；outer PowerShell1不是CTest码 |
| h / RX build bindings |h642inputs/58obj/root3.6=0；RX468/26/root3.6=0，真实stock2.14.5/FastCDR2.2.5和独立observed ABI |
| normal / observer cross |各20remoteGUID+payload takes8/4/8，replacementGUID变化，Host Fake Socket，不证明local edges或板通信 |
| Windows HostSocket RX |opaque6sends/6ingress/4held/1LocalBytesrefusal/5delivered/5released、joined close；非SDK、非ROS数据 |
| allocation |新28次连续真实bad_alloc失败后首次success index28，仅该path；size200/index26只是候选，未精确归因guard类型。原normal unused-seed24本轮也执行，不混作Persistent existing-view或native重建证明 |

此前记录的原始环境白名单字段（含rawPATH）257/257相同；历史未记录的完整OS环境未知，不声称全环境字节相同。args_after_exe/timeouts同，cwd符合old→fresh映射而非字符串相同。首seal helper断言失败保存于seal-attempt01：236个exe路径正反斜杠写法不同；绝对resolve、严格freshroot、size/hash证明同一copy。原receipts/oracles不改，无功能重跑。DG01 RX helper的WindowsPath.lower在Popen前失败也保留，随后只执行一次RX，不算产品失败。NativeToolhelp外采样miss仍UNKNOWN；self module path不是外部memoryhash。

SDK初测DG01新archive SHA `88b73d49f8e7d1c1a94e26c438bbf16028f563834041bde4d5c3dbfd2a7c478f`，21TU/21AArch64objects/21member/708inputs。DG02仅NOT_REEXECUTED_VALIDATED_REUSE，非本轮最终新compile，非旧DP87eb产物；无sharedlink/targetentry/GOT/C++ABI/device资格。

## 可复现命令、环境与原始证据

路径相对仓库：D=`build/r39-ros2-complete-matrix/recovery-20261023`，F=`D/dg02-domain-final-d01`。以下是封存的实际执行命令，本docs阶段未重跑；精确argv/cwd/timeout/此前记录的环境字段见各intent，真实退出/token见同目录RESULT及PROCESS_AUDIT。完整七条记录在 `D/docs-updater-default-domain-d01/ACTUAL_COMMANDS_AND_ENV.json`，已核文件存在。每条命令只列一次。

- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-dg-tf01\h\Release\stock_transport_default_domain_tests.exe assembly`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\dg02-domain-final-d01\work\dg-assembly`; intent `F/native/dg-assembly/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-dg-tf01\h\Release\stock_transport_default_domain_tests.exe raw`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\dg02-domain-final-d01\work\dg-raw`; intent `F/native/dg-raw/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-dg-tf01\h\Release\stock_transport_default_domain_tests.exe positive152`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\dg02-domain-final-d01\work\dg-positive152`; intent `F/native/dg-positive152/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-dg-tf01\h\Release\stock_transport_domain_tests.exe transport`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\dg02-domain-final-d01\work\domain-transport`; intent `F/native/domain-transport/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-dg-tf01\rx\Release\ros2_host_receive_socket_fixture.exe 78baf0cea6239e6f 53685 C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\stock-host-d01\install\bin\fastrtps-2.14.dll C:\pixi_ws\ros2-windows\bin\fastcdr-2.2.dll`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\dg02-domain-final-d01\work\rx-fixture`; intent `F/native/rx-fixture/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-dg-tf01\x\Release\original_raw_domain.exe transport`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\dg02-domain-final-d01\work\original-raw-control`; intent `F/native/original-raw-control/intent.json`.
- `C:\pixi_ws\.pixi\envs\default\Library\bin\ctest.exe --test-dir C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\dg02-domain-final-d01\ctest-h -C Release --output-on-failure -V`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\dg02-domain-final-d01\ctest-h`; intent `F/ctest-h/ctest.intent.json`.

- [DG02最终RESULT](../../recovery-20261023/dg02-domain-final-d01/RESULT.json) SHA `65b58e2d0c2f4544ead56276ac44a5e206ebc050477e3b526cee6e1766ce87a7`；[REPORT](../../recovery-20261023/dg02-domain-final-d01/REPORT.md) SHA `66018b07b5d50041a00cda54ee67373f22a120adeb18f88f142c7e3f987263b8`；RELEASE_MANIFEST1494 SHA `9ad1a9a0ceb0de7a082101ae2d2fa28cee63fd4ab34819d76d5720b548e1d589`；REPLAY27 SHA `e2ae3794271096cc00e43effe528d69f18bbc36af78bf3c42f8533e47298c199`。REPLAY_EQUIVALENCE、RX_RESULT、EXECUTION_LEDGER和SDK_REUSE绑定范围与实际失败。
- [DG01初测](../../recovery-20261023/dg01-domain-initial-d01/RESULT.json) SHA `c3cd12242d2447b4e07cae3719b734235c74ab58827842520ae243da76217f55`，release1625 SHA `1e11d69ca3b1a514407932c4839aa7c3a9172f95b73144e057f4a80a028338d4`；[独立review](../../recovery-20261023/review-default-domain-r01/RESULT.json) SHA `57e66f06d12f2426f19d608edc99dcf4b55fe5faff2a51c339a5c317d5c25b86`，manifest98 SHA `890fe9b6d75d9a93303cb660532f9e954def5a732e5fc32021762bc8c820ede1`。
- [Main交接审计](../../recovery-20261023/main-default-domain-docs-handoff-d01/RESULT.json)核final1494/replay27/review98/initial1625/Host988/SDK37/artifact56/HDC18/nativeLookup6和source869；[文档同步校验](../../recovery-20261023/docs-updater-default-domain-d01/RESULT.json)保存三docs delta、完整suffix、source/Git前后与release。
- [HDC d04 availability](../../recovery-20261023/main-default-board-availability-d04/RESULT.json)：2026-10-23 20:58:28UTC，两板nonce+aarch64，普通上下文0/0、三条host命令exit0。A3e01ff55454d202020104033bf453b00/B3e01ff55454d202020104433991c3b00，仅USB/shell时间点，无Runtime/Socket/DDS/ROS2板测或独立远端exit测量。
- [原生后续只读查证](../../recovery-20261023/main-dg-native-plan-d01/READ_ONLY_LEDGER.json) SHA `ea8d11733feac2c3eb53bdd8c7a301cd2c44c71e38ab6c11c2f972e436eaa8c4`，manifest6 SHA `6afad6479a1efef19e31d2ab0f9d3f414288f48e264ac0b807ef07c79ddb1f5a`：stockBootstrap只有publicQoS C++接口；旧TargetBridge.cpp第3及197–203行依赖patchedFactory，不能直接复用为stock入口。最小current link/stockentry/GOT资格尚未执行。

## 未验收项目和提交状态

当前37finite Jazzy双板accepted0，DEVICE/CROSSfalse；旧R38patched1801 PASS及旧lc03 loader8（old82ad archive/旧DSO）不资格当前88b7实现。SDKsharedlink/入口/GOT/ABI及真实Socket/DDS/ROS2通信仍缺。QoS/service/action/bag等未完成当前stock设备验收，security build OFF、max2/onepeer及flow/topology限制保留。

默认8次negative Socket耗尽未修，本轮没有重跑该容量probe；独立opt-in仍默认false，只在整个部署/freshRuntime native-name→完整identity/domain/peer/package/QoS/config映射不可变的强前提下有历史same12/distinct12 Host证据，无globalregistry强制。positive3RetainedNotQuiescent IDs、SDKdrain/namecleanup/nativefullclose-recreate及latecallback quiescence仍UNKNOWN；拒绝正常业务或failclosed不能称全功能。

图的LOW+dropped只是下界，UNKNOWN/stale/truncated及历史CRITICAL110/99direct未解决，没有clean graph门禁。W HEAD仍detached123051e6abcc55db0712befd71eca0c7670f681f，Root仍f1914c38d60f77cc620404b9563da87f73336838；用户原有Root189/staged0/原diffcheck2保持，W写前56/staged0/diffcheck0。没有R39stage/commit/push/newSHA/CI。文档同步只增加README/matrix前缀并新增本报告，旧正文完整byte suffix、生产测试及旧DP文档不变；同步不使正常gate通过，Ready for commit:NO。
