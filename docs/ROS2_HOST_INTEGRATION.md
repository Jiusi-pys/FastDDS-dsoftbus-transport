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
原 Git 源只作为归档输入读取，不要修改仓库中的 Fast DDS 子仓库。归档前先准备
一个全新的空目录和 tar 文件路径：

```powershell
$vendor = 'D:/src/fastrtps-2.14.5'
$archive = 'D:/archives/fastrtps-2.14.5.tar'
New-Item -ItemType Directory -Path $vendor
git -C D:/src/Fast-DDS archive --format=tar `
  --output=$archive ebcb5c05e9da9e262041e8616bbaff61ab40a296
tar -xf $archive -C $vendor
python integrations/ros2/apply_fastdds_hook.py --source D:/src/fastrtps-2.14.5
python integrations/ros2/apply_endpoint_guard.py --source D:/src/fastrtps-2.14.5
```

两个脚本会核对 `UPSTREAM.json` 中的原始文件 hash，并应用当前四个配置/工厂
补丁文件以及 `FastddsHook.hpp`。它们只改变版本绑定的 Participant/endpoint
配置接入和 owner 结算保护；不改变 UDP/TCP/SHM、SPDP/SEDP、History、ACK/NACK
或分片协议语义。

随附的 vendor patch 使用 zero-context diff；如确需手动应用，必须使用
`git apply --unidiff-zero`，但推荐始终使用上述两个带原始 hash 校验的脚本，避免
绕过版本基线检查。

用与 ROS Jazzy ABI 匹配的 Fast CDR、foonathan_memory 和 Windows OpenSSL prefix
构建这个隔离副本。下面的变量都是示例路径；实际构建使用 VS2022 x64 Release、
`/MD`、共享库、`SECURITY=ON`、`THIRDPARTY=OFF`，并关闭 vendor tests/examples：

```powershell
$vendor = 'D:/src/fastrtps-2.14.5'
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

required 模式使用一个由两端各自读取的 flat config。下面是完整键集合；A/B 只
改变 `side`、监听端口和对端端口，`run` 必须是同一个 16 位十六进制标识，且所有
路径为绝对路径：

```text
bridge_library=D:/install/fastdds-dsoftbus-ros2-host/bin/fastdds_dsoftbus_ros2_host_bridge.dll
fastdds_module=D:/install/fastrtps-2.14.5/bin/fastrtps-2.14.dll
fastcdr_module=D:/deps/ros2-jazzy/bin/fastcdr-2.2.dll
backend=HOST_OPAQUE_BYTES_TEST_ONLY
run=0123456789abcdef
side=0
port=41001
peer_port=41002
domain=201
forward=1
```

B 使用相同的前三个键、backend、run、domain 和 forward，并设置
`side=1`、`port=41002`、`peer_port=41001`。启动每个普通 ROS executable 之前，
分别设置自己的 config 路径；不能在 Participant 创建后切换配置：

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

对于实际双进程验证，应同时启动 A/B 并使用 `tests/ros2_host/run_pair.py`，它会
为每个 fresh run 生成唯一 run/端口和两份 config，保存 stdout/stderr、输入 hash、
实际 loaded DLL、Writer GUID、payload、graph、service、close 和 pump 收据。将
`reliable` 改为 `best_effort` 可验证另一种 QoS；示例中的输出不能手工填写 PASS。
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

提交的 vendor 交付材料只包含四个版本绑定的配置/工厂文件差异及两个应用脚本，
属于已审查的配置接入，不是 Fast DDS 协议改动。原始 vendor 源、依赖 prefix、
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
