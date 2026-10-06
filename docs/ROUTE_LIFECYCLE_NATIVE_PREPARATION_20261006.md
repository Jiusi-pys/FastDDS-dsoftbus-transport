# Route lifecycle native preparation — 2026-10-06

## Preparation status

This preparation task is based on commit
`eea2759cac4064f065a5995b2ac262acb9b5fde6` (parent
`b6a2ee8abd009f627eaaf541d796eaac16dfd59c`, tree
`b36492f7c9fca0f62497729512431de64b592c2d`). The preceding route lifecycle
implementation and Host/Target validation are inputs to a possible native
validation window; no native SDK or device business window has run. Current
status is:

- `NATIVE_PREPARATION_HOST_TARGET_VERIFIED`
- `DEVICE_NATIVE_TRIGGER_AUTHORIZED_USB_HDC_CONTROL`
- `DEVICE_NATIVE_CAPTURE_READY = NO`
- `COMMIT_GATE = FAIL` until the authorized device acceptance is closed
- `OVERALL_COMMIT_ELIGIBLE = NO`; documentation readiness does not authorize
  the task commit
- `DEVICE_TESTED = false`

The existing default route/node feature remains OFF and the default real
backend remains `NO_REACCEPT`. SDK epoch/drain, PF/P4 and production
qualification remain unchanged.

## Verified preparation evidence

The scoped Host/Target evidence is retained under
`build/route-lifecycle-native-20261006` and the preceding route lifecycle
evidence tree. Host validation recorded 20/20 business delivery and 13
public-boundary traces at the public API boundary; it is not device evidence.
One local node API type case passed; this is not SDK execution. API23 OFF/ON
compile/link completed with no Fake imports and no target execution. The
optional collector's 6/6 results are local child-I/O fixture tests covering
delayed, held, overflow and timeout behavior; the legacy selection retained
24/24 with the same local scope. Neither proves SDK events or device behavior.

The preparation changes are limited to the explicit route experiment wiring:
source/SDK trace capture, classifier and constructor route flags, export-after-
close capture, and an optional bounded collector/EOF path. The existing
implementation review is CLEAR in its local scope. No source implementation
finding is being repaired by this documentation task.

The current read-only device query R10 passed with explicit USB selection. The
observed topology was A/B Ethernet `eth1` up and `eth0`/WLAN down; fallback
identity remained unknown and the test process was absent after the query.
Only compact preflight/task references are retained here; private query
outputs, identities, permissions, claims and routes are not copied.

No native SDK registration, SDK notification, offline/online route event,
replacement POST window or device close has run in this preparation. The
read-only HDC query is separate and must not be counted as native acceptance.

## Native event contract to be exercised

The planned native window must observe, from the actual public SDK path:

1. Registration and SDK notification setup before any route event.
2. A public offline callback entering the HostLoop and NativeOwner operational
   availability/version gate.
3. Offline admission rejection for new sends and new connection admission;
   already-admitted work remains tied to its original ticket/lease.
4. A public online callback followed by actual `GetAllNodeDeviceInfo` and
   `GetNodeKeyInfo` for the expected UDID, with current nonce/peer/V1 checks,
   bounded route resolution and collision rejection.
5. The existing peer-ready `NATIVE_REPLACE` gate and post-publication business
   evidence.
6. Public DDS deletion, Runtime close, normal process exit and restoration.

The old executor `NATIVE_END` event cannot stand in for a physical node-offline
event. A physical scene must prove offline admission and version/result state,
private query-state handling, and POST delivery in one frozen live window. A
SDK fallback that retains a node is recorded as `NOT_EXERCISED`; it must not be
replaced by fabricated callbacks or Host events.

## Preserved preparation failures

R2 collector admission was rejected. R7 host-help output contained actual
enumeration only in an append correction. R9 had help-only stderr and produced
an invalid parse; LNN state was unchanged. R10 is the finalized read-only
selection with eight calls and exit 0. These earlier failures remain preserved
and were not silently converted into native success.

## Trigger boundary and prerequisites

The authorized trigger is one automatic A-side Ethernet `eth1` down/up cycle
controlled through the existing USB `hdc` link, with a maximum 90-second wait
for SDK offline state or the corresponding public event, followed by
restoration. USB remains the control and collection link; all other links
remain unchanged. This authorization does not mean the live trigger has run:
there is currently no native device business window or POST evidence, and the
fallback state remains `UNKNOWN`.

The current independent review found a restoration race and an OFF-evidence
attribution issue. Fixes and testing are in progress; they have not produced a
pre-device PASS or commit eligibility. A fresh independent pre-device review
must clear those findings before the authorized trigger is used.

Before that trigger, the task needs authorized public node registration/query,
a legal UDID source, current SDK event and record-scope evidence, and a fresh
real-route window. No process/SDK restart, same-ID reuse, autonomous retry,
permission expansion or platform-policy change is authorized. No ordinary
implementation commit is appropriate until native device acceptance closes.

Device execution remains unverified; SDK physical event epoch/drain, PF/P4 and
production qualification remain outside this preparation record.

## Closure of this preparation snapshot

The authorized bounded native window subsequently ran as iteration
`5dacd43f25fe2cbd` / `windows/data-23` with one A-side `eth1` DOWN/UP cycle
through the existing USB HDC control path; the raw controller record contains
one UP command and a final compensating UP command. The independent
feature acceptance is `PASS` (SHA-256
`2e9017e0d590795017eff731af89b147f13cc50af9278eb64ddf028bcf105a66`). The
original outer executor failure remains preserved as a PowerShell argument
construction failure; the local postrun repair was read-only reanalysis and
was not a second SDK/device run.

The preparation state therefore closes into the evidence index
[Route lifecycle native acceptance](ROUTE_LIFECYCLE_NATIVE_ACCEPTANCE_20261006.md).
The bounded device evidence verifies `OFFLINE_ROUTE_INVALIDATION_DEVICE_VERIFIED`,
`ONLINE_ROUTE_RERESOLUTION_DEVICE_VERIFIED`, and
`ROS2_ROUTE_RETURN_CONTINUITY_VERIFIED`. It does not verify a changed-network
device route, SDK record epoch/drain, unbounded reconnect, same-ID reuse, PF/P4,
or production qualification. The default feature remains OFF, the default
backend remains `NO_REACCEPT`, and the missing numbered A5/C1 source
disposition remains unchanged.
