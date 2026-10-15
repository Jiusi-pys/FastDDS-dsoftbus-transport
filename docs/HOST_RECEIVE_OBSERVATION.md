# Windows Host connection and receive observation (R11)

R11 adds bounded diagnostics for the Windows TCP Host adapter: unique peer
connection matching, original callback identity, receive admission, budget
occupancy and release. It preserves the existing Socket, Transport, DDS/RTPS
and QoS behavior. These are Host observations, not DSoftBus SDK identities.
Instrumentation can perturb scheduling; a successful capture is not proof that
every possible transport schedule or ROS feature is correct.

The 22-file candidate based on `41cdcd13a791eb1e32b9a02f0fac8363f3d5def5`
passed independent final scoped testing at 2026-10-15T04:41:11Z after review fixes.
Implementation: PRESENT. Host build and scoped integration: PASS. ARM64 build:
PASS, compile-only. DEVICE_TESTED=false. Native/cross-device communication:
BLOCKED / NOT VERIFIED. Main's graph, pre-commit and publication gates remain
separate from the tester's scoped `Commit gate: PASS`.

## Capture contract

The Windows bridge accepts `host_receive_observation=0` or `1`; absent is off.
Configuration is fixed before Runtime construction for the process lifetime.
Use `--observe-host-receive` on `tests/ros2_host/run_service_acceptance.py` to
enable the receive capture alongside the existing Host IO capture and require
strict connection/receive reconciliation. The existing R8 IO contract and its
historical evidence are in [Host IO diagnostics](HOST_IO_DIAGNOSTICS.md).

The Host CMake runtime exports `FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION=1` to its
consumers and installs `HostReceiveJournal.hpp`. Shared-source hooks and fields
are guarded by that macro. The native target does not enable it; the final
ARM64 build verified this. Mixing macro settings across that Host runtime and
its public consumers is not a supported ABI configuration.

- The 64-slot endpoint ledger records `HOST_CONNECTION_EVENT` at connection
  installation: role, logical socket, local epoch, Windows handle and queried
  local/peer IPv4 endpoints. Queries preserve the prior WSA error. Matching
  requires complete IO and endpoint captures, an install/close lifetime and a
  unique reversed tuple across independent processes. Tuple reuse is UNKNOWN,
  even if local lifetimes do not overlap. Process clocks do not establish
  cross-process ordering.
- The 65,536-slot scalar journal emits `HOST_RX_EVENT`: decisions and stages,
  charges, capacity refusals, queue/take/enter/requeue, and record/capture refunds.
  Budget mutation order is gate-sequenced per scope under the existing budget
  lock. Scope/record/gate identities are diagnostic identities. They do not
  replace runtime identities or retain payloads, native pins or owning handles.
- The independent 8,192-slot `HOST_CALLBACK_EVENT` ledger records entry/return
  around the actual Host OnBytes callback, with original ordinal, socket, epoch,
  handle and size. `HOST_RX_COVERAGE` exports existing Adapter/Binding counters
  after close. These receipts and counters are independent of observer rows.
- Export occurs after Runtime close and Host/pump thread joins. Missing rows,
  overflow, incomplete publication or output failure cannot qualify a capture.
  `FDDST_HOST_RX_CAPTURE_INCOMPLETE` makes the enabled runner fail; export does
  not turn a business failure into success or change business return codes.

`validate_receive_capture` defaults to `full_host`. It requires validated
endpoints, the independent final ingress count, callback receipts, close-time
counters, exact Adapter/Binding decision coverage and branch/refusal contracts.
Original admission identities join all five callback fields to one endpoint.
Deferred lifecycle stages join immutable scope/record identities, not the
thread-local callback that happens to run later. Charges and outcome-specific
stages/refunds must reconcile, including rollback and discard cases.

The qualified full profile is `EXCLUSIVE_BINDING`. Early, retired or shared
binding history that cannot be reconciled fails closed. Explicit `budget_only`
checks only its narrower journal contract and returns `full_host_complete=false`;
there is no automatic fallback. Older captures lack independent receipts and
cannot be upgraded to full coverage by inventing rows or totals. No payload or
DATA_FRAG identity is captured, so missing-fragment causality is NOT OBSERVED.

## Independent execution evidence

The final Host environment was VS2022 x64 Release `/MD`, Fast DDS 2.14.5 with
SECURITY=OFF and STATISTICS=OFF. Actual socket/ROS runs used the authorized
owned-localhost execution context. No firewall, device or security setting was
changed. All counts below refer to final execution unless labelled otherwise.

| Check | Evidence and limits |
| --- | --- |
| Fresh Host configure/build/install | PASS; 31/31 CTest; installed C++17 public consumer compile/run PASS |
| Python | 60/60 PASS: 31 receive, 7 IO, 15 Host analyzer, 6 native-log analyzer models, 1 control; analyzer models are not native execution |
| Macro-off core | First run 131/132 PASS, exit 8: `dsoftbus.public_socket_executor.flow` hit its original 90 s limit at 90.01 s. One original-limit rerun PASS in 30.04 s; see retained failure below |
| OpenHarmony SDK23 ARM64 | Compile/link and ELF AArch64 PASS, Host macro absent; no deployment or native communication |
| Directed actual TCP | Positive, peer-close and local-close runs PASS; all six child processes exited 0, no forced kill, ports exclusively rebound |
| Control formats | LF accepted; wrong identity, extra line and trailing junk rejected before successful business completion. Each negative driver exited 1 and force-stopped its remaining B process; owned processes ended and ports rebound. These are expected rejection checks, not graceful shutdown proofs |
| Actual capacity component | Run `f5abca8754c915d7`: single-process TCP through PublicSocketBackend/Runtime, 6 callbacks, 5 charges/refunds, 1 LocalBytes refusal; 73 events, 12 decisions, 35 stages; full_host PASS |
| Actual ROS service | Run `69350e7e061047a5`: two processes, 14 requests and response calls, 12 accepted responses, 2 timeout removals, 2 recreated clients; both exits 0; two connection pairs matched; full_host PASS |
| Strict raw-log negatives | 21 new-actual mutations plus 2 targeted Binding capacity-refusal erasures rejected. Separately: 7 connection mutations, 1 tuple-reuse mutation, 5 capacity mutations, 4 business-log mutations, and 3 old captures rejected without new receipts. Mutations validate analyzers, not injected runtime failures |

The capacity fixture holds four 65,500-byte opaque components (262,000 bytes)
against the default 262,144-byte local budget, observes the fifth refusal,
pumps/releases the four and explicitly resends the fifth. That resend is a new
accepted record; it is not transport automatic retry. Five complete payloads
were delivered and released; the process exited 0 and its port was released.
It is neither a ROS scenario nor native DSoftBus communication.

The service run used unchanged bounded R10 business semantics with new
diagnostics. A/B had 144/143 callbacks and charges/refunds, 288/286 decisions,
1,008/1,001 stages and 2,016/2,002 receive events; each had zero refusals. IO
records were 1,028/1,032. Actual module bindings and independent callback joins
were checked; both ports were released. The removed-future broken-promise result
does not independently prove RMW late-response discard. See
[service acceptance](ROS2_SERVICE_ACCEPTANCE.md) for the business boundary.

Final bridge:
`build/r11diagfinalfix/p/bin/fastdds_dsoftbus_ros2_host_bridge.dll`, SHA-256
`cef2b894dd397b46f0edb819f8bedc93df6cea326d8645a0006fd57e1440c572`.
Final service executable SHA-256:
`660f6c6e055dff2228de4076ae33cb70295a7a01567294848709d92c8b62a62b`.
The sealed final evidence manifest contains 374 files. Before/after checks
matched 22 candidate files, 33,134 Host dependencies, 28,326 target dependencies,
4,259 core source files and 11 selected tool executables. The complete Windows
OS/MSVC/Windows SDK tree was not exhaustively sealed.

## Review, retained failures and remaining gaps

Independent review found two P2 acceptance holes: R11-D1 allowed erased
DECISION/STAGE coverage to pass; R11-D2 did not join original callback identity
to its connection. Independent receipts/counters, exact decision/lifecycle
coverage and complete identity joins closed both at code-review level. The
fresh final actual captures and strict mutations above provide subsequent
execution evidence. Historical logs were retained and never supplied fabricated
receipts. A later test-only `small` variable collided with a Windows RPC macro;
its minimal rename to `capacity_ledger` received separate review before the
fresh build/tests. The failed compiler run remains in the evidence.

The core timeout is UNATTRIBUTED. Its single rerun kept the original 90-second
limit, but paused concurrent build/hash work and used `require_escalated`
instead of the initial default sandbox. Two conditions changed; neither is
proved causal. It is inaccurate to report an initial clean 132/132 run. A native
tester guard also stopped before configure after detecting the test-only edit;
unchanged core/native source inputs were independently established before the
successful target build. Earlier initial compile, fixture control/newline and
deployment-prefix failures remain linked from the initial tester records.

Actual paused-callback overtaking and reconnect/local-budget collision have not
been qualified; synthetic parser coverage does not fill that gap. R11 did not
rerun historical A/04. The qualified current profile (2.14.5, security/statistics
OFF) cannot substitute as equivalent for original 2.14.7 with security/statistics
ON. Version difference alone does not prove an ABI break. A separately isolated
and qualified original profile is required before any bounded historical run;
the fragment-identity gap also remains. No historical root cause is claimed.

Device permission remains last-known DENIED on both RK3588A boards from the
narrow query at 2026-10-14T21:51:07Z; this round made no fresh device calls.
Owner-supported deployment/permission recovery is still required. V1-05 service
and V1-07 parameters remain PARTIAL Host scenarios. The parameter stage was
published separately as `41cdcd13a791eb1e32b9a02f0fac8363f3d5def5`; it used the
R8 bridge and is not retested or upgraded here. The frozen capability denominator
remains 30, aggregate completion remains unknown, and overall ROS 2/DSoftBus is
NOT VERIFIED. The action outline is PLANNED only.

## Local evidence and reproduction

All following paths are relative to the original workspace root
`C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`, not W2. They are
local build evidence and are not supplied by a fresh clone.

- Candidate: `build/r11-observation/diagnostic-review-fixes/SOURCE_FREEZE_BUILD_FIX.json`.
- Initial: `build/r11-observation/diagnostic-initial-test-receive-fixed/INITIAL_TEST_RESULT.json`, which links prior corrected/extended attempts.
- Review: `build/r11-observation/diagnostic-review/REVIEW.json`, `REVIEW_FIX_CLOSURE.json`, `REVIEW_BUILD_FIX_CLOSURE.json`.
- Canonical final: `build/r11-observation/diagnostic-final-test/FINAL_TEST_RESULT.json`, `SOURCE_BINDINGS.json`, `EVIDENCE_MANIFEST.json` and `SELECTED_TOOLCHAIN_HASHES.json`.
- Actual final Host commands/results: `build/r11-observation/diagnostic-final-test-corrected/`; raw captures: `build/r11diagfinalfix/`.
- Historical gate: `build/r11-observation/HISTORICAL_COMPATIBILITY.json`; future planning only: `NEXT_ACTION_PLAN.md` in the same directory.

Reproduce with new source/build/output directories using the exact argv,
environment, timeout and matching exit/stdout/stderr records in these intents:

1. Corrected-final `configure.intent.json`, `build.intent.json`,
   `install.intent.json`, `ctest.intent.json`, then `consumer-configure`,
   `consumer-build` and `consumer-run` intents. Use the recorded Fast DDS/Fast CDR
   prefixes and `/m:1 /nr:false`; do not substitute unqualified binary families.
2. Corrected-final `fixture-positive`, `fixture-peer-close`, `fixture-local-close`,
   `receive-socket` and `service-pair` intents. The last invokes
   `run_service_acceptance.py --observe-host-receive` against the fresh bridge.
3. Canonical-final Python test intents and corrected-final negative-check intents;
   keep strict endpoint/ingress context and retain failed records. Native-log
   analyzer tests remain models. Preserve installed Windows PowerShell module
   paths for the core source probes.
4. Canonical-final core and native configure/build/CTest/ELF intents. Preserve the
   first core timeout evidence and its distinct rerun environment. Neither target
   compilation nor these Host tests replace the outstanding board acceptance.

These instructions reference executed commands; they were not run as another
business window during documentation sync. Documentation readiness does not
assert a future diagnostic commit, push or CI result.
