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
| Fast DDS QoS / keyed matrix, 8 cases both directions (see "QoS and keyed-topic matrix") | PASS 8/8 per side (m27, m28, m32, m33, m35, m37); content filter SKIPPED |
| Two route outages in one process | PASS (m29, m34, m36, m38) |
| Peer process SIGKILL mid-run, then a fresh run | Clean teardown on the survivor (m31); next run PASS (m32) |
| Board B reboot, then a fresh full run | PASS (m33) |
| Merged into the 2334b68 main line (`claude/stage1-merge-20261104`) | Host stock 66/66, root 131/133 (2 pre-existing); device m35–m38 PASS |
| Fast DDS feature matrix: 15 cases both directions (see "Fast DDS feature matrix") | PASS 15/15 per side, three consecutive runs (m56–m58) and combined with the QoS matrix (m59); 10 features NOT_APPLICABLE (unimplemented in 2.14, or not built on the board) |
| Fixes for the feature-matrix findings (see "Fixes for the feature-matrix findings"); matrix now 18 cases | PASS 18/18 per side with the QoS matrix 8/8, three consecutive runs (m83–m85); route outage recovered (m86, m87); plain baseline (m88) |
| Stage 1 closure: remaining review items fixed; TypeLookup and peer process restart added (see "Stage 1 closure"); matrix now 20 cases | PASS 20/20 per side with the QoS matrix 8/8, three consecutive runs (m99–m101); route outage recovered (m102, m103); plain baseline (m104) |
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

## QoS and keyed-topic matrix (m27–m37)

Opt-in runner block (`QOS_MATRIX` marker; test code only, `tests/stage1_native_successor/consumer_run.cpp`,
`keyed_sample.hpp`). Each case runs in both directions across the two boards, with a barrier topic between cases.

| Case | What is asserted |
| --- | --- |
| keyed dispose/unregister | 3 instances, 2 samples each; dispose and no-writers instance states seen (`autodispose_unregistered_instances=false`) |
| TRANSIENT_LOCAL late joiner | reader created after 5 writes receives all 5 in order |
| KEEP_ALL + RELIABLE burst | 300 samples received in order and acknowledged |
| DEADLINE 300 ms | requested and offered deadline-missed counts rise after the writer stops |
| LIVELINESS AUTOMATIC 1 s | alive seen, then not-alive after the remote writer is deleted |
| OWNERSHIP EXCLUSIVE | only the stronger established owner (strength 20) is delivered |
| LIFESPAN 2 s | an expired batch is not delivered; fresh samples are |
| PARTITIONS | same partition matches and delivers; a different partition neither matches nor delivers |
| Content-filtered topic | SKIPPED: the test types carry no type object |

Result: `QOS_MATRIX summary all_pass=1` on both sides in m27, m28, m32, m33, m35 (merged library) and m37 (review-fixed library).

## Robustness runs

- **Repeated outage.** The runner window blocks the peer IP twice (15 s each) in one 60 s window. Both outages
  recover, with one successful circuit redial per outage per side (`CircuitRenew_dial_result value=0` twice). Runs:
  m29, m34, m36 (merged), m38 (review-fixed: `recovered=1 outages=2 long_gaps=2 max_peer=300`, 165 received per side).
- **Peer process kill.** m31: B's runner is sent SIGKILL during the window. A's renewal attempts fail and back off (1–8 s).
  A then tears down cleanly. Because the fault from a failed attempt is non-sticky, `Close` is clean. The next fresh run (m32) passes in full.
- **Board reboot.** m33: board B is rebooted. A fresh full run, including the QoS matrix, then passes.
- **Not covered.** A restart of the peer process *inside* one live A process is not exercised. The harness binds admission to the PID.

## Main-line merge and independent gate

`claude/stage1-merge-20261104` merges this branch into 2334b68 (merge `b581d73`).
- **Conflicts.** Both sides were kept. HEAD's site-tagged faults, send refusal and shutdown diagnostics remain alongside the renewal code.
- **Independent tester: commit gate PASS.**
  - Stock suite: 66/66.
  - The two new circuit cases passed 3/3 repeats.
  - Root suite: 131/133. `dsoftbus.native_owner_model` and `dsoftbus.process_receive` time out or fail identically on clean 2334b68,
    so they are pre-existing and environment-sensitive.
  - `public_socket_executor.flow` fails only on the baseline: a GBK decode error.
- **Independent review: no blocking findings.** Fixed after review (`src/ProviderAdapter.cpp`, `src/HostProviderLoop.cpp`):
  - **P2: renewal attempt not pinned against `Close()`.** The attempt is now held as an adapter control (`++controls`)
    from the moment `control_gate` is still held until `binding`/`retained_member` are no longer touched. A second pump
    cannot start a concurrent attempt.
  - **P3: missing pump wake-up.** Circuit lanes ended through `Error`/`Stop` now wake the pump, as `NativeStop` already did.
  - **P3: per-pass overhead.** The pumps call `AdvanceCircuitRenewal` only when `CircuitRenewalPending()`.
  - **After the fixes:** stock 66/66, root 131/133 (same 2 pre-existing). Device m37 and m38 used library SHA256
    `2270588ffc2b950e307604c0ab0433b2b47716721beb102b468b59122a9cecd0` and runner `fed960cb…` (merged headers).
- **Open, non-blocking.** These are recorded as known limits:
  - A renewal `Bind` runs on the pump thread and can stall the other circuit for up to the SDK's bind time. The deadline is 10 s.
  - Fault suppression during an attempt covers all adapter faults.
  - A failed circuit reset latches `renewal_failed`.
  - `PeerOnline` only looks at the first 32 nodes.
  - Renewal does not take `domain_setup` serialization.
- **GitNexus.** `detect_changes` cannot run here. The index covers only the main checkout, not worktrees, so this is a recorded gate exception.

Evidence: `E:/Codex-FastDDS-stage1-directdds-manual-m27…m38-20261104/`, tester logs `E:/tester_stock_1104`, `E:/tester_root_1104`.

## Fast DDS feature matrix (m39–m59)

The opt-in runner block (marker file `TMPDIR/feature_matrix`, in `tests/stage1_native_successor/consumer_run.cpp`; new type in `large_sample.hpp`) is test code only. The runner is symmetric: both boards run every case and synchronize at a barrier between cases. The board Fast DDS is 2.14.5, with `SECURITY=OFF`, `SQLITE3=ON` and no `FASTDDS_STATISTICS`.

**Final runs.**
- Runner `0e6cd141…` (after the independent review fixes below) and transport library `2270588f…`.
- Three consecutive full passes on both boards: m63, m64 and m65. Each had 15/15 cases, controller exit 0 and status 0.
- m65 also ran the QoS matrix (8/8) in the same process.
- The pre-review runner passed in the same way in m56–m59.

| Case | Asserted on both boards |
| --- | --- |
| Second Participant, same Runtime and domain | It matches its peer and receives 3/3 samples while the main Participant stays live |
| Discovery user/topic/group data | The peer's `user_data`, `topic_data` and `group_data` all arrive through participant and publisher discovery |
| ASYNCHRONOUS publish + flow controller (64 KiB / 100 ms) | 6 × 150000-byte samples arrive byte-verified and acknowledged. Write-to-ack takes ≥ 1 s (observed 1.7–3.0 s), so the rate limit took effect |
| TRANSIENT durability, SQLite persistence | A writer writes 3 samples, is deleted and is recreated with the same persistence GUID, then writes 2 more. A late reader receives 1..5 in order |
| ContentFilteredTopic (DDS-SQL, DynamicType with TypeObject) | `n > %0 AND n < %1` with 3,8 yields exactly 4,5,6,7. After `set_expression_parameters(10,13)` it yields exactly 11,12 |
| BEST_EFFORT + KEEP_LAST | 50 samples at 20 ms intervals: at least 45 received, in order, no duplicates (observed 50/50) |
| Reader KEEP_LAST depth 1 | Of 10 acknowledged samples, the reader keeps exactly 1, the newest |
| Reader resource limits (`max_samples` 5), KEEP_ALL RELIABLE | Exactly 5 samples are buffered before take, then all 20 arrive in order and are acknowledged (back-pressure; writer heartbeat 100 ms) |
| Request/reply | 3 requests per side. Each reply carries `related_sample_identity` equal to the matching request's identity |
| WaitSet + StatusCondition + ReadCondition | The wait wakes on DATA_AVAILABLE or NOT_READ. The sample is NOT_READ / NEW / ALIVE. After `read` the condition clears and the unread count is 0 |
| RELIABLE with positive ACKs disabled | 20/20 in order. Writes are paced at 10 ms (see limits) |
| 1 MiB sample | 1048576 bytes, byte-verified and acknowledged |
| 20 topics at once | All 20 writer/reader pairs match and deliver |
| Second DDS domain (domain+1) concurrently | Uses a second Runtime with its own pump, because DSoftBus service names encode the domain. 3/3 samples are delivered and acknowledged, and the second Runtime closes cleanly while the main domain continues |
| Discovery Server | Board A runs a SERVER and board B a CLIENT that knows only the server prefix and locator. The effective role (and the server's GUID prefix) is checked on the created Participant. Both match, and 3/3 samples are delivered each way |

**Reported as not applicable.**

| Feature | Reason |
| --- | --- |
| TimeBasedFilter, DestinationOrder, Presentation, LatencyBudget, TransportPriority, DurabilityService | Not implemented in Fast DDS 2.14; `QosPolicies.hpp` marks them "not implemented in this version" |
| DDS Security | The board Fast DDS is built with `SECURITY=OFF` |
| Statistics | The board Fast DDS is built without `FASTDDS_STATISTICS` |
| Data sharing, SHM transport, zero-copy loans, intraprocess | Same-host only, so not a network-transport path |

Changing the board's Fast DDS build is outside the "do not modify the ROS 2 → Fast DDS path" rule.

**Findings and limits from these runs.**
- **Fast DDS 2.14.5 content-filter crash (not the transport).**
  - In m43, board B crashed (SIGSEGV) in `DDSSQLFilter::DDSFilterFactory::delete_content_filter`. The receive thread was removing a remote filtered reader while the user thread deleted the local CFT, and the factory's expression pool is not synchronized.
  - The test staggers local CFT create, update and delete between the boards.
  - Applications that change CFTs while remote filtered readers come and go are exposed to this upstream race.
- **At most two Participants per Runtime** (fixed later: descriptor `max_participants`, up to 4; see below). A Runtime serves at most two concurrent Participants (`HostProviderLoop` views capacity 2). A third Participant on the same Runtime does not join (m52), so the test deletes the second one first. Another domain needs another Runtime.
- **Discovery Server needs the initial peer locator.** DS Participants must keep the peer locator in `initialPeersList`. The transport confirms the DDS domain while configuring initial peers. Without it the views stay in Await and never match (m54).
- **Burst loss with positive ACKs disabled.** Unpaced 20-sample bursts lost the last 4 samples on one side in some runs (m40, m42, m48).
  - The transport queues are bounded: send 32 records / 256 KiB per peer by default, configurable through `send_limits`; receive 32 records / 256 KiB per local, fixed. Like UDP, overflow is dropped.
  - RELIABLE with normal ACKs recovers. With positive ACKs disabled the writer forgets samples after the keep duration, so a lost tail is not repaired.
  - Raising `send_limits` to 512 records / 8 MiB removed the loss in 3/3 runs (m44–m46). At 256 records / 2 MiB it still occurred once (m48), so the send queue is not the only factor.
  - All runtimes on one native table share a process-wide ceiling of 16 MiB / 2048 records. Per-runtime `send_limits` must leave room for the second Participant: with 16 MiB global it did not join (m44–m46).
- **Not run here.** Static EDP, TypeLookup service (remote type discovery), DataWriter loans, and a peer process restart within one live process. Static EDP and loans were added later (see below).

Evidence: `E:/Codex-FastDDS-stage1-directdds-manual-m39…m59-20261104/`. Runner builds: `E:/Codex-FastDDS-directgoal-feature-20261104` (final) and `E:/Codex-FastDDS-directgoal-burstdiag-20261104` (diagnostic, unpaced burst and raised `send_limits`).

**Independent review of the test code.** The review reported one P0 and several P2/P3 findings.
- **P0, gate deadlock in case 13: not confirmed by the device runs.** The claim was that the second Runtime's pre-connect gate would block `MakeStockDescriptor`. Case 13 passed in m41, m49–m51 and m55–m65 with `listener_gate=0`, so on the device the gate is not entered before Participant creation.
- **Fixed:**
  - Equal barrier counts when the domain+1 Participant is missing.
  - A failed second-Runtime teardown now leaks the Runtime instead of destroying it while in use.
  - 100 ms heartbeat in the resource-limits case, with exactly 5 samples required.
  - The Discovery Server prefix parse and effective role are asserted.
  - The fixed `main_participant_live` string was replaced by a real check.
- **The fix pass introduced a defect.** A `//` comment hid the second Runtime's factory assignment and crashed m60–m62 (SIGSEGV in `Second::teardown`). It was corrected before m63–m65.
- **Open (P3):**
  - The flow-controller timing bound relies on the link being much faster than the 64 KiB/100 ms limit. 1 MiB is observed in about 35 ms.
  - The no-positive-ACK case checks delivery only.
  - A failing run's teardown is not staggered for CFT.

## Fixes for the feature-matrix findings (m66–m88)

Transport changes (`src/ProviderAdapter.cpp`, `src/HostProviderLoop.cpp`, `src/DSoftBusTransport.cpp`,
`DSoftBusTransportDescriptor.hpp`). Defaults are unchanged, so every existing contract and host test keeps
its behaviour; the new capabilities are opt-in descriptor fields.

| Finding | Fix | Evidence |
| --- | --- | --- |
| Circuit renewal `Bind` ran on the pump thread and could stall the other circuit for up to 10 s | The redial runs on a renewal worker thread. The attempt stays pinned as an adapter control, so `Close()` waits for it and then joins the thread. A pump never joins a worker that is still cleaning up (`renewal_worker_busy`); the worker wakes the pump when it ends | Host `circuit.route-renewal` and `circuit.renewal-bind-failure` pass (stock 66/66, three repeats). Device route outage (15 s DROP on B): `CircuitRenew_dial_result=0`, `recovered=1`, `pump_failed=0` on both sides (m82, m86, m87) |
| At most two Participants per Runtime | Descriptor `max_participants` (1..4, default 2), set by the descriptor that starts the runtime | `four_participants_one_runtime`: four Participants on one Runtime, 3/3 samples each way on two extra topics |
| Discovery Server needed an initial peer, because the domain was confirmed only while configuring initial peers | Descriptor `bind_declared_domain_at_open` (default off): with `expected_domain_id`, a domain-circuit view binds its domain when its first input opens | `discovery_server` now runs with `initial_peers=0` on both the SERVER and the CLIENT |
| Unpaced bursts were dropped by the bounded queues (no-positive-ACK tail loss) | Descriptor `receive_records` / `receive_bytes` (0 keeps 32 records / 256 KiB). The feature runs use receive 256 records / 2 MiB and send 256 records / 2 MiB per peer and globally, which stays inside the 16 MiB / 2048-record process egress ceiling with 7 registrations | `reliable_disable_positive_acks` now writes 20 samples unpaced: 20/20 in order on both sides in m66 and m76–m85 |
| Fast DDS 2.14.5 `DDSFilterFactory` race (CFT ops vs remote filtered-reader removal) | Not fixable in this project: it is inside Fast DDS, which the rules keep unmodified. The test keeps staggering local CFT operations | Unchanged |

New feature cases (test code only):
- `datawriter_loans_plain_type`: `loan_sample()` on a plain bounded type (`plain_sample.hpp`), filled in place and written, 5/5 byte-verified; the reader takes with a `LoanableSequence` and returns the loan. Data sharing is set OFF on both endpoints so the case measures the network path. A plain type must override both `is_plain()` overloads for Fast DDS 2.14 to lend.
- `static_endpoint_discovery_xml`: a Participant with STATIC EDP from an inline `data://` XML (user and entity IDs on both sides). It matches and delivers 3/3 each way with SPDP only.

Test-harness fix: after a side emits `DATA_DONE`, its pump tolerates send failures. The peer may legally tear down from then on, and its closed socket made the next heartbeat `SendBytes` fail; this failed m77 after every case had passed.

**Runs.** Library SHA256 `842c6c95f1b85ddd27a0241a0daab8ae19b37c417c04b186f26a3aef3e72190b`, runner `465261f8…`.
- m83, m84, m85: QoS matrix 8/8 and feature matrix 18/18 per side, controller exit 0, status 0.
- m86, m87: route outage recovered on both sides.
- m88: plain run (no matrix flags): fragmented sample and hot rebuild pass.
- Host: stock 66/66 three repeats; root 131/133 with the same two pre-existing failures (`dsoftbus.native_owner_model`, `dsoftbus.process_receive`). `dsoftbus.service_record_binding.callback-destroy-fails-fast` failed once under `-j 8` and passed 5/5 alone; its code is untouched.

**Still open.**
- TypeLookup service (remote type discovery) and a peer process restart inside one live process are not run.
- Remaining P3 review items: fault suppression during a renewal attempt covers all adapter faults; a failed circuit reset latches `renewal_failed`; `PeerOnline` reads only the first 32 nodes; renewal does not take `domain_setup`.
- Test P3s: the flow-controller timing bound assumes a fast link; the no-positive-ACK case checks delivery only.
- GitNexus `impact` / `detect_changes` cannot run on this worktree (the index covers only the main checkout); recorded gate exception.

Evidence: `E:/Codex-FastDDS-stage1-directdds-manual-m66…m88-20261104/`, runner build `E:/Codex-FastDDS-directgoal-fix2-20261105`, install `E:/Codex-FastDDS-transport-arm-fix2-20261105/install`.

## Stage 1 closure (m89–m104)

Closes every open item recorded above. Branch `claude/stage1-closure-20261105`.

**Transport review items (all fixed).**

| Item | Fix | Evidence |
| --- | --- | --- |
| `PeerOnline` read only the first 32 nodes | It reads every node the SDK returns | New host case `stock_transport.circuit.renewal-many-nodes` (peer listed after 40 other online nodes). It fails with the old 32-node cap and passes with the fix |
| Fault suppression during a renewal attempt covered all adapter faults | Only faults raised on the attempt's own worker thread are scoped to the attempt; a fault on another lane stays a real provider fault | Host circuit cases; device route outage m102/m103 |
| A failed circuit reset latched `renewal_failed` | A failed owner reset is retried with the redial backoff (the failed owner is kept as `pending_reset_owner`); only 5 consecutive reset failures latch | Host `renewal-bind-failure`; device route outage |
| Renewal did not take `domain_setup` | The redial runs only while it holds a try-lock on the root `domain_setup` (`ProviderAdapter::SetCircuitRenewalSerializer`), like the first outgoing setup; a busy lock retries after 50 ms | Host circuit cases; device route outage |

**Previously not run, now device-tested.**
- `type_lookup_remote_type`: each side publishes a DynamicType the peer never builds, with TypeInformation in discovery but no TypeObject (`auto_fill_type_object(false)`). The peer resolves the type with `register_remote_type`, creates a reader from it and reads 3/3 samples with every field checked. Reported path `typelookup_before_register`: the TypeObject was already present when `register_remote_type` ran. It cannot have come from discovery, so it came from the peer's TypeLookup service.
  - Fast DDS 2.14 detail: when the TypeObject is already known, `register_remote_type` registers the type itself, returns `RETCODE_OK` and never calls the callback; the callback is used only on the `RETCODE_NO_DATA` path. A host A/B over plain UDP (`tl_host`) shows the same, so this is Fast DDS behaviour, not the transport.
- `peer_process_restart`: board B runs the peer in a separate OS process (the runner re-executes its own image with `MDDS_RESTART_HELPER`). It exchanges 3/3 samples with board A, is killed with SIGKILL (no teardown), and a fresh process (new PID, new GUID prefix) starts. Board A's process, Runtime and Participant stay live: it sees the peer leave (lease 3 s), renews its circuit, matches the new process and exchanges 3/3 again. The new process exits 0 with its Runtime closed; A's Runtime then closes cleanly with no pump failure.
  - Protocol detail found on the way: the DSoftBus listener appears with the first Participant, and Participant creation blocks in the pre-connect gate. Both sides must create their Participants with the gate closed and open it only after both listeners exist; a first dial to a peer without a listener is not retried.

**Test P3s.**
- Flow controller: the case now also sends the same 6 × 150000 bytes through an unthrottled async writer on the same link and requires the throttled time to be at least 1.2 s and at least twice the unthrottled time (observed 1704 ms vs 17 ms).
- No positive ACKs: delivery is the only observable outcome by design (the writer gets no positive acknowledgements to assert); the case now writes an unpaced burst and requires 20/20 in order.

**Runs.** Library SHA256 `41240aa9b7b1c8e18748b08673a4d9e8694d8e144bb36df6e69b1050c82d5cda`.
- m99, m100, m101: QoS 8/8 and feature 20/20 per side, controller exit 0, status 0.
- m102, m103: route outage recovered on both sides (`CircuitRenew_dial_result=0`).
- m104: plain run (fragmented sample, hot rebuild).
- Host: stock 67/67 three repeats; root 131/133 with the same two pre-existing failures.
- GitNexus now indexes this worktree (`gitnexus analyze --index-only .`); `impact` was run before the edits (risk UNKNOWN/MEDIUM, confirmed by text search) and `detect_changes` before the commit.

**What remains outside Stage 1.**
- The Fast DDS 2.14.5 `DDSFilterFactory` race is inside Fast DDS, which the rules keep unmodified. Applications must not change a ContentFilteredTopic while remote filtered readers come and go; the test staggers those operations.
- Security and statistics are not built into the board's Fast DDS; data sharing, shared memory and intraprocess delivery are same-host only.

Evidence: `E:/Codex-FastDDS-stage1-directdds-manual-m89…m104-20261104/`, host A/B `E:/tl_host_b`.
