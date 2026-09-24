# 有限实验源码基线

本基线整理从 `f6997da898d759b71a89421e4dd5e2cd471ac59c` 之后积累的实验源码、测试和工具。它保留默认关闭的实验入口；不是生产 Transport 发布。候选准备、本地提交和生产资格是不同门禁。

## 已有范围与证据

已闭合的限定 BYTES_NATIVE 双 RK3588A 实验包括真实 SPDP/SEDP/Reader 路径、共享 ProcessRuntime 与 application/receiver lease 接线、双实例局部删除、同进程同名 Participant 1/10/100 次功能性热重建、大样本 DATA_FRAG、受控接收边界缺失、multi-endpoint/QoS/late-joiner/isolation、keyed/History/resource-boundary，以及六配置三重复的有界性能测量。各项只按原报告的拓扑、QoS 和正常/受控条件成立，不能外推到所有使用组合。

原始 run、源码和 package 身份见 [EXPERIMENT_EVIDENCE_INDEX.json](EXPERIMENT_EVIDENCE_INDEX.json)。索引中的 `.artifacts/...` 是本地外部证据，**不随 Git 提供**；克隆代码不等于拥有这些日志、SDK、设备库或旧 sealed checkpoint。历史设备结果始终绑定原 run/package，不能称作未来某次提交已在设备执行。

历史完整 Host `243/243`、`245/245` 及后续分析器 `58/58` 只是历史收据。本次候选的新运行由独立 `COMMIT_READINESS` 收据记录，不用旧数字代替新执行。该收据属于提交准备工作目录，不是本文件的自我证明。

## 构建与外部依赖

- CMake >=3.22、C++17、Python 3、Node.js；Windows Host 验证使用 VS2022 x64 Release、`MultiThreadedDLL`，与依赖库 ABI 匹配。
- Fast DDS 固定 `343f155c36b561db3d9f047a65d866ce0f3da300 / 3.6.2.0`；声明并校验 Fast CDR 2.3.5、foonathan_memory 0.7.4 的配套 Host install prefix。不要把其它本机安装自动当替代品。
- 按 `.gitmodules` 和 gitlink 准备固定依赖源码。Host 也使用 Fast-DDS 的 HelloWorld 生成源和 DSoftBus 公共头文件。当前 DSoftBus 工具分支的局部修改不随本基线纳入；已消费的公共接口与所记录 gitlink 的对应关系在候选准备收据中核对。
- Native 构建另需声明的 OpenHarmony SDK 23、平台编译器、应用/平台 C++ ABI 依赖和权限桥接头文件/库。它们不在 Git 中；平台 access-token 桥接依赖不能称作全部来自 DSoftBus 公共 SDK。构建脚本参数显式指定实际位置，历史路径默认值不保证在新机器存在。

Host 示例（`<prefix>` 是已核验的外部 Release 安装目录；分别使用全新 OFF/ON build 目录）：

```text
cmake -S . -B <new-build> -G "Visual Studio 17 2022" -A x64 -DCMAKE_CONFIGURATION_TYPES=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DMDDS_FASTDDS_MODE=CONFIG -DBUILD_TESTING=ON -DBUILD_FASTDDS_DSOFTBUS_EXPERIMENT=ON -DMDDS_EXPERIMENT_NATIVE=OFF -DMDDS_EXPERIMENT_RX_DROP=OFF -Dfastdds_DIR=<prefix>/share/fastdds/cmake -Dfastcdr_DIR=<prefix>/lib/cmake/fastcdr -Dfoonathan_memory_DIR=<prefix>/share/foonathan_memory/cmake
cmake --build <new-build> --config Release
ctest --test-dir <new-build> -C Release --output-on-failure
python -m unittest discover -s tests -p dds_performance_analysis_tests.py
```

ON 指 `MDDS_EXPERIMENT_RX_DROP=ON`，不是启用 native。Host 参考 transport 不等于设备 DSoftBus 结果。确保 Node/Python、依赖 DLL 和 Windows PowerShell 标准模块在明确记录的环境中可见。单独的嵌套 Ninja build contract 测试需要 MSVC developer environment 及显式 prefix；普通 VS configure 不会替子进程设置该环境。

性能 Host 测试的冻结 plan/schema 已从历史 artifact 字节相同地纳入 `tests/fixtures/performance/`；它们是测试合同，不是 QualificationProfile。分析器和 fixture 不再偷偷读取历史工作目录。native 构建脚本保留正常 Git source-state 路径，另支持已审查的显式 `-RestoredSourceReceipt` 路径；没有使用收据就不得假称无 Git 的 source provenance 已验证。

`tools/checkpoint` 复用既有有界恢复工具，而不是新的自动部署框架。工具的 sealed checkpoint、声明依赖配置和历史集成测试数据是显式外部输入，不随克隆获得。其测试说明区分 portable contracts 与外部 pinned-declaration integration；所有代码来自本候选，外部数据只作为校验输入。工具没有授予设备运行或 Git 写入许可。

## 生成源、版权和固定身份

`large_sample/ExperimentLargeSample.idl`、`keyed/type/MddsKeyedSample.idl` 及各自七个 C++ 生成源保留，文件头标明 Fast DDS Gen 4.3.0。keyed 目录保留生成命令、Java/JAR 身份及全部输入/输出哈希；large_sample 的历史生成版本来自保留的生成头和对应实验构建证据，不能伪造未记录的生成器可执行文件哈希。Host/native 正常构建编译这些已保留源，不在 configure 时调用生成器。

生成源保留 eProsima 版权和 Apache-2.0 头；随附 `experiments/fastdds_socket/LICENSE.generated-Apache-2.0.txt` 为上游许可证正文，仅说明该生成代码的现有许可，不替整个项目新增统一许可声明。Fast DDS、DSoftBus 及其它依赖维持各自许可证。没有把 SDK、厂商二进制或整个系统目录纳入候选。

现有实验入口/绑定器及对应测试包含两块实验板的固定数值 AccessTokenID 和 selector。它们是真实设备本地权限身份，不是 synthetic 数据或 bearer 凭据；用户已明确允许本地实验候选保留这些安全准入所必需的现有绑定。没有放宽其白名单。凭据、私钥、live permission JSON、原始设备包及无关个人配置继续排除；源码存在这些入口不构成运行授权。

## 资格与操作边界

P0、PF-01/PF-02 保持 **BLOCKED / NOT QUALIFIED**；生产默认 **NativeUnavailable**，P4 未放行，SDK internal settlement **UNOBSERVED**。没有 QualificationProfile。正常热重建不证明旧 native 物理事件身份隔离；正常退出不证明故障取消上界；有限性能负载不证明最大吞吐或硬实时。

RAW、三板、mixed transport、native self-route 的未闭合范围、压力/故障关闭、长期稳定性及 ROS 2/RMW 不由本次基线整理升级。任何实际 HDC、权限、部署或发送都需其对应执行授权和 guards。

本次为 `PREPARE_ONLY`：不更改主仓库 index、refs 或 HEAD，不暂存、不提交；历史 sealed gate 中的 USER_PROHIBITED 原值保留。以后若用户单独授权本地 checkpoint，先核对候选/HEAD/index 漂移、Git 换行/过滤器和精确白名单，再按适用门禁执行；不得从准备通过推断许可。
