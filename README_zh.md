# Fast DDS OpenHarmony DSoftBus Transport

[English](README.md) | 简体中文

本项目为 OpenHarmony 设备提供基于 **DSoftBus Socket API** 的
[Fast DDS](https://github.com/eProsima/Fast-DDS) 自定义传输层，用于通过
DSoftBus 承载 RTPS 数据。适用于需要在 OpenHarmony 设备间使用 DDS 的应用，
也为经由 Fast DDS 接入 ROS 2 提供基础。

传输层负责寻址、连接、发送、接收和资源生命周期。发现协议、可靠性、分片、
历史缓存和 QoS 仍由 Fast DDS 负责；DSoftBus 集成仅使用 Socket API。

## 架构

```text
DDS 应用 / ROS 2 应用
          |
 Fast DDS DDS / RTPS
          |
  TransportInterface
          |
 DSoftBus Transport
          |
OpenHarmony DSoftBus Socket API
          |
 对端 OpenHarmony 设备
```

仓库包含传输层实现、不依赖 SDK 的生命周期和路由组件、原生 Socket 绑定、
集成示例，以及主机和设备测试工具。stock 集成通过 Fast DDS 的公开传输配置
注册 Transport。较早的集成和诊断路径也保留在仓库中，并有各自的构建说明和证据。

## 选择构建入口

| 目标 | 入口 | 适用范围 |
| --- | --- | --- |
| 为 OpenHarmony 构建原生传输层 | [版本与发布指南](docs/FASTDDS_VERSIONS_AND_RELEASE.md)、`scripts/release/`、`integrations/ros2/stock/` | Stock Fast DDS 2.14.5、3.6.2 或 3.2.5；各版本独立的运行库和设备测试套件 |
| 在 Windows 开发核心组件、检查接口契约 | 根目录 `CMakeLists.txt`；命令见下文 | 默认 `SOURCE_PROBE` 绑定、不依赖 SDK 的组件和主机测试夹具 |
| 在两块开发板上运行未修改的 ROS 2 节点 | [Hub 部署指南](docs/ROS2_HUB_DEPLOYMENT.md)、[Stage 2 验收](docs/STAGE2_ROS2_DEVICE_20261105.md) | 仅 `2.14.5-ros`；限定范围的 `DEVICE_TESTED`，`NOT_PRODUCTION_QUALIFIED` |
| 使用较早的安装包或实验性 consumer | [安装指南](docs/INSTALL.md)、[安装输入预检](docs/INSTALL_PREFLIGHT.md) | 独立的根 `CONFIG` 与历史集成路径；须遵守各自精确的依赖和 ABI 要求 |

根目录默认构建用于主机开发。设备运行时需要按版本与发布指南显式选择原生构建、
平台库和部署配置。根 `CONFIG` 绑定与 stock 构建使用不同的 CMake 入口。

## 获取源码

需要 Git，以及访问已配置子模块远端的权限。当前子模块 URL 使用 SSH。

```sh
git clone --recurse-submodules git@github.com:Jiusi-pys/FastDDS-dsoftbus-transport.git
cd FastDDS-dsoftbus-transport
```

对于尚未初始化子模块的新 checkout：

```sh
git submodule update --init --recursive
```

父仓库固定子模块提交。复现某个 checkout 时应保留这些提交；把子模块更新到
远端分支会改变源码输入。发布脚本会按发布指南获取其显式选定的上游版本。
固定的提交还必须是子模块远端能够提供的提交，否则上面的 clone 对其他人会失败；
2026-11-09 据此修正了 DSoftBus 子模块的 pin，所消费的头文件没有变化，详见
[版本与发布指南第 5 节](docs/FASTDDS_VERSIONS_AND_RELEASE.md#5-reproducing-from-a-clean-clone)（英文）。

## Windows 主机开发

依赖：CMake 3.22 或更新版本、安装了 C++ 工具的 Visual Studio 2022、
已初始化的子模块、Python 3、Node.js（`node` 在 `PATH` 中）和
Windows PowerShell。请使用新的构建目录。

```powershell
cmake -S . -B build/host-source-probe -G "Visual Studio 17 2022" -A x64 -DFASTDDS_DSOFTBUS_FASTDDS_MODE=SOURCE_PROBE -DBUILD_TESTING=ON -DBUILD_NATIVE_PERMISSION_PROBE=OFF
cmake --build build/host-source-probe --config Debug --parallel 4
ctest --test-dir build/host-source-probe -C Debug --output-on-failure
```

这些命令验证所选主机配置，不会向开发板部署，也不构成设备验收。
使用模拟 Socket 符号的测试仅验证对应夹具范围内的主机契约。

## OpenHarmony 构建与使用

原生流程使用 OpenHarmony SDK 23 的 AArch64 工具链，以及与目标固件匹配的
平台库。跨板示例需要两台已配对的 OpenHarmony 设备、`hdc`，以及 DSoftBus
要求的进程身份和权限。已记录的硬件为运行 KaihongOS/OpenHarmony 的 RK3588A。

完整依赖、版本选择、构建命令、部署路径和跨板步骤见
[版本与发布指南](docs/FASTDDS_VERSIONS_AND_RELEASE.md)。其中的工具负责：

- `build_fastdds_ohos.sh`：构建固定版本的 Fast DDS 及其依赖源码。
- `pull_platform_libs.py`：收集目标设备上的链接输入。
- `build_transport_ohos.sh`：构建传输层和设备测试程序。
- `run_cross_board.py`：部署并执行选定的双板场景。

应用集成可从 [stock bootstrap 头文件](integrations/ros2/stock/TransportBootstrap.hpp)、
[作用域 bootstrap 封装](integrations/ros2/stock/ScopedTransportBootstrap.hpp) 和
[设备套件示例](tests/device_suite/README.md) 开始。选择匹配的 Fast DDS 版本，
显式提供运行时与对端配置，并确保同一进程内各依赖的 ABI 一致。

## 验证范围与限制

构建和测试证据对应具体版本、配置、设备镜像和场景。
[版本矩阵](docs/FASTDDS_VERSIONS_AND_RELEASE.md) 记录了 Fast DDS 2.14.5、3.6.2
和 3.2.5 的构建与双板测试，以及主机测试各自的范围。这些记录不代表所有
ROS 2 发行版或部署配置都已通过验收。

- DDS 功能、安全、统计和恢复场景的覆盖范围见版本矩阵及相应验收记录。
- Fast DDS 2.14.5 和 3.6.2 均有 ContentFilteredTopic 过滤器工厂竞态记录。
  已记录的规避方式是关闭 writer 侧过滤，并在应用侧串行执行过滤器变更与销毁。
  提议的上游补丁尚未应用。详见
  [CFT 结果与规避方式](docs/FASTDDS_VERSIONS_AND_RELEASE.md#64-content-filter-race-on-fast-dds-3x-re-evaluated-not-assumed)
  和 [2.14.5 调查记录](docs/FASTDDS_FULL_BUILD_AND_CFT_RACE_20261105.md)。
- ROS 2 验收单独记录在 [ROS 2 能力矩阵](docs/ROS2_CAPABILITY_MATRIX.md)。
  Stage 2 仅在 `2.14.5-ros`（Fast DDS 2.14.5 加一项已批准的补丁）上为限定范围的
  `DEVICE_TESTED`，且为 `NOT_PRODUCTION_QUALIFIED`，详见
  [Stage 2 验收](docs/STAGE2_ROS2_DEVICE_20261105.md)。每板多个 ROS 进程的用法、由固定版本源码构建的 Python 层、限制
  和生产资格尚缺的工作见 [Hub 部署指南](docs/ROS2_HUB_DEPLOYMENT.md)（英文）。
  stock 2.14.5、3.6.2、3.2.5 上仍为 `NOT_RUN`。规避方式无法通过 `rmw_fastrtps` 应用，因此 ROS 应用不得在
  writer 活动期间创建 ContentFilteredTopic。
  较早的 ROS 测试不会自动使新构建的 Fast DDS 或传输层二进制通过验收。
- 平台部署契约、生产资格和分发权利有各自要求，参见
  [生产支持契约](docs/PRODUCTION_SUPPORT_CONTRACT_V1.md)。

带日期的开发进度与剩余工作见 [STATUS](docs/STATUS.md) 及其中引用的验收文档。
[PLAN](PLAN.md) 保存设计和历史工作记录；各阶段结论仅适用于其对应证据范围。

## 仓库目录

| 路径 | 内容 |
| --- | --- |
| `include/fastdds_dsoftbus/`、`src/` | 传输层接口、实现和支撑组件 |
| `integrations/ros2/stock/` | Stock Fast DDS 传输层 bootstrap 和原生库构建 |
| `integrations/ros2/` | ROS 2 集成、每板 hub（`hub/`）、预加载库（`autoload/`）、历史适配器和诊断工具 |
| `scripts/release/` | 原生依赖构建、传输层构建和跨板运行工具 |
| `examples/`、`tests/`、`probes/` | Consumer、主机契约、设备场景和探针 |
| `docs/` | 架构、安装、支持契约和验收记录 |
| `Fast-DDS/` | Fast DDS 源码子模块 |
| `communication_dsoftbus/` | OpenHarmony DSoftBus 源码子模块 |

## 开发与贡献

架构边界和验证流程见 [AGENTS.md](AGENTS.md)。修改应限制在传输层和必要的
适配代码中，保留 DDS/RTPS 语义。实现变更需要独立执行证据和审查；涉及通信
行为时，还需完成适用的实机与跨设备验证。

## 许可证与第三方材料

依赖归属和分发限制见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
仓库目前没有根目录第一方发布许可证；各依赖的许可证不等于所有项目代码的
授权。厂商 SDK、设备镜像和平台二进制适用各自条款。
