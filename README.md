# FastDDS dsoftbus Transport

ApplicationAttachments 的设计 R1（unique InputChannels/attachment 封装）与代码 R1（Close waiter 等待迟到 Opening）是两个不同问题，均已在各自限定范围通过 review/test；root public target wiring 也已通过。64 次仅为模型 application attachment 周期，不是 DDS Participant 或真实同名热重建。native backend、实际 DDS/SPDP/SEDP/样本、同名 Participant 热重建及 RMW 仍未运行。

本仓库用于在不改变 Fast DDS DDS/RTPS 语义的前提下，验证并实现基于 OpenHarmony DSoftBus Socket API 的 Transport。当前仓库包含 S1/S2 探针、S3 独立核心和 D2 发送资源桥接；完整可用于 Fast DDS 生产通信的 DSoftBus Transport 仍未完成。

## 当前状态

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
| N2 native send execution | DESIGN CLOSED / IMPLEMENTATION NOT STARTED / NOT RUN | `.artifacts/review/20260914T-native-send-execution/{DRAFT,REVIEW,MAIN_DECISION,CLOSURE}.md`；必须先完成 N1 精确 commit closure 并提交 N1，之后才可开始 N2；N2 的 fresh impacts、TDD、实现与测试仍未执行，不由 N1 模型结果自动获得 |
| Fast DDS 主机库构建 | BUILDS / PASS（准备阶段） | `.artifacts/build/fastdds-logs/HOST_BUILD_RESULT_20260914.md`：实际 237/237 编译、install exit 0；仅证明主机库构建准备完成，不是 Transport、DDS 集成或上游测试通过 |
| Fast DDS Transport / DDS / RMW 集成 | Descriptor facade IMPLEMENTED / BUILDS / HOST_TESTED / REVIEWED（限定范围）；native backend、DDS/RMW NOT RUN | 真实 Descriptor/Transport factory、接口与 selectors 已完成限定范围验证；native backend 缺失时 production init 固定失败，私有 HostModel 仅供测试。N1 owner host/source/consumer 与 SDK23 ABI recovery PASS（限定范围）；真实 SPDP/SEDP、用户数据、同名 Participant 热重建与 RMW 未执行 |
| 总体 | NOT VERIFIED | attachment 根接线及既有限定切片通过；native backend、quiescence、DDS/RMW 与同名 Participant 热重建尚未验证；这是完整 Transport 发布边界，不是 N1 tester 失败 |

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
