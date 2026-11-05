# Stage 2: unmodified ROS 2 Jazzy nodes over the DSoftBus Transport on two boards

Date: 2026-11-05. Boards: RK3588A A `3e01ff55454d202020104033bf453b00`, B `3e01ff55454d202020104433991c3b00`.
Status words follow `AGENTS.md`. Evidence: `E:/s2/runs/gate3-*`, taken on the working tree whose `git diff`
has SHA-256 `1a8a41cc21276785f6e3a68be7b0f5194c861ff3977164c769552ccd89ba30c0`, plus the untracked files of this change.

## 1. Status

**Stage 2 is bounded DEVICE_TESTED on `2.14.5-ros` only, and NOT_PRODUCTION_QUALIFIED.**

| Item | State |
| --- | --- |
| Implementation | PRESENT |
| Build | PASS |
| Host tests | PASS: stock suite 68/68 serial and 68/68 with `-j 4`; vendor tests 2/2 |
| RK3588A, Stage 1 regression on `2.14.5-ros` | PASS: 12/12 runs at 70/70; core route 8/8 |
| RK3588A, cross-device Stage 2 on `2.14.5-ros` | PASS within the scenarios in section 5 |
| Stock Fast DDS 2.14.5, 3.6.2, 3.2.5 | Stage 2 NOT_RUN |
| Production | NOT_PRODUCTION_QUALIFIED |

`2.14.5-ros` is not the stock 2.14.5 binary (`libfastrtps.so.2.14` `83f50053…`, against `3713bba0…` for the stock
line). A result here does not transfer to any stock line. This record covers execution evidence only. It does not
state that the review or PRE_COMMIT gates passed.

## 2. Path

```text
Unmodified ROS node: rclcpp -> rcl -> rmw_fastrtps_cpp
    -> Fast DDS 2.14.5-ros -> DSoftBus Transport -> DSoftBus Socket API
```

`rmw_fastrtps` builds each Participant from the Fast DDS default QoS. `libfastdds_dsoftbus_ros2_autoload.so`, loaded
with `LD_PRELOAD`, installs the Transport there before `main()`. The ROS 2 sources and the application carry no Transport
code. Any autoload failure ends the process with exit 71 (`FDDS_AUTOLOAD_REFUSED`), so a process cannot silently run on
another transport or on none.

ROS 2 is built from upstream Jazzy revisions pinned in `scripts/release/build_ros2_ohos.py`, for example
`rmw_fastrtps` 8.4.3, `rclcpp` 28.1.22, `rcl` 9.2.11 and `class_loader` 2.7.1. Each pin is on upstream history. The gate
found `rmw_fastrtps`, `rclcpp`, `rcl`, `rmw`, `rosidl_typesupport_fastrtps`, `demos` and `class_loader` sources unmodified.

## 3. What changed

| Path | Change |
| --- | --- |
| `integrations/ros2/autoload/` | New. `Autoload.cpp` is the preload library, `AutoloadConfig.hpp` is pure config and environment validation, and `DeviceDeployment.hpp` holds the deployment helpers now shared with the device suite. |
| `integrations/ros2/stock/CMakeLists.txt` | Option `BUILD_STOCK_ROS2_AUTOLOAD` (Fast DDS 2.14 line only); Host test `stage2.autoload.config`; `RESOURCE_LOCK` so the UDP loopback tests never overlap. |
| `integrations/ros2/target/build_ros_slice.py` | `console_bridge_vendor` support; `--exe-linker-flag`. |
| `scripts/release/build_fastdds_ohos.sh` | New variant `2.14.5-ros`: pinned v2.14.5 plus `integrations/ros2/apply_topic_rtti_key.py`. |
| `scripts/release/build_transport_ohos.sh` | `2.14.5-ros` line; turns the autoload library on. |
| `scripts/release/build_ros2_ohos.py` | New. Archives the pinned sources and builds ROS 2 for OHOS. |
| `scripts/release/run_ros2_cross_board.py` | New. Deploys and runs the Stage 2 modes. |
| `scripts/release/run_cross_board.py` | Accepts `2.14.5-ros`. |
| `tests/ros2_stage2/` | New. ROS workload `stage2_node` and the autoload configuration tests. `stage2_node` contains no Transport or Fast DDS code. |
| `tests/device_suite/dsb_suite.cpp` | Uses `DeviceDeployment.hpp`; records `failure_source`; `second_domain_concurrent` tolerance (section 7). |

## 4. Build and run

The plan named `build_ros2_ohos.sh`. The script is Python: `build_ros2_ohos.py`.

```bash
W=E:/s2   # any work directory
bash scripts/release/build_fastdds_ohos.sh 2.14.5-ros $W
python scripts/release/pull_platform_libs.py <board A target> $W/platform
bash scripts/release/build_transport_ohos.sh 2.14.5-ros $W $W/platform
python scripts/release/build_ros2_ohos.py --work $W

python scripts/release/run_ros2_cross_board.py --work $W --a <A> --b <B> --mode suite
python scripts/release/run_ros2_cross_board.py --work $W --a <A> --b <B> --no-deploy --mode smoke
# Stage 1 regression on the same Fast DDS build:
python scripts/release/run_cross_board.py --version 2.14.5-ros --work $W --a <A> --b <B>
python scripts/release/run_cross_board.py --version 2.14.5-ros --work $W --a <A> --b <B> --no-deploy --groups core --route 1
```

`build_ros2_ohos.py` defaults `--cmake`, `--python`, `--ninja` and `--host-tools` to paths on the recorded host, so
override them elsewhere. `run_ros2_cross_board.py` also takes `--groups`, `--domain`, `--timeout`, `--token-process`,
`--out` and `--diagnostic-preload`. Libraries deploy to `/data/local/tmp/fdds_dsb/2.14.5-ros/`. The directory is
separate because Fast CDR SONAMEs collide between lines. Each run writes `run.log` and `result.json`. Exit 0 means every
check for that mode passed.

| Mode | What runs |
| --- | --- |
| `suite` | `stage2_node` on both boards: graph, RELIABLE and BEST_EFFORT pub/sub, TRANSIENT_LOCAL late joiner, 150000-byte fragmented message, service, action goal/feedback/result, action cancel, remote parameter, lifecycle. `--groups` selects a subset. |
| `smoke` | Unmodified `examples_rclcpp_minimal_publisher` and `examples_rclcpp_minimal_subscriber`, stopped with SIGINT. |
| `components` | Unmodified `demo_nodes_cpp` talker and listener, which load their node through `class_loader`, stopped with SIGINT. |
| `route` | 60 s stream with a 15 s IP drop on board B. |
| `restart` | A streams for 75 s. B runs 15 s, exits normally, and a new B process rejoins. |
| `negative` | 12 forbidden configurations on each board; each must be refused before any case runs. |

## 5. Results

All evidence is under `E:/s2/runs/gate3-*`.

| Scenario | Runs | Result | Detail |
| --- | --- | --- | --- |
| Host stock suite | 2 | PASS | 68/68 serial, 68/68 `-j 4`. These Host tests do not run the OHOS binaries. |
| Vendor tests | 1 | PASS | 2/2 |
| ELF gate | 1 | PASS | `TopicDescription` typeinfo UND in the RMW and defined in `libfastrtps`; 148 ELF `.so` scanned with 0 drive-path `DT_NEEDED`; talker exports the `AbstractMetaObject<NodeFactory>` typeinfo. |
| Stage 1 full suite on `2.14.5-ros` | 12 | PASS | 70/70 each, 840/840. 9 NOT_APPLICABLE items per board are not counted as passes. One tolerated teardown send failure (section 7). |
| Stage 1 core route on `2.14.5-ros` | 1 | PASS | 8/8; route case max gap 19.8 s. |
| Stage 2 `suite` | 5 | PASS | 20/20 cases each. Transport counters at close: 438 to 531 records per direction. |
| Stage 2 `smoke` | 2 | PASS | Subscriber printed 21 `I heard` lines; both processes exit 0 and close normally. |
| Stage 2 `components` | 3 | PASS | Listener printed 13 `I heard` lines; both processes exit 0 and close normally. |
| Stage 2 `route` | 2 | PASS | 15 s outage; 600/600 both ways; 0 gaps; stall 17.7 to 17.8 s. |
| Stage 2 `restart` | 2 | PASS | A received 300 of 300 from the two B processes; each B received 200 or 201 from A. |
| Stage 2 `negative` | 1 | PASS | 24/24 refused with exit 71 and 0 cases run. |

Across the Stage 2 runs: `max_udp=0` in every socket sample, no Signal, no `CLOSE_INCOMPLETE`, and no
`FDDS_AUTOLOAD_SEND_FAILURE` line.

How to read these:

- **Path evidence.** The runner samples the process's UDP sockets once per second and reads the Transport's own counters at close. Suite runs have only 3 or 4 samples per board. It is not a wire capture, a socket that lives between samples would not be seen, and it does not sample shared memory.
- **Restart.** This mode is an orderly peer exit. SIGKILL of a ROS process is NOT_RUN. The SIGKILL case in the Stage 1 suite is a DDS case, not a ROS one.
- **Version label.** `dsb_suite` prints `fastdds=2.14.5` on `2.14.5-ros`, because the patch does not change the version macro. `result.json` carries `version=2.14.5-ros`.

Binaries, SHA-256 matched on both boards (`gate3-hashes.log`):

| File | SHA-256 prefix |
| --- | --- |
| `libfastrtps.so.2.14` | `83f50053` |
| `libfastdds_dsoftbus_stock_214.so` | `bdeb7227` |
| `libfastdds_dsoftbus_ros2_autoload.so` | `82f440c1` |
| `stage2_node` | `46089f8e` |
| `talker` / `listener` | `c330b62a` / `571c1371` |
| `dsb_suite` | `c8b49a30` |

## 6. Root causes and changes outside the Transport

**Fast DDS patch.** `2.14.5-ros` has exactly one patch, approved by the user: it gives `TopicDescription` a key
function, defining its destructor in `Topic.cpp` so that the library holds one typeinfo definition. No layout or protocol
change is intended. Without it, the unmodified RMW's `dynamic_cast` to `Topic` returned null and `delete_participant`
returned 4 (`RETCODE_PRECONDITION_NOT_MET`), so Participants could not be deleted.

**Build flag.** ROS executables are linked with `-Wl,--export-dynamic`, so `class_loader` works on OHOS. This is a build
flag, not a source change. Root cause: the executable's `AbstractMetaObject` typeinfo was absent from `.dynsym`, the
plugin held a second copy, and the pointer-comparing cast returned null. Tested: the flag fixes it. How this mechanism
relates to the Topic bug is not proven.

**Autoload behaviour:**

- The Runtime closes through `atexit`, because the SDK crashes when called from an ELF destructor.
- libffrt's SIGINT and SIGTERM handlers are cleared. `rclcpp` chains to the handler it finds, and libffrt's faults off an FFRT thread. Only a handler owned by libffrt is removed.
- Send failures are tolerated and counted, because a send can fail once the peer has gone. Exceptions still latch failure. In `suite` mode the run fails if one occurs while the peer is alive. The other modes do not make that check.
- If Participants remain at exit, the Runtime stays open and the process reports `FDDS_AUTOLOAD_CLOSE_INCOMPLETE`.
- Intraprocess delivery is off, as in the device suite, so traffic stays on the Transport.

## 7. Workarounds and unexplained behaviour (AGENTS.md 5.3)

**`second_domain_concurrent` tolerance.** From `wait_for_acknowledgments` onward, the case tolerates a send failure on
the first domain, and counts it in `teardown_send_failures`. Failures before that point still fail the case. Measured
cause (`E:/s2/runs/diag-full-9`): a send during the post-ack barrier hit a channel the peer had closed, with
`failure_source=1`, `failure_value=-426114885` (`SOFTBUS_TRANS_TDC_GET_INFO_FAILED`) and `failed_before_barrier=0`.
**It still occurs:** 1 tolerated teardown send failure in the 12 gate runs (`gate3-stage1-7`, side 0).

**Unique flows error logs.** `No unicast locators to create unique flows` and `Problem creating associated Reader` appear
once per process, and `rmw_fastrtps` then retries without unique flows. That reading comes from source and log counts,
not a trace. An application that sets unique flows to STRICTLY_REQUIRED would fail. That case is NOT_RUN.

**Content filters.** The race workaround (`reader_filters_allocation.maximum = 0`) is set in code on `DataWriterQos`. It
has no XML form and `rmw_fastrtps` never sets it, so it cannot be applied on the ROS path. **ROS applications must not
create ContentFilteredTopics while writers are active.** See
[section 6.4 of the version guide](FASTDDS_VERSIONS_AND_RELEASE.md#64-content-filter-race-on-fast-dds-3x-re-evaluated-not-assumed).

**Long IP outage.** A 70 s IP outage drops the board pair off DSoftBus until they are rebooted; a 15 s outage recovers. In
the control run `E:/s2/runs/final-ctl-longoutage`, each side received 63 of 600 samples and the route check failed. A
reboot restored the pair. Long-outage recovery is NOT verified.

## 8. Limits

- Point-to-point only: one peer process per process.
- NOT_RUN: `ros2` CLI, daemon, rosbag and multi-process graphs.
- NOT_RUN: SROS2. The build has `SECURITY=ON`, but security was not exercised on ROS.
- NOT_RUN: ROS on Fast DDS 3.x, and ROS on stock 2.14.5, 3.6.2 and 3.2.5.
- Not proven: SDK drain, epoch and cancellation behaviour.
- Production is not qualified.

Required environment: `RMW_IMPLEMENTATION=rmw_fastrtps_cpp`, `ROS_DOMAIN_ID` equal to the configured domain, and
`ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT`.

Refused with exit 71 because each could change the transport set or the configuration:

- `ROS_AUTOMATIC_DISCOVERY_RANGE` LOCALHOST or SUBNET, and `ROS_LOCALHOST_ONLY=1`;
- `ROS_STATIC_PEERS`;
- profile files (`FASTRTPS_DEFAULT_PROFILES_FILE`, `FASTDDS_DEFAULT_PROFILES_FILE`);
- `FASTDDS_BUILTIN_TRANSPORTS`, `FASTDDS_ENVIRONMENT_FILE` and `ROS_DISCOVERY_SERVER`.

## 9. Inputs a clone does not provide

- `ros2/src`: the ROS 2 source checkouts holding the pinned revisions. `ros2/` is not part of this change.
- The Host ROS tools (`--host-tools`) and the matching Python, CMake and Ninja.
- The `E:/` work directories and evidence.
- On each board, the native token and the `com.kaihong.mdds.*` permission entry. The other prerequisites are in [section 5 of the version guide](FASTDDS_VERSIONS_AND_RELEASE.md#5-reproducing-from-a-clean-clone).
