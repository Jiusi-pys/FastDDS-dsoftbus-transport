# MDDS installed library and external consumers

This package delivers the existing SDK-free cores and default descriptor, plus an
optional, isolated public-Socket runtime. Installation is not production qualification.
Default `DSoftBusTransportDescriptor::create_transport()` still creates a transport
whose valid initialization reports `NativeBackendUnavailable`. P0/PF/P4 remain unchanged.

## Current delivery checkpoint

The current library delivery has **local execution and static target evidence**.
The complete r1 CONFIG result is `524/524 PASS` (677.01 s). The r2
affected closure rechecked installation, moved-prefix external consumers, Host DDS and
default-Unavailable behavior, relative-dependency negative cases, Debug/Experimental/
empty-release-map rejection, real Fake-option exclusion, API23 real-library compile/link,
relocated external consumer `$ORIGIN` behavior and a limited ASan scope. The r2 tester
and formal review both PASS.

The new package proves Host/compile/static API23 boundaries. It does not prove device
execution, SDK runtime behavior, DSoftBus traffic, production readiness, P0/PF/P4 or a
QualificationProfile. Device/HDC execution for this delivery is `0`. The one nested DLL
allocator-injection case was `SKIPPED` because it was not triggered; it is not counted as
a failure or as allocator coverage. Existing P-first N10 and Q-first N100 records retain
their original run/package identities and are not relabeled as this library delivery.
The public headers are from the authorized `communication_dsoftbus` source checkout
`5518313`, not SDK-bundled headers.

Evidence is recorded under the library-delivery task artifact, including the final
tester and formal review. Initial cleanup found no pending project edits to commit.
This delivery adds a C++ assembly wrapper without changing existing lifecycle, SDK or
core implementations; deployment and commit remain separate actions and are not authorized
by this document. See the delivery report at
`.artifacts/library-delivery/run-20260929-local/docs-final/REPORT.md`.

## Dependencies and targets

Use the existing **shared Release Fast DDS 3.6.2.0 CONFIG installation**, with its
matching Fast CDR and foonathan_memory packages. MSVC consumers use the Release DLL
CRT (`/MD`). Source-probe headers are not an installable binary configuration.
The tested MSVC 14.44 packager uses private deterministic/path-map options to remove
source and object-output diagnostic paths from installed archives; consumers do not
inherit those options. Clang uses file-prefix maps for the same purpose.
The SDK-free interface needs Fast DDS headers/libraries and Threads, but no DSoftBus
headers, SDK or platform DSO. No dependencies are downloaded or bundled automatically.

`find_package(MDDS CONFIG REQUIRED)` exports the existing names under `MDDS::`:

- `mdds_dsoftbus_transport_facade`: default, unavailable native backend.
- Directory, owner, queue, input, attachment, send, process and service-record core
  targets: transitive dependencies with their existing `mdds_dsoftbus_*` names.
- `mdds_example_types`: the existing generated keyed sample, with public headers
  under `mdds/examples/`; not another Transport implementation.

`find_package(MDDS CONFIG REQUIRED COMPONENTS Experimental)` additionally exports
`MDDS::mdds_experimental_runtime` and its isolated real Socket backend archive.
It is built only for OHOS with an explicit platform DSO. It does not register or
enable a production backend. Never link default, Host-injected and experimental
facade variants into the same executable; CMake compatibility properties reject this.

The concrete macro-dependent `DSoftBusTransport` class, Host facade injection,
`HostProviderLoop`, `RuntimeProbeAccess` and `ProviderAdapter` assembly header are
not installed. Consumers use the stable descriptor, Fast DDS `TransportInterface`,
and optional `experimental::Runtime` pimpl. No consumer includes probes, tests or
artifacts. Fake SDK test libraries and implementation files are not installed.

## Host build, install and relocated consumption

PowerShell example (choose your own absolute directories; `$deps` is a matching,
already installed dependency prefix):

```powershell
$deps = 'D:/dependencies/fastdds-release'
$prefix = 'D:/install/mdds'
cmake -S . -B build-library -G 'Visual Studio 17 2022' -A x64 `
  -DCMAKE_CONFIGURATION_TYPES=Release -DMDDS_FASTDDS_MODE=CONFIG `
  "-Dfastdds_DIR=$deps/share/fastdds/cmake" `
  "-Dfastcdr_DIR=$deps/lib/cmake/fastcdr" `
  "-Dfoonathan_memory_DIR=$deps/share/foonathan_memory/cmake" `
  -DBUILD_TESTING=OFF -DBUILD_INTERFACE_PROBE=OFF -DBUILD_SOFTBUS_CAPABILITY_PROBE=OFF `
  -DBUILD_DSOFTBUS_TRANSPORT_FACADE=ON -DMDDS_INSTALL_PACKAGE=ON
cmake --build build-library --config Release --parallel 4
cmake --install build-library --config Release --prefix "$prefix"
cmake -S "$prefix/share/MDDS/examples/installed_consumer" -B consumer-build `
  -G 'Visual Studio 17 2022' -A x64 -DCMAKE_CONFIGURATION_TYPES=Release `
  "-DCMAKE_PREFIX_PATH=$prefix;$deps"
cmake --build consumer-build --config Release
$env:PATH = "$deps/bin;$deps/lib;" + $env:PATH
& ./consumer-build/Release/mdds_consumer.exe
```

The example checks the real default MDDS factory's failure, then separately creates
a real Fast DDS Participant, Topic, Publisher/Subscriber and Writer/Reader using
the default Fast DDS Host configuration (which may deliver intra-process), writes/takes one keyed sample and deletes all
entities before the Participant. It prints that this **is not DSoftBus traffic**.
The private logical endpoint identifiers are illustrative non-IP IDs; `::0`, `::1`
and multicast-looking IDs are reserved by the actual codec.

The MDDS prefix may move: supply its new `lib/cmake/MDDS` directory or new prefix to
the consumer. Exported MDDS include/archive locations are prefix-relative. Dependency
installations are separately declared; moving MDDS does not move or distribute them.
On other Host platforms use a supported Release generator/compiler and matching
dependency ABI; only the actually tested platform is reported as verified.

## Real Socket experimental library (compile/link, not deployment authorization)

Configure a separate build with the installed API23 OHOS toolchain, Release,
`OHOS_ARCH=arm64-v8a`, explicit target CONFIG dependency directories, and:

```text
BUILD_TESTING=OFF
BUILD_INTERFACE_PROBE=OFF
BUILD_SOFTBUS_CAPABILITY_PROBE=OFF
BUILD_DSOFTBUS_TRANSPORT_FACADE=ON
BUILD_DSOFTBUS_PROVIDER_ADAPTER_PHASE0=ON
BUILD_DSOFTBUS_PUBLIC_SOCKET_SHIM=ON
MDDS_PUBLIC_SOCKET_LIBRARY=<absolute saved target libsoftbus_client.z.so>
DSOFTBUS_SOURCE_DIR=<authorized communication_dsoftbus checkout>
MDDS_INSTALL_PACKAGE=ON
MDDS_BUILD_EXPERIMENTAL_RUNTIME=ON
```

Socket declarations come from the authorized `communication_dsoftbus` public source
headers (current bound checkout `5518313`), **not** bundled SDK headers. The target
toolchain, headers and platform DSO are distinct inputs. Target runtime-library source
equivalence and PF contracts remain unproven. No private fd or Session API is used.

After building/installing, copy the installed `share/MDDS/examples/experimental_socket`
directory outside the source tree. Configure that consumer with the same OHOS toolchain,
explicit `MDDS_DIR=<new-prefix>/lib/cmake/MDDS`, target dependency CONFIG directories,
and `MDDS_SOCKET_LIBRARY=<declared target DSO>`. Cross toolchains may root package
searches, so explicit `MDDS_DIR` is preferable to only `CMAKE_PREFIX_PATH`.
Build only. The example sets `$ORIGIN` loader policy; it does not package SDK/system
DSOs. Actual loader provisioning and permissions require their own authorized workflow.

The eventual executable arguments are:

```text
mdds_socket_consumer <side:0|1> <peer-network-id> <authorized-package> <shared-service-prefix>
```

There are no embedded device identities, tokens, grants or permission files. Launching
or granting privileges is not part of installation. A real run requires both sides'
current route/permission/loader prerequisites and separate device authorization. The
example does not change process tokens or promise the peer listener is already ready.

## Experimental ownership and shutdown

`experimental::Runtime` is a thin delegate to the existing real public Socket backend,
isolated adapter, facade and provider loop. Its class/namespace name is explicit; the
provider remains `DSoftBusProvider`, never disguised as a Mock or qualified by a boolean.
The first library entry deliberately supports **one Participant/one descriptor per
runtime**. It does not expose shared Join or claim the new wrapper was run in the
historical N10/N100 windows. Both descriptor issuance and factory invocation are one-shot.

1. Fill `PublicSocketConfiguration` and a consistent `DSoftBusTransportDescriptor`.
   Supply one local/remote logical route and matching listener/outgoing/peer names.
2. Keep Runtime alive; obtain its descriptor and set Fast DDS built-in transports OFF
   when selecting this experimental DSoftBus path. Configure the peer locator.
3. Run caller-owned send/receive pumps concurrently with DDS operations. The library
   creates no worker, background agent, retry queue or discovery protocol.
4. Create Participant, register type, create entities, wait for matching and use actual
   Writer/Reader APIs. Preserve native return codes and failures; no resend workaround.
5. Delete contained DDS entities and the Participant on an external control thread
   while pumps can still progress. Stop and join pump threads, call `Runtime::Close`,
   then release descriptor/runtime. Do not destroy from a receiver callback.

An active native call may still lack a bounded cancellation guarantee. Example match
and sample deadlines do not cancel an SDK invocation; join is not detached or replaced
by a watchdog. Retain payload/owner/ticket/lease on such failure under the existing
contract. `Close` reports local completion only; SDK drain remains unproven.
If DDS deletion or local Close reports failure, the minimal target example explicitly
terminates the failed process rather than unwinding and releasing a runtime that may
still be owned. That failure termination is not normal cleanup, SDK cancellation or
a recovery guarantee. It does not restart or retry.

## Verification boundaries

`tests/install_consumer_test.py` installs to a fresh prefix, copies the installed example
outside the repository, moves the prefix so the old location no longer exists, compiles
all installed headers and the external consumer, runs Host DDS/default-Unavailable
checks, and checks unsupported Experimental/Debug configurations. Tests use actual
libraries, not `add_subdirectory` or source objects. The separate experimental assembly
test links the existing Fake C boundary explicitly and is never device evidence.

Final results and the exact local commit are recorded in the library-delivery task
artifact. Historical P-first N10 and Q-first N100 retain their original run/package
identities; see [baseline boundaries](repeated-hot-reattach-baseline.md). This delivery
does not establish PF01/PF02, SDK epoch/drain/cancellation, production readiness or a
QualificationProfile. External artifacts and dependency binaries do not accompany a clone.
