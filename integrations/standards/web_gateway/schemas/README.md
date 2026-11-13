# DDS-WEB schema provenance

The files in `original/` are unchanged OMG machine-readable artifacts. The C1
REST artifacts' download URLs, resolved URLs and SHA-256 values are in
`provenance.json`; the separate WebSocket envelope artifact is pinned in
`websocket-provenance.json`.
The REST artifact is attachment 12351 to the official resolved DDSWEB-25 issue;
the landing-page/PDF links returned 404 during implementation. The normative
PDF is DDS-WEB 1.0, formal/2016-03-01. Copyright belongs to OMG, RTI, THALES
and the named specification contributors; consult the specification's notices.

`scripts/standards/prepare_web_schema.py` verifies the original hashes and
creates an offline binding set in the build directory. It preserves all named
type constraints except the following explicit PIM/schema conflict repairs:

- XTypes dependency: qualify its otherwise unresolved QNames and align its
  target namespace with the REST schema's import (trailing slash).
- Remove its redundant leading `moduleDecl/include` particle; the following
  repeated `moduleElements` already permits the same include sequence. This
  removes a non-deterministic content model without removing accepted inputs.
- REST `application` and `subscriber`: permit an empty child choice, matching
  the optional contained entities in the PIM. The downloaded schema otherwise
  cannot represent those ordinary lifecycle states.
- Rewrite schema dependency locations to local pinned files; no network load.

The binding adapter adds Table 6's global element declarations over those
named types and list envelopes for registered types and waitsets. It is not a
replacement schema presented as an untouched OMG artifact. Generated files,
hashes and patch descriptions are recorded in `schema-set.json`. The original
compiler failures are retained with C1 build evidence. This adapter and the
bounded data profile do not establish complete DDS-WEB conformance.

The WebSocket envelope XSD (`original/webdds_websockets1.xsd`) is the fourth
original DDS-WEB artifact, resolved from the official DDSWEB-30 attachment. It
has no imports and has no adapter repairs; its raw bytes remain separately
pinned and are not imported into the C1 REST repair set.
