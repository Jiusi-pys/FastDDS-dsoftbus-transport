# P0 实施与验证报告（2026-09-20；2026-09-21 更新）

## 结论

本轮状态为 `CLOSED_WITH_PLATFORM_DEPENDENCIES`（本轮可行性收敛已关闭，平台问题未解除）：同步用户已批准的共享回调表设计，仅补核 TCP_DIRECT 的 TCP_USER_TIMEOUT/keepalive 机制，并把能力缺口收口为有解除条件的平台问题。没有重跑 A/B/C/D、没有新增通用 observer 或主机框架、没有实现生产 backend。P0、生产入口与 P4 的限制保持不变。另有一项经明确授权的、独立且 default-OFF 的限定真实 Fast DDS × DSoftBus R4 实验，见 [实验报告](FASTDDS_DSOFTBUS_EXPERIMENT_20260921.md)；该 `SCENARIO_GATE=PASS` 不是 P0 资格、P4 放行或提交授权。

命题范围更正（适用于本文所有历史测量）：裸 SDK 的 `M+1` 完整传输只反证“SDK 在 M 有硬上限”，不反证 M 可以作为 adapter 的保守配置上限。DDS sample 可以由 Fast DDS 分片为多个 transport RTPS message；每个 native API 输入还可能被 SDK 加入长度、加密等编码开销，三种大小不能混用。本文不以寻找 SDK 最大值为目标。BYTES 的设备反例只否定 1 秒候选预算；其他有限机制上界与完整资源结算分别仍待证明。public socket counter 溢出导致进程 abort，不等于存活进程内正常 ID 重用；内部 channelId/channelType 的分配、复用与事件归属必须另行审查，不新增累计 Participant 或连接配额。

平台资格仍为 **P0 BLOCKED / NOT QUALIFIED**，完整 Transport 为 **NOT VERIFIED**，生产入口保持 `NativeUnavailable`，P4 未授权。本报告保留 BYTES 1 秒 `FAIL`、RAW 取消 `NOT_EXERCISED`、D 独立交付窗口各 `64/64` 和历史 V4 各 `1/64`；不拼接不同运行的压力与完整性证据，不生成生产 QualificationProfile，不 commit/push。

## 本轮设计修订与定向核查

共享表模型已由本轮用户批准为 **设计基线**，范围仅是回调入口的绑定粒度；完整条款及决策来源在 [冻结方案](MDDS_V1_IMPLEMENTATION_PLAN.md)。一个实际客户端 SDK service record 实例有 client/server 两个 role 表，各表保持一个固定 callback incarnation，表下 socket 各有 native owner。同一活表重复注册同一固定入口不消耗新 slot；真实 record 销毁则两 role incarnation 都结束，同名新建不继承旧入口。单个 owner 退出不误退休仍活的共享表，连续性未知时不按名字猜测、更不以新入口覆盖可能仍活的表。普通删除路径、公开可观察边界与单 owner host fixture 限制见 [生命周期核查](../.artifacts/closure-20260921/incarnation/NOTES.md)。本轮仅修订设计合同，未把现有 11 个单 owner 测试冒充共享表实现或平台证据，因此未增加为设计而造的 mock/registry，也未重跑这些未改代码的测试。

**C 定向结论：目标机制存在，但不足以覆盖契约。** 既有 C-mechanism/native-path 没有完成这一项，故本轮只补该缺口。目标 client `0xe0248 → 0xe0548` 的普通 TCP 分支尝试 keepalive `300 s / 4 s / 5 probes`；成功后在 `0xe05bc` 请求 TCP_USER_TIMEOUT `320000 ms`，经 utils `0x685dc`、adapter `0x31db8` 到真实 setsockopt。keepalive 失败可跳过本次 user-timeout；任一设置失败均可只记录 warning 后继续 channel 注册。**Bind 成功不证明设置成功。** 实际 channel/protocol 的对应调用、setter rc、effective value 及 timer 触发没有被既有实机记录绑定，分别仍是 NOT_OBSERVED/UNKNOWN/NOT_RUN。目标 Linux6.6.101 的供应方补丁/配置亦未绑定到公开 stable 源码。完整证据、错误出口、引用下降链和本地只读检查命令（exit0）见 [TCP timeout 定向核查](../.artifacts/closure-20260921/tcp-timeout/RECEIPT.md)。

连接级 timer 的状态起点/重置与整次 SendBytes 不同；它不覆盖所有 SDK 锁、服务 IPC、调度、callback 或资源结算。即使设置成功，也不能把 320 秒设为可接受关闭预算或把连接终止等同发送/关闭/排空已完成。本轮没有调用私有 native fd 接口、自行设置 socket 选项或关闭 fd，没有执行延长 hold、320 秒故障或增大发送量实验。

同机需求来源已在冻结方案记录 U1/U2：原用户实施计划明确批准“同机通信继续使用 Fast DDS 原有能力”；之后又要求独立 native self-route 探针且禁止用 UDP/SHM 成功替代。前者继续冻结，后者未获撤销，故 **native self-route 验收保留**；当前缺口不能因 mixed transport 未来成功而自动删除。

## 平台问题清单与解除条件（本轮收口）

以下是待平台证据闭合的问题，不是自动安排的新探针。收到具体新机制、平台契约、可判别 trace 或明确用户决策前，对应调查保持关闭；未完成的生产依赖保持 BLOCKED。

| 平台问题 / 作用域 | 当前准确命题与缺失证据 | 解除条件 / 可重新开启的证据 | 实际影响 |
| --- | --- | --- | --- |
| **PF-01 发送/关闭退出机制**：当前 client/utils/adapter hashes，BYTES 普通 IP/TCP_DIRECT 同步发送；其它 route 不自动外推 | 1 s 候选已反证；目标有 best-effort TCP timer 请求，但实际执行/成功/effective 未知。终止性错误→send 退出及普通 ref-drop 的二进制出口存在，不证明 timer 在目标故障中会触发，也不证明所有引用/回调完成。其它有限界与完整结算仍分开未决。 | 平台提供 channel incarnation/protocol 绑定的逐项请求、rc、effective（含未调用/后续覆盖），供应方内核版本/配置和故障范围；再给故障→公开取消或 timer→send-return→引用归零/自有资源释放、worker 完成与 callback 排空的结束证据。若仅覆盖连接无进展，明确剩余能力缺口；不能仅给 320000 常数或外层超时。 | 依赖 native 有界退出的生产发送、关闭、worker 回收及低复制资格仍 BLOCKED。无新机制时不重跑 hold。 |
| **PF-02 作用前的 channel/event 身份**：当前 UDP 控制事件，及共享 record/热重建的连续性前提 | UDP release→unlock→旧 TF_ASYNC 通知的目标路径已证实；旧 client 映射移除后，新同 client pair 能否先安装的完整轨迹仍 **CONDITIONAL**。真实 close 入口会先给当前对象写 isClosing、删除/关闭当前资源，再进入后置检查；OpenFailed 同样先操作资源。共享入口不能补回 SDK 内已经丢失的身份。 | 给完整旧映射移除→release→重用/安装→旧通知消费→**第一次 native 资源修改**轨迹，或证明该轨迹被屏障排除。屏障/原始身份校验必须在首次作用前成立；**旧通知已发送不等于已处理或作废**。另需证明 record 创建/连续存活/销毁边界及 SDK 内旧 application 时期事件的 drain/epoch 语义；当前表查得的新 generation 不能代替旧事件身份。 | UDP native 控制事件归属、共享表连续性与强热重建前提未闭合。不宣称全部 channel 已有故障；现有 adapter 旧 lease/no-app 合同不弱化。无可判别 trace/契约时不重跑健康 echo。 |
| **PF-03 RAW 压力下完整性与关闭**：RAW_PLAIN / RAW_ENCRYPTED，绑定各自 run/artifact/profile | D 的独立固定窗口各 64/64 精确交付成立，但本 run native_pressure=NOT_PROVEN；V4 实际压力栈与各 1/64 属另一关闭运行。正常读取会累计声明长度，错误空 buffer/跨记录条件及关闭时序仍有边界未证；selected=0 仍不是实际在途取消。 | 同一次可复现运行同时建立有效 native 压力与逐条长度、身份相关内容、重复/缺失/边界证据；关闭试验独立给真实在途区间、公开操作退出、worker/callback/资源结算。平台也可提供排除错误交付路径的版本绑定合同。watchdog 终止不算正常恢复；不把不同运行拼为资格。 | RAW 完整性、自然有界/在途关闭及资源资格仍待闭合。保留原始记录，不增加 framing、剩余字节补发或重放。 |
| **PF-04 保留的 native self-route 验收**：同设备同进程/跨进程，既有三种 profile 配置 | 公开 local networkId 的 self Bind 返回 -426049516；目标 peer UUID/ledger 失败门提供机制支持，历史返回 PC 未唯一观测。U1 的原生同机 DDS 选择没有撤销 U2 的 native self-route 验收。 | 平台给实际版本支持的**公开**地址/选项、适用阶段/权限及通道路径，并以该路径完成保留的两种 native 同设备拓扑验收；或用户明确变更该验收项。内部 0/99/loopback、UDP/SHM 或自建 IPC 不作替代。 | native self-route 项保持 BLOCKED；mixed transport 共存单独 NOT_RUN，不由此扩大为全部同机 DDS 通信不可能。没有受支持新路径不重跑同一 self Bind。 |

adapter 可修正事项与平台依赖分开：共享表绑定粒度为 **DESIGN_APPROVED / NOT_IMPLEMENTED**；NEED_ACK 已知危险组合拒绝为已有 **HOST_TESTED**，SDK 状态 `NotCalled`、无 SDK rc，继续保留。后续 Fast DDS transport hooks、SPDP/SEDP、可靠性/History/分片、混合 transport、DDS Security、多 Participant/Reader/进程与性能集成均为 **NOT_RUN**；三板专项 **DEFERRED**。本轮不会以这些未完成项自动启动另一轮全面调查。

> **2026-09-24 更新：** 共享表绑定粒度的**有界实现**已完成并通过限定 host 验证（见 [PLAN.md](../PLAN.md) 第 18 节与 [.artifacts/srt1/README.md](../.artifacts/srt1/README.md)）。本报告其余平台前提、`NOT_RUN` 项与资格结论不变；生产 backend 仍未接线。

## 前轮已保存的实现与测试证据

前轮工作树变更为 A callback-table 四个新文件、D 交付窗口四个文件的最小修改，以及 V1 配置三个文件的 NEED_ACK 准入 guard；更早的 profile/identity、address/config、ingress/local/options/observer、契约测试、构建脚本及 CMake 接线也保留为历史证据。本次设计收口不修改这些代码，不重新宣称其已获平台资格。没有修改 DSoftBus 源码、使用 Session API 或修改 DDS/RTPS 语义。

profile 探针覆盖 `BYTES_NATIVE`、`RAW_PLAIN`、`RAW_ENCRYPTED` 的选择和严格参数校验。RAW 加密请求使用 SDK 的 encrypted stream 类型；对端元数据归一为普通 stream 不能作为加密有效性证明。身份检查只记录长度和结果，不记录 UDID 内容。

cancellation 探针记录请求、实际 SDK 调用进入和返回三个边界，并测试回调、关闭、资源回收和有限预算的交错。当前 native 诊断切片包含既有 `CANCEL-01..42` 组和本轮交付窗口的 6 个记录/完整性断言；早期 `CANCEL-01..19` 记录保留为历史证据，不与当前 host 计数混淆。交付窗口断言不改变 selected=0 cancellation 的 `NOT_EXERCISED` 状态。

## 最近一次已执行验证证据（本轮未重跑）

- 最近一次 D native 构建产物：`.artifacts/feasibility-20260921/tester/D-initial/native/softbus_cancel_probe`，SHA-256 `283c15528e7d33c77122c3da4747b263cbff5d4512ddc553aad3bf028bc5ef26`；A callback-table native 产物 SHA-256 为 `3605e2d3feb695380fd6f49a932da1044f23b1e05c61cd278e2f5c51312ee046`。二者均为 probe build，不是生产 Transport build；旧 `a059...`、`694ac...` 产物保留为历史证据。
- Host：前轮最终 tester 的 [D Debug CTest](../.artifacts/feasibility-20260921/tester/D-final-host/Debug/ctest.log) 和 [D Release CTest](../.artifacts/feasibility-20260921/tester/D-final-host/Release/ctest.log) 均为 `35/35 PASS`，对应 Debug/Release 独立构建目录的 build/CTest exit 均为 `0`。旧 `breakthrough-20260920/final-host-server-admission` 和此前 `18/18` 结果保留为历史 P0 证据，不再称为本轮 canonical。
- Profile 双板早期回环：B→A→B、32/255/4096/16384/60000 B 五种精确负载，三种 profile 均 PASS；该结果是诊断 Socket 通信，不是 Fast DDS 数据路径。
- RAW 边界：`device-boundary-local/RESULT.md` 中 raw `2,097,151/2,097,152/2,097,153` 和 encrypted raw `2,097,123/2,097,124/2,097,125` 各两条均逐字节、逐长度通过；它反证本构建在 2 MiB/2 MiB−28 的 SDK 硬上限假设；不反证它们作为 adapter 保守配置上限的可行性，也不证明这些配置已合格。
- A callback-table 裁决：同进程、同本地 service name、同 client 角色的两个同时存活 outbound 分别连接两个 peer slot；第二次 Bind 后，第一条连接的后续记录也进入第二次注册的真实 callback 入口。A 两侧退出码均为 `0`，四条 `F1/F2` 记录逐长度、身份和完整内容核对通过。该结果支持 service/role 共享 callback table、反证 per-Socket 独立固定表；共享表固定 incarnation + 每 socket native owner 已由最新用户消息批准为入口绑定设计；生产实现仍未批准或实施。证据为 [A pair run](../.artifacts/feasibility-20260921/tester/A-device/pair-run.log)、[A chain audit](../.artifacts/feasibility-20260921/A-chain/A-chain-audit.md) 和 native artifact `3605e2d3…12ee046`。
- D 交付窗口裁决：释放压力后保持通道存活至预定义窗口结束的 raw/raw-encrypted 运行，各收到 `64/64` 条；长度、run/sequence 派生内容、重复、缺失、跨记录边界和 late 计数均为通过，远端退出码为 `0`。这是修正 fixture 后的完整交付观察，不是有效压力资格或关闭期间交付证明；V4 关闭/压力运行中各 `1/64` 的历史结果继续保留。证据为 [D result](../.artifacts/feasibility-20260921/tester/D-device/RESULT.json) 和 tester 的 [raw pair log](../.artifacts/feasibility-20260921/tester/D-device/raw-final-pair.log)、[raw-encrypted pair log](../.artifacts/feasibility-20260921/tester/D-device/rawenc-final-pair.log)。[D 接收路径审计](../.artifacts/feasibility-20260921/D-receive/RECEIPT.md) 是 source/ELF 分析，不替代设备交付证据。本轮 native pressure qualification 未完成，selected=0 cancellation 仍 `NOT_EXERCISED`。
- NEED_ACK 配置准入：对 BYTES 的 `SocketBeforeBind`/`SocketBeforeListen`、level `1`、id `4`，显式 `false` 和 `true` 均在调用 SDK 前被拒绝，适配器结果为 `UnsafeNeedAckBeforeReady`，SDK 调用状态为 `NotCalled`，未产生 SDK rc；省略 setter 与显式 false 分开覆盖。Debug/Release 各 `14/14` 通过。该项是 adapter 内可修正的配置问题，不代表 SDK 锁保持风险已解决。
- 设备版本绑定：两板 OpenHarmony 6.1.0.31；客户端 SDK API 23/6.1.0.32，板端 `libsoftbus_client.z.so` SHA-256 为 `9b431d...e6087`，完整库/Build ID 见 [native path audit](../.artifacts/breakthrough-20260920/native-path/findings.md)。这仍是设备探针证据，不是生产 QualificationProfile。
- 权限清理：本轮 A/D 新鲜运行的 A/B before/after guard 均为 `PASS`，字节快照匹配，撤权后的 owned `DATASYNC` 为空且同 token manager readback 为 `DENIED`。历史 `final guard` 仍保留 `Passed=false / NON_PROJECT_CHANGED`，不能改写为历史 guard PASS；Sep14/V4 历史差异不归因于本轮，也未用整份系统 JSON 覆盖恢复。范围收据见 [v4-current-investigation-scope.json](../.artifacts/breakthrough-20260920/device-waits/run-20260921/v4-current-investigation-scope.json)。

## 取消结果与边界

BYTES 60000×4096、hold 4500 ms 的追加运行中，客户端 invocation 进入 `938007 us`，Shutdown 请求 `1138198 us`，实际 Shutdown 调用进入 `1138209 us`、返回 `1139759 us`，发送返回 `4538256 us`。因此 SDK Shutdown 调用耗时约 `1550 us`，请求到 Shutdown 返回约 `1561 us`，请求到发送返回约 `3.400058 s`，仍在 1 秒诊断预算下 `FAIL`。另一组 hold 6000 ms 的 invocation 进入 `945122 us`，请求 `1145235 us`，Shutdown API 耗时 `1161 us`，发送返回 `5950357 us`，请求到发送返回约 `4.805122 s`，同样 `FAIL`。这些结果只反驳被测配置的 1 秒候选预算，不能推出所有有限时限都不成立。`-427229183` 为 `SOFTBUS_CONN_KERNELS_BASE`；它与目标初始 5 秒等待分支相符，但不能单独证明唯一原因。源码与目标 ELF 还显示 partial-progress loop 没有累计总截止时间；详见 [native path audit](../.artifacts/breakthrough-20260920/native-path/findings.md)。

RAW 和 `RAW_ENCRYPTED` 的早期 4096 轮结果仍为 `NOT_EXERCISED`。V4 运行已在三个栈样本中确认目标 native pressure：`pc 0x41e6c` 位于 FillP native buffer retry 路径；64 次 `SendStream` 均返回 0，instrumented 最大调用约 `85616/85688 us`，普通 Shutdown 约 `53518/52371 us`，worker 均 join。但这只证明 pressure/path 已触发；取消 oracle 没有 selected in-flight target，仍为 `NOT_EXERCISED`。RAW 和加密 RAW 各只观察到 `1/64` 接收记录，不能写成 64 条完整交付；客户端 SDK 调用成功不等于 receiver 已收到全部记录。V4 的栈采样会扰动时序，只作路径证据，不作正常时序资格证明。每条已观察记录仍需按长度和内容解释；不能增加 adapter 拆包、补发或把正返回统一解释为短写。

源码和设备 ELF 调查保留以下未资格化边界：Bytes/TCP_DIRECT 的等待可能按每次等待 5 秒重试而无总发送截止时间；native fd 的最终释放可能延后到发送引用归零；RAW 的 `FtSendFrame` 在特定部分发送路径可返回正的短计数，而上层只检查 `-1`，但当前设备运行尚未观察到截断 receiver 记录。边界实验只证明六个尺寸的完整记录，不能建立安全上限；2 MiB 可保留为 adapter 保守上限候选，但不能在未完成资格验证时写成已合格值。

本轮 C 收敛见 [退出机制证据](../.artifacts/feasibility-20260921/C-mechanism/RECEIPT.md)：BYTES 初次 5 s wait 的到期返回与部分进展后的零就绪/EAGAIN 循环不同，后者没有累计截止时间；本地 Shutdown 写 needRelease，但 active send 持引用时不直接关闭 fd，真实发送返回后才有引用归零机会。服务 IPC 可能间接促成退出，当前没有证据保证其覆盖全部所需故障。RAW 缓存重试依赖 CanSendData/core 进展；正常关闭先取得 stream 锁，再进入 FtClose 的消息入队与同步完成等待。原有自然返回和普通关闭仅是对应运行的观测，不是机制上界或真实在途关闭证明。完整资源结算仍是另一未决命题。

因此本轮没有再改变 BYTES hold 或以“观测最大值 + 余量”设资格阈值，也没有把 RAW selected=0 改判取消成功。下一项能够改变有界性结论的证据，是针对指定 channel/故障集合的公开总 deadline、可验证的公开取消机制，或能覆盖上述循环/关闭依赖的受限平台契约；单纯增加相同实验次数不能替代它。RAW 强背压后的交付窗口修正独立进行，不依赖先将取消能力判 PASS。

## 本轮 Review、只读核查、权限和未验证项

本轮独立设计 review 为 [REVIEW.md](../.artifacts/closure-20260921/review/REVIEW.md)，只读 evidence audit 为 [RESULT.md](../.artifacts/closure-20260921/verification/RESULT.md)，两者均为 PASS（限定范围），不构成生产批准、资格结论或 commit gate。只读核查记录五个 ELF 反汇编检查均 exit `0`、相关 client/utils/adapter ELF hash 与既有双板 inventory 一致、相关旧源码/头文件 hash 与既有记录一致，并完成本文与实施方案的 56 个相对 Markdown 链接审计（56 存在、0 缺失）。本轮 build/host/device 均 `NOT_RERUN`，没有 HDC、构建或 host 套件执行；历史 D 交付窗口 `64/64` 与 V4 `1/64` 不拼接为压力资格，`NEED_ACK` 的 `NotCalled` 保留。

独立 review 记录包括历史 P0 review [`.artifacts/implementation-20260920/p0-review/REVIEW.md`](../.artifacts/implementation-20260920/p0-review/REVIEW.md)，以及当前切片的 [configuration closure](../.artifacts/breakthrough-20260920/review-config/REVIEW.md)、[probe closure](../.artifacts/breakthrough-20260920/review-probe/REVIEW.md)、[server admission closure](../.artifacts/breakthrough-20260920/review-probe/SERVER_ADMISSION_CLOSURE.md)、[observer review](../.artifacts/breakthrough-20260920/review-observer/REVIEW.md)、[A review](../.artifacts/feasibility-20260921/review/A.md)、[NEED_ACK review](../.artifacts/feasibility-20260921/review/NEED_ACK.md)、[D review](../.artifacts/feasibility-20260921/review/D.md) 和 [本轮 D Debug host receipt](../.artifacts/feasibility-20260921/tester/D-final-host/Debug/ctest.log)。这些 review 覆盖当前新增纯逻辑/诊断切片，没有宣称 native integration 或生产资格；早期失败记录、初始环境变量问题和较早的设备结果均保留，未改写为 PASS。

尚未验证：生产 native backend、Fast DDS locator/discovery 集成、SPDP/SEDP、Writer/Reader、History/reliability/fragmentation、混合 UDP/TCP/SHM、DDS Security 组合、同进程/跨进程多 Participant、多 Reader、重连、进程崩溃隔离、资源预算、BLE/BR、三板并发和 RMW。三板 A→B/C 及多设备公平性为 `DEFERRED`。

## 当前放行判定

| 项目 | 状态 |
| --- | --- |
| 诊断实现存在 | PRESENT |
| 构建 | PASS |
| 主机测试 | PASS |
| 双板诊断测试 | LIMITED；含 BYTES FAIL、RAW NOT_EXERCISED |
| P0 平台资格 | BLOCKED / NOT QUALIFIED |
| Fast DDS 集成 | NOT RUN / BLOCKED |
| 生产发布 | NOT READY |
| PRE_COMMIT | FAIL |

当前没有可提交的生产 Transport。按 P0-P6 阶段推进，各配置在相应平台前提未满足时保持 BLOCKED；发放生产资格前必须完成 SDK 发送/关闭/短返回及 adapter 隔离/排空/预算验证，不能把“SDK Shutdown 后无迟到 callback”当作已成立前提。

## 工程推进状态与平台资格状态

本轮设计修订及唯一 C 定向核查完成后关闭可行性收敛工作包；不是关闭 P0 的失败或未决命题。重新开启只接受 PF-01..04 列出的具体机制、平台契约、可判别 trace 或明确用户决策，且只重开对应问题。此前 P1/P2/P3 的原始证据保留，但不再因同一阻塞自动扩展框架或全面探针。P4、生产注册与 DDS 集成仍未授权；三板继续 DEFERRED。

固定的 callback incarnation slot 规则已经确认：slot 只约束 native callback table incarnation，退休后永久 tombstone，热重建优先复用仍存活且兼容的 native InputChannel；它不是 Participant/连接累计配额，也不解决 numeric ID 重用、SDK 内部事件归属或 native 阻塞。本报告不以新增 Participant/连接次数限制掩盖这些缺口；该规则的真实公开回调验证仍属 P2/P0 边界。

## 既有细分命题与原始证据（受上方收口规则约束）

以下条目区分 `PROVED`、`REFUTED`、`CONDITIONAL` 和 `UNRESOLVED`。`SOURCE/ELF` 结论不是设备运行资格；`NOT_TRIGGERED` 不是 PASS。通过条件是后续资格化判据，未满足前对应工作保持受限。

| 阻塞命题 | 适用版本/profile/config 与当前证据 | 尚缺证据与下一实验/检查 | 判定条件与实际影响 |
| --- | --- | --- | --- |
| BYTES 在关闭请求后 1 秒内退出 SDK 发送 | SDK API 23/6.1.0.32，板端 6.1.0.31；BYTES 60000×4096、hold 4500/6000。同进程单调时钟 request→send-return 为 3.400058 s、4.805122 s，连同原始 1.393675 s，反证 **1 s 候选预算**。`REFUTED`；Shutdown API 本身约 1.550/1.161 ms。 | 原始失败保留，不通过增加阈值改判。下一项为下行退出机制审计，不再只改变 hold 时长。 | request→SDK send-return 超过 1 s 即 FAIL；当前该候选配置失败，阻塞相应发送/取消资格。未触发实际在途发送不能判 PASS。 |
| BYTES 在明确运行及故障范围内存在其他有限发送退出上界 | 同版本 TCP_DIRECT 路径。目标 ELF/source 显示初次 wait 为 5 s；partial-progress 分支中零就绪/EAGAIN 可继续，未见累计 deadline；Shutdown 标记释放但活跃引用延迟 fd close。**UNRESOLVED**，不能从 1 s 失败推导所有有限上界均不存在。 | 沿目标库检查循环退出条件、允许的故障轨迹、关闭唤醒及公开异步路径。若存在具体可持续等待轨迹，绑定该配置/故障范围形成外部能力依赖；不以观测最大值加余量作为机制上界。 | 有故障范围、退出机制及对应设备验证才可支持有限上界；不触发目标条件为 NOT_EXERCISED。阻塞 BYTES native worker 有界回收资格。 |
| SDK 返回与 worker join 后，callback、发送引用及 native 资源已经完整结算 | 同版本三种 profile；已有 Shutdown entry/return、部分 worker join 和 ref-count 路径，缺少完整结算链。**UNRESOLVED**，并未被 1 s 失败单独反证。 | 分开关联 admission、SDK send-return、Shutdown-return、callback drain、worker join、引用归零和 fd release。当前检查关闭唤醒/引用归零路径；不借跨进程或跨板时钟直接相减证明因果。 | 每项适用资源都有结算机制和实机证据才可解除。watchdog 杀进程、SDK 返回或 join 本身均不是完整结算证明。阻塞 P2/P3 生命周期资格及 P4；不阻塞纯逻辑证据。 |
| RAW 背压作用到目标 native 路径，且底层调用具备可证明的终止或自然有界返回 | RAW/RAW_ENCRYPTED 2 MiB 候选负载，V4 三个栈样本命中 FillP native buffer retry（`pc 0x41e6c`）；64 次调用均 rc=0，最大 instrumented 调用约 85.6 ms，普通 Shutdown/worker 也有记录。压力/path 为 `PROVED`；可靠终止或未扰动配置下的有界自然返回仍 `UNRESOLVED`，取消仍 `NOT_EXERCISED`。 | 两条可接受路径任选其一并分别验证：实际阻塞调用被可靠终止；或有效 pressure 下未扰动调用始终有界返回。两者都要另行验证 callback drain、worker/resource settle；栈采样不能作为正常时序证据。 | 具备上述任一路径、完整 invocation/return/settle 证据才可解除。当前阻塞 RAW profile 资格和 P3 native 调度，不阻塞 RAW host 状态机。 |
| RAW 不会把短写当完整消息或向 receiver 交付截断数据 | 同 SDK/固件；ELF 发现 `FtSendFrame` 特定部分发送路径可返回正短计数，而上层只检查 `-1`，这是 `CONDITIONAL` 可达风险，不是每个正返回都短写。边界测试 raw 2097151/2/3、encrypted 2097123/4/5 各两条逐长度逐字节通过，反证 SDK 在候选 M 的硬上限（不反证 adapter 保守限制）；尚未观测截断 receiver。 | 分开交付与关闭实验：交付组解除压力后保持通道存活到预定义窗口结束；关闭组如实计缺失。用 run/sequence 及身份派生内容核对逐条长度、全 payload、缺失/重复与跨记录边界；不只等短 callback。禁止加入 adapter stream framing/resend。 | SDK 隐藏短计数时只报告公开返回和 receiver 事实，不伪造外层完整性判断。长度/字节/身份/边界不一致即完整性 FAIL；关闭后的缺失另记交付结果。阻塞 RAW 生产资格及候选 max_message_size 的资格认证，不禁止先选择保守配置值。 |
| 每个 Socket 的回调表在 Bind 后独立固定 | **REFUTED（本轮 A 实机，BYTES 正常 OnBytes）**。B 同进程同 local service、同 client 角色的 socket 1/2 对应 A 两个 peer slot；基线 socket1→F1，Bind2 后 socket1/socket2/socket1→F2/F2/F2。四条独立记录逐长度/身份/内容通过，两侧 remote exit 0，关闭发生在观察完成后。产物 `3605e2d3…12ee046`，见 [A 单次运行](../.artifacts/feasibility-20260921/tester/A-device/pair-run.log)。 | 本命题已裁决，不重复健康 echo。表的真实键为本地 SDK service record + client/server 角色；下一项是共享表固定 incarnation 候选及独立的 native 事件身份缺口。 | 该结果支持共享表模型，反证 per-Socket 固定表；不把正常 BYTES OnBytes 观察扩大为所有回调/profile 的实机验证。可在 adapter 内修正入口绑定粒度；不代表整个身份链、关闭排空或热重建合格。 |
| 共享表固定 incarnation 与 socket owner 足以安全处理所有 native 事件 | 最新用户消息已批准每个实际 service record/role 表一固定入口、各 socket 独立 native owner。**批准仅限入口绑定设计**；原 11 个 ingress 用例是单 owner fixture，不能证明共享表实现或完整 native 事件身份。 | 仍需覆盖 OnBind/OnBytes/OnStream/OnShutdown/Error 的 event→channel pair→socket→表→owner 全链及表固定时点，区分表覆盖问题、内部 channel 复用和 application 热重建。 | 可修正表覆盖不等于身份链/排空/热重建合格；真实签名不提供 generation/context，host fake 不得补造。阻塞 P2/P4 native ingress 资格。 |
| adapter 记录归属线性化后不会被新 application 重新查表投递 | P1/P2 host ingress 诊断已证明 capture old application+receiver 后旧记录不改投新代；no-app 记录终结丢弃；这是 `HOST_TESTED`。SDK 尚未证明 adapter ingress 之前的内部事件原始归属。状态 `CONDITIONAL`。 | 用真实 callback 入口确认 capture 线性化点，覆盖 old receiver lease、重建交错和 callback return 后排队；分别记录 adapter 已捕获与仍在 SDK 内部的事件。 | 已捕获记录不得改投新代，未归属记录不得缓存给未来代；native 内部不可见部分仍保留资格缺口。阻塞 P2/P4 热重建证明，不阻塞 host P1/P3。 |
| public socket counter 溢出与存活进程内 socket ID 重用可区分 | 目标客户端库分配器的 signed overflow 分支调用 UBSan abort；这是目标构建的溢出行为证据，**不是存活进程内正常回绕/重用证据**。分配域包含同进程其他 SDK 使用者，MDDS 连接计数不能替代它。 | 审计实际 allocator 调用者和释放路径，独立判断是否存在其他 live-process ID 重用路径；不以强制触发巨大计数溢出为本轮目标。 | 只在已核对路径范围内下结论，不新增 Participant/连接累计配额。即使公开 ID 不复用，也不能推出 channel/event 身份安全。 |
| 内部 channelId/channelType 在分配、复用与事件分派全过程保留原 owner | 当前双板 service ELF `96da229b…401c8d` 已确认 UDP timeout 在 `0x47478c` 释放 ID、`0x474800` 解锁后，才在 `0x474908/0x47493c` 发送旧通知；代理采用 TF_ASYNC。目标 client 的普通 UDP close 在内层 guard 前先按当前 channelId 取得对象并写 `isClosing=true`；OpenFailed 也先清理当前资源再判断 OnError 状态。故这些后置 guard 不能单独充当 epoch 防线。完整误归属仍 **CONDITIONAL**。 | 缺少“旧客户端映射已移除→service 释放→旧通知发送前同 client 新 pair 安装”这一完整 IPC/协商轨迹的可达性，或能排除它的确定屏障。下一证据应为该区间的平台 trace/契约；重复健康 echo 不能裁决。详见 [A 身份链审计 §6](../.artifacts/feasibility-20260921/A-chain/A-chain-audit.md)。 | 该条件轨迹一旦成立，原生资源可能在 adapter ingress 前已被错误作用，固定入口无法补回原身份。当前只是 UDP 控制事件的外部能力依赖，不宣称板端已复现误投，也不扩大到全部 RAW/TCP 或混合 transport。阻塞相应 P2/P4 native 身份资格。 |
| 公开选项/异步 API 提供发送或关闭上界 | SDK headers/exports 中发现 BindAsync、SendBytesAsync、Get/SetSocketOpt、QOS_MAX_WAIT_TIMEOUT；审计显示 SendBytesAsync 仍走同步发送路径，ACK 是后续 bookkeeping，QOS 仅 Bind wait，未找到公开 send-cancel/deadline/drain API。状态 `PROVED`（能力边界），行为资格 `UNRESOLVED`。 | 对每个实际配置核对头文件、导出符号、权限、调用阶段和 effective value；保留真实 rc。NeedAck unsupported branch 的锁保持问题已有 target ELF/source 证据，不能静默忽略。 | 只有实际改变目标调用阻塞/关闭并有设备时限证据才算 PASS；当前阻塞 descriptor option qualification/P0，不阻塞保存选项的纯配置模型。 |
| NEED_ACK option 在 unsupported 分支不会破坏其他 SDK 线程 | 原始 BYTES CREATED、SUPPORT_ACK=false 的 `SetNeedAck(true)` 返回 `-426114808`，另一线程约 1.5 s 不返并被 watchdog 124 终止；原始 FAIL 保留。当前目标 recursive global mutex 的锁保持分支也适用于显式 false。 | 本轮已在现有纯配置校验中拒绝 BYTES 的 SocketBeforeBind/SocketBeforeListen、level1/id4；true/false 均返回 adapter `UnsafeNeedAckBeforeReady`，不删除请求，不进入 SDK，不伪造 SDK rc，证据记录 `NotCalled`。省略 setter 与显式 false 独立测试。Debug/Release 各 14/14，review 后复测 exit0，见 [NEED_ACK 证据](../.artifacts/feasibility-20260921/need-ack/IMPLEMENTATION.md)及 [审查](../.artifacts/feasibility-20260921/review/NEED_ACK.md)。 | **ADAPTER 内可修正的配置准入问题**；本轮不重复触发已知锁保持。其他 profile/阶段只保留静态请求，仍 NeedsPlatformQualification；guard 针对本次固定目标，不自动识别运行时 ELF，未来 SDK 迁移须重审。该拒绝不修复 SDK、不提升生产资格。 |
| 当前公开 local networkId 可用于同设备 native Socket 拓扑 | 既有三种 profile 的同进程/跨进程 self Bind 均返回 `-426049516`（SOFTBUS_NETWORK_GET_NODE_INFO_ERR）。本轮 B 当前 service ELF 确认 peer UUID 查询门在 channel/lane 建立前退出，并可产生该精确错误；key3 不由 vendor key24/132 扩展替代，DFX 查询不是失败门。历史运行缺精确返回 PC，具体归因仍为机制支持的推断。见 [B 目标路径](../.artifacts/feasibility-20260921/B-path/B-path-receipt.md)。 | 尚无受支持的公开 self 地址/选项或适用平台能力。只有供应方提供此能力/版本契约才值得新实验；重复 local-ID Bind、猜 `0/99/null/loopback` 均不能改变结论，本轮未执行。 | 该 native self 路径在当前已测配置不可用；adapter 无法修正远端 ledger 解析。**不反证 DSoftBus 与原生 UDP/TCP/SHM 混合共存**，也不改变同机采用 Fast DDS 原有能力的决定；混合集成仍需以后独立验证。 |
| 版本匹配足以生成生产 QualificationProfile | SDK API 23/6.1.0.32、固件 6.1.0.31、板端库 hash/Build ID 已记录，源码与 vendor binary 仍非同一可追溯构建；当前目标 ELF 的 send/recv 常量可支持 50 MiB 级检查，但这不是安全 `max_message_size`。Host 35/35 只证明切片。状态 `UNRESOLVED`。 | 固定 SDK、固件、四个库 hash、probe artifact、权限、profile、容量、拓扑、时限、run IDs；按实际 receiver/发送边界完成资格实验后才确定 M。 | 未满足完整版本绑定和 native 行为矩阵时不得生成 QualificationProfile；SDK 的 2 MiB 硬上限假设已反证，adapter 保守 M 未被反证；不能据 50 MiB 常量宣称已合格 M。阻塞生产发布和 P4/P6 release gate；前轮仅继续 A→B→C→D 裁决所需的最小代码和证据，本轮不构成新授权。 |

## 前轮新增切片与历史复现入口（保留记录，不构成本轮授权）

- 本轮 Host 的 Debug/Release 独立构建目录分别为 `.artifacts/feasibility-20260921/tester/D-final-host/Debug` 和 `.../Release`；对应 tester CTest 日志为 [Debug](../.artifacts/feasibility-20260921/tester/D-final-host/Debug/ctest.log) 与 [Release](../.artifacts/feasibility-20260921/tester/D-final-host/Release/ctest.log)。以下命令使用已有 CMakeCache，可复现本轮配置：

  ```powershell
  $env:PSModulePath = [Environment]::GetEnvironmentVariable('PSModulePath', 'Machine')
  cmake --build .artifacts/feasibility-20260921/tester/D-final-host/Debug --config Debug --target ALL_BUILD
  ctest --test-dir .artifacts/feasibility-20260921/tester/D-final-host/Debug -C Debug --output-on-failure
  cmake --build .artifacts/feasibility-20260921/tester/D-final-host/Release --config Release --target ALL_BUILD
  ctest --test-dir .artifacts/feasibility-20260921/tester/D-final-host/Release -C Release --output-on-failure
  ```

  实际 Debug/Release build exit `0`，CTest 各 `35/35`、exit `0`。旧 `breakthrough-20260920/final-host-server-admission` 命令和日志仍保留为历史证据，不作为本轮 canonical。P1 address/config/legacy、P2 ingress/observer/admission、P3 options/local/settle 均仅作累计 host/diagnostic 状态。
- native probe compile-only 命令（使用仓库已固定的 API23/AArch64 clang、平台 `__h` 头文件及 manifest 中的库快照）为：

  ```powershell
  powershell -NoProfile -File scripts/BuildCancelProbe.ps1 -OutputDirectory .artifacts/breakthrough-20260920/reproduce-native
  ```

  该命令只编译，不部署、不写权限、不调用 SDK；成功退出 `0` 才产生新的 native probe manifest。当前 canonical native 编译 manifest 与 artifact hash 见上文，设备运行不是该 compile-only 命令的结果。
- RAW 边界：使用 `.artifacts/breakthrough-20260920/device-boundary-local/RESULT.md` 中列出的六种 record-bytes，逐条比较 callback length/content；六项 PASS，取消 oracle 六项均 `selected=0 / NOT_EXERCISED`，远端 B exit 2 不能解释为取消成功。
- Native exit：Bytes 追加 probe 的 invocation/Shutdown/return 记录见 `.artifacts/breakthrough-20260920/device-exit/ebytes4500-summary.json` 与 `device-waits/run-20260921/bytes6k-summary.json`；RAW V4 命令和 stack capture 见 `device-waits/run-20260921/raw4k5-summary-v4.json`、`rawenc4k5-summary-v4.json`。PowerShell/设备复现使用上方的 canonical host build/CTest 命令和 [BuildCancelProbe.ps1](../scripts/BuildCancelProbe.ps1) API23/AArch64 compile-only 命令；完整设备命令保留在上述 JSON，避免把私有 networkId 写入报告。栈采样的 `PTRACE_PERTURBS_PROCESS` 只用于确认目标调用链，不作正常 timing qualification。
- A callback-table probe 的源码编译入口为 [BuildCallbackTableProbe.ps1](../scripts/BuildCallbackTableProbe.ps1)；该脚本是 compile-only，成功退出码为 `0` 才生成 manifest。双板运行的实际 A/B 命令、目标和两侧 remote exit `0` 见 [A commands](../.artifacts/feasibility-20260921/tester/A-device/COMMANDS.json) 与 [A pair run](../.artifacts/feasibility-20260921/tester/A-device/pair-run.log)。
- 本轮 D 交付窗口的实际 A/B 命令已由 tester 恢复并写入 [D RESULT](../.artifacts/feasibility-20260921/tester/D-device/RESULT.json)，并由 [raw pair log](../.artifacts/feasibility-20260921/tester/D-device/raw-final-pair.log) 与 [raw-encrypted pair log](../.artifacts/feasibility-20260921/tester/D-device/rawenc-final-pair.log) 关联验证。
- 二进制/source 审计：[native-path/findings.md](../.artifacts/breakthrough-20260920/native-path/findings.md) 和 [callback-audit/audit.md](../.artifacts/breakthrough-20260920/callback-audit/audit.md) 绑定当前 boards/library hashes、公开 callback 签名、service-name table overwrite、ID 分配域及可达时序。它们支持阻塞命题和下一实验，不替代设备完整资格。

当前工作树新增实现/诊断入口包括：[V1Address.hpp](../include/mdds/dsoftbus/V1Address.hpp)、[V1Configuration.hpp](../include/mdds/dsoftbus/V1Configuration.hpp)、[softbus_ingress_contract.hpp](../probes/softbus_ingress_contract.hpp)、[softbus_callback_table_probe.cpp](../probes/softbus_callback_table_probe.cpp)、[softbus_cancel_probe.cpp](../probes/softbus_cancel_probe.cpp) 和 [NativeWaitObserver.cjs](../scripts/NativeWaitObserver.cjs)。这些文件分别支持纯逻辑 codec/config、ingress 契约、callback table 诊断、native cancellation/交付窗口诊断和受控观测；它们不是生产 backend，也不改变 P0 资格结论。

## 证据判定和未完成工作

既有已证实结果：Host Debug/Release 各 35/35；locator/config/ingress/observer、callback-table oracle 和 NEED_ACK 准入等纯逻辑切片通过其限定契约测试；六个 RAW 边界尺寸各两条完整逐字节接收；BYTES 1 秒候选配置失败；前轮 D 独立交付窗口 raw/raw-encrypted 各 64/64。A 已证明同名 service/role 表会被后一次 Bind 覆盖。本轮不重跑以上测试，新结果仅是共享表设计修订、需求来源核对和 TCP timer 的目标机制证据。

已反证：本构建在 2 MiB/2 MiB−28 的 SDK 硬上限假设不成立，adapter 保守上限候选并未被反证；同名 service table 不可假定每个 live socket 拥有永久独立 callback table；BYTES 的 1 秒候选预算不成立。NEED_ACK 的已知危险配置可在 adapter 配置准入处拒绝，但该拒绝不修复 SDK 的锁保持或发送关闭契约。

剩余事项按责任收口：**adapter 可修正**的是已批准的共享表绑定设计及已有 NEED_ACK 准入拒绝，前者生产实现未开始、后者已有主机证据；**平台前提待闭合**的是 PF-01 退出机制、PF-02 作用前身份与连续性、PF-03 RAW 压力完整性/关闭、PF-04 保留的 native self-route；**后续集成 NOT_RUN** 是 Fast DDS discovery、Writer/Reader、可靠性/分片、混合 transport、安全、多 Participant/Reader/进程和性能，三板为 DEFERRED。后续集成不是一概要求供应方解决，也不由主机模型 PASS 替代。P4 未授权，P0 与受影响生产路径继续 BLOCKED；本轮关闭后仅凭具体新机制、平台契约、可判别 trace 或明确用户决策重开对应问题，不自动安排下一轮全面探针。

## 关键源码锚点

- [DSoftBus socket public header](../communication_dsoftbus/interfaces/kits/transport/socket.h)：公开 Socket、BindAsync、SendBytesAsync、listener callback 签名。
- [DSoftBus transport types](../communication_dsoftbus/interfaces/kits/transport/trans_type.h)：`QOS_MAX_WAIT_TIMEOUT`、Socket option/QoS 类型和公开结构。
- [BYTES send implementation](../communication_dsoftbus/core/transmission/common/src/trans_tcp_process_data.c)：当前 source 对完整加密记录长度的检查。
- [TCP direct send/close path](../communication_dsoftbus/sdk/transmission/trans_channel/tcp_direct/src/client_trans_tcp_direct_manager.c)：发送引用和 close/needRelease 路径。
- [RAW stream send path](../communication_dsoftbus/components/nstackx/fillp/src/app_lib/src/socket_app.c) 与 [VTP adaptor](../communication_dsoftbus/sdk/transmission/trans_channel/udp/stream/adaptor/src/client_trans_udp_stream_interface.cpp)：正短返回和上层转换线索。当前设备 ELF 精确偏移和版本绑定见 native path audit，不能用公开 source 替代 vendor 资格。
