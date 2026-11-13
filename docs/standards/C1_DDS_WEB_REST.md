# C1 bounded DDS-WEB REST core

Implementation present; final independent acceptance and focused reviews PASS,
2026-11-13. This milestone adds a bounded host gateway over public Fast DDS
APIs. It does not change Transport, DDS/RTPS discovery, reliability, Fast CDR or
the A2 patch. The result is HOST_TESTED and INTEGRATION_TESTED for this finite
profile, not full DDS-WEB conformance or overall product completion.

## Contract

`integrations/standards/web_gateway` builds a Boost.Beast HTTP server using
libxml2 XML/XSD and OpenSSL 3 hashing. Each build selects exactly Fast DDS
2.14.5 or 3.6.2 and consumes B3's hash-checked generated type support. The
independent `c1_peer` links that type support and DDS, without the gateway model
or XML library. The external acceptance client uses Python requests, ElementTree
and lxml, without importing gateway code.

The finite data profile is the keyed `Standards::BoundedRecord`: unsigned
32-bit id and sequence number, a label limited to 64 UTF-8 bytes, and an octet
sequence limited to 256 elements. XML samples use either the XTypes qualified
namespace `ddstype://www.omg.org/Standards` or the well-formed unqualified form;
sequence elements are `item`. Writes accept the bare sample element. Responses
carry the DDS-WEB namespace `http://www.omg.org/dds/` and media type
`application/dds-web+xml`. This is not a general XML-to-IDL compiler or full
DDS-WEB conformance claim.

The `/dds/rest1` hierarchy exposes applications, domain_participants,
registered_types, topics, publishers/data_writers, subscribers/data_readers and
application waitsets. POST creates with 201 and an absolute Location; PUT
validates before the supported no-op update; DELETE returns 204; GET and HEAD retrieve
representations. Duplicate resources return 409. Invalid XML or unsupported
profile options return 422; missing resources return 404. Runtime DDS errors
return 500. Table 5's `participants` spelling is accepted as an alias; returned
locations use Table 4's canonical `domain_participants` spelling.

PUT is limited to an identical logical representation in this fixed-QoS
profile and returns 204 without replacing DDS objects. Changes to topology,
domain, type/topic bindings or conditions are rejected before mutation; use
explicit POST/DELETE for those changes. General QoS and transactional container
updates are unsupported. DDS-WEB entity updates do not generally mean deleting
and recreating the entity. This bound preserves identity, pending reads,
reader history and waitset references. Replacement capacity is checked against
the incoming PUT payload before unsupported changes are rejected.

Successful GET/HEAD responses include Last-Modified and Date. Structural
modifications update the resource and ancestor timestamps; the type catalog
uses its startup timestamp. Read/wait result documents are generated dynamically
and use their generation time. Waiting reads accumulate distinct observed DDS
sample identities across polling and KEEP_LAST rollover, up to the batch cap.
Valid data is deduplicated by immutable DDS `SampleIdentity`; unknown valid
identity fails closed with HTTP 500. Lifecycle fallback identity uses
instance/generation/state and does not synthesize a timestamp; first-observed
metadata is retained.

Data readers support read/take through `removeFromReaderCache`, minSamples,
maxSamples and maxWait. This bounded profile caps each batch at 64 samples and
waiting at two seconds. Nonempty sampleSelector expressions are rejected. DDS
loans are returned within the call. Invalid-data lifecycle events carry metadata
without a fabricated data value. Waitsets support one DATA_AVAILABLE condition
referencing `P::Sub::R::status_condition`. Deleting a reader cancels a pending
read; after reader DELETE/recreate, a waitset requires DELETE/recreate/rebinding
of that waitset. PUT cannot rebind it.
Arbitrary types, QoS overrides, filters and additional condition kinds are
explicitly unsupported. DDS uses reliable KEEP_LAST depth 16, data sharing off,
UDPv4 only, and XCDR1 (0) or XCDR2 (2) per process.

## Authorization and lifetime

Exactly one `OMG-DDS-API-Key` header is required. Trusted local policy maps keys
from environment-variable names to read/write/manage permissions, an ownership
group and allowed DDS domains. Applications are authorization units: a caller
must hold every domain used in that application to access it or its descendants;
collection listings omit applications outside that grant. Key digests are compared in constant time; keys
are not emitted in receipts. Missing/invalid keys return 401, denied operations
403. REST ownership separates resources; domain grants allow DDS communication
with other entities in that domain and are not per-topic data isolation.

Only loopback HTTP is served in C1. TLS, WebSocket/WSS and notifications belong
to C2; SOAP to C3; the native gateway and cross-board DSoftBus path to C4. A
single gateway owns the process-wide libxml2 external loader. Header/body limits
are 8 KiB/64 KiB, with 32 live connections, eight backend workers, bounded I/O
and no keep-alive. XML DTD/entity declarations and external resource loading
are rejected. Runtime schemas are verified against compiled hashes at startup.

Applications expire after configurable idle TTL (5 through 3600 seconds).
Limits include eight principals, eight applications globally, four per owner,
eight participants and 128 tree nodes including the root. Nested creation and
replacement are capacity-checked before any DDS mutation. Cleanup orders DDS dependants before
their owners. Failed cleanup retains ownership and blocks new work; shutdown
failure exits unsuccessfully. A trusted local stop-file or process signal stops
waits, joins workers and checks DDS teardown. No successful shutdown is inferred
from a forced process termination.

## Specification and schema decisions

The mapping basis is [OMG DDS-WEB 1.0](https://www.omg.org/spec/DDS-WEB/1.0/PDF),
with XML data representation from
[DDS-XTypes 1.1](https://www.omg.org/spec/DDS-XTypes/1.1/PDF). Unmodified machine
artifacts, source URLs and hashes live under
[`schemas`](../../integrations/standards/web_gateway/schemas/README.md).
The public REST schema was recovered from official resolved issue DDSWEB-25
because the PDF and landing-page artifact links returned 404.

The offline adapter records four explicit repairs: XTypes QName/namespace
qualification, removal of its redundant nondeterministic include particle,
and empty application/subscriber children permitted by the PIM. Imports are
rewritten to local pinned files. Global element bindings and list envelopes
are supplied separately; sample XSDs constrain this profile. Original schemas
remain unchanged and their compiler failures are retained as evidence.

Table 5 refers to an authenticatedSessionRepresentation absent from the supplied
schema; PIM create_application has no such output. C1 returns an empty 201 with
Location. Its wait result uses a `condition_name_list/item` binding for the PIM
condition-name list. HTTP 204 omits Content-Length as required by RFC 9110,
despite DDS-WEB Table 8's blanket header requirement. These decisions and the
schema adapter are interoperability limits, not untouched normative artifacts.

## Build and verification

Configure `integrations/standards/web_gateway` with `DDS_STANDARDS_FASTDDS`,
`B3_GENERATED_DIR`, `B3_IDL`, `C1_BOOST_INCLUDE_DIR` (Boost 1.87 headers), the
matching DDS package prefix, OpenSSL 3, libxml2 and a Python interpreter with
lxml. Build `webdds_gateway` and `c1_peer`. The example policy gives environment
variable names only; supply distinct keys of 24 through 128 bytes locally.

`scripts/standards/run_web_acceptance.py --help` describes fresh-output build
and acceptance orchestration. Run it once per exact ABI with separate runtime
DLL directories. It generates B3 support afresh, builds, runs schema controls
and requests/native-peer suites for representations 0 and 2, and records
commands, exits, artifacts and unchanged source snapshots in `summary.json`.
Evidence for this milestone belongs under `build/c1-20261113/`. Development
runs are implementation diagnostics; independent tester results, review closure
and final gates must establish the delivered status.

Final evidence is retained in
[`build/c1-20261113/final-tester/FINDING.md`](../../build/c1-20261113/final-tester/FINDING.md),
[`FINAL_RESULT.md`](../../build/c1-20261113/final-tester/FINAL_RESULT.md) and
[`summary.json`](../../build/c1-20261113/final-tester/summary.json). Fresh
Fast DDS 3.6.2 and 2.14.5 runs each completed six commands with exit 0; each
HTTP/native-DDS invocation exercised 17 tests at representations 0 and 2.
The retained static suites report B1 16, B2 33, B3 11, standards 93 and XCDR2
22, with strict documentation consistency PASS. The staged autocrlf export
preserved all three original XSD byte blobs and all 20 source/config paths were
unchanged by validation. Review closures for the PUT/packaging and mapping
findings are under `build/c1-20261113/review/`.

## Operator runbook (bounded C1)

The example policy contains environment-variable names only. Provide local
values for those names; never put real API keys in source, policy examples,
receipts or logs. Start with the exact ABI-matched build and its dependency
directories, then run the independent client against loopback HTTP. Set the
trusted stop-file path (or use the supported process signal) and wait for the
gateway's `C1_SHUTDOWN PASS` receipt and successful process exit before judging
shutdown successful. A forced termination is not a shutdown pass.

For rollback, stop the C1 process cleanly, remove only the local stop file, and
revert/build the previous accepted revision for the components that existed
there. The pre-C1 revision has no gateway or C1 resources to restart; there is
no persistent migration. Use the validated C1 artifact again only when its
matched dependencies are retained. C1 resources are in memory and are not
persisted; recreate the bounded profile after restart. TLS, WS/WSS,
notifications, SOAP, native board gateway and cross-board DSoftBus acceptance
remain outside C1 and are planned/deferred to C2, C3 and C4.

RK3588A and DSoftBus verification are N/A for this host-only C1 milestone; the
required native cross-device acceptance remains deferred to C4. The
DSoftBus-backed standards probe is NOT_RUN. Existing
FastDDS/Transport/DDS/RTPS/QoS behavior remains unchanged.
