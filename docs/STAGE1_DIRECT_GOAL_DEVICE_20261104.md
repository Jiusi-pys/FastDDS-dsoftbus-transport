# Stage 1 direct-goal dual-board DDS — startup root cause and fix (2026-11-04)

Status: **DEVICE_TESTED (bounded scope below) / NOT_PRODUCTION_QUALIFIED**.

| Scope | Result |
| --- | --- |
| Fresh-process A↔B DDS, 3 samples each way, GUID/SN/payload matched by controller | PASS (run m2) |
| Participant delete, runtime destroy, process exit, entry identity restore | PASS (run m2) |
| Fragmented sample: 150000 bytes per direction, transport max 65536, byte-verified and acked | PASS (run m3) |
| Same-name Participant hot rebuild in process (PersistentNativeAnchor), 150000-byte sample both ways after rebuild | PASS (runs m4, m7) |
| Route outage then recovery (15 s IP drop on B, `NO_REACCEPT`) | **FAIL (runs m5, m6): no recovery**. Gap confirmed, transport fix not implemented |
| ROS 2 over this build | NOT_RUN |
| SDK quiet, worker/descendant restoration | UNKNOWN |
| Host build of `integrations/ros2/stock` (MSVC Release, stock-host-d01 Fast DDS 2.14) | BUILDS |
| Host ctest of `integrations/ros2/stock` | **FAIL: 53/64 pass**. 11 fail in the candidate tree's fake-socket tests: lifecycle, defaults-changed, work.pair/factory/stop/stop-inflight/factory-held-native, default_domain.raw, default_domain.startup-failure, normal_control.await-senders, circuit.raw. The runner fix is not part of this project. Log: `E:/hb1104/test.log` |

Boards: A `3e01ff55454d202020104033bf453b00`, B `3e01ff55454d202020104433991c3b00`
(RK3588A, OpenHarmony SDK 23), controlled only through `hdc`.

## Root cause

The earlier "A fails with `NS_STATE`, B exits" result is one defect. Run m1 used the
unfixed runner/launcher (`ec733e0c…` / `cbd24f3f…`) with the startup cause records enabled.
Both boards recorded `first_reason=PARTICIPANT_NULL` at stage `PARTICIPANT_CREATION`,
then Execute returned `NS_STATE(-9)`. Both `app.err` files show
`Fast DDS DSoftBus init: error=5 operation=no_fragment_limit`, and Fast DDS logs
`User transport failed to register`.

`tests/stage1_native_successor/consumer_run.cpp` builds its descriptor with
`DSoftBusTransportDescriptor(65536)`, but it never set the participant's
`qos.transport().max_msg_size_no_frag`. Fast DDS 2.14 therefore calls
`DSoftBusTransport::init(..., 0)`, and the effective limit becomes 65500. `init` then
rejects a 65536-byte maximum, which is the same rule Fast DDS applies to its built-in
UDP transport. The ROS bridges already set this field
(`integrations/ros2/Bridge.cpp`, `integrations/ros2/target/TargetBridge.cpp`).

In the D05 package run, B's earlier exit happened because the host harness halted
(`PRECREATE_FAILED_OR_UNKNOWN`) before it published the pair record. Both boards then
timed out at phase 14 (`Pair`). This is a host harness failure, not a transport failure.

## Fix

One configuration line in the test runner, with no Transport, RTPS, DDS or ROS 2 change:

```cpp
qos.transport().max_msg_size_no_frag = config.maxMessageSize;
```

## Device run m2 (fixed)

The deployed runner was the startup-diagnostics overlay of `consumer_run.cpp` plus the
line above. It was built with the SDK 23 clang from the recorded compile/link argv.

- Runner: SHA256 `906195590a5a812fb99d602e96ad61df7a472a5de4679db055ba6de94dfba3b1`, 305440 bytes.
- Launcher: SHA256 `5ede3ce192d03c1fa53be3d2978d973da63934fece8160bf785803f2567e560d`, 91928 bytes. Only `GOAL_APP_SHA` was changed in the launcher.
- Other libraries: the existing D05 payloads, including `libfastdds_dsoftbus_stock_214.so` `14878b1c…`.

The committed source is the un-instrumented `consumer_run.cpp` with the same one-line
change. It has not itself been rebuilt for ARM64 in this commit.

| Board | data-result | exit | cause |
| --- | --- | --- | --- |
| A | status 0, phase 4, writes 3, takes 3, destroy 0, owners_retained 0 | status 0, restored_entry 1, restore_rc 0 | NO_RECORDED_FAILURE; gate COMPLETED |
| B | status 0, phase 4, writes 3, takes 3, destroy 0, owners_retained 0 | status 0, restored_entry 1, restore_rc 0 | NO_RECORDED_FAILURE; gate COMPLETED |

The controller (`goal_controller.exe --coordinate`) exited 0 and reported
`ROOT_MATCH_GUID_SN_PAYLOAD_ACK_DONE_TEARDOWN=1`, with 4/4 releases delivered to both
boards. No `own_goal_*` or `goal_supervisor` process remained on either board after the run.

The run was driven by a plain `hdc` driver, not the D05 harness. That driver reuses the
package's `goal_records` builders and the existing Windows controller. Evidence, scripts
and full logs are on the operator host:

- `E:/Codex-FastDDS-stage1-directdds-manual-m2-20261104/`: `RESULT.md`, `driver.log`, `BUILD.json`, `manual_dds.py`, `rebuild_fix.py`.
- `E:/Codex-FastDDS-stage1-directdds-manual-m1-20261104/`: the failing pre-fix run.

Evidence-handling note: the D05 data-window board records were deleted before they were
archived, because of a Git Bash path-conversion error. Their contents had been read
before deletion, and no cause record existed among them. Records from m1 onward are archived.

## Device run m3 (fragmented sample)

The runner now also exchanges one 150000-byte sample per direction on a separate
RELIABLE topic, `native_run_<run>_<side>_frag`. The type is in `tests/stage1_native_successor/frag_sample.hpp`.
Each side writes its sample, takes the peer's, checks every byte against a deterministic
pattern, and waits for acknowledgment. Any failure fails the run. This exchange is not
reported through the 32-byte event protocol, so the controller and its record formats are unchanged.

- Runner: SHA256 `fe7d048d11e127052b7b0e40f2c6bb8974809ef96ca0d9a8bd9fbcc7ec4c9c94`, 337464 bytes.
- Launcher: SHA256 `c84d4108c9ba2618443317040c23720b0a052751a74f91de49d103d3c43b4108`, 91928 bytes.
- Both were built from the diagnostics overlay plus the same two changes that are committed here.

| Board | data-result | fragmented sample | exit |
| --- | --- | --- | --- |
| A | status 0, phase 4, writes 3, takes 3, destroy 0 | `FRAG_SAMPLE bytes=150000 max_message=65536 written=1 taken=1 verified=1 acked=1` | status 0, restored_entry 1 |
| B | status 0, phase 4, writes 3, takes 3, destroy 0 | same | status 0, restored_entry 1 |

The controller exited 0 with `ROOT_MATCH_GUID_SN_PAYLOAD_ACK_DONE_TEARDOWN=1`, and no
processes remained on either board. Fragmentation follows from the sample size, which
exceeds the maximum message size. RTPS DATA_FRAG counts were not captured.
Evidence: `E:/Codex-FastDDS-stage1-directdds-manual-m3-20261104/` (`RESULT.md`, `driver.log`, scripts, overlay source).

## Runs m4–m7: hot rebuild and route outage

**Hot rebuild (m4, and again in m7 with the final runner).**
- The runner now constructs `Runtime(config, RuntimeLifetime::PersistentNativeAnchor)`.
- After the fragment exchange, each side deletes its whole Participant and issues a second
  descriptor on the live native anchor. It then creates a Participant with the same name and a new GUID prefix.
- The two sides exchange one 150000-byte sample per direction on a TRANSIENT_LOCAL topic, each
  verified and acked. The runner then recreates the standard endpoints for the controller's ACK and teardown phases.
- Both boards: `HOT_REBUILD same_name=1 new_guid_prefix=1 descriptors=2 ... verified=1 acked=1`.
  Each also reports status 0, 3/3 writes/takes, fragment PASS, destroy 0 and restored_entry 1, and the controller exited 0.
- Final runner `b153c913f50674078cc0555b276611f9be421bc40ae0ea13d8a92709f9538ad2`, launcher `968c36165f07f7a285fa7639ffce7310f497b3f0f8f9187dcb125622fa020020`.

**Route outage (m5, m6).** The window is opt-in and runs only when `TMPDIR/route_window` exists.
- Each side writes a counter sample every 200 ms for 45 s on the rebuilt Participant.
- During the window, the host ran a board-side background script on B. It dropped all IP traffic
  to and from A's addresses (192.168.77.201 and 192.168.8.115, i.e. eth1 and wlan0) with `iptables` for 15.1 s.
  The script then removed the rules, which was verified (0 rules left).

Result on both boards: samples were received until the drop (about 5.8 s into the window), and
none arrived for the remaining ~24 s after the route returned (`recovered=0`).

The socket observation at the end of the window showed:
- `out_stopped=1`: DSoftBus shut down the outgoing socket.
- `socket_calls=2`: one listener plus the original outgoing socket, so no new Socket/Bind was attempted.
- `send_calls` stopped growing, and there was no pump error and no abort.

Cause: in the stock domain-circuit mode, a stopped outgoing socket is never renewed. Sends then
return ProviderStopped without surfacing to the pump. The existing renewal path
(`ProviderAdapter` AdvanceHostReaccept / ResetBoundedConnection) is enabled only in the
default-off bounded-reaccept experiment, and `Runtime` rejects that experiment together with the stock/persistent modes.
**Status: route-down re-accept NOT IMPLEMENTED for this path, DEVICE-confirmed gap.**

Evidence: `E:/Codex-FastDDS-stage1-directdds-manual-m4…m7-20261104/` (driver logs, board tars, scripts, overlay source).
