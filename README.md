# Fast DDS OpenHarmony DSoftBus Transport

## Current progress and navigation (2026-10-18)

- **R25 native C++ Action business/path PASS, bounded:** one real RK3588A pair exercised goals, feedback, concurrent execution, cancellation, rejection, abort and normal completion over native DSoftBus with project DDS/RMW. [Results and limits](docs/ROS2_NATIVE_ACTION_ACCEPTANCE.md).
- **Recorded limitation:** each board logged a failed optional unique-flow Reader attempt; independent review confirmed the existing RMW retries with original QoS on the same custom Transport. Unique flows were not provided, strict mode was not tested, and startup was not error-free.
- **R24 project-linked Python PASS, no Node:** native imports, exact String CDR and help/resource queries remain qualified and published as `d8b0fdfecaa654183c57d4b973cc936234080667`. [Dependencies and reproduction](docs/ROS2_PROJECT_PYTHON_ABI.md).
- **R25 ordinary Python/CLI node discovery PASS, bounded:** single public Node lifecycle and stock `ros2 node list --no-daemon --spin-time 3` passed on the real RK3588A pair in both directions. [Commands, evidence and limits](docs/ROS2_ORDINARY_CLI_NODE_DISCOVERY_ACCEPTANCE.md).
- **Ordinary topic evidence:** topic graph/pub passed in both directions under the preceding runner; the current four-file Host readiness repair has independent review/post-review tests and fresh real AB/BA `topic echo` PASS. [Results and evidence boundaries](docs/ROS2_ORDINARY_CLI_TOPIC_ACCEPTANCE.md).
- **Ordinary Action CLI PASS, bounded:** stock `ros2 action send_goal` accepted one Fibonacci order=6 goal in each direction, with five feedback prefixes and exact successful result on the real RK3588A pair. [Commands, evidence and limits](docs/ROS2_ORDINARY_CLI_ACTION_ACCEPTANCE.md).
- **Rosbag native ABI prerequisite PASS, no Node:** the SQLite rosbag overlay builds and composes; real imports, String/Resume CDR, native QoS conversions and stock parser checks passed on both RK3588A boards. Recording/playback and communication are NOT_RUN in this stage. [Evidence and limits](docs/ROS2_ROSBAG_ABI_ACCEPTANCE.md).
- **Rosbag record/play DEVICE_TESTED PASS, bounded:** record5 A-to-B persisted ten original String CDR messages; play1 B-to-A resumed through one actual service call and received all ten byte-for-byte in order. Normal close/exit, owned PID absence and token restoration passed. [Evidence, prior failures and limits](docs/ROS2_ROSBAG_RECORDPLAY_ACCEPTANCE.md).
- **R26 sustained pub/sub echo DEVICE_TESTED PASS, bounded:** 200 exact String application roundtrips, normal paired exit and identity restoration passed on the RK3588A pair; resource samples do not prove leak freedom. This is not ROS service/action traffic. [Evidence and restart limits](docs/ROS2_SUSTAINED_ROUNDTRIP_ACCEPTANCE.md).
- **R27 single process restart DEVICE/INTEGRATION PASS, bounded:** explicit ON Q3 kept A alive through one normal B replacement, PRE5/POST5 exact String echo and normal cleanup. Tester gate PASS; Main PRE_COMMIT/publication pending. Both earlier BLOCKED runs retained. [Evidence and limits](docs/ROS2_PROCESS_RESTART_ACCEPTANCE.md).
- **Publication / next boundaries:** S1 published as `9cdbc84ed1371d53f4ee53b88dad5b3e5f1612ab` (2026-10-18 11:45:04.427145 UTC CI NOT_RUN); R27 remains uncommitted pending Main gate. Default automatic reconnect is not enabled; SDK epoch/drain, repeated/long-duration and service/action recovery remain unverified. Full ROS 2 NOT VERIFIED; R22 STOPPED. [Current matrix](docs/STATUS.md).


以下带日期摘要保留其历史范围，不能替代上述当前状态，也不构成新的设备测试授权。

本项目是 Fast DDS 的 OpenHarmony DSoftBus Socket Transport 适配层，不是独立的
MDDS 中间件或新的 DDS/RMW 实现。公开命名已迁移为 `fastdds_dsoftbus`、
`<fastdds_dsoftbus/dsoftbus/...>` 和 CMake 包 `FastDDSDSoftBusTransport`。
这次迁移改变源码/ABI/构建接口；旧设备证据仍绑定旧命名的原源码与包。
迁移规则和保留的历史协议名称见 [命名说明](docs/NAMING.md)。

当前还提供一个隔离的 ROS 2 Jazzy Host 集成入口，使用原版 `rmw_fastrtps` 和
版本绑定的 Fast DDS 2.14 Host 前端；它不创建 `rmw_mdds`，不改变默认 Fast DDS
3.6 基线，也不构成设备或生产资格。构建、安装、外部 `find_package` 和已验证的
Host 范围见 [ROS 2 Host integration](docs/ROS2_HOST_INTEGRATION.md)。

> **当前限定 ROS 2 双板验收摘要（2026-10-04）**：SDK23/AArch64 的
> `build/ros2-target/r2` 已在登记双板上完成限定功能范围验证：query
> `d1c2cb4097279214`、data `be8cb2d90f287223`，RELIABLE/BEST_EFFORT 双向各 5/5，
> Topic 合计 20/20，并完成 graph、service、实体/Participant 删除、Runtime close、
> 正常退出和环境恢复。该结果不等于生产资格；P0/PF/P4 原状态、默认
> `NativeBackendUnavailable`、SDK epoch/drain/cancellation 和 `QualificationProfile`
> 边界保持不变。设备验收记录产生时 HEAD 为
> `4807765f53826ed0b882cac32610acb17ee30b4f` 且尚未提交；后续本地提交状态以 Git
> 历史为准，不预写推送结果。详见 [ROS 2 Host integration](docs/ROS2_HOST_INTEGRATION.md)
> 的当前摘要和 paired-close 证据入口。

> **ROS 2 device acceptance final stop（2026-10-01）**：补充 query
> `cdd5bbe62d49a8a6` 与修复 query `5b1d035fb8e6ae66` 均仅达到 `PASS_QUERY_ONLY`。
> canonical data `9b301c516da4d1eb` 保持初始化失败；修复 data
> `a6b0948bf98c9a78` 失败，A/B remote exit 均为 `4`，无正式 Topic write/take、graph、
> service 或正常 DDS/Runtime close。环境 guard 在其测量范围内通过，但不等于 SDK drain。
> 当前 verdict 为 `FAIL_CONFIRMED_AND_STOP_REQUIRED`，query 使用 3、data 使用 2，
> 剩余额度为 0；不再重试、不再提交成功基线。此前 `762fe826e929da8c` 缺原始 claim
> 的失败事实保持不变。详见 [ROS 2 Host integration](docs/ROS2_HOST_INTEGRATION.md)
> 与 [supplemental stop report](.artifacts/ros2-device-acceptance/run-20260930-local/SUPPLEMENTAL_RESUME_FINAL_REPORT.md)。

> **ROS 2 dual-board acceptance stop（2026-10-01）**：本次真实双板只完成了目录
> query `762fe826e929da8c` 及两端恢复/guard；数据程序未启动，Topic、graph、service
> 和 ROS 数据关闭保持 `NOT_RUN`。query postrun 因缺少原始执行 claim 被正式拒绝，
> 原 claim 不可回填或退款；当前状态为 `BLOCKED / DEVICE_ROS2_DATA_NOT_RUN /
> NOT_COMMITTED`。随后对未来 ROS real claim 分支的两项本地测试通过，不改变这次
> 历史 query 结果，也不授权重新查询或继续设备运行。Host/本地模拟证据仍按各自范围
> 记录，不能当作设备 ROS 证据。详见 [ROS 2 Host integration](docs/ROS2_HOST_INTEGRATION.md)
> 和本次 [device stop evidence](.artifacts/ros2-device-acceptance/run-20260930-local/OBSERVED_REAL_BUDGET_AND_STOP.json)。

> **Offline ROS startup diagnostics（2026-10-01）**：当前离线诊断已达到
> `ROS2_STARTUP_OBSERVABILITY_VERIFIED / ROS2_FAILURE_CLASSIFICATION_REPAIR_VERIFIED /
> INDEPENDENT_REVIEW_CLOSED / DEVICE_NOT_ADMITTED / NOT_COMMITTED`。R2 受影响 Host
> 与 ASan 各 `53/53`，安装版 hook 测试 `6/6`，本地正确依赖前缀 pipe `2/2`；Host
> ROS 双 QoS/graph/service 记录 `20/20` 仍是 Host 证据。启动观测现在区分 primary、
> cleanup、capture 和 remote exit，并保留首个错误；有限元数据容量不等于阻塞 I/O
> 硬期限或 SDK cancellation 保证。历史设备失败 `9b301c516da4d1eb` 与
> `a6b0948bf98c9a78` 仍为 FAIL，第二次启动根因 UNKNOWN；本轮无 HDC、设备、Git
> 或新额度操作。详见 [离线诊断收据](.artifacts/ros2-device-acceptance/run-20260930-local/offline-startup-diagnostics/FINAL_REPORT.md)。

> **Installed Runtime dual-board acceptance（2026-09-30）**：安装版公开消费者的
> 双板窗口 `data-2`（run `73ff0560343b4afb`）通过独立原始收据复核；配套
> `query-3` 为 `52093e521faf0dcb`。A/B 各完成 5 writes 与 5 Reader takes，
> 每个方向的 ordinal、Writer GUID、DDS SN、run 字段和 payload 均逐项匹配。
> A/B 均在 listener-ready 后释放连接并完成正常 DDS 删除、`Runtime::Close`、
> token restore 与进程退出。该结果只覆盖当前安装包和配置的限定双板功能窗口，
> 不证明 SDK epoch/drain/cancellation、完整 SDK 模块闭包、PF/P0/P4 或生产资格。
> 历史 `data-1` FAIL 与 `query-r6-1` FAIL/RECOVERY_BLOCKED 保持原样；本次查询和
> 数据额度已用尽，没有后续设备窗口。详见 [Installed Runtime acceptance](docs/INSTALLED_RUNTIME_ACCEPTANCE.md)。

> **Library delivery checkpoint（2026-09-29）**：r1 完整 CONFIG `524/524 PASS`
>（677.01 s）；r2 受影响安装、move-prefix 外部消费者、Host DDS/default-Unavailable、
> 负例、API23 真实库 compile/link/$ORIGIN 与限定 ASan 复测均 PASS。正式 review PASS；
> 仅 1 个 nested DLL allocator-injection case 未触发而 SKIPPED。该包只有 Host/compile/
> static API23 证据，设备/HDC=0，不是 DSoftBus runtime/device/生产资格。README/INSTALL
> 本轮新增薄封装及安装/导出，未改既有生命周期实现。详见 [安装与交付边界](docs/INSTALL.md)。

> **Local checkpoint closure（2026-09-28；`checkpoint-local`）**：标准 CONFIG
> `523/523 PASS`（复用原 source correspondence），Host100 replay `25 PASS`、target-core
> W2 replay `5 PASS`；final tool closure `11/11` commands、Flow21 PASS。17 个 task members
> + 2 个 external members 的绑定闭合是本地 promotion path，不是历史 device execution
> path。内部 9 个 RealText cases 保持 SKIP，DLL allocator injection 未触发；初次无效环境
> 日志保留，旧 ASan220 仅复用原身份。本轮 HDC/device=0；N10/N100 有限范围、默认
> `NativeBackendUnavailable`、生产/P4 限制保持不变。详见 [checkpoint docs report](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/checkpoint-local/docs-final/FINAL_REPORT.md)。

> **Final N100（2026-09-28；`final-n100`）**：`QUERY100` PASS（run
> `12ceebf2b741302e`），`NEW_PATH_100_CYCLE_FUNCTIONAL_REGRESSION_PASS` PASS（Q-first
> run `eab10c4b776d792c`，3000/3000），独立 N100 review PASS。既有 P-first N10
> `df9319fdf22ab3d9` 保持 PASS，本轮未重跑；这不是两种创建顺序各100。N100/新 query
> 额度均已用尽，`CHECKPOINT_READY` 仅表示有限记录证据，`NOT_COMMITTED=YES`。
> SDK drain/epoch、生产/P4/QualificationProfile 仍未证明。此段是运行后文档增量，
> 不宣称当前全树仍等于 source390；r14、manifest、claim 和原始验收件身份不变。
> 详见 [final-n100 report](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/final-n100/FINAL_REPORT.md)。

> **Query replay repaired / N10 accepted（2026-09-28；`query-replay`）**：
> `QUERY_CLAIM_PHASE_REPAIR_VERIFIED = PASS`、`POSTRUN_QUERY_REPLAY_VERIFIED = PASS`，
> `NEW_PATH_10_CYCLE_FUNCTIONAL_REGRESSION_PASS = PASS`（run `df9319fdf22ab3d9`，
> 独立证据复核接受）。N10 数据额度已用 1/剩 0；query 已用 1、新目录 query=0。
> N100 `NOT_RUN`：原 query10 不能作为 purpose100，缺 query100 且新 query 许可为0。
> `CHECKPOINT_READY_FOR_ORIGINAL_PACKAGE=NO`、`NOT_COMMITTED=YES`、SDK drain/epoch
> `UNPROVEN`。本段是运行后的文档增量，不宣称当前整树仍等于冻结 source390；旧 r12、r13、
> manifest 与 replay 身份保持不变。详见 [query-replay final report](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/query-replay/FINAL_REPORT.md)。

> **Observed retry stop（2026-09-28；`observed-retry`）**：新 N10 为
> `INCOMPLETE / PRE-DISPATCH STOP`，不是完成的 data FAIL，也不是 PASS。唯一获准
> query `5ca407e7845f0484` 在本地 post-query `route_query.py` 因
> `Supplemental quota already claimed` 停止；query quota 已用 1、余 0，data
> dispatch 为 0、data quota 余 1（见 `BUDGET_CLARIFICATION.json`）。两端
> 两端 query/restore guards PASS；两板只读预检/目录 query 已执行（local/remote rc0/0），
> 但 workflow 在本地 post-query gate 失败；无 P/Q data、deletion、live-load 或 postrun
> data 证据。README/PLAN 这一段是停止后的文档增量，不宣称当前全树仍匹配运行的
> source390；旧 r12/运行包保留原身份。
> N100 `NOT_RUN`（前置 N10 成功/接受未满足），历史 N10 `FAIL`、`CHECKPOINT_READY=NO`、
> repair=0 保持不变。27/27 local tester 与 review-stop 只覆盖其声明范围，不覆盖
> prepare→claim→postrun 整路径。详见 [observed-retry final report](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/observed-retry/FINAL_REPORT.md)。

> **离线诊断包（2026-09-28；`offline-c6`）**：`OFFLINE_CAUSALITY_GAP_BOUNDED /
> HISTORICAL_N10_CAUSE_PARTIALLY_UNRESOLVED / INDEPENDENT_REVIEW_CLOSED /
> DEVICE_RETRY_AUTHORIZATION_REQUIRED`。本包仅完成离线诊断与有界证据修复：tester-2
> 本地命令 `18/18 PASS`，无 HDC、设备、SDK、目标执行或真实 window，retry=0。
> 历史 N10 `FAIL`、N100 `NOT_RUN`、`CHECKPOINT_READY=NO` 与原六项 gate 不变；
> 不宣称找到历史 SIGTERM/SDK 根因。F05/F32 已按实际 stage 修正为 C5 并由两份
> independent offline review 复核关闭。详见 [offline-c6 final report](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/offline-c6/FINAL_REPORT.md)
> 与 [diagnosis](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/offline-c6/DIAGNOSIS.md)。

> **当前 NEW_PATH_EXACT_ONCE_AND_BOUNDED_REBUILD_REPETITION 状态（2026-09-28；artifact run `20260927T113439Z`）：**
> `EXACT_ONCE_PIN_VALIDATION_CLOSED = PASS`；`HOST_100_CYCLE_VERIFIED = PASS`；
> `NEW_PATH_10_CYCLE_FUNCTIONAL_REGRESSION_PASS = FAIL`；
> `NEW_PATH_100_CYCLE_FUNCTIONAL_REGRESSION_PASS = NOT_RUN`；
> `CHECKPOINT_READY = NO / BLOCKED`；`NOT_COMMITTED = YES`。r9 的 exact-once pin guard
> 及定向 T-A 历史结论保留，且已由最终 r5 review 覆盖；r8/r9 设备结论仍是各自历史证据，
> 不回填为本轮 N10/N100 结果。
>
> r5 是 post-validation 之前的两文件 Python-only 分析器/测试 delta；本次 README/PLAN
> 修改属于 r5 验证与设备运行结束后的另一个 documentation delta。r5 SOURCE390
> 冻结快照 SHA 为 `09ba0a53238890acff824d597f5ad60ed03e0d771502eb5ceabf4384ed59ef1f`，相对 r4 仅
> `scripts/public_socket/analyze_shared.py` 与
> `tests/public_socket_send_cancellation_analysis_tests.py` 变化，其余 388 个 SOURCE
> 文件与 EXECUTOR25 未漂移；这只是 r4→r5 冻结比较，不宣称当前整树仍为 390/390 零漂移。r4 独立 full CONFIG `507/507 PASS`、双 100-cycle、ASan
> 唯一并集 `220 PASS`（130+90；smoke 5 为重复子集）以及 r5 Python/target projection
> 套件均有独立 PASS；r4 的首次缺 target、ASan PATH、宽泛日志提取错误保持为历史原件，
> 没有改写成首次 PASS。预编译第三方 Fast DDS DLL 未完整插桩。
>
> 唯一获准的 repair N10 真实设备尝试 `c1ed9706ef5b794f` **FAIL**：整窗
> `timedOut=false`，A 在 `SHARED_READER_MISSING`（84 writes / 81 takes），B 在
> `C6_P_OLD_DELETED` 的控制 collector 收到 `SIGTERM`（`control-92.json` 的
> `code=null`、`collector_closed=true`，不能归因远程 SDK）。B 的 `q_deleted=0` 与
> 已存在的 `SHARED_DELETE Q FINAL` 原始收据并存，且 `egress_closed=1`、
> `egress_drained=1`；该差异不被宣称为完整删除账本 PASS。A 的 null-input load
> collection 失败、B 完整收集、两端 guards/cleanup/process/permission 收据均保留。
> 这是唯一 repair attempt，已耗尽；原 N100 限定授权的前置 gate 未满足，N100 当前不执行，
> 也不是“下一阶段未开始”或取消原授权。SDK
> 内部 drain/epoch 仍 `UNPROVEN`。新数据继续前只能离线定位 C6 collector 终止与 load
> collection 错误，并取得新的限定 attempt 授权；当前设备 gate 仍 BLOCKED。
>
> 详细最终记录见 [codex-resume/FINAL_REPORT.md](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/codex-resume/FINAL_REPORT.md)
> 与 [codex-resume/IN_PROGRESS.md](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/codex-resume/IN_PROGRESS.md)。

> **当前基线（2026-09-28）：共享 Participant 生命周期已提交。**
> 提交 `7f40798eb551c0ff89bc956367c1b31ba663855f`，parent `400e4ff3b27c39ce4ef25b4fc8b1128bc98e1880`，
> tree `6948c5e27a105e0329f286121b53ec8fe19b8677`，分支 `claude/dsoftbus-transport-20260924` 不变。
> 状态为 `SHARED_LIFECYCLE_BASELINE_COMMITTED / NOT_PUSHED / NOT_PRODUCTION_QUALIFIED`：
> 恰好一个新 commit，22 个路径（16 修改 + 6 新增），不含任何 gitlink，未 push、未 merge、未 amend。
> 完整契约、必测矩阵、逐项处置与证据入口见 [PLAN.md](PLAN.md) §21 与
> `.artifacts/public-socket-shared-reattach/run-20260927T113439Z/claude-r8/CLOSEOUT.md`。
>
> **本轮实现**：同一个进程里两个真实 Fast DDS DomainParticipant 共享一套 DSoftBus runtime
> （一条 listener/outgoing/accepted 与一份发送注册），各自保有独立的 application、receiver
> 与 DDS 实体。`HostProviderLoop` 成为「runtime 所有者 + 每个 façade 一个 View」，View 通过
> 活 runtime 上的显式 `Join()` 取得；每个 View 持有自己的 egress registration，停止一个
> Participant 不会撤销另一个的，最后一个 View 才执行最终关闭。接收侧在 native 准入点固定
> 候选集合，并且只为**各个原目标自己**取准入 pin，由不可拷贝、只可移动的 RAII 持有者释放。
>
> **验证（全部绑定冻结修订 freeze-r8，364 文件）**：完整 CONFIG Host **484/484 PASS**（0 fail/0 err/0 skip，
> 224.4 s）；受影响集 ASan **121/121 PASS**；**13 个独立变异全部被杀死，0 存活**，其中包含早前
> review 报出的两个缺口——逐 View egress 结算与 shared/exclusive claim 冲突；分析器 **36/36**，
> 含 9 个用真实设备日志的 text→parse→validate 入口驱动的边界用例。
>
> **设备（真实双板，freeze-r8 包 `main_sha256 31f48fd384b04018…`）**：`SHARED_W1 256cce1c37aabcac`、
> `SHARED_W2 372d9a20c0db57e9`、`F1M cb0a993d921f9c8b`、只读路由查询 `query-r8`，全部 PASS 且两板
> guard 完整（`byte_match`/`metadata_match`/`process_absent`/`DENIED`、`first_failure=null`、
> `cleanup_failures=[]`）。两种创建顺序下：P(old) 删除与同显示名 P(new) 重建全程，每板
> **shutdown 调用为 0**，`FINAL` 恰好 **1** 次且 `views=0`；listener/outgoing/accepted 各一、
> entry 与 record/owner 世代不变。该 `shutdown_calls` 是
> `PublicSocketObservation.operations[4]`，即本进程 shim 进入公共 Socket `Shutdown` API 的次数，
> **不是** SDK 终端通知，也不是 runtime finalization 计数，更不是物理关闭证明。
>
> **已提交的运行时代码与 freeze-r8 按行尾规范化后逐字节等价**：92 个 `src/include/probes` 路径中
> 18 个仅 EOL 不同、**零实质差异**；相对 freeze-r8 的唯一实质源码差异是本轮新增的
> `tests/public_socket_shared_analysis_tests.py`（纯测试）。因此设备证据仍绑定该提交的运行时代码，
> 但以该 EOL 前提陈述，而非宣称逐字节相同。
>
> **如实保留的限制**：`R8-R1`（fail-fast 诊断在一条当前不可达分支变为可达时会给出错误补救）、
> `R8-R2`（响亮释放检查不覆盖独占 façade 与最终关闭失败）记为已知非阻塞限制，未改运行时代码；
> 准入 pin 的**双重逻辑释放**这一类只有隔离变异能表达，本轮**未运行**，记为未闭合验证缺口；
> 三个未跟踪文件没有 freeze-r4 逐字节原件，其历史谱系为 `NOT_ESTABLISHED`（不影响当前内容被版本化）；
> 修复 diff 的独立复审在设备窗口**之后**才返回，**未回填为设备前批准**（`PROCEDURE_DEVIATION`，
> 见 CLOSEOUT §5）。
>
> **边界不变**：SDK 物理记录 epoch 连续性 `UNPROVEN`；PF-02、P0/PF 仍 `BLOCKED`；P4 未放行；
> 生产默认仍为 `NativeBackendUnavailable`；`QualificationProfile` 为 `NOT_GENERATED`。
> 未声称 `SDK_RECORD_CONTINUITY_FULLY_PROVEN`、`PF01/PF02_PASS`、`100_CYCLE_QUALIFIED`、
> `ARBITRARY_TOPOLOGY_SUPPORTED`、`PRODUCTION_READY`。

> **累计实验基线（2026-09-27）**：A 的七场景与真实 CloseBusy 责任修复，加上 B 的 opt-in 决策诊断及稳定化验证。旧 111/6 未知与新 107/2 已观察原因分别保留。
> 范围、容量单位、依赖与证据见 [bounded DDS stabilization baseline](docs/BOUNDED_DDS_STABILIZATION_BASELINE.md)。该 checkpoint 当时的 isolated Host CONFIG suite 为 `445/445 PASS`；设备未运行，未进行新的 ASan。下列工作包状态是历史快照，不代表当前 Git 提交状态或生产放行。

> **当前 checkpoint（2026-09-27；artifact run `20260927T103431Z`）**：独立 tester 在冻结候选树上完成 Release/CONFIG Host build 与完整 CTest，`445/445 PASS`、exit 0、commit gate `PASS`。配置显式开启 bounded regression 与 decision diagnostics；生产默认仍为 `NativeBackendUnavailable`，诊断默认 OFF。没有 HDC/SDK/device execution，也没有新的 ASan；B 的 `72/72` ASan 结果仍是历史、预构建 Fast DDS 未完整插桩的 source-equivalent evidence。此状态是 checkpoint evidence，不声明已提交、SDK qualification、release 或 production readiness。

> **当前文档同步状态（2026-09-27；artifact run `20260927T083510Z`）：**
> 本次 public Socket binding decision / close stabilization 工作包为
> `CLOSED_WITH_HISTORICAL_LIMITATION / CHECKPOINT_READY / NOT_COMMITTED`。
> scoped functional gate 与 independent review 为 PASS；该结论只覆盖本包的
> bounded source/test/runtime review，不构成 commit、SDK qualification、release
> 或 production-readiness approval。三次 guarded query 与 F1M、D1、D2 三个双
> RK3588A 数据窗口均 PASS；F1M 为每个 1 MiB 样本 131 fragments，D1/D2 各为
> 每方向 5 个 Reader samples。23 个 Reader rows、5 个 DDS wait API operations
> 与 0 次 data retry 均为独立计数，不能解释为 packet-loss 或 SDK settlement。
> 新 F1M 的 A 侧有 107 次直接记录的 `CAPTURE/LOCAL_RECORDS`；F1M 与 D2 的 B 侧
> 各有一次直接记录的 Deploying/`ADAPTER_PHASE` 拒绝。D1 无拒绝，D2 无 Binding 拒绝。
> 另行保留：旧 F1M 的 111 次是没有逐次原因的 Binding 聚合拒绝；旧六条 adapter
> `InvalidState` 的直接原因未观察。两组历史计数不能相加为 117 条唯一丢失，
> 新 107/2 也不回填旧原因。
>
> 新增的是 SDK-free value-only `ReceiveDecision`，由现有 process receive observer
> 在真实决策点捕获值；受锁保护的状态在对应 gate 内取得快照。现有 observer 在解锁后
> 接收该快照，不重新查询当前 map；diagnostic 默认 OFF，
> 每进程容量 16384 rows（reported storage 5243440 bytes），带 reason aggregates
> 与 overflow counters。该 instrumentation 有实际开销，并非 zero-overhead，且不
> 构成 SDK drain 证明。CloseBusy algorithm 未改变；继承的 original owner/namespace
> 与 single close credit retention 已由 Host deterministic tests 验证，设备未声称
> hit CloseBusy。QualificationProfile 未生成；local raw artifacts 与 package-specific
> profile helper 不会因 Git clone 自动进入仓库。
>
> Final Host canonical `443/443`、affected Host `72/72`、affected ASan `72/72`、
> analyzer `20/20`、bounded profile `27/27` 以及 independent target build/static
> audit 均有 PASS 记录；最终 discovery 为 445，但没有宣称完整 445 run。ASan
> 使用的预构建 Fast DDS 未完整插桩，TSan 未执行。目标包构建和静态审计 PASS，
> target execution 与 device validation 在该 scoped audit 中 SKIPPED；设备窗口
> 使用已构建包的独立 HDC evidence。CloseBusy 分支在设备上未证明实际命中，SDK
> cancellation/quiescence/settlement、full Transport qualification、性能、完整
> firmware ABI 与 QualificationProfile 仍未验证。P0/PF-01/PF-02 仍为
> `BLOCKED / NOT QUALIFIED`，P4 未放行，生产默认仍为
> `NativeBackendUnavailable`。详见 [public Socket binding decision and close stabilization](docs/NEW_PUBLIC_SOCKET_BINDING_DECISION_AND_CLOSE_STABILIZATION.md)。

> **当前工作包（2026-09-27；artifact run `20260927T014310Z`）：**
> `NEW_PUBLIC_SOCKET_BOUNDED_DDS_REGRESSION_VERIFIED / CHECKPOINT_READY /
> NOT_COMMITTED`。七个独立 fresh-process、双 RK3588A A→B DDS 数据窗口和七个
> fresh guarded query 均完成：七个数据窗口、七次查询、28 个 board-window guards、
> 26 个正式 Reader rows，0 次数据重试。用例结果为 F256 `3/3`（每个样本 33
> fragments）、F1M `3/3`（每个样本 131 fragments）、REL_LOSS `3/3`（一次精确
> drop 后 NACK_FRAG admitted repair）、BE_LOSS `2/3`（完整 post-drop 窗口中
> sample 1 absent，2/3 complete）、VOL_LATE `3/3`（仅新 4/5/6）、TL_LATE
> `6/6`（保留 1/2/3 后接收新 4/5/6）和 KEYED_LAST `6/6`（每个 key 保留
> 3/4 后接收 5）。最终 Host `438/438 PASS`，affected ASan `169/169 PASS`。
> Fragment size `8016`、native record bound `8192` 未改变；实际链路为
> public DDS factory → 当前 DSoftBusTransport façade → NativeSendExecution →
> PublicSocketBackend/public Socket API → remote accepted/Binding →
> ReceiverResource/DataReader。
>
> 该结果只关闭上述七个 bounded functional profiles。F1M 的 A 侧 Binding
> `refused_receive` 在 MATCH→DATA_DONE 增加 `111`，个别原因和 record ID 为
> `UNOBSERVED`；另有六个 B 侧 setup `OnBytesRejected`，均为本地
> `BackendStatus::InvalidState`，不能改写为 SDK 错误、容量结论、无害事件或
> all-ingress loss-free。DDS wait API 不是 ACK packet 计数，void `Shutdown`
> 没有 SDK rc0；SDK internal settlement、cancel/quiescence、旧物理事件身份和
> full TransportInterface 仍未证明。共享 close-credit 修复保留原 owner 并在
> gate 外等待单一 close credit；provider callback reentry 拒绝阻塞控制，精确
> self-wait destructor 受控，其他 native table 的合法销毁保留。默认
> `NativeBackendUnavailable`、P0/PF-01/PF-02 `BLOCKED / NOT QUALIFIED`、P4 未
> 放行、无 QualificationProfile；无性能/生产资格结论。详见
> [New Public Socket bounded DDS regression](docs/NEW_PUBLIC_SOCKET_BOUNDED_DDS_REGRESSION.md)。

> **当前 PRE-COMMIT SNAPSHOT（2026-09-27；artifact run 20260927T005155Z）：**
> `LOCAL_NATIVE_DDS_BASELINE_HOST_VERIFIED / CHECKPOINT_READY`。
> 用户授权范围为当前分支一次本地 commit；不包含 merge/push/device/SDK/release，且
> 当前尚无 commit hash。候选 345 个冻结文件前后哈希一致，最终 CONFIG Release
> CTest `420/420 PASS`、exit 0（144.58s）；初始 `418/420` 的两个
> `Get-FileHash` PowerShell 环境失败保留，并在恢复模块路径后完整复跑通过。最终
> affected helper gate `3/3 PASS`。详见 [Public Socket baseline build](docs/PUBLIC_SOCKET_BASELINE_BUILD.md)
> 与 [Public Socket evidence index](docs/PUBLIC_SOCKET_EVIDENCE_INDEX.json)。默认
> `NativeBackendUnavailable`、P0/PF-01/PF-02、P4 与 QualificationProfile 不变。

> **当前工作包（2026-09-27；artifact run 20260926T225417Z）：**
> `REAL_DDS_OVER_NEW_PUBLIC_SOCKET_DEVICE_BASELINE_VERIFIED / CHECKPOINT_READY /
> NOT_COMMITTED`。D1 `e6e553...` 与 D2 `c4af736...` 在两块 RK3588A 上通过实际
> DSoftBusTransport façade→NativeSendExecution→ProviderAdapter/PublicSocketBackend→
> public Socket API→remote accepted/ProviderAdapter/Binding→ReceiverResource/DataReader；
> 每窗口每方向 5 条、共 20 条正式样本 GUID/SN 链。Host full `414/414`，final affected
> Host/ASan 各 `161/161`，final probe Host/ASan 各 `9/9`（重叠不相加）。这是小型
> DDS baseline，不是 full QoS/TransportInterface/SDK drain/PF/P4/生产放行。默认
> `NativeBackendUnavailable`、P0/PF-01/PF-02、P4 与 QualificationProfile 不变。
> 详见 [Real DDS Public Socket device baseline](docs/REAL_DDS_PUBLIC_SOCKET_DEVICE_BASELINE.md)。

> **当前工作包（2026-09-27；artifact run 20260926T200859Z）：**
> `TARGET_LOAD_OBSERVED / BOUNDED_PUBLIC_SOCKET_CALLBACK_PATH_OBSERVED /
> NEW_SHIM_BIDIRECTIONAL_SMALL_BYTES_OBSERVED / CHECKPOINT_READY / NOT_COMMITTED`。
> L `93a421...`、guarded query `7ec64...` 与唯一实际 socket 数据窗口 `d82c4...`
> 均有双板 guard；后者为每方向 5 条 256-byte 合成 BYTES probe，非 DDS/RTPS。
> Host R3 `407/407`、最终受影响 Host/ASan 各 `151/151`，运行时 Host/ASan 各
> `10/10`（不相加）。目标包使用授权公共 headers 和同一 fresh probe；不证明
> firmware ABI、SDK drain、完整 DDS 或生产资格。默认 `NativeBackendUnavailable`、
> P0/PF-01/PF-02、P4 和 QualificationProfile 不变。详见
> [Public Socket runtime probe](docs/PUBLIC_SOCKET_RUNTIME_PROBE.md)。

> **最新工作包（2026-09-27；artifact run 20260926T174047Z）：**
> `PUBLIC_SOCKET_ACCEPTED_HOST_CONTRACT_VERIFIED / REAL_DDS_OVER_PUBLIC_SOCKET_FAKE_VERIFIED /
> SOURCE_HEADER_TARGET_LINK_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`。限定双 Host
> endpoint、one listener/outgoing/accepted、Fake public Socket C API 与真实公开
> Fast DDS DDS 链路；独立 focused `80/80 PASS`、full `396/396 PASS`，ASan affected
> `139/139 PASS`（总数不相加）。E4 是 live pair 上额外 accepted capacity refusal 后
> 继续 10 次 DDS take，不是 Participant create failure。目标 `727` inputs/`7` outputs
> 的 API23 AArch64 静态链接证据仍不代表 SDK/device/runtime。生产 `NativeBackendUnavailable`、
> P0/PF-01/PF-02、P4 与 QualificationProfile 不变。详见 [Public Socket accepted Host](docs/PUBLIC_SOCKET_ACCEPTED_HOST.md)。

Current versioned Host capability, build dependencies and external evidence: [Host DDS baseline](docs/HOST_DDS_BASELINE.md). Host validation does not enable the SDK backend or release P0/PF/P4.

> **最新工作包（2026-09-27；artifact run 20260926T160850Z）：** `DSOFTBUS_PUBLIC_SOCKET_SHIM_IMPLEMENTED / TARGET_SDK_BUILD_VERIFIED / HOST_SHIM_CONTRACT_TESTED / CHECKPOINT_READY / NOT_COMMITTED`。现有 `NativeBackend` 已有面向公共 DSoftBus Socket API 的薄 shim，并通过 Host Fake 公共符号、既有 adapter/Binding/owned-copy/receiver lease 路径测试；API23 AArch64 目标配置/编译及五个公共符号静态审计通过。头文件来自授权的 `communication_dsoftbus` 源码，不是安装 SDK 自带头文件；目标程序未运行、未部署，固件 ABI、loader、SDK drain 和真实设备仍未验证。默认 `NativeBackendUnavailable`、真实 provider 准入、P0/PF-01/PF-02、生产 P4 和 QualificationProfile 状态不变。详见 [Public Socket shim](docs/PUBLIC_SOCKET_SHIM.md)。

> **最新工作包（2026-09-26；artifact run 20260926T093712Z）：**
> `REAL_FASTDDS_PARTICIPANT_OVER_HOST_NATIVE_BACKEND` 达到 `REAL_FASTDDS_PARTICIPANT_HOST_INTEGRATION_VERIFIED /
> CHECKPOINT_READY / NOT_COMMITTED`。限定为真实公开 Fast DDS factory ->
> test-only descriptor -> 现有 Host facade -> SenderResource/
> NativeSendExecution/ProviderAdapter/Binding -> ReceiverResource -> Reader 的
> one-local、one-dial、双 Participant 单进程 Host 链路。独立 focused `9/9 PASS`，
> full Release CTest `346/346 PASS`、exit 0；ASan 新 `9/9`、既有 close `14/14`、
> loop `15/15` 通过。D1+D2+D3/D4 正常链路共有 25 次实际 DataReader take，D5 的
> 真实结果是 non-null Participant 但 input Bind rejected，不能写成 null。mock
> provider、非设备、非 SDK；真实 SDK/device/native qualification、P0/PF/P4 和
> QualificationProfile 仍未完成；P0/PF-01/PF-02 保持 `BLOCKED / NOT QUALIFIED`，生产 P4 未放行，默认保持 `NativeBackendUnavailable`。详见 [Real Participant Host](docs/REAL_PARTICIPANT_HOST.md)。

> **最新工作包（2026-09-26；artifact run 20260925T213718Z）：**
> `HOST_INPUT_CHANNEL_CLOSE_REOPEN` 达到
> `HOST_INPUT_CHANNEL_CLOSE_REOPEN_HOST_VERIFIED / CHECKPOINT_READY /
> NOT_COMMITTED`，限定为冻结 R3 源码上的 one-local、one-dial Host facade。独立
> corrected-environment receipt 为 `32/32 PASS`；随后独立全目标 Release 构建
> exit 0，标准环境完整 CTest 为 `337/337 PASS`、exit 0（canonical receipt：
> [`tester/canonical-full-r3/RECEIPT.json`](.artifacts/host-input-close-reopen/run-20260925T213718Z/tester/canonical-full-r3/RECEIPT.json)，原始日志
> [`ctest.stdout.raw.log`](.artifacts/host-input-close-reopen/run-20260925T213718Z/tester/canonical-full-r3/ctest.stdout.raw.log)，119.84 秒）。首轮
> uninterrupted full Release run 的 `335/337`、exit 8 及其环境失败保留为历史
> 原始证据。Release
> ASan input-close `14/14`、provider-loop `15/15` 通过，variants `1/1` 为
> 构建/default gate。独立 Close、gap drop、TX/native 保留及同 locator reopen 已
> 通过；callback Close 死锁修复经 RED/复测及 m10 deletion evidence 闭合。Debug
> 受 Release-only ABI guard 阻断，TSan 未执行；真实 ReceiverResource、Participant/
> SDK、完整 TransportInterface、native/device/cross-device 仍未验证。详见
> [Host input close/reopen contract](docs/HOST_INPUT_CLOSE_CONTRACT.md)。

> **最新工作包（2026-09-26；artifact run 20260925T202407Z）：**
> `HOST_FACADE_PROVIDER_LOOP` 达到 `HOST_FACADE_PROVIDER_LOOP_HOST_VERIFIED /
> CHECKPOINT_READY / NOT_COMMITTED`。限定 Host 注入 facade 已通过 focused `16/16`
> 与 full CONFIG `323/323`，并保持默认 OFF；真实 Participant、SDK provider、native/device
> 和完整 TransportInterface 仍未执行。详见 [Host facade/provider loop](docs/HOST_FACADE_PROVIDER_LOOP.md)。
> `NativeBackendUnavailable`、P0/PF-01/PF-02、生产 P4 与 QualificationProfile 状态不变。

> **最新工作包（2026-09-26；artifact run 20260925T190646Z）：** `NATIVE_PROVIDER_ADAPTER_DESIGN_PHASE0` 达到
> `NATIVE_PROVIDER_ADAPTER_BOUNDARY_DESIGNED / CHECKPOINT_READY / NOT_COMMITTED`。
> SDK-free `NativeBackend`/`NullBackend` 与 Host-only `ProviderAdapter` 已实现并经
> focused `16/16`、full CONFIG `307/307` 验证；真实 provider、SDK、设备和 DDS 接线
> 仍未执行。详见 [Phase 0 adapter boundary](docs/NATIVE_PROVIDER_ADAPTER_PHASE0.md)。
> 默认 `NativeBackendUnavailable`、P0/PF-01/PF-02、生产 P4 和 QualificationProfile
> 状态不变。

> **当前开发工作包（2026-09-25）：CALLBACK_CONTEXT_DESTROY_CONTRACT = `CALLBACK_DESTROY_CONTRACT_HOST_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`。** 本轮关闭组合层最后一项未闭合寿命合同：**在回调执行上下文内销毁 binding 会自等待**（释放必须等本帧持有的 receiver in-flight 许可）。采用方案 A —— 禁止回调内同步销毁，并提供不等候的关闭请求：`CloseRequest()` 是唯一合法于 ingress/投递回调内的控制入口，只记录关闭意图（相位转 `Closing`）并立即返回，从不 drain、释放或等待；`DeferredClose()` 由所有者在其后的控制上下文完成关闭，走与 `Abandon()` 完全相同的 drain+释放路径；`BeginClose()`/`Abandon()`/`DeferredClose()` 在回调内一律以 `WrongState` 明确拒绝而非等待；从回调内**销毁**则 fail-fast（带指向 `CloseRequest()` 的诊断后 abort），不再死锁。sink/receiver/owner/claim 四条寿命前提与 drain-before-free 语义不变。新增 11 个 case（CLOSE-C1…C7、C2b 完成守卫、无关 binding 与非末位 sibling 两个合法形状、禁止形状的跨进程 case），组合 case 共 32 个；Release/Debug/ASan 32/32、本配置 86/86、仓库标准 CONFIG host Release 290/290；本轮 11 个变异体全部被杀死。合同与执行状态见 [PLAN.md 第 20 节](PLAN.md)，收据与 RED 证据见 [.artifacts/srtc-close/](.artifacts/srtc-close/)；上一轮（SRT-MEM 寿命/并发/上下文与测试判据修复）见 [PLAN.md 第 19 节](PLAN.md) 与 [.artifacts/srtc-repair/](.artifacts/srtc-repair/)。本轮未提交；真实 SDK provider、设备与 DDS 证据仍 `NOT_RUN`。

> 累计实验源码的构建、生成来源、外部证据及资格范围见 [实验基线说明](docs/EXPERIMENT_BASELINE.md)。原始 `.artifacts`、SDK 和设备运行库不随 Git 提交提供。

> **当前性能基线（2026-09-24）：** `DDS_BOUNDED_PERFORMANCE_BASELINE` 已达到 `BOUNDED_PERFORMANCE_BASELINE_MEASURED / CHECKPOINT_READY / NOT_COMMITTED`。限定范围为 `BYTES_NATIVE`、两块 RK3588A、6 个固定配置、3 个独立 MEASURE 重复：正式 RTT 共 3,300 次；固定负载 Reader delivery 共 6,120 次（RTT full-echo 的 Reader delivery 不计入该固定负载合计）。合并 p50/p99 分别为 RTT_1K `3.219125/4.435375 ms`、RTT_4K `3.226709/4.408250 ms`、RTT_256K `11.085668/14.135335 ms`。固定负载 Reader business goodput 为 1K `0.09765625 MiB/s`、4K `0.390625 MiB/s`、256K `1.00000000 MiB/s`；这些是已执行的 nominal loads，不是最大吞吐量。

> **性能基线证据与边界：** final data review 无未解决 blocker；独立 host post-review 为 OFF `243/243 PASS`、ON `245/245 PASS`；`49/49` 是 TRACE 修复前的历史 full-host stage，focused R3 的最终 analyzer 为 `58/58 PASS`。独立双板记录为 query + 失败 TRACE01 + 成功 TRACE02 + M1/M2/M3，12/12 final board guards PASS。Native package 输入 1,737、6 artifacts，focused R3 executor checks 为 PowerShell 4、Python 10、Node 14，包与输入哈希保持一致；native executable SHA-256 为 `5f98207268441debefcfaa57f0396a7be0d1cc2ed9d40e98e49f11211b30a440`。原始 TRACE01 失败保持记录并排除正式统计；CSV 的 19,116 行是 planned-identity rows，不是 deliveries。SDK internal settlement、exact queue HWM 仍 `UNOBSERVED`；仅 M3/A 的两个累计 `NO_APPLICATION` rejected records（不是四个）保留 payload/Writer identity 与 control-versus-business classification 未观测状态；每侧 TX reject 为 2，RX prefix overflow 仅表示诊断细节受限，不证明业务丢失。P0/PF-01/PF-02 仍 `BLOCKED / NOT QUALIFIED`，生产默认 `NativeUnavailable`，P4 未发布，无 QualificationProfile，禁止 stage/stash/commit/amend/push。详见 [bounded performance results](.artifacts/dds-bounded-performance-20260924/run-20260923T183702Z/analysis/RESULT_TABLES.md) 与 [final data review](.artifacts/dds-bounded-performance-20260924/run-20260923T183702Z/review/final-data/FINAL_DATA_REVIEW.md)。

> **资源观察：** 单进程 single-core CPU 为每 case `3.293%–12.126%`；观测 Participant 生命周期跨度为 `85.656–86.118 s`；采样 RSS 最大 `29.824 MiB`，observed VmHWM 最大 `30.133 MiB`。这些是边界观察，不是长期泄漏或 SDK 内部结算证明。

> **历史已关闭工作包（2026-09-23）：DDS_MULTI_ENDPOINT_AND_LATE_JOINER_FUNCTIONAL_MATRIX = `DDS_NORMAL_FUNCTIONAL_MATRIX_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`。** 13 个限定 `BYTES_NATIVE` 双板矩阵场景均通过独立复核：M1/M2/M3/M4 多端点与删除后持续通信、Q_BE_BE/Q_REL_BE/Q_REL_REL/Q_BE_REL 兼容性、L1/L2/L3 晚加入、I1 Topic 隔离和 I2 Domain 隔离。该已关闭矩阵共 102 次原始写入、132 条独立 Reader 交付/native 关联；实际 QoS、当前匹配图、逐 Reader 账本和同轮 native 证据见 [.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/analysis](.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/analysis)。

> **本轮验证边界：** 独立 host 最终 OFF `175/175 PASS`、ON `177/177 PASS`；选定的 OFF/ON native 包分别为 1,693 inputs、6 artifacts，并由 `device-build/BUILD_SELECTION.json` 绑定。13 个矩阵窗口、4 个 native 回归窗口和 1 个 Socket-free directory query 共 18 个注册窗口，其中 17 个为实际 DDS native 窗口；36/36 板端 guards 完成。SDK internal settlement 仍 `UNOBSERVED`；观察到的 27 条非零 SendBytes 返回按原始证据保留，不能推断其统一原因。

> **资格边界保持不变：** P0、PF-01/PF-02 仍 `BLOCKED / NOT QUALIFIED`；生产默认仍 `NativeUnavailable`，P4 未放行，无 QualificationProfile；禁止 stage/stash/commit/amend/push。旧 [restored-package-device 交付线](.artifacts/restored-package-device/run-20260923T075212Z/FINAL_GATE.json) 已正式关闭，本轮未重开。L2/L3 证明的是仍存活 application 内的 DDS 历史/晚加入行为，不是新的 no-application gap 或 PF-02 物理旧帧隔离资格；RAW、三板、mixed transport、ROS 2/RMW、性能和 100 轮新资格均未执行。

> **历史已关闭工作包（2026-09-23）：DDS_KEYED_HISTORY_AND_RESOURCE_BOUNDARIES = `FINAL_CODE_AND_DEVICE_REVIEW_CLOSED / CHECKPOINT_READY / NOT_COMMITTED`。** 十个限定 `BYTES_NATIVE` keyed native cases K1/K2/K3/K4_TRUE/K4_FALSE/H1/H2/H3/R2/R3 均 PASS；R1 为 `HOST_ONLY_PASS`。canonical totals 为 62 writes、128 API data/take observations、58 unique data identities、42 taken identities、3 state records，形成 58 data + 3 state native associations。H1 的 4 个 oldest-before-Reader evictions 属于 Writer `KEEP_LAST` 行为；H2 的重复读取不等于网络重复；`valid_data=false` 记录未解析 payload。详细收据见 [.artifacts/dds-keyed-history-20260923/run-20260923T140748Z/docs/DOCS_SYNC.md](.artifacts/dds-keyed-history-20260923/run-20260923T140748Z/docs/DOCS_SYNC.md)。

> **本轮证据与边界：** 独立 host 最终 OFF `205/205 PASS`、ON `207/207 PASS`，53 analyzer tests、12 post-R4 affected gates 通过；native OFF/ON build 均 PASS，每包 1,719 inputs 且独立核对零 mismatch，设备只执行 OFF 包。14 registered windows（13 DDS attempts + 1 Socket-free query）对应 28 board windows，全部 guards PASS；K1-attempt-01 仅 outer launcher exit 为 `UNOBSERVED`，runner/remote exits 为 `0`，并排除 canonical totals。Fresh small run `87c0d1ef21b9257c` 为严格 actual-native replay PASS；fresh M2 run `50ebee5e4a4e215a` 为 10 writes、两个 Reader 各 10 deliveries、20 native associations。

> **资格边界保持不变：** Reader `get_key_value` 与 Writer `lookup_instance` unsupported，standalone registration propagation 未实现；WriterDataLifecycle 的 broad documentation warning 与本轮 true/false narrow observed behavior 冲突，不能据此宣称全策略合格。SDK internal settlement 为 `UNOBSERVED`；P0、PF-01/PF-02 仍 `BLOCKED / NOT QUALIFIED`，生产默认 `NativeUnavailable`，P4 未发布，无 QualificationProfile。RAW、三板、mixed transport、ROS 2/RMW、性能、100 cycles 及全拓扑资格均未执行。

> **Historical — RX accounting closeout（2026-09-22）：DONE_WITH_HISTORICAL_LIMITATION / NOT_COMMITTED。** 源码封存/恢复及最终 checkpoint 结论以该轮 `FINAL_REPORT.md` / `FINAL_GATE.json` 为准；本摘要不独立宣称恢复验证 PASS。`HOT_REBUILD_BASELINE_CLOSEOUT_AND_RX_REJECTION_ACCOUNTING` 已完成上一轮证据核对、16 条历史接收前 ingress ledger、诊断 delta、分析器测试、定向 1 MiB 与 hot-smoke 验证以及 PF-01/PF-02 契约材料。历史 16 条只有 `EXPERIMENT_NATIVE_INGRESS`，没有 Captured/owned-copy/rejection 字段，具体原因保持 `HISTORICAL_CAUSE_UNRESOLVED`；不能将其改称 DATA_FRAG 丢失或容量拒绝。

> **Historical RX closeout 验证摘要（2026-09-22）：** 独立 host 最终 OFF `148/148 PASS`、ON `150/150 PASS`，focused `8/8 PASS`，最终 RX analyzer `31/31 PASS`；诊断 delta 的 post-review native OFF/ON build 均 `0/0/0`，各有 1,657 inputs、6 artifacts，主程序 SHA-256 为 `6352b28f6b7f3d033bb8ecd58f5319eb4479cfe76d5d0ece461ed21487f1c0fc`（OFF）和 `dedfad61e0c26da6e9d7a5e26b60c21ab8db9130a573d4513a5223a8ce8f3a19`（ON）。新 1 MiB 窗口 `5923cd595106e8a6` 的 203 条 admission 全部 accepted、拒绝 `0`，本次 1 MiB 接收拒绝未复现（`NOT_REPRODUCED`），旧 16 条原因仍为 `HISTORICAL_CAUSE_UNRESOLVED`；新 hot-smoke `98e4d1228566a0a4` 完成 10 条同轮链、10 次 DDS `wait_for_acknowledgments`，并观察到 2 条严格 gap `NO_APPLICATION` 与 1 条 final-cleanup `NO_APPLICATION`。这些是限定窗口证据，不能替代 SDK internal settlement 或 PF 资格。

> **Historical RX closeout 接线边界（2026-09-22）：** Participant ExperimentalTransport facade 接入新的 application/receiver/logical-token attachment，再接共享 ProcessRuntime 与实际 ProcessCoordinator/ApplicationAttachments/InputChannels；每个 endpoint 保有独立 native State/listener/table/owner/worker。P1 detach 排空自身原 lease/TX/RX/last-payload 并保留 live native 表；Q 不受影响；所有 application detach 后才 Finalize native stop/join/tombstone。SDK TX 仍使用原有有界 owned-vector 队列和原 application 联合 drain，不宣称运行于 `NativeSendExecution` provider；RX 是一次 copy 加原 lease，无 application 时当场丢弃。默认 setup=1，双 endpoint runtime 显式 setup=2，池为 `2 setup / 2 send / 1 close`，没有延长 SDK timeout 或新增协议。

> **Historical RX closeout 资格边界（2026-09-22）：** P0、PF-01/PF-02 仍 `BLOCKED / NOT QUALIFIED`；生产默认仍 `NativeUnavailable`，P4 未放行，无 QualificationProfile；Git commit/push 为 `USER_PROHIBITED`。SDK internal settlement 为 `UNOBSERVED`。该历史轮次未修改 admission、调度、预算或生命周期语义；review 未发现该轮 Transport 缺陷。完整收尾记录见 [DOCS_SYNC.md](.artifacts/hot-rebuild-closeout-rx-20260922/run-20260922T012640Z/docs/DOCS_SYNC.md)，源码封存/恢复及最终 checkpoint 结论以该轮 `FINAL_REPORT.md` / `FINAL_GATE.json` 为准。

> **历史：same-name hot-reattach admission（2026-09-21）。** 该旧 STOP-0 结论记录了当时 facade 到 surviving live InputChannel/table 的接线缺口；本工作包现已在限定 live-table app reattach 路径上闭合该历史 STOP-0。OLD2/PF-02 与 full-native-record RED 仍是独立未资格化边界，不能被本次 PASS 删除或提升为生产资格。

> **Historical — Same-name hot-reattach admission (2026-09-21): BLOCKED / NOT_COMMITTED.** At that earlier STOP-0, the facade-to-surviving-live-InputChannel/table bridge was absent and native build/device pilot were `NOT_RUN`; the current limited live-table app-reattach path closes that historical wiring gap. OLD2/PF-02 and the full-native-record RED remain separate qualification boundaries. See [historical hot reattach record](.artifacts/experiment-hot-reattach-20260921/run-20260921T145916Z/FINAL_REPORT.md).

> **最新双实例验证修正（2026-09-21）**：实验验证器修正后的限定 `BYTES_NATIVE` 运行已完成并由独立证据收口。最终 host 套件为 OFF `55/55 PASS`、ON `57/57 PASS`；OFF/ON native bridge/configure/build 均为 `0/0/0`，各 manifest 记录 93 个输入，运行包哈希分别为 `09b16d92…f23c` 与 `5096447d…6f55`。两块 RK3588A 上五个新窗口均通过：双 Participant/P1 删除后 P2 继续写入、默认小消息、256 KiB 分片、RELIABLE 单次受控丢包恢复、BEST_EFFORT 目标样本缺失；每个窗口均为 HDC/remote exit `0`、无 watchdog，并完成撤权与最终 guard。完整字段、历史失败更正和证据索引见 [双实例最终报告](.artifacts/experiment-dual-20260921-second-instance-fix/run-20260921T121708Z/FINAL_REPORT.md)。该结果仅是 default-OFF 的限定实验，不改变 `P0 BLOCKED / NOT QUALIFIED`、`NativeUnavailable`、PF-01–04、生产 P4 未授权、`no QualificationProfile` 或 `COMMIT_GATE=PROHIBITED`；SDK 内部 settlement 仍 `UNOBSERVED`，本工作包未提交。

> **前一工作包：可行性收敛（历史，2026-09-21）**：该工作包状态为 `CLOSED_WITH_PLATFORM_DEPENDENCIES`（可行性收敛已关闭，平台问题未解除）；平台资格仍为 P0 **BLOCKED / NOT QUALIFIED**，生产 DSoftBus Transport 仍为 `NativeUnavailable`。该工作包的 build/host/device 均 `NOT_RERUN`；历史 D 交付窗口 `64/64` 与 V4 `1/64` 不拼接为压力资格，`NEED_ACK` 的 `NotCalled` 保留。只有具体新机制、平台契约、可判别 trace 或明确用户决策才重开对应问题；同一阻塞不会自动启动新一轮全面探针。Fast DDS 集成、生产 native backend、DDS discovery、可靠性、分片和 RMW 未验证，PRE_COMMIT 仍 FAIL，该工作包不可提交。详见 [docs/P0_IMPLEMENTATION_20260920.md](docs/P0_IMPLEMENTATION_20260920.md)、[review](.artifacts/closure-20260921/review/REVIEW.md) 和 [readonly verification](.artifacts/closure-20260921/verification/RESULT.md)。

> **有限例外实验（2026-09-21）**：经用户明确授权，独立的 default-OFF [Real Fast DDS × DSoftBus Socket 实验报告](docs/FASTDDS_DSOFTBUS_EXPERIMENT_20260921.md) 在两块 RK3588A 上完成了限定 R4 BYTES 场景：OHOS 包构建 `0/0/0`、host `7/7`、双板 discovery/Writer/Reader/10 samples/正常 close 均有证据，`SCENARIO_GATE=PASS`。这不改变上述 P0/P4 结论，不是生产放行；RAW、热重建、多 Participant、压力/故障 close、混合 transport 和 native self-route 仍未覆盖，`COMMIT_GATE=PROHIBITED`。

> **前序 R4 实验补充（历史）**：R4 生命周期/observer hardening 已由独立 host `16/16 PASS`、review 和设备复测闭合；硬化包 SHA-256 为 `448663…`。随后 default-OFF fragments host `17/17 PASS`、OHOS `0/0/0`，256 KiB 与 1 MiB 三样本 DATA_FRAG 场景均完成 ACK、Reader mask、正常 close 与无 watchdog 观察；1 MiB 的 79 条 DATA_FRAG 观察含每序列 18 个唯一范围，重复范围不推断 NACK/重试原因。小消息十样本路径仍保留；小/256 KiB guard 边界缺口与 1 MiB 即时撤权 guard PASS 见实验报告。

> **前序 controlled-loss 补充（历史）**：独立 OFF/ON host `21/21`、`23/23 PASS` 与 OHOS `0/0/0` 完成；RELIABLE 的单次 whole-message drop 已由 v2 因果收据关联到 NACK_FRAG、缺失范围重发、ReaderSampleInfo 与 ACK，BEST_EFFORT 则保留 samples 2/3、mask `006`、ACK `NotCalled`。两种窗口均正常清理并恢复 guards；SDK 内部物理结算仍 `UNOBSERVED`。这仍是 default-OFF 限定实验，不改变 P0/P4 结论。

本快照优先于下文较早的 N1/N2/N3 历史记录；那些记录仍保留其原始范围和证据，不计入本轮计划完成度。最新冻结方案见 [docs/FASTDDS_DSOFTBUS_TRANSPORT_V1_PLAN.md](docs/FASTDDS_DSOFTBUS_TRANSPORT_V1_PLAN.md)。

历史 N1/N2/N3 工作包中，ApplicationAttachments 的设计 R1（unique InputChannels/attachment 封装）与代码 R1（Close waiter 等待迟到 Opening）是两个不同问题，均已在各自限定范围通过 review/test；N3-1 的 N2 root public target wiring 也已通过独立 review 与最终复测。64 次仅为模型 application attachment 周期，不是 DDS Participant 或真实同名热重建。这些历史工作包当时未执行 native backend、实际 DDS/SPDP/SEDP/样本、同名 Participant 热重建或 RMW；本次 R4 的新增证据与覆盖范围见上方实验报告。

本仓库用于在不改变 Fast DDS DDS/RTPS 语义的前提下，验证并实现基于 OpenHarmony DSoftBus Socket API 的 Transport。当前仓库包含 S1/S2 探针、S3 独立核心和 D2 发送资源桥接；完整可用于 Fast DDS 生产通信的 DSoftBus Transport 仍未完成。

## 历史工作包状态（N1/N2/N3及前期切片）

以下表格保留各历史工作包的原始范围和证据；它们不是对 2026-09-21
限定 R4 实验的重新判定，也不覆盖完整生产 Transport 的未决资格。

| 项目 | 状态 | 证据 |
| --- | --- | --- |
| Fast DDS 接口探针 | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED | 当前 3.6.2.0 真实头文件与未修改 `TransportInterface.cpp` 源片段；S1 上轮 Host 7/7（`155515Z`），本轮 Host 9/9（`184049Z`）；接口契约内部覆盖 IF-01..04，两板 IF-01..04 各 4/4 为 S1 设备证据 |
| DSoftBus 符号探针 | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED | SDK 23 arm64 构建；两板客户端库 required/optional symbols PASS |
| DSoftBus Socket 通信 | 有界双板记录 PASS；quiescence 未验证 | token-first 运行在 A/B 各方向验证 32/255/4096/16384/60000 字节记录，发送 ≤3 ms、关闭观察 202 ms；权限随后已撤销并恢复原始记录。不等于 SDK 回调排空或生产 Transport |
| S3-A EndpointDirectory | BUILDS / HOST_TESTED（cross build） | Debug/Release、consumer/library-only/cross 共 13 组；仅目录模型及消费者，不是 DEVICE_TESTED 或完整目录 VERIFIED |
| Locator wire characterization | BUILDS / DEVICE_TESTED / REVIEWED（限定范围） | A/B 各 4 组仅验证 LocatorCodec 与真实 Fast DDS CDR/ParameterList；不覆盖 EndpointDirectory 全部算法、DSoftBus records 或 DDS discovery |
| S2-P native 身份/最小权限探针 | DEVICE_TESTED / PASS（限定范围） | A/B 同 ID regrant → Socket 创建/Shutdown 一次 → revoke 闭环 PASS；服务 GRANTED→DENIED、self 还原、JSON guard 和 provider hashes 保持；不等于 Transport 通信通过 |
| PLAN 15.2 thread-token diagnostic | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED（限定范围） | 两板受控测量均 PASS：旧线程保持原 token，新线程观察目标 token，主线程恢复成功；证据 `.artifacts/diagnostics/thread-token/20260914T035011891Z-tester-device/` 及 review `.artifacts/review/20260914T035632Z-thread-token-device-results/REVIEW.md`。不证明 DSoftBus callback 身份或 Socket 根因 |
| RecordPairHarness | IMPLEMENTED / HOST_TESTED / REVIEWED（限定范围） | final host/simulated 测试通过，closure review 完成；不代表真实 Socket 通信 |
| Record timing fix | BUILDS / HOST_TESTED / DEVICE negative TESTED；限定范围 | final Debug/Release 各 21 组、平台交叉构建通过；A/B 拒权模式真实退出 6，非法参数退出 64，符合负例预期。`BindReturned` 是 checkpoint，不等于物理 API 返回瞬间 |
| S3-B SendQueue / S3-D1 batch | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；NOT DEVICE_TESTED | D1 final 独立验证 direct Debug/Release 各 26 组、root Debug/Release 各 14/14、consumer 与 SDK23 arm64 static build PASS；review `.artifacts/review/20260914T040359Z-send-batch/REVIEW.md` 无 blocking。仅证明队列原子接纳与记录所有权移交，不证明 SDK Send/真实 Transport |
| S3-D2 RuntimeSendPath / SenderResource bridge | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；NOT DEVICE_TESTED | `.artifacts/verification/runtime-send-final-20260914T0527Z/RESULT.md`：D/R direct 各 27 组、root D/R 各 17/17、public consumer、SDK23 arm64 static build 均 PASS；review 无 actionable finding。仅证明 D2 发送计划与资源桥接，不证明 native backend、SDK 实际 Send、DDS/RMW |
| S3 real Fast DDS binding | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED（限定范围） | host SOURCE D/R 各 17/17、source consumer 1/1、CONFIG matrix 32/32、CONFIG root 17/17、flat 2/2、nested 4 组、poison/absent SOURCE 17/17；SDK23 arm64 与两板 ABI/model、wrong-architecture rejection 均 PASS。预加载 `fastdds`/`fastcdr`/`foonathan_memory` 必须是 `IMPORTED_GLOBAL`，Release 映射仅未设置或单一 `RELEASE`。证据见 real-binding final host/SDK 与 fix review；bare-PATH、长路径、初始部署失败保留。仅证明真实配置/ABI 绑定，不证明 DSoftBus Socket、DDS discovery、同名 Participant 热重建或 RMW |
| S3-LC Socket lifecycle/callback close | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（LC runner 限定范围）；NOT DEVICE_TESTED | 独立 final `.artifacts/verification/lc-runner-final-20260914T120200Z/RESULT.md`：80/42/130/16/169/10 + syntax 4 及私有 real-fd/process diagnostic PASS；review closure `.artifacts/review/20260914T-lc-runner-code/CLOSURE.md`。collector exit-before-probe race 已修复；full log overlap、SDK quiescence、设备 Socket/Transport 仍未验证 |
| ApplicationAttachments root wiring | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；NOT DEVICE_TESTED | `.artifacts/verification/attachments-wiring-final-20260914T093400Z/RESULT.md`：选项 7/7、SOURCE Debug/Release 22/22、CONFIG Release facade ON 23/23、consumer、既有 binding/facade 矩阵和 SDK23 arm64 link PASS；接线 review `.artifacts/review/20260914T-attachments-wiring-code/REVIEW.md`（SHA `89C726AF62F1B001523C2221B9BBE70DF74DC56927D544C67984900517912418`）无 blocking。仅为根构建接线和外部 consumer，不含 HDC、SDK runtime、DDS 或热重建 |
| N1 stable native owner | IMPLEMENTED / BUILDS / HOST_TESTED / SDK23 ABI LINK/ELF TESTED / REVIEWED（模型/owner slice 限定范围） | [N1 completion record](docs/N1_COMPLETION.md) 与 `.artifacts/verification/n1-completion-final-20260914T1455Z/RESULT.md`：候选树 87/87 字节一致、SOURCE D/R 23/23、CONFIG 24、root options 9、attachment 7、consumer 1、standalone、20 次重复、SDK23 link/readelf PASS；fresh cache 中默认 `BUILD_DSOFTBUS_NATIVE_OWNER` 跟随 `BUILD_DSOFTBUS_DIRECTORY`，测试仅在 `BUILD_TESTING=ON` 时注册。该结果不是 SDK runtime/设备验证 |
| N2 native send execution | IMPLEMENTED / BUILDS / HOST_TESTED / SDK23 ABI LINK/ELF TESTED / REVIEWED（SDK-free injected synchronous model 限定范围） | [N2 completion record](docs/N2_COMPLETION.md)；最终 receipt SHA `3AC3B952B424D3EDFF1A16D584463ABA89F369D9E0A37527316B32D3955612F7`，SOURCE Debug/Release 各 24/24、CONFIG 25/25、20×10 repeat、N1 9/9、attachments 7/7、binding 32/32、facade 4/4、SDK23 AArch64 link/readelf PASS；review SHA `227E9479AC4CA318B1B89BD51AE66D554ED47F69819DE2B13E1CAB8CA6D660D7` 无 actionable finding。真实 Socket/SDK Send、DDS/RTPS、设备、跨设备、RMW、同名 Participant 热重建 NOT RUN；scoped model gate PASS，不等于 Transport DONE |
| N3-1 N2 root wiring | IMPLEMENTED / BUILDS / HOST_TESTED / SDK23 LINK/ELF TESTED / REVIEWED（限定范围）；NOT DEVICE_TESTED | [N3-1 completion](docs/N3_WIRING_COMPLETION.md)；最终 tester `.artifacts/n3-execution/wiring-final/RESULT.md`：root/source-subproject 10/10，SOURCE Debug/Release 各 24/24，CONFIG Release 25/25，既有 N1/attachments/binding/facade 9/7/32/4，外部消费者 1/1/2，SDK23 AArch64 link/readelf PASS；唯一 review P2 已修复并由 clean-parent 10/10 复测。仅证明根构建接线，不含真实 backend、DSoftBus、DDS、设备、热重建或 RMW |
| Fast DDS 主机库构建 | BUILDS / PASS（准备阶段） | `.artifacts/build/fastdds-logs/HOST_BUILD_RESULT_20260914.md`：实际 237/237 编译、install exit 0；仅证明主机库构建准备完成，不是 Transport、DDS 集成或上游测试通过 |
| Fast DDS Transport / DDS / RMW 集成 | Descriptor facade IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；native backend、DDS/RMW NOT RUN | 真实 Descriptor/Transport factory、接口与 selectors 已完成限定范围验证；native backend 缺失时 production init 固定失败，私有 HostModel 仅供测试。N1 owner host/source/consumer 与 SDK23 ABI recovery PASS（限定范围）；真实 SPDP/SEDP、用户数据、同名 Participant 热重建与 RMW 未执行 |
| N3 frozen lifecycle model | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；SDK23 LINK/ELF TESTED；NOT DEVICE_TESTED | [N3 completion](docs/N3_COMPLETION.md)；final root Debug/Release 26/26，Coordinator 13、Process 8、N2 17、AA 9、Input 16、Runtime 30、N1 11；CONFIG facade 21 + Coordinator 13；N1 consumer 9/9、N2 consumer 10/10；SDK23 两个 ELF64/AArch64，补充 manifest 另验证 N2/Process/Coordinator 三个 ELF；review P2 已闭环。仅证明 SDK-free 同 local 模型组合，不证明真实 DSoftBus、DDS 或实际同名 Participant 热重建 |
| SRT-1 共享 service-record 表模型 | IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；NOT DEVICE_TESTED | `PLAN.md` §18 冻结契约的有界实现：`include/mdds/dsoftbus/ServiceRecordTable.hpp`、`src/ServiceRecordTable.cpp`、`tests/service_record_table_tests.cpp`（15 case × 独立进程）。Release 15/15、Debug 15/15、standalone 全量 43/43、CONFIG host Release 全量 258/258、`/W4` 无告警；RED 为 stub 15/15 行为失败并与 GREEN 共用同一 test/header 字节。收据 [.artifacts/srt1/README.md](.artifacts/srt1/README.md)。不含 SDK 调用、native backend 接线、设备、DDS、热重建或 RMW |
| 总体 | NOT VERIFIED | N3 frozen host model 已实现并通过限定范围复测；真实 native backend、callback quiescence、DDS/RMW、设备/跨设备和同进程同名 Participant 热重建仍未验证；这是完整 Transport 发布边界。**更新（2026-09-28）**：在该行所指的默认/生产 Transport 之外，显式实验路径（PublicSocket + 真实 DDS）的双板通信与同进程同名 Participant 单轮热重建**已有设备证据**，见本文档顶部当前基线与 [PLAN.md](PLAN.md) §21；默认 backend 仍为 `NativeBackendUnavailable`，该行的历史判定不改写 |

完整基线、哈希和执行记录见 [docs/BASELINE.md](docs/BASELINE.md) 与 [docs/STATUS.md](docs/STATUS.md)，实施计划见 [PLAN.md](PLAN.md)。

## 仓库布局

- `Fast-DDS`: Fast DDS 下游源码子模块。
- `communication_dsoftbus`: OpenHarmony DSoftBus 下游源码子模块。
- `probes/`: 当前接口和 DSoftBus 能力探针。
- `tests/`: 探针契约测试与 host-only fixture。
- `cmake/`: 独立探针构建配置。
- `.artifacts/`: 被忽略的构建产物、测试日志和设备证据。

## 初始化子模块

新 checkout 可执行以下命令初始化当前 root 记录的子模块版本；不要用 `pull` 替代 pin，也不要在此步骤提交子模块指针：

```powershell
git submodule update --init --recursive
```

## Host 构建和测试（Windows）

```powershell
cmake -S . -B .artifacts/build/host-capability -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DBUILD_NATIVE_PERMISSION_PROBE=OFF -DBUILD_INTERFACE_PROBE=ON -DBUILD_SOFTBUS_CAPABILITY_PROBE=ON
cmake --build .artifacts/build/host-capability --config Debug --parallel 4
ctest --test-dir .artifacts/build/host-capability -C Debug --output-on-failure
```

最终 N1 root CTest 需在该测试进程内设置 machine PowerShell module path，并使用绝对 host DLL `bin` 前缀；这不是全局环境变更。此前模块查找/相对 DLL 路径失败保留为环境边界证据，不计为通过。

当前权限生命周期 run 的 canonical Host suite 证据为 10/10 tests passed：接口契约 CTest 内部覆盖 IF-01..04，另有 capability cases、`permissions.flow` 的 10 个权限流程 groups、`snapshot_guard` 的 158 checks 和记录契约。fixture DLL 仅用于 host capability 测试，不部署到设备，也不代表设备上的 DSoftBus 实现。

## OpenHarmony arm64 构建

```powershell
$sdk = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native'
cmake -S . -B .artifacts/build/ohos -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DBUILD_NATIVE_PERMISSION_PROBE=OFF -DCMAKE_TOOLCHAIN_FILE="$sdk/build/cmake/ohos.toolchain.cmake" -DCMAKE_MAKE_PROGRAM="$sdk/build-tools/cmake/bin/ninja.exe" -DOHOS_ARCH=arm64-v8a -DBUILD_INTERFACE_PROBE=ON -DBUILD_SOFTBUS_CAPABILITY_PROBE=ON
cmake --build .artifacts/build/ohos --parallel 4
```

基础构建显式保持 `-DBUILD_NATIVE_PERMISSION_PROBE=OFF`，避免复用已有 cache 中的 ON 设置；这只构建基础探针。权限 probe 已使用官方平台 clang、原始 `__h` libc++ 头文件和板端 `libc++.so` 单独构建/链接并完成两板受控生命周期验证；早期应用 SDK `std::__n1` 链接失败属于历史候选路径。默认 CMake permission target 尚未自动切换该平台工具链，复现时须使用独立 build 目录并显式记录工具链。

ARM64 二进制不能在 Windows 主机直接运行 `ctest`，必须通过 `hdc` 部署后执行。

## RK3588A 设备探针

最终验证部署目录为 `/data/local/tmp/fastdds_dsoftbus_probe_20260913T155515Z/`。每块板均部署 `fastdds_interface_tests`、`softbus_capability_probe` 和 SDK `libc++_shared.so`；fixture 不部署。设备执行形式为：

```text
hdc -t <target> shell "cd /data/local/tmp/fastdds_dsoftbus_probe_<run> && LD_LIBRARY_PATH=. ./fastdds_interface_tests IF-01"
hdc -t <target> shell "cd /data/local/tmp/fastdds_dsoftbus_probe_<run> && LD_LIBRARY_PATH=. ./softbus_capability_probe --library /system/lib64/platformsdk/libsoftbus_client.z.so"
```

实际验证器为每条命令设置 10 秒外部 watchdog；上面的裸 `hdc shell` 示例本身不提供 watchdog。watchdog 触发必须记为失败，不能当作正常取消机制。Socket 检查通过 `--socket-check` 使用 `com.kaihong.mdds` 的 pkgName 字段和独占本地名称前缀；这不证明 hdc shell 进程具有该应用的 AccessToken 身份。最终运行命令示例为：

```text
hdc -t <target> shell "cd /data/local/tmp/fastdds_dsoftbus_probe_<run> && LD_LIBRARY_PATH=. ./softbus_capability_probe --library /system/lib64/platformsdk/libsoftbus_client.z.so --socket-check"
```

早期无授权 Socket 检查两板均为 probe exit 3（`Socket()` 返回 `-426115032`，约 5 秒）；本轮授权闭环中的独立 Socket 创建/Shutdown 已 PASS。HDC 外层 exit 0 不等价于探针成功；当前证据仍不包含跨设备记录或 Fast DDS 通信。

## 下游来源

- Fast DDS: `git@github.com:eProsima/Fast-DDS.git`
- OpenHarmony DSoftBus: `git@gitcode.com:openharmony/communication_dsoftbus.git`

## 最新证据更正（2026-09-14）

`InputChannels` S3-C 接收关联/交付寿命核心已完成 R1 修复、OOM 修复、独立 review 与最终限定范围验证：root CONFIG CTest 20/20、SOURCE Debug/Release 各 19/19、facade repeat 20 次、FCD 21/21、IC-12 及 public consumer 均 PASS。证据见 `.artifacts/verification/facade-host-final-20260914T075048953Z/RESULT.md` 与 `.artifacts/review/20260914T-facade-code/REVIEW.md`。这只证明该生产核心及 facade 的 host/build 契约，不证明 DSoftBus SDK 回调、真实 Transport、DDS discovery、用户数据或 RMW。

token-first 双板 Socket record 实验已在有界范围 PASS：A/B 各方向五种完整记录均通过字节、长度和计数校验；A Listen、B 同步 Bind 和关闭观察均有记录。证据为 `.artifacts/diagnostics/token-first/20260914T051101677Z-execute/` 与 `.artifacts/verification/20260914T045328Z-token-first-device/`；该实验明确记录 quiescence 未验证、DDS 未运行、Transport 未实现。此前 exit=6 的尝试保留为历史证据，不与本轮成功实验混同。整体仍 NOT VERIFIED（完整 Transport 发布未就绪）；N1 限定执行门已 PASS，提交须单独满足范围、文档、图分析与 Git 门槛。

线程身份诊断已完成独立 review、平台构建和两板受控测量。设备证据目录 `.artifacts/diagnostics/thread-token/20260914T035011891Z-tester-device/`；原体/runner 的 `032800Z`、`034800Z` 目录名保留为标签，实际执行时间以其中记录为准。该结果只支持本次短命诊断观察到的线程 token 行为：A 旧线程 `806246851` 保留、主线程目标 `671793191`，B 旧线程 `806017503` 保留、主线程目标 `671320465`；两板均恢复主线程 token、准确 PID 消失且持久 JSON/stat/label/guards 未变。它不证明 DSoftBus callback 使用何种身份，也不证明 Socket 失败根因。新的 completion IPC 记录已执行但仍 FAIL（0 bytes、remote exit 6），最终只读 manager 预检为 DENIED/before-select；证据见 `.artifacts/diagnostics/record-bind-readonly/20260914T041328638Z-execute/` 与 `.artifacts/verification/20260914T035259996Z-record-completion/final-readonly-20260914T041700Z/`。

后续真实 Transport、SPDP/SEDP、用户数据和 ROS 2 RMW 集成必须在 S2 Socket 契约及终止条件得到证据支持后再推进；不能由当前探针结果推导完成。

## 当前 ROS 2 Host 集成收据（2026-10-02）

本轮范围限定为 Host 集成与安装消费，设备验证为 `N/A_HOST_ONLY`。候选基于
`8077386403b81cd75cf691bf883ec0769bf6a1b0`，包含 38 个经选择收据绑定的源码/测试文件；独立
review 结论为 `NO_ACTIONABLE_SOURCE_FINDINGS_IN_HOST_SCOPE`。公开项目和安装包名为
`FastDDSDSoftBusTransport`，C++ 命名空间为 `fastdds_dsoftbus`；原版 `rmw_fastrtps` 未修改，Fast DDS 2.14.5
隔离前端与默认 3.6 依赖分开核对。

最新 Host 双进程运行 `bf9a3fe141ababbe` 实际完成 Reliable 与 Best Effort 双向各 5 条，合计
20 writes/20 takes；真实 ROS graph、service request/reply、应用及 Runtime 正常关闭均有原始收据。
独立安装 prefix 外部消费者完成构建，prefix relocation 为 10/10；builtin/XML、DataSharing
冲突拒绝、required 缺失/错误/bridge 不可用 fail-closed、禁转发 0/0 等边界通过。默认无
required 配置的测试确认 Node 可创建并可关闭，但因没有隔离 peer 退出 4，不能作为应用成功。

完整 CONFIG 原始执行为 540/543、退出码 8；两个环境问题后续 2/2 修复通过，子依赖路径修复
1/1 通过，原始失败收据保持不变。ASan 使用同源码范围的既有 53 项映射复用，Fast DDS、Fast
CDR 和 foonathan 依赖未插桩。上述 Host 结果不证明 OpenHarmony 设备、DSoftBus SDK、SDK
epoch/drain/cancellation 或生产资格；默认 `NativeBackendUnavailable`、P0/PF/P4 和
`QualificationProfile` 状态不变。

历史 ROS 设备 attempts 仍为 `FAIL`，其查询/数据额度账本保持 `0`，未因本轮 Host 验证改变。

证据入口：[当前 Host 集成收据](.artifacts/ros2-host-integration/run-20261002-current/FINAL_REVIEW.md)。
构建依赖、安装目录和外部输入属于本机只读材料，不随项目 clone 提供。

## 最新 ROS 2 双板收尾验收（2026-10-04）

最新受限设备窗口已完成：`ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_VERIFIED`。
查询 `d1c2cb4097279214` 和数据窗口 `be8cb2d90f287223` 使用 DevEco/OpenHarmony SDK23
构建的 `build/ros2-target/r2` 包，通过 SDK23 HDC 部署到登记的两块 RK3588A。普通
`rclcpp` → 未修改 `rmw_fastrtps` → Fast DDS 2.14.5 → `FastDDSDSoftBusTransport`
→ DSoftBus Socket/Binding → 对端 ROS 回调路径完成了 RELIABLE 和 BEST_EFFORT 双向各
5 条、合计 20/20 Topic 交付；graph 和 service request/reply 分开验收，正常
rclcpp/RMW/DDS/Runtime 关闭、进程退出和环境恢复均有独立收据。两端原始 Topic、
Publisher/Subscriber、Writer/Reader 删除及 Participant 删除均返回 0；native 记录与
20 条接收建立了同 run 的结构关联，但不证明 SDK 内部 event epoch 或物理包逐一因果。

本结果是限定双板功能范围的 `DEVICE_TESTED` / `VERIFIED`，不是全项目生产完成。实际
目标为 Fast DDS 2.14.5、Fast CDR 2.2.5、RMW 8.4.3、rcl 9.2.11、rclcpp 28.1.22，
构建与临时输出均在仓库 `build/ros2-target` 内。目标包含 109 个成员，原版 RMW
未修改；paired pre-native-close 协调、creation-complete 识别和串行控制修复均有源码与
独立测试收据。设备验收记录产生时 HEAD 为
`4807765f53826ed0b882cac32610acb17ee30b4f`，当时尚未提交或推送；后续提交状态以 Git
历史为准。

仍未证明 SDK epoch/drain/cancellation、任意故障安全、异构 OpenHarmony/非 OpenHarmony
互通或完整 ROS 工具链兼容性；P0/PF/P4、默认 `NativeBackendUnavailable` 和生产资格不变，
未生成 `QualificationProfile`。完整证据见 [paired-close 最终报告](.artifacts/ros2-topic-rtti-repair-20261004/paired-close/FINAL_REPORT.md)
和 [证据索引](.artifacts/ros2-topic-rtti-repair-20261004/paired-close/EVIDENCE_INDEX.json)。
