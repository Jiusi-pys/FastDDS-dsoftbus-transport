# Reconnect requirement closeout — 2026-10-05

## Scope and evidence basis

This is a requirement reconciliation for commit
`5e57b2c4a176d7dd7d1f84d2e6af7f99addf4e9d` (parent
`b4bd67d00f436beb107d9f324932b74c81641359`, tree
`6892ae7a00fb88f85ed46e3e7c2709fc223a69fa`). It records existing evidence;
it adds no recovery implementation, test run or device operation. The current
49 source/test hashes, old raw/review receipts and package/API23 provenance
were reused. The final tester receipt records no new build, test, HDC, SDK or
Host program operation.

The original numbered A5/C1 review text was not located in the searched local
sources. Its wording is therefore not reconstructed from later labels. The
normative sources used here are the current V1 plan, the bounded Host contract
and the bounded native admission contract. Current implementation paths are
evidence of observed behavior only; they do not define a requirement. The
independent review passed this reconciliation scope with no blocking finding;
its review receipt records the original numbered source as unlocated and
source-incomplete.

## Normative clauses and disposition

| Normative source / clause | Classification | Evidence and disposition | New code in this package |
| --- | --- | --- | --- |
| [V1 plan](FASTDDS_DSOFTBUS_TRANSPORT_V1_PLAN.md), lines 39 and 51: one outbound and one accepted per endpoint pair, both budgeted; old callbacks do not retarget new connections; close ownership and stable sender resources remain bounded | Current scoped hard invariant | The two retained-entity windows observed the bounded source path and opposite lane. This closes only the recorded normal scope; broad fault, multi-route and production qualification remain open. | No |
| [Host contract](A5C1_BOUNDED_REACCEPT_HOST_CONTRACT.md), line 39, items 2–8: old owner settles before publication; old tickets remain attached; candidates reserve then commit; replacement is driven by the existing control pump | Current scoped hard ownership invariant | The original owner/binding performed one normal END, retained its responsibility, and the existing pump performed replacement. This bounded ownership/entity-continuity subitem is closed on the two original receipts. SDK internal drain and unobserved old-event trajectories remain unproven. | No |
| [Native admission](A5C1_BOUNDED_NATIVE_REACCEPT_ADMISSION.md), line 50: explicit peer-readiness release precedes replacement | Current scoped approved contract | The selected normal-close contract is satisfied as a gated bounded experiment, not assistance-free recovery. | No |
| [V1 plan](FASTDDS_DSOFTBUS_TRANSPORT_V1_PLAN.md), line 38: offline route invalidates and online route resolves again | Broader existing hard requirement | Healthy normal windows do not exercise offline/online re-resolution. It remains unclosed in its original scope and is neither waived nor reclassified here. | No |
| [V1 plan](FASTDDS_DSOFTBUS_TRANSPORT_V1_PLAN.md), line 65, continuity boundary: physical record incarnation, event identity and SDK drain require platform evidence | Platform / qualification boundary | Fixed listener/entry observations do not prove firmware correspondence, physical record incarnation, event epoch or SDK drain. These remain `UNKNOWN/NOT_PROVEN`. | No |
| [Native admission](A5C1_BOUNDED_NATIVE_REACCEPT_ADMISSION.md), lines 24–25, and [Host contract](A5C1_BOUNDED_REACCEPT_HOST_CONTRACT.md), lines 19–24: unknown or retired IDs are rejected; tombstones are not recycled | Safety invariant | Same-ID reuse was not exercised or added. Rejection remains in force; same-ID support is not claimed or added. | No |
| Terminal event alone must initiate replacement with no external readiness release | Not-established extension; not adopted | The original numbered A5/C1 text is unavailable, so this audit cannot determine whether it contains a stronger obligation. The currently located clauses do not mandate assistance-free recovery; no extension is adopted and no global absence claim is made. The behavior is not demonstrated by 5e. | No |

The normative rows above are sourced from approved documents. The implementation
paths used only to verify the observed behavior are
`integrations/ros2/BoundedNativeControl.hpp:96`,
`integrations/ros2/target/TargetBridge.cpp:152`,
`src/ProviderAdapter.cpp:382`, `src/ProviderAdapter.cpp:391`,
`src/PublicSocketBackend.cpp:250` and `src/PublicSocketBackend.cpp:326`.
They show that public terminal callbacks latch terminal state, OnNegotiate
reserves a candidate, OnBind commits accepted ownership, and the existing pump
performs Socket/Bind; they do not create a new normative requirement.

## What the two windows close

The outgoing-first run `6c66b902d5c05f36` and accepted-first run
`d533f8880e53a5e4` retain the original Runtime, Participant, ROS entities,
listener, V1 locator and upper-layer business path. Each has one normal END,
one external `NATIVE_REPLACE` control and two `REACCEPT_DONE` releases. The
existing product pump and binding perform the actual SDK Socket/Bind work; the
external control is a required gate and is not a POST-business permit.

Each window records PRE 8/8 plus POST 12/12, totaling 20/20, with POST after
owner publication. The recorded normal cleanup includes original DDS deletion,
Runtime Close, public return and context/application exit at zero, plus load,
collector, security/bytes, DENIED, process-guard and restoration checks.

Accordingly, these precise subitems are closed for the recorded bounded scope:

- normal same-route replacement through the existing pump;
- continuity of the original ROS entities, listener and V1 locator;
- old-owner settlement before publication and new-owner business delivery;
- normal cleanup and restoration for both selected close orders.

This result must not be labeled `BOUNDED_EVENT_DRIVEN_RECONNECT_*`,
assistance-free reconnect, full A5/C1 closure or production qualification.

## What remains open

No current located hard clause requires removing the approved peer-ready gate,
so no conditional automation branch is activated. The historical statement
that general unassisted recovery is not met is retained as a limitation and is
not used as a new normative source. The original numbered A5/C1 finding
disposition remains source-incomplete until its exact text is available.

Peer restart, same-ID support, arbitrary fault cancellation and a broad fault
matrix are not silently adopted into this package. The existing broader V1
offline/online route requirement remains open. SDK physical record continuity,
event epoch, hidden old-event isolation, drain and source-to-firmware
correspondence remain platform/qualification boundaries.

The default real backend remains `NO_REACCEPT`; PF/P4 and platform policy are
unchanged. `FULL_REQUIREMENTS_COMPLETE`, `SDK_RECONNECT_QUALIFIED` and a
QualificationProfile are not established.

## Evidence index

The corrected requirement matrix draft is
`build/reconnect-closeout-20261005/REQUIREMENT_MATRIX_DRAFT.md`, SHA-256
`9652cc6804998bc786898b08b85b641374a8189f0e6ec01d4e3001427577f63f`. The
independent review is
`build/reconnect-closeout-20261005/INDEPENDENT_REQUIREMENT_REVIEW-r2.json`,
SHA-256 `8132a1c149d897af0824165c26c435ff9824f1bb5e22fc9c858d028fd79a372b`.
The execution reuse receipt is
`build/reconnect-closeout-20261005/native-execution-evidence-reuse/TEST_RESULT.json`
and its text counterpart; it is PASS for controlled bounded subclauses only.

The underlying run and review receipt hashes remain in the existing controlled
acceptance records. Private identities, permissions, routes, claims, SDK/DSO
files, package artifacts and raw log contents are intentionally excluded.
