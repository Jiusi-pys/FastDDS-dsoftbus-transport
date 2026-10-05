# A5/C1 bounded native reaccept acceptance — 2026-10-05

## Result

`BOUNDED_NATIVE_REACCEPT_VERIFIED`

`ROS2_SAME_ROUTE_RECONNECT_OBSERVED`

`API23_REACCEPT_BUILD_VERIFIED`

`NOT_PRODUCTION_QUALIFIED`

This acceptance covers the explicitly enabled, finite normal-close experiment
on the real DSoftBus public Socket path. It does not qualify the default
backend or the full reconnect requirement.

The source baseline is full commit
`b4bd67d00f436beb107d9f324932b74c81641359`. Current working-tree and package
provenance are bound through the public evidence index
`build/a5c1-native/final-independent-gate/TEST_RESULT.json`, the API23/package
receipts and `build/a5c1-native/commit-allowlist.json`; the target experiment
uses the real `DSoftBusProvider` Kind with its explicit experiment option
defaulting OFF. This document does not claim that a bare b4 tree was executed
without the reviewed working-tree/package inputs.

## Scope and identity continuity

Two fresh two-board windows were independently executed and restored:

| Order | Run / source receipt | Acceptance / review receipt |
| --- | --- | --- |
| A outgoing-first | `6c66b902d5c05f36`, iteration 34/data 19 | `INDEPENDENT_BOUNDED_NATIVE_ACCEPTANCE.json` SHA `42e424239a7c184d6667da6b1fecff4727a43de16d9c62c93b375a9bf52ca1a8`; independent review SHA `abd97d34ae502269cc0d5e6688a61e1e58b281286ced30e6f7a2b56e5190caae` |
| B accepted-first | `d533f8880e53a5e4`, iteration 37/data 20 | `INDEPENDENT_BOUNDED_NATIVE_ACCEPTANCE.json` SHA `525fb7a49d1103663b642322c1e4cd0961ba033385f76f5e397bf9e0a07471e5`; independent review SHA `4c61f8378bc4f233f4d4230eea30b892421c428051c11d70e247bd51aa64f59c` |

Within each fresh window, process, Runtime, Participant, Writer/Reader GUIDs,
service/client, listener and entries remained continuous. The V1 locator
encoding, complete locator selection and service-name contract were unchanged.
The SDK supplied distinct replacement IDs through the actual Socket,
OnNegotiate and OnBind path. The per-window endpoint mapping is:

| Window | A outgoing | B accepted | A accepted | B outgoing |
| --- | --- | --- | --- | --- |
| A outgoing-first | SDK `2 -> 4`, owner `4 -> 61` | SDK `2 -> 4`, owner `4 -> 80` | SDK `3`, retained | SDK `3`, retained |
| B accepted-first | SDK `2 -> 4`, owner `4 -> 80` | SDK `3 -> 4`, owner `7 -> 98` | SDK `3`, retained | SDK `2`, retained |

Each listener remained ID `1`. IDs and owner snapshots are process-local; they
are not cross-device global identities. Adapter incarnation is not claimed to
be an SDK event epoch.

## Business and lifecycle evidence

Each window used the frozen PRE/POST ledger: PRE 8 writes and 8 takes, then
POST 12 writes and 12 takes after the new owner was published, totalling 20/20.
POST delivery was observed through the real ROS callbacks and correlated with
the replacement native path. Graph and service controls remained valid.

Both windows passed the recorded normal cleanup: original DDS deletion `0`,
Runtime Close `0`, public return `0`, normal context/application exit `0`,
pump join, load, collector, bytes, security attributes, DENIED, process guards
and restoration. Shutdown responsibility was asymmetric by design: A's outgoing
first order used local old-owner Shutdown while B supplied the genuine callback;
B's accepted-first order used local old-owner Shutdown while A supplied the
genuine callback. Shutdown entry/return, OnShutdown and adapter activity
settlement are recorded separately; SDK quiescence remains `NOT_PROVEN`.

## Build, tests and review

The final independent execution receipt records current Release 27/27 and
current changed `PublicSocketBackend.cpp`/parser ASan supplement 27/27 as
PASS. The historical ASan 126 selection is reused only for its unchanged
scope and retains two explicit death exclusions; it is not a blanket current
126/126 result. CJS nine commands, Python five suites, eight replay cases and
adversarial Host 9 cases per order all passed. API23 OFF/ON TargetBridge
compilation, linking and consumer provenance were reused with hash
verification. The public commit allowlist contains 52
paths, of which 49 are source/test paths and 3 are documentation paths; private
deployment inputs, SDK/DSO files, package artifacts, raw logs, subrepositories
and unrelated dirty files are excluded. The independent graph review found no
unresolved blocking findings, while retaining HIGH risk for shared lifetime and
authorization/privacy concerns. Its graph proof hash is
`f97f95934d0ed76d96d8c007fac632d060f98ede4b13e1d12e307cb79ce37c26`.

The old 126-case ASan selection, split historical Host coverage and original
V1 device receipts remain historical evidence within their original scope.
The 577 registered cases were covered across runs, not by one 577/577 green
run. Earlier data17 preparation failure, data18 PRE_READY observer failure
with RuntimeClose 2, and iterations 35/36 refusal/id-mismatch evidence remain
unchanged and are not retrofilled.

## Formal requirement disposition

| Requirement | Disposition |
| --- | --- |
| Controlled normal same-route replacement | VERIFIED for the two selected orders |
| Original ROS entities, listener and V1 locator retained | VERIFIED within each fresh window |
| Replacement Socket/Bind via existing control after explicit peer-ready gate | OBSERVED and VERIFIED in this bounded experiment |
| POST business through replacement | VERIFIED, 12 writes/takes after publication per window |
| General unassisted automatic recovery | NOT MET |
| Peer process restart | NOT MET |
| Same numeric ID reuse | NOT MET; ambiguity remains rejection-safe |
| Fault cancellation, SDK epoch/drain and hidden old events | NOT MET / NOT PROVEN |
| Production qualification | NOT MET |

The default real backend remains `NO_REACCEPT`. PF/P4 remain unchanged. No
platform policy, SDK, firmware, system library, original RMW/rcl/rclcpp
implementation or QualificationProfile was changed or generated. The approved
concrete initiator deployment names remain explicit controlled inputs for future
revision; no run/side/role naming scheme was introduced.

## Evidence index

The authoritative execution summary is
`build/a5c1-native/final-independent-gate/TEST_RESULT.json` and its text
counterpart. The device review index is
`build/a5c1-native/final-independent-gate/final-device-review-audit.json`.
The source/graph review is
`build/a5c1-native/final-source-graph-review.json`; the public path allowlist
is `build/a5c1-native/commit-allowlist.json`. The two raw acceptance and review
receipts are retained under the controlled `.artifacts/ros2-device-acceptance`
tree named in the table above.

The final execution gate is PASS and the documentation is ready for the root
agent's separate index, hook, signing and commit checks. This document does
not authorize a commit or change any Git state.
