# Fast DDS OpenHarmony DSoftBus Transport V1 冻结方案

项目名称已整改；下述历史合同和运行结论保持原范围。旧源码名、协议字面量和
收据身份不代表独立MDDS中间件，也不作为新命名ABI已经运行设备的证明。

本文是用户确认的实施基线，并纳入 2026-09-21 本会话最新批准的共享回调表设计修订。它描述目标和验收门槛，不把计划当成完成证据。当前 hot-rebuild 工作包已达到 `DONE / CHECKPOINT_READY / NOT_COMMITTED` 的限定功能 checkpoint；其唯一收口 run 为 `.artifacts/hot-rebuild-production-integration-20260921/run-20260921T152404Z/`。该 checkpoint 不改变本计划的 `P0 BLOCKED / NOT QUALIFIED`、生产 `NativeUnavailable`、P4 未授权、无 QualificationProfile 和不 commit/push 边界；OLD2/PF-02、SDK internal settlement、RAW/三板/mixed/self-route/压力故障 close 仍未资格化。完整证据与历史分界见 [实验报告](FASTDDS_DSOFTBUS_EXPERIMENT_20260921.md) 和 [文档同步收据](../.artifacts/hot-rebuild-production-integration-20260921/run-20260921T152404Z/docs/DOCS_SYNC.md)。

## 固定边界

Transport 只使用 DSoftBus Socket API 和其原生链路复用、鉴权、加密及选路能力，不开发自有复用协议、broker、跨进程 IPC 或 Session API，不修改 DSoftBus。Fast DDS 上层继续负责 DDS/RTPS 语义、SPDP/SEDP、QoS、History、可靠性、分片、重发和 Writer/Reader 状态；adapter 不实现 RTCP、逻辑端口协商、stream 拆包、History 或消息重放。

一个活动 Participant 对应一个 DSoftBus listener，可拥有多个 accepted socket 和主动连接。Participant 的 profile 在创建时固定：`BYTES_NATIVE` 使用 `DATA_TYPE_BYTES`，`RAW_PLAIN` 使用 `DATA_TYPE_RAW_STREAM`，`RAW_ENCRYPTED` 使用 `DATA_TYPE_RAW_STREAM_ENCRYPED`。三种 profile 都必须实现并分别测试，最终取舍以实测报告为准；profile 切换通过重建 Participant 完成。

### 决策来源与同设备验收保留

以下来源均为本会话用户消息，2026-09-21 核对；不是由后来的失败实验推导的需求变更：

- **U1**：用户以 `PLEASE IMPLEMENT THIS PLAN: # Fast DDS DSoftBus Transport 开发与 TDD 验证方案` 明确要求实施的计划，第 1 节原文为“DSoftBus 与 UDP/TCP/SHM 正常共存。同机通信继续使用 Fast DDS 原有能力。”第 4.2 节同机同进程/跨进程行进一步要求“使用原生同机机制；单独验证与 DSoftBus 共存”。因此同机 DDS 通信选择已有用户批准，继续属于冻结基线。
- **U2**：用户后续“提交保存，然后继续执行……”消息第六节又要求最小 native Socket 同设备同进程/跨进程探针，并明确不能用 UDP/SHM 等成功替代。没有找到撤销该项的用户指令；因此 **native self-route 验收仍保留**，当前能力缺口单独记入 P0 平台问题，不能借 U1 或 mixed transport 结果删除它。
- **U3**：本轮用户“批准共享回调表模型的设计修订”只批准 `实际 SDK service record + client/server role` 的入口绑定粒度，明确不批准完整生产 backend，也不改变一活动 Participant 一 listener、预算和旧记录归属。

同机 DDS 采用原有机制、mixed transport 共存，以及 native self-route 是不同的实现选择/验收命题；后两者分别保留自己的证据和状态。若以后要移除 native self-route 验收，需要明确用户决策，本轮没有作此变更。

## 地址、配置与 Fast DDS 兼容

descriptor 必须显式携带 namespace/domain/service slot、profile、qualification profile、消息上限、初始 peer slots、TX/RX mode、资源预算、操作时限及公开 SDK 的 bind/listen/socket options。`service_slot` 由部署配置分配，不从 Fast DDS participantId 推导。每个选项保存 `requested → SDK result → effective value（可查询时）→ verified behavior`；不能静默降级。

locator 使用规范服务名、稳定的 namespace/domain/UDID 摘要和 profile/slot 编码；`networkId` 仅是在线路由信息，不是持久身份。暂时未连接不等于地址不可达；设备离线时当前 route 必须失效，已知不可达目标可以拒绝发送，重新上线后重新解析。initial peers 只用于原始 SPDP bootstrap，不公告虚拟地址，不创建新发现协议。UDP/TCP/SHM 与 DSoftBus 必须共存。

地址编码固定为：私有非负 locator kind；`address[16]` 为规范 `{namespace, domain, UDID}` 的 SHA-256 截断键，碰撞必须拒绝歧义路由；`port` 高 8 位编码 profile，低 24 位编码 `slot + 1`，零值和非法组合拒绝；同一 Participant 的 metatraffic/user traffic 公告同一 DSoftBus locator。profile 不兼容的 locator 在选择阶段排除。bootstrap 发送面为“已认证在线设备 × 显式 slot 集合”，不公告 bootstrap 虚拟地址。

配置和 locator 契约：

| 契约 | 固定要求 |
| --- | --- |
| SDK 公开选项 | 按适用阶段显式传递并记录 requested/result/effective/verified；Reserved 或未验证扩展可保留为显式 passthrough，但不得宣称效果；数值别名冲突直接拒绝 |
| remote locator 容量 | 配置容量必须保留原生 UDP/TCP/SHM locator 与 DSoftBus locator，不因默认容量截断 |
| 地址可达性 | 支持性、转换、可达性、选择接口完整；设备离线使 route 失效，在线后重新解析 |
| DSoftBus 连接 | 每个 endpoint pair 最多一个 outbound 和一个 accepted；两个都计入预算；发送选择 READY 通道，generation 防旧回调改写新连接，不重放失败消息 |

Fast DDS 的自定义 locator 选择必须按完整地址比较，避免通用 IPv6 multicast 判断误判私有 kind；必要时在 adapter 选择逻辑中遍历并匹配完整 locator，不先改 Fast DDS 核心。DSoftBus 初始化失败时，不能因为 UDP/SHM 仍可用而把 Participant 误报为 DSoftBus ready；需要 checked-create helper，独立记录每次 descriptor 尝试的 `NotAttempted/Starting/Ready/Failed` 和 listener/receiver 是否已就绪。

配置辅助接口必须保留原生 UDP discovery、TCP initial peers 和 SHM 设置，并按部署最大公告地址数配置 remote locator 容量；默认容量不足时创建失败或明确报告，不得静默丢地址。

## 数据、内存和生命周期

首版默认为有界 TX/RX 队列：TX 在接纳前把完整输入、目标 locator 和元数据取得所有权，默认最终池化消息块一次；同一次多目标发送共享不可变块，逐目标释放引用。`send=true` 只表示完整消息及投递任务已被本地接纳，不表示远端收到；超时、关闭、资源不足或已知不可达时拒绝接纳，接纳后失败不由 adapter 自动重放。RX 默认在 SDK 回调返回前复制一次进入有界池，再调用 Fast DDS receiver。条数、字节数、连接数、线程数和进程预算都必须可测；`max_message_size` 稳定，通道容量不足不得 READY，大样本继续由 Fast DDS 分片。

低复制模式为正式可选能力：`TX_NATIVE` 对连续输入不得产生额外 adapter copy；多个 input buffer 至多合并一次。`RX_INLINE` 在回调内直接交付且 adapter 不复制整包。只有输入寿命、重入、取消、关闭和压力测试通过后才列为支持；报告必须量化 Fast DDS 原生异步发布与各 TX mode 的双层队列成本。

每个 native handle 只有一个关闭责任人；旧回调不得修改新连接；Participant detach 时先阻止新任务并等待该 Participant 的 receiver、accepted-I/O 和 adapter worker 排空，再决定是否保持可复用的 native InputChannel；真正 native channel teardown 另有关闭责任人和顺序。每个 owned accepted socket 必须显式关闭，不能假设关闭 listener 会级联关闭 accepted socket。不能依靠遗留线程、无限 timeout 或进程退出掩盖 SDK 阻塞。每个 endpoint pair 首版最多一个 outbound 和一个 accepted，均计入预算；合法的同时 outbound/accepted 通道不主动互相淘汰；SenderResource 数量在重连中保持稳定；发送失败不重放已失败消息。单个阻塞目标不得无限占用全部发送 worker。adapter 内部锁不得包围 Fast DDS receiver 调用或可能重入的 SDK 调用。

### 已批准的 callback table incarnation 设计修订

归属单位为 **客户端 SDK 进程中的实际 service record 实例 + client/server role**，而不是 numeric socket、Participant generation 或单独的 service-name 字符串。一个 record 内有两个不同 role 字段；它们不是两个 listener。同一表下的各 socket 分别保留唯一 native owner、关闭责任及连接预算；一活动 Participant 恰好一个 listener 的要求不变。先前 A 的 per-Socket 独立表假设已被实机反证，不重复健康 echo。修订依据和公开边界见 [record 生命周期核查](../.artifacts/closure-20260921/incarnation/NOTES.md)；A 的机器码和实机证据仍按原 run 保存。

slot 继续遵守 `FREE → RESERVED_UNEXPOSED → EXPOSED → TOMBSTONED`。首次可能把入口交给 SDK 前先置 EXPOSED；即使注册返回失败也不能回收该 slot。只有从未暴露的 reservation 可回收。**在同一已确认存活表上重复注册同一固定入口不发生上述状态迁移，不是退休 slot 复用**；新增该表下的 socket 不自行消耗新 slot。重复固定入口仍须让每个 Socket 自身处于合法的 SDK 调用状态；这不表示对任意 Socket 重 Bind 都必然成功。slot 耗尽只阻止需要新 table incarnation 的创建，必须在相关 SDK side effect 前拒绝；既有活表上新增 owner 仍受原有连接/内存/线程预算约束，不新增累计 Participant 或连接配额。

| 实际表边界 | 设计合同与行为 | 公开可观察性及限制 |
| --- | --- | --- |
| 创建/首次暴露 | 在独占受控的本地 service-name 所有权范围内建立 native owner 与 role 表记录，为该 incarnation 固定一个真实函数入口。概念上的 record 实例身份不能伪装成 SDK 回调提供的 generation/context。 | `Socket()` 返回 socket ID，不返回 record 实例标识或“新建/已存在”标志；实现会吞掉内部 NAME_REPEATED。首次所有权/创建边界必须有版本绑定的生命周期前提；同名和新 ID 都不是实例身份凭证。 |
| 已确认连续存活 | 同表所有 Listen/Bind 注册使用同一固定入口，按各 socket owner 分派。一个 owner 关闭不退休仍为其他 owner 服务的表；一个 role 暂无 socket、但另一 role 维持 record 存活时，也保留该 role 的固定入口。 | 当前普通删除路径只在 sessionList 为空且 sessionAddingCnt 为零时删除 record；持续存活、独占管理的 listener/InputChannel 可作为这一已核路径下的连续性 anchor。它不是对所有异常/其他 SDK 使用者的全局证明。 |
| Participant/application detach 与重新 attach | 尽量保持兼容的 native InputChannel、listener 和活表。旧 application/receiver 的 adapter barrier 完成后，结构上允许复用同一活表，不重复建立 native record。 | 不得先关闭最后 anchor、再以“名字相同”声称旧 incarnation 继续存活。SDK 内部旧 application 时期事件的强隔离仍须平台前提闭合；此处不是已支持热重建或已排空的声明。 |
| 真实 record 销毁 | 结束该 record 的两种 role incarnation；所有已暴露入口永久 tombstone。同名新 record 必须有新的 incarnation/入口，绝不重新绑定旧 slot。单一 owner 的注销与整个 record 的销毁分开。 | 普通 `Shutdown → ClientDeleteSocketSession → TryDeleteEmptySessionServer` 可先 unlink/free 客户端 record，再移除服务侧信息。`Shutdown()` 为 void，不提供 record 删除结果或 callback drain 回执；其返回不能证明实例连续性或完整结算。 |
| 连续性未知/异常丢失 | 停止基于旧 incarnation 接纳新 owner 或热 attach；既不自动续用旧入口，也不用新入口覆盖可能仍活着且服务其他 owner 的表。先按已知所有权处理故障，等待可证明的存活/替换边界。 | 名字、当前 numeric ID、当前表查得的 generation、通知已发送均不能补出旧事件身份。缺少观察手段时形成平台前提，不调用私有接口、不引入配额、IPC、broker 或无限等待来掩盖。 |

adapter 接收记录的线性化点仍是在归属门内一次性取得 **native owner + application/receiver lease**。已捕获旧 lease 的记录只完成给旧归属，后续不得重新查表改投新 application；无 application 的记录当场终结丢弃，不留给后续 application。固定入口可辨别已复制旧表入口的迟到回调，但不能恢复 SDK 在 ingress 前丢失的事件原始归属。UDP 控制事件的身份屏障必须发生在第一次 native 资源修改之前；后置 OnError/isClosing 判断及“旧通知已发送”不等于旧通知已处理或作废。

**本轮状态为 CLOSED_WITH_PLATFORM_DEPENDENCIES；共享表设计为 DESIGN_APPROVED，生产共享表管理尚未实现、未资格化。** 现有 `softbus_ingress_contract` 的 11 个主机用例是单 owner fixture，仍覆盖已捕获 lease/no-app/tombstone 等合同；它的首次 Expose 操作不是 SDK 重复注册既有固定指针的模拟。其既有 PASS 不能提升为共享表、多 owner 或真实 record 生命周期 PASS。本轮不为测试设计而新建 registry/mock 框架，不修改这些仍有效的测试。未来实现的必要合同是：同活表重复注册不增 slot、多 owner 退出不误退休、跨 role 保活、真实销毁后同名新实例、连续性未知不误复用，以及旧 lease/no-app 不重新归属；当前均仅作为设计约束，未执行项标记 NOT_RUN。

**状态更新（2026-09-24）：** 上述必要合同已有一个 SDK-free **有界实现**并通过限定 host 验证（`include/mdds/dsoftbus/ServiceRecordTable.hpp`、`src/ServiceRecordTable.cpp`，15 个契约 case × 独立进程；Release/Debug `15/15`、standalone `43/43`、CONFIG host Release `258/258`），冻结契约与执行状态见 [PLAN.md 第 18 节](../PLAN.md) 与 [.artifacts/srt1/README.md](../.artifacts/srt1/README.md)。`softbus_ingress_contract` 的 11 个单 owner 用例保持不变，其 PASS 仍不能提升为共享表 PASS。生产共享表管理仍未接入 native backend、未设备验证、未资格化；target record 连续性与 ingress 前事件身份仍是未闭合的平台前提（PF-02）。

## TDD 阶段与放行

P0 先锁定 SDK/固件/库版本、权限、容量、取消/关闭、回调和资源契约；当前 P0 已因上述证据缺口 BLOCKED。以下 P1–P6 是保留的后续路线，不是本轮自动续跑授权：P1 descriptor/codec/配置，P2 listener/多 owner/共享表/热 attach，P3 池化收发与有界调度，P4 transport hooks/原生发现，P5 混合与故障，P6 性能与发布。只有出现具体新机制、平台契约、可判别 trace 或明确用户决策，才重新开启对应受影响调查；没有新证据时不重复全面探针。

每阶段都遵循失败测试→最小实现→重构→独立 tester→review→修复→复测→文档同步。必须分别记录 implementation、build、host test、device test、integration test 和 verified；未取得版本绑定、拓扑容量和完整运行证据不得生成生产 QualificationProfile。

QualificationProfile 至少绑定 SDK、固件、DSoftBus library 和构建 artifact hash；profile、有效 API 及调用阶段；安全消息上限、发送/关闭时限；Participant/进程资源预算；已验证拓扑容量；测试 run ID 和证据位置。生产配置必须匹配已有资格记录；没有匹配记录只能显式进入 qualification/probe 模式，不能用源码默认值替代。

## 追踪、测试与复现

要求追踪表把 P0（平台资格）、P1（descriptor/codec/directory）、P2（listener/connection/slot/lifecycle）、P3（buffer/queue）、P4（Fast DDS hooks/discovery）、P5（混合与故障）、P6（性能与发布资格）分别映射到实现文件、失败/通过测试、review、设备 run 和最终状态。当前 P0 证据见 [P0 报告](P0_IMPLEMENTATION_20260920.md)、最终 receipt、[review](../.artifacts/implementation-20260920/p0-review/REVIEW.md)、`p0-baseline` 和 final build logs；profile ELF SHA-256 为 `cfa123876be1a3cdd3643b23338410fd78f69a82f0700044cbaf046112958cea`，cancellation ELF SHA-256 为 `694ac60b8cd13ca26e36d7056bd3fbd1de4640621bca08d9a2b23c44b1b56e04`。

| 阶段 | 目标与必须关联的证据 | 当前状态 |
| --- | --- | --- |
| P0 | SDK/API/权限/版本资格、容量、发送/关闭、迟到 callback 防护、资源预算；失败测试、双板 receipt、review 和 QualificationProfile | BLOCKED / NOT QUALIFIED |
| P1 | descriptor、服务名、locator codec、设备目录、profile/容量/选项校验；编解码和冲突测试 | PLANNED |
| P2 | 一个 Participant 一个 listener、outbound/accepted 所有权、incarnation tombstone、热 attach、并发关闭 | PLANNED；共享表有界模型与共享表×native owner 组合层均已 IMPLEMENTED / HOST_TESTED（限定范围，见 [PLAN.md 第 18、19 节](../PLAN.md)）；真实 listener/连接与热 attach、设备证据未实现 |
| P3 | 有界 TX/RX、消息块所有权、多目标引用、native/inline 低复制资格 | PLANNED |
| P4 | Transport hooks、SenderResource、receiver、checked-create、SPDP/SEDP 和原生 locator 共存 | PLANNED |
| P5 | UDP/TCP/SHM 混合、重连、离线 route 失效、进程故障和资源隔离 | PLANNED |
| P6 | 三 profile 公平性能、复制计数、CPU/RSS、延迟、恢复/关闭、发布资格 | PLANNED；三板专项 DEFERRED |

复现至少保留 Windows Host Debug/Release CTest、profile/cancel TDD red→green 日志、OpenHarmony A/B build/deploy/route/guard/revoke 命令和退出码；双板矩阵覆盖每板一个 Participant、同进程/跨进程多个 Participant、多 Reader、三 profile 同/异 profile、UDP/TCP/SHM 混合、BEST_EFFORT/RELIABLE、大样本分片、late joiner、离线/重连、进程退出和拥塞关闭。数据类型专项覆盖容量 `M-1/M/M+1`、RAW 短返回风险和 DDS Security 组合；不能把小记录成功当作加密或完整发送证明。

性能必须覆盖 64 B、1 KiB、16 KiB、64 KiB、1 MiB，小控制消息与大样本并发，空闲/持续/饱和/突发；输出吞吐、p50/p95/p99 延迟、CPU、RSS、分配次数、adapter copy bytes、连接数、队列占用、恢复与关闭时间。比较 Bytes 与加密 RAW 的相近安全条件、明文 RAW 与加密 RAW 的单独成本，并比较 Fast DDS asynchronous publish 与 TX modes 的组合；不预设 RAW 更快。三板 A→B/C、单远端故障隔离和多设备公平性保持 `DEFERRED`。

双板必须覆盖跨 Participant、跨进程、跨 board、多 Reader、混合 UDP/TCP/SHM、BEST_EFFORT/RELIABLE、大样本分片、late joiner、离线/重连和进程退出。三板 A→B/C、单远端故障隔离和多设备公平性当前明确 `DEFERRED`，不能用双板多 Participant 代替。

## 2026-10-04 显式修订：PACKAGE_SCOPED_V1 native 服务名合同

本节为用户 `PACKAGE_SCOPED_V1_SERVICE_NAME_REVISION_AND_TASK_CONTINUATION` 授权的新合同修订，不回写旧裸名合同或其 checkpoint。设备验证仍待本轮真实收据。

原 `md1.<namespace小写hex>.<domain八位小写hex>.<slot六位小写hex>.<profile两位小写hex>` 保留为规范 V1 服务名主体。`.01` 表示原 profile1；slot 仍为原零基 service_slot。完整 SDK listener/peer-listener 名称唯一装配为 `<已批准完整部署前缀>.<规范主体>`，不得双前缀、截断、裸名 fallback 或失败后重试旧名。

部署显式声明 `PACKAGE_SCOPED_V1`、实际合法 package、属于该 package 的已准入完整前缀及对端同一合同。配置生成器必须对照受控 PRIVATE_CONFIG；实际 Runtime 在一次性 descriptor 发行前将 package、生成 listener/peerName、发起端映射绑定到 immutable PublicSocketConfiguration。输入 peer scope 经相等性校验后不另存别名；ValidateV1 用唯一存储 scope 重算所有结果名称，拒绝字段变更。原有独立发起端名称仍是明确部署输入，不由 bridge 重建 run/side/role 名；listener 与发起端 SDK record 必须保持不同，原 owner/lease 生命周期不变。

前缀仅影响 native 命名。原哈希输入及逻辑 namespace、UDID、domain、SHA-256前16字节、kind、profile/slot port、24字节CDR、meta/user相同公告、完整locator selection、碰撞拒绝及 NO_REACCEPT 全部不变。完整名称仍受255字节上限；加入前缀导致容量不足时明确拒绝，不缩短主体或 namespace。缺失/非法字符、错误包根、重复前缀和对端不一致均在 native 暴露前拒绝。静态策略匹配仅为本地准入依据，不是 SDK/device PASS；平台规则、身份、grant、PF/P4 和生产默认 backend 均不因本修订改变。
<!-- V1 initiator deployment decision, 2026-10-04 -->

本次部署暂时沿用原来已经批准的具体发起端 Socket 名字，分别通过
`local_initiator_name` 和 `peer_initiator_name` 显式输入。它们不是规范
`md1` listener 主体的一部分，也不进入 namespace 或地址哈希。真实名字仅
保存在受控部署材料中；TargetBridge 不根据 run/side/role 重建名字。
这是本轮用户明确批准的临时部署决定，后续修改发起端命名合同必须单独
说明兼容性并验证两端对应关系，不能将本轮沿用写成长期规范。

<!-- V1 device acceptance closeout, 2026-10-05 -->

本轮 final raw acceptance 已在受控范围完成：V1 locator device runtime 与
ROS2 dual-board acceptance 为 PASS，Commit Gate 为 PASS，生产资格仍为
`NOT_PRODUCTION_QUALIFIED`。两板双 QoS 合计 `20/20`，graph、两个跨板
service exchange、Participant delete、Runtime close、public return 0 和
正常退出通过；SPDP `SendBytes`/ingress 各板 `14/14`，实际观察保留完整
kind/address/port，profile 1、slot 0、port `0x01000001`。SDK-correlated
20 takes、109-member package、live origins 和 normal restore 均有收据。

该收据不证明 cleanup-tail absence、zero loss、SDK epoch 或 SDK drain；A
侧 19 个 `InvalidState` callback、B 侧 0 个及 startup cleanup flag 0 按
原始记录保留。A5/C1 `NO_REACCEPT`、PF/P4、正式 reconnect 缺口和其他
设计限制不变；未生成 QualificationProfile，未启动性能 100 轮或新设计。

<!-- A5/C1 bounded Host package disposition, 2026-10-05 -->

本轮另有明确授权的 A5/C1 有界 Host 实现，证据索引见
[A5C1 Host acceptance](A5C1_BOUNDED_REACCEPT_HOST_ACCEPTANCE_20261005.md)。
当前状态为 `A5C1_BOUNDED_REACCEPT_HOST_VERIFIED`，并已完成 API23 TargetBridge
编译、链接及安装 consumer 静态核对（`API23_REACCEPT_BUILD_VERIFIED`）；
Post-review targeted Release `19/19` 与受影响 ASan 选择 `117/117` 通过。
Host 双端结束顺序、同一 ROS 实体存活、Topic 恢复、graph/
service、原始 DDS 删除、Runtime close 和 public return 0 已通过。

该结果只适用于显式 Host opaque-callback 实验和有界 never-used native ID
模型。默认真实 backend 仍保持 `NO_REACCEPT`；SDK epoch/drain、同 ID 复用、
对端进程重启、route/permission rediscovery、任意长期 reconnect 及设备
reconnect 均未验证。冻结的全 CONFIG 运行是 `574/576`（574 PASS、2 FAIL）；
随后两个未变化 targeted case 加一个新注册 control case，使 577 个 case
在多个运行中均有覆盖，但没有取得单次 577 项全绿运行；历史
timeout/loader/PowerShell/configuration 失败保留。
因此正式 reconnect 要求仍未满足，`FULL_REQUIREMENTS_COMPLETE`、
`SDK_RECONNECT_QUALIFIED` 和 `PF01/PF02_PASS` 均未成立。

<!-- A5/C1 bounded native reaccept final status, 2026-10-05 -->

本轮有界 native reaccept 已完成两种正常关闭顺序的真实双板验证：A
outgoing-first run `6c66b902d5c05f36`，B accepted-first run
`d533f8880e53a5e4`。两次均保持原 Runtime、Participant、ROS entities、
listener、entries 与 V1 locator，实际 SDK Socket/Bind 路径产生了可判别的
新 ID；每次 PRE 8/8、POST 12/12，合计 20/20，replacement 后业务、graph、
service、DDS 删除、Runtime Close、public return 0、正常退出、加载和恢复
均有独立收据。因此状态为 `BOUNDED_NATIVE_REACCEPT_VERIFIED`、
`ROS2_SAME_ROUTE_RECONNECT_OBSERVED`。

该结论只覆盖显式、默认关闭的有限正常 close/fresh-distinct-ID 实验。
默认真实 backend 仍为 `NO_REACCEPT`；PF/P4、平台策略和服务名/V1 编码保持
不变。SDK physical record incarnation、event epoch、drain、隐藏旧事件、
同 ID 复用、对端重启、故障取消、一般无辅助自动恢复及正式 reconnect 的
其它 hard requirements 仍为未满足或未证明。`FULL_REQUIREMENTS_COMPLETE`、
`SDK_RECONNECT_QUALIFIED`、`PF01/PF02_PASS` 和 QualificationProfile 均未
成立。历史失败、577 分批覆盖、旧 V1/device receipts、data17/data18 以及
35/36 refusal evidence 保留原处分，不回填为成功。

<!-- Reconnect requirement closeout, 2026-10-05 -->

新增的 [RECONNECT_REQUIREMENT_CLOSEOUT_20261005](RECONNECT_REQUIREMENT_CLOSEOUT_20261005.md)
仅是规范对账和证据同步，不引入实现、测试或设备运行。基线为
`5e57b2c4a176d7dd7d1f84d2e6af7f99addf4e9d`；当前 49 项源码/测试 hash、
既有 API23、Host、ASan 和两次真实 SDK raw/review/恢复收据在原范围内复用，
本次没有新的 build、test、HDC 或 SDK 操作。

根据当前 V1 计划、A5/C1 Host contract、native admission contract 和实际
control/pump 源码，本次只关闭两种正常 close 顺序下的同路由替换、原 ROS
实体/listener/V1 locator 连续性、旧 owner 结算后新 owner 业务交付及正常
清理子项。两次各有一个 `NATIVE_END`、一个外部 `NATIVE_REPLACE` 和两个
`REACCEPT_DONE`；外部 peer-ready gate 是已批准合同的一部分，产品 pump 和
binding 才执行真实 Socket/Bind，不能称为 assistance-free 或
`BOUNDED_EVENT_DRIVEN_RECONNECT`。

“terminal event 必须无外部 gate 自动触发 replacement”当前未在定位到的
有效规范中成立，因此不新增状态机。V1 offline route invalidation/online
re-resolution 仍是既有未闭合要求；peer restart、same-ID、任意 fault
cancel、SDK physical record/epoch/drain 和原编号 A5/C1 finding closure 均
保持未满足、未证明或 source-incomplete。默认 `NO_REACCEPT`、PF/P4、平台
策略和 `NOT_PRODUCTION_QUALIFIED` 不变，`FULL_REQUIREMENTS_COMPLETE`、
`SDK_RECONNECT_QUALIFIED` 和 QualificationProfile 均未成立。
