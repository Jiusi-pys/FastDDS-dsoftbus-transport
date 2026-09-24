# Project status

## 当前工作包：DDS bounded performance baseline（2026-09-24）

`DDS_BOUNDED_PERFORMANCE_BASELINE` 已达到 **`BOUNDED_PERFORMANCE_BASELINE_MEASURED / CHECKPOINT_READY / NOT_COMMITTED`**。这是限定 `BYTES_NATIVE`、两块 RK3588A、6 个固定配置、3 个独立 MEASURE 重复的性能测量基线，不是生产资格或最大吞吐量声明。正式 RTT 共 3,300 次；固定负载 Reader delivery 共 6,120 次（RTT full-echo 的 Reader delivery 不计入该固定负载合计）。合并 p50/p99 为 RTT_1K `3.219125/4.435375 ms`、RTT_4K `3.226709/4.408250 ms`、RTT_256K `11.085668/14.135335 ms`。固定负载 Reader business goodput 为 1K `0.09765625 MiB/s`、4K `0.390625 MiB/s`、256K `1.00000000 MiB/s`。

最终 data review 为 PASS 且无未解决 blocker；独立 host post-review 为 OFF `243/243 PASS`、ON `245/245 PASS`；`49/49` 是 TRACE 修复前的历史 full-host stage，focused R3 的最终 analyzer 为 `58/58 PASS`。独立双板 receipt 为 `PASS_WITH_PRESERVED_TRACE_FAILURE`：query、失败 TRACE01、成功 TRACE02、M1/M2/M3 共 12/12 final board guards PASS；TRACE01 的失败、完整 charge 和 `RESERVED_UNKNOWN` ledger 保留，并排除正式统计。Native package 为 1,737 inputs、6 artifacts，focused R3 executor checks 为 PowerShell 4、Python 10、Node 14，package/input hashes 保持一致；native executable SHA-256 为 `5f98207268441debefcfaa57f0396a7be0d1cc2ed9d40e98e49f11211b30a440`。

测量限制已保留：19,116 行 CSV 是 planned-identity rows，不是 deliveries；SDK internal settlement、exact queue HWM 为 `UNOBSERVED`。仅 M3/A 的两个累计 `NO_APPLICATION` rejected records（不是四个）保留 payload/Writer identity 与 control-versus-business classification 未观测状态；每侧 TX reject 为 2，RX prefix overflow 只说明诊断细节受限，不证明业务丢失。该状态支持 `IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED / REVIEWED` 的限定性能基线证据，不升级为生产 `VERIFIED`。P0/PF-01/PF-02 仍 `BLOCKED / NOT QUALIFIED`，生产默认 `NativeUnavailable`，P4 未发布，无 QualificationProfile；禁止 stage/stash/commit/amend/push。完整结果见 [RESULT_TABLES.md](../.artifacts/dds-bounded-performance-20260924/run-20260923T183702Z/analysis/RESULT_TABLES.md)、[FINAL_DATA_REVIEW.md](../.artifacts/dds-bounded-performance-20260924/run-20260923T183702Z/review/final-data/FINAL_DATA_REVIEW.md) 与 [device receipt](../.artifacts/dds-bounded-performance-20260924/run-20260923T183702Z/device/CANONICAL_INDEPENDENT_DEVICE_ALL_ATTEMPT_RECEIPT.json)。

资源观察为单进程 single-core CPU 每 case `3.293%–12.126%`、Participant 生命周期跨度 `85.656–86.118 s`、采样 RSS 最大 `29.824 MiB`、observed VmHWM 最大 `30.133 MiB`；这些是边界观察，不是长期泄漏或 SDK internal settlement 证明。

## 历史已关闭工作包：DDS multi-endpoint and late-joiner functional matrix（2026-09-23）

`DDS_MULTI_ENDPOINT_AND_LATE_JOINER_FUNCTIONAL_MATRIX` 已达到 **`DDS_NORMAL_FUNCTIONAL_MATRIX_VERIFIED / CHECKPOINT_READY / NOT_COMMITTED`**，现作为历史已关闭工作包保留。13 个限定 `BYTES_NATIVE` 双板窗口全部通过独立复核：M1/M2/M3/M4 多端点与 Reader 删除、Q_BE_BE/Q_REL_BE/Q_REL_REL/Q_BE_REL、L1/L2/L3 晚加入以及 I1/I2 Topic/Domain 隔离。canonical matrix SHA-256 为 `3f51d3a4c5031e0b016d328497b1bae47b4d06082862b7229557fd3c6f4a8259`；结果为 102 次写入、132 条独立 Reader 交付/native 关联，详见 [matrix execution results](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/analysis/MATRIX_EXECUTION_RESULTS.json) 与 [final review closure](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/review-final/REVIEW_CLOSURE.md)。

独立 host 最终为 OFF `175/175 PASS`、ON `177/177 PASS`。四个受影响回归（hot1、1 MiB fragment、RELIABLE controlled-loss、BEST_EFFORT controlled-loss）均通过；另有 1 个 Socket-free directory query，本轮共 13 个矩阵窗口 + 4 个 native 回归窗口 + 1 个 query 窗口 = 18 个注册窗口，其中 17 个为实际 DDS native 窗口，36/36 板端 full guards。选定 native OFF/ON 包分别由 [BUILD_SELECTION.json](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/device-build/BUILD_SELECTION.json) 绑定，每包 1,693 inputs、6 artifacts。实际 QoS、匹配图和逐 Reader 账本见 [ACTUAL_QOS_MATCH_GRAPH_READER_LEDGERS.json](../.artifacts/dds-functional-matrix-20260923/run-20260923T094900Z/analysis/ACTUAL_QOS_MATCH_GRAPH_READER_LEDGERS.json)。

本状态仍不构成生产资格：P0、PF-01/PF-02 为 `BLOCKED / NOT QUALIFIED`，生产默认 `NativeUnavailable`，P4 未放行，无 QualificationProfile，SDK internal settlement 为 `UNOBSERVED`，禁止 commit/push。旧 [restored-package-device 交付线](../.artifacts/restored-package-device/run-20260923T075212Z/FINAL_GATE.json) 已正式关闭，本轮未重开。L2/L3 的晚加入证据未形成新的 no-application gap 或 PF-02 物理旧帧隔离证明；RAW、三板、mixed transport、ROS 2/RMW、性能及新的 100 轮资格均未执行。上一轮 RX closeout 以下内容保留为历史记录。

## 历史已关闭工作包：DDS keyed history and resource boundaries（2026-09-23）

`DDS_KEYED_HISTORY_AND_RESOURCE_BOUNDARIES` 已达到 **`FINAL_CODE_AND_DEVICE_REVIEW_CLOSED / CHECKPOINT_READY / NOT_COMMITTED`**。独立最终 review 报告 `FINAL_CODE_AND_DEVICE_REVIEW_CLOSED`，授权范围内无未解决 blocker。K1、K2、K3、K4_TRUE、K4_FALSE、H1、H2、H3、R2、R3 十个 keyed native cases 均 `PASS`；R1 为 `HOST_ONLY_PASS / DEVICE_NOT_APPLICABLE`。canonical totals 为 62 writes、128 API data/take observations、58 unique data identities、42 taken identities、3 instance-state records、58 native data joins 和 3 native state joins；58 data + 3 state native associations 是本轮证据支持的范围。

最终 host receipt 为 OFF `205/205 PASS`、ON `207/207 PASS`，53/53 analyzer tests 与 12/12 post-R4 affected gates PASS；OFF/ON native packages 均 build PASS，每包 1,719 inputs、独立检查零 mismatch，设备仅执行 OFF package。14 registered windows（13 DDS attempts + 1 Socket-free directory query）对应 28 board windows，168 individual guard JSONs 全部 verified，28/28 board full guards PASS。K1-attempt-01 仅 outer launcher exit 为 `UNOBSERVED`，runner/remote exits 为 `0`，不计入 canonical totals；query 保留 82 per-command exit files all zero，但 aggregate launcher/runner exit 不推断为 PASS。Fresh small replay `87c0d1ef21b9257c` 和 fresh M2 `50ebee5e4a4e215a` 均通过其限定审计。

本轮观察仅适用于 fixed Fast DDS 3.6.2.0、generated bounded keyed type、`BYTES_NATIVE`、两块现有板卡、有限 history/resources 和已执行 cases。H1 的 4 个 oldest-before-Reader evictions 是 Writer `KEEP_LAST` 预期行为；H2 repeated reads 不等于 network duplicates；`valid_data=false` 未解析 payload。Reader `get_key_value` 与 Writer `lookup_instance` unsupported，standalone registration propagation 未实现；WriterDataLifecycle broad documentation warning 与 true/false narrow observed behavior 存在冲突，不能宣称全策略合格。SDK internal settlement 仍 `UNOBSERVED`。

资格边界保持不变：P0、PF-01/PF-02 为 `BLOCKED / NOT QUALIFIED`，生产默认 `NativeUnavailable`，P4 `NOT_RELEASED`，无 QualificationProfile；RAW、三板、mixed transport、ROS 2/RMW、性能、100 cycles 及全拓扑均未执行。本轮没有 Transport/core/FastDDS/SDK implementation changes；Git state 为 `NONE / NOT_COMMITTED`。完整证据、文件哈希和 checkpoint-ready 范围见 [keyed docs receipt](../.artifacts/dds-keyed-history-20260923/run-20260923T140748Z/docs/DOCS_SYNC.md)。

## Historical — RX accounting closeout（2026-09-22）

`HOT_REBUILD_BASELINE_CLOSEOUT_AND_RX_REJECTION_ACCOUNTING` 当前为 **DONE_WITH_HISTORICAL_LIMITATION / NOT_COMMITTED**。本轮证据与分析已完成；源码封存/恢复及最终 checkpoint 结论以本轮 `FINAL_REPORT.md` / `FINAL_GATE.json` 为准，本状态页不独立宣称恢复验证 PASS。

上一工作包的限定功能证据仍唯一定位为 `.artifacts/hot-rebuild-production-integration-20260921/run-20260921T152404Z/`：focused `23/23 PASS`，fresh OFF `132/132 PASS`（实验 93），ON `134/134 PASS`（实验 95），native build `0/0/0`，各 manifest 1,655 inputs、6 artifacts，20/20 final guards；其失败 attempt 与 1/10/100 结果均保持历史归属。本轮新证据统一定位为 `.artifacts/hot-rebuild-closeout-rx-20260922/run-20260922T012640Z/`：最终 host OFF `148/148`、ON `150/150`、analyzer `31/31`，post-review native OFF/ON build `0/0/0`，每包 1,657 inputs、6 artifacts，主程序 SHA-256 为 `6352b28f6b7f3d033bb8ecd58f5319eb4479cfe76d5d0ece461ed21487f1c0fc` / `dedfad61e0c26da6e9d7a5e26b60c21ab8db9130a573d4513a5223a8ce8f3a19`。

实现接线是 Participant facade → application/receiver/logical-token attachment → shared ProcessRuntime/ProcessCoordinator/ApplicationAttachments/InputChannels；每个 endpoint 具有独立 native State/listener/table/owner/worker。P1 detach 仅排空自身旧 lease、TX、RX 和 last-payload，保留 live native table；Q 不受影响；全部 application detach 后才 Finalize native stop/join/tombstone。TX 仍是原有有界 owned-vector 队列与 application 联合 drain，RX 是一次 copy 加原 lease；默认 setup=1，双 endpoint 显式 setup=2，预算为 `2/2/1`（setup/send/close）。

本 checkpoint 的资格边界未改变：P0、PF-01/PF-02 为 `BLOCKED / NOT QUALIFIED`；生产默认 `NativeUnavailable`，P4 未放行，无 QualificationProfile，commit/push 为 `USER_PROHIBITED`。OLD2 物理旧帧隔离、SDK internal settlement、RAW/三板/mixed/self-route/压力故障 close 未验证；full-native destroy/rebuild 是保留的历史 RED。1 MiB functional coverage PASS 但原 all-send predicate 明确 `FAIL`，16 条 active pre-Captured ingress 的具体接收前拒绝原因 `UNOBSERVED`；旧 listener terminal map 不保证数值为零，不能写成旧 current-map 日志必为零。

本轮 RX closeout 将历史 16 条限定为 217 条 native ingress 中的 16 条 ingress-only 记录：没有 Captured、owned-copy、长度/hash、Writer GUID/SN 或拒绝结果，具体原因 `HISTORICAL_CAUSE_UNRESOLVED`。新诊断记录在决定点捕获原始 adapter 状态和原因，固定容量为每 State 1,024 条、最大保留 8 个 service incarnation；未知字段、overflow 或 incomplete snapshot 不得被分析器归类为 harmless。新 1 MiB 窗口 `5923cd595106e8a6` 为 203 admissions / 0 rejects（`NOT_REPRODUCED`），hot-smoke `98e4d1228566a0a4` 为 144 decisions、141 accepted、3 `NO_APPLICATION`（严格 hot gap 2、final cleanup 1）。这些新窗口不倒填历史 16 条，也不证明 SDK 内部结算。

本轮诊断为 `DIAGNOSTIC_ONLY`；未发现需要修复的 Transport admission/lifecycle 缺陷。PF-01/PF-02 的外部契约材料见 [.artifacts/hot-rebuild-closeout-rx-20260922/run-20260922T012640Z/PLATFORM_CONTRACT_REQUEST.md](../.artifacts/hot-rebuild-closeout-rx-20260922/run-20260922T012640Z/PLATFORM_CONTRACT_REQUEST.md)。源码封存/恢复及最终 checkpoint 结论由本轮 `FINAL_REPORT.md` / `FINAL_GATE.json` 统一记录；不生成 QualificationProfile，不启用生产 Native backend，不执行 stage/commit/amend/push。

## Historical — same-name hot-reattach admission: blocked at STOP 0 (2026-09-21)

The requested same-process, same-display-name Participant hot-reattach pilot is `BLOCKED / NOT_COMMITTED`, not a completed pilot. The corrected admission treats OLD1 as a detach/drain barrier; OLD2/PF-02 and the full-native-record RED are nonblocking for a normal pilot. The actual first gate is `PHASE0_EXISTING_REAL_DDS_LIVE_INPUTCHANNEL_REATTACH_PATH`: real Participant deletion reaches `ExperimentalTransport::shutdown` and terminally stops that State, while the experiment has no existing bridge attaching a new Participant facade to a surviving live native InputChannel/table. This is a present-code integration/lifetime-binding gap, not a platform impossibility or a same-display-name/all-channel claim.

The earlier same-native-service reproducer remains a separate, nonblocking RED artifact and is not the admission premise. No new build, test, device, SDK or permission operation occurred in this packet. Prior host suites remain historical; current native build, device pilot, single cycle and repetition are `NOT_RUN`.

P0 remains `BLOCKED / NOT QUALIFIED`; PF-01/PF-02, production `NativeUnavailable`, P4 unauthorized, no QualificationProfile and no commit/push remain unchanged. The prior dual package’s OFF/ON `55/57`, five device windows and `DONE` bounded status remain historical evidence of that prior package only. See [hot final report](../.artifacts/experiment-hot-rebuild-20260921/run-20260921T141304Z/FINAL_REPORT.md).

## Historical — 双实例验证器修正后的最终有限实验（2026-09-21）

独立 tester receipt [FINAL_RESULT.md](../.artifacts/experiment-dual-20260921-second-instance-fix/run-20260921T121708Z/tester/FINAL_RESULT.md) 记录五个新鲜 `BYTES_NATIVE` 双板窗口全部通过：双 Participant/P1 删除后 P2 存活与继续写入、默认小消息、256 KiB 分片、RELIABLE 单次 whole-message drop→NACK_FRAG 关联恢复、BEST_EFFORT 目标 sample 缺失。每个窗口均 wrapper/两板 remote exit `0`、无 host timeout/watchdog；两板均有 pre-grant Granted、最终 Revoked、byte/metadata/ownership/snapshot/process guards。

验证器修正后的 host 结果为 OFF `55/55 PASS`、ON `57/57 PASS`，review 后 native OFF/ON bridge/configure/build 为 `0/0/0`；两个 manifest 各 93 个输入，主程序 SHA-256 分别为 `09b16d92…f23c`、`5096447d…6f55`。双实例 raw 复核确认 P1/P2 两组各 6/10 个正常样本、P2 身份/状态/租约/回调槽连续性、P1 完成标记后的写入顺序，以及所有 32 条正常 native chain 的 opposite-side SDK 发送配对。B 侧额外的 closing/no-receiver ingress 已单独计入，不被静默丢弃；SDK 内部 settlement 仍 `UNOBSERVED`。

这只是 default-OFF 的受限实验结果，不是生产资格或发布结论。`P0=BLOCKED / NOT QUALIFIED`、`NativeUnavailable`、PF-01–04、生产 P4 未授权、`no QualificationProfile`、`COMMIT_GATE=PROHIBITED` 和未提交状态保持不变。RAW、热重建、多 Participant、强压力/故障 close、混合 transport、native self-route、1 MiB 本轮未重跑且三板测试继续 DEFERRED；旧 dual-r2 的 predicate 失败、window 启动/abort 缺口及其后 post-abort 重跑均保留历史边界。详见 [实验报告](FASTDDS_DSOFTBUS_EXPERIMENT_20260921.md) 与 [最终报告](../.artifacts/experiment-dual-20260921-second-instance-fix/run-20260921T121708Z/FINAL_REPORT.md)。

## 当前证据：独立有限例外与历史收口（2026-09-21）

### 独立有限例外：真实 Fast DDS × DSoftBus R4（2026-09-21）

经用户明确授权的 default-OFF 实验已在两块 RK3588A 上完成限定 BYTES 场景；详见 [实验报告](FASTDDS_DSOFTBUS_EXPERIMENT_20260921.md) 及其 [R4 receipt](../.artifacts/integration-experiment-20260921/tester/device-reader-r4/RESULT.md)。OHOS bridge/configure/build 为 `0/0/0`，Windows CONFIG/Release 为 `7/7 PASS`，A/B 的真实 SPDP/SEDP、Writer/Reader、10 个样本、RELIABLE acknowledgement 和正常 close 均有原始命令、日志、哈希与权限撤销证据；限定场景 `SCENARIO_GATE=PASS`。

生命周期/observer hardening 由独立 host `16/16 PASS`、review 和新包 `448663…` 的双板复测闭合；后续 fragments host `17/17 PASS`、OHOS `0/0/0`，256 KiB 与 1 MiB 各三样本 DATA_FRAG 均完成数据/ACK/Reader/close/no-watchdog 观察。1 MiB 79 条 DATA_FRAG 观察中每序列 18 个唯一范围，重复范围不解释为 NACK 或具体重试原因；小/256 KiB 的完整 guard 在撤权边界 `NOT_OBSERVED_AT_BOUNDARY`，1 MiB 即时撤权 guard 恢复 baseline。细节、GUID/SN/range/sample-size/native hash 记录和 package 绑定见 [实验报告](FASTDDS_DSOFTBUS_EXPERIMENT_20260921.md)。

Controlled-loss 独立验证也已完成：最终 OFF/ON host `21/21`、`23/23 PASS`，OHOS `0/0/0`。RELIABLE v2 记录实际单次 whole-message drop、NACK_FRAG 缺失范围请求、重发后的 ReaderSampleInfo/ACK 因果链；BEST_EFFORT 记录目标 sample 缺失、后续 mask `006` 和 `ACK=NotCalled`。两种运行均有正常 observable cleanup 与 fresh full guard 恢复；SDK internal settlement 仍 `UNOBSERVED`。详见实验报告及 `.artifacts/experiment-loss-20260921/tester/analysis-v2-checks.json`。

该结果是独立工作包的有限例外，不是生产 P4 放行或 P0 资格化：`P0=BLOCKED / NOT QUALIFIED`、`NativeUnavailable`、`COMMIT_GATE=PROHIBITED` 保持不变，SDK internal settlement 仍 `UNOBSERVED`。RAW 两种、热重建、多 Participant、强压力/故障 close、混合 transport 和 native self-route 均保留 `NOT COVERED`。

### 前一工作包：可行性收敛（历史）

该工作包状态为 `CLOSED_WITH_PLATFORM_DEPENDENCIES`（前一可行性收敛工作包已关闭，平台问题未解除）；平台资格仍为 `P0 BLOCKED / NOT QUALIFIED`；生产入口保持 `NativeUnavailable`，该工作包不生成 QualificationProfile、不进入 P4 native backend、不 commit/push。该工作包 build/host/device 均 `NOT_RERUN`；历史 D 交付窗口 `64/64` 与 V4 `1/64` 不拼接为压力资格，`NEED_ACK` 的 `NotCalled` 保留。只有具体新机制、平台契约、可判别 trace 或明确用户决策才重开对应问题；同一阻塞不会自动启动新一轮全面探针。详见 [该工作包 review](../.artifacts/closure-20260921/review/REVIEW.md) 与 [只读核查](../.artifacts/closure-20260921/verification/RESULT.md)。

平台问题的版本/profile/config、缺失证据和解除条件统一维护于 [P0 实施与验证报告](P0_IMPLEMENTATION_20260920.md) 的“平台问题清单与解除条件（本轮收口）”。callback incarnation slot 按已批准的实际 service record 实例 + role 及 process-lifetime tombstone 规则处理；它不是 Participant 配额，也不解决 numeric ID、SDK 内部事件归属或 native 阻塞。P1/P2/P3 主机诊断历史结果保留，本轮关闭后不自动续跑。

历史 final guard 保留 `Passed=false / NON_PROJECT_CHANGED`。前轮核查确认 device-exit 授权前与 V4 撤权后的 A/B 快照逐字节一致；前轮 A/D 的独立权限窗口也各有恢复收据，当时 owned DATASYNC 为空、manager 为 `DENIED`。该次只读收口没有操作或重新查询设备权限，不把历史 readback 写成新的设备观测；范围记录见 P0 报告。本次 R4 使用独立权限窗口，其授权、撤销和恢复证据见上方实验报告。

# 历史证据快照（2026-09-20）

本轮最终独立 tester 的 canonical receipt 为 [`.artifacts/implementation-20260920/p0-cancel-schedule-final-device/RESULT.md`](../.artifacts/implementation-20260920/p0-cancel-schedule-final-device/RESULT.md)，详细记录见 [P0_IMPLEMENTATION_20260920.md](P0_IMPLEMENTATION_20260920.md)。诊断探针源码和构建脚本存在，最终 Host Debug/Release 各 18/18 CTest PASS，取消探针内部 CANCEL-01..19 均 PASS；这不等于生产 Transport 或 Fast DDS 集成通过。

| 范围 | 当前状态 | 证据边界 |
| --- | --- | --- |
| Profile/identity 诊断探针 | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED（限定） | 三种 profile、身份可用性、32/255/4096/16384/60000 B 精确记录在双板 B→A→B 通过；不证明 DDS 或加密效果 |
| Cancellation 诊断探针 | IMPLEMENTED / BUILDS / HOST_TESTED / DEVICE_TESTED（结果受限） | BYTES 的 1 秒诊断配置 FAIL；RAW 与 RAW_ENCRYPTED 未观察到选定取消区间，标记 NOT_EXERCISED；不证明所有有限时限均不可能 |
| P0 平台资格 | BLOCKED / NOT QUALIFIED | SDK 发送/关闭总时限、短返回、迟到 callback tombstone/代际防护、adapter I/O/worker/receiver 排空、资源容量和完整权限/拓扑资格尚无充分证据 |
| Fast DDS DSoftBus production backend | NOT IMPLEMENTED / NativeUnavailable | 当前改动是 probes/helpers/build wiring，不是生产 Transport；SPDP/SEDP、Writer/Reader、可靠性、分片和 RMW 未运行 |
| P1-P6、三板测试 | PLANNED / DEFERRED | 依照冻结方案推进；三板 A→B/C 及故障隔离明确留待后续 |
| PRE_COMMIT | FAIL | P0 未资格化，Ready for commit = NO；不允许据此提交或发布 |

该段所指的 2026-09-20 权限快照属于历史记录；2026-09-21 final guard 现记录为 `Passed=false / NON_PROJECT_CHANGED`，因此不能继续称为完整 guard PASS。当前只确认 owned DATASYNC 已撤销、同 token manager readback 为 `DENIED`；完整 baseline 差异仍按 P0 报告记录。未修改 DSoftBus、UDP/TCP/SHM 或 Fast DDS DDS/RTPS 语义。历史状态记录从下一节开始保留，不得覆盖本快照。

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
