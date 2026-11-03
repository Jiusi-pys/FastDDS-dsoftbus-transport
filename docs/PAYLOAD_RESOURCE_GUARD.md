# Payload Resource Guard

## Status

USER_READER_PAYLOAD_LIMIT_HOST_VERIFIED (limited scope), PAYLOAD_POOL_BUDGET_HOST_VERIFIED (limited scope), API23_PAYLOAD_GUARD_BUILD_VERIFIED (compile/link only), SECURE_FRAGMENTED_BE_SCENE_HOST_VERIFIED (current Candidate 3 host scope), SEND_REFUSAL_FAILED_SCENE_CHECKPOINT_HISTORICAL_CAUSE_UNRESOLVED, DEVICE_RESOURCE_GUARD_NOT_RUN, NOT_PRODUCTION_QUALIFIED. Production M/B/N: NEEDS_PRODUCT_VALUE. The profile TEST_ONLY_BOUNDED is the only accepted profile and is never production approval; configs without the four keys are explicitly UNPROTECTED_LEGACY. The bounded send-refusal and last-sample evidence are recorded in [SEND_REFUSAL_FAILED_SCENE_CHECKPOINT.md](SEND_REFUSAL_FAILED_SCENE_CHECKPOINT.md); they do not change payload-guard qualification.

## What It Is

A project-owned IPayloadPool injected at the Fast DDS 2.14.7 SubscriberImpl public optional pool argument, before topic reference and reader construction. Applied by apply_payload_resource_guard.py from prepare_fastdds_source.py for version 2.14.7 only. No RTPS/RMW/rcl/SDK change. Unmodified ROS (rmw_fastrtps) readers get it through that seam.

## Contract and Units

Configuration keys: payload_guard_profile, payload_max_sample_bytes (M), payload_pool_budget_bytes (B), payload_max_allocations (N), in the same required configuration file.

M = max serialized sample bytes including the 4-byte CDR header.
B = sum of physical payload buffer capacity charged; each allocation charged as request+6 hidden tail bytes for fragment tail headroom.
N = live physical payload records (including incomplete fragment reassembly buffers; extra same-owner references do not consume another record).

One shared budget object across all per-reader pools in the process for one latched configuration. Not a whole-process memory bound. No free-buffer payload cache so retained equals live. Check, reservation, malloc and publication happen under one lock. Size checked before any allocation. Foreign custom pool or config drift/removal after latching makes reader creation fail closed.

## Coverage

**Protected (DDS readers created through Subscriber seam):** ROS graph/discovery-info, parameter services, rt topics in the host runs. Observed in the tester-canon-2 boundary scene: process A created and bound 9 readers, process B created and bound 11 readers (the two user data readers `rt/.../reliable/from_A` and `rt/.../best_effort/from_A` among B's), with zero bind rejections. Each process also logged one failed first creation of the `ros_discovery_info` reader (an existing Fast DDS unique-flow warning, present in unguarded runs too); the retried reader was created and bound. All readers were created through the seam by unmodified rclcpp/rmw.

**NOT covered:** Builtin discovery/security pools, decryption temporary buffers before reader, CDR object/sequence allocation, CacheChange metadata (separate finite QoS limits), SDK-owned memory, DataSharing/intraprocess (rejected by existing endpoint guard, not by this pool).

## Evidence

**Final canonical tester run build/payload-resource-guard-20261010/tester-canon-2**, Windows MSVC + WSL g++, TEST_ONLY values per scene (boundary and huge: M=4096, B=65536, N=16; count: M=4096, B=1048576, N=2; budget: M=4096, B=8304, N=64):

- Component tests: plain pool PASS, ASan pool PASS, unit race test PASS (max 3 payloads held simultaneously at a budget that fits exactly 3, eight reader wrappers on one budget, no double spend)
- Preflight: PASS
- Maintenance candidate tests and patch delivery checks for 2.14.5 and 2.14.7: PASS
- Boundary test: PASS, payload of exactly M (4096) delivered, M+4 rejected before allocation
- Huge test: PASS, declared size 2147483647 rejected (charge 0)
- Count test: PASS, records never above N=2
- Budget test: PASS, retained bytes never above B=8304 (this scene's own limits: M=4096, B=8304, N=64); peak observed 8204
- Guarded closure suite 4/4 PASS: secure dual pair 20 writes/20 takes, unauthenticated peer, mixed dependency refused, real fragmented sample with M=98304, B=2097152, N=64
- Mutant tests (guard checks removed, observers kept): all four scenes FAIL as required. The count and budget mutants reach 3 records and 12306 bytes. In the huge mutant a host safety limiter blocked the allocation, so that row shows only that the product rejection is absent.
- Fix validation (this round's §7 list, three declared rounds on the fixed bridge, every side exiting 0 with a released close and `close_contract.A/B` both ok): `huge`/`boundary`/`count`/`budget` PASS in `runs/f7-f7a-*`, `runs/f7-f7b-*` and `runs/f7-f7c-*`; guarded closure suite `runs/f7a-closure`; injected counterexample `runs/f7a-green-inject` (incomplete verdict reported as a failure, both sides exit 0, no fatal event and no dump) against pre-fix `runs/red-inject-1` (exit 3221226505, FAST_FAIL subcode 7, dump captured).
- Scene-runner policy change, deliberate and not a regression: `passed` now also requires each side's `close_contract.ok`, so *any* reported execution error fails the scene — including a host-fixture send error such as `c3-bnd-5`'s `-609`, which the old harness recorded as `passed:true`. The scenes' recorded pass rate drops accordingly; a scene that dies before printing its joined local close can no longer be reported as a pass.

## Known Issues

(a) RESOLVED — one earlier canonical run (tester-canon-1) FAILED: in the guarded huge scene the reader process aborted with exit 0xC0000409 after its own checks passed, with FDDST_RUNTIME_CLOSE_INCOMPLETE then 'Failed to delete participant'. HostProviderLoop shutdown returned false. The same signature was seen once on the writer side of a guard-removed mutant run. The abort was in the Host test bridge, not in the guard, and is fixed.

Cause, established from a minidump captured on the failing platform and reproduced deterministically. Frame 2 of the dump, symbolized against the archived original bridge image, is `fastdds_dsoftbus::ros2_host::'anonymous namespace'::'dynamic atexit destructor for 'read_thread''` under `dllmain_crt_process_detach`; the machine code at that frame is `sub rsp,0x28 / cmp dword ptr [<joinable flag>],0 / je done / call qword ptr [__imp_terminate]`, i.e. MSVC's `std::thread::~thread()` = `if (joinable()) terminate();`. A second real capture has the same exception code and subcode but its crash-time image was since overwritten, so it is recorded as consistent, not symbolized.

  1. `PublicSocketBackend::Send` legitimately *returns* `BackendStatus::InvalidState` with the SDK never entered (`api_called == false`) — a socket that is no longer usable, a buffer outside the descriptor bound, or a deadline already past when a reliable retransmission reached the front of the send queue. Where the `false` comes from: the single archived occurrence of the refusal (`runs/green-boundary/A.stderr:129`, `status=5` = `InvalidState`) predates the `api_called` field, so that capture does not print it. It is read from the backend instead: `PublicSocketBackend` is the only concrete `NativeBackend::Send` in the project, and it has exactly one `InvalidState` return (`src/PublicSocketBackend.cpp:442`), the pre-entry refusal disjunct, whose result leaves `api_called` at its default; its only post-entry returns are `{None, rc, true, true}` and `{ProviderException, {}, true, false}`. The `FDDST_ADAPTER_FAULT site=201` on the following archived line is that build's in-try catch site, which the pre-fix adapter reached for *any* non-`None` status, so it does not distinguish the pre-entry from the post-entry branch either way. A refusal on the current build prints both `api_called=` and `FDDST_BACKEND_SEND_REFUSAL reason_mask=`, which would settle it directly; no such line is archived yet.
  2. `ProviderAdapterState::Send` converted every returned status into a thrown provider exception, and `Fault()` latches `provider_status` on the first fault only, so one pre-SDK refusal pinned `ProviderException` for the rest of the process.
  3. That made `Runtime::Close()` false, so `CloseLocal()` returned 1 and `FastddsDsoftbusClose` returned 1 before joining the Host link seam.
  4. The seam's two namespace-scope `std::thread` objects (`integrations/ros2/HostSocket.cpp`) were therefore still joinable when the CRT ran its dynamic atexit destructors at DLL_PROCESS_DETACH, and MSVC's `std::thread::~thread()` is `if (joinable()) terminate();` — `abort()`, i.e. 0xC0000409 with FAST_FAIL subcode 7 (`FAST_FAIL_FATAL_APP_EXIT`). No ASan report is involved. The `Failed to delete participant` line is emitted by upstream `rmw_fastrtps_shared_cpp` when the participant deletion it was handed fails, so it reports the same underlying close failure; it is not a separate defect.

The aborting object is therefore the Host bridge's own `read_thread`, not a payload-guard object, not a Fast DDS object and not the SDK.

Coverage of that chain, stated exactly: its two halves were observed in *different* runs. The refusal, the latch and the reported incomplete close are all in `runs/green-boundary` — a bridge that already joined the seam, which is why that run reports the failure without aborting. The incomplete close → abort link is `runs/red-inject-1` (pre-join bridge, exit 3221226505 with a dump) and the original `runs/cap-h1-47`. No single archived run shows both halves, and `cap-h1-47` predates the diagnostics in steps 1-2 entirely, so the chain as a whole is code reading plus two runs, not one capture.

Fix (teardown accounting and fault classification only; no fail-fast removed, no exception swallowed, no success criterion relaxed, no payload released early, no sleep or serialization added): `FastddsDsoftbusClose` joins the seam on both of its exits, and a status returned without entering the SDK is reported through the caller's returned-code path without latching the close verdict, while a status returned after entering the SDK still latches exactly as before.

Evidence: deterministic RED/GREEN on the injected counterexample scene (`huge` with one side's close verdict pinned incomplete) — old code exits 3221226505 (0xC0000409, FAST_FAIL subcode 7, dump captured) with the business oracle already at PASS; fixed code exits 0 with the incomplete-close verdict still reported as a failure. Unit-level counterexample `dsoftbus.host_provider_loop.refused-send` fails on the old code (close verdict becomes ProviderException) and passes on the fixed code, while `entered-send-failure` pins the latch that must be kept.

The historical *upstream* reason a send was refused under sustained reliable retransmission remains **INFERRED/UNKNOWN**. Its whole archive footprint is one run: `runs/green-boundary` is the only run of the 23 archived boundary runs that carries the refusal marker, and no clean huge run carries it. The current checkpoint adds bounded host observations and a deterministic deadline-only settlement repair; it does not backfill the archived cause. The original secure 70KB fragmented 19/20 execution remains preserved as an unresolved historical attempt, while Candidate 3 current host scenes pass the strict 20/20 contract. See [SEND_REFUSAL_FAILED_SCENE_CHECKPOINT.md](SEND_REFUSAL_FAILED_SCENE_CHECKPOINT.md).

Two different boundary anomalies were seen and they must not be merged: `runs/green-boundary` is the adapter-level refusal above (no abort, close reported incomplete), while `runs/c3-bnd-5` is a *host-fixture* send error (`send_errors=1` from `HostSocket.cpp`, rc `-609`) that never latched anything (`incomplete:false`) and would behave identically on the pre-fix and fixed bridges.

Residual risk, left unfixed on purpose: the join is on the `FastddsDsoftbusClose` path, which is the path every captured abort took (all archived aborts print `FDDST_RUNTIME_CLOSE_INCOMPLETE`; none lacks it). A `Create` whose owner is never closed — the participant never deleted, so `after_delete`/`release_pending` never reached — would still leave the seam's threads joinable at DLL detach and produce the identical abort. No capture has that shape, and in that shape the harness has already broken its own contract, so a loud abort there is closer to correct fail-fast than to something a last-resort joiner should silence; there is deliberately no DllMain/atexit joiner. Related, latent: `StopLink()` is not idempotent (`ready`/`peer_ready` are not nulled after `CloseHandle`, `WSACleanup()` is unconditional), which is harmless only because the hook's `close_state` CAS keeps it to at most one call.

(b) WSL2 GCC 9 AddressSanitizer start-up crashes randomly on this kernel (trivial hello-world fails 13/60 with ASLR, 0/60 without), so sanitizer runs use setarch x86_64 -R.

(c) TSan not run (libtsan missing).

(d) The loopback relay mutates captured frames of two local test processes only (a test fixture, not the guard). A post-oracle socket teardown error is logged separately.

(e) API23: build r6 under build/payload-resource-guard-20261010/api23-r6 exit 0, aarch64 libfastrtps exports FastddsDsoftbusPayloadSnapshot, no target execution.

(f) Review: an independent reviewer found no blocking issue and one non-blocking null-slot dereference, fixed before the final test.

## Remaining Design Inputs Needed from Product Owner

- Production M, B, N and profile approval
- Whether the existing budget partition is the wanted shape: it is one budget per (process, latched configuration), shared by every protected reader pool of every Participant in that process, so a per-process total cap across participants already exists; what is still needed is the product value and the approval of that shape
- Builtin/crypto/CDR coverage decisions
- Device execution of the guard
- The historical reason for refusal and the original R2 19/20 fragmented
  progress attempt (see [SEND_REFUSAL_FAILED_SCENE_CHECKPOINT.md](SEND_REFUSAL_FAILED_SCENE_CHECKPOINT.md));
  current Candidate 3 Host acceptance is separately recorded as strict 20/20.
  The abort it used to cause is fixed and is no longer an open item.
