# D1 bounded UA-to-DDS host gateway

Implementation PRESENT; fresh host builds, acceptance, regression, strict
documentation checks and both focused Tier-2 reviews PASS/CLOSED. This is a
bounded Windows AMD64 host milestone, not the full OMG Basic conformance point.
D2–D5 remain PLANNED. D5 owns RK3588A/DSoftBus/dual-board validation and is N/A
for this host scope. C3 stays BLOCKED and the complete standards product
remains NOT VERIFIED. Canonical execution evidence is retained in
[`build/d1-20261114/final-tester/FINDING.md`](../../build/d1-20261114/final-tester/FINDING.md),
[`summary.json`](../../build/d1-20261114/final-tester/summary.json),
[`execution-receipt.json`](../../build/d1-20261114/final-tester/execution-receipt.json),
and [`MAIN_CENSUS.json`](../../build/d1-20261114/final-tester/MAIN_CENSUS.json).

The gateway is a Windows executable above the unchanged B4 public-DDS SDK.
It monitors one to eight numeric UA NodeIds, accepts scalar UInt32 values with
exactly Good status, and assigns the value to `Standards::BoundedRecord`'s
`sequence_number`. Each configured item supplies a constant unique DDS key and
an ASCII label of at most 64 bytes; the payload is empty. This is a finite
instance of the scalar and constant field assignment rules in DDS-OPCUA 1.0
sections 8.2 and 8.4. Arrays, events, arbitrary types, XML configuration and
Service Sets are unsupported. A missing item, unsafe type, bad/uncertain
quality, or local queue overflow causes an explicit failed run.

The direction key must be `ua_to_dds`; reverse direction is rejected. The
strict line-based profile rejects unknown/duplicate keys, bounds configuration
size and all time/queue/item values, and permits only numeric loopback UA
endpoints with SecurityPolicy None. This is a local development/host profile,
not a remote authenticated UA deployment. DDS uses B4's explicit UDPv4,
reliable KEEP_LAST depth 16, data sharing disabled, and XCDR1 or XCDR2.

UA calls run on one thread. Data-change callbacks copy into a fixed allocation
queue and never call DDS. A separate worker consumes FIFO records. Failed
writes are explicit errors; callbacks never throw across the C boundary. On a
lost connection, including transient transport/session failures during setup,
the gateway deletes the old client and recreates a Session,
Subscription and all MonitoredItems after the configured delay. Retries and
run duration have finite limits. UA subscriptions are not durable queues:
changes during an outage cannot be reconstructed, and a new subscription can
repeat its initial value. No end-to-end exactly-once guarantee is made.

Shutdown checks a stop file between bounded operations. A synchronous UA
request may finish its configured timeout before stop/deadline is observed;
`run_ms` is a supervision budget, not a hard wall-clock deadline. Shutdown ends UA production, deletes
the client, drains the local queue on normal stop, joins the worker, checks
DDS acknowledgment (one fixed 5-second wait) and releases samples/endpoints. On failure queued records
can be discarded with an explicit count. A close failure permits one
close-only retry and remains a failed run if ownership is retained.

`scripts/standards/build_ua_gateway.py` builds pinned unmodified open62541
1.4.21 (`v1.4.21`, exact commit pinned in the builder and build provenance)
from its verified source archive. The independent UA fixture has its own
CMake project and links only open62541; the plain DDS reader links public DDS
and generated type support, without gateway/B4/UA logic. Source snapshots,
upstream source provenance, commands, child exits and artifact hashes are
retained in each fresh build directory. Only DDS dependency-discovery CMake
arguments are accepted; ABI, generated input and UA prefix settings are locked.
Both CMake projects verify the actual imported static UA library and headers
against that prefix and record the resolved path/hash. `run_ua_acceptance.py` exercises both
exact DDS ABIs and audits actually loaded DDS modules.

Sources: [OMG DDS-OPCUA 1.0](https://www.omg.org/spec/DDS-OPCUA/1.0/PDF),
[open62541 client](https://open62541.org/doc/v1.4.21/client.html),
[subscriptions](https://open62541.org/doc/v1.4.21/client_subscriptions.html), and
[versioned source](https://github.com/open62541/open62541/tree/v1.4.21).

## Acceptance and review status

The final tester built both exact Fast DDS ABI variants (3.6.2 and 2.14.5),
ran the ABI CTest contracts and the bounded acceptance matrix, and retained all
child command, process, module, write and notification observations. The
independent UA server was built separately without DDS. These are measured
final-run claims; see the linked finding, receipt and census rather than
treating them as generic conformance counts.

The regression suite and strict documentation check passed; exact counts and
declared-digest details are retained in the canonical final tester records.
The review-native [`FINDING.md`](../../build/d1-20261114/review-native/FINDING.md),
[`CLOSURE.md`](../../build/d1-20261114/review-native/CLOSURE.md), and
[`closure.json`](../../build/d1-20261114/review-native/closure.json), together
with the same-named review-evidence records, close the earlier P2 findings with
no new actionable findings. The implementation snapshot verifies the pinned,
unmodified open62541 v1.4.21 source and the resolved imported static library,
include directory and hashes for both ABI builds. The independent UA server
links only UA; the plain DDS probe uses public DDS and generated support and
contains no gateway, B4 or UA logic.

## Operational usage and limits

Invoke the gateway as `ua_dds_gateway.exe CONFIG` (there is no `--config`
switch). A complete starting profile is
[`integrations/standards/opcua_gateway/profile.example.cfg`](../../integrations/standards/opcua_gateway/profile.example.cfg);
all keys in that profile are mandatory. The caller chooses a fresh local
`stop_file` and creates it to request stop. Stop-file shutdown releases owned
UA/DDS resources through the normal shutdown sequence; there is no automatic
garbage collection or RPC control surface. `run_ms` is a supervision
budget, not a hard wall-clock deadline: a synchronous UA request may finish its
configured timeout before stop/deadline observation. The builder's
`--help` output and `FINAL_BUILD_COMMANDS.json` are the supported command
references.

The profile is deliberately finite: local numeric `127.0.0.1`, SecurityPolicy
None, `ua_to_dds` only, one to eight numeric NodeIds, scalar UInt32 with Good
quality, constant unique keys and ASCII labels. Reverse direction, remote
authenticated deployment, arbitrary UA types, arrays, events, XML
configuration, Service Sets, durable outage queues, exactly-once notification,
and unsampled outage updates are outside this milestone. A loopback fault
proxy used by tests closes exactly one connection during a
CreateSubscription/CreateMonitoredItems request; it is
test-only and is not DSoftBus socket fault injection or a production proxy.

## Capability status

| Dimension | State |
| --- | --- |
| Implementation | PRESENT — bounded UA client/subscription/reconnect path above unchanged B4 public DDS |
| Build | PASS — fresh 3.6.2 and 2.14.5 ABI builds; all child commands exit 0 |
| Host test | PASS — final tester acceptance, ABI CTest contracts and regression; exact counts are in the canonical finding/census |
| Review | CLOSED — [`review-native/CLOSURE.md`](../../build/d1-20261114/review-native/CLOSURE.md) and [`review-evidence/CLOSURE.md`](../../build/d1-20261114/review-evidence/CLOSURE.md), with matching closure JSON records |
| Device / RK3588A / DSoftBus | N/A — D5 is a separate milestone; host success is not device evidence |
| Full DDS-OPCUA Basic / Complete | NOT VERIFIED — D1 is only the bounded UA→DDS host profile; D2–D5 remain PLANNED |
