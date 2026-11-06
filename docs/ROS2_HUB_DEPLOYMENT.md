# Deploying ROS 2 over DSoftBus with the per-board hub

This guide shows how to run unmodified ROS 2 Jazzy nodes on two OpenHarmony boards through `fdds_dsb_hub`.

**Status: bounded DEVICE_TESTED on `2.14.5-ros` between two RK3588A boards, and NOT_PRODUCTION_QUALIFIED.** Follow this
guide to set up a deployment and test it. It does not qualify one for production, and section 12 says what would.
Results and evidence are in the [Stage 2 record](STAGE2_ROS2_DEVICE_20261105.md).

Commands below use the layout the tests used. Every run started the hub over `hdc` as root. **Running the hub as a system
service is NOT_RUN**, so this guide has no service definition.

## 1. When to use the hub

| Need | Path |
| --- | --- |
| One ROS process per board, one domain | Direct path or hub |
| Several ROS processes on a board, `ros2` CLI, rclpy, launch, rosbag2, SROS2 | Hub. These were tested on the hub only. |

No RTPS, RMW or ROS source changes on either path. The hub path needs Fast DDS `2.14.5-ros` and works with exactly two boards.

## 2. What runs where

On each board:

- **`fdds_dsb_hub`**, one per board. It owns the DSoftBus link and needs the native token.
- **ROS processes**, any number. Each loads `libfastdds_dsoftbus_ros2_hub.so` through `LD_PRELOAD` and attaches to the hub through an `AF_UNIX` socket. They load no SDK and hold no token.

## 3. Build

Build on the Windows host. Host needs: Git Bash, the OHOS SDK, `ros2/src` with the pinned sources, the pixi Python
(default `C:/pixi_ws/.pixi/envs/default/python.exe`) with setuptools, wheel, pip and pybind11 2.11.1, the host ROS tools
(default `C:/pixi_ws/ros2-windows`), and network access, because third-party sources and rosbag2's `mcap_vendor` download.

```bash
W=E:/s2   # any empty work directory
# Fast DDS, Transport and hub
bash scripts/release/build_fastdds_ohos.sh 2.14.5-ros $W
python scripts/release/pull_platform_libs.py <board A target> $W/platform
bash scripts/release/build_transport_ohos.sh 2.14.5-ros $W $W/platform
# ROS 2 with the Python layer
python scripts/release/pull_python_target.py <board A target> $W
python scripts/release/build_ros2_thirdparty.py --work $W
python scripts/release/build_ros2_ohos.py --work $W --python-target $W/python-target
python scripts/release/build_ros2_python.py --work $W
```

The last four took 22, 41, 916 and 80 s from empty, on top of an existing Fast DDS and Transport build. Without the Python layer,
skip them and run `build_ros2_ohos.py --work $W`.

`pull_python_target.py` copies CPython and third-party modules from a board, so a clone needs a board with that Python, or an
equivalent copy. It refuses a libpython or NumPy header that differs from the recorded digests. CPython is not built here.
Two builds are not byte-identical (314 of 448 ELF files identical, the rest the same size), so verify what you ship.

The hub is built only for `2.14.5-ros`. Outputs:

| Output | Path |
| --- | --- |
| Hub and test tools | `$W/2.14.5-ros/transport-install/bin/` (`fdds_dsb_hub`, `dsb_suite`) |
| Transport and preload libraries | `$W/2.14.5-ros/transport-install/lib/` |
| Fast DDS closure | `$W/2.14.5-ros/prefix/lib/` |
| ROS 2 and its Python layer | `$W/ros/install/` |
| Python runtime and third-party modules | `$W/python-target/` |
| yaml-cpp, zstd, lz4, sqlite | `$W/third-party/install/` |

Your ROS application must be built against this tree. Fast CDR symbols differ between patch releases, so a typesupport
library built against another Fast CDR fails to load. `build_ros2_ohos.py` builds the common message packages for this reason.

## 4. Deploy to each board

**Board prerequisites** are outside these scripts: the boards are already DSoftBus-paired, and each has the native token
with `ohos.permission.DISTRIBUTED_DATASYNC` and the `softbus_trans_permission.json` entry for `com.kaihong.mdds.*`.
See [section 5 of the version guide](FASTDDS_VERSIONS_AND_RELEASE.md#5-reproducing-from-a-clean-clone).

The simple way to deploy is one hub test run. It copies whatever differs, by SHA-256, and proves the pair works. The Python
layer goes as one archive of about 209 MiB, and needs about that much free space again to unpack:

```bash
python scripts/release/run_ros2_cross_board.py --work $W --a <A> --b <B> --hub --mode smoke
```

This fills `/data/local/tmp/fdds_dsb/2.14.5-ros/`:

| Board path | Content |
| --- | --- |
| `lib/` | Every library from `prefix/lib`, `transport-install/lib`, `ros/install/lib`, `ros/install/opt/*/lib`, `third-party/install/lib`, plus `libc++_shared.so` |
| `lib/<package>/` | Package executables and scripts, such as the entry points `ros2 run` finds |
| `lib/python3.12/site-packages/` | The ROS Python packages and `sitecustomize.py` |
| `bin/` | `fdds_dsb_hub`, `dsb_suite`, `ros2`, test programs, unmodified ROS programs |
| `python/usr/` | The Python runtime |
| `python/third_party/` | NumPy, PyYAML and the other third-party modules |
| `share/` | Package resources and the ament index, with CRLF stripped |

If you copy files by hand, strip CRLF from the ament index, because a CR left in a resource file breaks `dlopen`. Files archived
on Windows have no execute bit, and `ros2 run` only offers executable files.

The runner kills every `fdds_dsb_hub` on both boards when it starts and ends, so do not run it against a hub you are using.

## 5. Configure and start the hub

### 5.1 Find the values

```bash
# Native token: the tokenId of your provisioned process in this file
hdc -t <board> shell cat /data/service/el0/access_token/nativetoken.json

# This board's UDID and network id
hdc -t <board> shell "cd /data/local/tmp/fdds_dsb/2.14.5-ros && LD_LIBRARY_PATH=lib ./bin/dsb_suite node token=<token>"
# prints: DSB_NODE network=<id> udid=<udid>
```

### 5.2 Write the config file

One file per board, `key=value`, one per line. Lines starting with `#` are ignored. All 8 keys are required and non-empty,
and any other key is refused.

| Key | Value |
| --- | --- |
| `side` | `0` on one board and `1` on the other |
| `run` | 16 hexadecimal digits, the same on both boards. Native socket names come from it. |
| `domain` | 0 to 232, the same on both boards. It identifies the link. It is not checked against `ROS_DOMAIN_ID`. |
| `local_udid` | This board's UDID |
| `peer_udid` | The other board's UDID. The hub finds the peer through it. |
| `peer_network` | Any non-empty value. It is replaced at start by a lookup from `peer_udid`. The tests passed the current network id. |
| `token` | The native token as a decimal number |
| `go_file` | An absolute path. The hub waits for this file to exist before it connects. `/dev/null` skips the wait. |

```ini
side=0
run=0123456789abcdef
domain=151
local_udid=<this board>
peer_udid=<other board>
peer_network=<network id of the other board>
token=<token>
go_file=/dev/null
```

This deployed form was tested with the hubs started 20 s apart, `go_file=/dev/null` and a placeholder `peer_network`: `--start-apart 20` gave `sigterm` 2/2 and `suite` 1/1 (`E:/s2/runs/deployed-*`), and `suite` again on the from-empty build. The other recorded runs used a real go file. Boards started at other intervals, and rebooted boards, are NOT_RUN.

The file holds the token, so restrict who can read it.

### 5.3 Start the hub

```bash
cd /data/local/tmp/fdds_dsb/2.14.5-ros
LD_LIBRARY_PATH=$PWD/lib ./bin/fdds_dsb_hub --supervise [--recover-bus-after <seconds>] <config> <socket path>
```

- Keep the flags in this order. `--recover-bus-after` takes 1 to 86400.
- The socket path must be absolute for clients, its directory must exist, and it must be shorter than 108 bytes.
- Use one socket path per board. The lock is on `<socket path>.lock`, so two hubs on different paths are not refused, and they would claim the same native names.
- The hub creates the socket with mode 0660. ROS processes need that access.
- Start the hub before ROS processes. A process waits 5 s for the hub when it creates a Participant.
- The hub logs to stdout and stderr, so keep them. Nothing handles SIGHUP, and detached starts are NOT_RUN.

The link is up when `FDDS_HUB_LINK_READY` appears. To stop, send SIGTERM or SIGINT to the supervisor. It forwards the
signal, the worker prints its final stats, and both exit 0. ROS processes keep running and reconnect if a hub returns.

### 5.4 `--recover-bus-after`: decide before you use it

After an outage of about 45 s the boards can leave the DSoftBus network and not return (section 10). With this flag, a
hub that has waited that many seconds for the peer restarts `softbus_server` by SIGKILL, and the service then came back
on these images.

| For | Against |
| --- | --- |
| Links return with no operator and no ROS process restart: measured 11.4 to 12.4 s after the route returned | Interrupts **every** DSoftBus user on the board |
| | Needs permission to kill `softbus_server`; the tests ran as root |
| | Fires even when the peer is simply off, so it keeps restarting a healthy service |

It is off by default. Only `--recover-bus-after 20` was tested, with both boards reachable. A peer powered off for hours is NOT_RUN.

Timing, from the code. A worker exits about 10 s after the peer goes missing, restarts 2 s later, then waits N seconds
before it restarts the bus. So with N = 20 a restart comes about 32 s into an outage, which is longer than the 15 s outage
that recovers by itself. A restart while the route is still down does not help, and the cycle repeats. The pause between
attempts doubles from 2 s to at most 300 s, and resets after a worker runs 120 s. Because of that reset, **with N of 120 or
more the backoff never grows**, and attempts stay about N + 2 s apart. Choose N with that in mind.

## 6. Environment for each ROS process

| Variable | Value |
| --- | --- |
| `LD_PRELOAD` | `/data/local/tmp/fdds_dsb/2.14.5-ros/lib/libfastdds_dsoftbus_ros2_hub.so` |
| `FASTDDS_DSOFTBUS_HUB` | The hub's socket path, absolute |
| `RMW_IMPLEMENTATION` | `rmw_fastrtps_cpp` |
| `ROS_AUTOMATIC_DISCOVERY_RANGE` | `SYSTEM_DEFAULT` |
| `ROS_DOMAIN_ID` | Your ROS domain, the same on both boards. The hub serves any domain. |
| `LD_LIBRARY_PATH` | The deployed `lib` directory first |
| `AMENT_PREFIX_PATH` | The deployed root, for `class_loader` and generic pub/sub |
| `HOME`, `ROS_HOME`, `ROS_LOG_DIR` | A writable directory. The tests set all three. |

```bash
R=/data/local/tmp/fdds_dsb/2.14.5-ros
cd $R && LD_LIBRARY_PATH=$R/lib AMENT_PREFIX_PATH=$R HOME=/data/local/tmp/app ROS_HOME=/data/local/tmp/app \
  ROS_LOG_DIR=/data/local/tmp/app/log RMW_IMPLEMENTATION=rmw_fastrtps_cpp ROS_DOMAIN_ID=151 \
  ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT FASTDDS_DSOFTBUS_HUB=<socket path> \
  LD_PRELOAD=$R/lib/libfastdds_dsoftbus_ros2_hub.so ./bin/talker
```

**Refused at start.** The process exits 71 with `FDDS_HUB_REFUSED reason=…`, because each of these could add another transport:

- `FASTDDS_DSOFTBUS_HUB` missing or not absolute;
- `RMW_IMPLEMENTATION` other than `rmw_fastrtps_cpp`;
- `ROS_AUTOMATIC_DISCOVERY_RANGE` other than `SYSTEM_DEFAULT`, or `ROS_LOCALHOST_ONLY=1`;
- `ROS_STATIC_PEERS` set;
- `FASTRTPS_DEFAULT_PROFILES_FILE`, `FASTDDS_DEFAULT_PROFILES_FILE`, `FASTDDS_BUILTIN_TRANSPORTS`, `FASTDDS_ENVIRONMENT_FILE` or `ROS_DISCOVERY_SERVER` set;
- `RMW_FASTRTPS_USE_QOS_FROM_XML=1`, which could let same-board traffic use shared memory.

The check is the one the direct path uses, and its Host test passes. On the device, per-setting refusal was tested on the direct path only, 26 of 26.

**Hub not there.** The process prints `FDDS_HUB_CLIENT_REFUSED reason=no_hub` and its Participant cannot be created. It
does not fall back to UDP or shared memory. The test program ended with exit 134 (SIGABRT), and your application's exit
depends on how it handles that error.

**Do not create ContentFilteredTopics while writers are active.** The Fast DDS race has no workaround on the ROS path.
See [section 6.4 of the version guide](FASTDDS_VERSIONS_AND_RELEASE.md#64-content-filter-race-on-fast-dds-3x-re-evaluated-not-assumed).

## 7. Python, `ros2` CLI, launch and rosbag2

This repository builds the ROS Python layer from pinned sources (section 3), and everything runs from the deployed root. Nothing
outside it is on any path, so a missing file fails rather than finding an older copy. `--mode cli` passed 42 of 42 checks, twice,
with 755 loaded files and none outside the deployment or system paths.

Python processes use section 6 with these differences, where `R` is the deployed root:

| Variable | Value |
| --- | --- |
| `PATH` | `$R/python/usr/bin:/system/bin:/bin`. Entry scripts start through `/bin/env python3.12`. |
| `LD_LIBRARY_PATH` | `$R/lib:$R/python/usr/lib`, in this order |
| `PYTHONPATH` | `$R/lib/python3.12/site-packages:$R/python/third_party` |
| `AMENT_PREFIX_PATH` | `$R` |
| `ROS_DISTRO`, `ROS_VERSION`, `ROS_PYTHON_VERSION` | `jazzy`, `2`, `3` |
| `LD_PRELOAD` | `$R/python/usr/lib/libpython3.12.so.1.0:$R/lib/libfastdds_dsoftbus_ros2_hub.so`, libpython first |

`ros2` is an ordinary command:

```bash
R=/data/local/tmp/fdds_dsb/2.14.5-ros
PATH=$R/python/usr/bin:/system/bin:/bin LD_LIBRARY_PATH=$R/lib:$R/python/usr/lib \
  PYTHONPATH=$R/lib/python3.12/site-packages:$R/python/third_party AMENT_PREFIX_PATH=$R \
  HOME=/data/local/tmp/app ROS_HOME=/data/local/tmp/app ROS_LOG_DIR=/data/local/tmp/app/log \
  ROS_DISTRO=jazzy ROS_VERSION=2 ROS_PYTHON_VERSION=3 RMW_IMPLEMENTATION=rmw_fastrtps_cpp ROS_DOMAIN_ID=151 \
  ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT FASTDDS_DSOFTBUS_HUB=<socket path> \
  LD_PRELOAD=$R/python/usr/lib/libpython3.12.so.1.0:$R/lib/libfastdds_dsoftbus_ros2_hub.so \
  $R/bin/ros2 topic list
```

The tests also set `PYTHONDONTWRITEBYTECODE=1` and `PYTHONUNBUFFERED=1`. `run_ros2_cross_board.py` class `PythonOnBoard` holds the exact lines.

libpython stays in `LD_PRELOAD` because `python3.12` is a thin launcher and NumPy's prebuilt modules find the interpreter through a
global libpython. C++ nodes need neither of these: installed C++ programs have no dependency on Python, so section 6 is enough for them.

`ros2 bag` works with sqlite3, mcap and zstd file compression. A play can deliver 1 message fewer than recorded, because the first
goes before the player's publisher matches.

`ros2 security` does not work on the board, because `cryptography` and `lxml` are absent there. See section 8.

## 8. SROS2

Create the keystore on the host, because `ros2 security` needs `cryptography` and `lxml`, which the target Python lacks. Set
`ROS_DOMAIN_ID` to the domain you will run first:

```bash
ROS_DOMAIN_ID=151 ros2 security create_keystore keystore
ROS_DOMAIN_ID=151 ros2 security create_enclave keystore /app/talker
```

- **Without `ROS_DOMAIN_ID`, the permissions cover domain 0 only**, and access is denied at runtime. In the first test both secure processes exited 134 for this reason.
- On Windows, `create_keystore` needs the right to create symbolic links.

Copy the keystore to the boards and add this to each secure process:

```bash
ROS_SECURITY_KEYSTORE=<keystore> ROS_SECURITY_ENABLE=true ROS_SECURITY_STRATEGY=Enforce ... ./bin/talker --ros-args -e /app/talker
```

Measured under Enforce: a keyed pair exchanged data, a process with no identity heard 0 and kept running, and a process
naming an unknown enclave failed (exit 134). The test copied the whole keystore to both boards for convenience. A real
deployment should not leave the CA private keys on a board, but that layout is NOT_RUN.

SROS2 is the authentication boundary on this path, not the Transport or the hub. See section 11.

## 9. Reading the hub

### Statistics

`SIGUSR1` makes each worker print one line to its stdout. The supervisor ignores the signal, so it is safe to signal both:

```bash
kill -USR1 $(pidof fdds_dsb_hub)
```

Board A at the end of the soak:

```text
FDDS_HUB_STATS exit=0 clients=0 routed=60135 link_sent=27625 link_received=25178 sdk_sends=27625 sdk_records=25178 send_failures=0 refused=0 dropped_out=0 dropped_in=0 dropped_client=0 malformed=0
```

Counters restart with each worker, so compare two samples from one worker.

| Field | Meaning | Healthy |
| --- | --- | --- |
| `exit` | -1 on request; the exit code on the final line | |
| `clients` | Connections open now | Your process count |
| `routed` | Frames routed by this hub | Rising under traffic |
| `link_sent`, `link_received` | Frames sent to and received from the peer hub | Rising under cross-board traffic |
| `sdk_sends`, `sdk_records` | Records that crossed the SDK | Track the two above |
| `send_failures` | SDK send failures | Flat |
| `refused` | Link sends not accepted within 2 s | Flat. Rising with `link_sent` flat means the link cannot be used. |
| `dropped_out` | Frames dropped because the link was down or its 32 MiB queue was full | Flat. Rises during an outage. |
| `dropped_in` | Frames from the peer dropped at the 32 MiB inbound queue | 0 |
| `dropped_client` | Frames dropped for a local process whose 32 MiB queue was full | 0. Otherwise that process is too slow. |
| `malformed` | Frames with a bad header, or sent before attaching | 0. Otherwise something other than the preload is writing to the socket. |

A drop behaves like a network drop: RTPS repairs reliable data, and best-effort data is gone. The 30-minute soak ended with all drops at 0.

**Sustained or burst traffic above link throughput is dropped by the hub.** Link throughput for large samples was about 9 to 13
MiB/s in these runs. In 3 of 5 runs, writing two 16 MiB samples at once overflowed the 32 MiB link queue and dropped 90 to 129
frames on one side. RELIABLE repaired them and every sample arrived intact. BEST_EFFORT data would have been lost. So size traffic
below link throughput, use RELIABLE for data that must arrive, and watch `dropped_out`.

### Log lines

| Line | Meaning |
| --- | --- |
| `FDDS_HUB_WAITING_FOR_PEER` | The peer's UDID is not on the bus. See section 10. |
| `FDDS_HUB_LISTENING` | Listening, about to wait for the go file |
| `FDDS_HUB_LINK_READY side=<n>` | Link open. Traffic can flow. |
| `FDDS_HUB_LINK_STATE peer_offline=1` | The peer has been missing for about 10 s |
| `FDDS_HUB_LINK_STATE closed=… stopped=… reason=… error=…` | The SDK socket closed or reported an error |
| `FDDS_HUB_LINK_STATE pump_failed=1 source=… value=…` | The pump failed. Source: 1 send, 2 receive, 3 wait, 4 exception. |
| `FDDS_HUB_LINK_LOST` | The worker is exiting so a fresh one can reopen the link |
| `FDDS_HUB_LINK_FAILED reason=…` | Opening the link failed. Read the reason. The supervisor keeps retrying, so a wrong token or stale identity shows as this line repeating. |
| `FDDS_HUB_SEND_FAILURE code=…` | First SDK send failure of this worker. Counted and tolerated. |
| `FDDS_HUB_BUS_RESTART done=<0/1>` | `--recover-bus-after` fired. `done=0` means no `softbus_server` could be killed. |
| `FDDS_HUB_WORKER_EXIT code=<n>` | Printed by the supervisor |
| `FDDS_HUB_REFUSED reason=…` | Stopped for good. See below. |
| `FDDS_HUB_CLIENT_DISCONNECTED`, `FDDS_HUB_CLIENT_RECONNECTED` | Printed by a ROS process when it loses and regains the hub |

| Worker exit | Meaning | Supervisor |
| --- | --- | --- |
| 0 | Stopped by a signal | Stops |
| 71 | Refused: unreadable or invalid config, or local socket problem | Stops. A restart cannot help. |
| 75 | Link lost | Restarts after 2 s |
| 76 | `softbus_server` was restarted | Restarts after the backoff |
| Negative | Killed by that signal number | Restarts after 2 s |

A second hub on one socket path prints `FDDS_HUB_REFUSED reason=local socket`. That text also covers a socket path that is
too long or a directory that does not exist.

## 10. Failure modes and recovery

| Event | What happens | Evidence |
| --- | --- | --- |
| ROS process killed | Others are not affected. A replacement joins with no restart. | `multiprocess`, `kill_peer` PASS |
| Hub worker killed | Supervisor restarts it, processes reattach, traffic stalls about 7 s. Reliable data resumed with 600/600. | `hub_restart` PASS 3/3 on one board |
| Route lost up to about 15 s | The open link survives. Reliable data resumes, 600/600, max silence 17.8 s. | `route` PASS |
| Route lost longer than the 20 s lease | Participants expire and volatile data sent meanwhile is lost by design. Traffic rematches when the bus returns. | `long_outage` PASS with the flag |
| Peer gone from the bus | Worker exits after about 10 s, then prints `FDDS_HUB_WAITING_FOR_PEER` | `long_outage` |
| Local process too slow | Frames for it are dropped at 32 MiB. Others are not affected. | Reading; `dropped_client` stayed 0 in all runs |
| Hub supervisor killed, both hubs killed together, board reboot under a running hub | NOT_RUN. Nothing starts the hub after a reboot. | |

### `remote device num = 0`

Check the bus on each board:

```bash
hidumper -s 4700 -a "buscenter -l remote_device_info" | grep "remote device num"
```

`1` means the peer is on the bus. `0` with the hub printing `FDDS_HUB_WAITING_FOR_PEER` means the boards left the network.

- 15 s outages recovered by themselves. 45 s and 70 s did not, without a restart of `softbus_server`. Between 15 and 45 s is NOT_RUN.
- **A reboot is not required.**

With `--recover-bus-after`, wait: the hub does this itself. In one measured case only one board needed to restart.

Without it:

1. Confirm the route is back, for example with `ping` between the boards.
2. Restart `softbus_server` on one board: `kill -9 $(pidof softbus_server)`. The system brought it back on these images. This interrupts every DSoftBus user on that board. Board tools differ between images, and the hub itself finds the process through `/proc`.
3. Check `remote device num` on both boards. If it stays 0, do the same on the other board. The operator saw this restore the bus in under 10 s; the run records show only the 11.4 to 12.4 s of the automatic path.
4. Stop and start both hubs. A waiting hub's SDK connection was to the old service, and the hub only starts a new one when it restarted the bus itself. A manual restart under a running hub is NOT_RUN, so this step is the cautious order.

## 11. Limits

- **Status:** bounded DEVICE_TESTED on `2.14.5-ros`, NOT_PRODUCTION_QUALIFIED. Stock 2.14.5, 3.6.2, 3.2.5 and Fast DDS 3.x are NOT_RUN for ROS.
- **Two boards only.** The hub has one peer.
- **Local access is the only protection on the socket.** Mode 0660, no other authentication. Any process with that access can inject frames. Use SROS2 where that matters.
- **Traffic above link throughput is dropped** like on a network (section 9). Watch `dropped_out`.
- **Every frame is copied through a local socket**, and same-board traffic crosses the hub too. Latency was not measured. Large-message rates in the [Stage 2 record](STAGE2_ROS2_DEVICE_20261105.md#5-results) are small samples that vary between runs.
- **The soak is 30 minutes:** 17980/17981 each way, 0 gaps, hub memory growth 28 KB. Days are NOT_RUN.
- **Scale tested:** 16 processes, 8 per board, at 1 Hz.
- **Hub restart stalls traffic about 7 s.**
- **Long outage** recovery needs the opt-in flag (section 5.4).
- **CPython and third-party modules** are a declared input pulled from a board, not built here (section 3). `ros2 security` works only off-board.
- **Direct-path second Participant** falls back to the 20 s lease, root cause not found. The hub is not affected.
- **`--recover-bus-after` of 120 s or more** never grows the backoff (section 5.4).
- **ContentFilteredTopics** must not be created while writers are active (section 6).
- **NOT_RUN:** rviz and rqt GUIs, `ros2 trace`, the hub as a system service.
- **Not proven:** SDK drain, epoch and cancellation behaviour.
- **Unique flows** set to STRICTLY_REQUIRED would fail. `No unicast locators to create unique flows` appears once per process and is expected.

The full list with evidence is in [section 8 of the Stage 2 record](STAGE2_ROS2_DEVICE_20261105.md#8-limits-workarounds-and-unexplained-behaviour-agentsmd-53).

## 12. What production qualification would need

None of the following is done. This list is the work the evidence does not cover, and
[the production support contract](PRODUCTION_SUPPORT_CONTRACT_V1.md) sets the wider requirements.

**Deployment**

- Run the hub as a system service with a defined identity, not root over `hdc`: start order, restart policy, log capture, permission to set the token.
- Choose a path outside `/data/local/tmp` and define the socket's owner and group.
- Decide how the token and config are provisioned and protected.
- Decide how CPython and its third-party modules are supplied and maintained, since they are pulled from a board and not built here.

**Endurance and failure**

- Soak for days, with real workload rates, memory and file descriptor limits, on both paths you ship.
- Test power loss, board reboot under traffic, supervisor kill, both hubs killed, and boards started at unrelated times.
- Test a peer off for hours with `--recover-bus-after`, and outages between 15 and 45 s.
- Find why the bus stays down after a long outage. A restart of the service is a workaround.
- Prove SDK drain, epoch and cancellation behaviour.
- Find the root cause of the direct path's lease fallback for a second Participant, if you use that path.

**Scope**

- Measure latency, and repeat throughput enough to give stable figures.
- Run the hub environment refusals on the device.
- Qualify each exact firmware, SDK and binary set you ship. A result here does not transfer to another build.
- Resolve the ContentFilteredTopic race upstream, or keep the rule that forbids the pattern.
- Decide whether local socket permissions are enough for your threat model.
- Add more than one peer, if your system has more than two boards. That is a design change.
