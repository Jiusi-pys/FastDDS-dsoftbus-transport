# B5 bounded Windows Java/JNI SDK

Bounded implementation present. Independent initial/final testing and both
Tier-2 reviews PASS; delivery and gate audit evidence is retained under
`build/b5-20261114/`. This extends B4's
hand-written keyed `Standards::BoundedRecord` SDK; it is not a generic IDL-to-Java
generator or a complete OMG Java mapping. B2/B3 language coverage is unchanged.
The B5 plan targets Windows Java. Device-side Java is outside this milestone;
no device/DSoftBus Java acceptance is claimed or replaced by a host test.

## Final evidence

The final Windows x64 tester reports PASS for fresh 3.6.2 and 2.14.5 builds,
9/9 CTest contracts per ABI, 12 exact Java↔independent-B3 C++ lanes, 24
zero-exit processes, ten keyed samples per Java process, and 42 loaded-module
records. The full regression is 175 cases with no failure/error/skip, and the
documentation guard is 93 tests with 7 declared digests. Source, artifact and
JDK provenance checks match all 82 recorded hashes. See
[`FINDING.md`](../../build/b5-20261114/final-tester/FINDING.md),
[`summary.json`](../../build/b5-20261114/final-tester/summary.json), and the
independent [native](../../build/b5-20261114/review-native/FINDING.md) and
[API/build](../../build/b5-20261114/review-api/FINDING.md) reviews. Device
Java/DSoftBus is N/A by the B5 Windows-only scope; these host results do not
substitute for device acceptance.

## Mapping and ownership

The Java package is `org.fastdds.standards.b5`. `BoundedRecord` is an immutable
copy with two unsigned 32-bit values represented as Java `long`, a non-NUL
string8 label of at most 64 bytes, and a payload of at most 256 octets. All array
inputs and outputs are copied. The primary label API preserves raw string8
bytes; the explicit Latin-1 helpers reject unrepresentable characters. It does
not reinterpret user data as JNI modified UTF-8. Empty and full-bound values
are supported. Null, out-of-range and malformed Java inputs are rejected.

`Library.load` resolves its DLL `Path` with `toRealPath` (so relative paths are
accepted) and requires the exact Fast DDS ABI. A process uses one ABI and the
system class loader; changing ABI or recovering from a failed native-load
attempt requires another JVM. Keep the API jar, JNI DLL,
generated support and dependencies from the same recorded build. Synthetic
fault DLLs are rejected by the public loader.

Create an `Endpoint` before attempting `open`, and keep the variable outside a
try-with-resources scope when a caller may need a close retry:

```java
import java.io.IOException;
import java.nio.file.Path;
import java.util.List;

static void useEndpoint(List<Endpoint> pendingClose) throws IOException {
    Library.load(Path.of("standards_b5_jni.dll"), "3.6.2");
    Endpoint endpoint = new Endpoint();
    try (endpoint) {
        Status opened = endpoint.open(219, "bounded_record", Endpoint.Role.WRITER, 0);
        if (opened != Status.OK) throw new DdsException(opened);
        // use endpoint
    } catch (RuntimeException failure) {
        pendingClose.add(endpoint); // caller-owned queue, before retry
        try {
            if (endpoint.tryClose() == Status.OK) pendingClose.remove(endpoint);
        } catch (RuntimeException retryFailure) {
            failure.addSuppressed(retryFailure);
        }
        throw failure; // preserve the original operation/close failure
    }
}
```

The caller owns and persists `pendingClose` after this helper throws. It
schedules bounded close-only retries and removes an endpoint only after
`tryClose` returns `OK`; it does not rely on automatic GC cleanup.

A native registry token is never a raw pointer and is
not reused. Thus partial failed open retains an already reachable Java owner.
`open` returns an explicit `Status`; after a failed open, only close is allowed.
Argument errors detected before native open do not consume that attempt.

`Endpoint` serializes its methods. Native per-owner locks additionally protect
lookup/operation/close races; global registry lookup does not hold a lock across
blocking DDS calls. `tryClose` clears the Java token only on success. A failed
close retains ownership for close-only retry. `close` throws `DdsException` on
an error, including when invoked by try-with-resources. Callers must handle that
failure and retain the endpoint. Successful close is idempotent at the Java
API; unknown or retired native tokens are rejected. These sequential IDs are
process-local lookup identifiers, not security capabilities. There is no implicit GC
cleanup, borrowed buffer, data loan or Java callback from a DDS thread.

Match and acknowledgment waits are bounded. `take` returns either copied valid
data or an explicit status with no sample; invalid-data lifecycle events are
consumed as NO_DATA, matching B4. Java allocation failure after a successful DDS
take cannot undo that take. Construction/open/take errors do not silently turn
into a success result. All normal endpoint and sample ownership must drain.

## Build and DDS profile

`scripts/standards/build_java_backend.py` requires a new workspace build output,
an explicit Windows x64 JDK and clang preprocessor, one exact DDS ABI and its
runtime directories. It generates fresh version-bound B3 support, runs
`javac -h` to define the JNI interface, builds the DLL and jars, and executes
the registered contracts with `-Xcheck:jni`. Tool versions, JDK/tool/header
hashes, source snapshots, commands, exits and artifacts are retained. CMake
rejects native/device and DSoftBus-enabled JNI builds for this milestone.

The production JNI DLL uses B4 above the public DDS API: reliable KEEP_LAST
depth 16, UDPv4 selected explicitly, data sharing disabled, and either XCDR1
or XCDR2. It exposes B4's bounded domain and timeout profile plus UTF-8 topic
names of 1..255 bytes without NUL. It changes no Fast DDS, DDS/RTPS, discovery,
reliability, QoS or Transport implementation. Native callbacks never use a
saved JNIEnv pointer from another thread.

`scripts/standards/run_java_acceptance.py` consumes both build summaries. It
rejects changed source, jar, DLL or JDK provenance, records the actually loaded
JNI/DDS modules, and launches a Java producer against the independent existing
B3 C++ reader, and the reverse. The data oracle includes ten actual keyed
samples, empty/full label and payload bounds, exact values and clean exits.
The actual samples are each sent once. The trusted launch gate permits module
inspection before data transmission; it is not a DDS reliability workaround.
Same-ABI lanes use XCDR1/XCDR2. Mixed-ABI lanes use XCDR1 only, preserving the
known stock mixed-version XCDR2 limitation.

Synthetic tests exercise failed-open retention, failed-close retry and a C++
exception inside the JNI call body. They use a separate synthetic DLL, not
physical DDS/Socket failures. The B4 C API still obeys its no-C++-exception
boundary; the tests do not throw across that extern-C contract.

## Operation and rollback

Use an explicit matching ABI dependency directory for each Java process. Check
every operation status. On shutdown, close endpoints, inspect close status and
retry only close if it fails; normal acceptance requires zero live owners and
exit 0. This SDK does not promise recovery from every DDS/runtime error.
Rollback stops the Java application and uses the prior accepted C/C++ SDK and
its matching dependencies; there is no persistent state migration.

The JNI implementation follows the pinned JDK headers and the Java 17
[JNI functions](https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html)
and [design](https://docs.oracle.com/en/java/javase/17/docs/specs/jni/design.html)
contracts for array copies, thread-local interface pointers and exception
handling. Full DDS-WEB, SOAP, DDS-OPCUA and complete IDL language mappings remain
separate scopes; C3 remains BLOCKED.
