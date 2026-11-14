# Remaining standards scope: implementation plan

Date: 2026-11-14. Decision record: on 2026-11-10 the four items below were
excluded from that session's delivery scope; on 2026-11-11 the project owner
reopened all four for implementation. This plan is the execution brief. It
does not change any recorded status: capabilities without their own evidence
stay PLANNED.

Baseline: branch `codex/dds-standards-product`, commit `366b6aed`. What
already exists and is verified: the UDP type probe and evidence runner, bounded
IDL generation via Fast DDS-Gen (one keyed fixture), the generated C++
roundtrip (host + both boards), the gated 2.14.5 type-consistency patch
(DEVICE_TESTED, default off), the delivered bounded A2 repair (final host and
dual-board evidence, default off), dsb_suite full-suite DSoftBus evidence for
2.14.5/3.6.2, and the stock mixed-version result: **XCDR1 interoperates
2.14.5↔3.6.2, XCDR2 does not** (both directions fail; known 2.x/3.x wire
divergence). A2.0 is now delivered as a finite, approved, default-off Fast DDS
2.14.5 framing repair with final host and dual-board evidence; its scope and
limits are recorded in [`A2_XCDR2_INTEROPERABILITY.md`](standards/A2_XCDR2_INTEROPERABILITY.md).
The C1 implementation was validated from branch commit `a959f240` and is
recorded separately below as a bounded host REST profile.
The documentation linter `scripts/standards/check_docs_consistency.py`
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
   - Stock/off XCDR2 remains non-interoperable between 2.14.5 and 3.6.2;
     A2's finite repair does not generalize beyond its documented profile or
     establish mixed-version XCDR2 over DSoftBus.
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
- **A2 — mixed-version XCDR2 reconciliation — DELIVERED, bounded.** Root cause
  was the legacy Fast DDS 2.14.5 DynamicData XCDR2 path omitting the DHEADER;
  both Fast CDR controls encode the framing correctly. The approved repair is
  therefore the finite Fast DDS 2.14.5 patch
  `patches/fastdds/2.14.5/0002-dynamic-xcdr2-framing.patch`, not a Fast CDR,
  Transport or general RTPS change. The exact gate, supported profile,
  rejection limits, rollback and evidence are documented in
  [`A2_XCDR2_INTEROPERABILITY.md`](standards/A2_XCDR2_INTEROPERABILITY.md) and
  `build/a2-20261112/`. Final host and dual-board testing passed, review was
  resolved, and the final tester recorded Commit gate PASS. Historical stock,
  gate-off, overlay/replay and forced-validation controls remain separately
  labelled; this milestone does not claim general XCDR2/XTypes conformance or
  mixed-version XCDR2 over DSoftBus.
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
- **B1 status — HOST_TESTED PASS (bounded core only).** The host Python
  preprocessor/parser, AST, name resolution and diagnostics are present and
  covered by the canonical final tester. Review findings R1–R3 are closed.
  See [`B1_IDL_FRONTEND.md`](standards/B1_IDL_FRONTEND.md) and the retained
  [`build/b1-20261113/final-tester/FINDING.md`](../build/b1-20261113/final-tester/FINDING.md)
  plus its `summary.json`; this does not claim full IDL 4.2 conformance,
  non-ASCII identifiers, bit-exact IEEE folding, or any backend/mapping.
  Numeric semantic/precision behavior is intentionally outside the B1 mode;
  it is covered by the separate B2 frontend mode below.
  Native/device and DDS/DSoftBus integration are N/A for B1 and begin at the
  applicable B3/B4 gates.
- **B2 status — HOST_TESTED PASS (bounded frontend/schema-2 scope).** The
  Windows-host Python frontend covers all 15 building blocks, six annotation
  groups with 24 signatures, and the seven §9 named profiles, including the
  numeric/precision, Latin-1, offline explicit repository-source binding and
  AST/schema-2 controls listed in [`B2_IDL_LANGUAGE_COVERAGE.md`](standards/B2_IDL_LANGUAGE_COVERAGE.md).
  The canonical final tester recorded 33 B2 tests, 16 B1 compatibility tests,
  306 compiler-backed cases, 20 semantic probes, 93 standards-regression
  tests and 22 XCDR2-regression tests as PASS; review R1–R5/C1 is CLOSED. See
  [`build/b2-20261113/final-tester/FINDING.md`](../build/b2-20261113/final-tester/FINDING.md)
  and [`build/b2-20261113/review/C1_CLOSURE.md`](../build/b2-20261113/review/C1_CLOSURE.md).
  This is host frontend evidence only: no native/generated/DDS/DSoftBus or
  RK3588A/device artifact is claimed, and exhaustive IDL certification is not
  inferred. The historical initial census failure and rerun remain retained
  without changing the historical census numbers.
- **B3 — DDS backend, C++ first — DELIVERED WITHIN BOUNDED SHARED SUBSET.**
  The existing generated-C++ shape is emitted and independently validated
  against pinned Fast DDS-Gen output, host DDS, combined and legacy CTest
  lanes, and exact cross-board UDP scenarios on both boards. Raw anonymous
  nested collections containing named elements (`E_BACKEND`) require the
  measured 3.6.2 named-inner-collection typedef boundary; the SDK and pinned
  generator also reject the raw form. The 2.14.5 data path supports the raw
  form and B2 semantics are unchanged. See
  [`B3_CPP_BACKEND.md`](standards/B3_CPP_BACKEND.md).
- **B4 — C mapping — DELIVERED, bounded SDK milestone.** Opaque handles,
  explicit ownership, error codes and close-only retry semantics are present.
  The hand-written C11 SDK maps the keyed `BoundedRecord` through B3 support;
  it is not a generic IDL-to-C generator, a complete OMG C mapping, a complete
  QoS/loan/lifecycle surface, or a claim that all B3 types are mapped. Fresh
  exact-ABI host and OHOS builds, ownership/close-failure tests, four host UDP
  pairs and the canonical dual-board DSoftBus matrix pass. Review R1/R2 is
  CLOSED. See [`B4_C_MAPPING.md`](standards/B4_C_MAPPING.md) and its linked
  final host/native findings. Close-failure fault injection is synthetic
  fake-Runtime/helper coverage, not physical Socket failure injection.
- **B5 — Java/JNI mapping — PRESENT, tested bounded Windows host milestone.** The
  hand-written `BoundedRecord` facade over unchanged B4 is present with one
  exact ABI per JVM. Fresh 3.6.2 and 2.14.5 builds pass 9/9 CTest contracts
  each; final acceptance passes 12 exact Java↔independent-B3 C++ lanes (same
  ABI rep0/2 and mixed ABI rep0 only), 24 process exits 0, ten keyed samples
  per Java process, and 42 module-closure records. Reviews and final tester
  PASS; the main pre-commit/commit gate remains pending. Device Java/DSoftBus
  is N/A for this Windows-only scope. This is not a
  generic IDL-to-Java generator or full OMG Java mapping. See
  [`B5_JAVA_JNI_MAPPING.md`](standards/B5_JAVA_JNI_MAPPING.md) and
  [`B4_C_MAPPING.md`](standards/B4_C_MAPPING.md).
- Each of B3–B5 ends with the same milestone protocol; B4's device leg uses
  the dsb_suite deployment layout.

## C. DDS-WEB gateway

Goal: WebDDS object hierarchy, authorization, resource lifetimes,
read/write/wait, notifications; REST and SOAP mappings incl. the API-key
header and XML media types; WS/WSS distinct interoperability tests;
Boost.Beast (HTTP/WS), OpenSSL (TLS), libxml2 (XML/XSD). SIMPLE-REST needs a
documented contract derived from OMG clarifications or a reproducible peer.

Phases:

- **C1 — REST core — DELIVERED, bounded host profile.** WebDDS object model over the public DDS API
  (applications, participants, topics, readers, writers), API-key
  authorization, XML payloads validated against the spec XSDs. Acceptance:
  an independent client (plain `curl`/Python `requests`, not gateway code)
  exercising the bounded REST mapping on the Windows host against a live DDS
  peer process. Final dual-ABI/XCDR1/XCDR2 host acceptance PASS; this is not a
  full DDS-WEB conformance claim. See [`C1_DDS_WEB_REST.md`](standards/C1_DDS_WEB_REST.md)
  and `build/c1-20261113/final-tester/`.
- **C2 — notifications + WebSocket/WSS — DELIVERED, bounded host profile.** Client
  notification endpoints, WS and WSS are implemented with distinct XCDR1/XCDR2
  interoperability lanes. Final independent dual-ABI host acceptance PASS
  (68 C1 regression and 152 C2 executions); see
  [`C2_DDS_WEB_NOTIFICATIONS.md`](standards/C2_DDS_WEB_NOTIFICATIONS.md) and
  [`build/c2-20261113/final-tester/FINDING.md`](../build/c2-20261113/final-tester/FINDING.md).
  This does not claim full DDS-WEB conformance or device/DSoftBus validation.
- **C3 — SOAP mapping — BLOCKED.** The formal SOAP materials referenced by the
  retained 2026-11-13 investigation are unavailable (public URLs return 404 and the
  member artifact returns 401); the owner has no local copies. The beta-1 drafts
  differ and are not a formal contract. No SOAP implementation or SIMPLE-REST
  mapping is inferred from those drafts. See the retained C3 finding records.
- **C4 — device path — PRESENT, bounded and verified.** Board A runs the native
  KaihongOS arm64 gateway and board B an independently built DDS peer over the
  existing Socket Transport; roles are reversed. Windows runs the independent
  REST/WS/WSS clients through a byte-only SSH forwarder. The final matrix replayed
  the bounded C1/C2 suites on both boards and both Fast DDS ABIs. C4 does not
  depend on C3 and does not establish full DDS-WEB conformance. See
  [`C4_DDS_WEB_DEVICE_PATH.md`](standards/C4_DDS_WEB_DEVICE_PATH.md) and the
  canonical final result under `build/c4-20261113/final-retest/`.

## D. DDS-OPCUA gateway

Goal: open62541-based, both directions. UA→DDS: UA client connections,
subscriptions, monitored items, DDS outputs. DDS→UA: types, topics, keyed
instances, DDS entity operations via UA nodes. Complete adds the service
sets through DDS-RPC and historical reads with an explicit retention window.
Independent UA implementations validate service responses, Query, and
historical reads.

Phases:

- **D1 — UA→DDS Basic — PRESENT, bounded host profile.** The Windows AMD64
  open62541 client path has bounded work queues, reconnect state,
  per-direction configuration and shutdown. Final host acceptance PASS covers
  the finite `ua_to_dds` scalar UInt32/Good-quality profile, independent
  separately built UA server and plain public-DDS probe; fresh 3.6.2 and
  2.14.5 ABI builds, acceptance, regression and strict documentation checks
  passed (exact measured results are in `build/d1-20261114/final-tester/`).
  This is not full OMG Basic/type/Variant/
  Event/XML mapping or service conformance. See
  [`D1_UA_DDS_BASIC.md`](standards/D1_UA_DDS_BASIC.md) and
  `build/d1-20261114/final-tester/`. D5 device/RK3588A/DSoftBus validation is
  separate and remains N/A here.
- **D2 — DDS→UA Basic — PRESENT, bounded and HOST_TESTED PASS.** The Windows
  AMD64 server exposes the finite `BoundedRecord` type, one configured topic,
  keyed instances, value reads/subscriptions and bounded lifecycle methods via
  public DDS APIs. Exact 3.6.2 and 2.14.5 builds, independent asyncua client,
  native lifecycle witnesses, reviews and final tester passed. The final
  tester recorded 20 acceptance cases, 46 expected child exits, no forced
  cleanup, 175/175 regression tests and strict documentation checks; see
  [`D2_DDS_UA_BASIC.md`](standards/D2_DDS_UA_BASIC.md) and
  [`final-tester/FINDING.md`](../build/d2-20261114/final-tester/FINDING.md).
  This is not full DDS-OPCUA Basic/Complete or device evidence: D5 owns
  RK3588A/DSoftBus/cross-device validation, N/A for this Windows milestone.
  The bounded five-second public-DDS acknowledgment fence and client budget
  are documented operational constraints; retained unfenced failures remain
  historical evidence.
- **D3 — request/reply groundwork — PLANNED.** Fast DDS exposes no DDS-RPC and the
  board closures ship none: implement the needed request/reply layer on
  public DDS APIs (reuse dsb_suite's `request_reply_related_sample_identity`
  pattern) or adopt an approved library; this is a named dependency, not an
  assumed one.
- **D4 — Complete — PLANNED.** Service sets over D3, historical reads with explicit
  retention; independent UA implementations validate Query and history.
- **D5 — device path — PLANNED.** Same dual-board arrangement as C4, DSoftBus-only
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
