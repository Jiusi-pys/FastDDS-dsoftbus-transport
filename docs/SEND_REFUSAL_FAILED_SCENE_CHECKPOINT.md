# Send refusal and failed-scene checkpoint

Status: **CURRENT BOUNDED HOST ACCEPTANCE VERIFIED**. The archived R2
19/20 execution remains historically unresolved and is preserved below; the
current frozen Candidate 3 host contract has a separate strict 20/20 result.
This is a bounded host checkpoint for the working tree
based on `e0824d75a2730f41261c14f79aba56b135d94db9` (parent
`cab0dcfb89520acacb1a0c6fd2d4763ea8dbc62e`). It does not reopen the earlier
abort/provenance closeout, and it does not qualify device or production use.

## Current diagnosis

| Question | Evidence-backed result |
| --- | --- |
| Refusing layer | The historical archived layer remains **INFERRED/UNKNOWN**. New deterministic host controls observe a local refusal in `PublicSocketBackend` before native `SendBytes`; this is a current mechanism, not a reconstruction of the archived run. |
| Executed predicate | The deadline control observes `state_reason=128` after provider admission. The separate terminal failed-scene control observes `state_reason=8` after the original Host outgoing socket terminates. These predicates are kept separate. |
| Native API entry | Both controls independently report `api_called=false`, `api_returned=false`, no native result, and no `SendBytes` entry. Entered-call failure controls remain separate and retain their error latch. `api_called=false` is not a universal success exemption. |
| Contract/progress | Expiry of the original deadline permits refusal of that operation. The repaired defect was the old generic catch that faulted the original owner and blocked later valid work on the route. The terminal negative scene remains an operational failure by contract; cleanup does not become business success. |
| Closing and joining | The original validated ticket is settled by `NativeSendInvocation`; the control-side close elects the original `BridgeOwner`. `CloseLocal` joins the pump and `StopLink` joins Host accept/read threads. The negative scene also checks owner release, preserved runtime error, and bridge module unmapping. Retained observation metadata is not live thread ownership. |
| Final-joiner gap | The exact documented gap is omitted public participant deletion/control close. The notice-only contract test retains ownership until explicit `after_delete`; ordinary failed-startup returns use existing close paths. Arbitrary escaping partial-DDS-construction exceptions retain an uncertain owner and remain an unverified lifetime design boundary. No universal last-resort joiner was added. |

## What is implemented and observed

The deadline-only repair uses a typed pre-entry refusal and settles the
original ticket without calling `Complete(0)`, faulting an otherwise usable
owner, or erasing an existing fault. Entered native failures and throws remain
latched. Bounded sender and receiver observations preserve original scalar
admission/decision facts, use explicit saturation or capture-drop accounting,
and emit after ownership gates are released. They do not persist payloads,
reconstruct physical packet identity, or backfill the archived refusal.

The single end-to-end negative scene drives the real terminal predicate. Its
raw result is A=0/B=5 with business progress `P=0`; the expected-negative
checker passes 15/15. It independently verifies the absent native entry/result,
original ticket failure, close decision, pump and Host joins, preserved runtime
error, and both bridge modules unmapped. The child exit and expected-negative
interpretation remain separate. No timeout, forced termination, access
violation, or watchdog result is accepted as cleanup success.

The runner's primary-failure detector now recognizes `ROS_FAILURE` from either
stdout or stderr and publishes the existing failed-window control without
overwriting cleanup state. Its four focused tests pass. The missing sample in
the supported current fragmented run did not recur in two finite R3 diagnostic
runs, so those green runs do not establish its cause or an end-to-end cleanup
claim for that earlier current-artifact missing-sample execution.

## Secure fragmented last-sample disposition

The original R2 failure is retained as an independent historical attempt:

| Item | Evidence-backed result |
| --- | --- |
| Failed attempt | `build/send-refusal-e0824d7-20261010/final-independent-r2/.../attempt-l4je092q/case-j9b364dz/run` |
| Direct last-sample evidence | B's real Writer GUID and `ROS_WRITE` ordinal 5 are present; A has valid receipts only for ordinals 1--4 and its application predicate emits `ROS_FAILURE stage=LEDGER reason=SAMPLE_SERVICE_INCOMPLETE`. |
| Exact DDS identity | Target DDS sequence number, serialized sample size, fragment range, original ticket/deadline, target payload hash, and target reassembly result are **UNKNOWN**. The old record cannot establish a unique callback or transport loss location. |
| Historical conclusion | `HISTORICAL_CAUSE_UNRESOLVED`; no later Writer GUID, sequence, or observation is backfilled into this attempt. |

The old `ROS_FAILURE stage=LEDGER reason=SAMPLE_SERVICE_INCOMPLETE` marker
bundles the application's `bad`, `taken.size()` and service-completion
conditions. It does not expose those scalar values, so it cannot uniquely
prove that no callback occurred; an invalid or duplicate callback setting
`bad` remains possible. It does establish that the unchanged application
oracle did not accept ordinal 5.

The bounded Candidate 3 current host evidence exercises the same supported
secure fragmented contract with the actual wire sample (`sample=70041`,
`fragment=65356`, ranges 1--2), and records B's SDK send, A ingress, the real
RMW sequence, and the validated payload (`data=70032` with its hash). The
application ledger and callback evidence complete strict 20/20 in the fresh
normal and late-release scenes. This establishes the current supported host
path and detection boundary; it does not recover the old ordinal 5.

The new receiver `capture_dropped` value is diagnostic observation loss from
the bounded try-lock observer. It is not a data rejection, and it is not added
to receive refusals, Host callback rejection, native errors, or application
sample counts. Missing observation coverage cannot prove that an action did not
occur. The application callback/ledger emits the accepted ordinal, payload
validation and target hash. `HostReceiveObserver` records only original
binding/adapter receive-decision metadata, including bad/mask fields where
that decision exposes them; it does not independently certify the application
callback, RMW take, pool allocation or physical packet. Its timing overhead is
bounded but not zero.

The normal controller waits for both sides' exact ledger completion and then
uses the existing teardown barriers. A mutation changing `BEST_EFFORT_DONE`
from all sides to any side reaches the intended completion invariant and fails
with both child processes exiting safely; it does not turn a cleanup result into
business success. A separate drop-last negative remains 19/20 with safe
cleanup. No retry, ACK, pacing change, timeout expansion, or QoS change was
introduced.

## Verification status

Historical evidence is recorded under
`build/send-refusal-e0824d7-20261010/`; current Candidate 3 evidence is
recorded under `build/secure-fragment-be-last-20261011/`:

- Host bridge and application rebuilds: **PASS**.
- Current final tester (`final-tester/FINAL_TESTER_RESULT.json`): **PASS**;
  fresh basic/normal/late-release scenes satisfy strict 20/20 and the
  drop-last scene is an expected negative.
- Candidate 3 review (`review/CANDIDATE3_RESULT.json`): **CLOSED**, 24/24
  checker units and no actionable finding.
- Current helper/fixture checks and targeted barrier/drop controls: **PASS**;
  the completion mutation reaches the intended invariant and is rejected.
- Post-review independent terminal negative scene: **PASS**, expected-negative checker
  15/15; raw children A=0/B=5; business result FAIL, P=0.
- M/B/N boundary, huge, count and budget checks: **PASS**.
- Core deadline/ownership evidence is reused from the prior scope: MSVC ASan
  **55/55**; guard WSL plain/ASan/UBSan and the R3 observer ASan/UBSan are
  retained with their stated source-equivalence and existing no-destructor
  leak-check limitation. TSan is **NOT_RUN**.
- API23 transport compile/link is reused from `API23_SOURCE_REUSE.json`:
  **23/23 source identities match**, compile/link only; no target execution.
- Independent Candidate 3 review closed with no actionable finding (24/24
  checker units); it retains the historical-cause limitation and the existing
  partial-construction lifetime boundary.
- Devices and HDC: **NOT_RUN / DEVICE_NOT_ADMITTED**.

The prior R2 artifact remains **HISTORICAL_UNRESOLVED**, because the supported
secure 70KB fragmented scene in
`build/send-refusal-e0824d7-20261010/final-independent-r2/CURRENT_BRIDGE_FRAGMENT_FAILURE.json`
delivered 19/20 samples and missed B's best-effort ordinal 5. Its cause is
unexplained: the saved run has no backend refusal or pump-failure marker that
identifies the missing sample. The current Candidate 3 fresh normal and
late-release scenes pass strict 20/20, and the final tester reports the scoped
host acceptance gate PASS. The historical failure is not rewritten as a
current failure and is not treated as causally closed. Project pre-commit still
requires the parent agent's final preservation, graph, whitespace and hook
checks; no commit is recorded in this document. Production M/B/N remain
`NEEDS_PRODUCT_VALUE`; the profile remains `TEST_ONLY_BOUNDED`; PF/P4 and
qualification status are unchanged.

Historical refusal attribution remains **INFERRED/UNKNOWN**. The prior wider
suite remains the recorded 608/609 with its locale failure; it is not converted
to an all-pass result here.
