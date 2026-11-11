# Remaining standards scope: implementation plan

Date: 2026-11-11. Decision record: on 2026-11-10 the four items below were
excluded from that session's delivery scope; on 2026-11-11 the project owner
reopened all four for implementation. This plan is the execution brief. It
does not change any recorded status: every capability stays PLANNED until its
own evidence exists.

Baseline: branch `codex/dds-standards-product`, commit `15f7981e`. What
already exists and is verified: the UDP type probe and evidence runner, bounded
IDL generation via Fast DDS-Gen (one keyed fixture), the generated C++
roundtrip (host + both boards), the gated 2.14.5 type-consistency patch
(DEVICE_TESTED, default off), dsb_suite full-suite DSoftBus evidence for
2.14.5/3.6.2, and the mixed-version result: **XCDR1 interoperates
2.14.5↔3.6.2, XCDR2 does not** (both directions fail; known 2.x/3.x wire
divergence). The documentation linter `scripts/standards/check_docs_consistency.py`
enforces `docs/standards/` claims against retained evidence.

## 0. Ground rules for every work item

1. **Workflow per AGENTS.md section 11**: IMPLEMENT → independent tester →
   independent review → fix → re-test → docs_updater → PRE_COMMIT gate →
   commit. One milestone, one commit, pushed to this branch.
2. **Evidence discipline**: every PASS traces to a retained file; device
   claims need both RK3588A boards (Tailscale SSH `root@100.97.6.8` /
   `root@100.97.143.74`; `hdc list targets` is empty — reuse
   `build/dsb-exec-20261110/hdc_ssh.bat` as the `HDC` shim; the boards have no
   scp, stream with `cat file | ssh root@<b> "cat > path"`). `COMPILED !=
   TESTED`, `HOST_TESTED != DEVICE_TESTED`, `BLOCKED != PASS`.
3. **Architectural boundary (AGENTS.md section 2)**: the Transport stays
   untouched; gateways and SDKs sit above the public DDS API. Any patch to
   Fast DDS / Fast CDR / Fast DDS-Gen that changes DDS or RTPS semantics
   needs its OWN argued, user-approved section 2.1 exception BEFORE
   implementation — the 2026-11-10 approval covers only the one existing
   gated 2.14.5 EDP patch. Plan each such approval as an explicit checkpoint.
4. **Documentation linter obligations**: new documents under `docs/standards/`
   are linted. Any new hash cited must be declared in
   `DECLARED_DIGESTS`/`DIGEST_CONTEXT` with provenance; new measured claims
   need a guard or must point at a FINDING.md instead of inlining numbers.
   The unit-test count is self-measured: adding tests requires re-measuring
   every `N/N` claim. Checkouts without the machine-local `build/` evidence
   run the linter with `--evidence-optional`.
5. **Known constraints to design around**:
   - XCDR2 is not interoperable between 2.14.5 and 3.6.2: mixed-version
     features must pin XCDR1 or same-version pairs, or the work item must
     include the upstream XCDR2 reconciliation explicitly.
   - Writer-side content filtering must stay disabled wherever CFTs can churn
     (recorded CFT race; workaround-only decision stands).
   - Secure Participants over DSoftBus need transport maxMessageSize ≤ 65535.
   - On the Windows host, avoid DDS domains 226/228/231/232 (reserved UDP
     ranges); boards are unaffected.
   - Fast DDS ships no DDS-RPC implementation on the installed closures
     (verified absent on the boards): item D's "Complete" level depends on a
     request/reply layer (A or custom) being built first.
   - 45 s+ network outages drop a board off DSoftBus; restart
     `softbus_server`, not the board.

## A. XTypes Complete patch set

Goal (plan of record): programming interfaces, Minimal + Basic network
interoperability, XTypes 1.1 interop, XML Data Representation — delivered as
versioned, default-off, out-of-tree patches to Fast DDS / Fast CDR /
Fast DDS-Gen, mirroring the existing `patches/fastdds/2.14.5/...` mechanism
(`FASTDDS_PATCH_DIR`, patch sha256 in `BUILD_INFO.txt`, gate variable echoed
into `results.json`).

Phases, each its own milestone commit:

- **A1 — 3.6.2 type assignability (gated).** 3.6.2's `EDP::valid_matching`
  never reads `TypeConsistencyEnforcementQosPolicy` and has no assignability
  algorithm. Implement XTypes 1.3 §7.2.4 TypeObject assignability for the
  cases the probe already measures (appendable/mutable added member,
  incompatible member, extensibility-kind enforcement), gated by the same
  `FASTDDS_XTYPES_HONOR_TYPE_CONSISTENCY` variable, default off, stock
  behavior byte-identical when off. **Checkpoint A1.0: section 2.1 approval
  for a 3.6.2 EDP patch before any code.**
  Acceptance: host three-way comparison (stock / gate-off / gate-on) on
  3.6.2 reproducing the 2.14.5 pattern; device matrix both board directions,
  gate off and on; extensibility-kind FINAL-vs-wider rejection case added to
  the probe and passing gate-on (closing the disclosed 2.14.5 gap or
  documenting it as upstream-identical).
- **A2 — mixed-version XCDR2 reconciliation (investigation first).**
  Root-cause the recorded XCDR2 failures (delimited CDR / DHEADER handling
  between Fast CDR 2.2.5 and 2.3.7) to a specific wire difference with
  captures; then either a gated Fast CDR patch making one line conformant, or
  a documented, linter-guarded interoperability statement ("XCDR2 requires
  same-line peers") if the divergence is upstream-intended. **Checkpoint
  A2.0: section 2.1 approval required only if a patch is proposed.**
  Acceptance: mixed matrix re-run (the `build/mixed-default-20261111/`
  harness) with XCDR2 either passing both directions or the exclusion
  documented as approved scope control.
- **A3 — XTypes 1.1 interoperability.** Legacy `TypeIdentifier`/`TypeObject`
  (v1) exchange against the 2.14.5 line, measured by new probe cases.
  Acceptance: probe cases on host + both boards, both directions.
- **A4 — XML Data Representation.** This is large: XML serializer/
  deserializer per XTypes Annex C for the supported type set, wired into
  `DataRepresentationQosPolicy` negotiation. Deliver incrementally: A4.1
  bounded fixture types only (the probe's record), A4.2 the generated-types
  set. The `xml` probe case flips from "asserted rejection (documented
  non-conformance)" to a delivery case — update `CASE_EXPECTATION`, the docs
  that call it a non-conformance, and the linter contexts in the same
  milestone.
  Acceptance: `xml` delivery PASS host + device for the fixture type;
  explicit list of unsupported constructs.

## B. Complete IDL 4.2 frontend and C/Java mappings

Goal: all 15 building blocks, six annotation groups, preprocessing, name
resolution, semantic diagnostics; the seven IDL §9 example profiles as
combination tests; C mapping with opaque handles and explicit ownership;
Java per-ABI JNI (Windows is the Java runtime target); C/C++ also target
KaihongOS arm64.

Phases:

- **B1 — frontend core.** Preprocessor (or clang-cl/`cl /EP`-equivalent
  bundled preprocessor with recorded version+hash), grammar for the base
  building blocks, AST + name resolution + diagnostics. Acceptance: a
  conformance corpus under `tests/standards_idl/` with per-construct
  positive/negative cases; diagnostics asserted by test, not by eye.
- **B2 — full building-block and annotation coverage.** The remaining
  building blocks (interfaces, components if claimed, templates) and the six
  annotation groups; the seven §9 profiles compile as combination tests.
  Any block deliberately not claimed is recorded as explicit scope control,
  not silently skipped.
- **B3 — DDS backend, C++ first.** Emit the existing generated-C++ shape so
  the current roundtrip harness validates the new frontend against
  Fast DDS-Gen output for the shared subset (differential test: same IDL in,
  equivalent types out, hash-recorded).
- **B4 — C mapping.** Opaque handles, explicit ownership, error codes;
  real send/receive acceptance: a C writer/reader pair over UDP on host,
  then over DSoftBus on the boards (link against the stock transport).
- **B5 — Java/JNI mapping.** Per-ABI JNI library, Windows runtime;
  acceptance: Java writer ↔ C++ reader (and reverse) real send/receive on
  the Windows host; document that device-side Java is out of scope.
- Each of B3–B5 ends with the same milestone protocol; B4's device leg uses
  the dsb_suite deployment layout.

## C. DDS-WEB gateway

Goal: WebDDS object hierarchy, authorization, resource lifetimes,
read/write/wait, notifications; REST and SOAP mappings incl. the API-key
header and XML media types; WS/WSS distinct interoperability tests;
Boost.Beast (HTTP/WS), OpenSSL (TLS), libxml2 (XML/XSD). SIMPLE-REST needs a
documented contract derived from OMG clarifications or a reproducible peer.

Phases:

- **C1 — REST core.** WebDDS object model over the public DDS API
  (applications, participants, topics, readers, writers), API-key
  authorization, XML payloads validated against the spec XSDs. Acceptance:
  an independent client (plain `curl`/Python `requests`, not gateway code)
  exercising the standard REST mapping on the Windows host against a live
  DDS peer process.
- **C2 — notifications + WebSocket/WSS.** Client notification endpoints, WS
  and WSS with distinct interop tests (OpenSSL certs recorded as fixtures).
- **C3 — SOAP mapping.** Only after C1/C2 are green; SIMPLE-REST contract
  documented separately with its provenance.
- **C4 — device path.** Board A runs the gateway natively (KaihongOS arm64),
  board B a DDS peer over DSoftBus, Windows runs the independent Web client;
  roles reversed. Mind constraint 5 (XCDR1 for any mixed-version pairing).
  Acceptance: the C1/C2 suites replayed against the board-hosted gateway.

## D. DDS-OPCUA gateway

Goal: open62541-based, both directions. UA→DDS: UA client connections,
subscriptions, monitored items, DDS outputs. DDS→UA: types, topics, keyed
instances, DDS entity operations via UA nodes. Complete adds the service
sets through DDS-RPC and historical reads with an explicit retention window.
Independent UA implementations validate service responses, Query, and
historical reads.

Phases:

- **D1 — UA→DDS Basic.** open62541 client side, bounded work queues,
  reconnect state, per-direction configuration and shutdown. Acceptance:
  independent UA server (e.g. open62541 example server built separately,
  version+hash recorded) feeding DDS samples observed by a plain probe.
- **D2 — DDS→UA Basic.** UA server surface exposing DDS types/topics/keyed
  instances; validated by an independent UA client.
- **D3 — request/reply groundwork.** Fast DDS exposes no DDS-RPC and the
  board closures ship none: implement the needed request/reply layer on
  public DDS APIs (reuse dsb_suite's `request_reply_related_sample_identity`
  pattern) or adopt an approved library; this is a named dependency, not an
  assumed one.
- **D4 — Complete.** Service sets over D3, historical reads with explicit
  retention; independent UA implementations validate Query and history.
- **D5 — device path.** Same dual-board arrangement as C4, DSoftBus-only
  between boards, roles reversed.

## Order and dependencies

Recommended: **A1 → A2 → B1..B3 → (B4 ∥ C1) → C2..C4 → B5 → D1..D5 → A3 → A4.**

- A1/A2 first: they are the smallest, unblock mixed-version claims that C/D
  device tests will otherwise trip over, and each needs a user approval
  checkpoint that should not stall later work.
- B3's differential testing de-risks the frontend before any mapping work.
- D needs D3; D3 can reuse B/C infrastructure, so D goes last.
- A4 (XML DR) is the largest single patch-set item and independent of C/D;
  schedule it when a stable window exists.
- Parallelize only across work items with disjoint files (e.g. B and C), one
  writer per area, per the repository's session rules.

## Definition of done, per item (maps AGENTS.md section 15)

- implementation present; build PASS host MSVC + OHOS arm64 where claimed;
- targeted + integration tests pass with retained logs;
- required dual-board RK3588A tests pass over DSoftBus (C4/D5/B4 device legs,
  A's device matrices); cross-device scope stated case by case;
- independent review completed, fixes re-tested;
- `docs/standards/` updated by docs_updater and `check_docs_consistency.py`
  exit 0 (capability-table row flips PLANNED → PRESENT with evidence);
- PRE_COMMIT gate walked explicitly; commit + push per milestone;
- anything not run is NOT_RUN/BLOCKED with the reason, never PASS.
