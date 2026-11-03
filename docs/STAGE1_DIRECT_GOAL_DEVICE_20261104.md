# Stage 1 direct-goal dual-board DDS — startup root cause and fix (2026-11-04)

Status: **DEVICE_TESTED (bounded scope below) / NOT_PRODUCTION_QUALIFIED**.

| Scope | Result |
| --- | --- |
| Fresh-process A↔B DDS, 3 samples each way, GUID/SN/payload matched by controller | PASS (run m2) |
| Participant delete, runtime destroy, process exit, entry identity restore | PASS (run m2) |
| Fragmented sample: 150000 bytes per direction, transport max 65536, byte-verified and acked | PASS (run m3) |
| Same-name Participant hot rebuild in process (PersistentNativeAnchor), 150000-byte sample both ways after rebuild | PASS (runs m4, m7) |
| Route outage then recovery (15 s IP drop on B) | FAIL before the fix (m5, m6). **PASS with circuit route renewal (m21, m22)** |
| Default path with the renewal library (no outage) | PASS (m23) |
| ROS 2 over this build | NOT_RUN |
| SDK quiet, worker/descendant restoration | UNKNOWN |
| Host build of `integrations/ros2/stock` (MSVC Release, stock-host-d01 Fast DDS 2.14) | BUILDS |
| Host ctest of `integrations/ros2/stock` | **PASS 65/65**, after the 11 stale lazy-TX oracles were updated (see "Host suite reconciliation") |

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

The aggregated socket observation at the end of the window showed:
- `socket_calls=2`: one listener plus the original outgoing socket, so no new Socket/Bind was attempted.
- `send_calls` stopped growing (A 212, B 252), and there was no pump error and no abort.
- The root `out_stopped=1` is **not** evidence of an SDK shutdown. In stock circuit mode the root object
  owns no outgoing socket and marks it closed by design (`PublicSocketBackend.cpp:369`). Per-circuit
  OnShutdown and its reason were not captured.

Likely cause, not yet proven per circuit: in the stock domain-circuit mode, a circuit socket that
DSoftBus stopped during the outage is never renewed, and later sends stop without surfacing an error. The existing renewal path
(`ProviderAdapter` AdvanceHostReaccept / ResetBoundedConnection) is enabled only in the
default-off bounded-reaccept experiment, and `Runtime` rejects that experiment together with the stock/persistent modes.
**Status: route-down re-accept NOT IMPLEMENTED for this path, DEVICE-confirmed gap.**

Evidence: `E:/Codex-FastDDS-stage1-directdds-manual-m4…m7-20261104/` (driver logs, board tars, scripts, overlay source).

## Circuit route renewal: transport fix and device runs m21–m23

**Observed cause (device trace, m8).** About 15 s after the IP drop starts, DSoftBus delivers
`OnShutdown(reason=12, SHUTDOWN_REASON_LINK_DOWN)` for both the accepted socket and the outgoing
circuit socket on each board. Before the fix, nothing reacted:
- **No pump wake-up.** Nothing woke the sleeping pump: the shutdown signals no queue work, and sends to the
  stopped lane are rejected before they queue any.
- **No pump path.** No pump path drove circuit adapters. In owned/persistent assemblies, the runtime-level
  `PumpReceive` returns before the domain branch.
- **Owner never closable.** A native shutdown sets no close intent, so the dead owner could never settle.
- **Re-accept refused.** `CircuitNegotiate` refused a new incoming socket while the circuit's old incoming slot existed.

**Fix (transport, Socket API only).** Changed files: `src/HostProviderLoop.cpp`, `src/ProviderAdapter.cpp`,
`src/PublicSocketBackend.cpp` and the two headers.
- **Per-circuit renewal mode.** Each domain circuit enables the existing bounded replacement owner
  algorithm (`bounded_reaccept_host`, `allow_closed_dial_retry`) in its own config. The root config is unchanged.
- **Driven from both pumps.** `HostProviderLoop` calls `ProviderAdapter::AdvanceCircuitRenewal()` at the top of
  `PumpSend` and `PumpReceive`, before view gating. While a renewal is pending, `WaitForWork` sleeps at most 250 ms.
  A native stop on a circuit lane calls `NotifyWork`.
- **Accepted lane.** The adapter requests close on the dead owner, waits for it to settle, closes the accepted record,
  and resets the parent's incoming slot and family ID (`ResetBoundedConnection(Server)`, circuit branch).
  The peer's next dial is then admitted.
- **Dial lane.** The adapter requests close and waits for the owner to settle. It then waits for a gate: the configured
  peer network ID must be in `GetAllNodeDeviceInfo` continuously for 1 s. Next it resets the child socket
  (`ResetBoundedConnection(Client)`, circuit branch) and rebuilds the dial binding through `InitializeCircuitOutgoing`
  (`Socket` + `Bind` + `Publish`). The old binding is released only after the replacement joins: the shared record
  seals when its last owner leaves.
- **Probe events.** Probe events `CircuitRenew_*` and `CircuitOutgoing_*` go through the existing `runtime_probe::TraceEvent` hook.

**Runner (test only).** In the opt-in route window, nonzero `SendBytes` returns are counted (`send_failures`)
instead of aborting, because sends during the outage are expected to fail. Outside the window the strict policy is unchanged.

**Device results.** Transport library SHA256 `bc8e17edc8385ed36e2b5b3a7f1507103487474548add76fd088166186317b5e`
(m22, m23; m21 used the same code before a cleanup-only refactor plus a debug print), runner `e9fda2a67fe0effc…`.

| Run | Scenario | A | B |
| --- | --- | --- | --- |
| m21 | 15 s IP drop | `recovered=1`, 157 received, `max_peer=225`, max gap 16.8 s, new outgoing socket 4 and new accepted socket 5 | same |
| m22 | 15 s IP drop, repeat | `recovered=1`, 155 received, `max_peer=225`, max gap 17.2 s | same |
| m23 | no outage, default path | 3/3 normal samples, fragment and hot rebuild PASS | same |

In every run each board also reported status 0, phase 4, 3/3 writes/takes, destroy 0, exit status 0 and
restored_entry 1, and the controller reported `ROOT_MATCH_GUID_SN_PAYLOAD_ACK_DONE_TEARDOWN=1` with exit 0.
No processes or iptables rules remained on either board.

**Host.** `integrations/ros2/stock` builds. ctest is 54/65: the new case passes, and the 11 failures are the
**identical** pre-existing ones.

New case `stock_transport.circuit.route-renewal` (`tests/ros2_host/stock_transport/domain_circuit_tests.cpp`, Fake Socket only):
- The test pairs stock Participants and exchanges traffic, then takes the peer node offline in the fake node directory.
- It ends all four circuit connections with `OnShutdown(SHUTDOWN_REASON_LINK_DOWN)` through the new `public_socket_fake::LinkDown`.
- It asserts that no `Socket`/`Bind` happens while the peer is offline.
- It brings the peer online and requires exactly one fresh `Socket` and `Bind` per side, each with a new SDK ID.
- It then requires two DDS round trips on the same Participants and a clean close with zero residue.

Mutation check: with the two `AdvanceCircuitRenewal()` pump calls removed, the case fails with
"circuit dial lane was not renewed after the peer came back online".

**Limits.**
- One outage per run was tested (two separate runs).
- Renewal is tested for a single domain circuit only.
- If the redial reports NoWork after the old binding was released, the record may already be sealed.
  `renewal_failed` latches on any other error, and there is no retry backoff beyond the 250 ms poll.
- The owner table's seen-ID tombstones reject an SDK socket ID that gets reused. The SDK issued new IDs (4, 5) here.
- SDK quiet and drain remain unproven.

Evidence: `E:/Codex-FastDDS-stage1-directdds-manual-m8…m23-20261104/`. m22 contains the scripts, the overlay source, `TRANSPORT_LIB.sha256` and the board tars.

## Host suite reconciliation (11 stale oracles)

All 11 historical failures had one cause. On 2026-10-26 the tree adopted the reviewed
**lazy-TX contract**: a view's outgoing circuit is dialed only by its first send after genuine
Ready, and a receive-only peer never dials. The STATUS record of that day says the revised suite "is not proof that every
unchanged historical oracle passes". These 11 tests still asserted the earlier eager dial at
transport creation or domain confirmation, which last passed on 2026-10-24 (56/56).

The tests were updated to the current contract with exact expectations. None was weakened to "any":
- **Bootstrap `lifecycle` and `defaults-changed`.** After the Factory Participant's first send: live=4, binds A=1, B=0.
  The raw receive-only peer B never dials.
- **Work `pair`, `stop`, `stop-inflight`, `factory` and `factory-held-native`.**
  - After raw confirmation: live=2 with binds 0,0, or 4 with binds 1,0 in the Factory modes.
  - Stop and inflight keep exactly those counts.
  - Factory terminal state: root owners 2, circuit owners 1. Because B never dialed, the circuit's accepted entry
    is now checked as *unused/Empty* rather than Closed. A's terminated socket count is 2.
- **`circuit.raw`, `default_domain.raw` and `normal_control.await-senders`.** The view replacement still must cause
  zero Shutdowns and zero listener changes. The only allowed native delta is exactly one first-TX Socket+Bind on the
  side whose first send came after the snapshot.
- **`default_domain.startup-failure`.**
  - RX now opens with no outgoing native work.
  - The injected outgoing Socket failure (-7654) is consumed by the first TX after genuine domain-0 Ready.
  - It must be observed at the real API boundary, refuse that TX, stay sticky (a retry makes no new Socket call),
    and leave the listener intact.
  - The original failed-close and circuit snapshot checks are unchanged.

No production code changed in this step. Result: stock ctest **65/65 PASS**, including `circuit.route-renewal`.
