# D3 bounded request/reply groundwork

D3 is PRESENT as a bounded Windows public-DDS foundation for the later DDS-OPCUA Complete work. It is IMPLEMENTED, BUILDS, HOST_TESTED and bounded INTEGRATION_TESTED PASS for exact Fast DDS 3.6.2 and 2.14.5 host SDKs. Device validation is N/A: this milestone contains no DSoftBus path; D5 owns RK3588A and cross-device validation. The complete standards product remains NOT VERIFIED, C3 remains BLOCKED, D4 is now a separate bounded services/history host profile, and D5 remains PLANNED.

## What is implemented

The reusable header-only layer in `integrations/standards/request_reply/` creates two service-derived Topics, `<service>_Request` and `<service>_Reply`, and uses public `DataWriter`/`DataReader` APIs. The generated unkeyed `RpcMessage` contains a bounded operation and status field, a label limited to 64 bytes, and a payload limited to 4096 octets. Reliable UDPv4, KEEP_ALL histories, data sharing off, representation 0 or 2, and bounded resource limits are configured by the host profile.

Correlation uses the complete native Writer GUID and DDS sequence number from `SampleIdentity`; request and reply records are linked with `related_sample_identity`. Business fields are never used as request identifiers. The client limits pending calls to 1–16, and that capacity includes completed but not yet taken results. It retains those terminal results until `take`, observes caller-driven monotonic deadlines from 1 to 60000 milliseconds, supports local cancellation, and records late, duplicate, malformed and unmatched replies. A bounded set of 64 recent retired identities is used for duplicate/late classification; it is not durable storage or an all-history deduplication record. The server bounds owned requests, accepts at most 16 samples per poll pass, and requires one local reply or explicit discard for each accepted request. Objects, polling and shutdown are thread confined. Close checks a fixed five-second writer acknowledgment wait, entity deletion and participant ownership.

There are no automatic application retries, remote cancellation, durable queues, exactly-once execution guarantees, keyed lifecycle operations, DSoftBus APIs, Transport changes, RTPS changes, or DDS semantic changes. The profile assumes one logical replier; multiple requesters may share the Topics. Matching counts establish readiness only; the independent native peer verifies actual sample identities and values.

## Dependency and interoperability boundary

The original plan's blanket statement that Fast DDS exposes no DDS-RPC is stale for the installed SDKs. Local Fast DDS 3.6.2 contains public RPC headers and service factory exports, while local 2.14.5 contains no equivalent RPC header directory or factory exports. D3 deliberately uses the common low-level public DDS surface. It does not claim interoperability with the 3.6.2 `Requester`/`Replier` classes, and it does not claim normative OMG DDS-RPC wire conformance. D4 owns service mapping and D5 owns the device path.

## Evidence

The final independent tester used fresh build directories and recorded:

- both exact ABI builders PASS, four builder commands and one CTest contract per ABI;
- 40 acceptance cases and 82 child processes, all natural exit 0 with no forced cleanup, covering mixed ABIs, both representations, malformed and duplicate traffic, late replies, timeout, cancellation, close, capacity, not-ready, discard and two simultaneous requesters;
- an independent `add_subdirectory` consumer configuring, building and running for both ABIs with six commands at exit 0, without manual DLL macros or extra generated sources;
- 175 standards regression tests PASS, strict documentation consistency 93 tests with 7 declared digests PASS, negative builder/oracle guards PASS, and `git diff --check` PASS.

The evidence is retained in [`final tester finding`](../../build/d3-20261114/final-tester/FINDING.md), [`structured test result`](../../build/d3-20261114/final-tester/TEST_RESULT.json), [`review closure`](../../build/d3-20261114/review/REVIEW_CLOSURE.md), and [`dependency assessment`](../../build/d3-20261114/DEPENDENCY_ASSESSMENT.json). The one review finding, SDK imported-target visibility for parent consumers, was fixed in CMake and closed by the fresh two-ABI consumer checks.

## Building and consuming

Use `scripts/standards/build_request_reply.py` with an exact Fast DDS version, a fresh directory below `build/`, the pinned clang executable, and the corresponding SDK runtime directories. The generated CMake invocation supplies `DDS_STANDARDS_FASTDDS`, `D3_GENERATED_DIR` and `D3_IDL`; it also supplies the exact SDK discovery path (`fastrtps_DIR` for 2.14.5 or `CMAKE_PREFIX_PATH`/`fastdds_DIR` for 3.6.2) and rejects unrelated CMake overrides. The builder generates the IDL support, configures and builds the host targets, runs the contract test, locks the source and generated snapshots, and records resolved import-library and runtime DLL provenance. The exact executed argv are retained for [3.6.2](../../build/d3-20261114/final-tester/abi-36/commands.json) and [2.14.5](../../build/d3-20261114/final-tester/abi-214/commands.json).

An external CMake project can `add_subdirectory(integrations/standards/request_reply ...)` and link `d3_request_reply`. The D3 CMake project promotes its locally discovered imported SDK targets to global visibility so the SDK's shared-library compile definitions propagate to parent consumers. The final consumer probe exercises this recipe for both exact ABIs without adding generated sources or workaround macros.

Primary references are the [Fast DDS 2.14.5 request/reply documentation](https://fast-dds.docs.eprosima.com/en/v2.14.5/fastdds/use_cases/request_reply/request_reply.html), [OMG DDS-RPC 1.0](https://www.omg.org/spec/DDS-RPC/1.0/PDF), and [CMake imported-target visibility](https://cmake.org/cmake/help/latest/prop_tgt/IMPORTED_GLOBAL.html). These references describe the public API and dependency boundary; the retained local execution records determine the status above.
