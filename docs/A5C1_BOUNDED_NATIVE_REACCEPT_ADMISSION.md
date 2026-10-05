# Bounded native same-route reaccept admission

> **Current status (2026-10-05):** The bounded native experiment is verified;
> see [the final acceptance record](A5C1_BOUNDED_NATIVE_REACCEPT_ACCEPTANCE_20261005.md).
> The design status below is preserved as the frozen admission contract and
> historical pre-run state.

Status: DESIGN UNDER REVIEW; no native reaccept run or SDK PASS is recorded.

Authorization: TASK_SCOPED_BOUNDED_NATIVE_REACCEPT_VALIDATION.
Base: b4bd67d00f436beb107d9f324932b74c81641359.
Parent: bdb1073e18192bce1e904e30517219ddda5e8bc5.
Tree: 4d3ef4fcab7d67cc1ba140d72d4185ce293fdf79.

The existing Host implementation is reused. Its 577 registered cases were
covered across recorded runs, not by one green 577/577 execution. Existing V1
encoding, authorized names, device evidence and unrelated worktree state remain
bound to their original receipts.

| Gate | Required condition and present evidence | Observable native facts | Refusal | SDK boundary still unknown |
| --- | --- | --- | --- | --- |
| A: listener/role | Original listener and fixed public callback entry stay alive. SDK source stores callbacks per named record/role; accepted child shares listener record. | Listener ID, adapter entry, Listen count remain constant; no callback-table replacement. | Listener terminal/failed, role or peer mismatch refuses renewal. | Adapter identity does not attest physical SDK record incarnation; source/installed-library equivalence needs separate evidence. |
| B: retired owner | RequestClose stops only original lane admission. Captured work keeps original lease/ticket; entered activity settles; original close is completed or safely retained. Existing table controls shutdown credit. | Owner incarnation, entered counters, Shutdown void entry/return, and OnShutdown ID/role/reason are separate facts. | Unsettled original owner, lost operation context, provider fault or incomplete close prevents exposure. | Adapter settlement and void return are not SDK quiescence or cancellation guarantees. |
| C: candidate | Actual Socket/OnNegotiate/OnBind supplies ID; peer/role/route match; all owned/prepared/retired/quarantined IDs remain in this process's table. | Fresh distinct SDK ID, candidate commit and new owner incarnation; A/B process domains are independent. | Seen ID, unknown first effect, wrong peer, candidate competition or unresolved old owner is rejected before publication. No ID-forcing Socket loop. | Same-name Socket may reuse a record/session. Outgoing SDK record is different from listener record; its recreation ordering is an explicit pre-device design gate. |
| D: bounded resources | Existing finite owner/candidate/queue budgets and tombstones; one automatic replacement attempt in each fresh window. | High-water values and admission result. | Capacity exhaustion or failed candidate retained, no table recycling or blind retry. | No arbitrary long-lived reconnect or faulty native cancellation claim. |

## Selected normal-close windows

Experimental client-trampoline qualification: the original canonical listener
and server-role callback record remain anchored. The client trampoline is held
for this Runtime's bounded ID-dispatch lifetime across a possibly recreated
outgoing SDK record; all observed IDs and old adapter cells remain retained.
This does not satisfy or replace the production physical-record incarnation
contract in the V1 plan, and does not close G1/PF. It is restricted to this
explicitly selected normal-close/fresh-ID experiment; default real assembly
continues to reject reaccept. No old no-application/rejection work is reassigned.

Source-conditional same-name recreation relies on exclusive ownership and
nondecreasing wall time: sessionAddingCnt protects active creation; service
create updates a timestamp and delayed remove deletes only older records.
The SDK uses gettimeofday, so rollback/read failure remains a concrete limit.
Observe wall time through the finite window and stop on detected rollback or
failure; neither those observations nor these source guards prove firmware
equivalence, SDK incarnation, event epoch or drain. Socket returning a new ID
does not establish physical record recreation.

First window: A requests normal close of its original outgoing through the
original owner/ticket; B observes termination of the corresponding accepted
child under its retained listener. Replacement is driven by the existing
connection control, after old owner settlement and an explicit peer-readiness
control release. HDC supplies only run/phase, never IDs or business payload.

Second window, conditional on first PASS and restored baseline: B initiates
normal close of the accepted child, A observes peer termination of outgoing.
This order needs an admissible outbound-record recreation boundary; an
OnShutdown callback alone must not be relabeled record deletion completion.
Each order has its own fresh run, process, package and restoration receipts.

Both QoS entity sets and service/client are created before PRE and survive POST.
Per side/QoS: PRE ordinals 1-2, POST 3-5. Entire window is 20 writes/20 takes;
PRE totals 8/8, POST totals 12/12. POST is newly written only after new owner
publication. Graph/service are positive controls before and after replacement.
PID, Runtime, Participant, Writer/Reader GUIDs, listener and V1 locator remain;
connection ID, adapter owner incarnation and attempt change. Adapter incarnation
is not an SDK event epoch.

## Planned implementation and gates

Only after this contract is independently cleared: add a default-off native
experiment assembly with real DSoftBusProvider Kind; reuse existing owner,
adapter and binding logic. Host/Fake capabilities remain separate from real
assembly. Normal close must use original shutdown responsibility, without
fabricating an OnShutdown callback. Native-control observations retain bounded
metadata, never private deployment values.

TDD covers default-real refusal, explicit-real selection, owner-scoped close
with in-flight work, fresh/reused IDs, callback-vs-return distinction and
same-entity PRE/POST. Actual API23 compile/link, package/load input consistency,
independent final diff review and restored-device preflight precede native
business. Independent tester/reviewer, final retest, docs sync and exact index
gates precede one necessary local commit only after acceptance closes.

Concrete missing safety evidence blocks the selected path. PF/P4 alone is not
a blanket reason to reject a finite experiment. SDK epoch/drain, process
restart, same-ID reuse, faults and production qualification remain outside this
package. No successful native label is authorized by this design document.

## 2026-10-05 evidence closeout

The selected bounded native experiment is now verified on the registered
two-board SDK public Socket path. This section records the result without
rewriting the frozen admission contract above.

Status: `BOUNDED_NATIVE_REACCEPT_VERIFIED`,
`ROS2_SAME_ROUTE_RECONNECT_OBSERVED`, `NOT_PRODUCTION_QUALIFIED`.

The outgoing-first window was run `6c66b902d5c05f36` (iteration 34, data 19)
and the accepted-first window was run `d533f8880e53a5e4` (iteration 37, data
20). Each fresh window retained its original process, Runtime, Participant,
Writer/Reader entities, service/client, listener, entries and V1 locators.
The actual SDK IDs and adapter owner incarnations changed. Per endpoint and
window, A outgoing-first had A outgoing SDK `2 -> 4` / owner `4 -> 61` and B
accepted SDK `2 -> 4` / owner `4 -> 80`; its retained A accepted and B
outgoing IDs were both `3`. B accepted-first had A outgoing SDK `2 -> 4` /
owner `4 -> 80` and B accepted SDK `3 -> 4` / owner `7 -> 98`; its retained A
accepted ID was `3` and B outgoing ID was `2`. Each listener remained ID `1`.
These numeric IDs and owner snapshots are process-local, not cross-device
global identities.

Both windows delivered 8 PRE and 12 POST writes/takes, for 20/20 per window;
POST samples were written after replacement owner publication. Graph and
service controls remained valid. Public Socket/Bind/SendBytes correlation,
normal DDS deletion, Runtime close and public return 0, pump join, context and
application exit, load, collector, byte/security-attribute, DENIED and process
restoration checks passed. Close responsibility was intentionally asymmetric:
the A outgoing-first order used local old-owner Shutdown on A and a genuine
callback on B; the B accepted-first order used local old-owner Shutdown on B
and a genuine callback on A. Shutdown entry/return, callback facts and adapter
activity settlement remain separate from SDK quiescence.

Controlled receipts and hashes are indexed by the final independent gate and
device audit; private UDID, networkId, token, route, permission and raw log
contents remain outside this document. The final execution receipt reports
Release 27/27 and the current changed `PublicSocketBackend.cpp`/parser ASan
supplement 27/27 passed. The historical ASan 126 selection is reused only for
its unchanged scope with two explicit death exclusions; it is not a blanket
current 126/126 result. Nine CJS commands, five Python suites, eight replay
cases and adversarial Host 9 cases per order passed. API23 OFF/ON consumer
reuse also passed, as did the Commit Gate. The historical Host split coverage
and prior V1/device receipts remain reused only within their recorded unchanged
scope; they do not become a single 577/577 run or reconnect evidence.

The result is limited to the explicit finite normal-close/fresh-distinct-ID
experiment. The default real backend remains `NO_REACCEPT`; PF/P4 and platform
policy are unchanged. Physical SDK record incarnation, SDK event epoch, drain,
hidden old-event trajectories and source-to-firmware equivalence remain
UNKNOWN/NOT_PROVEN. Peer restart, same-ID reuse, fault cancellation, general
unassisted automatic recovery and other formal reconnect requirements remain
unfulfilled. No QualificationProfile was generated.
