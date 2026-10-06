# N3-1 根构建接线完成记录

本文件保留 N3 历史构建与 target 命名以绑定原始收据；它不定义当前公开
`FastDDSDSoftBusTransport` 接口。当前命名和安装入口见 [NAMING](NAMING.md)
与 [安装说明](INSTALL.md)。

## 状态

本记录只覆盖 N3-1：将既有 N2 SDK-free 发送执行模型接入根 CMake 生产目标，并验证源码子工程消费者。它不代表 N3 application 生命周期语义完成，也不代表真实 DSoftBus Transport 可用。

提交前快照：HEAD `def10ea376cb3bf9921c1c7ea74c92b1da9e04e8`，分支 `codex/n3-application-lifecycle`。本记录不预先声明提交或推送结果。

| 维度 | 结果 |
| --- | --- |
| Implementation | PRESENT：`mdds_dsoftbus_native_send_execution` 使用既有 `src/NativeSendExecution.cpp`，根测试链接该生产目标 |
| Host build/test | PASS：`SOURCE_PROBE` Debug/Release 各 24/24；CONFIG Release 25/25 |
| Build/consumer matrix | PASS：N3 root/source-subproject 10/10；干净 `.artifacts` 父目录隔离复测 10/10 |
| Regression slices | PASS：N1 9/9、ApplicationAttachments 7/7、binding 32/32、facade 4/4；外部消费者 1/1/2 |
| SDK23 | PASS：OpenHarmony arm64-v8a 目标构建、链接及 ELF 检查；`native_send_execution_tests` 为 ELF64/AArch64 |
| Review | PASS（修订后）：review 唯一 P2 已通过创建 artifact parent 修复，closure 见 `.artifacts/n3-execution/wiring-review/CLOSURE.md`，final tester 已复测 |
| Device/DSoftBus/DDS | NOT RUN：本切片没有 provider、callback bridge、设备部署或 DDS traffic path |
| Overall | NOT VERIFIED：N3-1 scoped gate PASS；完整 N3 仍 PLANNED/未实现 |

## Canonical evidence

最终独立 tester：`.artifacts/n3-execution/wiring-final/RESULT.md`，并保存相邻 JSON、退出码、标准输出/错误和文件哈希。关键哈希：

```text
CMakeLists.txt                         890909980A77C407B53DC28FCDEA9E6118C3DB0E8BAF4D723B01ED75FF5F8A5B
tests/native_send_build_tests.cjs      A49A564E47B15F5866D042F52CFE51058A2748146ABF3661B329BD90881ED912
PLAN.md                                9D2153EEC58183C30BC1062E3285897FD6A9B191CA37E3D6106E21F5DD147AD5
SDK23 native_send_execution_tests      677EA367BC12EBB6D1B77F13A3C1A33AD214B51D24A65C16FB715C6EB00A8763
```

环境为 Windows/MSVC 19.44.35228、CMake 4.4.3、Node 24.19.0；SDK23 目标为 `arm64-v8a`。CONFIG 测试消费已安装 Fast DDS 前缀与本项目源码子工程；本项目自身 install/export package 不属于 N3-1。

## 限制与后续

生产初始化在真实 backend 未接入时仍 fail-closed（`NativeBackendUnavailable`）。真实 DSoftBus provider/callback/线程和跨设备通信、SPDP、SEDP、用户样本、可靠性恢复、同进程同名 Participant 热重建及 RMW 没有被本切片验证。N3-2 至 N3-5 的 pinned receive/receiver lease、application bundle、copying/admitted drain、Binding 和联合 Detach 仍待实现与独立验证。

初次 clean-parent 隔离尝试因未加载 MSVC 开发环境而无法配置编译器；后续在实际 MSVC 环境中同一隔离场景 10/10 通过。该环境边界保留在 final tester 记录中，不作为产品缺陷或 PASS 的替代证据。
