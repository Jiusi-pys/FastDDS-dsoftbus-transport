# C2 bounded notifications and WebSocket transport

Implementation present; final independent acceptance and review gates PASS on
2026-11-13 for the bounded host profile. C2 builds `webdds_gateway_c2`
separately from C1's executable, sharing its public Fast DDS model and bounded
`Standards::BoundedRecord` profile. It changes no Fast DDS/RTPS, transport, QoS
or discovery implementation. This is HOST_TESTED and INTEGRATION_TESTED for
the bounded loopback profile, not full DDS-WEB certification or product
verification.

## Wire contract

The basis is [DDS-WEB 1.0 section 8.5.2](https://www.omg.org/spec/DDS-WEB/1.0/PDF)
and its [resolved DDSWEB-30 clarification](https://issues.omg.org/issues/DDSWEB-30).
The unchanged WebSocket envelope XSD is pinned separately in
`schemas/websocket-provenance.json`. Its official attachment is used because
the old document artifact URL is unavailable. It has no schema imports and
requires no adapter repairs. C1's three original schemas and four explicit
repairs are unchanged.

Clients connect to `ws://127.0.0.1:PORT/dds/v1/connectionName` or the equivalent
`wss` URL and offer subprotocol `dds-web`. HELLO is the first text message, in
one WebSocket frame, containing CRLF-terminated Accept, Content-Type,
OMG-DDS-API-Key and Version fields. Version is 1; the media type is
`application/dds-web+xml`. A valid key receives HELLO_OK; invalid/missing fields
or keys receive HELLO_FAIL and closure. A connection name is unique per
authenticated principal across both listeners. TCP splitting of the HELLO frame
is accepted; fragmented WebSocket HELLO messages are rejected.

REQUEST/RESPONSE correlate their optional id and dispatch the C1 REST profile.
Correlation values are opaque XML strings bounded by the message limit,
including Unicode, punctuation and empty values. Envelope method, URI and
correlation fields apply the schema's whitespace-collapse semantics. A present
empty request id is echoed; an absent id stays absent. B_REQUEST requires an
explicit bind_id element. These rules do not normalize sample body data.
REST-relative resource URIs are accepted; the C1 prefix is also accepted.
Responses use the XSD's symbolic return codes. The schema omits
OBJECT_ALREADY_EXISTS; bounded profile conflicts and quota/input errors map to
INVALID_INPUT. BIND associates reader/writer URIs with bind_id values. An empty
URI cancels the binding. B_REQUEST writes through a writer binding; B_PUSH
delivers data from a reader binding without client polling. BIND and B_REQUEST
have no invented acknowledgement; a subsequent REQUEST is an ordering barrier.

Notification endpoints are the client's active WS/WSS reader bindings. The
service checks key permissions, application ownership/domain grants, and the
original DDS endpoint identity on each use. Reader DELETE/recreate invalidates
the old binding instead of silently attaching it to a new reader. Disconnect,
unbind, expiry and server shutdown release endpoint state. Stale or forbidden
bindings close the connection with a policy error. SOAP AddNotificationEndpoint
and callback WSDLs remain C3 work.

The data body follows C1's bare sample contract. Arbitrary IDL conversion,
write_sample_seq metadata/batch writes, compressed Z_REQUEST/Z_PUSH, binary
messages and QoS changes are unsupported. These bounds are not complete
DDS-WEB conformance. The uncompressed B_REQUEST/B_PUSH and REQUEST/RESPONSE
paths are the C2 acceptance profile.

## Lifetime and limits

Only loopback listeners are exposed. Both HTTP and HTTPS serve the C1 model;
the C1 executable retains its original upgrade rejection. Eight backend workers
perform DDS work while one I/O thread owns sockets. Per connection there are
at most 16 bindings, eight queued commands and eight outgoing messages. Incoming
messages are limited to 64 KiB, command backlog to 256 KiB and outgoing backlog
to 2 MiB. There are at most 32 connections across both listeners.

HELLO and writes have five-second deadlines. Beast owns frame validation,
masking, UTF-8 and control-frame handling. A small stream observer checks only
the initial HELLO frame boundary. The server samples bound readers every
50 ms and takes at most 16 samples per batch. It stops taking while a write or
output backlog is pending. Multiple consuming clients of one DDS reader share
its cache; this is not per-client event fan-out, durable replay, or an exactly-once
delivery protocol. In-flight data may be lost when a client disconnects.

TLS requires an explicit certificate/key pair and TLS listener option, supports
TLS 1.2 or newer, and checks key correspondence at startup. Clients verify the
server chain and hostname. No insecure verification mode is used in acceptance.
Public test-only CA/server fixtures and negative controls are retained in
`tls_test_fixtures/` with their hashes. TLS configuration/schema failures occur
before readiness and do not fall back to a working plaintext service.

## Build and validation

Use C1's exact ABI, B3 generation and dependency arguments. The new target is
`webdds_gateway_c2`; MSVC needs `/bigobj` for the two Beast stream instantiations.
Run with `--policy`, `--port`, and optionally the complete `--tls-port`,
`--tls-cert`, `--tls-key` set. TLS port 0 chooses an ephemeral port. Readiness
reports the actual HTTP and TLS ports. A trusted `--stop-file` or process signal
stops accepts/notifications, drains network callbacks and workers, then checks
DDS teardown. Success requires `C2_SHUTDOWN PASS connections=0 endpoints=0`
and exit 0.

`scripts/standards/run_websocket_acceptance.py` builds each exact ABI afresh,
runs C1 regression, then distinct WS/WSS suites for XCDR1 and XCDR2. Clients use
requests and websocket-client independently of gateway implementation modules,
with native `c1_peer` processes in both data directions. Commands, source hashes,
artifact hashes, TLS decisions and process receipts are retained under
`build/c2-20261113/`. The canonical final tester result is
[`FINDING.md`](../../build/c2-20261113/final-tester/FINDING.md), with fresh
Fast DDS 3.6.2/domain 219 and 2.14.5/domain 223 summaries both PASS. It records
68 C1 and 152 C2 executions, 20 readiness ledgers with no actual-sample retry,
the intentional negative readiness control (reader exit 1, no READY/PASS),
source-unchanged checks, staged raw-XSD checks, and strict documentation PASS.
The interrupted first 2.14.5 run is retained but excluded; the fresh rerun is
canonical. Device/DSoftBus gateway tests remain C4 scope; SOAP remains C3.
Device validation is N/A for C2 and the DSoftBus-backed standards probe was not
run. Overall product status remains NOT VERIFIED.

The C1/C2 acceptance tools use an explicit readiness marker before gateway-to-
native-reader data tests. A subscriber MATCHED receipt alone does not prove that
the writer has discovered that subscriber. The native peer acknowledges receipt
of the marker before the ten actual samples are sent once. Only the separate
marker may repeat, within a fixed 15-second readiness bound; it is excluded from
the data oracle. The reader-ready tool has separate 15-second discovery,
readiness and data phases. Production discovery, QoS and DDS writes are unchanged.
Original failed execution and writer-side match diagnostics remain in the C2
evidence directory; retries alone did not replace that failure.

## Operator rollback

Stop C2 with its trusted stop file or supported process signal and wait for
`C2_SHUTDOWN PASS connections=0 endpoints=0` plus exit 0. Retain the exact ABI
and dependency directories. To roll back, build and run the prior accepted C1
revision/executable; C1 remains HTTP-only and rejects the WebSocket upgrade.
C2 resources are in memory, with no persistent migration; recreate the bounded
profile after restart. The retained TLS fixtures are public test fixtures only;
use deployment-specific certificate and key material in production.
