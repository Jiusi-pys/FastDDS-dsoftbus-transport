# Route lifecycle host acceptance — 2026-10-06

## Status and scope

The route lifecycle implementation from `b6a2ee8abd009f627eaaf541d796eaac16dfd59c`
(parent `5e57b2c4a176d7dd7d1f84d2e6af7f99addf4e9d`, tree
`06d87141bf32648e5961dc79fba4fb9562924f16`) is implemented and Host-tested
within the bounded route lifecycle scope. This document records evidence; it
does not qualify device behavior, SDK event epochs or production reconnect.

The default route/node feature remains OFF and the default real backend remains
`NO_REACCEPT`. The explicit bounded experiment retains real
`DSoftBusProvider` Kind. PF/P4, production qualification and the missing
numbered A5/C1 source disposition remain unchanged.

## Implemented route lifecycle

On an offline entry, `INodeStateCb::onNodeOffline` flows through the HostLoop
weak callback into the NativeOwner operational-availability/version gate. The
existing native owner retains entered leases; already-entered old work and
queued old work follow their original cancellation/settlement rules. New send
and connection admission for the unavailable route is rejected, including
submissions through the retained SenderResource. Already-admitted work remains
owned by its original ticket/lease and is never retargeted. On a later online
entry, `onNodeOnline` invokes the
actual public `GetAllNodeDeviceInfo`/`GetNodeKeyInfo` path for the expected UDID
and performs a fresh bounded resolver outside adapter internal gates. It
validates the current nonce, peer and V1 identity, rejects directory
collisions and stale/failed resolution without fallback, and preserves the
approved peer-ready `NATIVE_REPLACE` gate. The local route revision is not an
SDK event epoch.

The implementation keeps node registration/unregistration in existing loop
activity accounting, makes node cleanup failure sticky across socket success,
uses owned bounded callback sinks, and avoids public resolver calls while
holding the adapter control gate. A replacement incarnation obtains its own
later offline close responsibility. Fixed callback-entry tombstones are not
reused.

## Verification evidence

The final independent evidence is
`build/route-lifecycle-20261006/final-evidence/FINAL_EVIDENCE.json`; the source
review is `build/route-lifecycle-20261006/independent-source-review.md` and
reports CLEAR with no concrete blocking finding in scope. The final raw-evidence
review is `build/route-lifecycle-20261006/INDEPENDENT_FINAL_EVIDENCE_REVIEW.json`;
it reports CLEAR for Host/API23 compile-link scope, verifies 15 source hashes and
7 artifact hashes, confirms 19 route cases, 219 Release cases, 219 current ASan
cases and two valid functional mutants killed. It also confirms raw R8 events,
POST only after owner publication, close/public returns 0 and API23 public SDK
linkage without Fake/Host symbols.

| Layer | Result | Evidence |
| --- | --- | --- |
| Route lifecycle CTest | PASS 19/19 | `build/route-lifecycle-20261006/final-route-ctest-r6` |
| Affected Release | PASS 219/219 | `build/route-lifecycle-20261006/affected-release-final` |
| Affected ASan | PASS 219/219 | `build/route-lifecycle-20261006/asan-final/retry-buildtools-env` |
| API23 OFF/ON | Compile/link PASS, execution not run | `build/route-lifecycle-20261006/api23/off` and `on` |
| ROS Host new-network | PASS, run `3392f80131a36504`, PRE8 + POST12 = 20/20 | `build/route-lifecycle-20261006/ros-final-canonical/new-network/result.json` |
| ROS Host same-network | PASS, run `9bfd49c4c7d94a9d`, PRE8 + POST12 = 20/20 | `build/route-lifecycle-20261006/ros-final-canonical/same-network/result.json` |

Both ROS runs retained the original ROS/DDS GUIDs, Runtime, Participant,
complete V1 locator and listener continuity. Each performed one fresh node
query and route resolution; the new-network case observed a changed effective
network and the same-network case observed the original network. Factory
participant deletion, public return, Runtime Close and normal process exit were
zero. The evidence records SDK epoch and drain as `NOT_PROVEN` and identifies
the Host source as an opaque public API shape.

API23 OFF and ON artifacts are ELF64/AArch64, link the real public node imports
and DSoftBus client library, and contain no forbidden Fake or Host seam symbols.
The target program was not executed.

## Boundary and mutation evidence

The offline-skip mutation had build exit 0 and test exit 1, with the actual
native counter changing from 1 to 2. The isolated resolver-skip mutation also
had build exit 0 and failed its fresh-query assertion in both new-network and
same-network modes; byte-exact restoration was recorded. An earlier invalid
mutant-2 run did execute both tests against the stale mutant-1 binary and is
the same earlier resolver attempt, so it is classified
`INVALID_MUTATION_EVIDENCE`/excluded; the incremental mtime guard prevented
NativeOwner recompilation for a valid current mutation. C1083 is the
introduced project-wiring missing-header build failure; C0000135 is a loader
failure; R8 `fcf4721ef3eb8732` is a separate functional-stage timeout. All
remain preserved. The 577-case history remains
split coverage, not a single 577/577 run.

## Verification limits and next device gate

This is Host and build evidence only: `DEVICE_TESTED = false`. It does not
prove physical SDK event identity, SDK epoch/drain, device route behavior,
autonomous reconnect, process restart, same-ID reuse, or production
qualification. No private identity, permission, claim, route, SDK/DSO, build
artifact or raw log content is included here.

Before a device run, the project needs an authorized public node registration
and query path, a legal UDID source, current SDK event/record-scope evidence,
and a fresh real-route window. The next run must use the existing bounded
task authorization and must not invent a route, restart the SDK/process,
force same-ID reuse, or bypass the existing owner and peer-ready gates.

Current labels: `OFFLINE_ROUTE_INVALIDATION_HOST_VERIFIED`,
`ONLINE_ROUTE_RERESOLUTION_HOST_VERIFIED`,
`API23_ROUTE_LIFECYCLE_BUILD_VERIFIED`,
`DEVICE_OFFLINE_ONLINE_NOT_VERIFIED`, `NOT_PRODUCTION_QUALIFIED`.
