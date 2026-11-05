# Stage 2: unmodified ROS 2 Jazzy nodes over the DSoftBus Transport on two boards

First recorded 2026-11-05, updated 2026-11-06 with the per-board hub path.
Boards: RK3588A A `3e01ff55454d202020104033bf453b00`, B `3e01ff55454d202020104433991c3b00`.
Status words follow `AGENTS.md`.

Evidence is under `E:/s2/runs/final-*` and `E:/s2/runs/final2-*`, with verdicts in `final-status.tsv` and
`final2-status.tsv`. It was taken on this tree:

```bash
git diff -- . ':!docs' ':!*.md' | sha256sum    # 93addcdcd672a60f6839df152fbbe7461ece4ec930fbfa03985a4a33132119ac
git ls-files --others --exclude-standard integrations tests | xargs cat | sha256sum    # fecbbc3a52984a7c... (new hub and test sources)
```

To run ROS nodes on boards, use the [hub deployment guide](ROS2_HUB_DEPLOYMENT.md). This file is the evidence record.

## 1. Status

**Stage 2 is bounded DEVICE_TESTED on `2.14.5-ros` only, and NOT_PRODUCTION_QUALIFIED.** Section 10 lists what
production qualification would still need.

| Item | State |
| --- | --- |
| Direct path (one ROS process per board per config) | Implementation PRESENT; Build PASS; DEVICE_TESTED |
| Hub path (any number of ROS processes per board) | Implementation PRESENT; Build PASS; DEVICE_TESTED |
| Host tests | PASS: stock suite 69/69 serial and 69/69 `-j 4`; root facade suite 23 groups, 0 failures |
| RK3588A, Stage 1 regression | PASS: `2.14.5-ros` 3 runs at 70/70, core route 8/8; stock 2.14.5, 3.6.2, 3.2.5 one run each at 70/70 |
| RK3588A, cross-device Stage 2 on `2.14.5-ros` | PASS within the scenarios in sections 5 and 6 |
| Hub as a system service | NOT_RUN: started over `hdc` as root |
| ROS on stock 2.14.5, 3.6.2, 3.2.5 and on Fast DDS 3.x | NOT_RUN |
| Python layer | DEVICE_TESTED on the hub, but not built by this repository (section 9) |
| Production | NOT_PRODUCTION_QUALIFIED |

`2.14.5-ros` is not the stock 2.14.5 binary (`libfastrtps.so.2.14` `83f50053…`, against `3713bba0…` for the stock
line). A result here does not transfer to any stock line. This record covers execution evidence only. It does not
state that the review or PRE_COMMIT gates passed.

The `-j 4` count is one run. The stock suite is not reliable in parallel in general: an earlier run
(`final-logs/ctest-j4-2.log`, 2026-11-05) failed one UDP case out of 68, while its two siblings passed. Use the serial
result as the reference.

## 2. Paths

```text
Direct: unmodified ROS node -> rmw_fastrtps_cpp -> Fast DDS 2.14.5-ros -> DSoftBus Transport -> DSoftBus Socket API

Hub:    unmodified ROS node -> rmw_fastrtps_cpp -> Fast DDS 2.14.5-ros -> HubTransport
            -> AF_UNIX SEQPACKET -> fdds_dsb_hub -> DSoftBus Transport -> DSoftBus Socket API
```

No RTPS, RMW or ROS source changed for either path. `rmw_fastrtps` builds each Participant from the Fast DDS default
QoS, so a preload library installs the Transport there before `main()`.

**Direct.** `libfastdds_dsoftbus_ros2_autoload.so` loads the SDK into the ROS process. Any autoload failure ends the
process with exit 71 (`FDDS_AUTOLOAD_REFUSED`), so it cannot silently run on another transport. One config names one
native socket, so the path serves one process per board per config, and one domain. A second process on one config is
now refused through `flock`. It used to look healthy, hear nothing, and exit 0.

**Hub.** `fdds_dsb_hub` owns the board's one DSoftBus link. It hosts the existing DSoftBus Transport through
`TransportInterface`, so link behaviour is the direct path's. `libfastdds_dsoftbus_ros2_hub.so` is a thin Fast DDS 2.14
Transport over a local socket. ROS processes on this path load no SDK and hold no token.

- The hub never parses RTPS. It routes on a 40-byte header. Each process has a random 16-byte address, used as a unicast locator, and joins group locators.
- Each process keeps its address when it reconnects, so a hub restart looks like a network outage to RTPS.
- `--supervise` runs the link in a worker process and restarts it when it exits. A Runtime cannot reopen its native sockets, so a fresh process opens a new link.
- A lock on `<socket path>.lock` allows one hub per socket path.
- The peer is found by UDID at each start, because the network id changes when a board rejoins. The hub polls every 2 s and treats about 10 s without the peer as a lost link.
- Built-in transports are off, so with no hub a process fails. It cannot fall back to UDP or shared memory.

The hub also calls the Bus Center query functions `GetAllNodeDeviceInfo` and `GetNodeKeyInfo`. These are not the Session API.

ROS 2 is built from upstream Jazzy revisions pinned in `scripts/release/build_ros2_ohos.py`. The build now also pins
`geometry2`, `message_filters` and `rosbag2`. These packages are built from those pins. `ros2/` in the working tree
is a separate clone of the ROS sources and is not part of this repository's changes.

## 3. What changed

| Path | Change |
| --- | --- |
| `integrations/ros2/autoload/` | Preload library, config and environment validation, deployment helpers. New in this update: config lock, refusal of `RMW_FASTRTPS_USE_QOS_FROM_XML=1`, optional domain check (the hub serves any domain), `close_at_destruction` set in `Configuration()`. |
| `integrations/ros2/hub/` | New. `fdds_dsb_hub.cpp`, `HubTransport`, `HubPreload.cpp`, `HubRouter.hpp` (pure routing, Host-tested by `stage2.hub.router`). |
| `include/.../DSoftBusTransportDescriptor.hpp`, `src/DSoftBusTransport.cpp` | New descriptor field `close_at_destruction`, default off (section 7). Host test `FCD-13a`. |
| `integrations/ros2/stock/CMakeLists.txt` | Hub, preload and dispose probe targets under `BUILD_STOCK_ROS2_AUTOLOAD`; Host tests `stage2.autoload.config`, `stage2.hub.router`; `RESOURCE_LOCK` on the UDP loopback tests. |
| `scripts/release/build_fastdds_ohos.sh` | Variant `2.14.5-ros`: pinned v2.14.5 plus `integrations/ros2/apply_topic_rtti_key.py`. |
| `scripts/release/build_ros2_ohos.py` | Builds pinned ROS 2 for OHOS. Now also the message packages and `tf2`, composition, QoS, lifecycle and action demos. |
| `scripts/release/run_ros2_cross_board.py` | Deploys and runs all Stage 2 modes; `--hub`, `--recover-bus-after`; strips CRLF from ament resource files. |
| `tests/ros2_stage2/` | ROS workloads `stage2_node` and `stage2_features`, `stage2_dispose_probe`, Host tests. The ROS workloads contain no Transport or Fast DDS code. |
| `tests/device_suite/dsb_suite.cpp` | Uses `DeviceDeployment.hpp`; two tolerances (section 8). |

## 4. Build and run

```bash
W=E:/s2   # any work directory
bash scripts/release/build_fastdds_ohos.sh 2.14.5-ros $W
python scripts/release/pull_platform_libs.py <board A target> $W/platform
bash scripts/release/build_transport_ohos.sh 2.14.5-ros $W $W/platform
python scripts/release/build_ros2_ohos.py --work $W

python scripts/release/run_ros2_cross_board.py --work $W --a <A> --b <B> --mode suite          # direct
python scripts/release/run_ros2_cross_board.py --work $W --a <A> --b <B> --hub --mode suite    # hub
# Stage 1 regression on the same Fast DDS build:
python scripts/release/run_cross_board.py --version 2.14.5-ros --work $W --a <A> --b <B>
python scripts/release/run_cross_board.py --version 2.14.5-ros --work $W --a <A> --b <B> --no-deploy --groups core --route 1
```

`build_ros2_ohos.py` defaults `--cmake`, `--python`, `--ninja` and `--host-tools` to paths on the recorded host, so
override them elsewhere. Libraries deploy to `/data/local/tmp/fdds_dsb/2.14.5-ros/`, because Fast CDR SONAMEs collide
between lines. Each run writes `run.log` and `result.json`. Exit 0 means every check for that mode passed.

| Mode | Path | What runs |
| --- | --- | --- |
| `suite` | both | `stage2_node` on both boards: graph, RELIABLE and BEST_EFFORT pub/sub, TRANSIENT_LOCAL late joiner, 150000-byte fragmented message, service, action, action cancel, remote parameter, lifecycle. |
| `smoke` | both | Unmodified minimal publisher and subscriber, stopped with SIGINT. |
| `components` | both | Unmodified `demo_nodes_cpp` talker and listener, loaded through `class_loader`. |
| `features` | both | `stage2_features`, one process pair per group (section 6). |
| `demos` | both | 8 pairs of unmodified upstream binaries. |
| `route` | both | 60 s stream with a 15 s IP drop on board B. |
| `restart` | both | A streams for 75 s. B runs 15 s, exits normally, and a new B process rejoins. |
| `kill_peer` | both | SIGKILL of the ROS process on B under load, then a new process. |
| `sigterm` | both | SIGTERM of both processes, as a service manager stops a node. |
| `dispose` | both | `stage2_dispose_probe`: tells a dispose (REMOVED) from a lease expiry (DROPPED). |
| `negative` | direct | 13 forbidden configurations on each board, 26 rows; each must be refused before any case runs. |
| `second_process` | direct | A second process on one config must be refused, and the first must not notice. |
| `multiprocess` | hub | 7 unmodified processes, SIGKILL of one, a late joiner. |
| `hub_restart` | hub | SIGKILL of B's hub worker under traffic. |
| `no_hub` | hub | With no hub a process must fail. |
| `second_hub` | hub | A second hub on one socket path must be refused. |
| `security` | hub | SROS2 Enforce between boards. |
| `scale` | hub | 8 processes per board. |
| `soak` | hub | Long reliable stream both ways, plus local processes, with hub memory sampled. |
| `long_outage` | hub | Outage longer than the lease. `--recover-bus-after N` opts in to bus recovery. |
| `cli` | hub | `ros2` commands, rclpy, launch and rosbag2 (section 9 limits). |

## 5. Results

| Scenario | Direct | Hub |
| --- | --- | --- |
| `suite` | PASS 2 runs, 20/20 each | PASS 2 runs, 20/20 each |
| `features` | PASS 2 runs, 70/70 each (see limit 1 in section 8) | PASS 72/72 (`final2-hub-features`) |
| `demos` | PASS 8/8 | PASS 8/8 |
| `smoke` | PASS, 21 lines heard | PASS, 21 lines heard |
| `components` | PASS, 13 lines heard | PASS, 13 lines heard |
| `route` | PASS 1 run: 600/600 both ways, 0 gaps, max silence 17.8 s | PASS 2 runs: same numbers |
| `restart` | PASS: A received 300; each B received 201 | PASS: A received 300; each B received 200 |
| `kill_peer` | PASS: first B exit 137; A max silence 8.4 s | PASS: first B exit 137; A max silence 3.4 s |
| `sigterm` | PASS | PASS |
| `dispose` | PASS 3/3, REMOVED at 3.00 to 3.01 s | PASS 3/3, REMOVED at 3.00 to 3.01 s |
| `negative` | PASS 26/26, exit 71, 0 cases run | NOT_RUN on device (see below) |
| `second_process` | PASS: second exit 71, first undisturbed | N/A |
| `multiprocess` | N/A: refused by design | PASS 3/3 |
| `hub_restart` | N/A | PASS 3 runs, 600/600 both ways, 0 gaps, max silence 6.5 to 6.7 s; B reattached with no process restart |
| `no_hub` | N/A | PASS: exit 134 (SIGABRT from the failed Participant), `FDDS_HUB_CLIENT_REFUSED`, 0 cases run |
| `second_hub` | N/A | PASS: exit 71, first hub kept working |
| `security` | NOT_RUN | PASS: keyed pair heard 14 lines, unkeyed process heard 0 and exited 0, unknown enclave exit 134 |
| `scale` | N/A | PASS: 16 processes, 60/60 on all 8 listeners, all exits 0 |
| `soak` | NOT_RUN | PASS: 1800 s, 17980/17980 each way, 0 gaps, max silence 0.1 s, hub drops 0, hub memory growth 28 KB per board |
| `long_outage` | NOT_RUN | Section 7 |
| `cli` | NOT_RUN | PASS 39/39 checks (section 9) |

Stage 1 regression, each run with 9 NOT_APPLICABLE items that are not counted as passes:

| Line | Runs | Result |
| --- | --- | --- |
| `2.14.5-ros` | 3 full suites; 1 core route | 70/70 each; 8/8 |
| Stock 2.14.5, 3.6.2, 3.2.5 | 1 full suite each | 70/70 each |

Host evidence is in `final-host/`: stock 69/69 serial and `-j 4`; root facade 23 groups, 0 failures, including
`FCD-13a`, from a fresh `E:/s2/host-root` using the `.artifacts/install/host` Fast DDS. These tests do not run the OHOS
binaries.

First-pass failures, kept as evidence. The product binaries did not change between the passes:

- `final-d-features` and `final-d-features-rep`: `graph_participant_leaves` failed at 20 s on the then 8 s limit, 69/70. See limit 1 in section 8.
- `final-h-multiprocess-1` and `-2`: the runner itself raised `IndexError`, because the board's `grep` stops at NUL bytes in `/proc/<pid>/cmdline`. The runner now takes the pid before the second listener starts. `final2-hub-multiprocess-1..3` passed.

**Hub environment refusal.** The hub preload calls the same `CheckRosEnvironment` as the direct path, and the Host test
covers that function. The device `negative` mode runs the direct path only, so per-setting refusal on the hub is
IMPLEMENTED and HOST_TESTED, not DEVICE_TESTED.

**Path evidence.** Across the final hub, direct and outage runs, 5181 socket samples showed `max_udp=0` in every sample.
The runner also reads the Transport or hub counters. Samples are once per second, so this is not a wire capture, and it
does not sample shared memory. An earlier negative control, `final-ctl-udp-without`, showed 8 UDP sockets per board.

**Signals.** `Signal 9` lines come from the intended SIGKILLs and `Signal 6` from the intended `no_hub` and
unknown-enclave refusals. There were no `CLOSE_INCOMPLETE` lines. `FDDS_AUTOLOAD_SEND_FAILURE` still appears after a
peer has shut down: `final2-direct-features-2` has 1 and `final-d-features-rep` has 3, codes `-426114885` and
`-427229151`. The `features` mode does not check its timing. The `suite` mode does, and it found none with the peer alive.

**Throughput.** Large-message rates vary between runs more than they differ between paths. Values are MiB/s, per side:

| Run | Path | 1 MiB | 1080p | 16 MiB |
| --- | --- | --- | --- | --- |
| `final2-hub-features` | hub | 30.9, 35.1 | 12.3, 12.1 | 6.7, 6.7 |
| `final-h-features` | hub | 40.6, 32.1 | 9.9, 9.9 | 8.1, 7.9 |
| `hub-features-1` (earlier binaries) | hub | 33.4, 34.6 | 11.3, 11.2 | 9.8, 9.8 |
| `final2-direct-features-1` | direct | 33.3, 39.1 | 3.1, 3.1 | 4.0, 4.0 |
| `final2-direct-features-2` | direct | 34.6, 34.3 | 11.1, 11.0 | 6.9, 6.9 |
| `final-d-features` | direct | 32.4, 31.1 | 10.7, 10.8 | 12.1, 12.1 |
| `final-d-features-rep` | direct | 45.2, 34.7 | 11.8, 11.8 | 7.3, 7.4 |
| `features-1` (earlier binaries) | direct | 30.7, 34.5 | 10.2, 10.3 | 5.9, 5.9 |

These runs do not show that either path is faster. The 3.1 MiB/s 1080p run is not explained. These are
feature checks with 2 or 3 messages, not a benchmark.

Binaries, SHA-256 matched on both boards:

| File | SHA-256 prefix |
| --- | --- |
| `libfastrtps.so.2.14` | `83f50053` |
| `libfastdds_dsoftbus_stock_214.so` | `e77ff87e` |
| `libfastdds_dsoftbus_ros2_autoload.so` | `35614d68` |
| `libfastdds_dsoftbus_ros2_hub.so` | `d2522f96` |
| `fdds_dsb_hub` | `a291086d` |
| `stage2_node` | `a4acc603` |
| `stage2_features` (`final2` runs) | `22e0b850` |
| `talker` / `listener` | `c330b62a` / `571c1371` |

`stage2_features` was rebuilt between the two tester passes for the limit in section 8. The product binaries above did not change.

## 6. Feature matrix

PASS means the scenario passed in the runs in section 5, on `2.14.5-ros` between these two boards, and nothing wider.
The direct and hub `features` rows are from `final2-direct-features-1`, `-2` and `final2-hub-features`.

| Feature | Direct | Hub |
| --- | --- | --- |
| Pub/sub: RELIABLE, BEST_EFFORT, TRANSIENT_LOCAL late joiner, 150000-byte fragmented | PASS | PASS |
| Service, action (goal, feedback, result, cancel), remote parameter, lifecycle | PASS | PASS |
| QoS events: deadline, liveliness manual_by_topic, lifespan, keep_last depth, incompatible event | PASS | PASS |
| Generic and serialized pub/sub | PASS | PASS |
| Loaned publish | PASS | PASS |
| Burst of 2000 RELIABLE KEEP_ALL | PASS | PASS |
| SensorData QoS at 100 Hz, 500 of 500 | PASS | PASS |
| 1 MiB, 1080p, 16 MiB messages | PASS | PASS |
| 40 topics | PASS | PASS |
| Graph endpoint details, node leave, type description service | PASS | PASS |
| Participant leave reaches the peer | PASS with a 25 s limit; one side at the 20 s lease (section 8) | PASS with an 8 s limit, 0.000 and 0.020 s |
| 200 concurrent service calls (depth 400) | PASS | PASS |
| Service introspection | PASS | PASS |
| 200 kB parameter | PASS | PASS |
| Concurrent and rejected action goals | PASS | PASS |
| Single, static, multi-threaded and events executors; wait set | PASS | PASS |
| Intra-process plus remote; namespaces and remap | PASS | PASS |
| Second context, same domain | PASS | PASS |
| Second context, other domain | N/A: the direct path serves one domain | PASS |
| rosout, parameter events, sim time, topic statistics, logger service | PASS | PASS |
| tf2 static and dynamic | PASS | PASS |
| Composition, `class_loader`, `manual_composition` | PASS | PASS |
| Upstream demos: loaned, serialized, add_two_ints sync and async, introspection, content filter | PASS | PASS |
| More than one ROS process per board | N/A: refused | PASS |
| SROS2 Enforce | NOT_RUN | PASS |
| `ros2` topic, node, service, param, action, lifecycle, component, interface, pkg, doctor, daemon | NOT_RUN | PASS (section 9) |
| `ros2 run`, `ros2 launch`, rclpy publisher and subscriber | NOT_RUN | PASS (section 9) |
| `ros2 bag` record, info, play on sqlite3 and mcap | NOT_RUN | PASS, with 1 message short on each play (section 8) |
| rviz, rqt, `ros2 trace`, bag compression | NOT_RUN | NOT_RUN |

## 7. Root causes and changes

**Fast DDS patch.** `2.14.5-ros` has exactly one patch, approved by the user: it gives `TopicDescription` a key
function, defining its destructor in `Topic.cpp` so that the library holds one typeinfo definition. No layout or protocol
change is intended. Without it, the unmodified RMW's `dynamic_cast` to `Topic` returned null and `delete_participant`
returned 4 (`RETCODE_PRECONDITION_NOT_MET`), so Participants could not be deleted.

**Build flag.** ROS executables are linked with `-Wl,--export-dynamic`, so `class_loader` works on OHOS. Root cause: the
executable's `AbstractMetaObject` typeinfo was absent from `.dynsym`, the plugin held a second copy, and the
pointer-comparing cast returned null. Tested: the flag fixes it. How this mechanism relates to the Topic bug is not proven.

**Clean exits looked like crashes.** Fast DDS calls `shutdown()` before it sends the Participant dispose. The Transport
closed there, so the dispose was lost and peers waited out the 20 s lease on every clean exit, on every line.
`close_at_destruction` (default off) makes `shutdown()` a no-op and closes at destruction, as UDP does.
`DeviceDeployment::Configuration()` sets it. Close then waits at most 250 ms, best effort, for queued records to leave.
A peer that misses the dispose still ends the Participant by its lease.

**Negative control for that bug.** The first test, the ROS-level `graph_participant_leaves`, passed with the bug restored,
because ROS removes nodes through `ros_discovery_info`. So `stage2_dispose_probe` was added. With the bug restored it
reports `removed=0 dropped=1 seconds=20.46` (`E:/s2/runs/dispose-ctl`). With the fix it reports REMOVED at about 3 s.

**Ament resource files.** Files written on the Windows build host kept CRLF, and a CR in `rclcpp_components`' library
path breaks `dlopen`. The runner now strips CRLF when it deploys the index.

**Message packages.** Fast CDR symbols differ between 2.2.5 (this build) and 2.2.7 (the board's older ROS tree), so a
typesupport library built against the other one fails to load. The message packages are built here.

**Bus loss after a long outage.** After an outage of about 45 s the boards leave the DSoftBus network
(`remote device num = 0`) and do not rejoin when the route returns. A reboot is not required: restarting `softbus_server`
restores the bus. A reboot was how this was first cleared, and an earlier version of this record said one was required.
That was wrong. Why the bus stays down is not known.

| Outage | Result | Evidence |
| --- | --- | --- |
| 15 s | Recovers by itself, 600/600 | `route` runs |
| 45 s, no recovery flag | `recovered=false` after 60 s, `remote device num = 0` | `final-o-control`; expected result of the control |
| 45 s, `--recover-bus-after 20` | Recovered 12.4 and 12.2 s after the route returned | `final-o-45-1`, `-2` |
| 70 s, `--recover-bus-after 20` | Recovered 11.4 s after the route returned | `final-o-70` |
| Between 15 and 45 s | NOT_RUN | |

In all these, the ROS processes were not restarted and exited 0. Volatile samples sent during an outage longer than the
lease are lost by design. In `final-o-45-1` only board A restarted `softbus_server`; in the other two both boards did.
The operator's manual restart took under 10 s, which is not in a run record; the runs show only the times above. After
`final-o-control` the boards were restored by restarting `softbus_server`, with no reboot.

**Why bus recovery is opt-in.** It needs permission to kill the service and it interrupts every DSoftBus user on the board.

**Autoload behaviour:**

- The Runtime closes through `atexit`, because the SDK crashes when called from an ELF destructor.
- libffrt's SIGINT and SIGTERM handlers are cleared, because `rclcpp` chains to them and they fault off an FFRT thread. The hub installs its own handlers once the link is open, for the same reason.
- Send failures are tolerated and counted. Exceptions still latch failure.
- If Participants remain at exit, the Runtime stays open and the process reports `FDDS_AUTOLOAD_CLOSE_INCOMPLETE`.
- Intraprocess delivery is off on the direct path, as in the device suite, so traffic stays on the Transport. The hub preload does not set this.

## 8. Limits, workarounds and unexplained behaviour (AGENTS.md 5.3)

1. **Direct path Participant leave.** `graph_participant_leaves` has a 25 s limit on the direct path and 8 s on the hub. This limit was raised after `final-d-features` failed at 8 s. A second Participant in one process on the direct path is often not announced, so a peer removes it at the 20 s lease.
   - With `close_at_destruction`, one side fell back to the lease in every direct run: `dbg-on-1..3` at 20.00, 20.01, 19.99 s, and `final2` at 19.99 and 20.01 s.
   - With the flag off, 6 of 6 sides took about 20 s (`dbg-off-1..3`), though one earlier graph-only run before the flag (`r1-graph-direct`) passed at 0.000 and 0.010 s.
   - Leaving in turn did not help (`turn-1..5`): one side was at the lease in 4 of 5 runs, and the other side's row ended `left=0` at that run's 30 s limit.
   - Root cause not found. A single Participant on the direct path passes `dispose` 3/3. The effect is a delay in graph cleanup, not data loss. The hub is not affected.
2. **Bag play.** `ros2 bag play` delivered 7 of the 8 recorded messages on sqlite3 and 8 of 9 on mcap. The first message goes before the player's publisher matches. Not investigated further; this is what was observed.
3. **Content filters.** The race workaround (`reader_filters_allocation.maximum = 0`) is set in code on `DataWriterQos`. It has no XML form and `rmw_fastrtps` never sets it, so it cannot be applied on the ROS path. **ROS applications must not create ContentFilteredTopics while writers are active.** The `content_filter` demo passes but does not exercise the race. See [section 6.4 of the version guide](FASTDDS_VERSIONS_AND_RELEASE.md#64-content-filter-race-on-fast-dds-3x-re-evaluated-not-assumed).
4. **`second_domain_concurrent` tolerance.** From `wait_for_acknowledgments` onward, the case tolerates a send failure on the first domain and counts it in `teardown_send_failures`. Measured cause (`E:/s2/runs/diag-full-9`): a send during the post-ack barrier hit a channel the peer had closed, with `failure_value=-426114885` (`SOFTBUS_TRANS_TDC_GET_INFO_FAILED`). **It still occurs:** 1 in the 3 `2.14.5-ros` runs (`final-s1-2`) and 1 in the stock 3.6.2 run (`final-s2-362`).
5. **`peer_process_restart` tolerance.** New in `dsb_suite`: sends from `delete_contained_entities` onward are tolerated, because the dispose goes to a helper that has already exited. Failures before that point still count.
6. **Unique flows error logs.** `No unicast locators to create unique flows` and `Problem creating associated Reader` appear once per process, and `rmw_fastrtps` then retries without unique flows. That reading comes from source and log counts, not a trace. An application that sets unique flows to STRICTLY_REQUIRED would fail. That case is NOT_RUN.
7. **Two boards only.** The hub has one peer. Three or more boards are not supported.
8. **Soak** is 30 minutes, not days.
9. **Not proven:** SDK drain, epoch and cancellation behaviour.
10. **Local socket security.** The hub socket is mode 0660 with no authentication beyond filesystem permissions, so any process with that access can inject frames. The Transport is not an authentication boundary; SROS2 is.
11. **Hub data path.** Every frame is copied through a local socket, and same-board traffic crosses the hub too. A drop on overflow (32 MiB per client and per link direction) behaves like a network drop.
12. **Hub restart** stalls traffic about 7 s.
13. **Long outage.** Longer than the lease drops volatile data by design, and recovery needs the opt-in flag. It is tested only with `--recover-bus-after 20`, with both boards reachable. A peer powered off for hours is NOT_RUN; the backoff to 300 s is checked by reading only. By reading, a value of 120 s or more does not grow the backoff, because a worker that waited that long counts as a healthy run.
14. **Deployed configuration.** `go_file=/dev/null`, a placeholder `peer_network` and hubs started 20 s apart passed `sigterm` 2/2 and `suite` 1/1 with the gate's product binaries (`E:/s2/runs/deployed-*`, `--start-apart 20`). Other start intervals and rebooted boards are NOT_RUN.
15. **One hub per path, not per board.** The lock covers the socket path. Two hubs on different paths on one board are not refused.
16. **NOT_RUN:** ROS on stock 2.14.5, 3.6.2 and 3.2.5, and on Fast DDS 3.x; rviz and rqt GUIs; `ros2 trace`; bag compression; the hub as a system service; killing the hub supervisor, or both hubs together; a board reboot under a running hub.

Required environment and refusals are in the [deployment guide](ROS2_HUB_DEPLOYMENT.md#6-environment-for-each-ros-process).

## 9. Python layer and inputs a clone does not provide

**The Python layer is not built by this repository.** rclpy 7.1.11, ros2cli 0.32.11, launch, rosbag2 and sros2 come from the
older tree already on the boards at `/data/local/tmp/ros2`, plus third-party modules from `mdds_dds_*/package/python`.
Our rcl, `rmw_fastrtps`, Fast DDS and message packages come first on `LD_LIBRARY_PATH`. A clone cannot reproduce this
tree, and it is one patch release behind the checkout. It needs libpython in `LD_PRELOAD`, `RTLD_GLOBAL` on `dlopen` and
`python3.12` on `PATH`. The `cli` results are bounded by this.

A clone also does not provide:

- `ros2/src`: the ROS 2 source checkouts holding the pinned revisions.
- The Host ROS tools (`--host-tools`) and the matching Python, CMake and Ninja.
- The `E:/` work directories and evidence.
- On each board, the native token and the `com.kaihong.mdds.*` permission entry. The other prerequisites are in [section 5 of the version guide](FASTDDS_VERSIONS_AND_RELEASE.md#5-reproducing-from-a-clean-clone).

## 10. What production qualification would need

The status stays NOT_PRODUCTION_QUALIFIED. The [deployment guide](ROS2_HUB_DEPLOYMENT.md#12-what-production-qualification-would-need)
lists the work, and [the production support contract](PRODUCTION_SUPPORT_CONTRACT_V1.md) sets the wider requirements.
