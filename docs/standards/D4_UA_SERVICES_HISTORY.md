# D4 bounded UA services and history

## Status and boundary

D4 is PRESENT, BUILDS, HOST_TESTED and bounded INTEGRATION_TESTED PASS for the
Windows AMD64 public-DDS/open62541 profile. It is a separate milestone above
the unchanged D3 request/reply foundation. D4 does not modify D1, D2, D3,
Fast DDS, Fast CDR, RTPS, Transport, discovery or reliability semantics, and it
does not add history to the D2 executable. D5 remains PLANNED for RK3588A,
DSoftBus and cross-device validation. The overall standards product remains
NOT VERIFIED; C3 remains BLOCKED.

This is bounded application coverage, not full OMG DDS-RPC, DDS-OPCUA Complete,
Nodeset, Variant, event, XML or device conformance. The service envelope uses a
versioned application codec: canonical UA Binary bodies without a TypeId prefix
inside the bounded D3 `RpcMessage.payload` (maximum 4096 bytes). It is not a
normative generated DDS-RPC IDL/wire binding and makes no native 3.6.2
Requester/Replier interoperability claim.

## Components and service set

The build produces separate executables:

| Component | Role |
| --- | --- |
| `d4_service_gateway` | D3 public-DDS responder forwarding to a native open62541 UA client |
| `d4_history_gateway` | Native public-DDS `BoundedRecord` reader mirrored by a native open62541 historical server |
| `d4_native_peer` | Independent public-DDS fixture; it does not link D3/D4 core or the UA library |
| `d4_contract` | Native contract checks |

The service gateway forwards twelve selected operations: Browse, BrowseNext,
TranslateBrowsePathsToNodeIds, RegisterNodes, UnregisterNodes, QueryFirst,
QueryNext, Read, Write, HistoryRead, HistoryUpdate and Call. They cover View(5),
Query(2), Attribute(4) and Method(1). Full Writer GUID plus DDS sequence and
`related_sample_identity` provide correlation; business fields are not IDs.
There are at most four Writer-GUID-bound UA client/session bindings per run,
with no automatic reclamation or application retry. The endpoint is numeric
loopback only (`opc.tcp://127.0.0.1:<port>`), logical label `local`, anonymous
SecurityPolicy None, and a fixed one-second operation budget managed by the UA
SDK/gateway. Invalid opcodes/routes, malformed or trailing bodies, capacity
exhaustion and responses over 4096 bytes return explicit envelope errors.

The Python `asyncua` 2.1.0 client is independently installed and verified. Stock
asyncua does not provide QueryFirst/QueryNext; `d4_ua_fixture.py` adds only a
finite test-fixture Query over SensorA/B/C, with filter and continuation. Its
Browse pagination and HistoryUpdate insertion are also explicit fixture
extensions. The fixture does not import D3/D4 implementation code, and these
extensions are not claims about stock asyncua.

## History contract

History mirrors the finite D2 `BoundedRecord` descriptor without changing D2.
The custom aggregate is in namespace `urn:fastdds:standards:d4:history`, with
fields `id: UInt32`, `sequence_number: UInt32`, `label: ByteString<=64` and
`payload: ByteString<=256`. Stable per-key nodes expose the aggregate and each
member directly below Objects. Four keyed instances are retained, each with
configurable capacity 1..16 and receipt-based steady-clock retention
100..60000 ms; the latest current value survives historical expiry. The DDS
reader uses reliable UDPv4, KEEP_ALL 64 per instance, bounded total resources,
and data sharing off. Application history is a separate bounded layer.

Raw reads sort by delivered SourceTimestamp and support forward/reverse and
open-ended ranges; at least two of start/end/count are required. Equal source
timestamps retain the newest version and set ExtraData `0x408`. Source,
Server, or Both timestamps are supported; Neither/Invalid is rejected. Server
timestamps are captured at receipt and remain stable through repeated pages.
Empty bytes remain empty ByteStrings rather than UA Null, for both members and
aggregates. Eight session/node-bound continuation slots use rotating single-use
tokens and explicit release; a lease expires at the earliest selected sample's
receipt plus retention, so pagination cannot extend retention. Restart creates
a new volatile cache. Unsupported interpolation, processed/modified/event or
at-time history and historical editing of the DDS mirror return explicit UA
errors. Observed invalid lifecycle data preserves the value with
`BadOutOfService`; valid resurrection restores good quality.

## Build and acceptance recipe

`scripts/standards/build_ua_complete.py` creates a fresh output, builds exact
Fast DDS 3.6.2 and 2.14.5 bundles, enables open62541 1.4.21 QUERY and
HISTORIZING, uses the pinned asyncua client environment, and builds nested D3
support. The UA static library uses the dynamic MSVC CRT to match the DDS
profile. Use the repository's strict profile configuration (`domain`, `rep`,
`port`, `run_ms`, `capacity`, `retention_ms`, `service`, `topic`, `endpoint`,
`stop`) with domain 0..225, representation 0 or 2, capacity 1..16, retention
100..60000 ms, numeric loopback endpoint and an absolute stop path. The exact
executed link arguments and provenance are retained in:

- [`ABI 3.6.2 command manifest`](../../build/d4-20261114/final-tester/abi-36/commands.json)
- [`ABI 2.14.5 command manifest`](../../build/d4-20261114/final-tester/abi-214/commands.json)
- [`D4 scope`](../../build/d4-20261114/D4_SCOPE.md)

The final matrix passed both fresh builders (eight outer commands each), top
CTest 3 and nested D3 contract 1 per ABI, 14 D4 acceptance cases with 64
natural child exits and no forced cleanup, and D3 regression with 40 cases and
82 natural exits. The shared standards regression passed 175 tests. Independent
base and extra probes passed all 12 natural child exits, including historical
empty bytes before eviction and stable one-value Server/Both pagination and
re-query behavior. Strict documentation consistency passed 93 tests and 7
declared digests. Review closure is recorded in
[`review closure`](../../build/d4-20261114/review/REVIEW_CLOSURE.md).

## Limits and retained diagnostics

The evidence is Windows host evidence only; D4 has no DSoftBus or RK3588A path.
The domain-224 extra-probe failure remains a diagnostic, not a product pass or
failure relabel: the pinned DDS mapping selected UDP 63400, inside the observed
Windows excluded range 63345..63444, and direct bind returned WSAEACCES (10013).
The same binaries and strict probe passed on canonical domain 220 without
source, OS or timeout changes. See
[`PORT_ENVIRONMENT.json`](../../build/d4-20261114/PORT_ENVIRONMENT.json).

All four review findings are closed: incomplete selectors and invalid timestamp
modes reject explicitly; duplicate source times choose the newest with 0x408;
empty bytes preserve their representation; and receipt ServerTimestamp is
stable. The review still records GitNexus HIGH risk for the changed graph; that
risk is not erased by passing tests and absent flows are not proof of no impact.
