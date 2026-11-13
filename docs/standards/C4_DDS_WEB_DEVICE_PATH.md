# C4 native DDS-WEB device path

Implementation present; independent review and final independent testing PASS;
the bounded C4 profile is DEVICE_TESTED, INTEGRATION_TESTED and cross-device
verified. This extends the bounded C1/C2 profile to KaihongOS arm64. C3 remains
BLOCKED on its unavailable formal SOAP contract and is not a C4 dependency.
This does not establish full DDS-WEB conformance or overall product verification.

## Architecture and boundaries

The Windows requests/websocket-client/lxml clients communicate with a native
`webdds_gateway_c4` on one RK3588A board. A separately compiled native DDS peer
runs on the other board. The roles are reversed in acceptance. The SSH byte
forwarder reaches the gateway's loopback listeners; it does not parse or
rewrite HTTP, TLS, XML, WebSocket or DDS data. TLS chain and hostname checks
remain end to end between the Windows client and native gateway.

The gateway uses the same C1 public DDS model and C2 network protocol. A
generic participant-transport configuration callback selects the installed
Socket Transport; the default C1/C2 host path remains UDP. Builtin transports,
UDP/SHM fallback and DDS data sharing are disabled in the native DDS profile.
The code changes no Fast DDS, RTPS, discovery, reliability or QoS semantics.
Native DSoftBus permissions/authentication and HTTPS are distinct; no new DDS
Security conformance claim is made.

The native deployment owns one persistent Runtime and its progress thread per
process. A short-lived bootstrap DDS participant coordinates the two listeners
and is deleted before Web readiness. Native sockets remain owned until all
application DDS participants have been deleted and the Runtime closes. This
uses the existing bounded domain-circuit and active/pending-view contract; it
is not unlimited multi-participant or multi-peer admission. The acceptance
tools configure their two application domains explicitly and use the generated
BoundedRecord type. These are C4 application settings, separate from the older
standards-probe domain table; exact values are in the native configuration and
execution receipts.

The independent peer reuses the exact native sample oracle from C1, with its
transport injected independently of the gateway. Its daemon accepts trusted
local test jobs over the authenticated control channel, not a public Web API.
Every actual sample is sent once. Readiness markers and REQUEST ordering
barriers coordinate discovery and bounded unacknowledged WebSocket writes.
The original eight-command bound and separate overload rejection test remain.

## Build and deployment

`scripts/standards/build_web_device.py` generates fresh version-bound B3 code,
then builds the native gateway, peer and read-only public directory query with
the explicit OpenHarmony toolchain. Provide one exact DDS prefix, matching
Stock Transport prefix, Boost 1.87 headers and an arm64 libxml2 prefix per
ABI. OpenSSL 3 comes from the declared native prefix. Canonical libxml2 input
provenance and build logs are retained under `build/c4-20261113/deps/`.

SDK 23 lacks pthread cancellation. Native Boost targets select Boost 1.87's
mutex-backed SmartPtr path with `BOOST_SP_USE_PTHREADS`; synchronization remains
enabled. This deprecated but supported 1.87 option is scoped to native builds,
with no vendored Boost or SDK source patch.

`scripts/standards/run_web_device_acceptance.py` accepts a bundle JSON mapping
each exact version to its build, DDS prefix, Stock Transport prefix and XML
prefix. A new evidence output is mandatory. The default matrix replays every
C1 and C2 client test with both gateway-board roles, same-ABI XCDR1/XCDR2 and
mixed-ABI XCDR1. Targeted mode is explicitly separate development evidence.
Mixed-ABI XCDR2 is outside this acceptance contract.

The HDC control shim is explicit because hardware HDC enumeration is empty on
this setup. Optional `--ssh-map` maps logical board identities to private LAN
SSH addresses; the original logical-board host keys remain pinned. The tools
query live device UDIDs, native permissions and mutual DSoftBus network IDs.
No permission policy, token database, network rule or Transport library is
modified on either board.

The Windows forwarder allocates from the finite 20000..29999 pool, consumes
ports monotonically without reuse, and excludes every observed TCP state plus
both boards' live ephemeral ranges (32768..60999). Allocation and native bind
are not atomic; a later native bind error fails the run rather than retrying or
silently changing the URL port.

Each invocation uses its own `/data/local/tmp/c4_...` tree. Compressed archive
streaming preserves exact artifact bytes and in-root aliases; every deployed
path is SHA-256 checked before use and after the run. Process maps verify that
DDS, Stock Transport, XML and TLS DSOs resolve from the declared ABI closure.
System DSoftBus, token, libc and C++ runtime providers remain platform boundaries.
Native commands, target identities, process receipts and SDK send/receive
counters are retained. Test API-key values are omitted from launch receipts;
the committed TLS keys are public fixtures only.

## Operation and rollback

Use `--profile rest` for the C1 contract, including Upgrade rejection, or
`--profile websocket` for C2. Specify `--transport-config`, `--policy`, trusted
stop-file controls, schema paths and optional complete TLS settings. The
transport configuration contains token, run, local_udid, peer_udid,
peer_network, side, domain, gate and stop fields and is trusted operator input.
Web clients cannot change the native deployment or Token identity.

Request the gateway stop first and wait for its actual exit, DDS deletion,
Runtime close, `C4_SHUTDOWN PASS`, and post-join health receipt. Only then stop
the peer and wait for its exit/EOF and shutdown receipt. Successful shutdown
requires zero Web ownership counters and both process exits; the final run's
ordering keeps gateway DDS drain traffic alive until gateway exit.
Keep dependencies matched to the ABI. Rollback uses the previously accepted
host C1/C2 executables or the previous native revision with its own closure.
Resources are in memory and must be recreated after restart; no persistent
migration exists. Production certificate/key material must replace test fixtures.

## Evidence status

Final independent evidence is retained in
`build/c4-20261113/final-retest/`. It records PASS with 12 ABI/role/
representation lanes, 36 suites and 660 actual client tests (REST 17, WS 19,
WSS 19 per lane), zero failures/errors/skips, unchanged source hashes, 112
verified deployed paths, 708 normal native exit receipts, 60 expected
standalone-negative receipts and 156 native jobs. The matrix covers same-ABI
XCDR1/XCDR2 and mixed-ABI XCDR1 in both gateway-board roles for Fast DDS
2.14.5 and 3.6.2. `device/port-allocation.json` and
`MAIN_DEVICE_CENSUS.json` confirm 1,320 unique common ports, raw TCP inventory
exclusion and exact launch-port multiset equality; `device/summary.json` does
not itself establish the port census. The port allocator is finite and observation
plus later native bind is not atomic; a later bind error fails the run.

Fresh post-review native builds for both ABIs PASS (actual ELF64 AArch64 and
declared DSO hashes); host C1/C2 regression PASS (68 REST and 152 WS/WSS
executions), shutdown contract PASS (four cases per ABI), static 175 PASS with
zero skips, and strict documentation consistency PASS (93 tests, 7 declared
digests). The final execution gate is PASS; normal shutdown requires close/join,
then health, with the C4 pump and close receipts. The original 376/660 device
failure remains retained separately and is not combined with this final run.

Development diagnostics and replay records are retained in
`build/c4-20261113/`, including original upload failures, bounded WebSocket
flow-control diagnostics and the C1/C2 Upgrade contract check. Independent
acceptance is now complete for this bounded profile. The original UDP
standards probe remains separate from this native gateway and peer and remains
NOT_RUN over DSoftBus. C4 is not a full DDS-WEB/SOAP conformance claim, and the
overall product remains NOT VERIFIED while C3 and the other planned scopes are
unfinished.
