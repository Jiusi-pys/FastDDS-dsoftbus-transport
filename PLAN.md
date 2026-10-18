# Fast DDS / OpenHarmony DSoftBus Transport 实施计划

> **Publication-state timestamp:** Any R28 Main gate/publication/CI pending statement in this document describes the **2026-10-18 16:41 UTC pre-commit documentation snapshot**, not a permanent current-state assertion. The finite scene evidence is PASS; Main records the actual commit, remote branch and CI outcome separately in root-relative `build/r28-repeated-restart/main-repeated-restart-publication/POST_PUBLISH.json` when publication completes. No future commit SHA or CI success is prefilled.

> **Current R28 update (2026-10-18 16:41 UTC):** Two explicit bounded normal B replacements (B0 -> B1 -> B2), with one continuous A and 15 String pub/sub echo roundtrips, passed independent real RK3588A cross-device acceptance. Business 34.532/35 s; resource samples do not prove leak freedom or long-duration operation. Tester gate PASS for the finite R28 scope; Main graph/PRE_COMMIT and R28 publication pending. R27 is already published as `46ef5523fdee66421dc29b23aaeae9e095d8fa0d` (14:14:15.875911 UTC exact-SHA CI NOT_RUN). Next: complete this stage's gate/publication, then choose a separate bounded recovery case and fresh device admission; do not increase the two-cycle allowance or timing limits implicitly. Default automatic reconnect, SDK epoch/drain, service/action recovery and full ROS 2 remain unverified; R22 STOPPED. [Evidence and matrix](docs/ROS2_REPEATED_RESTART_ACCEPTANCE.md). Older pending-publication and blocked notes below are dated historical snapshots.

> **Current R27 update (2026-10-18 13:52 UTC):** Newly authorized Q3 completed one explicit ON bounded B process replacement while A retained its process, Runtime, Participant, endpoints and listener. PRE5/POST5 String pub/sub echo, normal exits and identity restoration passed independent device acceptance. Tester gate PASS for ten sources and this experiment only; Main graph/PRE_COMMIT and R27 commit/push/CI remain pending. Next: finish the publication gate, then separately qualify further normal-process recovery cases with fresh admission and unchanged safety bounds; S3 both-process cold restart, repeated/long-duration recovery and service/action recovery remain NOT_RUN. Default automatic reconnect is not enabled. Full ROS 2 NOT VERIFIED; R22 STOPPED. [Q3 evidence and limits](docs/ROS2_PROCESS_RESTART_ACCEPTANCE.md). The following 13:08 stop is a preserved historical snapshot, superseded for Q3 by explicit authorization and execution.

> **Current R27 stop (2026-10-18 13:08 UTC):** Ten-source process-restart candidate is implemented, freshly built OFF/ON and independently reviewed/host-tested, but both real windows are BLOCKED and the candidate remains UNCOMMITTED. Window1 had early bounded ACK failure followed by a tail-budget HDC timeout; after the reviewed probe-only 2-to-5-second ACK repair, window2 had raw PRE phase completion but a zero-output HDC response with a missing remote exit marker. Neither obtained accepted PRE/restart ARM/B1 launch. Device-dependent work is paused; do not run a third window. Next: preserve and diagnose existing host/capture evidence, then only after the blocker is resolved and dependent work resumed use fresh separately reviewed device admission. No timeout/remote-marker/NO_REACCEPT relaxation, service/network/permission change or normal R27 commit. Ready for commit NO; full ROS 2 NOT VERIFIED; R22 STOPPED. S1 `9cdbc84ed1371d53f4ee53b88dad5b3e5f1612ab` is published (11:45:04.427145 UTC CI NOT_RUN). [Blocked acceptance and evidence](docs/ROS2_PROCESS_RESTART_ACCEPTANCE.md). Older plans below retain their original scope.

> **Current R26 S1 stage (2026-10-18 11:35 UTC):** Real RK3588A run `f31ef86f8ed07cf2` passed 200 ordered String pub/sub echo roundtrips (400 publications, 800 side-local payload records), normal paired exit, four owned PID absences and both identity restorations, with no signals. This is not ROS service/action traffic. Final tester Commit gate PASS covers four source files; Main graph/PRE_COMMIT/publication remain pending. Prior record/play `6d025da0efd088be46fb59fdadfd18755f5cc31e` is published (10:45:07.955187 UTC CI NOT_RUN). Next: after publishing S1, continue checking the actual package compile flag/control qualification and pursue the bounded S2 surviving-A/B-restart case within existing safety constraints, with the required independent architecture/experiment decision; it is BLOCKED_CURRENT_DEFAULT_NO_REACCEPT, not proven impossible. S3 both-process cold start is NOT_RUN; same-process Participant recreation/SDK epoch/quiescence and leak freedom are NOT_PROVEN. Do not silently enable replacement or weaken NO_REACCEPT. S1 does not qualify long-duration/concurrent/large-message/general-QoS behavior. R22 STOPPED; full ROS 2 NOT VERIFIED. [Evidence and next gate](docs/ROS2_SUSTAINED_ROUNDTRIP_ACCEPTANCE.md) and [current matrix](docs/STATUS.md). Older plans below are dated snapshots.

> **Current bounded stage (2026-10-18 10:33 UTC):** Real RK3588A record5 `1403cdfc8beb937b` recorded ten String messages A-to-B; play1 `5ca772d545190a98` replayed the same immutable SQLite bag B-to-A with one successful Resume service call and ten byte-identical ordered CDR payloads. Normal close/exit, owned PID absence and token restoration passed. Final tester Commit gate PASS covers seven source files; Main graph/PRE_COMMIT/publication remain pending for this stage. Earlier ABI `06e5917abd8906a5d17066b87223d239109c5374` and offline oracle `ee04d945b993b4e7b4cdc96f9773afa3a18817ff` are published (latest exact-SHA CI 05:52:42 UTC NOT_RUN). Next acceptance must separately cover remaining types/QoS/reconnect/large-message/stress gaps; SDK epoch/quiescence is unproven, dedicated SEDP unqualified, unique-flow unavailable/strict untested. R22 STOPPED; full ROS 2 remains NOT VERIFIED. [Commands and evidence](docs/ROS2_ROSBAG_RECORDPLAY_ACCEPTANCE.md) and [current matrix](docs/STATUS.md). Older paragraphs below are dated snapshots.

> **Current bounded stage (2026-10-18 05:45 UTC):** Rosbag no-Node ABI prerequisite is published as `06e5917abd8906a5d17066b87223d239109c5374` on the existing branch; exact-SHA CI at 05:23:02 UTC was NOT_RUN. The next two-file offline closed-SQLite/CDR oracle has independent review and final host tests PASS (23 tests, 63 independent controls); tester Commit gate PASS covers this helper only. Main graph/PRE_COMMIT/publication remain pending for the helper. Next: implement and separately qualify real cross-board recording, post-close collection, native replay and received payload/order checks. Device/native-reader/record-play execution for this helper is NOT_RUN; system overall NOT VERIFIED. Both registered boards were USB Connected in the read-only 05:42:32 UTC observation, which is not communication evidence. R22 STOPPED. [Oracle evidence](docs/ROS2_ROSBAG_SQLITE_ORACLE.md) and [current matrix](docs/STATUS.md) supersede older snapshots only within this scope.

> **Current bounded stage (2026-10-18 02:06 UTC):** The five-file rosbag build/composition/no-Node ABI prerequisite has independent final tester Commit gate PASS. Unchanged native build and post-review host evidence were revalidated; real RK3588A run `daf356bea873e640` proves imports, fixed CDR, native QoS conversion and stock parser behavior on both boards. No Node, recording/playback or ROS communication was exercised; `teardown_observed=0` supplies no Transport-close evidence. Next: separately implement and review native cross-board record, immutable SQLite payload/order verification and fresh-window playback, then execute the required host/device gates. Main graph/PRE_COMMIT/publication for this prerequisite remain pending. Prior Action documentation was published as `a0a4fcdbe0b1e2ae7c4f32abbe0f5c08074f17c3` (23:41:04.775 UTC CI NOT_RUN). R22 remains STOPPED. [Current matrix](docs/STATUS.md) and [ABI acceptance](docs/ROS2_ROSBAG_ABI_ACCEPTANCE.md) qualify only this scope; older paragraphs remain dated snapshots.


> **Current bounded stage (2026-10-17 23:34 UTC):** Echo readiness repair was published as `cb9c54dc6564362b52dc652e55ae9f7b5559749e` on the existing branch (22:49:05 UTC CI NOT_RUN). Stock ordinary Action CLI AB/BA now has fresh real RK3588A PASS for one positive Fibonacci order=6 goal per direction, five ordered feedback prefixes and exact SUCCEEDED result. Published sources and native package are unchanged; previous independent review/post-review tests and build evidence were revalidated, not rerun. The final execution gate supports documentation-only synchronization; Main graph/PRE_COMMIT/publication remain pending. Ordinary CLI cancel/reject/abort/concurrent goals, rosbag, broader QoS/reconnect/load and full ROS 2 parity remain UNVERIFIED; R22 stays STOPPED. Next work requires separately scoped acceptance for these gaps. [Current matrix](docs/STATUS.md) and [ordinary Action CLI evidence](docs/ROS2_ORDINARY_CLI_ACTION_ACCEPTANCE.md) supersede older NOT_RUN statements only within this fixed scope. Historical plans below remain snapshots.

> **历史计划归档说明（实验基线整理）**：下列带日期的状态均为当时快照，包括旧的 INVESTIGATING 和 PRE_COMMIT 记录；不作为当前提交许可。当前限定实验范围、外部依赖和证据边界以 [EXPERIMENT_BASELINE](docs/EXPERIMENT_BASELINE.md) 为准。P0/PF 仍 BLOCKED，生产默认 NativeUnavailable；准备候选不等于授权提交。

> **当前状态更正（2026-09-21）**：工程任务恢复为 `INVESTIGATING / BREAKTHROUGH_ANALYSIS`，平台资格仍为 P0 `BLOCKED / NOT QUALIFIED`，生产 Transport 仍为 `NativeUnavailable`。当前 Host Debug/Release CTest 各 35/35；BYTES 1 秒候选预算 FAIL，RAW/RAW_ENCRYPTED native 压力已观测但各仅 1/64 接收且取消未触发；六项 RAW 边界逐字节通过但不形成生产容量资格。P1 纯逻辑和必要 P2/P3 主机诊断可继续，P4 native backend 未进入，三板测试 `DEFERRED`，PRE_COMMIT `FAIL`。详见 [docs/P0_IMPLEMENTATION_20260920.md](docs/P0_IMPLEMENTATION_20260920.md)；其余内容保留为历史计划，不能当作完成证据。

> 当前规划裁量（2026-09-15）：N3 范围和七条硬契约已由用户冻结，见 §17。N2 已提交为 `def10ea376cb3bf9921c1c7ea74c92b1da9e04e8`；下文 N2 “未提交”等文字保留为原提交前快照。N3 仍为 PLANNED，不包含本项目 install/export package，也没有新增真实 backend、DDS 或同名 Participant 热重建通过证据。

> LC pure-validator 当前证据（2026-09-14）：实现已存在；独立 final host tester、code review 与 review 后复测均 PASS。已记录的那次实验结束时两板权限已撤销，但身份记录保留；这不等于设备启动或 Transport 验证。LC observation runner 四文件的独立 final tester 已通过 harness 80/80、runner 42/42 及相关 validator/diagnostic；full log overlap、SDK quiescence 和设备 Transport 仍未验证，整体仍 NOT VERIFIED。N1 tester 的 scoped gate 已 PASS；Main 必须在提交前独立完成文档、最终图与 Git 门槛。

> 用户新增硬要求（2026-09-14）：首版必须支持同名 Participant 在同一进程内热重建。此前“不复用已关闭 SoftBus 名称/ID，从而不支持同名热重建”的 native backend 候选方案未获批准，不可作为首版范围限制。按相同 DDS Participant 名称与逻辑配置建立销毁/重建验收，不以修改 DDS 名称或重启进程通过。进程级稳定底层端点与每代 receiver 交付寿命分离的方向已独立审核；首个纯接收 attachment 切片按 §16.9 修订后批准 TDD。它不能继承旧队列任务、伪造 SDK 回调代数或改变 DDS/RTPS 语义，尚不是实现/设备通过结论。

> 最新执行状态（2026-09-14）：S3-D2 RuntimeSendPath / DSoftBusSenderResource 已完成限定范围 TDD、独立 final tester 和 code review；`.artifacts/verification/runtime-send-final-20260914T0527Z/RESULT.md` 报告 D/R direct 各 27 组、root D/R 各 17/17、public consumer 和 SDK23 arm64 static build PASS。token-first 双板 Socket record 实验也在有界范围 PASS，证据为 `.artifacts/diagnostics/token-first/20260914T051101677Z-execute/` 及 `.artifacts/verification/20260914T045328Z-token-first-device/`。Fast DDS 主机库准备构建另已 237/237 编译并安装成功，记录于 `.artifacts/build/fastdds-logs/HOST_BUILD_RESULT_20260914.md`。这些证据不等于 SDK callback quiescence、完整 native Transport、DDS/RMW 或整体 VERIFIED；已记录的那次实验结束时两板权限已撤销，但身份记录保留，原始记录已恢复。

线程身份诊断已完成独立 review、platform build 与两板受控测量；设备证据为 `.artifacts/diagnostics/thread-token/20260914T035011891Z-tester-device/`，实际执行约 03:50:12–03:50:41 UTC。结果仅支持被测线程的 token 观察，不证明 DSoftBus callback 身份或 Socket 失败根因。新的 completion IPC 记录已执行但仍 FAIL（A/B remote exit=6，双方 HDC exit=0，0 bytes），Main audit 为 `.artifacts/verification/20260914T035259996Z-record-completion/MAIN_AUDIT.md`；manager 只读预检为 DENIED/before-select，不得据此宣称根因已修复。

## 1. 目标、授权与执行状态

状态更正（2026-09-14）：N1 精确提交已完成（HEAD `2c28e9325b941a2b8c0f198b0ddd82f90c4b073e`，tree `8147a261e437cb3a62b59ccd7c9a3f456be93bd5`）。N2 是未提交的工作树范围；用户已授权 N2 模型单独提交，本记录仍是提交前证据快照，不等于完整 Transport 验证。下文较早的 N1 “仍须提交”描述属于历史门槛记录，不代表当前状态。

后续 LC 实机启动准入补充：按 `.artifacts/review/20260914T-lifecycle-launch-binding/MAIN_DECISION.md` 完成的纯校验器之后，LC observation runner 四文件 TDD 已获批准（`.artifacts/review/20260914T-lc-runner/MAIN_DECISION.md`）；最终独立 tester 已通过 harness 80/80、runner 42/42 及相关 validator/diagnostic。runner 必须保留固定 profile、原始证据与权限边界；它不授予权限，也不证明 Transport 或同名 Participant 热重建。实际设备启动需独立验收，本项不改变同名 Participant 热重建硬要求。

目标是在 Fast DDS Transport 扩展边界内接入 DSoftBus 公共 Socket API，保留现有 DDS/RTPS 语义、发现、QoS、可靠性、分片和历史缓存。用户已审阅整体方向及两轮修订，并于本轮要求保存本计划、按 AGENTS.md 开始执行。先完成计划审定，再以 TDD 实现；本文件不是测试通过证明。

当前状态：S1、S2-P、S3-A/B/C、S3-D2 及 facade 组合层均已在各自限定范围取得证据；token-first 双板记录实验通过五种长度，但 callback quiescence、持续负载和生产 SDK 语义未验证。Fast DDS 主机库准备构建已 237/237 编译并安装成功。Descriptor/Transport facade 已实现并通过限定范围验证，但 native backend/connection manager 缺失；SPDP、SEDP、用户数据、RMW 未运行。整体仍 NOT VERIFIED（完整 Transport 发布未就绪）；N1 限定执行门已 PASS，提交须单独满足范围、文档、图分析与 Git 门槛。每阶段的实际结果必须链接本轮原始执行记录。未执行用 NOT RUN / SKIPPED 表述；遇到实际阻碍后才使用 BLOCKED，记录具体原因。汇总见 [docs/STATUS.md](docs/STATUS.md)。

历史 G1/G6：当前仓库未找到原始记录，本轮相关测试尚未执行。不推断历史测试不存在，不继承历史 PASS 或 BLOCKED，不把 agent 配置中的示例当成记录。本计划采用新的测试标识。

## 2. 基线与证据边界

计划建立时的实际 Git 基线：

| 对象 | 完整提交 / 当前信息 | 证据边界 |
| --- | --- | --- |
| 顶层仓库 | `1636043cbcc4b871d8969f9ed122b11f1d007885` | 工作区存在用户原有修改，须保留 |
| Fast-DDS | `343f155c36b561db3d9f047a65d866ce0f3da300` | CMake 声明 `3.6.2.0`；不能按 v3.2.2 签名实现 |
| communication_dsoftbus | `dd6037ce496828133bb9af89b37e2d08232a10d9` | 指定源码可本地读取；尚未证明对应板端库 |
| 板 A | `3e01ff55454d202020104033bf453b00` | 前次只读查询：RK3588 EVB1、KaihongOS 6.1.0.04 / API 23；本轮重新登记 |
| 板 B | `3e01ff55454d202020104433991c3b00` | 同上；所有设备操作必须显式 `hdc -t` |
| 后续 ROS 2 / RMW | 待锁定 | 不影响独立探针；不能宣称 RMW 兼容性 |

S0 必须记录：源码 SHA 与差异；SDK 路径与版本、编译器、目标架构、工具链、实际头文件和链接库、库 SHA256、Fast DDS 构建选项；板端系统构建标识、实际运行库路径和可取得的 SHA256/Build ID；程序哈希、配置、启动命令、退出码和日志。

编译、链接、启动、Socket 通信、正常关闭分别取证。缺少源码与板端库的对应关系允许开展黑盒探针，不允许据其它版本内部机制认定板端根因。

初始用户改动包括 `.codex/agents/tester.toml`、两个子仓库的 agent/工具说明，以及顶层未跟踪的 `.claude/`、`.vscode/`、`CLAUDE.md`、`SuggestionsFromChatGPT6Pro`。不覆盖、不提交这些无关内容。

## 3. 架构与非目标

```text
DDS API / 后续 ROS 2
  -> Fast DDS DDS / RTPS
  -> TransportInterface / SenderResource / TransportReceiverInterface
  -> DSoftBusTransport
  -> DSoftBus 公共 Socket API
```

- 配置工厂：`DSoftBusTransportDescriptor`。
- 运行实例：`DSoftBusTransport`，内部包含 LocatorCodec、EndpointDirectory、ConnectionManager、CallbackDispatcher、ReceiveDispatcher。
- 发送资源：`DSoftBusSenderResource`，绑定当前版本的 `send_lambda_`。
- 共享连接由连接管理器拥有；SenderResource 和输入通道持有受控引用，明确唯一关闭责任。
- 不按 Topic 建 Socket，不在 Transport 内解释 DDS 类型/QoS/RTPS 子消息。
- 不调用旧 Session 建连接口，不取得内部 fd 绕过公共 Socket API，不以修改 RTPS 行为规避失败。
- 首轮使用一进程一 Participant、双板一对端、确定主动方；这是测试拓扑，不是产品单实例限制。
- 首轮通过 C++ 注入 Descriptor；XML/RMW 配置注入另列阶段，不默认原生 XML 能加载任意 Descriptor。
- 优先一条完整 RTPS 报文对应一条 SendBytes 记录。额外握手或封装不是当前实现步骤；若必要，先形成具体方案重新审定。

## 4. 当前 Fast DDS 契约

依据 `Fast-DDS/include/fastdds/rtps/transport/` 的当前头文件逐项实现，不能复制旧签名。

- `SenderResource::send(..., deadline, transport_priority)` 优先调用 `send_lambda_`；旧回调仅兼容回退。
- `transport_priority` 完整接收并可观测；首轮 FIFO，不宣称差异化优先级，不机械映射 DSoftBus QoS。
- 按 `total_bytes` 前缀边界聚合 buffers，检测可读总长度不足、溢出、上限；允许最后一个缓冲区只取前缀，不要求所有缓冲区长度之和恰好相等。异步任务持有自有字节及目标快照。
- `OnDataReceived()` 返回前缓冲区有效；异步接收复制数据及所需字符串，不能保留 SDK 临时指针。
- 输入通道匹配、重复打开、接收器关联、共享通道关闭必须一致。
- `is_locator_reachable()` 表示合法可解析路由，不等同于此刻 READY；实现 Locator 规范化、转换、选择及发现完整往返。
- `SenderResource` 默认析构不调用 `clean_up`。自定义资源使用明确的 RAII/幂等清理，不从多个拥有者重复关闭。
- `maxMessageSize`、`min_send_buffer_size()`、接收容量及实际记录上限一致；上限用于完整 RTPS 报文，不能当成业务净载荷。
- `sendSync()` 当前不汇总发送资源返回值，因此应用写入、Transport 接纳、SDK 结果、对端记录、DDS 样本分别取证。

### 4.1 首轮异步队列的发送返回契约

为消除实现歧义，S3 的首轮候选策略为：`true` 表示本次所有受支持且允许的目标已经完整接纳到有界队列，并接管字节与目标快照；不表示 SDK 已提交或送达。后续失败/过期/关闭作废必须独立记录。

接纳采用全有或全无；容量不足、过期、非法 Locator 或没有受支持目标返回 `false`，不悄悄部分入队。共享字节可引用计数，但全局与逐目标容量计费规则必须一致。接纳后各目标独立提交，某个目标失败不取消其它已接纳目标。单连接最多一个在途 SDK 发送。

这是本项目候选契约，不是 Fast DDS 强制要求。S1 用测试固定接口观测，S2 根据发送可终止证据确认 S3 是否具备实施条件；不得用队列成功掩盖永不退出的 SDK 调用。

## 5. 地址、路由和连接归属

逻辑 Locator：项目私有候选 kind 精确为 `0x40000000`、port=1、16 字节稳定 endpoint_id。endpoint_id 不是 DDS GUID，也不是 networkId 截断值。拒绝全零、首字节 FF 和 `00…00 01`：当前 Fast DDS 的通用 IPLocator 路径会将后两者分别误判为组播和 localhost。其它合法字节逐字节保持；不得以 kind 位与、IP helper 或隐式修复代替自定义校验。该编号尚非标准分配证明。

三张逻辑表：

| 表 | 映射 |
| --- | --- |
| 对外路由 | `(local_locator, remote_locator)` -> remote networkId、remote listen name、local initiator name、route version |
| 接入反向索引 | `(local callback namespace, remote networkId, remote initiator name, dataType)` -> 唯一 Locator 对 |
| 活跃连接 | SDK 回调可辨识范围内的 socket ID -> Locator 对、generation、route version、state |

- 显式区分 remote listen name 与 expected remote initiator name；以目标平台实际观察值核验。
- 一个观察键不能对应不同 Locator 对；初始化时拒绝歧义。合法别名可以指向同一身份。
- 未知对端、类型不符、非法字段不交付 RTPS；关闭该接入连接并记录原因。
- `PeerSocketInfo.pkgName` 在当前源码取本地注册包名，禁止单独作为远端身份认证依据。路由匹配不等同于安全认证。
- 本地回调没有通用 user_data/监听 ID。选择进程级稳定 dispatcher，或具有独立本地名称的静态入口。
- **同一本地名称、同一客户端/服务端角色共用 SDK listener 槽**；必须使用同一稳定 dispatcher，不能被另一个实例覆盖。不同命名空间使用不同本地名称。
- **Socket 成功可能返回已有 ID**。在登记 BINDING 和调用 Bind 前检查现有归属，禁止覆盖健康连接或将其作为本次新资源清理。
- networkId 失效后由明确的配置/平台集成入口更新 route version；旧路由不可作为永久身份。动态组网接入在后续范围内明确，不从 RTPS 猜测身份。

## 6. 状态机、并发和关闭

```text
DISCONNECTED -> BINDING -> READY -> CLOSING -> CLOSED
                         \-> FAILED -> CLOSING
```

上述为 native 连接状态域，监听资源独立管理。每代 application attachment 另有 Active -> Detaching -> Detached 状态，不随 Participant 删除而自动退役健康 native owner。状态迁移在受控同步下完成；SDK 调用及向 Fast DDS 交付数据时不持有全局连接锁。

- 同步 Bind：Socket 返回并核实所有权后，调用前登记 BINDING；绑定成功且同代未关闭才 READY。客户端不等待 OnBind 建映射。
- 异步 Bind：提交成功仍为 BINDING；完成回调核实身份、代数和关闭状态后才 READY。
- 被动 OnBind：复制参数、恢复本地命名空间、唯一反查、校验、重复连接裁决、登记归属，再发布 READY。
- OnBind 为 void，不能以返回 false 拒绝。拒绝后禁止交付并通过安全路径关闭。
- Bind 返回前收到的数据进入受限暂存，回调不得等待 Bind 返回；失败/关闭则作废。
- 健康连接不被重复连接覆盖；旧任务绑定旧代及路由版本，不静默转投新连接。
- generation 只解决本地旧任务误投；不能识别仅携带复用 ID 的迟到 SDK 回调。必须验证 SDK 回调收尾与 ID 再用条件。
- 禁止接收线程自 join、自析构等待、detached 卡死线程、强杀线程或遗留连接伪装正常关闭。

Participant/application 关闭：撤销本代新任务/交付准入 -> 停止本代发送队列并作废待处理工作 -> 等待 Open 发布或回滚 -> 排空本代实际 receiver 回调并解除关联 -> 释放逻辑 claim。旧复制/已取工作保留原代身份和预算，不可改投新代；每个重复关闭者都等待本代真实屏障。正常同名重建保留健康 native owner，不重新 Socket/Bind/Listen/Shutdown，也不按次数消耗永久名称/ID槽。

native service 最终关闭另行执行：停止新的 application/native 准入 -> 撤销并排空所有 receiver -> 独立请求 SDK Shutdown -> 等待可证明终止的在途调用/回调 -> 回收 native 资源及线程。SDK 尚未结束则明确 Pending/未验证，不能假装物理资源已释放；关闭控制路径不得排在可能卡死的发送任务后面。Transport 不自行杀进程/重启。零 application 不自动触发破坏热重建连续性的闲置回收。

后续 native-send 在共同门下不可逆的 ExecutionAdmitted 是在途归属边界，与 detach 互斥线性化；它不同于 SdkCallEntered/Returned。已取得许可的 P0 工作可能稍后才实际调用 SDK，仍属于 P0 并占原容量；未获许可的旧工作拒绝，不获取 P1 身份。真实调用前仍检查原绝对截止时间；准入不等于已发送。此契约还需三处并发 barrier 测试，不表示 native-send 已实现。

### 6.1 有界契约及实验参数

队列接纳、SDK 提交、关闭回收分别定义预算。一次任务沿用同一绝对截止时间；连接等待、多个目标和重试不能重置预算。在途超时不等于未发送，不得释放 SDK 仍使用的内存。

逐目标记录：未接纳、已排队、未提交即过期、SDK 提交中、SDK 成功、SDK 错误、在途超时结果未知、关闭作废。SDK 错误也不自动证明对端未收到任何数据。

初始实验候选（不是产品保证）：每对端发送/接收各 32 条且 256 KiB；全局容量另行固定；单连接 SDK 在途发送 1；接纳等待至多 min(剩余预算,20 ms)；SDK 观察阈值 1 s；关闭观察阈值 3 s；外部 watchdog 10 s。记录上限由目标 SDK 和设备实验确定，并与队列容量相容。Bind 的 SDK 支持范围单独核验，不把发送阈值当 Bind 参数。

watchdog 仅保护测试环境，触发即测试失败；不得算作正常取消机制。正式验收前固定负载和阈值，不因失败持续放宽。

## 7. 实施阶段与依赖

| 阶段 | 交付物 | 通过条件 / 依赖 |
| --- | --- | --- |
| S0 基线及计划审定 | 本 PLAN、基线记录、SDK/依赖/板端可用性 | 实际路径、版本、SHA、差异及缺口可复现；独立审定首轮范围 |
| S1 当前接口与测试框架 | 独立 CMake 工程、Fast DDS 接口/资源探针及测试、Socket 探针 CLI 骨架 | S1a 为当前真实头文件编译；S1b 为实际源文件/依赖支持的链接和生命周期执行。先行为 RED 再 GREEN，发送参数、接收寿命和释放可观测；目标交叉编译分开登记。允许构建所需的最小未修改上游源文件集合，但必须标明不是完整 Fast DDS 库集成，禁止补造上游符号 |
| S2 双板 Socket 准入 | 公共 Socket API 原生探针、双板执行记录 | 权限及组网、身份字段、重复 ID、回调时序、变长/上限/双向记录、异常退出和关闭；每项独立 PASS/FAIL/BLOCKED |
| S3 Transport 实现 | Descriptor、Transport、SenderResource、内部目录/连接/收发模块 | S2 关键记录和终止契约通过后接入真实通信；TDD 固定映射、队列、状态机及所有权 |
| S4 SPDP | 两 Participant、无用户端点 | 双向发现、通告 Locator 往返保留并选中、无旁路 |
| S5 SEDP 与样本 | 最小 Writer/Reader、小消息、大样本及双向数据 | 匹配/不匹配反例、样本内容数量序列、DATA_FRAG 和可靠性控制路径 |
| S6 异常与回归 | 断连、重连、丢失、压力、完整 Participant 销毁 | 对应 QoS 和负载包络下满足预期，资源/线程可回收，不修改协议语义 |
| S7 产品集成 | 经审定的 XML/配置注入、ROS 2/RMW 版本与构建 | 明确允许改动范围后实施；真实应用生命周期及兼容性验证 |

S1 与 S2 的独立准备可并行；实现、测试、review、复测、文档、提交阶段不得乱序。S2 失败时继续安全的独立接口/模拟测试，但不宣称真实 Transport 完成，不绕过准入推进依赖阶段。

## 8. TDD 测试清单

| 标识 | 场景 | 期望 |
| --- | --- | --- |
| IF-01 | 当前 Descriptor/Transport 实例化、工厂创建 | 所有纯虚接口精确匹配，配置/运行对象分离 |
| IF-02 | 多缓冲区、多个目标、deadline、priority | 按 total_bytes 完整聚合；参数可观测；非法长度拒绝 |
| IF-03 | SenderResource/输入通道/Transport 销毁 | 清理恰好一次，引用寿命符合契约 |
| IF-04 | 输入数据回调、并发关闭 | 自有缓冲有效；关闭后不再交付；无死锁 |
| MAP-01 | 反向键冲突、未知对端、类型不符 | 初始化/接入明确失败，不伪造 Locator |
| MAP-02 | 同名同角色多个主动 Socket | dispatcher 不被覆盖，既有连接归属稳定 |
| MAP-03 | Socket 再次返回已有 ID | 不覆盖旧代、不关闭旧连接 |
| LIFE-01 | Bind 期间收到数据/失败/关闭 | 有界暂存，绑定返回后不复活关闭对象 |
| LIFE-02 | 重连后旧任务/迟到回调 | 本地旧任务隔离；SDK ID 收尾条件实证 |
| SEND-01 | 多目标部分失败、容量不足、过期 | 接纳原子性及逐目标结果符合 §4.1 |
| SOCK-01 | 变长连续记录、双向并发、边界大小 | 每次 SendBytes 与 OnBytes 一一对应且字节相同 |
| SOCK-02 | 本端 Shutdown/对端退出/故障期间发送 | 实际退出及回收在记录的测试要求内 |
| DDS-01 | 两 Participant | SPDP 成立，不据此宣称用户端点匹配 |
| DDS-02 | Writer/Reader；不同 Topic/不兼容 QoS | 正例匹配、反例不匹配 |
| DDS-03 | 小样本/大样本/双向/丢失恢复 | 数据及可靠性/分片行为符合配置 |
| DDS-04 | 压力下删除整个 Participant | 测试完整 Fast DDS 停用顺序 |

所有测试保留失败命令、exit code、日志和测试对象哈希。假 SDK 不能替代 SOCK / DDS 实机测试。双板通信禁用 builtin transport，并检查 Data-sharing/Intra-process 旁路及收发计数。

## 9. 目录和实现边界

首轮新增顶层独立目录，不修改上游子仓库：

- `probes/`：当前接口探针、原生 Socket 能力探针。
- `tests/`：先编写的行为/契约测试。
- `cmake/`、顶层 `CMakeLists.txt`：明确选择 SDK 和依赖的独立构建。
- `scripts/`：可复现的构建/执行入口；设备命令强制 target，watchdog 失败显式记录。
- `.artifacts/`：忽略的构建产物、原始日志、运行库快照；每轮独立子目录，禁止覆盖外部已有数据。
- `docs/`：实际基线及 tester/review/gate 摘要，引用原始记录与哈希。
- 后续 `include/`、`src/`：真实 Transport；在 S2 准入后按需创建。

设备程序部署只限显式目标上的新建 `/data/local/tmp/fastdds_dsoftbus_probe_<run>/` 测试目录。不得修改全局权限策略、固件、系统库或网络信任关系以取得通过；若确需这些操作，先提出具体需求与影响。普通目标构建/测试部署属于本次授权工作。用户后续已授权学习并配置项目 native 权限身份；此项有状态例外严格限于 §14，不要求复用已有身份。

## 10. 每个实现增量的强制工作流

1. 计划/场景审定；读取适用 AGENTS、官方 Socket 示例/测试/权限要求。
2. GitNexus impact 后编辑。UNKNOWN 必须通过源码搜索澄清；新符号记录为未索引，不能声称零影响。HIGH/CRITICAL 先报告。
3. 先写有意义的失败测试并保留 RED，再实施最小代码使其 GREEN。编译器权限拒绝、缺依赖等环境失败不计为 TDD RED；RED 必须来自待验证的接口或行为尚未满足。
4. 独立 `tester` 编译当前工作区并执行适用的 host/SDK/device 测试。
5. 初测通过后独立 `agentic-review`，修复 correctness/architecture/safety/regression 阻塞项。
6. 再由 `tester` 复测，结果作为最终执行证据。
7. `docs_updater` 根据实际 diff、测试、设备和 review 记录同步文档。
8. Main 执行 PRE_COMMIT：上述齐全、tester `Commit gate: PASS`、docs `Ready for commit: YES`、适用设备验证满足、无意外改动、GitNexus detect_changes 完整。
9. 仅 Main 可 stage/commit；未达 gate 不创建正常实现提交，不推送。保留用户原有改动。

不适用项目写 N/A 并给出原因。缺环境/权限/身份信息时登记具体 BLOCKED 项和已穷尽的安全检查，不把未执行项改为 PASS。

## 11. 图证据及限制

已用 GitNexus 分别定位 Fast-DDS 与 communication_dsoftbus 的实际接口和调用路径；子仓库索引提交与上述 SHA 相符。概念搜索曾因 FTS 运行依赖缺失降级，具名 context 可用，所有关键结论已定向核对源码。

新 Transport 类 impact 返回 UNKNOWN/未找到，当前源码搜索未发现实现；这是新增代码，不是“证明没有调用方”。首轮只新增独立探针，不修改既有符号。后续编辑及提交仍须重新执行适用图检查；partial/truncated 结果不能作为干净 gate。

## 12. 完成定义与当前待决项

整体完成要求：实现、目标构建、host 契约测试、Fast DDS 集成、DSoftBus 集成、双板及回归、独立 review、review 后复测、文档同步、PRE_COMMIT 全部满足适用项。

尚需实际核实：SDK/库 ABI 对应关系、合法 native 部署身份与权限、已组网对端获取方式、记录最大容量、SDK 调用终止与回调收尾、多实例命名空间容量、最终性能目标、RMW/配置注入版本。任何未决项只阻止其依赖步骤，不扩大为无关工作的阻碍。

状态更新由最终执行证据驱动；计划条目不自动变成 VERIFIED。

## 13. 上轮执行状态（2026-09-13 UTC run）

最终 tester 证据位于 `.artifacts/verification/20260913T155515Z-final/`：Host Debug CTest 整体 7/7 PASS，OpenHarmony SDK 23 arm64 构建 PASS；两块 RK3588 EVB1 上 IF-01..04 与符号探针 PASS。

S2 Socket 检查在两板均返回 `-426115032`，耗时约 5015–5017 ms；根因尚未确认，S2 gate 为 `BLOCKED`。Bind、Listen、SendBytes、真实 Shutdown/回调排空、双板通信、SPDP、SEDP、用户数据和 RMW 均未执行。总体为 `NOT VERIFIED`，PRE_COMMIT 未通过。G1/G6 没有当前原始运行记录，不与本轮测试自动对应。

## 14. S2-P：独立 native 身份与最小权限探针（2026-09-14 CST）

用户授权：可依据 DSoftBus 资料为上述两板配置新的项目 native 测试身份，不以复用现有身份为前提。当前仅验证本机 Socket 创建/关闭所需权限，不扩大为配对、系统策略修改或产品部署方案。

依据：本地 DSoftBus 官方 `tests/sdk/transmission/benchmarktest/trans_test.cpp` 的权限初始化示例、`tests/sdk/common/src/softbus_access_token_test.cpp`、`core/frame/standard/init/src/softbus_server_stub.cpp` 的 REGISTER_SERVICE 权限检查。只采用其 native 权限模式，绝不采用示例的 Session 通信代码或已有系统服务名。AccessToken 官方对照源码另锁定 `53287abcc13b418c091154fdb629c37dcfa34ce6`（OpenHarmony-6.0-Release，`.artifacts/reference/security_access_token`）；这是补充参考，不是板端源码对应证明。

实施契约：

1. 新增独立、默认不构建的板端权限探针；原无权限修改能力探针保持不变。使用真实官方头文件和两板已核验的库，不补造上游声明；公共 Socket API 是唯一通信入口。
2. 固定独立 `processName=fastdds_dsoftbus_probe`，APL=`normal`，仅 `ohos.permission.DISTRIBUTED_DATASYNC`，dcaps/acls 均为空。不申请 CENTER、UDID，不冒用 `dsoftbus_service` 或其它已有身份。DSoftBus 的 `pkgName=com.kaihong.mdds` 与 Socket 命名白名单独立核验；不把名称白名单当作 AccessToken 授权。
3. 创建前查询身份并检查持久化文件；若同名记录不属于本次探针，停止而不覆盖。保存 `/data/service/el0/access_token/nativetoken.json` 的原文、哈希、元数据至本轮本地和板端备份；配置后比对非项目记录不变，不恢复整份旧文件覆盖并发更新。
4. 授权模式保存原 token；调用 `GetAccessTokenId`、`SetSelfTokenID` 并读回，再 `ReloadNativeTokenInfo`。逐项检查返回值。验证服务中同名 ID、native 类型和 DATASYNC 实际权限，最后恢复原进程 token 并退出。Reload 的 0 返回值可能是 user 构建空操作，不能单独视作授权成功。
5. 三个模式使用独立进程：`--grant`、`--socket-check --expected-token N`、`--revoke --expected-token N`。Socket 模式仅切换至已验证的项目 token、复核实际权限、执行 Socket/Shutdown 并恢复原 token，不调用 GetAccessTokenId 或写 token 文件。撤权模式只更新已核对的同名项目 token 为空权限，Reload 后验证 DATASYNC=DENIED、恢复原 token；保留空权限身份，不声称删除 token。已有项目记录必须显式提供与其一致的 expected-token；不接受任意 processName 或权限参数。
6. 官方对照实现的 GetAccessTokenId 会 O_TRUNC 原地重写完整 JSON，解析失败还可能清空文件。因此**不以超时强杀授权/撤权进程**；10 s 仅作超时 FAIL 观察，超时后不并发启动另一个写入者，等待其结束或报告状态未知。仅不写权限文件的 Socket 模式适用 10 s 强制 watchdog。每次授权/撤权前由执行者完整解析持久化 JSON、检查项目 name/token 归属及非项目记录与备份一致；文件损坏、并发变化或归属不明则停止，不再调用 GetAccessTokenId，不盲目覆盖旧备份。
7. 授权、Socket 失败或 watchdog 终止后均须先重新检查文件完整性/归属，再执行独立撤权模式并取得 DENIED 证据；失败时报告“撤权未证实”，不将进程退出解释为持久授权已消失。成功 Socket ID 只 Shutdown 一次，报告返回时间，仍不宣称回调排空或双板通信。完整闭环完成后保留空权限记录供后续受控实验重新授权。长期 native 服务/init 集成不在本增量。
8. 首次写前持久记录 run、board、固定 name、原 JSON 哈希、已有项目 token（若有），grant 返回后补实际 token。仅匹配该归属记录的身份可独立撤权；同名不代表所有权。整轮按板串行，原 token 恢复失败也不能跳过外部持久撤权。撤权成功同时要求 JSON 项目 token ID 不变、APL normal、perms/dcaps/acls 为空，且服务识别同一 native token 并明确返回 DENIED；未知 token/查询错误不能当撤权成功。
9. 若 grant 已落盘但服务尚未识别身份，独立 `--revoke --recover-unregistered --expected-token N` 仅在完整 JSON 与本轮归属记录确认后允许清空该条目；只放宽写前服务缺失条件，写后仍必须取得同名/native/DENIED 的正常证据。无法取得则继续标为撤权验证未通过。纯快照守卫 `scripts/NativePermissionGuard.ps1` 不操作设备、不持有系统文件锁；其通过不能替代执行者串行控制或服务查询。
10. 文件预检必须保留原始字节，以严格 UTF-8 解码（`UTF8Encoding(false, true).GetString(ReadAllBytes(path))`），不得先替换非法字节、去除 BOM 或 Trim 后再校验。守卫按照已核对参考解析器拒绝非法 token 类型位、超长 UTF-8 字段、嵌入 NUL、非法代理项、空记录数组及超限文件/数组，避免把会导致解析失败或 C 字符串身份混淆的快照放行；仍不能据此声称板端解析器版本已匹配。

TDD 与验收：先写可注入假接口的权限流程测试，记录真实行为 RED，再实现。至少覆盖创建 token 为 0、设置失败、身份读回不符、Reload 失败/成功但未授权、未知/不符 token、Socket 模式绝不写权限、撤权未生效、恢复失败、成功 Socket 只关闭一次；授权验证失败不得进入 Socket。只读/无参数和非法参数不得写权限。独立 tester 执行 host 回归、目标编译、两板原始无配置对照与三进程闭环验证（Socket 失败仍需撤权），保留 SHA/命令/远端退出码及项目记录差异。首次仅在板 A 运行，确认撤权和非项目记录保持后才扩展到板 B。

权限修改属于高风险边界（agentic-review Tier 3）；实现前独立审定本节，设备写入前核对备份与撤权入口，初测后独立 review、复测、文档同步。所有未验证条件均保留为未验证，不据源码推断此次 `-426115032` 必定由权限导致。

### 14.1 SDK ABI 实证后的历史阻碍与替代路径审核

首轮独立 tester 的应用 SDK 路径曾证实 `native_permission_probe` 目标链接 FAIL：应用 SDK 的 STL 为 `std::__n1` / `libc++_shared.so`，系统 AccessToken 库使用 `std::__h` / `libc++.so`。该失败是历史候选构建路径；不修改 ABI 宏或补造声明强行链接。随后已找到并验证官方平台 clang/原始 `__h` libc++ 头文件路径，完整探针可构建并完成受控 A/B 生命周期闭环；旧失败证据保留用于基线，不再作为当前 ABI 阻塞。

两个实际板端都可用 `/bin/atm dump -t -n <processName>` 返回 native JSON，曾据此提出独立 C-ABI staging helper 候选。但进一步核对 `accesstoken_info_manager.cpp:1822`：`NativeTokenToString()` 通过 ConfigPolicyLoader 重新加载 native 文件，再生成权限明细；它不是直接导出当前内存权限状态。指定名称只在此前经过服务的 name→ID 查询。该源码仍是对照版本，不断言板端实现相同，但足以阻止把 CLI 输出直接当作服务已加载撤权的证明。

因此此拆分方案尚未获准执行，也没有实现或部署。`GetAccessTokenId`/`GetSelfTokenID`/`SetSelfTokenID` 及无 STL 参数的 Reload 可以解决部分调用边界，但 `OH_AT_CheckSelfPermission` 的 false 仍折叠未授权与查询错误；再加一份可能重读磁盘的 `atm` 输出，不会自动成为完整的服务撤权证据。不得用 ABI 宏、别名、降低判据或修改系统全局策略绕过。

历史继续路径现已完成到受控闭环：平台 ABI 构建、只读检查、独立审定及 A/B regrant→Socket/Shutdown→revoke 均有独立证据。早期无授权 Socket 失败保留为历史对照；A 本轮服务日志明确显示 DATASYNC 权限拒绝，不能将旧失败自动追认为当前根因。后续仍不得把有限本机闭环扩展解释为跨设备通信或完整 Transport 验证。

### 14.2 官方平台工具链及只读 ABI 准入（2026-09-14 UTC 续行）

本轮找到官方 OpenHarmony-6.0-Release 构建清单中的 Windows 平台 clang 包 `clang_windows-x86_64-bb5cdf-20250726.tar.gz`，SHA256 与官方一致：`716de5a203ae5cb0fc31e8e12a09c49cbb884c7a0fc0854b950f79a8dc84c157`。原始 libc++ 头文件自带 `__h`；没有修改 ABI 宏、SDK 或系统库。采用平台 compiler/原始 C++ 头文件、SDK 23 C sysroot 和板端库快照，未改旧权限探针已编译链接成功；记录位于 `.artifacts/permissions/20260914T000800Z-platform-abi-diagnostic/`。这是新候选构建路径，不证明参考源码与厂商系统完全对应，也不抹除旧应用 SDK 的真实链接失败。

授权前先实现并验证唯一参数 `--inspect`：读取当前 self token，检查 native/shell 类型，以 `GetVersion`、`GetNativeTokenInfo`、名称→ID 往返和强制 manager 的 vector `VerifyAccessToken` 检查真实字符串/结构体/向量调用路径。每级返回码独立记录；已知 DENIED 可以是只读检查成功，查询错误、未知状态或身份不一致则失败。退出前确认 self 未变。进程名只记录长度，不原样输出控制字符。

该模式不进入授权流程，不调用持久化写入、SetSelfTokenID、Reload、Socket/Shutdown。参数混用、重复或未知参数必须在 SDK 调用前 exit 64；合法查询可以使用 10 s watchdog。shell 若被平台策略拒绝，保持精确错误与失败状态，不借用其它身份求通过。旧 CLI 在板 A 的真实 `--inspect → 64` 为 RED；相对 HDC 传输路径曾产生嵌套目录、不可执行的部署错误不计入 RED，之后改用绝对本地路径并核对远端 SHA。

独立 tester 须验证 A/B 的只读调用、负例、产物及系统库哈希、token JSON 前后不变；再独立 review、修复、复测。只读通过仅标记 `READONLY_ABI_QUERY`，不是项目授权、撤权或 Socket 通信通过；§14 的备份、归属、三进程闭环及先 A 后 B 准入条件保持不变。

本轮 A 无授权 Socket 复现还取得同时间段服务日志：`DISTRIBUTED_DATASYNC` 权限拒绝与客户端 `SoftbusRegisterService` 重试失败，原始日志位于 `.artifacts/diagnostics/20260914T000333Z-socket-recheck/`。这是本轮设备证据；不推断配置权限后所有后续 Socket/组网问题都会消失。

### 14.3 首板受控写入准入

只读增量已完成独立初测、review 和最终复测：`.artifacts/verification/20260914T002100Z-inspect-final/`，两板查询及负例通过、Host 9/9、系统 JSON 和提供库不变。独立写入准备审核见 `.artifacts/review/20260914T002922Z-permission-write-readiness.md`。对实际厂商 native-token 库 `b539ec42801e036db4b66f42af7a3e2e97610a13942d5e5b67c2c4572b9d7e81` 的反汇编核验证明：正常解析后的追加/更新路径保留外部 JSON 树，包括未知 uid 字段和重复数组项；这不是字节不变、原子写入或并发创建保证。

沿用用户已授予的新项目最小身份配置权限，由 main 协调、独立 tester 作为唯一执行者，首次只操作 A。实际写入前必须重新记录无并行项目配置进程、原始字节的本地及板端双备份、文件元数据、严格 Absent 守卫、owner(expectedToken=0)、探针与提供库哈希；紧邻 API 调用再次检查 JSON 哈希及不存在同名身份。串行控制只能降低 TOCTOU 风险，不能把 GetAccessTokenId 变成 create-only/CAS。

grant 的已返回实际 token 必须立即保存归属，再取得 Granted 守卫及服务 native/name/GRANTED 证据，才允许独立 Socket 进程。成功或失败均须在重新确认完整性/归属后独立 revoke，取得同 ID、服务 DENIED、空项目权限及外部记录结构不变的闭环，才能扩展到 B。grant/revoke 不得由 watchdog 强杀；超过 10 s 只记录观察失败并等待真实结束，禁止并发新写者。未知归属、返回 ID 丢失/为零但记录新增、竞争或 JSON 损坏/外部变化立即 STOP，不盲目整文件恢复。仅 Socket 测试可使用 10 s 外部 watchdog。

本节是准入决定，不是首次授权或撤权已经通过的证据。后续须以实际独立测试结果更新状态。

## 15. S2 记录与路由探针实施切片

独立记录 CLI 不创建/重载/撤销 token，不修改权限文件；实验前后由 §14 的独立受控流程管理项目身份。每次运行验证同一已归属 normal/DATASYNC native token，设置后再次强制 manager 查询，结束恢复原 self。平台 `__h` 权限查询探针与应用 SDK `__n1` Fast DDS 库分开构建；真实 Transport 不引入 AccessToken C++ ABI 依赖。

首轮 server/client 仅一进程、一监听命名空间、一对端，无重连/Socket ID 再用。同步客户端在 Bind 前登记归属，成功返回后才 READY；被动回调严格匹配 networkId、主动名和 BYTES 类型，pkgName 仅诊断。回调只复制有界事件/字节，不在回调线程调用 SendBytes/Shutdown；未知新接入的正 ID 也必须有唯一清理责任，既有 ID 不得覆盖或误关。32 条/256 KiB 队列、15 个拥有句柄是实验限制，溢出保留失败。

发送 32、255、4096、16384、60000 字节五条确定性记录，server 原记录回送，逐字节校验每条边界、顺序、数量及观察窗口内额外回调。完整记录直接交给 SendBytes，不增加 Transport 私有包头。7 s 原始绝对运行预算、1 s 单次发送和 3 s 关闭观察阈值、10 s 外部 watchdog；阈值不是可取消保证。200 ms 静默观察及进程生命周期的回调存储只保证探针不访问失效栈，不能证明生产 Transport 回调已排空。首轮成功不自动覆盖异步 Bind、重连、压力或所有记录上限。

辅助 `--list-nodes` 只读模式使用公开 GetLocalNodeDeviceInfo/GetAllNodeDeviceInfo/FreeNodeInfo 取得当前可路由 networkId；不是 Transport 数据通道，不调用 Join/Leave/Publish/Refresh 或 Session API，不申请 CENTER。指定源码的这两个查询映射到 DATASYNC，但仍需板端验证。与 Socket 模式互斥，非法参数在 SDK 调用前拒绝。先验证全部返回内容再输出路由：数量 0..32、指针组合、64 位十六进制 networkId 及终止字符，拒绝重复/本地别名，SDK 数组所有非空返回路径恰好释放一次。只记录 networkId，不采集 UDID/MAC 等额外标识；零远端节点是查询成功但没有可用路由。

验证按实际工作树执行：pure contract 行为 RED/GREEN → 独立 host/平台编译与 CLI 负例 → 受控双板只读路由和记录测试 → agentic-review → 受影响复测 → 文档同步。主机模型通过不使 S2 通信 gate 自动通过。

首轮独立 review R1 要求修复：accepted 正 ID 必须在可能抛异常的对端字符串复制前，以预留 storage 登记；关闭目标采用固定容量、无分配列表，使复制失败仍能由控制线程尝试关闭。有限 owner 容量耗尽必须进入不可清除的 fatal 状态，记录未跟踪 ID/计数及 `cleanup=UNCONFIRMED`，尽力关闭已知资源、恢复 self、非零退出，外部仍独立撤权；不能仅把漏记 ID 混入普通失败后据 owned 等式声称回收完整。这是短时探针异常终止，不是无限来流下每个连接均正常回收的保证，也不得作为生产 Transport 的正常关闭机制。成功门槛和原有预算均不放宽。

### 15.1 Bind 失败后的诊断契约修正

本轮实际发现 Error/Shutdown 事件未填写 `before_bind_return`，默认 false 不能作为回调时序证据。新增 TDD 场景要求客户端在 BindReturned 前/后记录的两类事件分别保存 true/false；服务端无客户端 Bind 返回，始终 false。标记在事件入队时固定，不随稍后出队时状态改变。该修正不改变连接成功、队列、截止时间或关闭判据。CLI 合法启动日志补充实际 `getpid()`，便于关联服务日志。

下次设备尝试须先完成独立测试、审阅及必要复测，并使用新鲜的完整权限双备份/归属/transition guard，不以旧 hash 对照替代。测试脚本必须保存逐板原始 stdout/stderr、进程 PID、真实远端退出码与 HDC 自身退出码，并在 A Listen 成功后才启动 B；同时连续采集相关 hilog，避免短缓冲区覆盖。不得清日志、降低隐私保护、增加超时阈值或借用其它身份。首次尝试中的缺证保留，不补造历史执行结果。

### 15.2 线程身份诊断（独立 review、两板测量已完成；限定范围）

实际 `20260914T024355674Z-execute` 记录显示 lane/TCP 已建立、A SDK 已处理 channel-open，但服务端完成反馈仍等待超时。独立诊断随后在两板短命进程中实测：主线程设置目标 token 后，预先存在的旧线程保持原 token，新建线程观察到目标 token，主线程最终恢复。公开内核的 `current->token` 和 Binder sender-token 路径支持该候选机制；但实际 DSoftBus callback 线程身份及本次 IPC 拒绝原因仍未直接测量。空 OnNegotiate 默认成功、invalid userId 跳过协同检查，不能作为已有根因。

先在忽略的诊断目录建立独立短命线程身份测量程序，不改现有 record probe 的初始化顺序。程序只调用公共 GetSelfTokenID/SetSelfTokenID 和线程 API，不加载 AccessToken manager 或 DSoftBus、不授予/撤销权限、不写 JSON、不选择其他应用身份。外部执行者必须绑定板 A/B 与各自已归属、normal-native、REVOKED 的固定项目 token，实际运行 origin、fresh-transition 与 ownership 守卫，并在独立进程中复核服务已识别该身份且 DENIED。CLI 严格拒绝任意身份值和未确认预检。

正常测量顺序：旧线程先读自身 baseline，在同一 mutex 的 ready 谓词下通知主线程并等待；主线程确认 ready 后 Set 目标并读回；新线程读取并 join；释放旧线程读取并 join；主线程恢复原 token 并读回。记录完整 uint64_t 值与实际 PID/TID；GetSelfTokenID 没有单独 RC，返回 0 为失败/未知，不能伪造 get_rc=0。original 为 0 则停止，original 已等于目标则 INCONCLUSIVE/拒跑，不能据此判断继承语义。

任何 Set 调用过，即使返回失败，也必须尝试主线程恢复；异常路径先尝试恢复，再处理可能无法 join 的线程，不能把唯一恢复机会放在无界 join 后。等待使用谓词循环，不靠 sleep；未确认 join 时共享 args/mutex/cond 必须继续存活，不允许 detach 或返回成功。诊断可在明确失败后终止整个短命进程，标明 cleanup 未确认，而非正常线程回收。仅该无持久写入的测量程序适用 10 s 外部 watchdog；触发为失败，随后核对准确远端 PID 已消失及持久 JSON hash/stat 未变，不把 HDC 退出当作进程终止证明。

TDD 固定控制顺序及失败分支：旧/新线程创建失败、等待/通知失败、Set 返回失败但可能已改变、读回 0/不符、恢复失败、join 未确认。模型与平台构建及两板结果已由独立 tester/review 记录；设备证据为 `.artifacts/diagnostics/thread-token/20260914T035011891Z-tester-device/`，review 为 `.artifacts/review/20260914T035632Z-thread-token-device-results/REVIEW.md`。该结果只证明这些被测线程的 token API 观察，尚不能直接认定旧 channel3174 的故障根因。新的 completion IPC 记录已执行但仍 FAIL（0 bytes、remote exit 6），最终 manager 只读预检为 DENIED/before-select；准备与 final 证据分别见 `.artifacts/verification/20260914T035259996Z-record-completion/PREPARATION.md` 和 `.artifacts/verification/20260914T035259996Z-record-completion/final-readonly-20260914T041700Z/`。随后若需调整 Socket 探针启动顺序，另行审定，仍保留完整身份核验与恢复义务。

执行脚本审核补充：每次读取持久身份文件须确认普通文件且非符号链接、mode=640、uid=0、gid=3020；SELinux label 从已保留的实际板端证据核对，并在前后比较，不只比较原始字节。独立 manager-D013 预检也是不写持久状态的短命进程，另行明确允许 10 s 外部终止监督：启动 shell 必须 exec 并记录准确 PID；超时或未知退出只表示预检失败，继续只读核查准确 PID 已消失、JSON hash/stat/label 与双快照守卫，不能继续测量。此监督不包围任何授予/撤权调用，也不证明 SDK 正常关闭。HDC/主机收集未确认时保留 UNCONFIRMED，停止后续动作，不用 HDC 退出替代远端终止证据。

### 15.3 补充：Socket 探针 token-first 初始化修正（设计审定，仅准入 TDD）

依据 `.artifacts/review/20260914T042201Z-token-first-design/DESIGN.md` 的 D1–D7 与 TI-01..13，实施最小诊断顺序修正，不改生产 Transport 身份策略。新记录探针仅接受项目已有 A=671793191 / B=671320465；冻结 D013 保留为明确路径/哈希的外部 manager，原 list-nodes DENIED-before-Set 行为不得静默改变。外部本轮预检必须绑定设备、项目身份、manager 识别结果/退出/PID 消失、产物路径/哈希、相邻快照及 ownership/origin/fresh guards、元数据/SELinux 和精确命令；CLI 确认只是操作者声明，不是探针内部验证证据。

首次显式 AccessTokenKit/SoftBus 调用前，先以本地 `/proc/self/task` 检查恰好只有 main TID；读取失败、枚举不完整、零/多线程或非 main 均拒绝。随后 Get 原 token（非零）→在 Set 前登记恢复责任→Set 目标并核对完整 readback→完整验证项目 token lookup/type/native info/process/APL/单条 DATASYNC GRANTED→才允许 provider/Runtime/Socket。此检查不证明装载期从未 IPC；已查到依赖库 pre-main 注册入口，但没有据此认定它曾创建 Binder 线程。

每次 Set 尝试包括失败/错误 readback 都必须恢复原 main token 并检查，恢复失败保持失败，重复清理不能把失败改成成功。异常或身份验证失败不得访问 SoftBus、借原身份重试或放宽权限。恢复不代表 SDK 全线程身份恢复，短命进程最终退出并由外部核对准确 PID。保留原 7s 记录预算、既有发送/关闭阈值、200ms 观察和 `quiescence=NOT_VERIFIED`；初始化时间与记录预算分别如实记录。

TDD 使用实际探针复用的控制流，固定成功顺序、CLI/allowlist、单线程准入各反例、Get=0、Set 失败但已改变、readback 不符、每项身份/权限失败、真实异常、sticky restore failure、记录失败不能被恢复成功掩盖、原 token 已是目标、旧 manager 分支不变，以及外部预检错误不能启动记录。先真实 RED/GREEN，再独立 initial tester、agentic-review、修复/final tester，才允许以全新 guards 执行两板记录实验。记录成功也仅证明该实验，不代表 Transport/DDS 完成。生产 Transport 不切换/授予 token，运行身份由部署端提供。

## 16. S3-A 地址与目录模型（独立于通信准入）

先实现真实 Transport 将复用的 LocatorCodec / EndpointDirectory，不改名复用 S1 探针。依赖仅标准 C++ 与真实 Fast DDS Locator 头；不调用 SDK、AccessToken 或上层 DDS/RTPS。编解码失败不改变输出，合法端点保持全部 16 字节。kind/port/保留地址规则见 §5；真实 Transport 的 localhost 策略关闭，逻辑本地性由配置目录判定。

Build 一次完整验证并返回不可变快照：LocalBinding(local Locator、callback namespace、监听名)，Route(Locator 对、networkId、远端监听名、本地主动名、显式远端主动名别名、Bytes 类型、version)，反向键(namespace、networkId、实际主动名、类型)。输出返回自有值，不借用输入字符串/容器元素。拒绝未知 local、重复 route、冲突反向键、未配置 namespace、非法 Locator/类型/字符串和回调槽冲突。同一 `(local socket name, Listener/Initiator role)` 只能属于一个 namespace；不同角色是不同槽。目录的一一性不证明真实 callback 能恢复 namespace。

字符串精确按字节比较，不 trim/折叠大小写；名字与 namespace 非空且至多 255 字节、networkId 非空且至多 64 字节，均拒绝嵌入 NUL。这是目录的 SDK 长度边界，首轮设备探针更窄的 64hex/命名限制不自动成为全部产品平台的格式保证。pkgName 不进入认证或路由键。

WithUpdatedRoute 产生新快照：同一 Locator 对、namespace 不变，expected_version 必须匹配，version 严格加一；溢出/反向键或槽冲突失败且原快照不变。更新不隐式保留旧 networkId/名字，不改投旧队列。RoutesTo 返回所有合法候选，发送资源后续明确绑定本地端点，目录不取第一个掩盖歧义。发布锁、Socket 表、generation 和 SDK 回调排空属于后续模块。

TDD 覆盖逐字节往返、保留地址/非法 kind/port、失败输出保持、正反向/监听名不可冒充主动名、冲突及输入顺序、别名、字段长度/NUL、输入所有权、更新/stale version/溢出、旧快照有效、多本地候选和并发只读。真实 CDR 双字节序、参数 Locator 保留、selector 和 SPDP/SEDP 集成另列执行证据，不计入纯目录测试通过数。

### 16.2 S3-B 单目标发送所有权核心（设计已独立审核）

先实现生产 `SendQueue` / move-only `OwnedSendRecord`，与探针模型分离；不创建 SDK worker、不默认 fake backend，不称为完整 Transport。任务保存自有完整 Route 快照、连接 generation、priority、原始 steady-clock 绝对 deadline 和指定 `total_bytes` 的 buffers 前缀。成功返回仅表示单任务接纳；多目标 SenderResource 后续必须另做 batch 接纳事务，不能由单任务循环暗中规定 partial false 行为。

容量同时约束全局及稳定 remote Locator（不按 networkId/version/generation）；reservation、queued、inflight 均计费。先预留再分配/复制，发布前取得完整所有权；异常、非法前缀、关闭或截止均退费并唤醒。record 禁止复制 payload 逃逸，计费随最后一个字节所有者存活，lease 保留 SharedState 可晚于 queue 析构。bytes 是有效 payload 字节上限，不是 RSS；无占用 peer 条目必须回收，不允许失败请求累积表项。配置上限非零且计数/加法防溢出；实验默认每 peer 32 条/256 KiB、全局 128 条/1 MiB，单条上限由调用者提供并核验，不冒充产品保证。

接纳在入口仅计算一次 `min(original_deadline, entry + 20ms)`；等待和复制后发布均核对此截止点，不重新计时。该策略不是内存分配或系统调度的硬实时保证。Take 淘汰已过原始 deadline 的任务；FIFO 指成功发布顺序，不承诺预约/调用到达顺序或等待公平性。priority 仅保存。Close、发布和取出共享明确线性化点：Close 先发生则不发布；Take 先发生则保留仍计费 inflight、不假装取消。清队列锁内移出、锁外析构，避免释放 lease 自锁死锁。调用者必须保证 queue 对象在成员函数执行期间存活；独立 state 不许可并发析构正在调用的对象。

TDD 至少覆盖：buffers 前缀与源销毁、容量/溢出/退费、同 peer 更换版本与 generation 不绕过限额、失败新 peer 不积累、复制/分配异常、复制暂停→Close→恢复不发布、发布序 FIFO、队列过期、等待共用截止点、Take 后 Close/析构 queue 而 record 继续持有自有数据直到释放。故障和同步测试应使用明确的测试注入，不实际耗尽主机内存。独立 tester、agentic-review、修复后复测及文档同步仍按 AGENTS 顺序；该切片最多证明生产核心的构建/主机契约，不证明 SDK 发送、排空或通信成功。

### 16.1 实际 Fast DDS 运行入口补充约束

当前 `NetworkFactory::BuildReceiverResources` 在相同 local 已打开时直接成功，不再次创建 ReceiverResource；meta/user 可复用同一接收资源。因此输入通道不能按端点数量或配置重复次数累加 open 引用计数。每个规范化 local 至多一个 receiver，重复同 receiver 可幂等、不同 receiver 必须明确冲突，所有匹配/查询/关闭使用同一逻辑键；重复 shutdown 不得重复释放。

接收经 `ReceiverResource::OnDataReceived` 同步进入 RTPS/用户 listener，期间持有非递归锁；Participant disable 的 UnregisterReceiver 也获取该锁。当前 UDP self-thread detach 不能证明回调内同步删除自身 Participant 安全。本实现不改上层锁，也不用 detach 掩盖问题；SDK 回调只投递自有数据，独立交付线程不持连接全局锁。完整 Participant 销毁测试由外部控制线程执行，必须验证另一线程正在接收时关闭安全，不能宣称支持 listener 内同步删除自身 Participant。

`RemoteToMainLocal` 没有被当前 NetworkFactory 用来选择发送源，SenderResource::send 也没有 local Locator/Writer GUID。首版实际 Transport 必须满足“实例 + remote Locator → 唯一选定 local/route”；目录可表示多个候选，但 Transport 对没有显式选择依据的多候选在初始化拒绝。资源只处理其负责的目标，避免 sendSync 将完整目标列表交给每个 sender 而重复投递；不解析 RTPS 报文猜测发送身份。

当前远端 Participant 发送资源清理仅特化 TCP，不能期待通用 CloseOutputChannels 自动调用。DSoftBus ConnectionManager 必须自有有界的 native cache、断连处理及明确 service 最终关闭；SenderResource/application 释放与健康共享双向连接最终关闭分开。为支持同名热重建，不能因零 application 自动按闲置超时关闭其稳定 native owner；也不得用等待一个不会调用的上游入口代替生命周期设计。

### 16.3 S3-C 接收关联及交付寿命核心（设计已独立审定）

独立生产 `InputChannels` 持有不可变目录及 1..UINT32_MAX 记录上限，使用当前真实 `TransportReceiverInterface`，不调用 SDK。每个已配置 local 对应至多一个非空 receiver；同指针重复 Open 幂等，不同指针拒绝，不按端点数量累计引用。同一 raw receiver 在本实例的不同 local 上禁止重复注册，直至旧注册排空；跨实例共享 receiver 的整体存活仍由外部拥有者负责。Capture 返回带本实例归属的不可伪造 entry ticket；身份由 ticket 持有的独立稳定控制块保证，不能只比较可复用的 raw this 地址。每次重新 Open 创建不同 entry，旧 ticket 和跨实例 ticket 不可交付给新 receiver。

Deliver 校验 ticket、完整 local/remote 路由、字节指针和记录长度；在同步边界内核对仍开放并取得 in-flight 交付租约，然后释放本模块全部锁，同步调用 OnDataReceived。字节由调用者持有直至返回；该核心不接管 SDK 临时指针或证明接收队列有界，未来 ReceiveDispatcher 必须另行复制和计费。异常必须释放交付租约并返回明确失败，不能以成功吞掉异常。

Close 先停止该 entry 新交付，等待实际 in-flight 清零，再解除 raw receiver；关闭进行中不允许重新 Open。关闭完成的线性化点是锁内排空并解除旧 receiver，不承诺线程被系统调度后实际返回的瞬间；之后允许重新 Open，但旧任务不迁移。并发重复关闭必须各自固定同一旧 entry 并等待其真实交付结束，不能清除稍后新代。DisableAll 永久停止新 Open/交付并逐项排空，幂等；admission-disabled 不等于 drain-complete，重复 DisableAll 也必须等待全部旧在途交付，包括已处于 closing 的 entry。它不证明 SDK 回调终止。调用者保证成员调用期间对象存活、receiver 至少存活至 Close/DisableAll 返回；析构只在外部控制线程进行，不宣称支持用户 listener 内同步删除自身 Participant。若上层 callback 不返回，Close 不得报告已正常关闭或擅自释放 receiver。

TDD 覆盖重复打开、不同 receiver、未知 local/remote、错误长度、跨实例/旧 ticket、关闭后重开、异常 callback 租约归还、暂停 callback 时关闭尚未返回且新交付已拒绝、并发关闭及永久 DisableAll。独立设计审核确认后才进入行为 RED/GREEN；此切片最多证明 receiver 生命周期核心，不能代替 Socket 通信、完整 Participant 生命周期或 native callback 排空验证。

key-only Close(local) 关闭调用时的当前注册；它不能辨认在新 Open 之后才执行的旧资源迟到关闭请求。已进入 Close 的等待者必须固定旧 entry，但未来异步关闭任务还需携带 entry identity，或由控制面串行化资源生命周期。不能用内部 ticket 声称任意 key-only 外部关闭都具备旧代识别能力。

独立设计决策：`.artifacts/review/20260914T023633Z-input-channels-design/DECISION.md`。上述五项澄清与 key-only 关闭边界均已采纳；允许开始 TDD，但没有据此新增实现或执行通过结论。

### 16.4 S3-D1 原子批量发送接纳（final host/build/review 已完成；限定范围）

依据 `.artifacts/review/20260914T032655Z-runtime-send-design/DESIGN.md`，先扩展已有生产 SendQueue，不增加第二套队列/计费，不接入 SDK。`SendTargetView{const Route* route, uint64_t generation}` 只在调用期间借用，全部 targets/route/buffers 必须有效且不变；null、空批次及重复 remote 拒绝。现有 Enqueue 只构造一个栈上 view 委托 EnqueueBatch，不在包装层先复制 Route；只计算一次 min(original deadline, entry+20ms)。

整个批次预检路由、前缀、限制及乘加溢出；每目标保留独立 payload 与 lease，N 个目标计 N 条/N*total_bytes。沿用同一个 mutex，一次预留所有 stable-remote/global 容量或一个也不预留，不持有部分配额等待其它 peer。peer map 插入失败必须清理本次零占用节点；离开锁后复制所有自有 Route/payload，失败、关闭或截止前未发布则全部退还且迭代销毁暂存链。完整 intrusive chain 在一次无分配临界区发布，TryTake 不能看到未发布批次的前缀。发布后各目标的过期、提交及结果仍独立；此事务只覆盖本 SendQueue 调用的接纳，不能称为网络送达或跨 Transport/write 的原子性。

TDD 至少包括第二目标满时零接纳、重复/null/不可能大批次及溢出不增长 map、逐目标真实分配失败全部退款、复制期间 Close/截止/并发 TryTake 不暴露前缀、成功顺序及原始 deadline/priority、所有 inflight 在 Close/queue 销毁后继续计费，保留旧单目标回归。当前工作区已加入 batch 接纳及记录所有权移交；final tester `.artifacts/verification/20260914T041229981Z-send-batch-final/` 已完成 direct Debug/Release 各 26 组、root Debug/Release 各 14/14、consumer 与 SDK23 arm64 static build，独立 review `.artifacts/review/20260914T040359Z-send-batch/REVIEW.md` 无 blocking。本阶段仍仅证明队列核心，不能标 DEVICE_TESTED 或完整 Transport VERIFIED。

### 16.5 S3-D2 实际发送资源桥接（实现与限定范围验证完成）

具体规格为 `.artifacts/review/20260914T041949Z-runtime-send-d2/DESIGN.md`，独立审定 `.artifacts/review/20260914T042537Z-runtime-send-d2-independent/REVIEW.md` GO。新增生产 EgressPlan / RuntimeSendPath / DSoftBusSenderResource，不改 Directory、Batch、InputChannels 或上游 Fast DDS；本切片仍不包含 Descriptor、完整 Transport、native worker 或默认可用 backend。

EgressPlan 保留经 Directory::Build 验证的不可变快照，为每个完整 remote 选择唯一本地路由。单候选自动选择；多候选必须显式指定 local，并固定非零 expected_route_version；重复、非法、不存在或过期选择使整个构建失败。Runtime 的连接代数来源必须由显式、线程安全、非阻塞的已有连接快照接口提供，核对完整 Locator 对、route version、非零 generation；无默认 generation、无 SDK 建连或授权。测试使用显式 ControlledGenerationSource，不当作真实连接。

实际 SenderResource 绑定当前 send_lambda_，保留优先级和 legacy=0；完整目标迭代器按本 runtime 的规范 remote 归属筛选、去重、保留首次顺序，转成精确 NetworkBuffer 前缀，恰好调用一次 EnqueueBatch。只在该资源的非空归属批次全部接纳时返回 true；NoOwnedTargets、非法项目 Locator、解析/分配/截止/队列失败分别可诊断且不部分接纳。20ms 只指队列接纳阶段；资源门等待和准备继续受原始绝对 deadline 检查，记录不丢失原 deadline，不承诺整次 send 的硬时间界。

每个 runtime 使用强持有的独立 identity；同一 SendResourceList 重复 Attach 幂等，同一列表内不同 runtime 的 remote 所有权重叠则拒绝。列表访问由调用方串行化；Attach 最终 stop 检查/发布与 Runtime Close 请求共用短门，避免检查后追加竞态。Release 与 send 共用资源门：先取得门的重叠 send 可先完成，Release 返回后不得再发布；Release 不关闭共享 runtime 或作废既有 inflight。send_lambda_ 构造后保持不变，Release 不清空/重绑它，避免基类在门外读取 std::function 的数据竞争。Runtime Close 每次都跨过队列关闭线性化点；不声称 SDK drain。对象不得在成员调用期间并发析构。

TDD 覆盖规格中的 DSP 场景：真实当前基类 priority/legacy 调用、路由选择及冻结、混合/重复/未知目标、完整所有权、连接快照反例、attach 幂等/重叠/关闭竞态、资源释放与共享 runtime 区分、真正分配失败退款、原始截止时间以及无晚发布。先行为 RED/GREEN，再独立 host/consumer/SDK23 build tester、代码 review、final tester 和文档同步；不合并宣称设备 Transport 或 DDS 验证。

实施中真实 allocation-fault RED 暴露 Directory 依赖缺口；最小修正已完成并纳入最终回归。最终独立 tester `.artifacts/verification/runtime-send-final-20260914T0527Z/RESULT.md` 已验证 direct D/R 各 27 组、root D/R 各 17/17、public consumer 和 SDK23 arm64 static build 均 PASS；独立 review `.artifacts/review/20260914T052640Z-runtime-send-d2-code/REVIEW.md` 无 actionable finding。该切片仍明确排除 Descriptor、native backend、真实 SDK Send、设备 Transport、DDS/RMW；状态为 IMPLEMENTED/BUILDS/HOST_TESTED/REVIEWED（限定范围），不是完整 Transport VERIFIED。

### 16.6 Socket 生命周期与回调关闭（LC3a/LC2 已实现并完成限定范围验证；后续切片 PLANNED）

#### 16.6.1 LC pure-validator（限定范围完成）

六个冻结文件已完成 TDD、独立 initial tester、independent code review 及 review 后 final tester。最终证据：`.artifacts/verification/lc-launch-validator-final-20260914T074210082Z/RESULT.md`（review SHA `D6EC21A9FA1EAAF4422CC22B8DCCAFD32D4F96FE076ED9EEFF5BCAD308F6CEB1`）。`record v1` 130/130、兼容性 16/16、LC validator 169/169、RecordPairHarness 10/10、语法检查 6/6 均 PASS，九份冻结/对照文件哈希为 MATCH。

本切片保留 record v1 原有行为，但对显式 `lifecycle-*` profile 作有意的明确拒绝（包括 schema1 的 record argv）；LC 使用 schema2 严格 profile、board role、ELF/provider/lstat/raw reference/timestamp 绑定，并输出 detached frozen binding。该纯校验器不读设备、不计算输入文件哈希、不授予或撤销权限、不认证证据真实性，也不证明进程退出或 provider 已加载。LC observation runner 的四文件 TDD 已由 `.artifacts/review/20260914T-lc-runner/MAIN_DECISION.md` 批准；最终独立 tester `.artifacts/verification/lc-runner-final-20260914T120200Z/RESULT.md` 已通过 harness 80/80、runner 42/42、validator 130/130、兼容性 16/16、LC validator 169/169、RecordPairHarness 10/10、syntax 4/4 及私有 real-fd/process diagnostic。修复覆盖已观测 collector exit-before-probe race；full log overlap、SDK quiescence、设备/Transport 仍未验证，因此不能把这些结果当作设备或 Transport 证据。

状态：`IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED`（仅纯校验器）；`DEVICE_TESTED / INTEGRATION_TESTED / VERIFIED = NOT RUN`。同名 Participant 热重建仍为首版硬要求，不能被测试启动器改成名称复用限制。

设计为 `.artifacts/review/20260914T052846Z-socket-lifecycle/DESIGN.md`（SHA 前缀 `FD5E69`），独立设计 review 为 `.artifacts/review/20260914T054021Z-socket-lifecycle-independent/REVIEW.md`。LC3a ownership 与 LC2 callback-close 当前实现已冻结，并由独立代码 review `.artifacts/review/20260914T-lifecycle-code/REVIEW.md` 报告无 actionable finding；最终限定范围 tester 证据为 `.artifacts/verification/socket-lifecycle-final-20260914T063102248Z/RESULT.md`。批准顺序仍为 LC3 ownership → LC2 callback close → LC1 固定最多 3 次 → LC3b/c + LC4a/b；LC4c 不在当前批准范围。

当前状态为 `IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED`（仅 LC3a/LC2 host/platform slice）。最终 tester 验证 build-host Debug/Release 各 12/12、build-probe-host Debug/Release 各 11/11，以及 SDK23 AArch64 build/readelf；冻结的 5 个源文件和 3 个 recipe 在执行前后 SHA 一致。该结果不是设备生命周期验证：两板、权限/token、真实 DSoftBus Socket 通信、SDK callback、SDK Shutdown/quiescence 均 `NOT_RUN`，因此不能标记 `DEVICE_TESTED`、`INTEGRATION_TESTED` 或 `VERIFIED`。LC1、LC3b、LC3c、LC4a、LC4b 仍为 `PLANNED`。

本切片冻结实验包络：单次发送 1 s、关闭 3 s、进程绝对工作预算 7 s（须在其中预留关闭时间，不是独立 watchdog）、外部 watchdog 10 s、服务端 hold 500 ms、安静观察 200 ms。必须区分 `NOT_EXERCISED` 与 PASS/FAIL；未覆盖的场景不能宣称已验证，但不阻塞无关的核心工作。回调保留、进程失败非零退出、token 恢复和资源归属必须保留；权限写入由 Main 串行执行，每次写入前后执行三项 guard 和新捕获，tester 不写权限。该计划仍不改变 DDS/RTPS 语义，也不等于生产 Transport 或 callback quiescence 已验证。

### 16.7 真实 Fast DDS 配置与二进制绑定（IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED；限定范围）

实施规格为 `.artifacts/review/20260914T055000Z-real-fastdds-binding/DESIGN.md`（SHA256 `CE8C9960EE053343B82EDA5D1FFFCA32C20BD7C3F2A719387EB4977F46F45808`）。默认 SOURCE_PROBE 保留现有独立探针；显式 CONFIG 使用指定绝对路径下的 Fast DDS 3.6.2.0 shared CONFIG 导入目标 `fastdds`。四核心和 S1 使用统一构建依赖；CONFIG 不生成或导出探针配置头，不导出源码 include，不编译另一份上游 TransportInterface.cpp，不定义 FASTDDS_STATIC_LINK。现有核心 C++ 语义不变。

当前已构建的真实 host/OHOS 库仅 Release，CONFIG 拒绝空/Debug/混合配置和不兼容 MSVC CRT，不能借 Debug→Release 映射假装 ABI 匹配。`fastdds`、`fastcdr`、`foonathan_memory` 三个预加载 imported target 必须由创建者提升为 `IMPORTED_GLOBAL`；`MAP_IMPORTED_CONFIG_RELEASE` 只接受未设置或单一、大小写不敏感的 `RELEASE`，显式空值、DEBUG 或列表均 fail-closed。验证实际包路径、导入目标/依赖及配置头来源，拒绝错误模式/版本/路径/冲突导入，保持现有 core option 依赖。所有核心与 S1 关闭的工具构建不强加 Fast DDS 依赖。

先建立真实目标属性和 consumer RED，再修改 CMake。当前 frozen eight-file slice 的最终 host 证据 `.artifacts/verification/real-binding-final-20260914T064920994Z/RESULT.md` 为 PASS：SOURCE_PROBE Debug/Release 各 17/17、source directory consumer 1/1、CONFIG 规则矩阵 32/32、CONFIG root Release 17/17、flat consumer 2/2、nested GLOBAL/mapping consumers 4 组、poison/absent SOURCE_PROBE 17/17。SDK23 arm64 真实 consumer/S1 构建、ELF 检查及两板 ABI/model 执行均 PASS，wrong-architecture 拒绝为预期 PASS，证据 `.artifacts/verification/real-binding-sdk-final-20260914T065331423Z/RESULT.md`；两板测试未运行 DSoftBus Socket 或 DDS。初始 bare-PATH、长路径和错误部署失败仍保留为边界证据，未被改写或计为通过。真实 DLL 内部分配不自动纳入 EXE operator-new 注入覆盖。独立 fix review `.artifacts/review/20260914T-binding-fixes/REVIEW.md` 为 PASS；这里只建立真实库绑定，不宣称完整 Transport、发现、样本、SDK 关闭、同名 Participant 热重建或 RMW 已验证。

### 16.8 Descriptor 与 Transport 组合层（限定范围已完成；native backend 待实现）

具体规格 `.artifacts/review/20260914T045545Z-transport-facade/DESIGN.md`，独立 GO 为 `.artifacts/review/20260914T054558Z-transport-facade-independent/REVIEW.md`。本切片新增实际 Descriptor/Transport 派生类，复用现有核心；它是原架构的中间实施阶段，不是用模型代替 native backend。后者未接入时，生产 create_transport 创建的对象必须在 init 明确失败并留下固定、无分配的 stderr 诊断 NativeBackendUnavailable；私有 HostModel 仅供测试，不可通过配置/环境选择为生产 fallback。

最终独立 host tester 与 SDK/双板 ABI 边界测试已通过，证据分别为 `.artifacts/verification/facade-host-final-20260914T075048953Z/RESULT.md` 与 `.artifacts/verification/facade-sdk-final-20260914T075114524Z/RESULT.md`；独立 review `.artifacts/review/20260914T-facade-code/REVIEW.md`（SHA `11CDE5D3BD5394CFADE995577239CE233115CA0494C2725D7AA33C38F5695F35`）无 actionable finding。SOURCE facade/root Debug/Release、CONFIG root/consumer、重复运行、配置负例及 IC-12 均按报告 PASS；两板仅验证 SDK23 AArch64 ABI 与 failed-factory 边界，未验证通信。状态为 `IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED`（限定范围），native backend、真实 Socket/DDS/RMW 仍未实现或未运行。

冻结规范 port=1、精确 local/default/initial peer，保留他 transport 的 selector 选择，aggregate sender 同一列表只附加一次。输入 Open 成功后若 shutdown 赢得最终发布竞争，必须同步回滚后才返回 false，不能遗留已失去拥有者的 raw receiver。每个并发 shutdown 都穿越 send queue 和 input callback 排空屏障；局部输出端点消失不关闭仍服务其它远端的 aggregate sender。callback 栈内 Open/Close/shutdown/destruction 均不在支持的控制契约内，外部控制线程负责生命周期。

实现与全部核心必须采用 16.7 的真实 CONFIG 头/库/Release ABI；不得嵌入第二份 TransportInterface.cpp，不把 SOURCE_PROBE 配置混入。新增 FCD 用例覆盖工厂、配置冻结/失败、Locator/selector、receiver 注册与并发关闭、实际 sender 及真实库 consumer。初始 tester→review→修复→final tester→docs 后才登记限定范围状态；native Socket、SPDP、SEDP、样本及完整 Participant 实机销毁仍另行验证。

### 16.9 同名 Participant 热重建：接收 attachment 首切（R1 与 root wiring 限定范围完成；仍未实机验证）

完整设计 `.artifacts/review/20260914T-hot-rebuild-native/DESIGN.md`，独立审核 `.artifacts/review/20260914T-hot-rebuild-independent/REVIEW.md`；Main 裁量和实施规格为同设计目录 `MAIN_DECISION.md`。修订 R1 已完成：新增 ApplicationAttachments 自行构造每代唯一 InputChannels，封闭 Open/Capture/Close/DisableAll；调用方不能任意组合 local、owner 与 opaque ticket。内部铸造 AppIdentity/ReceiverBinding，旧 binding 的迟到 Close 与旧 claim 的重复 Detach 不触及新代。final 独立证据 `.artifacts/verification/application-attachments-final-20260914T082700Z/RESULT.md`：SOURCE Debug/Release、CONFIG Release 三组 AA-01..08 均 8/8，Release 重复 20 次，SDK23 arm64 final link PASS；R1 closure review `.artifacts/review/20260914T-attachments-r1-closure/REVIEW.md` 无 actionable finding。root public target wiring 已由独立 final tester 在限定范围通过：`.artifacts/verification/attachments-wiring-final-20260914T093400Z/RESULT.md` 报告选项 7/7、SOURCE Debug/Release 22/22、CONFIG Release facade ON 23/23、consumer、binding/facade 矩阵和 SDK23 arm64 final link PASS；接线 review 无 blocking。64 次是模型 application attachment 周期，不是 DDS Participant 或真实热重建；本 slice 未执行 HDC、SDK runtime、DSoftBus Socket 或 DDS。首切不调用 SDK，不创建线程池；后续 facade 必须委托唯一控制面，不能维护第二份实际 core。native backend、真实 DDS/SPDP/SEDP/样本、同名 Participant 热重建和 RMW 仍未运行。

固定逻辑 local 槽的 claim 全有或全无；接收准入先固定本代身份并预留配额，再锁外复制完整字节，发布时核对同一 binding。无接收器空窗丢弃；move-only 已取工作持续计费并只调用旧 core。Detach 撤销本代准入、作废队列，等待控制发布/回滚及真实 receiver 排空后才释放活跃槽；晚结束的旧有界复制/工作保留原代预算但不得再访问已解除的 raw receiver。正常顺序重建不累积 tombstone，强引用无环并可观测回收。

首切实验默认上限：8 逻辑槽、16 retained applications、全局 64 条/512 KiB、每 local 32 条/256 KiB、8 并发复制、单条 60000 bytes；正有限可配置且拒绝溢出/不可能配置，准入不等待。保留外部旧 claim/binding/work 也计 retained-app 配额；不得用总重建次数作为容量单位。这些不是产品性能保证。

TDD 覆盖独立审核八组反例及至少 64 次同槽正常重建、真实同步 receiver barrier、并发两个 Detach、旧 copy/queued/taken 不串投、同代关闭后重开、Open/Detach 竞争、OOM 回滚退款、跨模块非法句柄、异常回调与最终回收。限定 host/module 测试不等于真实 Participant 热重建；最终仍须双板相同 DDS 名称和逻辑配置、启用后 GUID、重新 discovery/match/样本及单方/双方重建，不改 DDS/RTPS 语义。

TDD 发现的前置修复例外：真实 Debug 分配失败栈及 Main 新增 IC-12 均复现既有 InputChannelsState 默认 vector 构造中 noexcept 分配导致终止。Main 批准仅将空 entries 改为可传播 OOM 的 `entries(std::size_t{0})`，保留全部输入接口/锁/交付语义，增加逐实际构造分配点故障及恢复测试；不通过禁用 Debug iterator、跳过 OOM 或绕开核心掩盖失败。原始 RED 仍保留于 `.artifacts/tdd/transport-facade/input-oom-red.log`；facade/root/SDK 消费者及 IC-12 已由最终独立 host/ABI 证据复测通过（见 `.artifacts/verification/facade-host-final-20260914T075048953Z/RESULT.md`），DLL 内部 OOM 仍 SKIPPED。原核心 final 证据仅证明其旧哈希，不能覆盖本次修复。

### 16.10 稳定 native owner 首切（实现已存在；限定范围验证完成；不是 native backend）

完整补充合同与 Main 批准位于 `.artifacts/review/20260914T-native-service-slice/N1_CONTRACT.md`、`MAIN_DECISION.md`；独立 R1–R3 closure 位于 `.artifacts/review/20260914T-native-owner-design-review/CLOSURE.md`。只新增 NativeOwnerTable 头/源与模型测试，不改现有核心，不调用 SDK 或创建线程。按真实普通 Bytes 创建等价键拒绝不同逻辑 owner 的冲突，分别检查逻辑反向唯一性和同名同角色 callback namespace 一致性；不要求 Socket 名称全局唯一。健康同配置 owner 重取必须 NoCreate，跨正常 application 换代保持原 ID/代数/槽，不按重建次数耗额。

每次 Socket/Listen/同步 Bind/Shutdown 用一张内部 typed ticket；调用前预约、进入后仅真实/模型完成结算，关闭先于 Socket 返回也须保留并登记原返回 ID，不能误关其他 owner。controller 与 opaque leases 强持无环固定 State，controller 销毁封新准入、不虚构在途完成；原 ticket 可完成原 State。OnShutdownSeen 是原 owner 单调停止事实，取消尚未 entered 的 Shutdown 不得被旧 ticket 或迟到 Bind 再生；已 entered 调用仍独立计费至返回，ID/owner 保留且 quiescence 始终 Unproven。

批准模型实验上限 8 namespaces / 12 total owners / setup 1 / send 2 / close 1，允许正值下调，不是板端容量结论。N1-CFG/CACHE/TICKET/CLOSE-SOCKET/CLOSE-POOL/LATE/INBOUND/LIFETIME/QUOTA 九组已完成独立 final tester 的 SOURCE Debug/Release、host CONFIG、public consumer 验证；SDK23 recovery 的实际 arm64 build/link/readelf 及 root wiring final `.artifacts/verification/n1-root-wiring-final-20260914T1413Z/RESULT.md` 也 PASS。fresh cache 中 `BUILD_DSOFTBUS_NATIVE_OWNER` 跟随 `BUILD_DSOFTBUS_DIRECTORY`，启用时构建公共 SDK-free `mdds_dsoftbus_native_owner`；测试和 CTest 仅在 `BUILD_TESTING=ON` 时注册。旧 Permission denied 失败记录保留为历史证据。64 轮仅为同 State 的模型 cache reuse；实际 Participant 热重建仍必须实机验收。N2 同门发送、接收窄扩展、线程/provider 驻留、实际 SDK 并发关闭/ID 复用另行过门，production 继续 NativeBackendUnavailable。

### 16.11 已验证 attachment 模块的根构建接线（限定范围完成）

有限方案见 `.artifacts/review/20260914T-attachments-wiring/PLAN.md`：新增 PUBLIC 静态 attachment target 和依赖 input 的独立构建开关，注册既有八组 attachment 测试及两个纯 LC/record 兼容 Node suite；新增配置矩阵与真实 CONFIG 外部 consumer。先证明根目标缺失的 RED，再接线，不改已验证模块行为或旧测试预期。最终独立 tester `.artifacts/verification/attachments-wiring-final-20260914T093400Z/RESULT.md` 报告选项 7/7、SOURCE Debug/Release 22/22、CONFIG Release facade ON 23/23、consumer、binding/facade 矩阵和 SDK23 arm64 link PASS；接线 review 无 blocking。外部 consumer 不调用 SDK、不创建 Participant；本项仅表示 root build wiring 限定范围完成，不表示完整 Transport、DSoftBus runtime、DDS 或实机热重建完成。

### 16.12 N2 native send execution（限定模型已实现；完整 Transport 未验证）

N2 仅负责在同一原始 `NativeState` 上注册受控 egress execution，并复用现有 `RuntimeSendPath` / `OwnedSendRecord`；不创建第二套 payload queue、不接入真实 Socket、不启动线程、不启用 facade、不修改 DDS/RTPS，也不提供 RMW。设计资料为 `.artifacts/review/20260914T-native-send-execution/{DRAFT,REVIEW,MAIN_DECISION,CLOSURE}.md`；closure 无 actionable contract finding。

实施契约固定为：不暴露 runtime getter/raw `TryTake` 造假入口；Attach 只能经 private runtime bridge；一个 `NativeState` 统一承担 whole-runtime ceilings，必须计入 externally retained sender；`Revoke`、taken、P0-admitted、provider-entered 三窗口分别定义；实际 positive return/exception 使用 typed fault/unknown-submission，不伪造 SDK code；最终 entry gate 后重新检查原始绝对 deadline。注册、工作和调用对象的清理在 gate 外完成，不能关闭健康 native owner。

N1 精确提交 `2c28e9325b941a2b8c0f198b0ddd82f90c4b073e`（tree `8147a261e437cb3a62b59ccd7c9a3f456be93bd5`）后，N2 六文件模型已完成实现、TDD RED/GREEN、独立 review 与 final tester。Canonical final receipt 为 `.artifacts/n2-execution/final-tester/RESULT.md`（SHA-256 `3AC3B952B424D3EDFF1A16D584463ABA89F369D9E0A37527316B32D3955612F7`）；SOURCE Debug/Release 各 24/24、CONFIG 25/25、20×10 repeat、N1 9/9、attachments 7/7、binding 32/32、facade 4/4、SDK23 arm64 link/readelf 均 PASS。六文件 closure2 certutil hashes 与 initial/review receipts 一致；review SHA `227E9479AC4CA318B1B89BD51AE66D554ED47F69819DE2B13E1CAB8CA6D660D7` 无 actionable findings。

该状态仅为 `IMPLEMENTED / BUILDS / HOST_TESTED / SDK23 ABI LINK/ELF TESTED / REVIEWED` 的 SDK-free injected synchronous model。隔离入口 `.artifacts/n2-execution/build/CMakeLists.txt` 被忽略，root CMake 有意不暴露 N2；其完整内容、创建位置、SOURCE/CONFIG/SDK 复现命令及外部依赖路径见 `docs/N2_COMPLETION.md`。真实 Socket/API、SDK Send、DDS/RTPS、设备/跨设备、RMW、同名 Participant 热重建仍 `NOT RUN`；device 仅对此模型 slice 为 N/A。首版同名 Participant 热重建硬要求仍有效，64 model cycles 不替代该验收；N2 仍未提交，用户已授权 N2 模型单独提交；本记录为提交前证据快照，不等于完整 Transport 验证；scoped model gate PASS 不等于完整 Transport DONE。

## 17. N3 冻结实施契约（2026-09-15；PLANNED）

本节记录用户接受独立设计审核后作出的最终范围裁量。N3 以本节为准；与 §6、§16.9、§16.12 中早期规划或 N2 历史行为冲突的内容，不再作为 N3 实施规格。那些章节及 `docs/N2_COMPLETION.md` 的原始提交前证据继续保留，不改写为 N3 测试结果。

### 17.1 基线、目标和证据边界

开工基线为 N2 提交 `def10ea376cb3bf9921c1c7ea74c92b1da9e04e8`。独立设计审核位于 `.artifacts/n3-contract/INDEPENDENT_REVIEW.md`；审核指出的发送排空、锁外关闭、真实 receiver lease 和 copying 排空要求已由用户提升为硬契约。设计名称仅表达职责，不能据此认定 API 已经存在。

目标：复用真实 N1、N2、ApplicationAttachments 和 InputChannels，组合出具有严格 application 代际边界的运行模型；所有成功准入的发送可终局结算，接收同时固定 native/application/receiver 身份，copying/delivery 均有账本，冻结不在协调门内等待，联合 Detach 完成后才允许同 local 下一代发布。

当前本节状态仅为 PLANNED / 用户范围已冻结；N3 实现、构建、测试、SDK23 验证及提交均须重新取得证据。N2 历史通过不能继承为 N3 PASS。

### 17.2 构建范围：根目标，不交付安装包

N1、N2、N3 均进入根 CMake 生产目标；测试链接同一生产目标，不通过另一份源码列表重复编译形成影子实现。N3-1 先独立完成 N2 根接线并复现原验证，再开展生命周期修改。

保留以下验证：

- SOURCE Debug / Release（当前 CMake 模式字面值为 `SOURCE_PROBE`）。
- CONFIG facade 和根构建外部消费者：已安装 Fast DDS + 本项目作为源码子工程。
- SDK23 编译、链接、AArch64 ELF 检查，分别记录命令、产物和结果。
- 无真实 backend 时 `DSoftBusTransport::init()` 继续以 `NativeBackendUnavailable` fail-closed。

本项目自身的 `install(TARGETS ...)`、`install(EXPORT ...)`、项目 Config/ConfigVersion 文件、`find_package(FastDDSDSoftBusTransport ...)`、安装后 include/lib 布局、package namespace/exported target ABI 和独立安装树消费者均不属于 N3。后续单列分发契约：install tree → find_package → imported target → 外部消费者。当前仅保持 PUBLIC/PRIVATE、include 可见性和测试依赖隔离的基本卫生。

### 17.3 七条不可豁免的不变量

| 编号 | 硬契约 |
| --- | --- |
| N3-I1 | 记录一旦固定到 application P0，永远不能重新绑定 P1。 |
| N3-I2 | 接收记录的 native 身份是独立约束，不依赖长期持有 NativeCallbackLease。 |
| N3-I3 | Revoke 线性化点阻止新的成功准入；此前成功准入的所有 invocation 都计入待排空集合，包括尚未 Run 的 invocation。 |
| N3-I4 | 接收执行许可必须同时取得 InputChannels 对原 receiver incarnation 的真实使用租约。 |
| N3-I5 | Detach 等待本 application copying activity 归零，不能仅等待现有队列状态。 |
| N3-I6 | 协调门仅作短状态转换；Close、Revoke 等待、provider/receiver 调用和排空等待都在锁外。可能反向调用的清理/析构也在锁外。 |
| N3-I7 | P0 尚未 Detached 时，同 local 的 P1 不得发布。 |

application、native connection 和 process service 三种寿命独立：application 换代不强制 native generation 换代，native 重连也不自动创建新 application。稳定服务可跨 application 存活，但不能保留旧 receiver 权限或重置容量账本。application 不默认等同 Participant 名称、GUID 或 Socket。

### 17.4 成组创建、发布和回滚

Coordinator 内部创建不可拼接的 ApplicationBundle，封装原 claim、egress registration、接收附件及代际；外部只取得受控句柄。

Preparing → Active → Detaching → Detached；Preparing 失败进入回滚，不对外发布：

1. 预留同 local 槽位。
2. 准备 claim、N2 registration 和附件。
3. 成组发布 Active 后，才返回可执行的句柄。
4. 任一失败点撤销并结算已经获得的资源；回滚完成前保留 local 占用。

允许 Active 尚未打开 receiver；此时接收为 NoReceiver，不保存给未来 receiver。Preparing 或无 application 空窗期的输入不进入下一代。创建失败注入覆盖登记、分配、附件准备及准备期间服务关闭；同 local 并发 Attach 不得出现双发布。

### 17.5 发送准入与可组合撤销

发送 drain 使用 application 级 `admitted_outstanding`，不是仅使用 `provider_inflight`：

成功 Admit 加一；终局结算恰好减一。计数绑定执行权寿命，不绑定 C++ invocation 壳对象的析构。必须保持 `provider_inflight <= admitted_outstanding`。

最新用户 S1/S2 明确要求 N3 **取消并终局结算已准入但尚未进入 provider 的 invocation**；即使调用者从不 Run 或保留旧壳，也不能永久阻止排空；恢复 Run 不得进入 provider。已经进入 provider 的 invocation 则等待其实际返回和结算，不能伪造取消成功。

这是相对当前 N2 legacy Revoke 行为的显式扩展：现有 N2 允许已经 Admit 的调用在 Revoke 后继续 Run。不得静默改写旧测试或把它称为既有缺陷；N3 使用受控的组合撤销接口及新的确定性测试建立更强契约，不复制 N2 执行器。

窄接口职责（名称待实现确定）：

- BeginRevoke：短时冻结新成功准入，撤销未进入 provider 的旧调用资格，取得稳定关闭票据；不等待 provider，不在协调门内执行 Runtime Close/释放记录。
- PollRevoke / AwaitRevoked：锁外推进必要清理并检查/等待所有成功准入调用的终局结算。
- BeginRevoke 与最终 provider entry 必须有共同、可证明的先后关系，不能由两个无关 bool 检查拼成。
- 终局结算必须涵盖取消、过期、失败、异常和正常返回；持有终局对象仍占其实际 retained 资源，但不再占执行活动计数。

保留原字节、目标、generation、route version、绝对 deadline、transport_priority 及 N2 全局容量契约。旧发送资源撤销后不得重新获权，仍持有的 runtime 继续计费；不得为了热重建清零账本。

### 17.6 接收身份、复制及真正的交付许可

复用现有接收 payload、预留、容量、队列和记录所有权，不新增第二套存储。

每条受控记录固定 service/state incarnation、原 application、原 receiver incarnation、native owner/generation、route version、local/remote、自有字节及额度。Socket ID 仅关联和诊断，不是完整身份；固定约束不能公开任意拼接。

NativeIdentityConstraint 是不可执行、可验证撤销的原始身份约束，不增加 callback_inflight。NativeCallbackLease 仅覆盖 SDK callback 栈上活动，不能留在 Queued/Binding 记录中。身份约束不意味着 owner 当前 Ready，也不凭 Socket ID 重新补上最新 generation。

Capture 在最早受控入口固定原归属、预留额度并增加 application copying_inflight；退出门复制字节，再入门仅验证原凭据后提交或取消。所有额度、记录所有权转移/退款结算完成后，才减少 copying_inflight。RAII 覆盖失败/异常；copying=0 必须意味着不再有旧 capture 能产生记录或改变旧 application 接收账本。

最终 TryEnterDelivery 联合验证原 application、receiver、native owner/generation/route/version、Ready、未过期及 once-only 状态，同时取得 InputChannels **实际** receiver-use lease，并进入 Dispatching、增加真实在途计数。锁外使用此 lease 调用原 receiver；不重新查找当前 receiver。异常同样完成租约和记录结算，不重复交付。

不得在排队时长期持有 receiver-use lease。撤销之前已取得真实许可的交付属于在途，允许原 receiver 完成，关闭等待其返回；撤销之后不能再获许可。当前 InputChannels 的原 ticket 已不会改投新 receiver，本修订补的是联合准入与真实租约的原子边界，不宣称当前核心已经存在 R0→R1 误投缺陷。

### 17.7 共同裁决和联合 Detach

实施前列明现有 native、attachment、input、runtime/queue 锁顺序及全部状态修改入口，包括 N1 operation 完成、RequestClose、OnShutdown、service close。只让接收路径遵循一把 coordinator mutex 不足以建立共同裁决。使用窄桥接/受控入口，不要求无关 N1 模型调用者采用 N3。

三阶段：

- Freeze：门内 Active→Detaching，永久关闭本代新发送/接收准入及许可，固定原 registration/receiver/control 对象，保留 local。
- Revoke / Close / Drain：门外关闭队列/receiver，推进未进入 provider 的调用和待处理记录终局结算，等待已进入 provider、复制、真实接收租约归还。
- Finalize：再次短暂入门，确认仍为原代 Detaching 且联合条件成立，再标 Detached 并释放 local。

Detached(A) 至少要求同时满足：

```text
ClosedForAdmission(A)
AND SendAdmittedOutstanding(A) == 0
AND ReceiveCopyingInflight(A) == 0
AND ReceiveDispatchInflight(A) == 0
AND PendingAttachmentWork(A) == 0
AND BindingHeldRecords(A) == 0
AND NoFutureExecutablePermit(A)
```

先 Freeze 后 Drain，不能先看计数为零再封入口。已撤销的外部壳可以晚释放，但必须已失去执行权/receiver 使用权，仍按实际资源计费。重复 Detach 指向同一过程；Pending 显示具体阻塞项，不是生产关闭成功。可在 callback 内发起非阻塞关闭，不可等待包含自身的 drain。SDK/provider 不返回时保持 Pending，不允许 P1 提前发布，也不杀进程掩盖失败。

### 17.8 Binding 有界停驻

Binding 记录必须已经固定 P0、R0、O0/G0/V0、字节、容量和绝对到期时间；不是等待未来主人的缓存。到期时间为 `min(bind_deadline, capture_time + finite_binding_hold)`；无 bind deadline 时也必须有有限 hold。唤醒、Ready、复查和重新排队不得重置时间或取得免费额度。

同一身份 Ready 后才可能获得最终交付许可；失败、native/application/receiver 撤销、原路由失效或过期都使原记录作废，不改投新身份。晚到 Ready 不复活 P0；callback lease 可以已归还，而 Binding 记录独立存活。不得让长期 Binding owner 造成其他 Ready owner 全局队首阻塞。

N3 使用注入时钟和受控驱动器，不引入生产计时线程。当前 NativeState 配置不可变；路由失效检查针对原身份/版本及撤销，不扩张为未经批准的动态配置发布系统。

### 17.9 必测矩阵及证据规则

测试链接真实 N1/N2/Attachments/InputChannels，仅注入外部 provider、时钟及确定性驱动；不得用三个 mock 的各自成功替代组合证据。

| 标识 | 确定性交错/失败 | 必须观察到的结果 |
| --- | --- | --- |
| A1–A3 | claim 成功 egress 失败、Preparing 来流、同 local 并发 Attach | 回滚完成前不让出 local；未发布不可执行；无双发布 |
| R1–R5 | 错 native/generation/version、复制中换代、空窗、receiver 换代、凭据重复使用 | 拒绝/原代结算，不重新绑定、不重复交付/退费 |
| R6–R7 | native revoke 时 Binding/Queued/Taken；真实许可后暂停 | 未获许可作废；已获许可计在途、仅原 receiver 完成 |
| B1–B3 | 同步 Bind 返回前字节；失败/关闭/到期与 Ready 竞争；转 Ready | 有界停驻、唯一终局、不复活、不重置期限/复制/额度 |
| D1–D4 | Taken 未准入后 detach；provider 进入后暂停；持有旧 sender；重复/回调发起 detach | 无新 provider 进入；等待真实结算；旧壳计费无权；不自等待 |
| H1–H2 | 多轮同 local 重建；故意保留旧句柄达到上限 | 正常释放回基线；明确拒绝，释放后恢复，不重置容量 |
| S1 | Admit 成功但从不 Run，然后 Detach | 不以 provider_inflight=0 提前成功；撤销使未进入 invocation 终局结算，不等待壳析构 |
| S2 | Admit 后暂停，Revoke，再恢复 Run | 不进入 provider，恰好结算一次 |
| S3 | 已进入 provider，Revoke | 等实际返回及 settlement，不能伪造完成 |
| S4 | 一个 local 排空受阻，同时 Attach 其他 local/P1 | 同 local P1 不发布；无关 local 不被大锁阻塞 |
| R8 | 外层检查后、真实 receiver lease 前，关闭/替换 receiver | 恢复后不能调用新 receiver，也不能使用已撤销的原 receiver |
| R9 | 真正 ReceiverLease 已获得后 Detach | 等原 lease 返回，允许原 receiver 完成 |
| R10–R11 | P0 正在复制但队列为空，revoke 并请求 P1 | copying 非零不能 Detached；复制取消结算，无 P1 record |
| N3-N1 | callback 返回而记录仍排队 | callback_inflight 可为零；原 native constraint 仍在 |
| N3-N2 | G0 记录排队后 Socket ID 被 G1 使用 | G0 不能借 G1 获权；注入身份不算真实 SDK 复用证明 |
| B4 | Binding 等待时 callback lease 释放 | callback 正常排空，记录仍受独立额度/原期限约束 |

N3-N1/N3-N2 是接收测试标识，不混淆既有 N1/N2 开发阶段，也不对应历史 G1/G6。

barrier 至少覆盖固定归属后、复制结束前、Taken 后、真实许可后、provider 进入后、provider 返回但结算前。重复运行和线程检测仅补充，不能用 sleep 或循环次数代替交错证明。每轮重建检查计费与权限；故意不让 provider 返回只能证明安全 Pending，不能算正常关闭 PASS。安全性与在明确进展前提下的最终 Detached 分开记录。

### 17.10 执行顺序及准入门

| 切片 | 交付及准入 |
| --- | --- |
| N3-0 | 固定 HEAD/已有差异，刷新 GitNexus；核对锁、句柄、清理和本节硬契约；不清理用户/子模块改动 |
| N3-1 | 独立构建接线变更：N2 根目标、测试复用目标，先 RED 再 GREEN；复现原 SOURCE/CONFIG/消费者/SDK 验证 |
| N3-2 | pinned 接收、独立 native constraint、真实 receiver lease、once-only 许可；补必要窄接口和确定性测试 |
| N3-3 | 成组发布/回滚/Detach；N2 短撤销和 admitted_outstanding、copying 账本及联合 drain |
| N3-4 | 有界 Binding、完整竞态矩阵、容量边界、多轮同 local 模型重建 |
| N3-5 | 独立审核、修复后复测、文档同步、完整 pre-commit gate；明确实际 backend 后续准入缺口 |

语义切片实施前对最终接口和锁顺序做窄范围核对；不能因一个接口尚未存在而擅自减掉硬契约。各正常实现提交必须依次满足 §10 和 AGENTS：IMPLEMENT → 独立 TEST → agentic-review → FIX → 独立 RE-TEST → docs_updater → PRE_COMMIT → COMMIT。N3-1 的构建提交和后续语义提交保持可分辨。

### 17.11 N3 完成不等于真实 backend 交付

N3 scoped 完成需：上述代码范围、全部适用矩阵、SOURCE Debug/Release、CONFIG facade/源码子工程消费者、SDK23 compile/link/ELF、独立审核/复测/文档和提交门。未执行项目不得记 PASS。

N3 之后仍不得宣称：生产 Transport、真实 SendBytes、SDK callback 收尾、DDS SPDP、SEDP、样本、可靠性恢复、真实同进程同名 Participant 热重建或 RMW 已通过。

真实 backend 准入紧接 N3：实际 provider、callback 归属、固定线程、service 关闭及实际 DDS 热重建另取证。公开 callback 的 Socket ID 没有项目 generation；不能查当前 owner 后给迟到旧事件补新身份。必须以可验证 callback 收尾/无歧义复用或经批准的公开机制解决，不能把模型注入正确 generation 当解决方案。

首版真实热重建硬门槛不变：同一测试进程持续存活，复用同一 Participant 名称与 Transport local 配置，真实创建/销毁 Participant/Writer/Reader，新代分别完成 SPDP、SEDP 和校验样本；正常释放时资源不按轮次无界增长，无需杀进程、重启板卡/服务或换名。禁止将已固定到 P0 的内部记录投给 P1，但不把 DDS 合法的可靠性/持久性历史交付误判为内部串代。

## 18. SRT-1 共享 service-record 表管理冻结契约（2026-09-24 CST；IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED，限定范围）

本节冻结 `docs/FASTDDS_DSOFTBUS_TRANSPORT_V1_PLAN.md` 中已批准设计（DESIGN_APPROVED / NOT_IMPLEMENTED）的**有界实现**范围与验收合同。该计划文件是历史设计合同的重命名；本节不重写其历史事实。它不发布生产 backend，不改变 `NativeUnavailable`、P0 `BLOCKED / NOT QUALIFIED`、无 QualificationProfile 与设备未验证边界。

### 18.1 设计单位与责任边界

归属单位是**客户端 SDK 进程中的实际 service record 实例 + client/server role**，不是一个 numeric socket、也不是单独的 service-name 字符串（依据 `.artifacts/closure-20260921/incarnation/NOTES.md`）。本切片实现该单位的有界模型：

- 一个 record 含两个 role 字段（`Client` = Bind/拨号侧注册的字段，`Server` = Listen 注册并服务 accepted socket 的字段）。它们**不是一个 Participant 两个 listener**；"一活动 Participant 一 listener"不变。
- 每个存活 `(record, role)` 只有一个**固定 callback entrance**（进程内固定函数指针），该 role 下所有 socket 的注册都用同一指针；分派在 entrance 内按 socket 归属进行。
- 每个 socket 仍保有独立 native owner、独立关闭责任与既有连接/内存/线程预算；本组件只做**表归属**，不复制 owner 生命周期。owner 调用生命周期仍属 `NativeOwnerTable`（本切片不修改它）。

不新增 registry/mock 框架以外的机制：本组件自身即是必要的有界实现，`probes/softbus_ingress_contract.cpp` 的 11 个单 owner 用例保持不变，不被本切片替换或提升。

### 18.2 交付物与构建范围

- `include/mdds/dsoftbus/ServiceRecordTable.hpp`、`src/ServiceRecordTable.cpp`：SDK-free 有界模型，target 只依赖 Threads，不依赖 Fast DDS、不调用 SDK；测试目标为校对 callback 形状额外编译 pin 住的 DSoftBus 公共头文件（与既有 ingress 契约测试同做法）。
- `tests/service_record_table_tests.cpp`：按 case 分进程执行的 host 契约测试；根 `CMakeLists.txt` 增加独立 option/target/test 接线，默认跟随 `BUILD_DSOFTBUS_DIRECTORY`。
- 不交付安装包、QualificationProfile、设备包或权限操作。

### 18.3 不可豁免的合同

| 合同 | 固定行为 |
| --- | --- |
| slot 状态机 | `FREE → RESERVED_UNEXPOSED → EXPOSED → TOMBSTONED`；`UNANCHORED` 是 EXPOSED 内"零 anchor"子状态，仍不可回收、入口不可复用。首次可能把入口交给 SDK 前置 EXPOSED；注册失败也不回收该 slot。只有从未暴露的 reservation 可回到 FREE。 |
| 同活表重复注册 | 同一存活 `(record, role)` 上的重复注册不消耗新 slot、不改变 incarnation、不更换固定入口；入口不得二次派生。 |
| 多 owner 退出 | 释放一个 owner 不退休仍在服务其他 owner 的活表；已释放 socket 的**旧归属**不再重新归属：已捕获记录只以旧归属完成，释放后、重新注册前该 id 的迟到回调是终态丢弃。同一个 numeric id 之后可以由一个新 socket 重新注册为**新 membership**（新 incarnation），旧捕获永不改投它；numeric id 复用导致迟到旧事件被归属到新 membership 属已知平台前提（PF-02），不在 adapter 可修正范围内。 |
| 跨 role 保活 | 一个 role 归零而另一 role 仍有 anchor 时，record 与两 role 的固定入口都保留；该 role 之后的新注册仍用原入口。全部 anchor 消失后进入 `UNANCHORED`。 |
| 连续性未知 | `UNANCHORED` 下：同名 `ReserveRecord` 拒绝（`ContinuityUnproven`），新 owner 注册拒绝，该表的入口回调为终态丢弃。不得按名字、numeric id 或"通知已发送"补出连续性；其它 service name 不受影响。 |
| 真实销毁与同名新实例 | `ConfirmDestroyed` 只能从 `UNANCHORED` 进入；它结束两 role incarnation、永久 tombstone 入口并解除同名封印。同名新 record 必须取得**新 slot、新 incarnation、新入口**，绝不重新绑定旧 slot。 |
| slot 耗尽 | 只在需要新 incarnation 时、且在暴露前拒绝（`SlotExhausted`）；既有活表新增 owner 不受 slot 耗尽影响，仍受原 owner 容量约束。tombstone 永不回收。 |
| 句柄所有权 | reservation 与 membership 句柄为 RAII：一个从未暴露的 reservation 被析构或被覆盖时其 slot 回到 FREE；membership 被析构或被覆盖时释放该 owner。已失效句柄不释放任何东西，也不改变任何已捕获归属。调用者因此为每个已注册 socket 保留一个 membership 句柄，且不得在持有表门时销毁句柄。 |
| 归属线性化点 | 归属在归属门内一次性取得 `(record incarnation, role, membership incarnation, socket)`；已捕获旧归属只以旧归属完成，永不重新查表改投新 owner/role/record。无 anchor 记录当场终态丢弃。 |
| 关闭安全 | adapter 内部门不得包围 sink 调用；sink 可在回调内重入表操作；回调异常与无效输入被边界吸收并计数，不写入 SDK 调用、不重试、不迟到改投。 |

### 18.4 必测矩阵

`unexposed`、`shared-role`、`sibling-release`、`cross-role`、`unanchored`、`destroy-replace`、`still-anchored`、`exhaustion`、`failed-registration`、`capacity`、`handles`、`reentrant`、`concurrent`、`identity-boundary`、`allocation-failure` 共 15 个 case，每 case 独立进程（进程生命周期入口池不可重置）。并发 case 只断言不变量（归属计数恒等式、归属身份不越界、无错投、异常被吸收、in-flight 归零、释放后为终态丢弃、无死锁），不以 sleep、轮次或"确实交错"断言代替交错证明；该 case 因此不断言也不声称释放与回调确实重叠（在合法调度下可能完全不重叠，此时它仍是有效的计数与状态不变量测试）。两个边界 case 通过仓库既有做法触发有界计数与分配失败（header 内声明的 test-only 计数注入、测试进程内分配拒绝），并断言拒绝后没有部分状态。

### 18.5 声明边界

本切片完成后只能声明 `IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）`。真实 SDK service record 创建/销毁观察、target 版本入口指针等价、SDK 内 ingress 前事件身份、callback 排空/静默、设备与双板、DDS SPDP/SEDP/样本、同名 Participant 热重建与 RMW 仍为 `NOT_RUN`；生产 `init()` 继续 `NativeBackendUnavailable`。PF-01/PF-02 不被本切片闭合。

### 18.6 执行状态（2026-09-24 CST）

上述合同已在有界模型内实现并通过限定 host 验证：`include/mdds/dsoftbus/ServiceRecordTable.hpp`、`src/ServiceRecordTable.cpp`、`tests/service_record_table_tests.cpp`，根 `CMakeLists.txt` 以 `BUILD_DSOFTBUS_SERVICE_RECORD_TABLE`（默认 ON、只链接 `Threads::Threads`）接线 15 个测试。

| 检查 | 结果 |
| --- | --- |
| RED（stub，与 GREEN 同一 test/header 字节） | `0% tests passed, 15 tests failed out of 15`（行为失败；同轮的链接失败尝试作废、不计为 RED） |
| GREEN Release / Debug | `15/15`、`15/15` |
| standalone 全量（全部目标已构建） | `43/43` |
| 仓库标准 CONFIG host Release 全量 | `258/258`（需 `.artifacts/install/host/bin` 在 PATH） |
| `/W4` 构建与 header 自包含编译 | 无告警（`CMAKE_CXX_FLAGS:STRING=/W4` 已在 cache 核对；此前一次经 POSIX shell 传参被 MSYS 路径转换，未真正生效） |
| 重复运行 | 多轮 2–7 个 case × 20–25 次（含并发 case），无失败；具体配置以收据记录的执行为准，不作为固定矩阵声明 |
| 变异被杀 | 删除 reservation 暴露守卫 → `unexposed` 失败；reentrant 释放状态预期写错 → `reentrant` 失败；异常路径漏减 in-flight → `reentrant` 失败（三者在对应修复前均存活） |
| 产品字节与复核绑定 | 第 3 轮验证的 header/impl 哈希（`c8c6d696…` / `9c7bfd57…`）与提交字节一致；该轮之后仅测试文件改动 |
| 句柄所有权边界 | 新增断言覆盖 reservation 的两条方向（从未暴露→归还、已暴露→不回收）与覆盖赋值；删除 RAII 暴露守卫的变异会使 `unexposed` 失败 |

收据、文件哈希、三处测试预期更正（`Expose` 检查顺序、`capacity` 未认领 exposure 仍是 anchor、`handles` 释放后为 `UNANCHORED`）与各状态可达性核对见 `.artifacts/srt1/README.md`。每个 `RecordStatus` 值都由已实现规则产出并被某个 case 断言，唯一例外是作为"未设置默认值"的 `NotImplemented`。§18.5 的声明边界不变：真实 SDK service record 生命周期、target 入口等价、ingress 前身份、callback 排空、设备/双板、DDS、热重建与 RMW 仍为 `NOT_RUN`。

## 19. SRT-C 共享表与 native owner 的组合（2026-09-25 CST；IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED，限定范围）

本节是工作包 `SERVICE_RECORD_AND_NATIVE_OWNER_COMPOSITION_HOST` 的冻结契约与执行状态。起始提交 `145d0ee97de5e8728a985550da82f169c640d89e`；本轮**未提交**，状态为 `CHECKPOINT_READY / NOT_COMMITTED`。

### 19.1 交付物

- `include/mdds/dsoftbus/ServiceRecordBinding.hpp`、`src/ServiceRecordBinding.cpp`：最薄的组合层，组合三个既有组件，不新增第四个状态真相、不新增通用 registry。记录级的分发控制对象（record 的 sink 与 `(role, socket)` 路由）与 `(application, local)` 共享注册都按 **record 槽位** 存放在进程生命周期的有界存储里，binding 只登记/撤销**自己**的那一份。
- `ServiceRecordTable`：新增 `ReleaseSink(entrance, sink)`，在记录已 `UNANCHORED` 时解除该 record 的 sink 登记；`RecordReservation`/`RecordEntrance` 对组合层开放槽位，作为记录级键。
- `tests/service_record_binding_tests.cpp`：21 个组合 case（原 14 + 寿命/并发/上下文三个回归 + 独立复核确认的三个回归 + `accepted` 拓扑一个），每 case 独立进程；根 `CMakeLists.txt` 新增 `BUILD_DSOFTBUS_SERVICE_RECORD_BINDING`（默认跟随 `BUILD_DSOFTBUS_DIRECTORY`，并要求 `BUILD_DSOFTBUS_SERVICE_RECORD_TABLE` 与 `BUILD_DSOFTBUS_PROCESS`）与一对投递句柄封装的编译正/负控制（`tests/delivery_context_legal.cpp`、`tests/delivery_context_bypass.cpp`，configure 期 compile-only 检查）。
- `src/ProcessReceiveInternal.hpp`、`src/ProcessReceivePath.cpp`：投递上下文挂在**执行对象**上（`ProcessReceiveExecution` 的 enter/exit 钩子），因此任何入口（含直接 `Run()`）都会进入上下文，而不是只在组合层的 `RunAdmitted` 里标记。

### 19.2 身份与作用域（谁拥有什么）

| 身份 | 拥有者 | 作用域 |
| --- | --- | --- |
| record 实例、client/server role、固定 callback entrance | `ServiceRecordTable` | 进程 |
| 该 record 下的 socket membership | `ServiceRecordTable` | 进程 |
| **该 record 的 ingress 分发控制**（record 自己的 sink + 每条 `(role, socket)` 路由） | `ServiceRecordBinding` 的组合层，但对象本身归 **record**：按 record 槽位存放在进程生命周期、永不析构的有界存储中 | record |
| native owner / generation / 调用票据 / native 身份 pin | `NativeOwnerTable` | 表实例 |
| application claim / receiver registration / 有界 payload 队列 | `ApplicationAttachments` | 表实例，per (application, local) |
| **该 record 每个 local 的共享 `(application, local)` 注册**（claim + receiver registration 的生命周期与结算责任） | 记录级的 `ApplicationGroup`，由使用它的 binding 共同持有；**最后一个**使用者结算 | record × local |
| (record, role, entrance) ↔ (native plan) ↔ 该 local 的共享注册 | `ServiceRecordBinding` | binding |

**一次成功准入固定的四类身份**：表在自身门内固定 `(record incarnation, role, membership incarnation, socket)`；sink 在表门已释放后取 `AdmitCallback(socket)` → `CaptureIdentity()` 固定 native owner/generation；再在同一个 `ProcessReceiveBridge::Admit` 调用内用该 pin 捕获 application/receiver lease。之后交付只对**原始**身份复核（`TryEnter`），永不重新查表改投。

**准入的寿命**：分发控制对目标 binding 只持一个 `weak_ptr` pin；拿到 pin 的那次准入在整个准入期间持有它，所以目标 binding 即使并发释放也不会被读到已释放状态——它会在自己的相位门上拒绝。控制对象本身在进程生命周期存储里，永远不会悬垂。

### 19.3 不可豁免的合同

| 合同 | 固定行为 |
| --- | --- |
| 单一入口 | 一个 record 一个 sink；按 `(role, socket)` 路由到拥有该 socket 的 binding，路由容量 = record owner 容量。 |
| 三拓扑 | create（reserve + 暴露两个 role）、join（复用已发出入口，不占 slot）、accepted（`ObserveInbound` 采纳 accepted socket）。 |
| 共享粒度 | application/receiver 注册 per (application, local)，且**归 record**：同 local 的 joiner 采用同一份注册，创建者释放只是交还自己的那一份引用，最后一个使用者负责关闭并 detach。任何 release 都不写另一个 binding 的字段。 |
| 记录级分发 | 一个 record 一个 sink 与一张路由表，属 record 而非创建者：创建者被释放并销毁后，joiner 仍由该 record 继续服务。binding 释放只撤自己的路由。 |
| sink 寿命 | record 的 sink 登记在记录**无 anchor** 时解除（`ReleaseSink` 只接受 `UNANCHORED`，此前提与 retirement 同源，且不可逆：unanchored 的 record 不再接纳新 owner）。解除后新回调不再进入该 sink；已捕获的回调不回收。承载 sink 的控制对象是进程生命周期的有界存储，永不释放，因此登记解除前后都不会悬垂。 |
| 入口寿命 | 固定 entrance 属于 record，不随 binding 释放失效；释放后 `Entrance()` 仍返回该指针（SDK 可能仍持有），retirement 也用它。 |
| 关闭分层 | `BeginClose` 只关闭**本 binding 的逻辑准入**（新 ingress 拒绝）并保留已准入工作；native owner 的关闭由释放路径 claim 一次；`DrainComplete` 由表的 `callbacks_in_flight`、native owner 的 in-flight 与 attachments 的队列/复制/投递计数判定。 |
| 四个前提分开 | logical admission closed / adapter in-flight drained / native operation completion premise（仅票据结算）/ record destruction premise（仅显式模型边界，`RetireRecord` 要求已关闭且已排空）。Host 不声称 SDK 销毁回执。 |
| 失败回滚 | 暴露前失败回收 slot；暴露后失败保留 slot 且不重绑入口（两个 role 的 pending 暴露一并放弃）；发布失败释放 membership/共享注册（claim 由该共享注册 detach，绝不丢句柄）；已取得的 socket 关闭责任恰好 claim 一次。 |
| 回调边界 | 从 ingress 或投递回调内调用控制操作被**明确拒绝**（`WrongState`），不在回调内等待或死锁。投递上下文挂在**执行对象**上由组合层在发出时装载，所以任何入口都进入同一上下文；组合层只交出 `BindingDelivery`，它没有自己的执行入口，且只被发出它的 binding 接受。观察接口只报告状态，不交出可调用入口。 |
| fail-closed | 组件不调用 SDK、不建线程；provider 由调用者实现（Host 测试 / 未来 backend），返回值是模型输入。`DSoftBusTransport::init` 仍 `NativeBackendUnavailable`。 |
| 回调内销毁（HUMAN_REQUIRED） | 从 ingress 或投递回调内**销毁** binding 是头文件已声明的合同禁止误用，且无法像 `BeginClose`/`Abandon` 那样拒绝：析构必须结算它持有的注册，而结算会等待**本线程自己**持有的在途许可（`InputChannels` 的 in-flight permit），因此会挂起（stderr 有报告行）。本轮**保持**这一行为而不是改成拒绝或静默跳过：拒绝/跳过会永久搁置 claim 与 local（违反 §19.6「必须归零」并让该 local 不可再 claim），而「延后回收器」是需要独立设计与独立复核的新机制，超出本轮最小修复范围。因此这是一项需要人类决定的范围问题（HUMAN_REQUIRED），方案与证据见 `.artifacts/srtc-repair/README.md` §7。 |

### 19.4 必测矩阵

| 指令条目 | case | 覆盖 |
| --- | --- | --- |
| C1 双 owner 同 record | `two-owners` | 一个 slot、一个 incarnation、一个入口；A 关闭后 B 继续准入与投递；A 的迟到指针为终态丢弃 |
| C2 跨 role | `cross-role` | 空 role 保留 incarnation 与入口；另一 role 继续存活；retirement 只在显式边界且另一 role 仍活时被拒 |
| C3 部分准备失败 | `partial-failure` | 同名 create 拒于 record reserve；未知 route 拒于 owner reserve 且 slot 不回收、名字仍封印；同 local 第二 claim 被拒；无 receiver 时发布被拒并完整回滚 |
| C4 关闭与捕获 | `close-vs-capture` | 关闭后新 ingress 在组合边界被拒（表仍归属）；关闭前已准入工作仍以原 owner/receiver 完成 |
| C5 已准入未运行 | `admitted-not-run` | `queued>0` 且 `delivering==0` 时 drain 未完成；retirement 拒绝未关闭的 binding；关闭后排队工作仍可运行 |
| C6 关闭与运行中 receiver | `running-receiver` | 关闭与投递重叠（屏障为 receiver 自身）。**复核已修**：判定改用**独立于 receiver 本体**的事件计数，并在两次读数之间用真实入口做一次晚到 ingress；“不进入失效 receiver”因此可以失败。关闭后、释放前的真实 ingress 会被组合层拒绝（准入相位门），运行中的投递不被丢弃 |
| C7 关闭责任唯一 | `unique-close` | 两个 owner 两线程关闭：各自 socket 关闭恰好一次、重复释放为 settled |
| C8 回调边界重入 | `reentrant-boundary` | 投递回调内合法查询可用；控制调用被拒；拒绝后 binding 仍正常 |
| C9 异常准入 | `exceptional-admission` | 分配失败时不留下 in-flight、payload 或投递；**复核已修**：在真实路径的 attachments 上观测 `high_copying≥1`（证明确实发起过 copy 占用），失败后 `copying/bytes/taken/delivering` 全部回到基线；恢复后的准入按阶段断言 `queued/records/bytes/copying/delivering`；随后恢复正常 |
| C10 旧入口与新 incarnation | `old-entry-new-incarnation` | 显式退休后同名新 record 取新 slot/incarnation/入口；旧入口终态丢弃；新入口工作（排除“全拒绝”假保护）；释放后该实例无 active/detaching application 与 payload；后继使用新的 per-Participant 实例（native 表 + attachments），与 C11 同形 |
| C11 同进程重复生命周期 | `repeated-lifecycles` | 一个进程内 4 轮 acquire/expose/use/release/retire；**复核已修**：预算探针接在**该轮自己的** attachments 上（不再读未使用的 fixture 成员），每轮断言 charge（`queued=1/bytes=64`）与释放后归零；只有 slot 按设计递增 |
| C12 slot 耗尽 | `slot-exhaustion` | 需要新 incarnation 的创建在暴露前被拒且不触达 provider；活 record 的 role 复用不耗 slot |
| C13 sink/receiver/payload 释放 | `resource-release` | 释放后 membership/claim/receiver 均释放、payload 归零、attachments 不再持有 receiver；**新增**：最后一个 binding 释放后该 record 的 sink 登记已解除 |
| C14 fail-closed 默认路径 | `fail-closed` | provider 调用次数精确匹配；被拒部署不调用任何 provider；失败的 socket 不产生 shutdown |

**矩阵未覆盖（如实记录，不作为通过依据）**：`accepted` 拓扑已由 `accepted-topology` 覆盖（成功路径：`ObserveInbound` 采纳、`Connect` 短路、准入与投递、释放路径的 accepted socket shutdown，并断言该拓扑不产生 Socket/Listen 调用；三条拒绝分支：client role、负 socket id、未知 peer，各自断言失败状态与「未注册 membership」）；仍未覆盖的是`ingress_seen`/`correlation` 等诊断计数与 `refused_no_route` 同样无断言；没有用例让「某 binding 正在准入」与「同一 binding 的释放」重叠（`ReleaseLocal` 撤路由与释放 membership 之间的窗口）。这些是覆盖边界，不是已证缺陷。
| R-D 结算窗口内的 join | `join-during-settle` | 最后一个使用者已决定结算、尚未应用时，新来的 joiner 不得采用该注册（否则它会以 Published/`receiver_open` 报告一个正在关闭的 receiver，且同一 local 会出现第二份 application）；本地在结算完成后可再次 claim |
| R-E joiner 失败不得动创建者的暴露 | `joiner-failure-keeps-creator` | 只有**暴露过**该 role 的 binding 才可报告「该入口的注册尝试未产生 owner」；两个 role 上各有一个 joiner 在暴露后、发布前失败，record 必须仍是 `EXPOSED`、sink 仍在、两个 pending 仍在，创建者仍能 `Publish` |
| R-F 注册对象构造失败 | `registration-allocation-failure` | 已安装的 claim 与 receiver 已挂到 state 上，故构造共享注册对象时的分配失败由释放路径结算（在门之外）：`active/retained` 归零、同一 local 可再次 claim；重试句柄本身也被结算 |
| R-A 创建者先退出 | `creator-exits-joiner-lives` | 创建者按合法契约 close/release/销毁后，joiner 仍通过 record 自己的入口被准入与投递；创建者的 socket 已不是成员（迟到调用为终态丢弃）；最后一个 binding 释放后 record 无 sink 登记且为 `UNANCHORED` |
| R-B 共享注册并发释放 | `shared-group-release-race` | 同 record 同 local 的两个 binding 由两个线程释放，交错由 `before_group_settle` 调度点固定为“先释放者已决定、后释放者已整段完成”；断言共享 claim/receiver 恰好结算一次、local 可再次 claim。两个 binding 在断言时都仍存活，所以断言的是释放路径留下什么，不是析构偶然清掉什么 |
| R-C 投递句柄与上下文 | `delivery-context-bypass` | 组合层只交出 `BindingDelivery`（无自己的执行入口）；同一记录的另一 binding 拒绝运行它、moved-from 句柄被拒、同一 delivery 只运行一次；合法投递内控制调用被拒、查询可用；编译期负控制（`tests/delivery_context_bypass.cpp`）与同环境合法正控制（`tests/delivery_context_legal.cpp`）在 configure 期以 compile-only 检查，失败必须是缺成员（MSVC C2039）而非缺头文件或链接错误 |

### 19.5 执行状态（2026-09-25 CST，SRT-MEM 修复轮后）

本节记录**当前**状态。上一轮（SRT-C 组合轮）的执行状态与当时未闭合的 10 项发现原样保留在 [.artifacts/srtc/README.md](.artifacts/srtc/README.md)、[.artifacts/srtc/ISSUE_TABLE.md](.artifacts/srtc/ISSUE_TABLE.md) 与三份只读复核原件；本轮对每一项的关闭依据见 [.artifacts/srtc-repair/](.artifacts/srtc-repair/)。

| 检查 | 结果 |
| --- | --- |
| 组合 case（`SOURCE_PROBE` Release `/W4`，每 case 独立进程） | **21/21 PASS**（原 14 + `creator-exits-joiner-lives`、`shared-group-release-race`、`delivery-context-bypass`，加独立复核确认后新增的 `join-during-settle`、`joiner-failure-keeps-creator`、`registration-allocation-failure`，以及 `accepted-topology`） |
| 本配置全量（`srtc` Release，含既有全部模型与探针） | **75/75 PASS** |
| 仓库标准 CONFIG host Release 全量 | **279/279 PASS** |
| 组合/表 case（Debug `/W4`） | **21/21**、**15/15 PASS** |
| 组合/表 case（AddressSanitizer，`RelWithDebInfo`） | **21/21**、**15/15 PASS**，无 sanitizer 报告；未修复树在同一 case 上被 ASan 报为 heap-use-after-free |
| 告警 | `/W4` 已在各 cache 核对；本轮改动的目标（binding/table 库与两个测试目标）**0 告警**；全树强制重建下另有 **7 条**（31 行编译器输出）告警，全部落在五个**既有的、与本轮无关**的测试目标中，如实记录 |
| 编译期封装检查 | 四个构建目录 configure 均通过：合法调用点可编译、绕过调用点因**缺成员**失败；人为给投递句柄加 `Run()` 时该检查会失败 |
| 变异验证 | 16 个变异体：**15 个被对应用例杀死**（“恢复创建者裸 sink 寿命”“共享注册从不被最后一个使用者结算”“不装载投递上下文”“漏减 copying/bytes”“错误 binding 运行他人的 delivery”“删除 drain”“已安装 claim 未挂到 state”“删除准入相位门”“record 仍有 anchor 就解绑 sink”“结算窗口内仍可被采用”“joiner 替创建者放弃暴露”“孤立已安装 claim 被丢弃”“accepted 请求按普通 plan 部署”，以及编译期的“投递句柄暴露执行入口”）；1 个（M8b：把 C11 探针换回**未使用**的 fixture 成员）按设计**存活**，这正是“失效判据曾经看不见该泄漏”的证据。运行器对每个变异体校验替换是否真的改动了文件，未改动记为 `NOT_APPLIED` 而不是伪造一行结果 |
| 投递上下文 | 组合层不再交出可执行对象；`Run()` 的绕过路径在类型层面不存在，且上下文挂在被准入的执行对象上。封装性由**每次构建都编译**的静态断言（`tests/delivery_context_legal.cpp`）以及 configure 期的编译正/负控制共同保证，后者还要求失败原因是缺成员而非缺头文件或链接错误 |
| sanitizer 范围 | ASan 可用且有正/负控制；TSan 与本仓库支持的 Host 工具链（Windows/MSVC）不兼容，记 `TOOL_UNAVAILABLE` —— 数据竞争自由由锁纪律与逐字段复核承担，**不由 TSan 报告证明** |

**范围边界（明确未断言）**：真实 SDK provider、目标入口指针等价、adapter 之前的原生事件身份、callback 排空与静默、设备与跨设备、DDS SPDP/SEDP/sample、同名 Participant 热重建、RMW 全部 `NOT_RUN`。`DSoftBusTransport::init` 仍 `NativeBackendUnavailable`；P0、PF-01/PF-02、P4 与 QualificationProfile 状态不变。

### 19.6 资源预算与保留责任

| 资源 | 容量 | 归属/释放 | 耗尽行为 |
| --- | --- | --- | --- |
| record slot | 进程 8 个，暴露后永久 tombstone | 只有从未暴露的 reservation 归还 FREE；显式退休不回收 | `ReserveRecord` → `SlotExhausted`（暴露前） |
| record 元数据 | 每 slot：name ≤255 B + 12 个 owner 单元 + 固定计数（<1 KiB） | 进程生命周期，无动态业务对象 | — |
| ingress 路由 | 每 record 12 条（= owner 容量） | binding 发布时加、释放时移除 | `Publish` → `AdmissionSaturated` 并回滚 |
| native owner | 每表 ≤12（`total_owners`） | 外部创建后不复用；关闭责任经 shutdown 票据 claim 一次 | `Reserve*` → `AdmissionSaturated` |
| payload 队列 | ≤64 records / 512 KiB / copying ≤8 / 单条 ≤60000 B | attachments 预算；工作被取走或释放即归还 | 准入 → `Capacity`/`TooLarge` |
| application claim / receiver 注册 | per **(record, local)**，同 record 同 local 只有一份，≤ record owner 容量 | **必须归零**：最后一个使用者关闭 receiver 并 detach（C13/C11 断言） | 第二 claim → `ExistingPending` |
| in-flight 计数 | 表 `callbacks_in_flight`、owner in-flight、attachments copying/delivering | **必须归零**（C9/C13 断言） | — |
| sink 与 ingress 路由 | 每 record 一份**固定**分发控制（sink + ≤12 条路由），按 record 槽位存放在进程生命周期、永不析构的存储里。它不含 payload、claim、receiver 或任何动态业务对象，容量与 record 槽位同阶（≤8） | 路由由 binding 发布时加、释放时撤；sink **登记**在 record 无 anchor 时由 `ReleaseSink` 解除。对象本身不释放——record 表在 record 暴露期间一直持有该地址，地址必须永不悬垂 | 路由满 → `Publish` → `AdmissionSaturated` 并回滚 |

故意保留：tombstoned slot、进程生命周期状态（不析构）、暴露后的 slot。必须归零：payload、application/receiver 注册、各 in-flight 计数、binding 的 membership/路由。重复失败是否消耗 slot：**失败于暴露前不消耗，暴露后永久消耗**（fail-closed，按冻结合同）。

### 19.7 SRT-MEM 修复轮的合同修订（2026-09-25 CST；工作包 `SRT_MEMORY_SAFETY_AND_TEST_ORACLE_REPAIR`）

本轮只修复复核已确认的寿命、并发、投递上下文与测试判据缺陷，并**先让每个实际问题进入可重复回归**。对 §19.2/19.3/19.6 的修订仅限内存安全范围，且不降低任何核心保证（创建者退出/joiner 存活、old lease 不重新归属、no-app 终结都只被加强）：

| 修订 | 原 | 现 |
| --- | --- | --- |
| sink 与 ingress 路由的归属 | 创建该 record 的 binding 的 `State`，且该 binding「必须活过暴露期」 | **record 自己**：按 record 槽位存放在进程生命周期的有界存储里，binding 只登记/撤销自己那一份 |
| 同 local 的共享注册 | 创建者持有，释放时把字段写进另一个 binding 移交给它 | **引用计数**：每个使用者各持一份引用，最后一个使用者结算；任何 release 都不写别人的字段 |
| joiner 的前置条件 | 创建者「必须活过每个 joiner」 | `record_owner` 只是 joiner 的**非空声明**（不读它的任何状态）；路由与共享注册都从 record 自身的槽位取得，创建者被释放并销毁后 joiner 仍被服务 |
| 投递 API | `NextAdmitted()` 交出公开的 `ProcessReceiveInvocation`（其 `Run()` 可绕过上下文） | 交出 `BindingDelivery`：没有自己的执行入口，且只被发出它的 binding 接受 |
| 投递上下文 | 只在组合层 `RunAdmitted` 内标记 | 挂在被准入的**执行对象**上，任何入口（含直接 `Run()`）都进入同一上下文 |
| record 表的 sink 登记 | 只能由 `ConfirmDestroyed` 清空 | 新增 `ReleaseSink(entrance, sink)`：只在 record 无 anchor（`UNANCHORED`）时解除，与 retirement 同源、不可逆 |

**本轮可复核的不变量**（逐条对应上面的实现与回归）：

| 不变量 | 内容 | 复核入口 |
| --- | --- | --- |
| I1 | record 的 sink 地址在 record 暴露期间恒定，且永不悬垂（进程生命周期的有界存储，永不析构） | `creator-exits-joiner-lives`（AddressSanitizer 下无 UAF）、`resource-release`、`repeated-lifecycles` 的 `sink_attached` 断言 |
| I2 | record 的每条路由只持有目标 binding `State` 的 **weak pin**；一次准入持有该 pin 直到返回，所以并发释放不会让准入读到已释放状态 | `creator-exits-joiner-lives`、`unique-close`、ASan 下的并发压力。**未覆盖**：`refused_no_route`（表已归属但控制无该 `(role, socket)` 路由）只可能出现在「路由已撤、membership 未撤」的瞬时窗口，没有用例能确定性地到达它，因此它只是诊断计数，不作为断言依据 |
| I3 | 一个 binding 的可变字段只在其 `State::gate` 下读写；锁序为 `State::gate` → {record 表 / native 表 / attachments / record control}，没有任何路径反向取锁 | 逐字段清单见本轮收据；`unique-close` / `shared-group-release-race` 覆盖并发释放 |
| I4 | 可能在 attachments 上等待的步骤（`CloseReceiver`、`AwaitDetached`）与 provider shutdown 都在 `State::gate` 之外执行 | `running-receiver`（释放与运行中 receiver 重叠）、`partial-failure` |
| I5 | 投递上下文由被准入的执行对象在 `Run()` 内进入/退出，与调用者走哪个入口无关 | `delivery-context-bypass` + compile-only 正/负控制 |
| I6 | record 的 sink 登记只在 `UNANCHORED` 时解除，此后不会重新登记（unanchored 的 record 不再接纳 owner）；且只有**暴露过**该 role 的 binding 才能报告暴露放弃，所以 joiner 的失败不会替创建者解除 sink | 变异 M10/M12；`two-owners`/`cross-role`/`slot-exhaustion`/`joiner-failure-keeps-creator` |
| I7 | 一个 `(record, local)` 注册的「最后一个成员决定结算」与「新成员加入」在**同一临界区内**定序：一旦决定结算，任何 join 都不能再成功 | 变异 M11；`join-during-settle` |
| I8 | 已安装但尚未成为共享注册的 claim/receiver 由 state 持有，因此该窗口内的任何失败（含分配失败）都由释放路径在门之外结算 | 变异 M13；`registration-allocation-failure` |
| I9 | 组合层的记录级键是 record 槽位，而「暴露过的槽位永不回收」是 record 表的不变量（`ReclaimReservation` 只回收从未暴露的预留）；`Prepare` 校验槽位范围，越界即失败而不是别名到别的 control | 变异（越界槽位）不可构造；`old-entry-new-incarnation`/`repeated-lifecycles` 断言新 record 取新槽位 |

**本节之外的已知边界（如实记录，不报为已闭合）**：① 回调内销毁会挂起（上一行，HUMAN_REQUIRED）；② `accepted` 拓扑的三条拒绝分支现已覆盖，但其成功路径只有一条夹具形状；**新记录**：joiner 依赖**采纳方**的 `ApplicationAttachments` 实例仍然存活（`ApplicationGroup::attachments` 是裸指针）——若该实例被销毁而采纳它的 binding 仍在服务，行为未定义；本组合的契约要求该实例在所有采纳者之前保持存活（见 §19.6 的 per-Participant 设计单位），Host 用例（含 `creator-exits-joiner-lives`）都只用同一个实例，因此没有覆盖这条前提；③ `Observe()` 读取 attachments 用量时**不在** `State::gate` 内（报告指针本身已在门内捕获）；诊断视图允许瞬时不一致，`State` 自己的字段仍在门内读取；④ `RecordControl::RegisterGroup` 在数组满时静默丢弃（当前不可达：两张有界表的键空间同阶）；⑤ `ReleaseSink` 的返回值在调用处被丢弃（结果不可行动，仅诊断损失）。

**本轮的检测工具范围**：AddressSanitizer（MSVC `/fsanitize=address`）可用，并有同环境、同开关的正/负控制；ThreadSanitizer 与本仓库支持的 Host 工具链（Windows/MSVC）不兼容，记为 `TOOL_UNAVAILABLE`，因此**数据竞争自由由锁纪律与逐字段复核保证，不由 TSan 报告证明**。Sanitizer 无报告不等于全部时序安全。

## 20. CALLBACK_CONTEXT_DESTROY_CONTRACT（2026-09-25 CST；IMPLEMENTED / BUILDS / HOST_TESTED，限定 Host 范围）

本节是工作包 `CALLBACK_CONTEXT_DESTROY_CONTRACT` 的合同与执行状态，关闭 §19.7 记录的那一项 HUMAN_REQUIRED：**在回调执行上下文内销毁 binding 会自等待**（释放必须等待本帧持有的 receiver in-flight 许可）。本轮采用**方案 A：禁止回调内同步销毁，并提供不等候的关闭请求**；不新增 GC/reclaimer、不改 provider/SDK、不加设备测试、不加新的 Runtime 单例。起始提交 `145d0ee`；本轮**未提交**。

### 20.1 回调上下文的关闭/销毁合同

| 入口 | 合法上下文 | 行为 | 拒绝/重复时 |
| --- | --- | --- | --- |
| `CloseRequest()` | **任意上下文，含 ingress/投递回调** | 记录关闭意图（`phase = Closing`）后立即返回；不 drain、不释放、不等待 | 已有意图 → `ServiceClosing`；未发布 → `WrongState` |
| `DeferredClose()` | 仅控制上下文 | 完成进行中的关闭：与 `Abandon()` 同一条 drain+释放路径，然后标记 `Closed` | 回调内或无进行中的关闭 → `WrongState`；二次完成 → `AlreadySettled` |
| `BeginClose()` / `Abandon()` | 仅控制上下文 | 语义不变 | 回调内 → `WrongState`（明确拒绝，绝不等候） |
| `~ServiceRecordBinding()` | 仅控制上下文 | 释放语义不变 | 在本组合的回调栈上被调用时 **fail-fast**（带指向 `CloseRequest()` 的诊断后 abort），不再自等待 |

**为什么是 fail-fast**：析构无法“拒绝”，而另外两条路各有代价——拒绝并搁置注册会让 claim 与 local 永久不可用（违反 §19.6「必须归零」）；延后回收器是需要独立设计与评审的新机制，超出本轮授权。诊断文本直接点名合法路径，因此触发者能就地改正。

**不变的四条寿命前提**：§19.7 的 I1（sink 登记只在 `UNANCHORED` 解除）、native owner lease、receiver lease 的 drain-before-free、以及 claim 恰好 detach 一次；关闭请求路径与同步路径走同一个 `ReleaseLocalLocked()`，未跳过任何一步。

### 20.2 必测矩阵（新增 8 个 case，全部已注册）

| 指令条目 | case | 覆盖 |
| --- | --- | --- |
| CLOSE-C1 回调内请求关闭 | `close-request-from-callback` | 请求返回 `None`、二次请求 `ServiceClosing`、相位转 `Closing`、无遗留 in-flight、新 ingress 被拒且不入队 |
| CLOSE-C2 回调内同步关闭 | `close-sync-refused-in-callback` | `BeginClose`/`Abandon`/`DeferredClose` 均 `WrongState` 且不阻塞；binding 仍 Published 且仍准入；随后控制上下文仍可正常关闭 |
| CLOSE-C3 外部关闭 | `close-completes-outside-callback` | 所有者完成请求：`Closed`、membership/claim/receiver 释放、无 in-flight、provider 关闭恰好一次、二次完成 `AlreadySettled` 且不重复关闭 |
| CLOSE-C4 回调异常后关闭 | `close-after-callback-exception` | 抛异常的 receiver 回调被报为 `CallbackFailure`（不外传）、意图在异常前已记录、关闭仍能完成且不留残留 |
| CLOSE-C5 多回调并发请求 | `many-close-requests-one-close` | 一个来自投递回调 + 四个来自控制线程：恰好一次 `None`、四次 `ServiceClosing`；请求本身不产生关闭；最终恰好一次关闭 |
| CLOSE-C6 请求后新准入 | `no-admission-after-close-request` | 请求前已准入的两个投递仍全部运行并到达 receiver；新 ingress 被拒且不入队 |
| CLOSE-C7 关闭后资源 | `close-request-releases-resources` | sink 登记释放、receiver 注册与 claim 释放（local 可再次 claim）、owner lease 释放且关闭恰好一次 |
| 禁止形状 | `callback-destroy-fails-fast`（子 case `callback-destroy-child` 故意不注册进 CTest） | 子进程**停止**而不是死锁；同一意图的合法形状（回调内请求、所有者销毁）完成且只产生一次关闭 |

### 20.3 执行状态（2026-09-25 CST）

| 检查 | 结果 |
| --- | --- |
| RED（改动前，见 `.artifacts/srtc-close/RED-EVIDENCE.md`） | ① 回调内请求关闭：`status=19 expected=0`（今天回调无任何入口可记录关闭意图，binding 仍 Published 并继续准入）；② 回调内销毁：打印诊断后**挂起**，20s 有界超时 rc=124 |
| 组合 case（Release `/W4`，每 case 独立进程） | **32/32 PASS**（§19.4 的 21 个 + 本轮 11 个） |
| 本配置全量（`srtc` Release） | **86/86 PASS** |
| 仓库标准 CONFIG host Release 全量 | **290/290 PASS** |
| Debug / AddressSanitizer | **32/32**、**32/32 PASS**，无 sanitizer 报告 |
| 变异验证 | 本轮 11 个变异体（M16 请求执行同步关闭、M17 回调内拒绝请求、M18 请求不关闭准入、M19 完成不释放、M20 取消 fail-fast、M21 析构改为 return、M22 完成路径丢守卫、M23 守卫退回线程全局、M24 同步关闭/释放丢回调守卫、M25 完成拷贝不计 queued、M26 守卫忽略“本次释放是否真的结算”）**全部被对应用例杀死**；累计 27 个变异体中 26 个被杀死、1 个（M8b）按设计存活 |

### 20.4 边界

真实 SDK provider、目标 ABI 等价、callback 排空、设备与 DDS、热重建与 RMW 仍 `NOT_RUN`；`DSoftBusTransport::init` 仍 `NativeBackendUnavailable`；P0/PF/P4/QualificationProfile 不变；组合层仍无生产调用方。本轮记录的边界：① fail-fast 只能跨进程观测（由子进程 case 固定，父进程同时要求退出码、契约诊断与“销毁回调未返回”标记），其等待由每 case 的 CTest 超时与运行器上下界约束；② 守卫的身份由**被投递工作自身的执行**给出（拥有它的 attachments 实例 + 该投递的 application 身份），因此在结构上覆盖了“同一 attachments 队列上另一个注册的工作被本帧取走”这一形状，但该形状**没有**独立用例：要在 Host 里驱动它需要一个第二 local 及其 dial 路由进入共享夹具的目录，而组件的方向校验会拒绝该配置。若要固定它，可选 **B**（把结算延后到之后某个非回调入口——本轮工作包禁止的新机制）或 **C**（在 attachments 上开放注册身份访问器，使用例无需改共享夹具即可构造该形状）；③ 部署阶段内的关闭请求、以及请求与另一线程释放的竞争未覆盖；④ 已请求但无人完成的关闭会让 binding 停在 closing 相位并持有注册——这是所有者的义务（头文件、本节与收据均已声明，`Observe()` 报告相位），但没有强制。

## 21. SHARED_LIFECYCLE_R8_CLOSEOUT_AND_CHECKPOINT（2026-09-28 CST；IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED，限定实验范围；COMMITTED_LOCAL_NOT_PUSHED）

### 21.1 目标与交付物

在一个进程内让两个真实 Fast DDS DomainParticipant 共享同一套 DSoftBus runtime，并验证两种创建者顺序下的单轮热重建。交付物：`include/mdds/dsoftbus/{ApplicationAttachments,HostProviderLoop,ProviderAdapter,ServiceRecordBinding}.hpp`、`src/` 同名的四个实现、`cmake/PublicSocketDdsExperiment.cmake`、`CMakeLists.txt` 的接线，以及新增的 `probes/PublicSocketSharedDds.inc`、`tests/{PublicSocketDdsTestSupport.hpp,host_shared_channel_tests.cpp,public_socket_shared_dds_tests.cpp,public_socket_shared_analysis_tests.py}`、`scripts/public_socket/analyze_shared.py`。累计相对 `400e4ff` 为 16 修改 + 6 新增，共 22 个路径，无 gitlink。

所有权：`HostProviderLoop::State` 持有 backend、native owner table、ApplicationAttachments、NativeSendExecution 与唯一的 ProviderAdapter；每个 façade 是一个 `View`，只持有自己的 egress registration、`BindingInput`、receiver 指针与 `ReceiverIdentity`。创建者本身也是一个 View，不是 native 寿命的所有者；新 façade 只能通过活 runtime 上的 `Join()` 取得，因此合法共享不能来自同名服务查找。

### 21.2 不可豁免的合同

1. **准入时固定接收集合**：`State::Admit` 在 record 自身 gate 下把当时合格的注册固定成只含弱句柄的 `CandidateSet`；之后新建的注册无法加入该集合，执行期也不做 current-application 回查。
2. **逐目标准入 pin**：只为该目标自己取 pin，紧邻它自己的 `AdmitOne`，由不可拷贝、只可移动的 `ApplicationGroup::Pin` 释放恰好一次。任一目标的关闭都不得等待本记录尚未到达的另一目标的工作。
3. **逐 View egress**：每个 View 持有自己的注册；关闭一个 View 只强撤销（`BeginRevoke` + `Progress`，即 `FreezeAdmissions` + 队列关闭 + barrier）它自己的注册，另一个 View 的注册保持可用。
4. **claim 互斥**：同一 local 上 shared claim 与 exclusive claim 不得并存，方向对称；拒绝不得改动持有者的身份、预算或寿命。
5. **关闭与回滚**：被拒绝的关闭要发布确定状态并唤醒等待者，保留 receiver/claim/成员资格；不得伪装 Closed，不得释放仍在使用的资源。
6. **pump 权限单一规则**：`Start` 从未成功的 View 不得驱动共享 runtime；已启动的 View 关闭后仍保留该权限，因此存活 Participant 与最终 drain 不会失去唯一推进者。
7. **失败要响亮**：在无法关闭输入的上下文中释放成员 View 时快速失败，并命名合法替代做法；共享输入释放同理。

### 21.3 必测矩阵

| 条目 | case | 覆盖 |
| --- | --- | --- |
| 两个真实 façade 共享一个 runtime | `shared`、`reattach`、`concurrent-first` | 一个 native owner、各自 receiver、重复 open 身份不变、新建 P 的全新 application/receiver/token 世代 |
| creator 退出、Q 存活 | `creator`、`controller-released`、`queued`、`running`、`sending` | creator 销毁不关 Q 的 owner，Q 的收发继续，P 的在途/排队工作按原对象结算 |
| joiner 退出、Q 仍工作 | `foreign`、`foreign-peer-records`、`foreign-peer-bytes`、`foreign-global-records`、`foreign-global-bytes`、`foreign-record-bound-legal` | 六个比较字段逐个单字段负例 + 记录边界本身合法的正控制 |
| 捕获粒度与寿命 | `pinned-close`、`own-lease`、`candidate-order`、`capture-accounting` | 较早目标暂停时 joiner 关闭**必须完成**；关闭者**自己的**关闭在自己的拷贝计费时**不得返回**；无首目标特权；200 轮含关闭重开的精确计数 |
| 逐 View egress | `egress-settled`、`egress-queued`、`egress-old-resource` | 强撤销与 barrier 可观察、queue 关闭且为空、旧 sender resource 被拒且不新建 ticket、反复关闭不重复结算 |
| pump 权限 | `pump-authority` | `Start` 失败的 View 两个方向泵都被拒；已启动并关闭的 View 仍保留权限 |
| 预算分层 | `budget-two-views`、`budget-ceiling` | 两个注册各自驱动；第二个注册被 native ceiling 在暴露前拒绝且不留成员与 claim |
| shared/exclusive | `AA-09-mixed-shared-exclusive-claims` | 双向拒绝、拒绝不改身份与预算、真实模型接收正控制、合法释放后可再 claim、不同 local 不过度拒绝、多共享成员下仍拒独占 |
| 分析器 text 入口 | `public_socket_shared_analysis_tests.py` 的 9 个真实文本用例 | 用真实日志的独立副本驱动 `analyze(case, texts)`；正例仍通过，删/重复/错 run/writer/life/phase/缺字段/结构非法目标行必须失败 |

### 21.4 执行状态（2026-09-28 CST）

| 检查 | 结果 |
| --- | --- |
| 完整 CONFIG Host（最终候选） | **484/484 PASS**，0 fail / 0 err / 0 skip，224.4 s，exit 0 |
| 受影响集 ASan | **121/121 PASS**（预构建 Fast DDS 未插桩，TSan 未执行） |
| 独立变异 | **13 个全部被杀死，0 存活**；含 `g1-no-strong-revoke`（逐 View egress）、`exclusive-ignores-shared`（组合层）、`f1-bulk-pin`、`f1-pin-released-early`、`f4-*`、`f6-stale-identity`、`f7-ceiling-ignored` 及五个回归变异 |
| 分析器 | **36/36 PASS**，含 9 个真实文本边界用例；round-5 与 round-8 共四份设备日志 observed == expected == 30 |
| 设备 SHARED_W1（P 先建） | `256cce1c37aabcac` PASS；删除顺序 B→A；runner/controller/analyzer 全 PASS |
| 设备 SHARED_W2（Q 先建） | `372d9a20c0db57e9` PASS；删除顺序 A→B；同上 |
| 设备 F1M 回归 | `cb0a993d921f9c8b` PASS |
| 只读路由查询 | `query-r8` PASS，对端真实 local 精确匹配一次 |
| 两板 guard | 每个窗口：`final-guard Passed`、`byte_match`、`metadata_match`、`process_absent`、`service_permission=DENIED`、`first_failure=null`、`cleanup_failures=[]`，且每板 PID 全程不变 |
| Native 连续性 | P 删除与同名重建全程 `socket/listen/bind=2/1/1`、`shutdown=0`、entry 与 record/owner 世代不变；`FINAL` 时 `views=0` 且 `shutdown` 恰好 1 |
| 目标包静态审计 | AArch64、`$ORIGIN`、shared-reattach define 存在、DDS-testing define 不存在、六个禁用符号均不存在、五个公共 Socket 导入齐全、必需链接符号齐全、无未声明工程编译源、六个包成员且不含系统 DSoftBus DSO、授权 `trans_type.h` 哈希匹配 |
| 提交 | `7f40798`，parent `400e4ff`，tree `6948c5e`；恰好一个新 commit；分支不变；22 路径；无 gitlink；`git diff --cached --check` 通过；未 push/merge/amend；无 upstream |

独立 review 覆盖冻结修订 r7（回归、两道 review 视角与一个单问题 reviewer 均返回结论），其报出的两个真实缺陷与三个次要项已修复；修复 diff 另有一次定向复审，五个修复全部判为 correct，并报出 `R8-R1`/`R8-R2`。设备窗口在 review 完成并修复重测之后运行，但早于修复 diff 复审的返回，该时序偏差记为 `PROCEDURE_DEVIATION`，未回填为设备前批准。

### 21.5 边界与未验证项

- `R8-R1`、`R8-R2`：已知非阻塞限制，按各自可达性记录，未改运行时代码，也未扩大为「同一 guard 覆盖所有析构与错误路径」。
- 准入 pin 的**双重逻辑释放**无直接用例；RAII 使其只能靠改 pin 代码表达，本轮未运行对应隔离变异。
- `capture-accounting` 的阻塞关闭调用无自身 watchdog，挂起仅由 CTest 的 20 s 超时兜底；不能仅凭 ASan 无报告证明逻辑计数正确。
- 三个未跟踪文件（`tests/host_shared_channel_tests.cpp`、`scripts/public_socket/analyze_shared.py`、`tests/public_socket_shared_analysis_tests.py`）没有 freeze-r4 逐字节原件，其 r4→r8 谱系为 `NOT_ESTABLISHED`（其中测试文件有 PARTIAL 快照）；五个跟踪文件的 r4 原件可从 `freeze-r4/SOURCE.diff` 或 HEAD 复得，为 `ESTABLISHED`。
- `src/service_record_binding_tests.cpp` 是未跟踪的过期重复源文件，保留原样；已核实无任何 target 引用它，且 `CMakeLists.txt` 与 `cmake/*.cmake` 无 `file(GLOB)`/`GLOB_RECURSE`/`aux_source_directory`。
- Graph：`HostProviderLoop` 47 个上游依赖 risk `CRITICAL`、`ApplicationAttachments` 43 个 `HIGH`；`Affected processes: 0` 是建模缺口（准入经 `RecordIngressSink::OnAttributedBytes` 虚接口与 `weak_ptr::lock()` 到达扇出），不是无影响。
- 平台边界不变：SDK 物理记录 epoch 连续性 `UNPROVEN`；PF-02、P0/PF `BLOCKED`；P4 未放行；默认 `NativeBackendUnavailable`；`QualificationProfile` `NOT_GENERATED`；SDK drain/cancellation/original-event identity、完整 firmware ABI 未验证。

### 21.6 证据入口

工作包根 `.artifacts/public-socket-shared-reattach/run-20260927T113439Z/`：`claude-r8/FINAL_REPORT.md`、`claude-r8/CLOSEOUT.md`、`claude-r8/POST_COMMIT_VERIFICATION.json`、`claude-r8/FINAL_EVIDENCE_INDEX.json`、`claude-r8/{host-r1,asan-r1,mutations-r8,review}/`、`claude-r6/{ISSUE_CLOSURE.md,F7_BUDGET_LAYERING.md,GRAPH_COVERAGE.md,review/FINDINGS_VERBATIM.md,RETRY_EVIDENCE_R6.json}`、`DEVICE_CONTRACT-r8.json`、`CASE_MATRIX_R6.md`、`target-audit-r8/`、`freeze-r6|r7|r8/`、`windows/{query,w1,w2,f1m}-r8/`。round-5 的窗口、包与 receipt 保留为其自身历史身份，未被作废或复用。
## 22. NEW_PATH_EXACT_ONCE_AND_BOUNDED_REBUILD_REPETITION — FINAL BLOCKED STATE (2026-09-28 CST)

本节是当前工作包的 post-validation 文档 delta，发生在 r5 验证与设备运行结束之后。
它不改变 r4 C++/ASan、r5 两文件 Python-only 分析器/测试 delta 或既有 r8/r9 历史证据
的归属。r5 SOURCE390 SHA 是冻结快照；“其余 388 SOURCE 与 EXECUTOR25 无漂移”仅
表示 r4→r5 冻结比较，不宣称当前整树仍为 390/390 零漂移，也不把计划状态改写成执行结果。

| Gate | 当前证据状态 |
| --- | --- |
| `EXACT_ONCE_PIN_VALIDATION_CLOSED` | **PASS**；原 r9 exact-once pin guard 与定向 T-A 历史收据保留，最终 r5 host/target review 已覆盖 |
| `HOST_100_CYCLE_VERIFIED` | **PASS**；实际 Host100 formal CLI/replay 通过，3000 writes/takes；自然窗口无 cancellation，不替代 cancellation 专项用例 |
| r4 C++/Host full CONFIG | **PASS**；507/507，exit 0；首次 missing-target 与宽泛日志 extraction 失败原件保留 |
| r4 ASan | **PASS**；唯一并集 220（130+90，smoke 5 为重复子集）；预编译第三方 Fast DDS DLL 未完整插桩 |
| `r5 Python-only delta` | **PASS**；冻结 SOURCE390 SHA `09ba0a53238890acff824d597f5ad60ed03e0d771502eb5ceabf4384ed59ef1f`，仅两 Python 路径变化，其余 388 SOURCE 与 EXECUTOR25 无漂移（仅 r4→r5 比较） |
| r5 review | **PASS**；`review-fix-r5-host/REPORT.md` 与 `review-fix-r5-target/REPORT.md` 均闭合 R4-C1，无未解决代码 review blocker |
| Device N10 repair | **FAIL**；唯一尝试 `c1ed9706ef5b794f`，formal cycle analysis rc=1/pass=false |
| `NEW_PATH_100_CYCLE_FUNCTIONAL_REGRESSION_PASS` | **NOT_RUN**；N10 repair 失败，原 N100 限定授权的前置 gate 未满足，当前不执行 |
| `CHECKPOINT_READY` | **NO / BLOCKED**；设备 gate 未通过 |
| `NOT_COMMITTED` | **YES**；工作树未提交 |

| `NEW_PATH_10_CYCLE_FUNCTIONAL_REGRESSION_PASS` | **FAIL**；唯一获准 repair N10 真实设备尝试失败 |

N10 的失败边界须按原始收据表述：整窗 `timedOut=false`；A 为
`SHARED_READER_MISSING`（84 writes/81 takes），B 在 `C6_P_OLD_DELETED` 收到
collector `SIGTERM`（`control-92.json`：`code=null`、`collector_closed=true`、空
stdout/stderr），不能归因远程 SDK。B 的 `q_deleted=0` 与已有 `SHARED_DELETE Q FINAL`
收据并存，且 egress 已 closed/drained；不宣称十轮完成或完整生命周期账本 PASS。A 的
null-input load collection 失败、B 完整收集、两端 grant/revoke、JSON/byte/metadata/
ownership/DENIED/process_absent guards、`cleanup_failures=[]` 与 collector closed 均保留。

继续取得新数据前，必须先离线定位 C6 控制 collector 终止与 load collection 错误，并
取得新的限定 attempt 授权。本轮停止新数据；原 N100 限定授权的前置 gate 未满足，当前不执行；SDK 内部 drain/epoch 仍
`UNPROVEN`，P0/PF/P4、默认 `NativeBackendUnavailable` 与 `QualificationProfile`
状态不变。

证据入口：

- `.artifacts/public-socket-shared-reattach/run-20260927T113439Z/codex-resume/tester-n10-repair-initial/TEST_RESULT.json`
- `.artifacts/public-socket-shared-reattach/run-20260927T113439Z/codex-resume/tester-r4-asan/TEST_RESULT.json`
- `.artifacts/public-socket-shared-reattach/run-20260927T113439Z/codex-resume/tester-final-r5/TEST_RESULT.json`
- `.artifacts/public-socket-shared-reattach/run-20260927T113439Z/windows/n10-codex-repair-r1/N10_REPAIR_TEST_RESULT.json` (SHA `594ac61796983c94f12b408174b6de435d1b35f369df4f52490b98b3232cd26f`)
- `.artifacts/public-socket-shared-reattach/run-20260927T113439Z/codex-resume/review-fix-r5-host/REPORT.md`
- `.artifacts/public-socket-shared-reattach/run-20260927T113439Z/codex-resume/review-fix-r5-target/REPORT.md`

## 24. N10_OBSERVED_RETRY_AND_CONDITIONAL_N100 — STOPPED PRE-DISPATCH (2026-09-28 CST)

停止后的文档增量：新 N10 为 `INCOMPLETE / PRE-DISPATCH STOP`，不是完成的 data FAIL
或 PASS。唯一 query `5ca407e7845f0484` 在本地 post-query `route_query.py` 因
`Supplemental quota already claimed` 停止；query pair 使用 1、剩余 0，data dispatch
为 0、data quota 剩余 1，详见 `windows/query-observed-n10/BUDGET_CLARIFICATION.json`
（SHA `19c0460634c52c840fa5b7f30b409c4863663db7cfa4ce6ac0a0a21c3cd77f70`）。两端
query/restore guards PASS，但无 P/Q data、删除、live-load、postrun data 或 DDS 样本证据。

新包身份仅记录 stopped r12 scope：SOURCE390 SHA `8628e57951767cf235b00d6ca836050bfca35eeeb3547e768452d50b6c7c2641`、EXECUTOR27 SHA `3332d6a067631c13ad7c084aae3d6592288597e2116c0a234cecbd91bbbd1fb3`、main SHA `590bfc9ecbb04c6c0ebfb2c95a6fdd397326a30e2ef67cc2a2348b89cfff2bc3`、r12 contract SHA `1243a809208348a9837e91523b5f7843a6dd211cafc580c259f78d14046ca737`。这是停止后的文档增量，不改写 r12 报告、selection、manifest、历史窗口或原件，也不声称当前全树仍匹配该 source390。

27/27 tester 与 review-stop 只覆盖其声明的本地/预设备范围；两板只读预检/目录 query
已执行且 local/remote rc0/0，但未进入 data runner/collector。新静态构建只证明
BUILDS + static audit；旧 r4 507/507、ASan220、r5 结果保留各自版本归属，未在本轮重跑。
README/PLAN 此处是停止后的文档增量，不宣称当前全树仍匹配运行的 source390；旧 r12
与运行包身份不改写。前置 review 漏掉
prepare→claim→postrun 整路径，必须在未来修复中补充同一 consumed query receipt 的
完整回放、10.0/100.0 非整数拒绝、query/data quota 分离、route binding/result receipt、
claim replay/耗尽、27 execution-file drift 与新的独立 review。历史 N10 FAIL、N100 NOT_RUN、
CHECKPOINT_READY NO、repair0 保持；N100 条件额度未准入，不自动继续。完整记录见
[observed-retry/FINAL_REPORT.md](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/observed-retry/FINAL_REPORT.md)。

## 25. QUERY_CLAIM_POSTRUN_REPAIR_AND_EVIDENCE_REPLAY (2026-09-28 CST)

运行后的文档增量：`QUERY_CLAIM_PHASE_REPAIR_VERIFIED`、`POSTRUN_QUERY_REPLAY_VERIFIED`
与 `NEW_PATH_10_CYCLE_FUNCTIONAL_REGRESSION_PASS` 均 **PASS**；N10 run
`df9319fdf22ab3d9` 已由独立证据复核接受。每端 P11/Q1、150 writes/takes，合计
300/300；166 control 全部成功，339 条 host HDC 收据已独立复核；本轮新目录 query=0。
N10 data 用1剩0，query 用1剩0，repair0。N100 **NOT_RUN**：原 query10 不能作为
purpose100，缺 query100 且新 query 许可为0；条件额度1不构成准入。`CHECKPOINT_READY_FOR_ORIGINAL_PACKAGE=NO`、
`NOT_COMMITTED=YES`、SDK drain/epoch `UNPROVEN`，不能宣称 P4/生产资格。

本段是运行后的文档增量，不宣称当前整树仍等于冻结 source390；r12、r13、manifest、
replay 与历史原件身份不改写。无 C++ 新构建；新静态包仅 `BUILDS + static audit`，旧
r4 507/507、ASan220、r5 结果保留原版本归属且未重跑。完整记录见
[query-replay/FINAL_REPORT.md](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/query-replay/FINAL_REPORT.md)。

## 26. QUERY100_AND_FINAL_N100_EXECUTION (2026-09-28 CST)

运行后的文档增量：query100 `12ceebf2b741302e` PASS，Q-first N100
`eab10c4b776d792c` PASS，独立 review 接受 2894 evidence hashes。每端 P101/101、Q1/1，
1500 writes/takes；全局3000/3000，1606 control 完整正常关闭；host receipt recount
1861。既有 P-first N10 `df9319fdf22ab3d9` 保持 PASS，本轮不重跑，不把两顺序各100写成已验证。

query100 与 N100 quota 各用1剩0，N10本轮0，repair0。`CHECKPOINT_READY` 仅表示有限
证据记录，`NOT_COMMITTED=YES`；SDK drain/epoch/cancellation、长期资源上限、生产/P4、
QualificationProfile 仍未证明。新静态包仅 `BUILDS + static audit`；旧 r4 507/507、ASan220、
r5 结果保留原版本归属且未重跑。此处是运行后 documentation delta，不宣称当前全树仍等于
source390；r14/manifest/claim/replay/验收原件不改写。完整记录见
[final-n100/FINAL_REPORT.md](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/final-n100/FINAL_REPORT.md)
与 [EVIDENCE_INDEX.md](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/final-n100/EVIDENCE_INDEX.md)。

## 23. OFFLINE_DIAGNOSIS_AND_BOUNDED_REPAIR_ONLY (2026-09-28 CST)

离线包 `offline-c6` 的结论为：`OFFLINE_CAUSALITY_GAP_BOUNDED / HISTORICAL_N10_CAUSE_PARTIALLY_UNRESOLVED / INDEPENDENT_REVIEW_CLOSED / DEVICE_RETRY_AUTHORIZATION_REQUIRED`。
本包只做本地诊断与有界证据修复；tester-2 的 18/18 命令为本地 Node/PowerShell/Python
子进程和合成控制文本，未执行 C++/ASan/HostDDS100、HDC、SDK、目标程序、设备或真实
window。freeze-2 SOURCE427、EXECUTOR25、817 历史窗口与六个 r5 包成员的哈希比较均通过，
但 README/PLAN 后改属于 post-validation documentation delta，不宣称当前整树仍与
SOURCE427 完全一致。纠正后的 FACTS SHA 为 `f33bb84bca9166a8075e2066c4d2c728cd3e6ce9116d86151c143bb631bd005a`。

F05/F32 的错误 C6 归属已按实际 `C5_SHARED_DATA_DONE` stage 修正并经
`review-final/REPORT.md`、`review-causality/REPORT.md` 复核关闭。历史
`TERMINATION_INITIATOR_UNOBSERVED` / `TERMINATION_REASON_UNOBSERVED` 保持不变：
control-92 的 SIGTERM、空输出和 `timedOut=false` 不能归因 timer、远程 shell 或 SDK。
150 个前五轮跨侧样本严格匹配；C6 A84/81、B81/81 的未观察项不证明丢包。Q FINAL
public delete 返回与 B `q_deleted=0` 的差异保留为未满足后置条件未知，不改写为正常完成。

离线修复包括 typed incomplete receipts、structured load failure、确认 close 的 UTF-8/
CRLF/tail 处理、依赖 release 停止、command-scoped termination/requester/reason/deadline/
PID/exit/close/bytes 收据、显式 executable 与有界 exit-close drain；原 7s/5s/250ms
预算和容量未扩大。真实 Windows held-pipe gap 未直接复现，注入 close-delivery 的
RED/GREEN 保留其限定。`NEXT_N10_REQUEST_DRAFT.md` 仍是草案，不创建 window/contract，
不恢复授权。历史 N10 FAIL、N100 NOT_RUN、CHECKPOINT_READY NO、retry=0 及原六项 gate
保持不变。完整记录见 [.artifacts/.../offline-c6/FINAL_REPORT.md](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/offline-c6/FINAL_REPORT.md)。

## 27. CHECKPOINT_LOCAL_R6_FINAL_CLOSURE (2026-09-28 CST)

本地文档 checkpoint：标准 CONFIG `523/523 PASS`、Host100 replay `25 PASS`、target-core
W2 replay `5 PASS`；tester final closure `11/11` 命令与 Flow21 PASS，HDC/device=0。
17+2 task/external execution-member closure 是本地 promotion path，不是历史 device
execution path。内部9项历史 RealText 保持 SKIP，DLL allocator injection 未触发，初次无效
环境日志保留；ASan220 仅复用原证据，未新跑。r6 review closure PASS，CP-1/CP-2/CP-3
及 external analyzer dependency 已闭合。

N10/N100 有限范围与历史失败边界不改写；默认 `NativeBackendUnavailable`、P0/PF/P4、
生产限制和 SDK drain/epoch 边界不变。README/PLAN 为运行后文档增量，不宣称当前全树
等于候选快照。完整文档收据见
[checkpoint-local/docs-final/FINAL_REPORT.md](.artifacts/public-socket-shared-reattach/run-20260927T113439Z/checkpoint-local/docs-final/FINAL_REPORT.md)。
