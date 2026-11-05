> Evidence link maintenance (2026-11-05): unavailable historical local files are shown as explicit paths. Original labels, hashes and dated results are retained; see the [local evidence index](LOCAL_EVIDENCE_INDEX.md). Availability does not requalify an old run as current. Historical “byte-preserved” statements describe earlier archival steps; this navigation update changes Markdown bytes and does not assert the old document hash. Original execution results and artifact hashes remain unchanged.

# PV01：显式 PersistentNativeAnchor 双视图阶段记录（2026-10-23）

**有限 Host Fake 范围 PASS；Overall FAIL / NOT_VERIFIED；正常 Commit gate FAIL；Ready for commit: NO；DEVICE_TESTED=false；CROSS_DEVICE_TESTED=false。** 用户目标仍是“我需要完整实现 ros2 功能，不改变 transport 以上”；10月23日“windows已经重连，继续Fastdds-transport-dsoftbus项目的推进”（Sentinel_78def7a953c48191b57ddae71cf2cfe2）。本记录只同步已执行的 PV01 增量，不是完整 ROS2 验收或新周报。

## 实现与仍然存在的边界

精确六路径增量 +201/-28，封存文件 `main-persistent-review-artifact-d01/INCREMENTAL.patch` SHA256 `27c595cacc3acbe57ebe5a58ced6e61b927a1c1e3879023b740c3e29239670e9`。生产行为仅 `src/HostProviderLoop.cpp` 的 Join 移除 pinned 分支的 live_views!=0 限制；`ExperimentalRuntime.hpp`、`HostProviderLoop.hpp` 更新合同；其余是 `integrations/ros2/stock/CMakeLists.txt`、`runtime_views_tests.cpp` 和新增 `multi_participant_tests.cpp`。这六路径不是整个50项脏树。

现有显式 PersistentNativeAnchor 允许同 Runtime 最多两个 active 或 borrowed view，共享同一个活 native anchor，各自持有 receiver/egress leases；精确 owner/root、身份、双槽及8record/16entrance预算保留。第三个 view 被拒绝，借用未初始化 view 也占槽并使 CloseBusy；关闭一个 view 可独立释放槽，最后 view 消失仍不等于 native anchor 关闭重建。默认 Participant 仍 one-shot；local same-Runtime all-to-all edges 未提供；没有自动 ROS_DOMAIN_ID/native-domain 选择。

T2FactoryViews/T2Borrowed 强化为两次成功、第三拒绝；Held 用另一个未初始化 guard 占槽，继续检查 queued/entered lease、CloseBusy 和旧回调隔离。原 T2Domains 的621字节块完整保留，SHA256 `a1266107a4b5582fe5a5c2ee16139e5a5e395bef2ff5700a1e9b2ae73fa856fc`，仍在两个独立 Runtime 的首 Participant151/152 上 FAIL，不是被单view限制遮蔽。selected151 的 MakeStockDescriptor 是显式兼容边界；152Refused 是所选配置限制，不可当自动domain映射。DM01/DM02分别给出显式/默认限制的历史实测；旧SF stockUDP固定cross151/152也能通信，mapped不同domain负控才隔离，此处只复用旧证据，未本轮重跑。未来 default-admission-plan-d01 的50成员方案（PLAN SHA `c85197f079e5fae80d63edad99cd09abcdc7039b85ac4598e63f228ae08b07e5`）仅 PLANNED，authority/publication/refund/unique-stopowner 四生产候选未实现。

## 独立执行结果

PI01初测 → 一名独立 GPT-6.1 Sol reviewer 的三种视角审核 → PF01 post-review最终复测顺序完成；无有效P0–P3 finding，无review代码修改，Tier3风险保留，不虚构多模型审核或以无finding证明正确。最终源码865条 manifest SHA `b6b95df47a893d3a7dd02421f98286c946e822c01334f61f2c924627db5e2912`。

| 范围 | 实际结果与限制 |
| --- | --- |
| targeted |8/8实际 native wait/OS exit0 |
| component |188/188，CTest0；有限3.6接口/Host组件，不是完整3.6 DDS库 |
| stock |29/30，CTest8；仅原 `stock_transport.views.domains` nativeOS1 |
| remote crosspairs |同topic四crosspairs、八方向，精确remoteGUID/payload，8takes→关闭A0后4takes→替代view8takes，共20；replacement writerGUID改变，native计数未增加 |
| allocation |24次真实bad_alloc均失败并退款，注入成功数0；原view健康，取消注入后第二pair正常 |
| borrow/lifecycle |空owner并发borrow有2winners，第三拒绝，nativecalls0；heldRX/queued old lease/旧callback/CloseBusy及独立释放通过 |
| per-view selected domain |151Ready/152Refused；Factory非空/write0不能解释为实际nativeTX成功 |

真实 stock Fast DDS2.14.5/FastCDR2.2.5 Factory/Writer/Reader 与项目Transport经过 Host Fake public Socket 验证。PF01把22份PI01 exe逐字节复制到新 `build/r39-pv-tf01`，在新cwd/log重新执行；226个新native children=225 OS0+原domain OS1，均实际查询普通非AppContainer、非提权token，并记录Popen.wait/GetExitCodeProcess。用例集合、参数、timeout及必要环境与PI01相同，仅exe/cwd路径变；无最终build/link，无旧日志/cache改写。外部DLL68ed采样成功的用例与快速用例外采样UNKNOWN分开；进程内OS模块路径检查不是外部逐进程DLL哈希观测。

## 构建、资源与保留的失败

PI01 fresh source构建：genuine2.14实际输入611/31objects/root3.6输入0；有限3.6接口组件实际输入540/84objects/32rootheaders，不能称完整3.6库。实际SDK23无HostFake seam静态编译21TU/21AArch64 objects/21archive members/708inputs，archive SHA `fbfd2e2ca16ad64f095c45986250bb8574b043bb74607508092480a498a7be94`。PF01仅 NOT_REEXECUTED_VALIDATED_REUSE，校验输入、对象、源码及产物；未重新SDK编译，没有共享link/native run/device通信。

首次fullbuild磁盘满为 C1085/MSB6003 infrastructure FAIL，原nativeexit UNKNOWN_DATA_LOSS；165个不完整build文件、0B/raw/tmp仍保留。普通磁盘写probe恢复后逐hash快照，仅一次增量resume实际OS0；28份原targeted products不变。没有删移压缩项目、ebooks或用户文件，PDB实际0。仅收尾本轮精确归属的1个 compiler telemetry VCTIP 进程，实际退出码15；不是强退功能测试；初始helper失败也保留。两个历史 stock-sdk23-d02/host-api-d01 原buildexit仍UNKNOWN，未重跑，不以新PASS补写。

默认nohandle仍8次负Socket耗尽，9/10在API前拒绝：这是前SF01证据保留的FAIL，PV01未重跑原default probe。独立的 immutable_deployment_no_listener_record_reuse 仍默认false；历史same12/distinct12的有限Host PASS必须满足整个部署（含freshRuntime）native-name→完整identity/domain/peer/package/QoS/config映射不变，没有globalregistry强制保证。未放宽同名身份碰撞，不回收exposed record/entrance，不增加quota，不声称SDKdrain或资源全清零。当前cross-close控制的records_created4/NativeFake6有其自身scope；每Runtime关闭后保留3 NotQuiescent。旧其他case的free0/unanchored8不能冒充本case全部数值。nativeclose/recreate、latecallback quiescence、SDKname清理/drain仍UNKNOWN，多view不是native重建。

## 可复现命令及证据入口

以下路径相对仓库：D=`build/r39-ros2-complete-matrix/recovery-20261023`，F=`D/pf01-persistent-final-d01`，B=`build/r39-pv-tf01`。这是封存执行记录，文档同步没有重跑。targeted与CTest的完整实际argv/cwd/timeout/environment在对应intent；stdout/stderr、nativewait及完成receipt是退出结果依据。Windows普通上下文使用recorded PATH中的pinned stock2.14.5 DLL及ROS2/本机依赖；不能按PATH名字推断所加载版本。下列命令的全部环境快照另保存在 `D/docs-persistent-d01/COMMANDS_REPRODUCTION.json`。

- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-pv-tf01\h\stock_transport_multi_participant_tests.exe allocation`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\pf01-persistent-final-d01\work\multi-allocation`; intent `F/native/multi-allocation/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-pv-tf01\h\stock_transport_multi_participant_tests.exe cross-close`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\pf01-persistent-final-d01\work\multi-cross-close`; intent `F/native/multi-cross-close/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-pv-tf01\h\stock_transport_multi_participant_tests.exe per-view-domain`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\pf01-persistent-final-d01\work\multi-per-view-domain`; intent `F/native/multi-per-view-domain/intent.json`.
- `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-pv-tf01\h\stock_transport_views_tests.exe factory`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\pf01-persistent-final-d01\work\views-factory`; intent `F/native/views-factory/intent.json`.
- `C:\pixi_ws\.pixi\envs\default\Library\bin\ctest.exe --test-dir C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\pf01-persistent-final-d01\ctest-c -C Release --output-on-failure -V`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\pf01-persistent-final-d01\ctest-c`; intent `F/ctest-c/ctest.intent.json`.
- `C:\pixi_ws\.pixi\envs\default\Library\bin\ctest.exe --test-dir C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\pf01-persistent-final-d01\ctest-h -C Release --output-on-failure -V`; cwd `C:\Users\17715\Documents\codes\FastDDS-dsoftbus-Transport\build\r39-ros2-complete-matrix\recovery-20261023\pf01-persistent-final-d01\ctest-h`; intent `F/ctest-h/ctest.intent.json`.

- Canonical final RESULT (historical local evidence unavailable in this checkout: `../../recovery-20261023/pf01-persistent-final-d01/RESULT.json`) SHA `65e761e79053f799bd7b53ac14d82352a701977e45e8b8234f84eab62644c4ac`；REPORT (historical local evidence unavailable in this checkout: `../../recovery-20261023/pf01-persistent-final-d01/REPORT.txt`) SHA `841fd38851da83632f7d354174c4e27067ff815be7cd9f77d4a325454b253e42`；RELEASE_MANIFEST1314成员 SHA `5f6d425c40a97315da9feb8ebae21085cd15e5c5ace4bd452a02a5da2978deab`。CASE_REPLAY_COMPARISON/BINARY_COPY_BINDINGS/PROCESS_AUDIT/CROSS_EDGE_LEDGER/REPLACEMENT_GUID_CHECK/SDK_REUSE绑定参数、二进制、真实退出、20条边与复用事实。
- Initial PI01 (historical local evidence unavailable in this checkout: `../../recovery-20261023/pi01-persistent-initial-d01/RESULT.json`) SHA `8a1c6e1843c82861f531a7aebee1f29544925351e0bc3d0a1fc4a4910e7c0dc8`；release1522成员 SHA `b48be6e20f420a9d1461faf7d4d1f879c78a162fd86b576401e60220d225d28f`；c/h/s构建receipt、HOST_ACTUAL_BINDINGS、SDK_ACTUAL_BINDINGS、diskfull-attempt01与RESUME_DELTA分别保留原始编译事实/失败。
- Independent review (historical local evidence unavailable in this checkout: `../../recovery-20261023/review-persistent-r01/RESULT.json`) SHA `be7ac2fa405ba244d9ade78c3283c251ccf99e74007de3fd3f5ee720e549f942`；81成员manifest SHA `3f4203497d87b45b3530cc610298fa759f981867922c7af999c42a5f5404e7b2`。Main独立final审计 (historical local evidence unavailable in this checkout: `../../recovery-20261023/main-persistent-final-audit-d01/RESULT.json`)核1314+22产物和source865。
- 最新设备可用性 (historical local evidence unavailable in this checkout: `../../recovery-20261023/main-domain-board-availability-d01/RESULT.json`) SHA `c1f632b0bd8eb2896860b459a423a3030ef1f045e13c969582bf55cf7ed1ffd7`；2026-10-23 16:49:43UTC两板USBConnected，仅availability。A3e01ff55454d202020104033bf453b00、B3e01ff55454d202020104433991c3b00，没有PV01部署或通信。
- 本阶段文档校验 (historical local evidence unavailable in this checkout: `../../recovery-20261023/docs-persistent-d01/RESULT.json`)：原正文byte suffix、三docs delta、before/after全源/Git、原始Git输出及封存release。旧nohandle25成员seal保持不变。

## 未完成项目与正常提交门禁

当前stock37项有限Jazzy双板通信0accepted，DEVICEfalse。新SDK静态产物缺共享link、目标入口/ABI及实际通信资格；C ABI/可靠正常退出与当前stock ROS2设备接受尚未完成。旧R38patched1801轮与旧4580 LC04loader8/8不能升格为本次stock设备验收；QoS/service/action/bag/大消息/并发仍按未验收记录，security OFF、topology/flow等既有限制保留。

图缓存陈旧，UNKNOWN/丢3caller/globaltruncation未解决；历史CRITICAL110及99direct不降级，无clean graph gate，本docs阶段不reindex或运行提交门禁。HEAD W仍123051e6abcc55db0712befd71eca0c7670f681f，Root仍f1914c38d60f77cc620404b9563da87f73336838；未stage/commit/push，无新SHA/CI。Root189dirty/staged0及原diffcheck2保留，不为文档整理清理用户工作树。文档写前865source逐条匹配，之后只新增本报告并给README/matrix增加前缀；所有既有正文原bytes后缀保留，生产/测试不变。仍为Overall FAIL，正常Commit gate FAIL，Ready for commit: NO。
