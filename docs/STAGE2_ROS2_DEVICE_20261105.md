# Stage 2: unmodified ROS 2 Jazzy nodes over the DSoftBus Transport on two boards

First recorded 2026-11-05, updated 2026-11-06 with the per-board hub path and the Python layer built from pinned sources,
and 2026-11-09 with the runner audit fixes in section 11. Those fixes are HOST_TESTED only: every device result in
sections 5 and 6 was produced by the previous runner and was not rerun.
Boards: RK3588A A `3e01ff55454d202020104033bf453b00`, B `3e01ff55454d202020104433991c3b00`.
Status words follow `AGENTS.md`.

Current evidence is under `E:/s2q/runs/final-*`, `final3-*` and `final4-route`, with verdicts in `final-status.tsv` and
`final3-status.tsv`. It was taken on the ROS and Python tree built from empty in `E:/s2q`, on top of commit `46e6615c`:

```bash
git diff 46e6615c -- . ':!docs' ':!*.md' | sha256sum    # 4227845e4363797ce26b426ce284c543b37e2c66261a73c9e588d0d5891a6974
git ls-files --others --exclude-standard integrations tests scripts | xargs cat | sha256sum    # c335037b6c35490c... (new sources)
```

The numbers recorded at `46e6615c` (`E:/s2/runs/final-*`, `final2-*`) ran the Python layer from the older tree on the boards.
They stand as history and are kept in section 5.

The two hashes above describe the tree at that recording. The 2026-11-09 change to `run_ros2_cross_board.py` (section 11)
makes the first of them no longer match the working tree; it is kept as the identity of the tree the device runs used.

To run ROS nodes on boards, use the [hub deployment guide](ROS2_HUB_DEPLOYMENT.md). This file is the evidence record.

## 1. Status

**Stage 2 is bounded DEVICE_TESTED on `2.14.5-ros` only, and NOT_PRODUCTION_QUALIFIED.** Section 10 lists what
production qualification would still need.

| Item | State |
| --- | --- |
| Direct path (one ROS process per board per config) | Implementation PRESENT; Build PASS; DEVICE_TESTED |
| Hub path (any number of ROS processes per board) | Implementation PRESENT; Build PASS; DEVICE_TESTED |
| Host tests | PASS: stock suite 70/70 serial, including `stage2.drop_python_needed`. `-j 4` not rerun on this tree. Root facade suite 23 groups, 0 failures, recorded at `46e6615c`. The 2026-11-09 `stage2.runner_oracle` test passes standalone and under `ctest`, 18 cases; the 70/70 suite was not rerun with it registered (section 11) |
| RK3588A, Stage 1 regression | Not rerun: the Transport, hub and preload binaries are unchanged (section 5). The results for those binaries stand: `2.14.5-ros` 3 runs at 70/70, core route 8/8; stock 2.14.5, 3.6.2, 3.2.5 one run each at 70/70 |
| RK3588A, cross-device Stage 2 on `2.14.5-ros` | PASS within the scenarios in sections 5 and 6 |
| Hub as a system service | NOT_RUN: started over `hdc` as root |
| ROS on stock 2.14.5, 3.6.2, 3.2.5 and on Fast DDS 3.x | NOT_RUN |
| Python layer | Built from pinned sources by this repository; DEVICE_TESTED on the hub. CPython and third-party modules are a declared input (section 9) |
| Runner audit fixes (2026-11-09) | Implementation PRESENT; HOST_TESTED, 18 oracle cases; DEVICE_TESTED false, BLOCKED with no board attached; two audit findings OPEN (section 11) |
| Production | NOT_PRODUCTION_QUALIFIED |

`2.14.5-ros` is not the stock 2.14.5 binary (`libfastrtps.so.2.14` `83f50053…`, against `3713bba0…` for the stock
line). A result here does not transfer to any stock line. This record covers execution evidence only. It does not
state that the review or PRE_COMMIT gates passed.

The `-j 4` count, 69/69 at `46e6615c`, is one run and was not repeated on this tree. The stock suite is not reliable in parallel in general: an earlier run
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

ROS 2 is built from upstream Jazzy revisions pinned in `scripts/release/build_ros2_ohos.py` and, for pure Python packages,
`scripts/release/build_ros2_python.py`. No ROS source changes. `ros2/` in the working tree is a separate clone of the
ROS sources and is not part of this repository's changes.

## 3. What changed

| Path | Change |
| --- | --- |
| `integrations/ros2/autoload/` | Preload library, config and environment validation, deployment helpers. New in this update: config lock, refusal of `RMW_FASTRTPS_USE_QOS_FROM_XML=1`, optional domain check (the hub serves any domain), `close_at_destruction` set in `Configuration()`. |
| `integrations/ros2/hub/` | New. `fdds_dsb_hub.cpp`, `HubTransport`, `HubPreload.cpp`, `HubRouter.hpp` (pure routing, Host-tested by `stage2.hub.router`). |
| `include/.../DSoftBusTransportDescriptor.hpp`, `src/DSoftBusTransport.cpp` | New descriptor field `close_at_destruction`, default off (section 7). Host test `FCD-13a`. |
| `integrations/ros2/stock/CMakeLists.txt` | Hub, preload and dispose probe targets under `BUILD_STOCK_ROS2_AUTOLOAD`; Host tests `stage2.autoload.config`, `stage2.hub.router`; `RESOURCE_LOCK` on the UDP loopback tests. |
| `scripts/release/build_fastdds_ohos.sh` | Variant `2.14.5-ros`: pinned v2.14.5 plus `integrations/ros2/apply_topic_rtti_key.py`. |
| `scripts/release/build_ros2_ohos.py` | Builds pinned ROS 2 for OHOS. Now also the message packages and `tf2`, composition, QoS, lifecycle and action demos. |
| `scripts/release/run_ros2_cross_board.py` | Deploys and runs all Stage 2 modes; `--hub`, `--recover-bus-after`; strips CRLF from ament resource files. 2026-11-09: the deploy prunes, reads back and records what it sent, and the socket, `ros2` CLI and rosbag2 oracles are stricter (section 11). |
| `scripts/release/pull_python_target.py` | New. Pulls the Python runtime and third-party modules from a board. Refused unless libpython and the NumPy config header match recorded digests. |
| `scripts/release/build_ros2_thirdparty.py` | New. Builds yaml-cpp, zstd and lz4, pinned by commit, and sqlite, pinned by hash. Vendor packages find these and need no change. |
| `scripts/release/build_ros2_python.py` | New. Builds the pure Python packages into wheels and unpacks them into the install tree. |
| `integrations/ros2/target/python/` | New. `FindPython3.cmake`, `Ros2PythonTarget.cmake`, `drop_python_needed.py` (section 7). |
| `tests/ros2_stage2/drop_python_needed_test.py` | New Host test `stage2.drop_python_needed`. |
| `tests/ros2_stage2/runner_oracle_test.py` | New Host test `stage2.runner_oracle`: 18 cases over the runner's deploy, socket-watch, CLI and bag oracles against a fake board and fake `sh`/`send` (section 11). |
| `tests/ros2_stage2/` | ROS workloads `stage2_node` and `stage2_features`, `stage2_dispose_probe`, Host tests. The ROS workloads contain no Transport or Fast DDS code. |
| `tests/device_suite/dsb_suite.cpp` | Uses `DeviceDeployment.hpp`; two tolerances (section 8). |

## 4. Build and run

```bash
W=E:/s2   # any work directory
bash scripts/release/build_fastdds_ohos.sh 2.14.5-ros $W
python scripts/release/pull_platform_libs.py <board A target> $W/platform
bash scripts/release/build_transport_ohos.sh 2.14.5-ros $W $W/platform
# With the Python layer. These four commands are the from-empty build recorded in section 5:
python scripts/release/pull_python_target.py <board A target> $W
python scripts/release/build_ros2_thirdparty.py --work $W
python scripts/release/build_ros2_ohos.py --work $W --python-target $W/python-target
python scripts/release/build_ros2_python.py --work $W
# Without Python: python scripts/release/build_ros2_ohos.py --work $W

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
| `cli` | hub | `ros2` commands, rclpy, launch and rosbag2, plus a check of where every loaded file comes from. |

## 5. Results

**From-empty build (`E:/s2q`).** The four commands of section 4 exited 0 in 22, 41, 916 and 80 s (`logs/part1.tsv`), with
26 Python packages, 0 `.pyd` files in the install tree, and 846 entries removed from 124 files. The tester counted 0 Python
leaks in 448 ELF files. `python-layer.tar` is 218736640 bytes (about 209 MiB). Scope: `E:/s2q/2.14.5-ros` links to the existing
`E:/s2/2.14.5-ros`, so "from empty" covers ROS and Python, not Fast DDS or the Transport.

**Reproducibility.** Two builds are not byte-identical: 314 of 448 ELF files are identical, and the rest have the same size.
`NEEDED` sets equal those of the verified C++ tree, with the order different. An earlier gate asked for "exact" equality. That
requirement was the coordinator's own and was over-strict, and it is withdrawn here rather than reported as met.

**Binaries.** Transport, hub and preload hashes are unchanged, which is why Stage 1 was not rerun: `fdds_dsb_hub` `a291086d`,
`libfastdds_dsoftbus_ros2_hub.so` `d2522f96`, `libfastdds_dsoftbus_ros2_autoload.so` `35614d68`,
`libfastdds_dsoftbus_stock_214.so` `e77ff87e`. ROS binaries were rebuilt: `stage2_node` `063cc454`, `stage2_features` `4dcb2582`,
`talker` `a1212c4d`, `listener` `6725c293`.

| Scenario | Direct | Hub |
| --- | --- | --- |
| `suite` | PASS 20/20 | PASS 20/20, 3 runs |
| `features` | PASS 70/70; leave at 19.99 and 0.010 s under the 25 s limit | PASS 72/72 ×3 in `final3`, plus 72/72 in `final-h-features-rep` |
| `demos` | PASS 8/8 | PASS 8/8 |
| `smoke`, `components` | PASS, 21 and 13 lines heard | PASS, 21 and 13 lines heard |
| `route` | PASS 2 runs, 600/600 both ways, 0 gaps | PASS 600/600 ×3 in `final3`, plus `final4-route` and `final-h-route-rep`; max silence 17.7 to 17.8 s, 20.8 s in `final3-h-route-3` |
| `restart`, `kill_peer`, `sigterm` | PASS | PASS |
| `dispose` | PASS, REMOVED at 3.01 s | PASS ×2, REMOVED at 3.01 s |
| `negative` | PASS 26 rows | NOT_RUN on device |
| `second_process` | PASS: second exit 71 | N/A |
| `multiprocess` | N/A | PASS 3/3 |
| `hub_restart` | N/A | PASS ×2, 600/600, max silence 6.5 s |
| `no_hub`, `second_hub` | N/A | PASS: exit 134; exit 71 |
| `security` | NOT_RUN | PASS: keyed pair heard 14 lines, unkeyed process 0, unknown enclave exit 134 |
| `scale` | N/A | PASS: 60/60 on all 8 listeners |
| `--start-apart 20`, `suite` | N/A | PASS 20/20 (`final-h-start-apart`) |
| `soak` | NOT_RUN | PASS: 1800 s, 17980/17981 and 17981/17980, 0 gaps, max silence 0.1 s, hub drops 0, hub memory growth 28 KB per board |
| `long_outage` 45 s with `--recover-bus-after` | NOT_RUN | PASS: recovered 12.6 s after the route returned |
| `cli` | NOT_RUN | PASS 42/42 ×2. 755 loaded files, none outside the deployment or system paths. |

All 12 direct modes passed. First-run failures, kept as evidence:

- `final-h-features`: 72/72 cases, but it failed the then zero-drop rule on `large` (128 frames dropped). See limit 18 in section 8.
- `final-h-route`: 600/600 delivered, but it failed the then record minimum. See limit 17 in section 8.

Hub rates here were 8.7 to 12.9 MiB/s for 16 MiB and 31.9 to 46.1 MiB/s for 1 MiB. Direct, one run: 7.4 and 34.3 to 39.3 MiB/s.

### Earlier results at `46e6615c` (history, older Python tree)

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

Host evidence at that commit is in `final-host/`: stock 69/69 serial and `-j 4`; root facade 23 groups, 0 failures, including
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
| `stage2_node` (earlier build) | `a4acc603` |
| `stage2_features` (`final2` runs, earlier build) | `22e0b850` |
| `talker` / `listener` (earlier build) | `c330b62a` / `571c1371` |

`stage2_features` was rebuilt between the two tester passes for the limit in section 8. The product binaries above did not change.

## 6. Feature matrix

PASS means the scenario passed in the runs in section 5, on `2.14.5-ros` between these two boards, and nothing wider.
The rows are from the `E:/s2q` runs in section 5.

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
| `ros2 run`, `ros2 launch`, rclpy publisher, subscriber and service client | NOT_RUN | PASS (section 9) |
| `ros2 bag` record, info, play on sqlite3 and mcap | NOT_RUN | PASS, with plays up to 1 message short (section 8) |
| `ros2 bag` record and info with zstd file compression | NOT_RUN | PASS |
| rviz, rqt, `ros2 trace` | NOT_RUN | NOT_RUN |

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

**Message packages.** Fast CDR symbols differ between 2.2.5 (this build) and 2.2.7 (the older ROS tree on the boards), so a
typesupport library built against the other one fails to load. The message packages are built here.

**Python layer.** Each piece exists for one reason:

- `FindPython3.cmake`, first on `CMAKE_MODULE_PATH`: the host interpreter runs the generators while extension modules compile against the target Python. CMake's own module cannot express that split when cross compiling, and this needs no ROS source change.
- An empty `libatomic.a`: rclpy links `-latomic` on Clang, OHOS has no such library, and the compiler runtime provides atomics.
- `drop_python_needed.py`: message packages list `<pkg>__rosidl_generator_py` as a library, so every C++ consumer linked it and, through it, libpython. After installation this tool removes only `*__rosidl_generator_py` `DT_NEEDED` entries outside the Python package directory, so C++ nodes need no Python. `--as-needed` was tried and rejected: it stripped other libraries and broke `class_loader` plugins.
- `sitecustomize.py` sets `RTLD_GLOBAL`: Python loads extension modules privately, and rosbag2's plugins must share C++ typeinfo with those modules, or no storage plugin is found.
- libpython stays in `LD_PRELOAD`: `python3.12` is a thin launcher and NumPy's prebuilt modules find the interpreter through a global libpython.
- pybind11's self-contained copy in `site-packages`: the Library copy shares an include directory with every host library.
- `PYBIND11_PYTHON_EXECUTABLE_LAST`: otherwise pybind11 clears the target settings on a first configure, and modules lose the target suffix.

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
   - On the `E:/s2q` tree the same pattern held: 19.99 and 0.010 s.
   - Root cause not found. A single Participant on the direct path passes `dispose` 3/3. The effect is a delay in graph cleanup, not data loss. The hub is not affected.
2. **Bag play.** `ros2 bag play` delivered 7 of 8 recorded messages on sqlite3 in both runs, and on mcap 8 of 8 in one run and 7 in the other. At `46e6615c` it was 7 of 8 and 8 of 9. The first message goes before the player's publisher matches. Not investigated further; this is what was observed. The 2026-11-09 playback row allows exactly this one-message head loss and no more, and cites these pairs as its bound (limit 20).
3. **Content filters.** The race workaround (`reader_filters_allocation.maximum = 0`) is set in code on `DataWriterQos`. It has no XML form and `rmw_fastrtps` never sets it, so it cannot be applied on the ROS path. **ROS applications must not create ContentFilteredTopics while writers are active.** The `content_filter` demo passes but does not exercise the race. See [section 6.4 of the version guide](FASTDDS_VERSIONS_AND_RELEASE.md#64-content-filter-race-on-fast-dds-3x-re-evaluated-not-assumed).
4. **`second_domain_concurrent` tolerance.** From `wait_for_acknowledgments` onward, the case tolerates a send failure on the first domain and counts it in `teardown_send_failures`. Measured cause (`E:/s2/runs/diag-full-9`): a send during the post-ack barrier hit a channel the peer had closed, with `failure_value=-426114885` (`SOFTBUS_TRANS_TDC_GET_INFO_FAILED`). **It still occurs:** 1 in the 3 `2.14.5-ros` runs (`final-s1-2`) and 1 in the stock 3.6.2 run (`final-s2-362`).
5. **`peer_process_restart` tolerance.** New in `dsb_suite`: sends from `delete_contained_entities` onward are tolerated, because the dispose goes to a helper that has already exited. Failures before that point still count.
6. **Unique flows error logs.** `No unicast locators to create unique flows` and `Problem creating associated Reader` appear once per process, and `rmw_fastrtps` then retries without unique flows. That reading comes from source and log counts, not a trace. An application that sets unique flows to STRICTLY_REQUIRED would fail. That case is NOT_RUN.
7. **Two boards only.** The hub has one peer. Three or more boards are not supported.
8. **Soak** is 30 minutes, not days.
9. **Not proven:** SDK drain, epoch and cancellation behaviour.
10. **Local socket security.** The hub socket is mode 0660 with no authentication beyond filesystem permissions, so any process with that access can inject frames. The Transport is not an authentication boundary; SROS2 is.
11. **Hub data path.** Every frame is copied through a local socket, and same-board traffic crosses the hub too. A drop on overflow (32 MiB per client and per link direction) behaves like a network drop. Sustained or burst traffic above link throughput is dropped this way, and BEST_EFFORT data is then lost (limit 18).
12. **Hub restart** stalls traffic about 7 s.
13. **Long outage.** Longer than the lease drops volatile data by design, and recovery needs the opt-in flag. It is tested only with `--recover-bus-after 20`, with both boards reachable. A peer powered off for hours is NOT_RUN; the backoff to 300 s is checked by reading only. By reading, a value of 120 s or more does not grow the backoff, because a worker that waited that long counts as a healthy run.
14. **Deployed configuration.** `go_file=/dev/null`, a placeholder `peer_network` and hubs started 20 s apart passed `sigterm` 2/2 and `suite` 1/1 with the gate's product binaries (`E:/s2/runs/deployed-*`, `--start-apart 20`), and `suite` again on the `E:/s2q` tree (`final-h-start-apart`). Other start intervals and rebooted boards are NOT_RUN.
15. **One hub per path, not per board.** The lock covers the socket path. Two hubs on different paths on one board are not refused.
16. **NOT_RUN:** ROS on stock 2.14.5, 3.6.2 and 3.2.5, and on Fast DDS 3.x; rviz and rqt GUIs; `ros2 trace`; the hub as a system service; killing the hub supervisor, or both hubs together; a board reboot under a running hub.
17. **Changed oracle: route record minimum.** The `route` check's record minimum is now half of the samples delivered, floored at 1. It used to be all of them. Records can be fewer than samples because RTPS batches several samples into a record: `final-h-route` had 537 records and `final3-h-route-2` had 555, each for 600 samples delivered. That repair after the outage causes this is inferred, not measured. `delivered_all` still requires 600 of 600.
18. **Changed oracle: hub drops in group `large`.** Group `large` now allows hub drops. All other groups stay at zero. 3 of the 5 hub `large` runs on this tree dropped 90 to 129 frames on one side (`final-h-features` 128, `final3-h-features-2` 129, `final3-h-features-3` 90). Two 16 MiB samples exceeded the 32 MiB link queue at link speed. RELIABLE repaired them and every sample arrived intact, which the cases still require. This is a real hub property, not a test artefact, and the deployment guide states the consequence.
19. **`ros2 security` works only off-board.** `cryptography` and `lxml` are absent on the target, so keystores are made on the host.
20. **Changed oracles: run verification (2026-11-09).** A run used to pass the DSoftBus-only check when one watched process had been sampled and none had been seen holding a UDP socket, so a process that was never sampled counted as evidence. `udp_socket_count` now returns `('live', n)`, `('nopid', None)` or `('unreadable', None)`, `SocketWatch` counts the misses per side and samples once before its first wait, and the verdict fails any run in which a watched process was never observed (`no socket observation on <side>`). `sockets_ok` is reported apart from `path_ok`, so `mode_long_outage`, which is excused the record counts (limit 13), is no longer thereby excused the socket evidence. A fault in the sampler thread is now named as such instead of reading as an unreadable board. The `ros2` CLI and rosbag2 rows are stricter too: a `bag info` case needs a printed non-zero message count, each `bag play` case is tied to the bag it reads and must replay the whole recording bar `ALLOWED_PLAYBACK_HEAD_LOSS = 1` message, with at least one message arriving whatever the allowance, and a long-running CLI case must have stopped because it was stopped, judged by a crash blacklist (`CRASH_EXITS`: SIGILL, SIGABRT, SIGFPE, SIGKILL, SIGSEGV) plus a required exit line. A whitelist of clean SIGINT exits was rejected: which code each entry point really reports is not measured, so it would have failed cases that worked. That one-message allowance exists because limit 2 above gives the cause, not merely the shortfall: `ros2 bag play` publishes its first message before its own publisher has matched the subscriber, and a volatile reader cannot receive that sample however sound the transport. It is also the tightest bound the recorded evidence permits. Limit 2 records six (recorded, replayed) pairs — sqlite3 8/7 twice, mcap 8/8 and 8/7, and 8/7 and 9/8 at `46e6615c` — so the largest observed loss is exactly one message, five of the six pairs show it and none shows more; a bound of 0 would have failed a healthy board five times in six. At least one message has to arrive whatever the allowance, because otherwise a recording small enough for the allowance to cover all of it would pass having delivered nothing (`0 >= 1 - 1`), and the `bag info` row asks only for a printed non-zero count, so both rows would have gone green on a total playback failure. The bags here record about eight messages, so reaching that needs a prior catastrophic failure; it is closed rather than left because it is the same weak-criterion class the audit raised. The row is still far stronger than the one it replaced, which accepted any three messages however many were recorded: a 20-message bag now requires 19. **Consequence:** a run that previously reported PASS on a CLI or bag row may still now report FAIL, because a `bag info` row without a printed non-zero count, a long-running case that did not stop cleanly, and a playback short by two messages or more are each new grounds for failure. None of it has been rerun on a board.
21. **Changed deploy: prune, read-back and manifest (2026-11-09).** An incremental deploy used to leave files from earlier builds in the board's `lib/` and `bin/`, so a dependency this build is missing could be satisfied by a stale library and the same artifacts would fail on a clean board. `deploy()` now builds a path-to-SHA-256 manifest of everything the build produces, deletes every file directly in `lib/` and `bin/` that the manifest does not name, reads back what it sent and exits non-zero on a mismatch, confirms the prune removed the files, and writes `<root>/manifest.sha256`. Spared from the prune: the Python layer's own files (`LAYER_OWNED_FILES`, currently `bin/ros2`, asserted by `deploy_python_layer`) and a library the operator staged under the board root and passed with `--diagnostic-preload`. The [deployment guide](ROS2_HUB_DEPLOYMENT.md#4-deploy-to-each-board) states the operator consequence.
22. **Audit findings 2 and 3 are OPEN.** The same audit raised the hub's handling of failed JOIN control messages, and the connection timeout not bounding the blocking `connect()`. Neither is addressed in this change set, and neither can be validated without two boards. They are deferred, not closed.

Required environment and refusals are in the [deployment guide](ROS2_HUB_DEPLOYMENT.md#6-environment-for-each-ros-process).

## 9. Python layer and inputs a clone does not provide

**This repository builds the ROS Python layer from pinned sources, with no ROS source change.** This replaces the older tree on
the boards, which nothing uses any more: the `cli` mode checks that every loaded file is under the deployment or a system
path. rclpy, ros2cli (0.32.12), launch, rosbag2 and sros2 come from the pinned revisions.

**CPython and third-party modules are a declared input, not built here.** `pull_python_target.py` copies them from a board, from
`/data/local/tmp/python312-rk3588a/usr` and `mdds_dds_*/package/python`, and refuses anything whose libpython or NumPy
header differs from the recorded digests. So a clone still needs a board with that Python, or an equivalent copy.

The host needs:

- The pixi Python (default `C:/pixi_ws/.pixi/envs/default/python.exe`, `--python` to change it) with setuptools, wheel, pip and pybind11 2.11.1 in its `Lib/site-packages`.
- Network access: `build_ros2_thirdparty.py` fetches its sources, and rosbag2's `mcap_vendor` downloads mcap during the ROS build.

A clone also does not provide:

- `ros2/src`: the ROS 2 source checkouts holding the pinned revisions.
- The Host ROS tools (`--host-tools`) and the matching CMake and Ninja.
- The `E:/` work directories and evidence.
- On each board, the native token and the `com.kaihong.mdds.*` permission entry. The other prerequisites are in [section 5 of the version guide](FASTDDS_VERSIONS_AND_RELEASE.md#5-reproducing-from-a-clean-clone).

## 10. What production qualification would need

The status stays NOT_PRODUCTION_QUALIFIED. The [deployment guide](ROS2_HUB_DEPLOYMENT.md#12-what-production-qualification-would-need)
lists the work, and [the production support contract](PRODUCTION_SUPPORT_CONTRACT_V1.md) sets the wider requirements.

## 11. Runner audit fixes (2026-11-09)

An external audit of commit `c975ec6f` raised six findings. Four were addressed, in the order the user approved them.
Nothing in this section has touched hardware.

| Item | State |
| --- | --- |
| Implementation | PRESENT |
| Build | N/A for the runner, which is Python; host configure of the stock integration PASS |
| Host Test | PASS: 18 oracle cases, standalone and under `ctest`; the neighbour test `stage2.drop_python_needed` PASS |
| Submodule pin | VERIFIED at the git object level: the new pin is fetchable from `origin`, and the build input is the identical tree |
| RK3588A Test | BLOCKED: no board attached, `hdc list targets` returned `[Empty]` |
| Cross-device Test | BLOCKED |
| Audit findings 2 and 3 | OPEN, deferred; both need two boards (limit 22) |
| Overall | NOT DEVICE-VERIFIED |

### 11.1 What was fixed

**Finding 1 (P1): the DSoftBus submodule pin could not be fetched by anyone else.** `b3afc287873022e7ea870fa7d6f8501aa5a3defa`
exists only in this machine's clone of `communication_dsoftbus` ("chore: ignore local native_dsoftbus_test artifacts",
Thu Nov 5 2026) and is on no remote branch, so a `--recursive` clone failed for every other checkout. The gitlink now names
that commit's parent, `dd6037ce496828133bb9af89b37e2d08232a10d9`, which is on `origin/master`, `origin/HEAD` and
`origin/weekly_20260914`, and which `git describe --tags` reports as `weekly_20240115-v-11438-gdd6037ce4`. The two commits
differ by two lines of the submodule's `.gitignore`, and the `interfaces` tree, the only part of it the build consumes, is the
identical git object `a30132e72fea4ac5a9b9427631818bc0de29e19b` at both pins, so no build output changes. The build reads only
`interfaces/kits/transport` (`socket.h`), `interfaces/kits/bus_center` and `interfaces/kits/common`, as named in
`integrations/ros2/stock/CMakeLists.txt`. **Do not re-pin to `b3afc287`:** the rule it carried
(`tools/native_dsoftbus_test/`) is already in the submodule clone's own `.git/info/exclude` independently of the pin,
`git ls-files --others --exclude-standard --directory tools/` in the submodule returns nothing, and on a fresh clone that
directory does not exist at all, being local device-test output.

**Finding 4 (P2): a missing observation counted as proof of a UDP-free path.** Limit 20 in section 8 states the change.

**Finding 6 (P2): the `ros2` CLI and rosbag2 PASS criteria were too weak.** Limit 20 in section 8 states the change.

**Finding 5 (P2): incremental deployment kept libraries from earlier builds on the board.** Limit 21 in section 8 states the
change, and the deployment guide states what it means for an operator.

An independent review (`AGENTS.md` section 8) then found five defects in the above, and all five were fixed before testing: the
prune deleted the operator's `--diagnostic-preload` library; an empty bag passed its playback row; a whitelist of SIGINT exit
codes (0, 130) would have failed cases that actually worked, and was replaced with the crash blacklist plus a required exit
line; the board was hashed twice per deploy; and a bare `except Exception` in the socket sampler made a sampler bug
indistinguishable from an unreadable board. Each of the five was then reverted one at a time in a throwaway copy of the runner,
and in every case the matching named test failed while the unreverted copy passed, so none of the five is a test that would
pass either way.

A sixth defect was found after that review, during this documentation sync, and fixed before any device run: `playback_ok`
required the whole recording, which limit 2's recorded cause shows would have failed a healthy board on nearly every run.
The bound is now `ALLOWED_PLAYBACK_HEAD_LOSS = 1`, the test pins it from both sides (8 recorded and 7 replayed passes; 20
and 18, and 20 and 3, fail; the constant itself is asserted), and the tester re-ran. A seventh followed from it and was
fixed in the same pass, before any device run: with the allowance alone a one-message recording that replayed nothing
passed (`0 >= 1 - 1`), so `playback_ok` now also requires `replayed > 0`, pinned by `playback_ok(0, 1, 0)` False and
`playback_ok(0, 2, 1)` True. Limit 20 in section 8 states the rule.

### 11.2 Final test evidence

Executed by the independent tester in the `claude/audit-fixes-20261109` worktree on the post-review tree, with `c975ec6f`
as the base.

| Check | Command | Exit | Verdict |
| --- | --- | --- | --- |
| New Host oracle test | `python tests/ros2_stage2/runner_oracle_test.py` | 0 | PASS: `run_ros2_cross_board oracle tests passed (18 cases)` |
| Pre-existing neighbour test | `python tests/ros2_stage2/drop_python_needed_test.py` | 0 | PASS |
| Byte-compile both Python files | `python -m py_compile <file>` | 0 | PASS |
| pyflakes both Python files | `python -m pyflakes <file>` (pyflakes 3.2.0) | 0 | PASS, zero diagnostics |
| `ctest` registration | `ctest -C Release -R "stage2\.(runner_oracle\|drop_python_needed)" -N` | 0 | PASS: Test #3 `stage2.drop_python_needed`, Test #4 `stage2.runner_oracle` |
| `ctest` execution | `ctest -C Release -R "stage2\.(runner_oracle\|drop_python_needed)" -V` | 0 | PASS: 100% tests passed, 0 failed out of 2 |
| New pin on `origin` | `git -C communication_dsoftbus branch -r --contains dd6037ce` | 0 | PASS: `origin/master`, `origin/HEAD`, `origin/weekly_20260914` |
| Old pin local-only | `git -C communication_dsoftbus branch -r --contains b3afc287` | 0, empty | PASS: the finding is confirmed |
| Consumed headers identical | `git -C communication_dsoftbus rev-parse b3afc287:interfaces dd6037ce:interfaces` | 0 | PASS: both `a30132e72fea4ac5a9b9427631818bc0de29e19b` |
| Device presence | `hdc list targets` | 0, `[Empty]` | BLOCKED: no board attached |

The tester's final line is `Commit gate: PASS`, qualified as host-level only.

**Qualification on the `ctest` rows.** The submodule is not populated in that worktree (`git submodule status` reports
`-dd6037ce…`), so the host configure was done in a scratch directory outside it, with `-DDSOFTBUS_SOURCE_DIR` pointed at the
parent checkout's populated clone. The headers were therefore the identical git tree, and the sources and tests that
configured and ran were this tree's; the rows do not evidence a configure from this worktree's own submodule.

### 11.3 What is not verified

- **DEVICE_TESTED = false.** RK3588A test BLOCKED, cross-device test BLOCKED.
- No part of the modified runner's real behaviour has run on hardware: no real `deploy()` against a board, so no real
  `sha256sum`, prune, `manifest.sha256` or `send`; no real `/proc` sampling over `hdc`; no real `ros2` CLI or rosbag2
  record, info or play; `mode_cli` and `mode_long_outage` NOT_RUN. All of it is exercised through test doubles only.
- The C++ side was not built and the other stock Host tests are NOT_RUN; this change set touches no C++.
- The device results in sections 5 and 6 were produced by the previous runner. They remain true of what they measured and
  are not restated here as validating this change set.
