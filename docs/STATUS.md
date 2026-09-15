# Project status

## 最新证据同步（2026-09-14 UTC）

Fast DDS 主机库已完成当前准备构建：`.artifacts/build/fastdds-logs/HOST_BUILD_RESULT_20260914.md` 记录实际 237/237 编译和 install exit 0；该构建使用已记录的 UTF-8/兼容性编译选项，失败日志仍保留。此证据只支持 `BUILDS`，不支持 Fast DDS Transport、DDS 集成或上游测试通过。

S3-D2 RuntimeSendPath / DSoftBusSenderResource 发送资源桥接已完成限定范围实现，并由独立 final tester 和 code review 验证：`.artifacts/verification/runtime-send-final-20260914T0527Z/RESULT.md` 报告 direct Debug/Release 各 27 组、root Debug/Release 各 17/17、public consumer、SDK23 arm64 static build 均 PASS；review `.artifacts/review/20260914T052640Z-runtime-send-d2-code/REVIEW.md` 无 actionable finding。该结果仅覆盖发送计划、当前 `send_lambda_` 桥接、批量队列接纳、释放/关闭契约及消费者构建，不覆盖 native backend、DSoftBus SDK Send 或设备 Transport。

token-first 双板 Socket record 实验在有界范围 PASS：`.artifacts/diagnostics/token-first/20260914T051101677Z-execute/` 中 A/B 各方向均完成 32、255、4096、16384、60000 字节五条记录的字节/长度/计数校验；A Listen、B 同步 Bind、发送和关闭观察均有证据。`.artifacts/verification/20260914T045328Z-token-first-device/MAIN_AUDIT.md` 与 `final-readonly-20260914T0514Z/RESULT.md` 证明随后两板权限均撤销、身份记录保留且原始 JSON 哈希恢复。结果仍明确 `quiescence=NOT_VERIFIED`、DDS 未运行、Transport 未实现；不把旧 G1/G6 或此前 exit=6 尝试自动改写为本轮结果。

PLAN 15.2 thread-token diagnostic is IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED within its bounded scope. Independent two-board evidence is `.artifacts/diagnostics/thread-token/20260914T035011891Z-tester-device/`, with result review `.artifacts/review/20260914T035632Z-thread-token-device-results/REVIEW.md`; runner result is `.artifacts/verification/thread-token-runner-final-20260914T034800Z/result.txt` (retained label, actual UTC 03:48:30–03:48:31). Board A observed old worker 806246851 unchanged while the main thread used 671793191; B observed 806017503 unchanged while the main thread used 671320465. Both main tokens were restored, exact diagnostic PIDs disappeared, and persistent JSON/stat/label/guards were unchanged. This does not identify an actual DSoftBus callback token or prove the historical Socket failure root cause.

## 最新证据更正（2026-09-14）

S3-C `InputChannels` 已在 OOM 修复后由独立 final tester 重新验证：`.artifacts/verification/facade-host-final-20260914T075048953Z/RESULT.md` 报告 root CONFIG 20/20、SOURCE Debug/Release 各 19/19、facade repeat 20 次、FCD 21/21、IC-12 及 public consumer 均 PASS；状态为 `IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED`（限定范围），不扩展为 device-tested 或完整 Transport verified。较早的 14/14 结果保留为历史证据。

Descriptor facade 已实现并完成限定范围 host/SDK ABI 验证；native backend/connection manager 仍未实现。此前 S2 跨设备 exit=6 尝试及其审计限制保留为历史证据；本轮 token-first 有界 record 实验另有独立 PASS 证据，不能互相覆盖。CommSvc 旧线程 token 拒绝仍只是待审线索，不能宣称根因或修复；整体仍 `NOT VERIFIED`（完整 Transport 发布未就绪）；N1 限定执行门已 PASS，其提交门槛单独核对。

状态依据最终权限生命周期 tester run `20260914T005000Z-permission-lifecycle-final`、S3-A final `20260914T011000Z-s3a-final` 和 Locator wire final `locator-wire-final-20260914T013535Z`；S1 设备证据仍独立来自 `20260913T155515Z-final`。旧 Socket positive 尝试的审计结论见 `20260914T012000Z-record-positive-second/audit-current/TEST_RESULT.md`；RecordPairHarness 是独立的新 host/simulated helper，不能混同。

| Scope | Status | Evidence / boundary |
| --- | --- | --- |
| S0 baseline and plan | RECORDED / PARTIAL | `PLAN.md`、源码/SDK/设备基线已登记；完整 ABI/产品集成基线仍未完成 |
| S1 Fast DDS interface probe | PASS | Host CTest 中接口契约为 1 个 CTest（内部 IF-01..04）；OHOS arm64 build；A/B 设备 IF-01..04 各 4/4 |
| S1 DSoftBus symbol probe | PASS | A/B required and optional symbols present |
| S2 Socket capability check | PASS（本机已授权限定范围） / NOT RUN（跨设备） | 无授权历史对照 `20260914T000333Z-socket-recheck` 记录权限拒绝；当前 A/B 已授权 probe 均成功创建 Socket 并 Shutdown 一次；不覆盖跨设备通信 |
| S2 Bind/Listen/SendBytes/Shutdown contract | 有界双板 record PASS；quiescence 未验证 | token-first 实验 A Listen、B 同步 Bind 及双向五种记录通过；发送 ≤3 ms、关闭观察 202 ms。SDK callback 排空、并发取消/重连和持续负载仍未验证 |
| S2-P native 身份/最小权限探针 | DEVICE_TESTED / PASS（限定范围） | A/B 同 ID regrant → 独立 Socket 创建/Shutdown 一次 → revoke 闭环 PASS；服务 GRANTED→DENIED、self 还原、JSON guard、外部记录及 provider hashes 保持；不等于 Transport 通信通过 |
| PLAN 15.2 thread-token diagnostic | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED（限定范围） | 两板受控短命进程测量 PASS；旧线程保持原 token、新线程观察目标 token、主线程恢复；不含 DSoftBus callback 身份或 Socket 根因证明 |
| S2 record/route probe | IMPLEMENTED / HOST_TESTED / DEVICE_TESTED / PASS（有界） | token-first 独立执行与 Main audit：A/B 各方向五条完整记录通过，真实 PID 收尾并独立复核；权限已撤销、原始 hash 恢复。仅为诊断包络，不等于生产 Transport 或 callback quiescence |
| S3-A EndpointDirectory | BUILDS / HOST_TESTED（cross build） | `include/mdds/dsoftbus/EndpointDirectory.hpp`、`src/EndpointDirectory.cpp`；最终 Debug/Release consumer/library-only/cross 共 13 组 PASS，证据 `20260914T011000Z-s3a-final`；当前不标 DEVICE_TESTED 或完整目录 VERIFIED |
| Locator wire characterization | BUILDS / DEVICE_TESTED / REVIEWED（限定范围） | 真实 Fast DDS 3.6.2.0 Locator wire A/B 各 4 组 PASS，最终证据 `locator-wire-final-20260914T013535Z`，review `.artifacts/review/20260914T013118Z-wire/REVIEW.md`；不含 Socket/DDS discovery |
| RecordPairHarness | IMPLEMENTED / HOST_TESTED / REVIEWED（限定范围） | final host/simulated 测试通过，closure review 完成；无实际 SDK records 成功证据 |
| Record timing fix | BUILDS / HOST_TESTED / DEVICE negative TESTED（限定范围） | final `.artifacts/verification/record-timing-final-20260914T014906Z/`：Debug/Release 各 21 组、平台交叉构建通过；A/B 拒权模式真实退出 6、非法参数退出 64，符合负例预期。`BindReturned` 是 checkpoint，不等于物理 API 返回瞬间 |
| S3-B SendQueue / S3-D1 batch | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；NOT DEVICE_TESTED | final `.artifacts/verification/20260914T041229981Z-send-batch-final/`：direct Debug/Release 各 26 组、root Debug/Release 各 14/14、consumer、SDK23 arm64 static build PASS；review `.artifacts/review/20260914T040359Z-send-batch/REVIEW.md` 无 blocking。仅覆盖队列原子接纳与记录所有权移交，不含 SDK Send/Transport |
| S3-C InputChannels | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；NOT DEVICE_TESTED | OOM 修复后的 final `.artifacts/verification/facade-host-final-20260914T075048953Z/RESULT.md`：root CONFIG 20/20、SOURCE Debug/Release 各 19/19、FCD 21/21、IC-12、重复运行与 public consumer PASS；review `.artifacts/review/20260914T-facade-code/REVIEW.md` 无 actionable finding；不含 SDK callback 或完整 Transport |
| S3-D2 RuntimeSendPath / SenderResource | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED；NOT DEVICE_TESTED | final `.artifacts/verification/runtime-send-final-20260914T0527Z/RESULT.md`：direct D/R 各 27、root D/R 各 17/17、public consumer、SDK23 arm64 static build PASS；review `.artifacts/review/20260914T052640Z-runtime-send-d2-code/REVIEW.md` 无 actionable finding；不含 native backend、SDK Send 或 DDS/RMW |
| S3 real Fast DDS binding | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED（限定范围） | frozen eight-file slice：host SOURCE D/R 各 17/17、source consumer 1/1、CONFIG matrix 32/32、CONFIG root 17/17、flat 2/2、nested 4 组、poison/absent SOURCE 17/17；SDK23 arm64 consumer/S1 与两板 ABI/model PASS，wrong-architecture rejection PASS。证据 `.artifacts/verification/real-binding-final-20260914T064920994Z/RESULT.md`、`.artifacts/verification/real-binding-sdk-final-20260914T065331423Z/RESULT.md`、review `.artifacts/review/20260914T-binding-fixes/REVIEW.md`。预加载依赖须为 `IMPORTED_GLOBAL`，Release 映射仅未设置或单一 `RELEASE`；bare-PATH、长路径和初始部署失败保留为边界。仅为配置/ABI 绑定，不含 DSoftBus Socket、DDS discovery、同名 Participant 热重建或 RMW；该历史切片不证明完整 Transport 发布就绪；N1 提交门槛单独记录 |
| S3-LC Socket lifecycle/callback close | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（LC runner 限定范围；NOT DEVICE_TESTED） | 独立 final tester `.artifacts/verification/lc-runner-final-20260914T120200Z/RESULT.md`：harness 80/80、runner 42/42、validator 130/130、兼容性 16/16、LC validator 169/169、RecordPairHarness 10/10、syntax 4/4，以及 Runner 私有 real-fd/process diagnostic 均 PASS；review closure `.artifacts/review/20260914T-lc-runner-code/CLOSURE.md`。修复覆盖已观测 collector exit-before-probe race；full log overlap、SDK quiescence、真实板端 Socket/Transport 仍 NOT_RUN，不能宣称完整 Transport 或 VERIFIED |
| S3-LC pure launch validator | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（纯校验器限定范围；NOT DEVICE_TESTED） | final tester `.artifacts/verification/lc-launch-validator-final-20260914T074210082Z/RESULT.md`：record v1 130/130、compatibility 16/16、LC 169/169、harness 10/10、syntax 6/6 PASS，冻结文件哈希 MATCH；review `.artifacts/review/20260914T-lc-validator-code/REVIEW.md` 无 actionable finding。校验器不读设备、不改权限、不认证证据真实性；LC runner 的后续 final evidence 见 `lc-runner-final-20260914T120200Z/RESULT.md`，但设备启动未执行 |
| S3 real Transport facade | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围；NOT VERIFIED） | Descriptor/Transport factory、真实接口与 selectors 已实现；native backend 缺失时 production `init()` 固定失败并报告 `NativeBackendUnavailable`，私有 HostModel 不可作为生产 fallback。final host `.artifacts/verification/facade-host-final-20260914T075048953Z/RESULT.md`、SDK/双板 ABI `.artifacts/verification/facade-sdk-final-20260914T075114524Z/RESULT.md`、review `.artifacts/review/20260914T-facade-code/REVIEW.md`（无 actionable finding）。未证明 native communication、DDS discovery、同名 Participant hot rebuild 或 RMW |
| S4 SPDP | NOT RUN | 无 Fast DDS Participant integration |
| S5 SEDP/user data | NOT RUN | 无 Writer/Reader、匹配或样本证据 |
| S6 fault/reliability | NOT RUN | 无真实 Transport |
| S7 RMW | NOT RUN | RMW 版本和注入边界尚未锁定 |
| ApplicationAttachments root wiring | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；NOT DEVICE_TESTED | `.artifacts/verification/attachments-wiring-final-20260914T093400Z/RESULT.md`：选项 7/7、SOURCE Debug/Release 22/22、CONFIG Release facade ON 23/23、public consumer、binding/facade 矩阵和 SDK23 arm64 link PASS；接线 review `.artifacts/review/20260914T-attachments-wiring-code/REVIEW.md`（SHA `89C726AF62F1B001523C2221B9BBE70DF74DC56927D544C67984900517912418`）无 blocking。仅证明 root 构建接线，不含 HDC、SDK runtime、DDS 或热重建 |
| N1 stable native owner | IMPLEMENTED / BUILDS / HOST_TESTED / SDK23 BUILD/LINK PASS / REVIEWED（模型/owner slice 限定范围） | N1 completion final `.artifacts/verification/n1-completion-final-20260914T1455Z/RESULT.md`：候选树 87/87 字节一致、SOURCE Debug/Release 各 23/23、CONFIG 24/24、root options 9/9、attachment 7/7、consumer 1/1、standalone、20 次重复、SDK23 link/readelf PASS；root wiring 目标仅在 fresh cache 中按 `BUILD_DSOFTBUS_DIRECTORY` 默认，测试仅随 `BUILD_TESTING=ON` 注册。review 无 actionable finding。无 SDK runtime、DSoftBus、DDS 或热重建证据 |
| N2 native send execution | IMPLEMENTED / BUILDS / HOST_TESTED / SDK23 ABI LINK/ELF TESTED / REVIEWED（SDK-free injected synchronous model 限定范围） | [N2 completion](N2_COMPLETION.md)；final receipt SHA `3AC3B952B424D3EDFF1A16D584463ABA89F369D9E0A37527316B32D3955612F7`；SOURCE Debug/Release 各 24/24、CONFIG 25/25、20×10 repeat、N1 9/9、attachments 7/7、binding 32/32、facade 4/4、SDK23 AArch64 link/readelf PASS；review SHA `227E9479AC4CA318B1B89BD51AE66D554ED47F69819DE2B13E1CAB8CA6D660D7` 无 actionable finding。真实 Socket/SDK Send、DDS/RTPS、设备/跨设备、RMW、同名 Participant 热重建 NOT RUN；scoped model gate PASS，不等于完整 Transport 或普通项目 commit |
| Overall | NOT VERIFIED | frozen N3 lifecycle model is IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED in its bounded host scope; native backend remains `NativeBackendUnavailable`; real DSoftBus provider/callback quiescence, device/cross-device runtime, same-name DDS Participant hot rebuild, DDS SPDP/SEDP/user data/RMW remain NOT_RUN; full Transport release NOT VERIFIED |

## S1 上轮 review and re-test

Independent Tier-2 review 发现唯一 P2：“positive Socket Shutdown 分支未测试”。该问题已补测并由最终 tester 重新验证；这些是 S1 历史证据，不改变后续跨设备记录尚未执行的事实。无授权 Socket 失败的最新设备证据见 `.artifacts/diagnostics/20260914T000333Z-socket-recheck/`，其中同 PID 服务日志明确记录 `DISTRIBUTED_DATASYNC` 权限拒绝；当前已授权本机 Socket 生命周期则由 S2-P final run 单独证明。

## Remaining gates

ApplicationAttachments 的设计 R1（unique InputChannels/attachment 封装）与代码 R1（Close waiter 等待迟到 Opening）是两个不同问题，均已在各自限定范围通过 review/test；root wiring final 也已通过。64 次仅为模型 application attachment 周期，不是 DDS Participant 或真实同名热重建；本 slice 未执行 HDC、SDK runtime、DSoftBus Socket、DDS 或 RMW。native backend、实际 DDS/SPDP/SEDP/样本、同名 Participant 热重建和 RMW 仍 NOT_RUN。

LC runner 最终独立 tester 已通过其 host/private real-fd/process 诊断范围，但没有设备执行；N1 completion 的候选树、host/source/consumer、SDK23 ABI 和 root wiring 范围通过，完整记录见 [N1_COMPLETION.md](N1_COMPLETION.md) 与 `.artifacts/verification/n1-completion-final-20260914T1455Z/RESULT.md`。N2 限定模型的实现、review 与 final tester 已完成，完整记录见 [N2_COMPLETION.md](N2_COMPLETION.md)；root CTest 使用进程内 machine `PSModulePath` 与绝对 host DLL `bin` 前缀。该段是 N2 完成文档的原始提交前快照：当时 N2 尚未提交；随后 N2 已提交为 `def10ea376cb3bf9921c1c7ea74c92b1da9e04e8`，N3-1 root wiring 已提交为 `ddc08c55aee78414015d194f2c976da690948f12`。原始模块查找/相对 DLL 路径失败、早期 invalid-hash 诊断均保留为历史证据，不计为 PASS。真实 Socket、DDS、设备和同名 Participant 热重建仍 NOT RUN。

- N1 独立 final tester 的适用执行结果为 PASS；N1 精确提交已完成。N2 文档后的最终图 receipt 与 Git 检查仍由 Main 执行，不能仅由 tester PASS 宣称整个 working tree clean。
- N1 completion 已完成候选 87 路径、18 个 disjoint graph batch 的限定范围 reconcile；完整 Transport 的发布 gate 仍未通过，不能把历史 gate 失败改写为当前 N1 tester 失败。
- token-first 双板 records 在五种长度的有界范围通过，但 SDK 回调排空、并发取消/重连、持续负载、DDS discovery、用户数据和 RMW 仍未验证。S3-A/B/C/D2 与 facade/输入核心仅在各自限定范围通过；Descriptor facade 已实现，但 native backend/完整 Transport 仍未实现。
- GitNexus 当前有 N1 candidate closure 的限定范围 reconciliation，最终文档树的 post-edit graph receipt 由 Main 在提交前单独核对；这不构成完整 Transport 的运行验证。
- 不得将历史缺少原始记录的 G1/G6 与本轮 IF/SOCK 测试自动对应。

## N3-1 根构建接线证据（2026-09-15 UTC）

N3-1 已在限定范围完成：既有 N2 `NativeSendExecution.cpp` 通过根 `CMakeLists.txt` 进入所属生产静态目标，根测试复用该目标，未增加 shadow source list；生产 backend 仍保持 fail-closed。最终独立 tester 见 `.artifacts/n3-execution/wiring-final/RESULT.md`：root/source-subproject 10/10，`SOURCE_PROBE` Debug/Release 各 24/24，CONFIG Release 25/25，既有 N1/attachments/binding/facade 9/7/32/4，外部消费者 1/1/2，SDK23 arm64 link/ELF PASS。review 唯一 P2（artifact parent）已修复，并由 clean-parent 10/10 复测。

该切片不是 N3 生命周期完成：真实 DSoftBus provider/callback/线程、Socket 通信、DDS SPDP/SEDP/样本、设备/跨设备、同进程同名 Participant 热重建及 RMW 均未执行。N3 application bundle、pinned receive/receiver lease、copying/admitted drain 和联合 Detach 仍为 PLANNED；本记录只支持根接线的限定范围 IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED，不支持完整 Transport VERIFIED。

## N3 frozen lifecycle model 证据（2026-09-15 UTC）

冻结的 N3 SDK-free 组合模型已在当前工作树实现并通过 review 后最终 tester：根 SOURCE_PROBE Debug/Release 各 26/26；直接组件 Coordinator 13、Process receive 8、N2 17、ApplicationAttachments 9、InputChannels 16、Runtime 30、N1 11（两种配置均通过）；CONFIG facade 21 + Coordinator 13；N1 consumer 9/9、N2 consumer 10/10；SDK23 两个 Coordinator/Process ELF 均为 ELF64 AArch64。最终证据为 `.artifacts/n3-execution/full-final-test/RESULT.md`，review 原报告与闭环分别为 `.artifacts/n3-execution/full-review/REVIEW.md`、`CLOSURE.md`。

已记录的硬契约包括：P0 记录不可改投 P1；native identity constraint 独立于 callback lease；Revoke 在线性化点后禁止新成功准入且排空全部 admitted invocation；最终接收执行取得原 receiver 的真实 use lease；Detach 等待 application copying；协调门只做短状态转换、等待/provider/receiver/cleanup 在锁外；同 local 的 P1 必须等 P0 Detached。测试覆盖事务回滚、代际/路由校验、Binding 有界停驻、copying 与 dispatch drain、发送 admitted drain、receiver 替换、重复 Detach、保留配额及 80 轮同 local 模型重建。

本状态仅支持 N3 frozen host model 的 IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）及 SDK23 LINK/ELF TESTED。补充 manifest `.artifacts/n3-execution/full-final-test/COMPLETE_N3_MANIFEST_RECEIPT.md` 对完整 27 路径做了补充区间的前后哈希一致性检查，并补测 N2/Process/Coordinator 的 Debug/Release 与 SDK23 三个 ELF；新增六个路径没有原始 before-hash，不能追溯宣称其更早时点未变。真实 DSoftBus provider/callback bridge/生产线程、Socket callback-ID reuse/quiescence、板端权限和运行、DDS SPDP/SEDP/样本/可靠性、RMW，以及实际同进程同名 DDS Participant 热重建均 NOT_RUN；模型的 same-local rebuild 不得升级为实际 same-name Participant PASS。production `init()` 在 backend 未接入时继续 `NativeBackendUnavailable`，本项目自身 install/export package 不属于 N3。

初测中 AA/options/binding 矩阵未在 test-only review 修复后重跑；最终 tester 明确保留其前序 PASS 为历史证据，未冒充 post-change 新执行。初始失败缓存和历史失败记录继续保留，不改写为当前最终结果。

## S2-P final evidence

最终 tester 证据见 [.artifacts/verification/20260914T005000Z-permission-lifecycle-final](../.artifacts/verification/20260914T005000Z-permission-lifecycle-final)：Host CTest 10/10 PASS（权限流程 10 groups、快照守卫 158 checks）；官方平台 clang 路径下 OHOS probe 构建/链接 PASS。A/B 均完成同一已归属 native token 的 regrant、服务 GRANTED 验证、独立 Socket 创建并 Shutdown 恰好一次、revoke 后服务 DENIED 与 self 还原。A token `671793191`、B token `671320465`；最终项目记录保留为空权限身份，origin/transition JSON guard、外部记录结构及 provider hashes 均 PASS。

既有 `com.kaihong.mdds` allowlist 未被替换；本次仅使用用户授权的新 `fastdds_dsoftbus_probe` 身份，最终保留空权限记录。权限 lifecycle review `.artifacts/review/20260914T004622Z-permission-lifecycle/REVIEW.md` 无 blocking finding。该证据只覆盖本机 Socket 生命周期和权限闭环，不覆盖 Listen/Bind/SendBytes、跨设备记录、SPDP/SEDP、用户数据或 RMW。该历史权限切片不证明完整 Transport 就绪；N1 限定提交门槛另见 N1_COMPLETION.md。
