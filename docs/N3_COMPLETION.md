# N3 冻结生命周期模型完成记录

## 结论

N3 的冻结范围已实现，并通过 review 后独立最终 tester 的限定范围门禁。该结论只适用于 SDK-free 的进程内生命周期组合模型；不代表真实 DSoftBus Transport、DDS 集成、设备运行或生产发布完成。

提交前快照：HEAD `ddc08c55aee78414015d194f2c976da690948f12`；N3 语义改动尚未提交或推送。本记录不预填未来 SHA。

## 实现范围与硬契约

实际实现位于 `ProcessCoordinator`、`ProcessReceiveInvocation`、`ProcessReceivePath` 及现有 N1/N2/ApplicationAttachments/InputChannels 组合路径。各实现仅编译入其所属根生产目标，测试复用这些目标，没有 shadow implementation。N3 实现了：

- application bundle 的 Preparing/Active/Detaching/Detached 事务、回滚和同 local 槽位保留；
- 不可重新绑定的 application、receiver incarnation、native owner/generation/route identity；
- Binding 的有限期限、原始归属和现有队列/预算复用；
- application copying、send admitted、receive dispatch 和 controls 的联合 drain；
- 最终接收执行的原 receiver-use lease；
- 短冻结、锁外 Revoke/关闭/等待/provider/receiver 调用，以及完成后才发布下一代。

七项硬不变量均纳入实现和测试：

1. P0 记录永远不能重新绑定到 P1。
2. native identity constraint 不依赖长期 callback lease。
3. Revoke 阻止新的成功准入；已准入 invocation 必须终局结算，即使尚未 Run。
4. 接收执行许可必须取得原 receiver incarnation 的真实 use lease。
5. Detach 必须等待 application copying activity 归零。
6. 协调门只执行短状态转换，外部等待和调用全部在锁外。
7. P0 Detached 前，同 local 的 P1 不得发布。

## 最终验证

权威结果：`.artifacts/n3-execution/full-final-test/RESULT.md`。

| 验证 | 结果 |
| --- | --- |
| 根 SOURCE_PROBE Debug/Release | PASS，26/26、26/26 |
| 直接组件（Debug/Release） | PASS，Coordinator 13、Process 8、N2 17、AA 9、Input 16、Runtime 30、N1 11 |
| CONFIG facade + Coordinator | PASS，21 + 13 |
| N1/N2 consumer | PASS，N1 9/9，N2 10/10 |
| SDK23 | PASS：原始最终记录中的 Coordinator/Process 均 ELF64 AArch64；补充 manifest 另补测 N2/Process/Coordinator 三个 ELF64 AArch64 |
| N1/AA isolation | PASS |
| Review | PASS，`full-review/CLOSURE.md` 已关闭唯一 P2 |
| Device/HDC | NOT_RUN |

测试覆盖 A/R/B/D/S/N3 矩阵的事务回滚、错误身份、代际替换、Binding 超时/公平性、copying 空队列、receiver lease、admitted send drain、provider 返回后 settlement、重复 Detach、容量保留和 80 轮同 local 模型重建。最终 tester 未在 test-only 修复后重跑未受影响的 AA/options/binding 矩阵；这些沿用初测 PASS 并在结果中明确标注。

最终补充 manifest `.artifacts/n3-execution/full-final-test/COMPLETE_N3_MANIFEST_RECEIPT.md` 对完整 27 个 N3 CMake/生产/测试路径执行了补充前后哈希一致性检查，并补测 Debug/Release 的 N2、Process、Coordinator 目标以及 SDK23 三个 ELF。原始 `RESULT.md` 的 23 路径快照不包含新增的六个相关头/内部文件；manifest 只证明这些新增路径在补充检查区间内未漂移，不追溯声称它们已有原始 before-hash。

## 关闭前提与边界

模型要求外部驱动调用 `BeginShutdown`、推进所有 Detach 并 join 成员调用；析构不是隐藏 watchdog，也不能从 callback 或仍持有 permit 的线程等待自身。保留但已撤销的外部 shell 继续计入资源额度，不保留执行权限。

N3 不包含本项目自身 install/export CMake package；CONFIG 验证仍是“已安装 Fast DDS + 本项目源码子工程”。真实 provider、SDK callback 身份与 Socket ID 复用/quiescence、生产工作线程、板端权限和跨设备通信、DDS SPDP/SEDP/样本/可靠性、RMW、实际同进程同名 DDS Participant 热重建均未验证。生产 backend 缺失时 `DSoftBusTransport::init()` 继续 fail-closed，报告 `NativeBackendUnavailable`。
