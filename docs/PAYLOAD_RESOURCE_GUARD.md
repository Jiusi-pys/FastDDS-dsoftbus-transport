# Payload Resource Guard

## Status

USER_READER_PAYLOAD_LIMIT_HOST_VERIFIED (limited scope), PAYLOAD_POOL_BUDGET_HOST_VERIFIED (limited scope), API23_PAYLOAD_GUARD_BUILD_VERIFIED (compile/link only), DEVICE_RESOURCE_GUARD_NOT_RUN, NOT_PRODUCTION_QUALIFIED. Production M/B/N: NEEDS_PRODUCT_VALUE. The profile TEST_ONLY_BOUNDED is the only accepted profile and is never production approval; configs without the four keys are explicitly UNPROTECTED_LEGACY.

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

## Known Issues

(a) One earlier canonical run (tester-canon-1) FAILED: in the guarded huge scene the reader process aborted with exit 0xC0000409 after its own checks passed, with FDDST_RUNTIME_CLOSE_INCOMPLETE then 'Failed to delete participant'. HostProviderLoop shutdown returned false. The same signature was seen once on the writer side of a guard-removed mutant run. About 1 failure in ~36 guarded huge runs, 0 in the last 24. Root cause in the runtime close path under sustained reliable retransmission is NOT established and not shown to be caused by or fixed by this change.

(b) WSL2 GCC 9 AddressSanitizer start-up crashes randomly on this kernel (trivial hello-world fails 13/60 with ASLR, 0/60 without), so sanitizer runs use setarch x86_64 -R.

(c) TSan not run (libtsan missing).

(d) The loopback relay mutates captured frames of two local test processes only (a test fixture, not the guard). A post-oracle socket teardown error is logged separately.

(e) API23: build r6 under build/payload-resource-guard-20261010/api23-r6 exit 0, aarch64 libfastrtps exports FastddsDsoftbusPayloadSnapshot, no target execution.

(f) Review: an independent reviewer found no blocking issue and one non-blocking null-slot dereference, fixed before the final test.

## Remaining Design Inputs Needed from Product Owner

- Production M, B, N and profile approval
- Whether a per-process budget across participants is required
- Builtin/crypto/CDR coverage decisions
- Device execution of the guard
- Root-causing the close race (exit 0xC0000409 in HostProviderLoop shutdown under sustained retransmission)
