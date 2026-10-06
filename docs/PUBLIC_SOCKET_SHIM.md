# Public Socket shim

> **后续工作包边界：** 本文保留 `DSOFTBUS_PUBLIC_SOCKET_SHIM_COMPILE_AND_CONTRACT`
> 的原始结果与限制。后续 `PUBLIC_SOCKET_ACCEPTED_AND_REAL_DDS_HOST_INTEGRATION`
> 在此 shim/Host 路径上增加了 bounded accepted contract 与真实公开 DDS fixture；
> 它不把本文的旧 9 个 Mock Participant 回归改写为 PublicSocketBackend 端到端证据，
> 也不改变本文关于 SDK/device/runtime、P0/PF/P4 和 QualificationProfile 的限制。

状态：`DSOFTBUS_PUBLIC_SOCKET_SHIM_IMPLEMENTED / TARGET_SDK_BUILD_VERIFIED / HOST_SHIM_CONTRACT_TESTED / CHECKPOINT_READY / NOT_COMMITTED`

证据范围：`.artifacts/public-socket-shim/run-20260926T160850Z/`。本工作包实现了现有 `NativeBackend` 到 DSoftBus 公共 Socket C API 的薄绑定，并保持默认生产路径关闭。实现包含 `Socket`、`Listen`、`Bind`、`SendBytes` 和 `Shutdown` 的实际符号调用；Host 合同测试使用与公共声明一致的 Fake 符号，经过实际 shim、`ProviderAdapter`、`ServiceRecordBinding`、owned-copy 和 receiver lease 路径。

## 已验证

- 独立 Release 复测：shim 合同 `25` 个实际 CTest 加 ProviderAdapter `17` 个实际 CTest，共 `42/42`；受影响核心目标另为 `73/73`，均 exit 0。
- 独立 ASan 复测：shim/provider `42/42`、既有 Host Participant 回归 `9/9`、input-close `14/14`，均 exit 0。9 个 Participant 用例仍使用既有 Host NativeBackend Mock fixture；本轮没有把该 DDS fixture 替换成 `PublicSocketBackend`/Fake SDK，因此不构成新 shim 到真实 DDS 的端到端证明。预编译 Fast DDS、Fast CDR 和 foonathan 依赖未全部插桩；TSan 未运行。
- 25 个 shim CTest 经过实际 facade、ProviderAdapter、ServiceRecordBinding、owned-copy/receiver lease 和测试 receiver；覆盖参数与寿命、原始返回值、typed listener、早到 bytes/stop/error、错误归属、原始发送 ticket、重复关闭和失败回滚。`42/42` 是 25 + 17 的测试总数，不是把逻辑 case 扩成 CTest 实例。
- API23 AArch64 (`native6.1.0.32`, clang 15.0.4) 新目标配置和编译通过。静态审计确认最终 ELF 导入五个公共符号并保留实际 shim 对象；目标程序未运行、未加载、未部署。
- 目标输入清单为 727 个唯一已哈希输入、0 个缺失引用；审计输出 ELF SHA-256 为 `a06a0877557fd1b66c3cc9dc7a7534dc2a42794d9e8adc53ec5103b2138d0b9c`。

测试时间线保留了完整失败历史：freeze-r1 确实执行过完整 `372/372`，随后发现 Fake C 外部函数异常展开的 `/EHs-c-` 控制问题（C4530），并以确定性 RAII control 复现 RED=1；修正为仅 Fake shim 使用 `/EHsc-` 后 GREEN=0。最终 r3 先重新执行受影响 Release `42/42 + 73/73` 和 ASan `42/42 + 9/9 + 14/14`，随后在冻结源码上又独立执行最终完整 Host `372/372`（build exit 0、CTest exit 0、124.517 秒、13/13 source-freeze 无漂移，且无 C4530 或 `/EHs-` 覆盖）。这次最终 full-r3 是新的实际运行，不是把 r1 结果改名；r1 的观察和编译器失败历史仍保留。

## 身份和边界

头文件来自用户授权的 `communication_dsoftbus` 公共源码 checkout（checkout HEAD `5518313ee70e79f788fc4d9f9a9310c8b24827cc`；`socket.h` SHA-256 `c6feffeaf60419b2788da32b4a9b4611062017854c2d9306b5a0bfd2bbb5c9de`；`trans_type.h` SHA-256 `763247518fc2c5ec7ea704bdc8a86874ee38d41721259e82ea55fecaafa0f39d`），不是 API23 安装 SDK 自带的 DSoftBus 声明。API23 sysroot、公共源码头文件和历史 `libsoftbus_client.z.so` 是不同输入；头文件到固件 ABI 的等价性仍为 `NOT_ESTABLISHED`。目标链接使用受限的 `--allow-shlib-undefined` 允许导入平台 DSO 的间接依赖，不能据此推出 loader 或运行时可用性。

回调经过由原始 `ServiceRecordTable` slot/role 导出的 shim 专用 trampoline，再进入保存的 adapter callback 和既有 Binding/lease；SDK 不直接拿 Binding 或 RecordTable entrance。没有通过当前名称或最新 socket map 重新归属。固定表项和参数 backing 在进程生命周期内有界保留，因为 `Shutdown` 是 `void`，不能用返回事实证明 SDK drain 或 quiescence。Listen 的 accepted/negotiation ownership 仍拒绝并记录，通用 accepted/reconnect 生命周期为 `DEFERRED`。

`NativeBackendUnavailable` 仍是默认行为；真实 `DSoftBusProvider` kind 仍被生产准入拒绝，Fake SDK 只存在于 Host 测试构建。没有 SDK runtime、设备、真实 Socket 运行、SDK 内部 settlement、PF-01/PF-02、生产 P4 或 QualificationProfile 证据。`P0/PF-01/PF-02` 保持 `BLOCKED / NOT QUALIFIED`。

## 可追溯收据

- API 与边界：[API_MAPPING.md](../.artifacts/public-socket-shim/run-20260926T160850Z/API_MAPPING.md)、[RESULT_AND_CAPABILITY_BOUNDARIES.md](../.artifacts/public-socket-shim/run-20260926T160850Z/RESULT_AND_CAPABILITY_BOUNDARIES.md)
- 回调寿命：[CALLBACK_ENTRY_AND_LIFETIME.md](../.artifacts/public-socket-shim/run-20260926T160850Z/CALLBACK_ENTRY_AND_LIFETIME.md)
- Host 覆盖：[HOST_SHIM_TESTS.md](../.artifacts/public-socket-shim/run-20260926T160850Z/HOST_SHIM_TESTS.md)
- 最终审查：[FINAL_REVIEW.md](../.artifacts/public-socket-shim/run-20260926T160850Z/FINAL_REVIEW.md)
- 目标结果：[target-r3/result.json](../.artifacts/public-socket-shim/run-20260926T160850Z/target-r3/result.json)、[target-r3/input-manifest.json](../.artifacts/public-socket-shim/run-20260926T160850Z/target-r3/input-manifest.json)
