# ROS 2 Jazzy Host integration

本项目是 Fast DDS 的 OpenHarmony DSoftBus Socket Transport。ROS 2 集成使用原版
`rmw_fastrtps` 和真实 `rclcpp` API；项目不提供 `rmw_mdds`，也不修改 `rcl`、
`rclcpp` 或 `rmw_fastrtps` 的业务实现。

## 支持边界

当前 Host 集成针对 ROS 2 Jazzy 的 Fast DDS 2.14 ABI，使用隔离的 2.14.5
Fast DDS 构建和一个版本绑定的工厂配置补丁。项目默认的 Fast DDS 3.6 基线保持
不变。2.14 Host 包与默认 `FastDDSDSoftBusTransport` 包不能在同一个进程混用。

选择路径必须在 Participant 创建前设置。required 模式使用一个绝对路径的配置
文件，并通过安装的 `factory_configuration` target 导入版本绑定的工厂 ABI。配置、
插件或 backend 缺失时应明确失败，不能静默回退到 UDP、SHM 或进程内交付。

Host 验证使用 `HOST_OPAQUE_BYTES_TEST_ONLY` 桥接和两个独立进程。该桥只转发
不透明字节，不解析 ROS/RTPS，也不伪造发现、匹配或 Reader 结果；运行记录中的
`fake_sdk=1` 是 Host 测试边界标识，不是设备或 SDK 证据。

## 当前 G2/A1 package 状态（2026-10-04）

本次 G2/A1 package 没有执行 HDC、设备或跨设备操作；设备 V1 状态为
`NOT_RUN`，不能由 Host 结果替代。最终 named tester 在当前工作树上完成了
Fast DDS 2.14.5 Host 与 API23 TargetBridge 的 clean-first build/link/static
检查，Host214 与 CTest `14/14 PASS`，安装并 relocated consumer `PASS`，以及
ROS 双进程 paired-close `20 writes / 20 takes PASS`。failure-close harness
本身 exit 0，所监控 child processes 按预期各 exit 4；forward-0 pair 的 exit 4/0 writes/0 takes 是保留的
负向 harness 结果，不是普通业务成功。完整 Release CONFIG 为 `552/552 PASS`，
受影响 ASan 为 `208/208 PASS`；预构建依赖 DLL 未插桩，TSan 未运行。review
receipt 为 `build/g2a1/REVIEW.md`，当前无阻塞 finding。A5/C1 的
`NO_REACCEPT`、PF/P4 和生产资格边界保持不变。

## 历史限定双板验收摘要（2026-10-04）

在 SDK23/AArch64 的 `build/ros2-target/r2` 包上，登记的两块 RK3588A 已完成限定范围
的 ROS 2 双板验收：query `d1c2cb4097279214`、data `be8cb2d90f287223`；普通
`rclcpp` → 未修改 `rmw_fastrtps` → Fast DDS 2.14.5 → `FastDDSDSoftBusTransport`
→ DSoftBus Socket/Binding → 对端 ROS 回调路径实际运行。RELIABLE 和 BEST_EFFORT
分别双向 5/5，合计 Topic 20/20；graph、service request/reply、实体删除、Participant
删除、Runtime close、正常进程退出和环境恢复均有独立原始收据。

该摘要只覆盖上述限定双板功能范围。目标构建、安装消费和 Host 使用方式见
[构建、安装和外部消费](#构建安装和外部消费)。SDK epoch/drain/cancellation、任意故障
安全、异构端点和生产资格仍未证明；P0/PF/P4 原状态、默认
`NativeBackendUnavailable` 和 `QualificationProfile` 状态保持项目既有边界。设备验收
记录产生时 HEAD 为 `4807765f53826ed0b882cac32610acb17ee30b4f` 且尚未提交；后续
本地提交状态以 Git 历史为准，本摘要不预写推送结果。

## 历史快照：ROS 双板验收停止状态（2026-10-01）

独立 tester/reviewer 已确认 `FAIL_CONFIRMED_AND_STOP_REQUIRED`。补充 query
`cdd5bbe62d49a8a6` 和 repair query `5b1d035fb8e6ae66` 均为
`PASS_QUERY_ONLY`，不能证明 Socket、DDS、ROS 数据或生产成功。canonical data
`9b301c516da4d1eb` 保持 `FAIL_LOGGING_INITIALIZATION`；repair data
`a6b0948bf98c9a78` 远端 A/B 均退出 `4`，controller 首个失败为
`ROS_CONTEXT_CLOSE`，两侧均未释放 listener/stage，正式 Topic writes/takes 为
`0/0`（合同期望 `20/20`），没有 graph、service 或正常 DDS/Runtime close。

原始输出观察到 publisher、缺少 unicast locator/remote match、删除 Participant
失败和 factory destructor incomplete；这些是症状，根因仍 `NOT_CONFIRMED`。两端
正式环境 ownership、snapshot、metadata、DENIED 和 process guards 在测量范围内
通过，且 collector 已关闭；这不替代正常业务关闭、SDK drain 或 native completion。
两端 live load 均为 `NO_NATIVE_PID_RECORD`，native 结果不通过。历史
`762fe826e929da8c` 缺原始 claim 的失败仍保持原样，不能由后续 query 或 data 改写。

累计实际账本为 query 使用 `3`、data 使用 `2`，剩余 query/data/repair 均为 `0`。
因此设备操作停止、未创建成功基线提交；当前状态为
`FAIL_CONFIRMED_AND_STOP_REQUIRED / DEVICE_ROS2_DATA_NOT_RUN_AS_PASS /
NOT_COMMITTED`。本地修复、Host 和 LOCAL_MODEL 结果仍按各自证据范围记录，不能
升级为设备 ROS/SDK 通过。默认 `NativeBackendUnavailable`、P0/PF/P4、SDK
epoch/drain/cancellation 和 `QualificationProfile` 状态不变。

## 历史快照：ROS 双板观察窗口（2026-10-02）

在上述历史结果之外，新增 query run `9a55d8c042a1741a` 已按本次独立
授权完成并通过 `PASS_QUERY_ONLY`；其 claim、两端原始查询输出及恢复事实
单独保留。随后唯一数据 run `75c5b5e6159dccd8` 已实际启动，因此数据额度
已消费；独立原始数据审计结果为 `FAIL_FUNCTIONAL`，commit gate 为 `FAIL`。
该窗口不产生成功提交，也不启动重试。

本次 A/B 均观察到实际 Socket 返回 `-426442743`；`Listen` 和 `Bind` 均为
`NotCalled`，不能据此解码 SDK 根因，也不能把该值解释成权限或某个命名的
平台错误。两端正式应用首错均为 `MATCH / REMOTE_MATCH_MISSING`，远端退出码
均为 `4`；Topic 正式 writes/takes 为 `0/0`，graph、service 和后续阶段未完成。
原始日志同时包含 `Failed to delete participant`；关闭失败与业务首错分开记录，
不能由 `ROS_CONTEXT_CLOSED` 或 guard 通过覆盖。权限/进程恢复 guard 在测量范围
内通过，但 live load identity 未捕获，SDK 内部结算仍未观察，故不等同于正常
ROS/RMW/DDS/Runtime 关闭。

本次新账本为 query 使用 `1`、data 使用 `1`、repair 使用 `0`；历史 query `3`
和 data `2` 的账本保持不变，累计剩余均为 `0`。历史 `a6b0948bf98c9a78`
及旧 query 失败仍保持原状态，旧启动根因继续为 `UNKNOWN`。当前结论为
`ROS2_DEVICE_ACCEPTANCE_FAIL / NOT_COMMITTED / NOT_PRODUCTION_QUALIFIED`；
不得把本窗口的 Socket 返回或关闭症状写成已确认 SDK/权限根因。

本窗口原始审计入口见
[observed-device evidence](../.artifacts/ros2-device-acceptance/observed-device-20261002/)。
其设备执行与失败收据不改变 Host 验证、默认 `NativeBackendUnavailable`、
P0/PF/P4 或 `QualificationProfile` 状态。

## 历史快照：离线启动诊断状态（2026-10-01）

在上述设备停止状态之外，当前启动诊断源码差异已经完成实现、初测、review 修复
和 R2 复测，独立 review verdict 为 `INDEPENDENT_REVIEW_CLOSED`。R2 受影响 Host
与 ASan 各为 `53/53`，安装版 hook/unload 测试 `6/6`，正确依赖前缀的本地 pipe
测试 `2/2`；ROS 双 QoS、graph、service 和关闭的 `20/20` 记录仍属于 Host 证据。
目标端 109 成员的静态包结果来自独立 tester 收据，未在本次 review 中重新执行，
也不构成设备或 SDK 运行证明。

本轮修复使启动阶段的本地拒绝、SDK/Socket 操作、primary、cleanup、capture 和
remote exit 分层记录，并在 capture 不完整时保留原始首错。异常终止、输出背压和
有限 metadata 存储的边界仍存在：它们不证明有界 wall-clock 输出、SDK drain 或
cancellation。历史真实失败 `9b301c516da4d1eb`、`a6b0948bf98c9a78` 保持 FAIL，
启动根因仍为 `UNKNOWN`；本轮未执行 HDC、设备、查询或 Git 状态变更。下一次设备
申请仍是草案、未授权，不能由离线结果自动启动。

## 历史快照：ROS 双板验收状态（2026-10-01）

当前 ROS 设备验收为 `BLOCKED / DEVICE_ROS2_DATA_NOT_RUN / NOT_COMMITTED`。
真实 query run `762fe826e929da8c` 的 A/B 目录查询命令均返回本地退出码 0，
并保留了两端 grant、ownership、final-snapshot、final-guard 及撤权/恢复证据；
这些是查询窗口的事实，不是 ROS 数据窗口通过。查询流程在 postrun 阶段因缺少
原始执行 claim 被正式拒绝，原件不可事后补写或退款，因此本次没有启动、部署或
执行 ROS 数据程序，Topic、graph、service 和正常 ROS 数据关闭均保持未验证。

当前修订已将 `ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_ACCEPTANCE` 接入正式查询
claim 分支，并由两项本地 PowerShell 定向测试验证该分支在首个执行效果前调用
claim 校验；这改善未来执行路径，不能回填上述历史 query 的缺失 claim。离线重放
仍为失败，数据额度未消费但当前设备流程停止；不允许通过新 run、重新查询或本地
模拟收据继续推进。既有 Host 和本地模型结果仍按各自范围保留，不能升级为设备
ROS/SDK 证据。默认 `NativeBackendUnavailable`、P0/PF/P4、SDK epoch/drain/
cancellation 和 `QualificationProfile` 状态不变。

## 构建、安装和外部消费

以下命令展示流程，路径应替换为本机已安装的 ROS 2 Jazzy、Fast DDS 2.14.5、
Fast CDR 和依赖 prefix。ROS 业务程序只需要安装包、公开头文件和导出 target，
不应 `add_subdirectory` 本仓库或 include `probes/`、`tests/`、`.artifacts/`。

```powershell
$deps = 'D:/deps/fastdds-2.14.5'
$ros = 'D:/deps/ros2-jazzy'
$prefix = 'D:/install/fastdds-dsoftbus-ros2-host'

cmake -S integrations/ros2 -B build-ros2-host `
  -G 'Visual Studio 17 2022' -A x64 `
  -DCMAKE_CONFIGURATION_TYPES=Release `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
  "-DCMAKE_PREFIX_PATH=$ros;$deps" `
  "-Dfastrtps_DIR=$deps/share/fastrtps/cmake" `
  "-DDSOFTBUS_SOURCE_DIR=D:/src/communication_dsoftbus"
cmake --build build-ros2-host --config Release --parallel 2
cmake --install build-ros2-host --config Release --prefix $prefix
```

### 构建版本绑定的 Fast DDS 2.14.5

ROS Jazzy 的已验证 Host 环境使用 Fast DDS `2.14.5`。从该版本的提交
`ebcb5c05e9da9e262041e8616bbaff61ab40a296` 建立一个独立的、可写的 vendor 副本；
原 Git 源只作为归档输入读取，不要修改仓库中的 Fast DDS 子仓库。vendor 目录必须是
**仓库 `build/` 下的真实（非链接）目录**，因为补丁链中的 `apply_topic_rtti_key.py`
只接受隔离的仓库构建目录。归档前先准备一个全新的空目录和 tar 文件路径：

```powershell
$repo = (Get-Location).Path
$vendor = "$repo/build/ros2-host/fastrtps-2.14.5"
$archive = "$repo/build/ros2-host/fastrtps-2.14.5.tar"
New-Item -ItemType Directory -Path $vendor
git -c core.autocrlf=false -C D:/src/Fast-DDS archive --format=tar `
  --output=$archive ebcb5c05e9da9e262041e8616bbaff61ab40a296
tar -xf $archive -C $vendor
python integrations/ros2/prepare_fastdds_source.py --source $vendor
```

`prepare_fastdds_source.py` 是唯一受支持的入口，按固定顺序执行完整的五步补丁链：
`apply_fastdds_hook.py`、`apply_endpoint_guard.py`、`apply_teardown_observation.py`、
`apply_residual_observation.py`、`apply_topic_rtti_key.py`。每一步前，它用与
`target/rebuild.py` 相同的 exact-EOL 规则核对 `UPSTREAM.json`、
`TEARDOWN_UPSTREAM.json`、`RESIDUAL_UPSTREAM.json`、`TOPIC_RTTI_UPSTREAM.json` 记录的
原始文件 hash：只接受内容相同或仅换行符不同的文件，其余一律拒绝。结束时它确认
`FastddsHook.hpp`、`PlatformModule.hpp`、`StartupDiagnostics.hpp`、
`TeardownDiagnostics.hpp`、`ResidualDiagnostics.hpp`、`NativePrefix.hpp` 六个头文件
已放入 `src/cpp/fastdds/domain/` 且与仓库副本一致。不要只运行其中一两个脚本，
也不要手工复制头文件：缺任何一步都会得到缺少头文件或与已验收构建不一致的源码树。
这些补丁只改变版本绑定的 Participant/endpoint 配置接入、owner 结算保护和只读观察；
不改变 UDP/TCP/SHM、SPDP/SEDP、History、ACK/NACK 或分片协议语义。

补丁交付检查（不修改上游仓库）：

```powershell
python tests/ros2_host/check_patch_delivery.py --upstream-git D:/src/Fast-DDS
```

它从固定提交重放完整五步链，核对六个头文件的 hash（CRLF 折叠为 LF 后计算，不随
克隆的 autocrlf 设置变化）、`fastdds-2.14.5-factory.patch` 的逐字节一致、重复应用被
拒绝以及上游漂移被拒绝；成功时输出 `PATCH_DELIVERY_PASS`。

随附的 `fastdds-2.14.5-factory.patch` 只是前两步（hook 与 endpoint guard）输出的
zero-context diff 快照，供审阅用，不能替代完整补丁链。不要手动应用它；始终使用
`prepare_fastdds_source.py`，避免绕过版本基线检查。

用与 ROS Jazzy ABI 匹配的 Fast CDR、foonathan_memory 和 Windows OpenSSL prefix
构建这个隔离副本。下面的变量都是示例路径；实际构建使用 VS2022 x64 Release、
`/MD`、共享库、`SECURITY=ON`、`THIRDPARTY=OFF`，并关闭 vendor tests/examples：

```powershell
# $vendor 沿用上一步已按完整补丁链准备好的 build/ 下的目录。
$vendorBuild = 'D:/build/fastrtps-2.14.5'
$vendorPrefix = 'D:/install/fastrtps-2.14.5'
$deps = 'D:/deps/ros-jazzy-dependencies'

cmake -S $vendor -B $vendorBuild -G 'Visual Studio 17 2022' -A x64 `
  -DCMAKE_CONFIGURATION_TYPES=Release `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
  "-DCMAKE_PREFIX_PATH=$deps" `
  "-DCMAKE_INSTALL_PREFIX=$vendorPrefix" `
  -DBUILD_SHARED_LIBS=ON -DTHIRDPARTY=OFF -DSECURITY=ON `
  -DCOMPILE_EXAMPLES=OFF -DBUILD_TESTING=OFF
cmake --build $vendorBuild --config Release --parallel 4
cmake --install $vendorBuild --config Release --prefix $vendorPrefix
```

`$deps` 必须同时提供 Fast CDR、foonathan_memory 和 OpenSSL 的实际 CMake
配置；不能用改名 DLL、伪造版本或忽略链接错误替代 ABI 匹配。当前实际运行的
ROS 环境是 `rclcpp 28.1.16`、`rmw_fastrtps_cpp 8.4.3`、Fast DDS `2.14.5`、
Fast CDR `2.2.5`，使用 MSVC 19.44 `/MD`。仓库内另有 Jazzy 源码工作区输入，
其中的 Fast DDS 2.14.7、`rmw_fastrtps` 8.4.4 等版本没有被本次运行混称为实际
执行版本。

### 安装并构建仓库 Host 前端

将上一节的 `$vendorPrefix` 传给本仓库集成工程，生成单独的 ROS 2 Host 包：

```powershell
$transportBuild = 'D:/build/fastdds-dsoftbus-ros2-host'
$transportPrefix = 'D:/install/fastdds-dsoftbus-ros2-host'
$dsoftbusSource = 'D:/src/communication_dsoftbus'

cmake -S integrations/ros2 -B $transportBuild `
  -G 'Visual Studio 17 2022' -A x64 `
  -DCMAKE_CONFIGURATION_TYPES=Release `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
  "-DCMAKE_PREFIX_PATH=$vendorPrefix;$deps" `
  "-Dfastrtps_DIR=$vendorPrefix/share/fastrtps/cmake" `
  "-DDSOFTBUS_SOURCE_DIR=$dsoftbusSource"
cmake --build $transportBuild --config Release --parallel 2
cmake --install $transportBuild --config Release --prefix $transportPrefix
```

从安装结果复制示例到仓库外的独立目录，再以安装 prefix 消费它：

```powershell
$consumerSource = 'D:/work/ros2-transport-consumer'
Copy-Item -Recurse `
  "$transportPrefix/share/FastDDSDSoftBusTransportRos2Host/examples/ros2_transport" `
  $consumerSource
$ros = 'D:/deps/ros2-jazzy'
$consumerBuild = 'D:/build/ros2-transport-consumer'

$env:AMENT_PREFIX_PATH = $ros
$env:PYTHONPATH = "$ros/Lib/site-packages"
$python = 'D:/deps/pixi/python.exe'
cmake -S $consumerSource -B $consumerBuild -G 'Visual Studio 17 2022' -A x64 `
  -DCMAKE_CONFIGURATION_TYPES=Release `
  "-DCMAKE_PREFIX_PATH=$transportPrefix;$ros;$vendorPrefix;$deps" `
  "-Dfastrtps_DIR=$vendorPrefix/share/fastrtps/cmake" `
  "-DPython3_EXECUTABLE=$python"
cmake --build $consumerBuild --config Release
```

`Python3_EXECUTABLE` 应指向与已安装 ROS 2/ament 环境对应的解释器；实际验证使用
Pixi Python 3.12。不要让系统 Python 3.13 在缺少 ament 依赖时被 CMake 自动选中。
显式 `fastrtps_DIR` 也不可省略：它保证消费者绑定上面构建的、带工厂 ABI 的
2.14.5 安装，而不是从 ROS 优先搜索路径拾取未打补丁的同版本库。复制示例和
构建目录都应是本次执行新建的目录，不能复用旧 prefix 或旧 consumer build。

该示例通过 `find_package(FastDDSDSoftBusTransportRos2Host CONFIG REQUIRED)` 和
`FastDDSDSoftBusTransportRos2Host::factory_configuration` 导出 target 编译，业务
源码不包含 Transport 私有头。运行时还需把 `$vendorPrefix/bin`、ROS DLL 目录和
依赖 DLL 目录加入 `PATH`。

## Required flat configuration and Host run

required 模式使用一个由两端各自读取的 flat config。当前 V1 配置必须显式选择
`locator_version=V1`，并提供 `namespace`、`local_udid`、`peer_udid`、
`service_slot`、`peer_slot` 和 `profile=BYTES_NATIVE`。UDID 来自已授权的部署输入；
该路径不申请新的 SDK API 或 permission，也不从 `networkId`、device name 或随机
runtime id 推导身份。下面是当前 Host 示例的完整 V1 键集合；A/B 只改变 `side`、
监听端口和对端端口，`run` 必须是同一个 16 位十六进制标识，且所有路径为绝对路径：

```text
bridge_library=D:/install/fastdds-dsoftbus-ros2-host/bin/fastdds_dsoftbus_ros2_host_bridge.dll
fastdds_module=D:/install/fastrtps-2.14.5/bin/fastrtps-2.14.dll
fastcdr_module=D:/deps/ros2-jazzy/bin/fastcdr-2.2.dll
backend=HOST_OPAQUE_BYTES_TEST_ONLY
locator_version=V1
namespace=ros2.synthetic
local_udid=synthetic-local-A
peer_udid=synthetic-local-B
service_slot=17
peer_slot=18
profile=BYTES_NATIVE
run=0123456789abcdef
side=0
port=41001
peer_port=41002
domain=201
forward=1
```

B 使用相同的 library/module、backend、namespace、run、domain、forward 和
`locator_version`/`profile`，并设置
`side=1`、交换 `local_udid`/`peer_udid` 与 `service_slot`/`peer_slot`，并设置
`port=41002`、`peer_port=41001`。启动每个普通 ROS executable 之前，
分别设置自己的 config 路径；不能在 Participant 创建后切换配置：

旧的 probe 配置如果缺少这些 V1 identity/slot 字段会在 descriptor publication
之前拒绝；它不能通过回退到 legacy port-1 locator 继续运行。历史文档中把多个
补丁脚本手工串联的示例也不适用于当前 package；当前唯一入口是
`integrations/ros2/prepare_fastdds_source.py`，它必须从声明的 2.14.5 baseline
执行完整五步并校验六个头文件。

```powershell
$env:RMW_IMPLEMENTATION = 'rmw_fastrtps_cpp'
$env:ROS_DOMAIN_ID = '201'
$env:ROS_AUTOMATIC_DISCOVERY_RANGE = 'SUBNET'
$env:SKIP_DEFAULT_XML = '1'
$env:PATH = "$vendorPrefix/bin;$ros/bin;D:/deps/pixi/Library/bin;" + $env:PATH

$env:FASTDDS_DSOFTBUS_REQUIRED_CONFIG = 'D:/work/ros2-transport-consumer/A.conf'
$a = Start-Process -PassThru -WindowStyle Hidden `
  -RedirectStandardOutput 'D:/work/ros2-transport-consumer/A.stdout' `
  -RedirectStandardError 'D:/work/ros2-transport-consumer/A.stderr' `
  -FilePath "$consumerBuild/Release/ordinary_ros2_transport_consumer.exe" `
  -ArgumentList 'A','0123456789abcdef','reliable'

$env:FASTDDS_DSOFTBUS_REQUIRED_CONFIG = 'D:/work/ros2-transport-consumer/B.conf'
$b = Start-Process -PassThru -WindowStyle Hidden `
  -RedirectStandardOutput 'D:/work/ros2-transport-consumer/B.stdout' `
  -RedirectStandardError 'D:/work/ros2-transport-consumer/B.stderr' `
  -FilePath "$consumerBuild/Release/ordinary_ros2_transport_consumer.exe" `
  -ArgumentList 'B','0123456789abcdef','reliable'
$a.WaitForExit(); $b.WaitForExit()
if ($a.ExitCode -ne 0 -or $b.ExitCode -ne 0) {
  throw "ROS consumer failed: A=$($a.ExitCode), B=$($b.ExitCode)"
}
```

### DSoftBus Socket 服务名配置

ROS 目标配置中的服务名前缀分为两层。私有 profile 中的
`ros2_service_prefix` 只保存项目配置的 name stem，例如 `ros`；写入每端
`ros.conf` 时，执行器会将它序列化为
`authorized_package + "." + ros2_service_prefix`，例如
`org.example.transport.ros`。其中 `authorized_package` 必须与实际授权的
Socket package 一致，不能使用与授权身份无关的裸 stem。`TargetBridge` 随后在
该完整前缀后追加当前 `run`、`side` 和 `role`，以生成 listener、outgoing 及对端
匹配所用的服务名。这样可以保持服务名处于已授权的 package 命名空间内，也避免
通过启发式改写或扩大权限通配符来“修复”名称。

下面仅展示脱敏后的形状；`org.example.transport` 是示例 package，实际部署值
必须来自当前授权配置：

```text
authorized_package=org.example.transport
service_prefix=org.example.transport.ros
run=<current-run-id>
side=0
```

当前配置修复经过正式 materializer、local query/restore、window preparation
和命名空间测试验证；该验证没有执行新的设备操作，也不能替代目标板上的
Socket/Listen/Bind 或权限策略验证。详见
[本次配置审查报告](../.artifacts/ros2-device-acceptance/dsoftbus-call-review-20261002/FINAL_REPORT.md)、
[配置差异审查](../.artifacts/ros2-device-acceptance/dsoftbus-call-review-20261002/FINAL_DIFF_REVIEW.md)
及其[独立测试收据](../.artifacts/ros2-device-acceptance/dsoftbus-call-review-20261002/independent-test/TEST_RESULT.json)。

对于既有 Host 双进程验证，应同时启动 A/B 并使用
`tests/ros2_host/run_pair.py`。该入口为每个 fresh run 生成唯一 run/端口和两份
Host config，保存 stdout/stderr、输入 hash、实际 loaded DLL、Writer GUID、payload、
graph、service、close 和 pump 收据。将 `reliable` 改为 `best_effort` 可验证另一种
QoS；示例中的输出不能手工填写 PASS。该 Host 夹具不代表目标板部署、设备权限或
真实 DSoftBus Socket/Listen/Bind 已经验证。
每个进程只创建一个 Participant、一个 Runtime/descriptor 和一组 ROS 实体，
由集成装配管理 pump；业务节点不自行启动或销毁 DSoftBus Runtime。

外部 ROS 项目使用：

```cmake
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(example_interfaces REQUIRED)
find_package(FastDDSDSoftBusTransportRos2Host CONFIG REQUIRED)

add_executable(my_ros_node main.cpp)
ament_target_dependencies(my_ros_node rclcpp std_msgs example_interfaces)
target_link_libraries(my_ros_node
  FastDDSDSoftBusTransportRos2Host::factory_configuration)
```

`main.cpp` 使用普通 `rclcpp::Node`、publisher/subscription、service/client 和
`rclcpp::shutdown()`。应用不创建 Fast DDS Participant，不调用 Socket/SendBytes，
也不拥有 DSoftBus Runtime 或 pump。配置应在进程启动前通过既定环境/配置入口提供，
不能在 Participant 已创建后热切换。

## 当前 Host 证据

最终 Host 收据（`run-20260930-resume`）记录了：

- Reliable：run `b6a383a1775fa5a8`，两端各 5 writes/5 takes，真实 Writer GUID、
  graph、service response 和正常关闭均通过。
- Best Effort：run `033add5dd4ea7f22`，两端各 5 writes/5 takes，正常关闭通过。
- builtin/XML 配置路径：run `6c386f43acd5fa0b`，配置后的 builtin/intraprocess 状态
  与不透明字节 I/O 有记录。
- required XML/DataSharing 冲突：run `b6d02b71d56419ca`，两端均按合同拒绝，
  没有 writes/takes。
- 禁止转发：run `b9f607da0d3e64dd`，两端均按合同失败且没有业务交付。

安装 prefix 外部消费者、版本绑定 ABI import guard、完整 CONFIG 及受影响 ASan
结果由对应 tester/review 收据分别记录。旧设备 N10/N100 证据保持原 run 和包身份，
本轮没有 ROS 设备执行；没有因此获得生产资格或 PF/P0/P4 放行。

本次最终测试还包括完整 CONFIG `531/531 PASS`，受影响 ASan `4/4 PASS`，以及
2.14 owned Runtime/Bridge 的双进程 5/5 Host 结果。外部 ROS/Fast DDS 依赖未插桩；
Windows 不提供 LSan，因此这些结果不包含外部依赖的插桩泄漏结论。
完整 CTest 的 `LastTest.log` 中，`DLL-internal LocatorList allocation injection`
因未观察到 exe allocator 注入点而标记 `SKIPPED`；该故障子项没有被宣称为 PASS。
此前命名阶段的 `530/531` 失败仍是历史收据，当前 `531/531` 属于新的完整收据。

提交的 vendor 交付材料是完整的五步版本绑定补丁链（`prepare_fastdds_source.py` 及其五个
应用脚本、四份 `*_UPSTREAM.json` 和六个头文件），属于已审查的配置接入，不是 Fast DDS 协议改动。原始 vendor 源、依赖 prefix、
构建目录、DLL 和测试 artifact 不随项目 clone 提供；需要按上述版本与 hash 重新
准备，或使用保存的本地交付收据核对。

## 生命周期和限制

工厂配置拥有 descriptor、Runtime 和 pump 的装配；业务节点只拥有 ROS 实体。正常
关闭顺序是停止新业务、删除 DDS 实体、保持 pump 可推进、关闭 Runtime、再释放
公开对象。Host 收据验证了该路径，但不证明 SDK 内部 epoch、drain 或 cancellation
保证，也不等同于 OpenHarmony 双板 ROS 2 运行。

默认 `NativeBackendUnavailable`、生产 backend 禁用、`QualificationProfile` 未生成。
目标设备上的 DSoftBus 加载、权限、平台库来源和跨设备 ROS 2 通信必须另行按设备合同
验证，不能由本 Host 结果推断。

## 当前 Host 集成收据（2026-10-02）

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

证据入口：[当前 Host 集成收据](../.artifacts/ros2-host-integration/run-20261002-current/FINAL_REVIEW.md)。
构建依赖、安装目录和外部输入属于本机只读材料，不随项目 clone 提供。

## 三项离线故障修复状态（2026-10-02）

本轮离线修复已通过限定范围的最终 tester 和独立 review：业务期限在
`READY` 阶段释放后重新起算；加载/分析子进程的未确认关闭会阻止后续非必要采集
和阶段释放；已知的 `Failed to delete participantROS_CONTEXT_CLOSED` 拼接行只按
严格格式解析，并分别保留 participant 删除失败与应用状态。实际 Host 延迟
31 秒释放测试和修正配置后的 ASan 等价延迟测试均完成 20/20 Topic 交付、graph、
service 和正常 Runtime 关闭。相关脚本、控制器和分析器测试均通过；这些结果仍是
Host/WSL 或本地夹具证据。

本轮目标消费者重新构建并产生 109 个成员，其中旧包的 108 个未变；该目标配置
构建的部分命令记录由最终缓存和原始输出重构，不能当作原始完整准入审计。没有执行
HDC、设备查询、部署或设备程序；历史设备窗口 `f389bce8e0d13d5c` 仍为 FAIL，
A 端退出码 139 的原因仍 UNKNOWN。当前状态为实现和限定 Host 验证 PRESENT/PASS，
设备验证 BLOCKED，未获得生产资格，也未创建提交。

本轮证据入口：[三项修复最终报告](../.artifacts/ros2-device-acceptance/three-bug-repair-20261002/FINAL_REPORT.md)、
[最终 tester 收据](../.artifacts/ros2-device-acceptance/three-bug-repair-20261002/final-tester/TEST_RESULT.md)、
[独立 review](../.artifacts/ros2-device-acceptance/three-bug-repair-20261002/REVIEW.md)。

## Fast DDS 2.14 direct-DDS 层补齐状态（2026-10-02）

本层新增的是安装包消费的 direct DDS 2.14.5 基线示例，不是 ROS/RMW 实现，也不是
设备验收。示例通过已安装的 `factory_configuration` target 创建 Participant、
Topic、Writer 和 Reader，使用两个方向独立的 run-scoped Topic；应用不 include
Transport 私有头、不直接调用 Socket/SendBytes，也不创建第二套 Runtime 或 pump。

| 项目 | 当前证据状态 |
| --- | --- |
| 安装后 Host consumer、CDR round-trip/bounds | PASS；CTest 1/1，CDR 五项检查 |
| direct DDS normal | PASS；run `71997f4878cde186`，两端各 5 writes/5 takes，合计 10/10，peer GUID/SN、DDS wait rc=0、公开删除 rc=0 |
| Host delete-refusal 对照 | PASS；run `0af25762d43a13cf`，公开 owned endpoint 列表证明仍有 1 writer/1 reader，公开 Participant 删除返回 PRECONDITION=4，随后继续数据并正常清理 10/10 |
| forwarding disabled / missing bridge | PASS（预期失败）；均无阶段释放、0/0 业务数据 |
| 修复后的 analyzer 与历史 raw replay | PASS；5 个 analyzer 测试，重复/倒序 DDS writer SN 均被拒绝；raw replay 的规范化文本 hash 与原始字节 hash 由 `RAW_REPLAY_HASH_MAPPING.json` 分开绑定 |
| ASan | PASS；direct DDS normal/refusal 各 10/10；Fast DDS、Fast CDR 和其它预置依赖未插桩 |
| API23/AArch64 target static package | PASS（静态）；8 个成员，含 `libfastrtps.so.2.14`、`libfastcdr.so.2`、目标 bridge 和 token bridge；不含 `rclcpp`、`rmw`、3.6 或 Fake |
| 目标加载、Socket 调用、双板运行 | NOT_RUN / NOT_ADMITTED；本层没有 HDC、query、deploy 或 target execution |

这些 Host normal/refusal 结果使用明确标记的 `HOST_OPAQUE_BYTES_TEST_ONLY` 设施；它们
证明 direct DDS 2.14 的安装消费、实体身份、样本账本和公开关闭顺序在 Host 范围内成立，
不证明 OpenHarmony DSoftBus 设备加载或 SDK 内部 drain/epoch/cancellation。日志中的
`FDDST_ROS_CLOSED` 出现在公开 `delete_participant` 调用期间，随后公开调用返回 0；文档
不将其描述为 API 返回后的关闭。

本层冻结收据入口为
[最终 tester](../.artifacts/ros2-device-acceptance/layer-gap-validation-20261002/final-tester/TEST_RESULT.md)、
[最终差异 review](../.artifacts/ros2-device-acceptance/layer-gap-validation-20261002/FINAL_DIFF_REVIEW.md)、
[静态选择收据](../.artifacts/ros2-device-acceptance/layer-gap-validation-20261002/STATIC_SELECTION_RECEIPT.json)以及
[raw replay hash 映射](../.artifacts/ros2-device-acceptance/layer-gap-validation-20261002/final-tester/RAW_REPLAY_HASH_MAPPING.json)。
旧 ROS 设备失败、A 端 139 的 UNKNOWN 原因、历史 query/data 额度 0 均保持不变。
“下一步设备请求仍是未授权草案”属于本节所引用的离线/预设备证据时点；它不覆盖
下面已经保存的分层设备窗口结果。

## ROS teardown 离线闭合状态（2026-10-02）

本轮只完成离线 Host/ASan 诊断与收尾修复，未执行 HDC、设备、查询、部署或 Git
提交。新观察将 Fast DDS 原始删除返回、项目 Close ABI 结果和映射后的公开返回分开
保存；删除前快照、逐项删除返回和清理结果不再依赖正常 Close 才能导出。诊断缓存为
进程生命周期的 metadata-only 存储，不持有 Participant、Runtime、owner、payload
或 SDK 资源。

最终 Host 正例使用 rclcpp/rmw_fastrtps，完成双 QoS 20/20、graph、service 和正常
关闭（run `6378fc65ca11f50a`）；Host-only 注入的 Close 负例保留原始 DDS 删除值
`0`、项目 Close 值 `2`、映射公开返回 `1` 和应用退出 `4`。另有实际实体仍存活时的
公开删除拒绝对照：Topic 删除返回 `4`，随后由原责任方完成合法清理。上述负例用于
验证首错和责任记录，不是历史设备故障的根因证明。

控制器现在将业务阶段和 cleanup 分开；A 侧失败时允许 B 进入有限合法清理，且不把
cleanup 结果改写成业务成功。跨语言七阶段合同、collector 尾部排空、失败导出和
物化依赖检查均通过最终受影响测试。完整 Host CONFIG 的 346 项运行发生在最后两项
跨语言修复之前，结果为 344 通过、2 项 PowerShell 环境失败；两项随后在显式 PS5
模块路径下通过，因此不标记为最终源码的 346/346。ASan 只覆盖本项目重建的 runtime、
bridge、consumer 和 metadata cache，预置 ROS/RMW/Fast DDS 依赖未插桩。

历史 ROS 设备窗口 `34d9a4ba979667d9` 仍为 FAIL，原始 Participant 删除数值未被
保存，继续为 UNKNOWN；Host 负例不回填该设备原因。直接 DDS 设备 PASS、ROS 设备
20/20 业务记录及 graph/service 记录均保持原修订归属，ROS 设备缺少最终 native
结构化链，不能升级为 ROS 设备通过。设备额度仍为零，`DEVICE_NOT_ADMITTED`、默认
`NativeBackendUnavailable`、P0/PF/P4、SDK epoch/drain/cancellation 和
`QualificationProfile` 状态不变。

证据入口：[离线 teardown 收据](../.artifacts/ros2-device-acceptance/offline-teardown-20261002/)、
[对象责任差异](../.artifacts/ros2-device-acceptance/offline-teardown-20261002/OBJECT_RESPONSIBILITY_DIFFERENCE.md)、
[最终 tester](../.artifacts/ros2-device-acceptance/offline-teardown-20261002/final-tester/TEST_RESULT-r3.md)、
[独立 review](../.artifacts/ros2-device-acceptance/offline-teardown-20261002/REVIEW.md)。

## 分层设备验收（2026-10-02）

本轮新增的两类设备窗口必须分开解释。直接 Fast DDS 2.14 窗口
`a5542422e96f4508` / `058f528038f39cc6` 已在登记双板上完成限定范围验证：两端各
5 次写入和 5 次对端读取，结构化 SDK 观察各为 5 条，公开 DDS wait、实体删除和
Participant 删除均返回 0，加载映射与恢复 guard 通过。该结果只覆盖 direct DDS
2.14 层，不覆盖 ROS、`rmw_fastrtps`、graph 或 service。

随后独立 ROS 窗口查询 `f7ba02faa5713877` 通过并正常恢复，数据窗口
`34d9a4ba979667d9` **FAIL**。原始应用文本支持 20 writes/20 takes、四条 graph
记录和两组 service 记录，且两端加载了 109 成员的 ROS/2.14 链；但控制器在
`ABEST_EFFORT_DONE` 后没有发出 `BBEST_EFFORT_DONE`，B 侧产生
`PHASE_COMPLETION_RELEASE_MISSING`。两侧均记录了 Participant delete 和 Factory
normal-close 失败；应用退出码分别为 A=0、B=4。这里的关闭失败记录不表示两端
`rcl` shutdown 都以非零退出。该窗口没有 `ROS_NATIVE_SUMMARY`、`ROS_NATIVE_DATA`
或 `FDDST_REAL_SOCKET`，因此 20 条 ROS 样本的 SDK 结构化发送/接收链为
`NOT_PROVEN`。Guard 通过只证明记录的环境恢复范围，不能改写为正常 ROS/RMW/DDS/
Runtime 关闭成功。

本轮事实状态：

| 范围 | 状态 |
| --- | --- |
| Direct DDS 2.14 双板正常窗口 | PASS（限定 5/5 每端） |
| ROS 2 双板业务窗口 | FAIL；data run `34d9a4ba979667d9` |
| ROS Topic/graph/service 文本子集 | 已观察，但不能覆盖整体失败或 native 链缺失 |
| ROS 正常关闭 | FAIL/不完整；不是 guard PASS |
| 新增 DDS/ROS query 与 data 额度 | 各自均已消费；剩余为 0，无 repair retry |
| 生产资格、P0/PF/P4、SDK epoch/drain/cancellation | 未证明，保持原门禁 |

证据入口：[分层验收收据](../.artifacts/ros2-device-acceptance/layered-device-20261002/)、
[direct DDS 独立验收](../.artifacts/dds214-layered-acceptance/dds214-20261002-r1/device-independent/TEST_RESULT.md)、
[ROS 独立失败审计](../.artifacts/dds214-layered-acceptance/ros2-20261002-r1/device-independent/TEST_RESULT.md)。
历史 ROS 失败窗口、A 侧退出 139 的未知原因和旧额度账本保持不变；本轮没有提交，也没有
把 ROS 窗口标记为设备通过。
## Task-scoped teardown stop（2026-10-03）

本次授权任务的 source/repair 已通过 Host 与独立 review；final tester 的 `10/10`、
`21/21`、`7/7`、`5/5`、`7/7`、`8/8` 是不同 scope，不能相加为单一功能总数。r19
model-native 材料不是 PASS，r18 taint 材料不是 canonical；role binding 修正保留在
原始收据中。实际 query `c94d3fc03c6dc707` PASS 且完整恢复。

唯一数据准备 `cfb0f5d9f48ff4d5` 在 B 首个基线系统 hash 步骤因
`HDC E001005 Device notfound` 停止：local rc0 但缺 remote marker。A 的 ELF/辅助发送
准备记录存在；未发生 data grant、ROS 启动、DATA marker、central data claim 或业务
样本。该结果是 pre-business infrastructure/connectivity failure，不是 SDK/ROS data
FAIL。正常双端 restore 收据缺失，后续 connectivity 检查不等于 restore。

当前状态：`QUERY=PASS / DATA_PREPARATION=FAIL / ROS_DATA=NOT_RUN / DEVICE_GATE=BLOCKED /
NOT_COMMITTED`。旧 N10/N100、ledger、query 与历史失败保持原身份；默认
`NativeBackendUnavailable`、P0/PF/P4、生产资格不变。本任务停止，不启动新的 business
执行。证据入口：[task FINAL_REPORT](../.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/FINAL_REPORT.md)、
[EVIDENCE_INDEX](../.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/EVIDENCE_INDEX.md)。

## Repository-contained ROS2 build usage（2026-10-03）

ROS 2 source for this phase is contained in the repository under `ros2/src`; an empty
external `Documents/ros2/src` path is not the source-of-truth location. The target
rebuild entry `integrations/ros2/target/rebuild.py` uses only repository
`build/ros2-target` output/temp/install/package plus installed SDK/Host tools. It does
not require pre-populating an external source tree.

For a fresh output directory, run from this repository:

```powershell
python -B integrations/ros2/target/rebuild.py --output build/ros2-target-r2 --phase all
```

An unfinished build can continue with `--phase prepare`, `fastdds`, `ros`,
`assembly`, `consumer`, or `package`; use the same explicit output directory.
Completed package/binding receipts are not overwritten. SDK, CMake, Ninja,
Host Python and Host ROS generators are explicit CLI inputs. The existing
API23 dependency prefixes and identity bridge under `.artifacts` are checked
inputs, not files automatically supplied by a clone. The build Python selection
must not replace the independently bound device-executor Python environment.

The current build evidence records Fast DDS 2.14.5, RMW 8.4.3, rcl 9.2.11 and rclcpp
28.1.22, a transport installation, an independent installed consumer, and a 109-member
AArch64 package with maximum package-source path 155 and no RPATH/3.6 Host Fake leakage. Final
layout/vendor/debugger/task/Node checks are recorded in the repository-build artifact;
these are build/compile/static or Host checks, not ROS device acceptance.

The build-only evidence preceded iteration 6 device selection; its subsequent
query/preparation/recovery is recorded below. Existing data4 21-topic residuals,
A139 uncertainty and RMW cast/map reference
uncertainty remain historical. Default `NativeBackendUnavailable`, P0/PF/P4 and
production restrictions remain unchanged. Evidence: [repository build result](../.artifacts/ros2-repo-build-20261003/BUILD_RESULT.md)
and [evidence index](../.artifacts/ros2-repo-build-20261003/EVIDENCE_INDEX.md).

## 历史快照：Current continuation stop（2026-10-03）

本次 query `a35093db86b9cc67` 通过并完整恢复；错误 Pixi 验证
`9135d4608d7abbe5` 在 claim/HDC 前失败，不能与实际 query 结果混称。数据准备
`81ccfee5ad0dbc7e` 在 B 首个 action_msgs 类型支持库 hash 步骤因缺少
`MDDS_REMOTE_EXIT` 停止；未发生 grant、ROS 启动、DATA marker 或 central data claim。
已有 recovery `f4022cac...` 及独立接受通过，114 个 owned deployment files/side
保留并验证，但这不是正常 ROS close。

当前 repository build 已在 `build/ros2-target` 路径解决旧 45 项 legacy-path inputs；
原 C-root prefix 不恢复。实际执行器副本的发送路径最大170字符，辅助文件最大189字符。
本次预检、查询、部署与恢复实际使用了HDC；没有启动ROS业务，也未在恢复后创建下一
continuation或开启新设备窗口。Git/production 状态不变。历史 21-topic、Participant=4、A139 和无新 RMW branch
观察保持原结论。详见 [current turn report](../.artifacts/ros2-repo-build-20261003/CURRENT_TURN_REPORT.md)
及 [evidence index](../.artifacts/ros2-repo-build-20261003/CURRENT_TURN_EVIDENCE_INDEX.md)。

## Observer lifetime repair（2026-10-03）

时间点说明：此前 `cfb0f5d9f48ff4d5` 已完成追加恢复并获独立接受；随后 query `c4c95555ba2f3fc9` PASS，data `7cec05d3a24e5b4c` 观察到 ROS Topic 20/20、graph/service，但正常关闭 FAIL、声明范围内环境 guard PASS。本次 observer 修复工作没有新的设备操作。

`NativeObservation` 的固定元数据状态已按受审方案改为在控制侧 `Begin` 完成初始化并发布，随后保留为有界进程级元数据；callback 路径不再惰性构造观察状态，也不持有 Runtime、SDK handle、payload、receiver、thread 或 module 资源。独立 review 绑定为 PASS，Host/ASan 定向范围各 7/7，普通 ROS Host 双进程为 20/20 且正常退出，Close-error 负例保留预期应用退出 4；完整 CONFIG 收据为 544/544、无失败、无 skip。目标包 109 个成员中只有 bridge 变化，设备未加载、未执行 HDC。

该修复关闭的是 observer metadata lifetime/诊断可用性问题，不关闭 ROS 设备 teardown。既有设备 data 结果仍为 FAIL：两端 Participant 删除返回 4/`ACTIVE_ENTITIES`，Topic cast/map/reference 责任和残留集合仍未确定，A 端远程 139 仍 UNKNOWN，native 完整链仍缺失。当前设备状态继续为 `DEVICE_NOT_ADMITTED`；不把 Host 20/20、guard 或 observer 修复写成设备通过或生产资格。证据入口：[observer lifetime repair FINAL_REPORT](../.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/observer-lifetime-repair-20261003/FINAL_REPORT.md)、[EVIDENCE_INDEX](../.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/observer-lifetime-repair-20261003/EVIDENCE_INDEX.md)。

## 历史 paired normal-close 双板验收（2026-10-04）

后续受限设备窗口已完成 `ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_VERIFIED`。查询
`d1c2cb4097279214` 和数据 `be8cb2d90f287223` 使用 SDK23/AArch64 构建的
`build/ros2-target/r2`，通过 SDK23 HDC 部署到原登记双板。该结果更新本文档此前的
`DEVICE_NOT_ADMITTED` 快照；此前历史失败及其原始收据仍保持不变。

设备端实际范围为普通 `rclcpp`、未修改 `rmw_fastrtps`、隔离 Fast DDS 2.14.5 和本项目
DSoftBus Socket/Binding。RELIABLE、BEST_EFFORT 均为双向 5 writes/5 takes，合计
20/20；graph 和 service request/reply 单独记账。两端 Reader/Writer/Topic/Publisher/
Subscriber 删除及原始 Participant 删除均返回 0，Runtime integration close 和 public
factory delete 均为 0，应用与远端进程正常退出，完整恢复 guard 通过。预删除容器快照为
空且明确标记为顺序、非原子采样。native 结构化记录与 20 条 Topic 接收具有同 run 的
逻辑/结构关联，不能扩大为 SDK 内部 epoch、逐物理包因果或完整 drain 证明。

本轮修复并纳入最终包的范围包括：Topic RTTI key 修复后的删除路径、creation-complete
与 failed-construction 区分、每 Runtime 的 pre-native-close paired 协调，以及共享
控制出口的串行化；没有修改原版 RMW。目标身份为 Fast DDS 2.14.5、Fast CDR 2.2.5、
RMW 8.4.3、rcl 9.2.11、rclcpp 28.1.22；目标包 109 成员，构建和临时输出均位于
仓库 `build/ros2-target`。Host、目标静态审计和设备原始收据分开保存，不能将 Host
数字相加成一项全集测试。

状态边界：本节限定功能已 `DEVICE_TESTED / VERIFIED`；验收当时 HEAD 为
`4807765f53826ed0b882cac32610acb17ee30b4f` 且尚未提交，后续提交状态以 Git 历史为准。
P0/PF/P4 原状态和默认
`NativeBackendUnavailable` 保持不变。SDK epoch/drain/cancellation 仍未证明，当前不具备
生产资格，未生成 `QualificationProfile`。历史
A139、早期 Participant/Topic 删除失败及其原因不被本次成功回填为已解释。证据入口：
[最终报告](../.artifacts/ros2-topic-rtti-repair-20261004/paired-close/FINAL_REPORT.md)、
[EVIDENCE_INDEX](../.artifacts/ros2-topic-rtti-repair-20261004/paired-close/EVIDENCE_INDEX.json)。

### 可分发的最小证据索引

| 项目 | 记录 |
| --- | --- |
| Query / data | `d1c2cb4097279214` / `be8cb2d90f287223` |
| Target main / build manifest | `9dc9068c3ae6acf08a429d21df130e759bbd7d68a285697897ab5c5091fbd0f2` / `10664d4c00d7c986459086c141e37954a1597d39c95e278bfdc03962b6fa0a6a` |
| Independent review / raw teardown | `FINAL_DIFF_REVIEW.json` SHA-256 `6aeb7bd30957b4769d781648044032393cc9a91bb0bff519ef3dbf44c73a2687` / `RAW_TEARDOWN_ACCEPTANCE.json` SHA-256 `8705c369e08ba07fa11b68cf5de50b3e265775595dae611c42b550ea72b89bee` |
| Relative evidence | `../.artifacts/ros2-topic-rtti-repair-20261004/paired-close/FINAL_REPORT.md`; `../.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/iterations/iteration-20/windows/{query-15,data-15}/` |

这些路径和哈希用于绑定本机证据；`.artifacts`、SDK、设备运行库、私有配置和原始日志不随
项目 clone 或安装包自动提供。
