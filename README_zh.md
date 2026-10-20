<!-- R38_CURRENT_SCOPED_PASS_PREFIX; the complete prior body below remains byte-preserved historical evidence. -->
# R38 双板有限耐久与持久采集：限定范围 PASS（2026-10-20）

**实现 PRESENT；原生构建 / Host / RK3588A 双板 / 当前集成范围 PASS；Full ROS2 仍为 NOT_VERIFIED。** 最终独立 tester D03 接受新 run `940f4e9c10a29830`：1801 次 String 往返、7204 条完整 33B CDR 阶段记录、丢失/重复/乱序/外来内容均为0；两侧发送跨度分别1811.239383904秒与1811.244691655秒，原始完整日志的 `final=False/True` oracle 均 PASS。两板 RuntimeClose0、observation_complete1、公开删除0、解释器0、业务子进程0；四个自有设备 PID 已消失、token 恢复，未强制清理。

Windows x64 有限 worker 通过独立 Job 管理 executor/runner/SDK 后代，SDK 前检查 source13、精确 PID/创建时间、Job 与一次性许可，使用有界追加状态。独立 observer 启动时保留真实存活 worker/child 句柄，最终观测二者实际 OS exit0；runner/原 CLI 的 Popen 与外部短 helper 实际退出也为0。**observer 自身 OS 退出为 UNOBSERVED_NONBUSINESS_AUXILIARY**，不能用它自己的收据证明自己的退出码；其精确身份已不存在、句柄已关闭、输出完整有界，双次哈希快照稳定，满足独立 review 的限定条件。

D07 实际68项 Python/0 skip 与新构建的13项 Host CTest PASS，本轮最终封存仅复验其原字节；review 后另有4项新 Windows 控制 PASS，其中准入和 runner 为 HOSTMODEL。SDK23/AArch64 原生桥的真实 D03 构建含23 TU/objects、1037 inputs，D07/最终阶段复验而非重新编译。SPDP 批次聚合不构成 DDS 逐载荷 wire 因果证明。DDS/RTPS/QoS 语义不变，只使用 DSoftBus Socket API。

[R38 完整验收、实际命令与证据](docs/ROS2_DURABLE_CAPTURE_ACCEPTANCE.md) / [支持、部分支持、不支持与未验证矩阵](docs/ROS2_CAPABILITY_MATRIX.md)。完整 ROS2、广泛 QoS/大消息/并发/小时级 soak、重启重建、SDK epoch/late-callback drain/全局无泄漏仍未验证；默认自动重连当前未提供、第三观察者不受当前两 Participant/单 peer 配置支持；loan/recorder 内部 take 缺观测，client status/feedback ordering 仍 OPEN。缺少部署 stripped-rclpy 的精确源码/编译/链接/unstripped lineage，不能据此断言 ABI 唯一根因或实现不可能；历史 service/action/parameter/bag 不自动用新桥哈希重新资格化；R22 STOPPED。

最终 tester seal `9d8dbe9e15813da1fad73dbc213dfd505d871868e69e24a765d711a3d2f97459` 绑定730项，Commit gate:PASS 仅为 tester 技术范围。文档已同步；Main PRE_COMMIT、完整 all/staged 图检查及 commit/push 尚待执行，CI NOT_RUN_NO_NEW_COMMIT。R37 FAIL、历次初始/review失败、旧 R38 `efa7336e7ca4b78b` 的 BLOCKED 与 tool24208 exit UNKNOWN 不改写。下方全部原文为逐字节保留的历史快照。

---

<!-- R37_BLOCKED_CURRENT_PREFIX; 以下 R36/R35/R26 历史正文逐字节保留。 -->
# R37 原生观测与关闭：FAIL / BLOCKED（2026-10-20T03:08:59.634827+00:00）

**实现 PRESENT；native BUILDS；HOST_TESTED PASS；DEVICE_TESTED=true、CROSS_DEVICE_TESTED=true 但验收 FAIL；集成 INTEGRATION_NOT_QUALIFIED；VERIFIED=false。Commit gate: BLOCKED；Ready for commit: NO。** 真实双板仅执行一轮 `5f28247d2fc3258a`；独立只读提取确认 1801 轮、7204 条完整 33 字节 CDR 一致、无丢失/重复/乱序。原始完整流含 fixed1910s probe deadline FAIL，未修改的原 oracle 在 final=False/True 下均 FAIL；内容观察不等于完整耐久与正常关闭通过。

R37 使用 SDK23/AArch64 实际编译了源码绑定的 23 TU、1037 个编译/链接输入、23 个 object 的 native bridge；最终 tester 重验复用 D01 build，未进行新的 final SDK build 或全量 Fast DDS 重编。HOST 为 46 个 Python 方法（0 SKIP）与新构建的 13 个 C++ CTest 用例，分别计数。原 256 槽以完整有序 SPDP batch 身份聚合，两板 captured1/faults0/adverts4，7 个 fatal counter 均为0；本轮未重现原容量溢出，仍未通过正常关闭。

两板 OriginalDeleteParticipant0、RuntimeClose2、observation_complete1、公开删除1、child exit71；stderr 保留 CONTROL_MISSING_OR_INVALID 和 Failed to delete participant。Host 在 A385/B385 后失去捕获，原 BUSINESS_COMMAND.json 仍为 RUNNING；原 host 退出码和中断原因均 UNKNOWN。缺少原 PROBE_FINISH/normal-close control 及 runner RESULT/CLEANUP/ANALYSIS。11 条只读恢复命令取得6个原始 raw 文件，远端前/本地/远端后 SHA 相同；四 owned PID 均不存在、token 恢复 matches1/rc0，signals/replay/restart/manual close/cleanup mutation 均0，不能替代正常关闭。

详见 [R37 构建、实际命令、结果与边界](docs/ROS2_NATIVE_CLOSE_ACCEPTANCE.md) 和 [当前矩阵](docs/ROS2_CAPABILITY_MATRIX.md)。完整 ROS2 NOT_VERIFIED；广泛 QoS/大消息/并发/长时 soak、强 DDS/wire provenance、SDK epoch/global drain/泄漏自由未证实。默认自动重连 NOT_PROVIDED，loan/recorder-internal take UNOBSERVABLE，第三 observer 不支持，selected-client status/feedback ordering OPEN，R22 STOPPED/excluded。仅同步 BLOCKED 交接；无新 commit/push，CI NOT_RUN_NO_NEW_COMMIT，e27b7e9 是历史 R35 基线。以下旧文中 current/PASS/发布状态只属于原 dated snapshot；原 R36 FAIL 等全部失败保留，不自动扩大旧测试对新 native 的资格。

---

<!-- R36_BLOCKED_CURRENT_PREFIX; original body below is byte-preserved. -->
# R36 有限耐久测试：FAIL / BLOCKED（2026-10-20T00:34:50.480663+00:00）

实现 PRESENT；52 个 HOST 测试方法 PASS；真实双 RK3588A 已执行，DEVICE_TESTED / CROSS_DEVICE_TESTED **FAIL**，集成 INTEGRATION_NOT_QUALIFIED，整体 NOT_VERIFIED。1801 次 String 应用层往返的内容检查通过，但两板 native close 均失败、子进程均退出 71。内容通过不能替代关闭和退出门。最终 tester Commit gate: BLOCKED；Ready for commit: NO。

使用 R26 已有 native 包（3236 个文件、220 个不变 ELF），R36 未重新编译 C++。资源与 A 本地应用 echo RTT 只描述该有限窗口；泄漏自由 NOT_PROVEN、完整 ROS2 NOT_VERIFIED、强 DDS BLOCKED_PROVENANCE_GAP、loan UNOBSERVABLE、SDK epoch/global drain NOT_PROVEN。所测仅 RELIABLE/VOLATILE、小 String、单 outstanding、无应用重试/去重；更广 QoS、大消息、并发、更长 soak 尚未验证。当前配置不提供默认自动重连，也不支持第三 observer。R22 STOPPED/excluded。

详见 [R36 命令、原始失败及边界](docs/ROS2_FINITE_ENDURANCE_ACCEPTANCE.md) 和 [当前能力矩阵](docs/ROS2_CAPABILITY_MATRIX.md)。后续须独立审查源码绑定的 native 观测/关闭契约和新 build，再按完整双板标准验证；当前源码分析只是充分失败条件推断，尚未证实唯一根因。无失败后设备重跑或修改 cap/timeout/断言/返回码；无 R36 新提交或 push，CI NOT_RUN_NO_NEW_COMMIT。基线 e27b7e9 属历史 R35，不是新 R36 发布或新远端核验。

以下原文逐字节保留，编码也不改写。R35 三个 bounded case PASS 与 R26 200 次往返 PASS 保持原有历史范围；旧文中的 current/PASS/发布状态只属于各自 dated snapshot。

---

# FastDDS dsoftbus Transport

本仓库用于组合 Fast-DDS 与 OpenHarmony `communication_dsoftbus`，为基于
dsoftbus 的 Fast DDS 传输集成开发提供统一的源码快照。

## 目录结构

- `Fast-DDS`：Fast DDS 下游源码仓库。
- `communication_dsoftbus`：OpenHarmony dsoftbus 下游源码仓库。

这两个目录作为 Git submodule 管理。顶层仓库会记录每个下游仓库在当前
集成快照中使用的确切提交，确保源码组合可复现。

## 初始化源码

```bash
git clone --recurse-submodules git@github.com:Jiusi-pys/FastDDS-dsoftbus-transport.git
```

已有工作区可以执行：

```bash
git submodule update --init --recursive
```

如果要有意更新集成快照，请先更新下游仓库，再提交顶层仓库中的 submodule
指针：

```bash
git -C Fast-DDS pull
git -C communication_dsoftbus pull
git add Fast-DDS communication_dsoftbus
git commit -m "Update downstream repositories"
```

## 下游仓库

- Fast DDS：`git@github.com:eProsima/Fast-DDS.git`
- OpenHarmony communication_dsoftbus：`git@gitcode.com:openharmony/communication_dsoftbus.git`

See [README.md](README.md) for the English documentation.
