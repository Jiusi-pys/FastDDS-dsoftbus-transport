# A5/C1 bounded same-route reaccept Host acceptance — 2026-10-05

This is the evidence index for the authorized `A5C1_SAME_ROUTE_REACCEPT_HOST_INTEGRATION`
package at base `bdb1073e18192bce1e904e30517219ddda5e8bc5`. It records a bounded
Host experiment only. It is not device reconnect evidence, SDK qualification or a
`QualificationProfile`.

## Status matrix

| Area | State | Evidence / boundary |
| --- | --- | --- |
| Contract and implementation | `A5C1_BOUNDED_REACCEPT_HOST_VERIFIED` | `docs/A5C1_BOUNDED_REACCEPT_HOST_CONTRACT.md`; current owner table, adapter, binding, public backend and Host opaque seam |
| Boundary and DDS tests | PASS | `build/a5c1-20261005/boundary-final-initial/`; 16 boundary cases plus 3 DDS cases, 19/19 |
| Effective mutations | PASS | `mutation-results-r2`, `mutation-results-physical`, `mutation-results-old-generation`: four effective mutants killed and restored (NO_REACCEPT, ambiguous positive ID, physical duplicate close, old-generation retarget) |
| Surviving / unrun mutations | RECORDED | Duplicate binding-layer shutdown survived because backend idempotence absorbs it; early-settlement and candidate-overwrite mutations were not run |
| ROS Host ordinary | PASS | `ros-controlfix-ordinary/result.json`: both QoS 20 writes/20 takes, graph/service, paired close, original deletion and Runtime close, exit 0 |
| ROS Host reaccept | PASS, bounded | `ros-controlfix-reaccept-A/result.json` and `...-B/result.json`: both origin directions, same Node/pub/sub/context/GID, resumed Topic traffic, graph/service, replacement, original deletion, Runtime close and exit 0; source is the opaque public-callback-shaped Host seam and `sdk_epoch=NOT_PROVEN` |
| Invalid Host controls | PASS | `ros-controlfix-negative-invalidpid` and `ros-controlfix-negative-windowfailed`; invalid control input and failed control window are rejected |
| Post-review targeted Release | PASS | `regression-initial/postreview-targeted19-explicit-env/result.json`: return code 0; 19/19, including reaccept, DDS reaccept and real-backend Host-mode rejection |
| ASan affected set | PASS with declared exclusions | `regression-initial/asan-affected-explicit-env/result.json`: return code 0; 117/117 selected cases, with two shutdown/destroy cases explicitly excluded by death-contract policy |
| Full CONFIG | COVERAGE RECORDED, run not green | The frozen run recorded 574/576 (574 pass, 2 fail). Later unchanged targeted coverage plus one newly registered control test covers 577 cases across runs; no single 577-test green execution was obtained. Earlier timeout, loader and PowerShell/configuration failures remain historical evidence |
| API23 target | `API23_REACCEPT_BUILD_VERIFIED` | `api23-current/target-runtime-static-provenance-r4.json`: current owner/adapter/binding/backend/runtime objects compiled for AArch64, archive linked into the TargetBridge, Fast DDS 2.14 and Socket DSO present, Fake absent; target was not executed |
| API23 consumer | PASS | `api23-current/consumer-r6-build-receipt.json` and `consumer-r6-static-audit.json`: configure/build and static/dynamic audit return 0 |
| Installed / relocated Host consumer | PASS | `host-default-installed-consumer/relocation-run-r1/results.json`: install, configure, build and execution pass; experimental/debug/empty-map/obsolete-package negative checks reject as expected |
| Independent review | PASS | `build/a5c1-20261005/independent-review-final.txt`; no blocking source finding, with graph stale/dynamic callback limits retained |
| Device reconnect | NOT RUN / NOT VERIFIED | HDC, device queries, deployment, grants and target execution were forbidden in this package. The prior V1 device PASS remains bound to the historical source/package/run recorded in `docs/V1_LOCATOR_DEVICE_ACCEPTANCE_20261004.md`: device run `e72e18d67cbc5b6d`, executed at the earlier `d670773` revision plus the reviewed native-name/capture/config working-tree manifest and `build/v1c26/bound-build` package |
| Production / PF/P4 | NOT QUALIFIED | Default real backend remains `NO_REACCEPT`; PF/P4, SDK epoch/drain, process restart, same numeric ID reuse, route/permission rediscovery and general reconnect remain unmet |

## What this Host result proves

The existing Runtime, Participant, Writer and Reader survive a controlled
same-route terminal event in either endpoint order. The existing control pump
settles the old owner, reserves a fresh bounded owner and native ID, performs
replacement through the existing adapter/binding path, and delivers new Topic
traffic. The Host model also covers candidate rollback, finite capacity,
unknown-first-event quarantine, old-queue lease retention, entered work
settlement, close-once and opposite-lane survival. No callback generation or
synthetic SDK identity was added.

The fixture pauses the existing pumps, delivers the normal terminal callbacks,
allows the accepted endpoint to retire and settle, and resumes the existing
outgoing control path for one automatic Socket/Bind attempt. The application
does not call a rebuild operation. A failed Dial candidate is retained without
blind retry. This establishes fresh-ID plus deferred-control behavior, while
peer-readiness races and process restart remain outside the evidence.

The ordinary real path remains protected by `NO_REACCEPT`; the Host behavior is
enabled only by its explicit experiment configuration. The API23 build confirms
the implementation is present in the target archive and that the default target
path does not compile the Fake or Host C seam.

## Remaining minimum conditions for a real reconnect claim

The next SDK run needs evidence that the public callback/record scope identifies
the old event and the replacement event, that the replacement Socket/Bind result
is a fresh native identity, and that the old SDK close responsibility remains
attributable, either completed or safely retained, before the new owner is
exposed. This Host result proves adapter settlement only; SDK-internal resource
drain and event scope remain separate, unproven facts. Public mechanisms may
verify callback ordering, returned IDs and close results when the SDK exposes
them. If an ingress event can reuse a numeric ID without a distinguishable record
or generation, the safe result remains rejection; names, current maps and stable
V1 locator bytes do not supply the missing event identity. Peer process restart,
route/permission rediscovery and arbitrary repeated reconnect require their own
SDK evidence and are outside this Host acceptance.

At documentation preparation time the package is ready for the parent agent's
local checkpoint gate. No commit hash is recorded here; the actual local
checkpoint is identified by Git history after the parent performs the exact
allowlisted commit.
