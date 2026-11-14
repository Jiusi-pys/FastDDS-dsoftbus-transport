# D2 bounded DDS-to-UA host gateway

Implementation PRESENT; BUILDS, HOST_TESTED and bounded host
INTEGRATION_TESTED PASS. The final independent tester and both focused reviews
are closed for this finite Windows AMD64 host profile above public DDS APIs.
D5 owns RK3588A/DSoftBus/dual-board validation; it is N/A here. C3 remains
BLOCKED, and the full standards product remains NOT VERIFIED.

Canonical final evidence is retained in
[`build/d2-20261114/final-tester/FINDING.md`](../../build/d2-20261114/final-tester/FINDING.md),
[`summary.json`](../../build/d2-20261114/final-tester/summary.json), and
[`execution-receipt.json`](../../build/d2-20261114/final-tester/execution-receipt.json).
The native lifecycle and provenance review closures are
[`review-native/CLOSURE.md`](../../build/d2-20261114/review-native/CLOSURE.md),
[`review-native/ACK_CLOSURE.md`](../../build/d2-20261114/review-native/ACK_CLOSURE.md),
and [`review-evidence/CLOSURE.md`](../../build/d2-20261114/review-evidence/CLOSURE.md).

The server uses pinned open62541 1.4.21 and generated `Standards::BoundedRecord`
support with exact Fast DDS 3.6.2 or 2.14.5. It exposes one configured domain
and topic, the finite record datatype, and stable keyed instance objects.
Native DDS uses explicit UDPv4, reliable KEEP_LAST depth 16 and data sharing
disabled, with either XCDR1 or XCDR2. No Transport, RTPS, DDS or Fast CDR source
is changed. The independently compiled native fixture uses public DDS only.
The independent UA protocol stack is Python asyncua 2.1.0, rather than the
server SDK; its installed package is checked against the pinned official wheel.
The configured UDPv4 fixture uses an external native DDS peer for the observed
normal-path witness. `dataSharing` is explicitly off for the fixture, but the
Fast DDS default intraprocess library setting is unchanged; local self-echo is
not therefore claimed to be wire-UDP evidence. The reviewed KEEP_ALL and
intraprocess-off controls were not selected as the implementation configuration.

## Address space and operations

Resolve namespace URI `urn:fastdds:standards:d2`, then browse
`Objects/DDS/Types/BoundedRecord` and `Objects/DDS/Domain_<id>/<topic>`.
`DomainId`, `RegisteredTypeName` and datatype `Fields` describe the finite
configuration. The datatype uses numeric identifiers 3001 (datatype) and 3002
(binary encoding) in that namespace. This local namespace and field metadata
are a bounded application profile, not a claim to implement the complete OMG
normative Nodeset or arbitrary DDS datatype discovery.

Each observed or explicitly registered key owns `Instance_<key>` with stable
string NodeIds and children `id`, `sequence_number`, `label`, `payload`,
`Value`, `ValidData`, `InstanceState` and `SourceTimestamp`. The aggregate
`Value` is a custom UA Structure with UInt32 key and sequence, then ByteString
label and payload. The label preserves non-NUL string8 octets with a 64-byte
bound; the payload has a 256-byte bound. ByteString avoids silently interpreting
arbitrary string8 octets as UTF-8. This is an explicit finite mapping, not full
OMG String/type mapping conformance. UA null and empty ByteStrings both map to
an empty DDS value.

Topic methods `RegisterInstance(id)`, `WriteSample(id, sequence_number, label,
payload)`, `DisposeInstance(id)` and `UnregisterInstance(id)` call the native
DDS writer. A value-only Write service to an instance's aggregate `Value`
also publishes through DDS; its key must match the node. Register first before
using that writer. Member and metadata nodes are read-only. Incoming quality
and timestamps cannot be overwritten: UA clients must omit StatusCode and
timestamps from Write requests. Incorrect types, bounds, keys, duplicate
registration and capacity exhaustion return explicit UA errors.

`UnregisterInstance` includes a checked native acknowledgment boundary before
returning Good or permitting another keyed publication. An independent native
probe reproduced ACK failure and blocked deletion when a written key was
unregistered and another key was written on the same writer, using both ABI
versions and both official and B3 generated type support. The probe contains
no UA code. A public `wait_for_acknowledgments` immediately after unregister
passes the focused closure controls; the original failure, and the unsuccessful
KEEP_ALL and intraprocess controls, remain retained historical evidence. This
is an explicit dependency sequencing adaptation, not a Fast DDS/RTPS patch.
See [`ACK_DEPENDENCY_FINDING.md`](../../build/d2-20261114/review-native/ACK_DEPENDENCY_FINDING.md)
and its [`ACK_CLOSURE.md`](../../build/d2-20261114/review-native/ACK_CLOSURE.md).
The wait retains its fixed five-second maximum and any error fails the run.
The single UA event loop may block for that bounded operation. The independent
client uses eight-second request and watchdog budgets, and its process budget
allows several such operations; native ACK and shutdown budgets are unchanged.

The UA view changes only when the native reader observes a DDS sample or
lifecycle event. Successful UA writes are not evidence of remote delivery;
acceptance also checks the independent native reader. Initial placeholders
carry `BadWaitingForInitialData`. A disposed or no-writers instance retains
its latest value with `BadOutOfService` and `ValidData=false`; native states
are 1 (alive), 2 (disposed) and 4 (no writers), with 0 before any observation.
Valid writes resurrect the instance. Source timestamps come from native DDS
SampleInfo and are exposed as UA DateTime. Read and data-change subscriptions
are supported; there is no historical read or durable notification queue.

The current public DDS `get_key_value` path does not provide the required
lifecycle key. The server retains a bounded correspondence between keys and
previously observed valid sample handles or locally registered writer handles.
Known UA-owned keys support Dispose before their first write and retain bad
initial-value quality until an actual value arrives. An unmatched external
lifecycle handle fails the run explicitly; external lifecycle-only instances
without an observed or locally registered key are outside this profile.
A configured instance slot is retained, including tombstones;
there is no silent eviction. UA registration at capacity returns an error;
an external DDS sample introducing another key fails the run.

## Build, configuration and ownership

`scripts/standards/build_dds_ua_gateway.py` requires a fresh workspace build
directory, pinned open62541 source/archive, exact DDS discovery paths and a
client toolchain manifest. It builds the unchanged D1 dependency project,
generates type support, then builds D2 and runs its contracts. ABI, generated
input and actual imported UA prefix/library are locked; only DDS discovery
CMake options are accepted. Source, upstream, client package, binary and
resolved library hashes and actual child exits are recorded. The acceptance
runner checks those inputs before and after the matrix and audits loaded DDS
DLLs in the native processes; the independent UA client must load none.

Invoke `dds_ua_gateway.exe CONFIG`; all keys in
[`profile.example.cfg`](../../integrations/standards/dds_ua_server/profile.example.cfg)
are mandatory. Configuration is strict and size bounded. The UA endpoint is
numeric loopback `127.0.0.1` with SecurityPolicy None; this is a local host
development profile. The instance cap is configurable from one to 64. All UA
callbacks, native sample polling, cached records and node creation run on one
thread; callback contexts and custom type descriptors outlive UA server
deletion. DDS work is polled with a finite per-iteration budget. Callback
exceptions are caught and fail the run.

Create the configured absolute stop file to stop. Shutdown stops/deletes UA,
checks DDS acknowledgments with one fixed five-second wait after published
data, then deletes DDS entities and the participant. Errors remain failed
runs, including shutdown errors. Deadlines, external capacity overflow and
bind failure have explicit failed exits and ownership logs. `run_ms` is a
supervision budget, not a hard wall-clock deadline. Restart builds a new
volatile address space; cached values and subscriptions are not persisted.

## Evidence and limits

The final tester recorded both exact ABI builds PASS, a 20-case acceptance
matrix PASS with 46 child exits matching expected results and no forced
cleanup, 175/175 regression tests PASS, and strict documentation checks PASS.
Normal cases retained sixteen terminal ACK records (three unregister and one
shutdown per case), all Good. The client used the independently installed
asyncua 2.1.0 package and loaded no DDS module. Implementation alone does not
establish correctness; these are bounded host execution results. The bounded
profile excludes arbitrary types, dynamic multi-topic discovery, normative
Nodeset completeness, full Basic conformance, authenticated remote UA,
Service Sets, Query, history, exactly-once delivery and device validation.
D3 is now PRESENT as a separate bounded public-DDS request/reply foundation;
D4 is PRESENT as a separate bounded services/history host profile and does not
modify this D2 executable. D5 remains planned and owns device dependencies and
acceptance.

Sources: [OMG DDS-OPCUA 1.0](https://www.omg.org/spec/DDS-OPCUA/1.0/PDF),
[open62541 versioned source](https://github.com/open62541/open62541/tree/v1.4.21),
[asyncua source](https://github.com/FreeOpcUa/opcua-asyncio/), and
[asyncua release](https://pypi.org/project/asyncua/2.1.0/).
