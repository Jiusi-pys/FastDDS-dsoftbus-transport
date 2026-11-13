# B4 bounded C mapping

Bounded milestone delivered, 2026-11-13. Final host/native acceptance and
focused review closure are recorded below. This SDK maps the existing keyed
`Standards::BoundedRecord` fixture through B3's generated C++ support. It is a
finite C consumer contract, not a general IDL-to-C generator or complete OMG C
language-mapping implementation. B2/B3's broader accepted types are unchanged.

## API and ownership

`integrations/standards/c_backend/b4.h` is a C header. The library is C++17;
`c_client.c` is compiled as C11. Each exact Fast DDS ABI needs its own build,
generated support, dependency closure and library. Handles cannot cross builds
or library instances. This is not a stable binary ABI across SDK revisions.

Samples and endpoints are opaque owning handles. Create/open requires a valid
output pointer initialized to NULL. Destroy/close accepts NULL. After successful
destruction the caller clears its variable and must not use the old pointer.
Callers serialize all operations on each handle, including destruction. Forged,
dangling and double-destroyed pointers cannot be validated by this API.

`b4_record` is caller-owned: two unsigned 32-bit fields, a length-delimited
label with capacity 64, and an octet payload with capacity 256. Sample set/get
copies data; no loan or borrowed pointer escapes the library. Labels do not
include a terminator and embedded NUL bytes are rejected. Empty and full-bound
values are supported. Failed sample set/get leaves the destination unchanged.
No allocation by this library is freed by the caller's C runtime.

The API returns explicit BAD_PARAMETER, BOUNDS, NO_DATA, TIMEOUT, ERROR and
UNSUPPORTED results. C++ exceptions are caught at every exported C function.
Open failure normally leaves the output NULL. If cleanup itself fails, ERROR
may return a non-NULL endpoint solely for retrying close. Failed close retains
the handle; users must inspect every return code. A successful take reports
valid sample data only; lifecycle notifications are consumed as NO_DATA.

## DDS and transport scope

Each endpoint owns one participant, topic, publisher/writer or subscriber/reader.
The exposed profile is reliable KEEP_LAST depth 16, data sharing disabled,
domain 0 through 232, and one representation: XCDR1 (0) or XCDR2 (2).
Write copies the sample before calling public DDS APIs. Wait-match and ACK
timeouts are bounded by 60000 ms. This API does not expose the complete DDS QoS,
entity, discovery, key lifecycle or loan surface. B5 adds a separate bounded
Windows Java/JNI facade over this unchanged C SDK; see
[`B5_JAVA_JNI_MAPPING.md`](B5_JAVA_JNI_MAPPING.md).

The default build explicitly selects UDPv4 with built-in transports disabled.
The optional OHOS adapter uses the installed stock DSoftBus Transport and the
existing device deployment/Socket runtime. It supplies transport configuration
to the public participant API; DDS/RTPS, discovery and reliability are untouched.
There is no Session API or UDP fallback on that path. Token setup is process
state; the embedding application must coordinate permissions and other users
of that process. Deployment strings are copied during open, not retained.

Both device listeners synchronize through a new run's explicit gate file before
dialing. The adapter retains the existing runtime pump/error latch and closes
entities before stopping the runtime. Transport counters expose successful SDK
sends and received records for device acceptance; UDP returns UNSUPPORTED.

## Build and acceptance

Configure `integrations/standards/c_backend` with the exact
`DDS_STANDARDS_FASTDDS`, `B3_GENERATED_DIR` and `B3_IDL` used by B3. The existing
B3 provenance gate verifies all generated artifacts and input/backend hashes.
Build target `b4_client`; `standards_b4` is the static SDK target. On OHOS,
`B4_DSOFTBUS=ON` additionally requires a matching installed
`FastDDSDSoftBusStock` package and its platform dependency closure.

Required acceptance is evidenced for this bounded milestone: ownership/error
controls, actual C writer/reader UDP exchange on Windows for both ABIs and
representations, and real DSoftBus exchanges on both RK3588A boards with
physical writer roles reversed. Device evidence records source/deployed hashes,
Socket counters, exact sample checks, reliable ACKs, endpoint exit codes and
cleanup. Initial diagnostics remain retained and are excluded from the
canonical result; compilation alone is not device acceptance.

## Evidence and status

| Dimension | State | Evidence |
| --- | --- | --- |
| Implementation | PRESENT — bounded C SDK over the B3 keyed `BoundedRecord` support | [`final-implementation-snapshot.json`](../../build/b4-20261113/final-implementation-snapshot.json), [`review-fix-source-snapshot.json`](../../build/b4-20261113/review-fix-source-snapshot.json) |
| Host build/test | PASS — both exact ABIs, four CTest ownership/close-failure tests, four two-process UDP cases, SDK probes, B1–B3/regression suites, docs and whitespace checks | [`final-host/FINDING.md`](../../build/b4-20261113/final-host/FINDING.md), [`final-host/machine-summary.json`](../../build/b4-20261113/final-host/machine-summary.json) |
| Review | CLOSED — R1 runtime-owner retry and R2 empty-payload memcpy findings closed; actual-source instrumentation observed zero null memcpy calls and four targeted tests passed | [`review/CLOSURE.md`](../../build/b4-20261113/review/CLOSURE.md), [`review/closure-evidence.json`](../../build/b4-20261113/review/closure-evidence.json) |
| Native/device build | PASS — both OHOS ABI targets and four ELF64/AArch64 executables | [`final-native/FINDING.md`](../../build/b4-20261113/final-native/FINDING.md), [`final-native/machine-summary.json`](../../build/b4-20261113/final-native/machine-summary.json) |
| DSoftBus integration | PASS — 8 canonical cases, 16 endpoint exits at 0, both boards, both ABIs/representations, roles reversed; positive Socket counters and exact values/ACK/cleanup recorded | [`final-native/final-canonical-result.json`](../../build/b4-20261113/final-native/final-canonical-result.json) |

Close failure is a close-only retry contract: a failed or throwing cleanup
retains the endpoint and its runtime owner for a later `b4_close`, while
continuing to report `B4_ERROR`. It does not promise that every runtime or
socket failure will recover. The fault-injection coverage uses a synthetic
fake Runtime/helper and actual-source instrumentation; it is not physical
DSoftBus Socket failure injection.

The canonical native run uses `/data/local/tmp/b4-20261113-final-r1` and the
matching `/data/local/tmp/fdds_dsb/{2.14.5,3.6.2}` libraries. The declared
runtime closure has 16 needed files equal across both boards and 12 matching
host prefixes. `libsoftbus_client.z.so`, libc and the system loader remain
explicit un-hashed system boundaries; the pre-all-runtime check was NOT_RUN,
so this is not a full-OS closure certification. [`runtime-closure-compare.json`](../../build/b4-20261113/final-native/runtime-closure-compare.json)
and [`full-runtime-closure-audit.json`](../../build/b4-20261113/final-native/full-runtime-closure-audit.json)
record those limits.

Wrong-path, peer-ID and side-harness runs remain retained diagnostics and are
excluded from the canonical R3 result; command, PID and actual exit evidence
remain available in the final-native records. The first host attempt's missing
`IDL_CLANG`/wrong B3 suite environment failure and older partial-install
header uploads are likewise preserved, with corrected final runs PASS.
