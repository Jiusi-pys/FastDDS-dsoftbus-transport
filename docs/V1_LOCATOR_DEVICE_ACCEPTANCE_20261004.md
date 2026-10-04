# V1 locator device acceptance — 2026-10-04

Status: **`V1_LOCATOR_DEVICE_VERIFIED / ROS2_V1_DUAL_BOARD_VERIFIED / COMMIT_GATE_PASS / NOT_PRODUCTION_QUALIFIED`**.
The initial 2026-10-04 preflight was stopped by the unchanged-policy gate; the
later query, deployment, recovery and conditional business actions were not
executed in that historical attempt. The final acceptance below is a later,
successful bounded run.

## Final raw acceptance

The final independent acceptance used run ID `e72e18d67cbc5b6d` on base Git
HEAD `d670773a8385194c409b09b3d028d44edb01b81e`, plus the reviewed working-tree
native-name/capture/config revision. The binaries are bound by the
`build/v1c26/bound-build` source/manifest receipt; this does not imply a new
commit existed at device execution. The run was iteration 26,
`windows/data-16`. Immutable evidence index SHA-256:
`aa295d6c5376bccacccaf16d41afd5f783f0cc65dd0b29ed10fd05f1e87066fe`.
Final tester evidence is in `build/finalpostreview/FINAL_TEST_GATE.json`; the
raw acceptance receipt is the iteration-26 `INDEPENDENT_V1_RAW_ACCEPTANCE.json`,
with its `V1_RAW_EVIDENCE_INDEX.json` carrying the immutable index above. The
original `build/v1-device-20261004/final-gate/FINAL_GATE.json` is the historical
blocked preflight receipt and remains `FAIL`; it is not final-pass evidence.
The final Commit Gate is `PASS`. The V1 device and ROS dual-board labels are valid for this bounded
acceptance scope; production qualification remains `NOT_PRODUCTION_QUALIFIED`.

Both boards completed the ROS 2 V1 window: both QoS modes totalled `20/20`,
with graph, two cross-board service exchanges, original Participant delete,
Runtime close, public return 0 and normal process exit passing. SDK-correlated
takes were 20. SPDP `SendBytes` and ingress were complete at `14/14` per board;
PID `0x0031`/`0x0032` observations retained the full kind, 16-byte address,
profile 1, slot 0 and port `0x01000001`. The selected ledger values were A=95
and B=68. The fresh package’s 109 members, live origins and normal restore
also passed. Current policy hashes matched the saved hashes; no platform policy
change or repeated SDK query occurred.

The earlier read-only policy denial is retained as preflight history and is not
rewritten as a Socket or device failure. The final package still does not prove
cleanup-tail absence, zero loss, SDK epoch, or SDK drain: A retained 19
`InvalidState` callbacks, B retained 0, and startup cleanup flag remained 0.
SDK epoch/drain remain unproven. The original iteration-23 `windows/query-16` cleanup failure and
consumed claim remain historical; only the appended 12-command recovery receipt
passed. No QualificationProfile, new design, performance-100 run, policy,
SDK, RMW, platform policy or firmware change was introduced; this package did
modify implementation/configuration/executor/test files as recorded by the
parent work package.

## Final acceptance matrix

| Layer | Result | Boundary |
| --- | --- | --- |
| G2/A1 implementation | `PASS` | V1 production path and full-locator selection retained |
| Host / installed consumer | `PASS` | Prior Host and relocated-consumer evidence retained |
| API23 target compile/link | `PASS` | Fresh package build/link/static evidence |
| V1 device runtime | `PASS` | Bounded iteration-26 dual-board window |
| ROS 2 V1 dual-board | `PASS` | Both QoS modes, `20/20`, graph/service/close checks |
| SPDP / locator selection | `PASS` | `14/14` SendBytes and ingress per board |
| Production qualification | `NOT QUALIFIED` | PF/P4, QualificationProfile and SDK epoch/drain limits remain |

## Historical preflight gate

The named initial tester completed 29 HDC preflight commands and two fixed
policy receives. Both boards reported the same policy hash
`f98b0770c1da57a0a8939d38fdd16386dfcfabb801d9413e18ed781cc217d12e`.
The preflight performed zero directory queries, zero permission mutations,
zero deployments, and zero business windows. The frozen PRIVATE_CONFIG tuple
was confirmed against the actual A/B `ros.conf` package and prefix; its
identity values are intentionally omitted from this document.

The independent reviewer inspected the policy regex patterns and the source
path `Socket → CreateSocket → ServerIpcCreateSessionServer →
CreateSessionServerInner → CheckTransPermission → CheckPermissionEntry`.
The POSIX regular-expression no-match path is the native enforcing denial:
no installed rule can match the canonical `md1` plus lowercase-hex/dot service
name family. This is **static-source-aligned policy denial**, not an observed
Socket return code. No `GetNodeKeyInfo`, Socket application, or business run
was performed.

The reviewer assessed the package as high-confidence `BLOCKED` and stopped
because resolving it requires a platform policy modification or service
contract revision. Both are outside the user-authorized scope and forbidden in
this package. The final tester’s raw audit confirms the same conclusion in
`build/v1-device-20261004/final-gate/FINAL_GATE.json`; Commit Gate is `FAIL`.

## Historical preflight acceptance matrix

| Layer | Result | Interpretation |
| --- | --- | --- |
| G2/A1 implementation | `PASS` retained | Prior V1 production-path evidence is unchanged |
| Host / installed consumer | `PASS` retained | Prior evidence is not device evidence |
| API23 target compile/link | `PASS` retained | Static target build only |
| Read-only policy preflight | `PASS` | 29 commands and two policy receives; no state-changing operation |
| V1 device runtime | `BLOCKED / NOT RUN` | Policy denies the canonical service-name family before Socket execution |
| SPDP / locator selection | `NOT RUN` | The unchanged-policy gate blocked execution before a business/application window |
| ROS 2 `20/20`, graph/service/delete/runtime close | `NOT RUN` | No current device window exists |
| Commit gate | `FAIL` | This historical preflight was not eligible for the ordinary successful commit path |

Do not label that historical preflight `V1_LOCATOR_DEVICE_VERIFIED` or
`ROS2_V1_DUAL_BOARD_VERIFIED`; the final raw acceptance above carries those
bounded labels. Existing A5/C1 `NO_REACCEPT`, other design
limitations, PF/P4, and production qualification remain unchanged. A formal
reconnect requirement therefore remains unmet; `FULL_REQUIREMENTS_COMPLETE`
is not true when reconnect is required.

The historical preflight made no codec, RMW, SDK, policy, implementation
source, or executor changes. The project documentation increments are
uncommitted. Its read-only preflight was the only executed phase in that
attempt; the final acceptance is recorded above.
