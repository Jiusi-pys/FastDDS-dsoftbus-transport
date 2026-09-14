# Baseline and evidence

## Current bounded evidence (2026-09-14 UTC)

The pinned Fast DDS host library preparation build is now PASS: `.artifacts/build/fastdds-logs/HOST_BUILD_RESULT_20260914.md` records 237/237 build steps and install exit 0. The source tree was not changed; the recorded UTF-8/compatibility flags were used and earlier failure logs are retained. This is build evidence only, not Transport, DDS integration, or upstream test evidence.

S3-D2 RuntimeSendPath / DSoftBusSenderResource is present and has bounded final evidence: `.artifacts/verification/runtime-send-final-20260914T0527Z/RESULT.md` reports direct Debug/Release 27 groups each, root Debug/Release 17/17, an independent public consumer, and SDK23 arm64 static build all PASS. Code review `.artifacts/review/20260914T052640Z-runtime-send-d2-code/REVIEW.md` found no actionable finding. This covers the immutable egress plan, current Fast DDS `send_lambda_` bridge, batch admission and resource release/close contracts; it does not cover a native backend, SDK SendBytes delivery, device Transport, DDS/RTPS or RMW.

The token-first Socket record experiment is PASS only within its bounded two-board diagnostic envelope. `.artifacts/diagnostics/token-first/20260914T051101677Z-execute/` validated five complete records (32, 255, 4096, 16384 and 60000 bytes) in each direction, with byte/length/count checks; Main audit and independent final read-only evidence are under `.artifacts/verification/20260914T045328Z-token-first-device/`. Both project identities were subsequently revoked and the original native-token file hashes restored. The probe explicitly records callback quiescence as NOT_VERIFIED; no production Transport, DDS or RMW claim follows.

## Latest evidence correction (2026-09-14)

The S3-C `InputChannels` correction has completed independent review and final bounded verification. `.artifacts/verification/facade-host-final-20260914T075048953Z/RESULT.md` records the post-OOM root CONFIG 20/20, SOURCE Debug/Release 19/19, FCD 21/21, IC-12 and public-consumer checks as PASS. The earlier 14/14 result remains historical. This supports `IMPLEMENTED / BUILDS / HOST_TESTED` for the component only; it does not support device callback, real Transport, DDS, or RMW verification.

Earlier cross-device Socket attempts with remote exit=6 remain historical FAIL evidence and are not overwritten by the successful token-first diagnostic. The old CommSvc thread-token denial remains a lead, not a proven root cause or fix. Descriptor facade, RuntimeSendPath and SenderResource bridge are implemented and verified only in their bounded host/ABI scopes; native backend/connection manager remains unavailable and the overall project remains NOT VERIFIED.

The independent thread-token diagnostic is now reviewed and measured on both RK3588A boards within its bounded scope. Device evidence is `.artifacts/diagnostics/thread-token/20260914T035011891Z-tester-device/`; result review is `.artifacts/review/20260914T035632Z-thread-token-device-results/REVIEW.md`. The retained runner label `.artifacts/verification/thread-token-runner-final-20260914T034800Z/` records actual execution at approximately 03:48:30–03:48:31 UTC. A observed old worker 806246851 unchanged while the main thread used 671793191; B observed 806017503 unchanged while the main thread used 671320465. Main-token restoration, exact PID disappearance, and persistent JSON/stat/label/guard invariants passed. This is evidence for the measured thread-token behavior only; it does not prove an actual DSoftBus callback identity or the Socket failure root cause.

本文件保留 S1 上轮可复现基线，并追加已完成的 S2-P 权限生命周期证据。目录名 `20260913T155515Z-final` 是 S1 的 UTC run label；当前权限 tester 证据为文末 `20260914T005000Z-permission-lifecycle-final`，不能将两者混写。

## S1 上轮 source and build

- Root HEAD: `1636043cbcc4b871d8969f9ed122b11f1d007885`。
- Fast DDS: `343f155c36b561db3d9f047a65d866ce0f3da300`，源码版本 `3.6.2.0`。
- communication_dsoftbus: `dd6037ce496828133bb9af89b37e2d08232a10d9`。
- SDK: `C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native`，native package 6.1.0.32，Clang 15.0.4。
- Host: Visual Studio 2022 / MSBuild 17.14.51，Debug；构建 PASS。
- Target: OpenHarmony arm64-v8a，实际 SDK toolchain/Ninja；构建 PASS。
- Host CTest: `.artifacts/verification/20260913T155515Z-final/host-ctest-debug.log`，7/7 PASS。
- Target build: `.artifacts/verification/20260913T155515Z-final/ohos-build-debug.log`，PASS。

以上是 S1 上轮源接口/能力探针构建，不是完整 Fast DDS 库、DDS 集成或 RMW 构建；不代表 S2-P candidate 目标已通过。Fast DDS 第三方子模块不与主机安装的其他版本混用。DSoftBus client 是探针运行时通过 `dlopen` 加载的设备库，不是交叉链接的 SDK client library。

## Devices

两块设备均为 KaihongOS 6.1.0.04 / API 23 / RK3588 EVB1：

- A: `3e01ff55454d202020104033bf453b00`
- B: `3e01ff55454d202020104433991c3b00`

运行库 `/system/lib64/platformsdk/libsoftbus_client.z.so` 两板哈希均为 `9b431d2976616becbbe5b3b18f3c9507ca42f7a15c965c9aa86457f4ff0e6087`。

最终远端产物哈希：

- `fastdds_interface_tests`: `e2773fb2024f15f8ebd81555b7fb2027c2f9bb4e1f9b11f44b500a0a4e50164b`
- `softbus_capability_probe`: `69aacda8df156ad3b1a562cb10ff5d0705bac84344a98832552096784d6fa1ed`
- `libc++_shared.so`: `d1a368b58e7cb80a7422b8bd344ec9328a43884d62f409af6e9ec161c73a2f4d`

## Evidence locations

- Initial tester: `.artifacts/verification/20260913T154230Z-initial/`
- Final tester: `.artifacts/verification/20260913T155515Z-final/`，started UTC `2026-09-13T15:55:15.3342482Z`
- Device preflight: `.artifacts/preflight/20260913T000000Z/`；真实采集时间见其中 `collection-time-utc.txt`。

早期无授权 Socket 检查 A/B 均返回 `-426115032`，耗时约 5015–5017 ms，证据位于 `.artifacts/diagnostics/20260914T000333Z-socket-recheck/`；同 PID 服务日志明确记录 DATASYNC 权限拒绝。该结果是历史无授权对照，不能继承为当前权限阻碍。随后受控授权闭环已证明 A/B 本机 Socket 创建/Shutdown 一次成功；最新 token-first 实验另已证明有界双板记录，但不覆盖生产 Transport 或完整回调排空。静态源码对照只能作为线索，不能替代当前固件行为证据。

## S2-P 权限探针最终证据

最终 tester run 为 `.artifacts/verification/20260914T005000Z-permission-lifecycle-final/`：Host configure/build exit 0，CTest 10/10 PASS（权限流程 10 groups、快照守卫 158 checks、记录契约纳入该 CTest）。使用官方平台 clang（包 SHA `716de5a203ae5cb0fc31e8e12a09c49cbb884c7a0fc0854b950f79a8dc84c157`，原始 libc++ ABI namespace `__h`）和板端 `libc++.so` 的 OHOS `native_permission_probe` 构建/链接 PASS；应用 SDK 的 `std::__n1` 链接失败是历史候选路径，不再作为当前 ABI 阻塞。

两板按顺序完成已归属身份的 regrant → 本机 Socket 创建/Shutdown 一次 → revoke：A token `671793191`、B token `671320465`；服务查询分别从 DATASYNC GRANTED 转为 DENIED，三个模式均恢复原 self。最终项目记录为空权限身份；origin/transition guard、JSON 外部结构、四个 provider 以及平台 C++ runtime hash 均保持。该 S2-P 闭环为限定范围 PASS；随后 token-first 双板记录的独立证据见本文件开头，完整 Transport、DDS discovery、用户数据或 RMW 仍未验证，整体仍 NOT VERIFIED。

旧应用 SDK `std::__n1`/`libc++_shared.so` 链接失败证据仍保留在早期 run 中，属于历史路径；当前默认 CMake permission target 尚未自动切换到平台 clang 路径，后续构建应显式复用该工具链并重新记录命令。

## S3-A 目录与 Locator wire 最终证据

- EndpointDirectory 最终 tester：`.artifacts/verification/20260914T011000Z-s3a-final/`；Debug、Release、consumer/library-only/cross 共 13 组 PASS，包含 review 修复后的 `FASTDDS_NO_LIB` CMake 路径。该结果只证明 host/cross build 的目录模型及其消费者，不能标记 DEVICE_TESTED 或完整目录 VERIFIED，也不证明 DSoftBus Socket 通信。
- Locator wire 最终 tester：`.artifacts/verification/locator-wire-final-20260914T013535Z/`；A/B 各 LW-01..LW-04 PASS，实际测试对象仅为 LocatorCodec 与 Fast DDS 3.6.2.0 的 CDR/ParameterList 编解码，不覆盖 EndpointDirectory 全部目录算法；独立 review 为 `.artifacts/review/20260914T013118Z-wire/REVIEW.md`。该测试不创建 DSoftBus 连接、不覆盖 SPDP/SEDP。

## S2 正向记录尝试审计

R1 修复后的 host/CLI negative 测试通过，但两次正向记录尝试不能记为 PASS。第二次记录 A Listen 返回 0，B Bind 等待约 6 秒超时，没有 OnBind 或数据回调；审计证据 `.artifacts/verification/20260914T012000Z-record-positive-second/audit-current/TEST_RESULT.md` 明确指出缺少 fresh guards、真实 probe PID 和 remote exit code。审计期间仅以只读方式确认 A/B 仍处于 revoked 安全终态（fresh hash、origin owner、service DENIED）。因此该次尝试不能证明跨板 records、Bind/SendBytes 或回调排空；后续 token-first 有界记录结果另见本文件开头。

RecordPairHarness initial/final host/simulated tests 已 PASS；独立 review 的 2 个 P2 已修复并完成 closure。旧 Socket positive FAIL 属于另一条错误 harness/实验路径，不能归因于 RecordPairHarness。Descriptor facade、Runtime 和 SenderResource bridge 已在各自限定范围实现并验证；native backend、真实 SDK Send 和完整 Transport 仍未完成，不能将 S2 records 或 S3-A 目录模型称为完整 Transport。

## N1 completion baseline

The SDK-free NativeOwnerTable model and public root wiring have an independent
completion record in [N1_COMPLETION.md](N1_COMPLETION.md). The final candidate
export is byte-exact under `core.autocrlf=false` / `core.eol=lf`; the initial
CRLF export is historical and was not used for the final result. External host,
SDK, compiler, toolchain and package inputs are captured with absolute paths and
hashes in `.artifacts/n1-completion/external-inputs.json`; these are declared
external prerequisites, not source artifacts reconstructed by this repository.
N1 is scoped PASS only and does not establish native DSoftBus, DDS, RMW, board,
or same-name Participant hot-rebuild verification.

RecordPairHarness final 证据为 `.artifacts/verification/record-pair-harness-final-20260914T014315Z/`，closure review 为 `.artifacts/review/20260914T014207Z-record-pair-harness-closure/`；该结果仅覆盖 host/simulated helper，不覆盖真实 SDK records。Record timing final 为 `.artifacts/verification/record-timing-final-20260914T014906Z/`，Debug/Release 各 21 组及平台交叉构建通过；A/B 拒权模式真实退出 6、非法参数退出 64，符合负例预期。这是退出码，不是测试组数；`BindReturned` 仅为时序 checkpoint。

SendQueue 原单目标 final 证据为 `.artifacts/verification/send-queue-final-20260914T023024Z/`；当前 S3-D1 batch final 证据为 `.artifacts/verification/20260914T041229981Z-send-batch-final/`，direct Debug/Release 各 26 组、root Debug/Release 各 14/14、consumer 与 SDK23 arm64 static build PASS。冻结哈希为 header `449967D484A09AB173C04FCB538CD8F1924E66A5049D0B786C210D42436B7AB4`、implementation `FBD412D50B1CF0705D0C1F7B1B16A691E796D50C7AB749F36DD7F54B8496592A`、tests `C0398F585D73D67F7DBC6C75437A37159C73BCBB81A9CA4C999A4B8B90BDF3C1`。独立 review `.artifacts/review/20260914T040359Z-send-batch/REVIEW.md` 无 blocking。该结果仅证明队列原子接纳与记录所有权移交，仍无 SDK Send/真实 Transport 或设备通信证据，因此 S3-B/D1 未 DEVICE_TESTED/完整 VERIFIED。
