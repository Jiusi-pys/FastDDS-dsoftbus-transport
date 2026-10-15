# Windows Host Socket IO diagnostics

## R11 extension (2026-10-15 04:41:11 UTC)

The sections below preserve the R8 implementation and execution record. R11
adds endpoint matching and independent callback/receive/budget reconciliation,
with a fresh final bridge and actual service rerun. See
[Host connection and receive observation](HOST_RECEIVE_OBSERVATION.md) for the
current capture contract, final tests, review repairs and remaining gaps. R8's
27 CTest/29 Python counts and its final pub/sub NOT RUN statement remain
historical facts; the R11 service run does not rerun that pub/sub scenario.


This optional diagnostic records the Windows TCP Host seam used by the ROS 2
transport harness. It does not execute the OpenHarmony DSoftBus SDK. The scoped
patch was independently built, tested and reviewed on published base
`de1cb5d8b0d1d4a38427c80edeb5bf65ad405967` on 2026-10-15. It imports no W1 Runtime,
receive-ledger, security or fragmentation changes.

## Enable and interpret capture

The bridge configuration accepts `host_io_observation=0` or
`host_io_observation=1`. Absence means disabled; other values are rejected.
For the existing pair runner, add `--observe-host-io` to the normal
`tests/ros2_host/run_pair.py` invocation. This writes the enabled configuration
for both sides and requires complete captures. Omitting the switch preserves
the default-off diagnostic setting and does not require capture output.

The process-local ledger reserves at most 16,384 immutable event slots. Each
slot is published with release/acquire synchronization; overflow is counted,
not overwritten. Enabled recording adds no allocation, output or mutex to the
hot path, but can still perturb scheduling. The process-global Host seam
supports one configured lifetime; this patch adds no reset/reconfiguration API.

`HOST_IO_EVENT schema=1` rows carry run, side, PID, thread, observation sequence,
call-bracketing sequence points, logical socket ID, raw Windows handle, local
connection epoch and send ID. Events cover connection installation, send entry,
handle capture and return, prefix/body transfers, receive exit, shutdown
callbacks, local shutdown/close and final thread joins. Sequence values express
process observation order, not elapsed time or kernel linearization. Reservation
order need not equal sequence order; sort by `seq` when comparing events.

Transfer records retain the last request/result, call count, partial-call count,
and immediate `WSAGetLastError()` only when the syscall returns `SOCKET_ERROR`.
`wsa_known=0` means no error value is asserted. Read zero is `EOF`; write zero is
`ZERO_PROGRESS`. Successful syscalls are aggregated per prefix/body transfer,
not individually traced. Connection epochs are local diagnostic identities,
not native SDK epochs. Runtime owner, Pump attribution and cross-process causal
ordering are not observed by this patch.

Export follows resource-complete `CloseLocal()` and `StopLink()` joins. An
incomplete close returns before export. The `HOST_IO_CAPTURE` footer reports
reserved/published/overflow/unpublished counts and `complete`; output or flush
failure also emits `FDDST_HOST_IO_CAPTURE_INCOMPLETE` on stderr. Export failure
does not change bridge business return codes. A missing footer, malformed
identity, overflow, unpublished rows, invalid count/sequence, missing terminal
`LINK_JOIN_RETURN`, or the stderr failure marker makes the enabled runner's
diagnostic verdict fail. Footer `complete=1` alone is insufficient if output
subsequently fails.

The runner's `result.json` separates `business_pass` from the combined `pass`.
An otherwise successful business run can have `business_pass=true` and
`pass=false` when capture is incomplete; the runner exits 1. Complete diagnostic
capture proves record integrity, not transport correctness or absence of
callback rejection. The existing framing, callbacks and transport/close return
semantics remain the business contract.

## Evidence and limits (2026-10-15)

| Scope | Result and boundary |
| --- | --- |
| Implementation / build | PRESENT / PASS; independent fresh MSVC x64 Release `/MD` configure, build and install after review |
| CTest | 27/27 PASS: 14 existing cases plus 3 scripted/ledger cases and 10 actual Windows Socket cases |
| Scripted / ledger | Transfer-result injection, concurrent publication, overflow and real broken-output-pipe handling; not native transport execution |
| Actual Host sockets | EOF, reset, invalid length, local stop and successful send, each capture on/off; owned localhost TCP, not SDK or board communication |
| Python | 29/29 PASS: 7 capture, 15 analyzer, 6 native-log-parser-only, 1 control test |
| Actual final bridge DLL | 7/7 invalid configurations rejected before socket operations |
| Synthetic runner replay | 4/4 PASS: default-off, complete-enabled, missing capture and overflow; zero consumer processes spawned |
| Independent review | Tier 2 complete, no actionable findings; 10 source hashes unchanged |
| Initial ROS Host pair | One separate reliable W2 consumer run: A 5 writes/5 takes, B 5 writes/5 takes; complete A 782/B 787 records; B `rejected_callbacks=1` retained |
| Final ROS Host pair | NOT RUN with final DLL; final tester rehashed/reparsed retained initial same-source logs and cleanup receipt, not a new business run |
| Native / RK3588A / cross-device | N/A for this Windows-only diagnostic patch; DEVICE_TESTED=false; broader product acceptance remains BLOCKED/unverified |

The initial pair used a freshly built W2 consumer and bridge, not W1 binaries.
Its recorded `fake_sdk=1` identifies the Host adapter. No failed SendBytes was
observed in that pair; W2 failed-transfer coverage is scripted. Separate W1
negative fixtures are not W2 validation. Historical A/04 failures were neither
reproduced nor explained here. The outstanding device permission/deployment
gate remains unchanged, and no security or network setting was changed.
Service/action, full QoS, discovery/reconnect, security, large-message and
cross-device product coverage are not promoted by this diagnostic patch.

The canonical final record reports scoped `Commit gate: PASS`; documentation
readiness does not replace Main's graph, identity, remote and pre-commit checks.
Overall ROS 2/DSoftBus product status remains NOT VERIFIED.

## Local evidence and reproducibility

Paths in this section are relative to the original workspace root
`C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`, **not** the W2
worktree. These build artifacts are local evidence, not files supplied by a
fresh clone.

- Frozen 10-file patch: `build/r8-negative-fixtures/publish-implementation/IMPLEMENTATION.json`.
- Initial tester: `build/r8-negative-fixtures/w2-initial-test/INITIAL_TEST_RESULT.json`.
- Independent review: `build/r8-negative-fixtures/w2-review/REVIEW.json`.
- Final tester: `build/r8-negative-fixtures/w2-final-test/FINAL_TEST_RESULT.json`.
- Exact commands, environment, exit codes and timing: the respective `COMMAND_LEDGER.json`, `*.intent.json`, `*.json`, `*.stdout` and `*.stderr` records in the initial/final tester directories.
- Final DLL: `build/r8w2final/p/bin/fastdds_dsoftbus_ros2_host_bridge.dll`, SHA-256 `94a33c8d29801beab43a6f38538cfd23d265e608e091ae04462a5dda6d5107eb`.
- Initial pair command: `build/r8-negative-fixtures/w2-initial-test/publication-reliable-pair.intent.json`; raw output: `build/r8w2initial/publication-reliable-pair/result.json`.

The compatible dependency selection is recorded in
`build/r8-negative-fixtures/w2-final-test/COMPATIBLE_DEPENDENCY_SELECTION.json`:
Fast DDS prefix `build/g2a1/host214-install`, Fast CDR CMake package
`C:/pixi_ws/ros2-windows/Lib/cmake/fastcdr`, ROS runtime prefix
`build/ros2-target/h3/runtime`, Pixi prefix `C:/pixi_ws/.pixi/envs/default`,
and public headers `build/r3ros/softbus-headers`. Fast DDS DLL SHA-256 is
`60408a27139ce59bf9aaa92c080e799d98f1531b7318dfbf0abf7308e7d555a4`;
Fast CDR DLL SHA-256 is
`badcea3777c7a6b23961310dc5857e07c064d94af9babe52d2c097d8842d8c71`.
This dependency has SECURITY=OFF and qualifies only the ordinary Host scope.

To reproduce, use the exact argv and environment in the following records in
order, substituting a new source snapshot and fresh output directories to
preserve evidence. Each intent records `cwd`, environment and timeout; each
matching result records exit status. Do not substitute an arbitrary Fast DDS
prefix or infer ABI compatibility from its version alone.

1. Final `configure-compatible.intent.json`, then `build-compatible.intent.json`:
   Visual Studio 17 2022 x64, Release `/MD`, `BUILD_TESTING=ON`, install target,
   `/m:1 /nr:false`.
2. Final `ctest-final-approved.intent.json`: `ctest --test-dir <fresh-build> -C Release --output-on-failure --output-junit <fresh-report>` with the recorded dependency PATH. Real socket cases need the same authorized owned-localhost execution context; sandbox denial is not a passing test.
3. Final Python intents for `host_io_capture_tests`, `analyzer_tests`,
   `native_analyzer_tests`, `test_control_files`, `bridge-invalid-config` and
   `runner-replay`. The last replay is synthetic and consumes retained logs.
4. For a new ordinary Host business probe, follow initial
   `consumer-configure.intent.json` and `consumer-build.intent.json` against
   that fresh install, then the initial pair intent with fresh output and the
   new consumer/bridge paths. Its recorded mode is `reliable` with
   `--observe-host-io`. This is a recipe, not a claim that the final DLL pair ran.

Retained failures are part of the evidence: the first dependency prefix lacked
W2 factory symbols; sandbox execution denied 10 localhost connections; an
invalid CRT descriptor terminated the original output fixture; its first
correction incorrectly assumed EPIPE. The final fixture uses a valid broken
pipe and checks export failure/stream error. Both fixture corrections preceded
canonical initial testing and independent review. Tester-only include/path and
stdout-versus-stderr evidence parsing mistakes are separately recorded; none
were erased or counted as passing runs. No timeout, expected business outcome
or native gate was relaxed to obtain the scoped result.
