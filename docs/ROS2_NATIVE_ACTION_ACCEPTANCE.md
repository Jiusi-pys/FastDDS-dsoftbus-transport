# R25 native C++ Action on two RK3588A boards

> **Publication / successor (2026-10-17 23:34 UTC):** This native C++ stage remains published as `4794ffb08d2d1cf49e758a0a4627f4aa2c87de92`; later node/echo work is published through `cb9c54dc6564362b52dc652e55ae9f7b5559749e`. [Ordinary Action CLI AB/BA](ROS2_ORDINARY_CLI_ACTION_ACCEPTANCE.md) now separately passes one positive order=6 goal per direction, with feedback and exact successful result. That CLI fixture has its own sequence contract; native C++ cancellation/rejection/abort/concurrency results below do not qualify those ordinary CLI paths. Action documentation publication remains pending; rosbag remains unverified. The native-stage body below is a historical snapshot.

At 2026-10-17 08:13:12–08:14:19 UTC, the independently reviewed native window
`484675d3f084d757` passed its Action, native-path, SPDP, process-exit and cleanup
oracles. **Scoped native Action PASS with a recorded optional unique-flow retry.**
Both A and B emitted `No unicast locators to create unique flows` and
`Problem creating associated Reader`. Independent post-run review classified
these as a real first-Reader failure for OPTIONALLY_REQUIRED unique flows.
The selected existing RMW then recreates the graph Reader with its original
QoS on the same Participant/custom Transport. Actual selected binary paths,
two create-attempt records and successful context/business completion support
that recovery; this is not builtin UDP/TCP fallback or an error-free startup.
Original logs and the sealed tester report remain unchanged. Main's full
PRE_COMMIT/publication gate is separate; overall feature completeness is NOT VERIFIED.

This is the existing bespoke C++ Fibonacci Action with PAIR/TAIL application
readiness services and three process-lifecycle barriers. It is real native
DSoftBus transport execution, not a Host stub; it does not qualify ordinary
`ros2 action send_goal`, Python Node startup or `ros2 bag record/play`.

## Implemented boundary

The stage contains 14 files: fixed identity admission/provisioning and bridge
(10), native peer query/build driver (2), and native Action runner/fixture (2).
The existing ROS-only C++ business source, DDS/RTPS semantics and RMW remain
unchanged. The path is `rclcpp_action -> rmw_fastrtps_cpp -> project Fast DDS ->
Transport abstraction -> DSoftBus Socket API`; no Session API is introduced.

The fixed identity is `mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16`, normal
APL with only `ohos.permission.DISTRIBUTED_DATASYNC`, empty dcaps/acls. Its
authorized create-once operation succeeded once per board in the earlier
identity stage, with original process tokens restored. Persistent markers are
retained: do not repeat creation or delete the markers. Exact post-review
identity queries confirmed GRANTED; legacy `fastdds_dsoftbus_probe` remained
DENIED. No whole-registry read, system identity borrowing, reboot or network
policy change supplied this result. The Action runner never provisions identity.

The peer query selects/verifies the token before native directory calls, frees
the SDK node list, exports only the local UDID into a private 0600 file in a
0700 directory, restores the original token and exits. Fresh peer run
`fe7c9c0cb93d52df` confirmed reciprocal current routes immediately before Action.
The 300-second freshness check is a local harness policy, not an SDK guarantee.
UDIDs, network IDs and private runtime configuration are not reproduced here.

## Actual environment and results

| Item | Observed value |
| --- | --- |
| Checkout / baseline | `build/r15-lifecycle/publish-worktree`, HEAD `d8b0fdfecaa654183c57d4b973cc936234080667` before R25 publication |
| Board A / B HDC target | `3e01ff55454d202020104033bf453b00` / `3e01ff55454d202020104433991c3b00` |
| Build / execution | Windows host Python, SDK23 OHOS AArch64; exact HDC targets; native binaries executed on RK3588A |
| Package | 104 ELF members, 805 payload files; independent `PACKAGE.json` SHA256 `968911a55a4cf408166578d47360d6c0ca0364fcb5b4870a992bab3086b2dd39` |
| Action executable | SHA256 `411bd50aa93e76293834a05eec951a231470b89495a6396961676b6ef03f7c98` |
| Project providers | Fast DDS 2.14.5 / Fast CDR 2.2.5, project RMW; listener/business maps and actual DSO hashes match package and real platform SoftBus |
| Profile | `OHOS_PUBLIC_SOCKET_EXPERIMENTAL`, `BYTES_NATIVE`, `PACKAGE_SCOPED_V1`, domain 201; builtin=0, intraprocess=0, DataSharing OFF, fake_sdk=0 |
| Native send / ingress | A: 109 / 98; B: 98 / 109; matching peer writer/sequence observations A→B 81, B→A 53 |
| Discovery evidence | Complete same-DATA SPDP V1 metadata/user locators and selectors; no dedicated SEDP wire oracle |
| Exit / cleanup | Both children exit 0, restored tokens, public Participant delete returns 0, local close and pump join, no forced signals, owned Action directories removed; fresh query/identity evidence directories retained |

| Fibonacci order | Exact outcome |
| --- | --- |
| 6 | Accepted; SUCCEEDED `[0,1,1,2,3,5]` |
| 40 | Accepted concurrently with 6; targeted cancellation; CANCELED `[0,1]` |
| -1 | Rejected; null client goal handle; no invented client UUID/result |
| 0 | Accepted; ABORTED `[]` |
| 5 | Accepted after rejection/abort; SUCCEEDED `[0,1,1,2,3]` |

There are five requests, four accepted, one rejected and four terminal results,
with pending=0. The unchanged R18 business oracle checks UUID joins, observed
feedback subsequences and server-local concurrency/cancellation order. Its old
Host scope label is historical: native-path proof comes separately from actual
device receipts, maps/hashes and SDK records. Action UUIDs are not directly
mapped to RTPS sequence numbers. Status-topic behavior is not separately tested.

The three barriers release listener readiness, completed business and local
runtime close. Existing finite deadlines remain unchanged. `Shutdown` is void,
and Socket handle 0 is valid; accepted child sockets are not required to equal
the count of Socket creation calls. Process-local closure and exit do not prove
SDK epoch, cancellation or global callback quiescence; modules remain loaded
until process exit. Broad reliability/load/security/production qualification
does not follow from this single paired window.

## Optional unique-flow diagnostic

The selected RMW graph subscription requests OPTIONALLY_REQUIRED (enum 2).
When the unique-flow Reader cannot be enabled, upstream RMW explicitly retries
creation using the saved original QoS. STRICTLY_REQUIRED (enum 1) does not take
that retry. The package's actual RMW binaries match this path, and both boards
record two graph-reader create attempts before application READY. Those records
are pre-enable observations, not two live Readers. Fast DDS deletes the failed
Reader and releases its payload pool before the second creation succeeds.

The current generic unique-port allocation and custom V1 profile/slot locator
representation do not directly fit. This explanation is based on selected
source/binary contracts; the run did not log every failed locator or a separate
retry-success event. The observed optional attempt provides no unique flow.
Strict mode was not executed and is not qualified; this does not establish
permanent incompatibility or failure of all DDS QoS. Reliability, durability,
deadline, liveliness and graph semantics require their own evidence.

The original Error lines are retained without suppression. A different Error
signature, additional repetitions, errors after READY or missing context
completion requires new triage; this classification is not a blanket allowlist.

## Validation, evidence and reproduction boundary

Independent initial validation freshly built identity/Action and peer-query
artifacts and executed Host controls (identity CTest 4/4; peer CTest 3/3).
Native harness initial checks included 17 independent controls plus 31 unchanged
Action controls, package closure, and 18 shell syntax checks. Separate Tier 3
agentic reviews covered identity, peer query and native harness. No reviewed
native source changed afterward. Final tester reran the frozen Host fixture,
then fresh queries and the actual two-board native window. Host/fake controls
are not counted as native execution evidence.

Local evidence root is `ROOT/build/r25-native-actionbag`; ROOT is the original
workspace, not the nested publication checkout. These ignored records are
not uploaded by this documentation:

| Evidence | SHA256 / scope |
| --- | --- |
| `native-action-device-test/RESULT.json` | `208d09da1bc9bfc61ec7caa7b9ba44004dd928940a40347cd91590eeb8868702`; original tester Commit gate PASS limited to frozen identity10 + peer2 + harness2 |
| `native-action-device-test/SEALED_MANIFEST.json` | `4b792e52487625e1f35835e8a2e4d70acdf7c08a7e573ad40e373da65e7d98a3`; 2,376 sealed files, independently rehashed by Main |
| `native-action-device-test/REPORT.md`, `INDEPENDENT_CHECKS.json` | Execution times, command counts, protected workspace checks and evidence-label limitations |
| `native-action-device-test/commands/`, `fresh-peer/`, `window/commands/` | Complete argv/environment/deadlines, stdout/stderr, separate host returns and remote exit markers |
| `native-action-device-test/window/{ORACLES,CHILD_EXITS,CLEANUP}.json` | Business/native/SPDP outputs, exits and exact owned cleanup |
| `native-action-review-postrun/RESULT.json` | `704e210048618ab760340f77ccf64927a6aba612b3d3047091e5b0717bf0d4c2`; recovered optional unique-flow classification |
| `native-action-review-postrun/REPORT.md` | `c5a0d42fe189fa31d9665b9fde2fe09879cb8a90d2740c1fd95957d9e685877d`; selected source/binary path and limits |
| `native-action-device-postrun/FINAL_GATE_SUPPLEMENT.json` | `91ed9b85f11f8d4c4c139700a9182ed8e681ffda73885f45860e06cd9d738ebf`; additive final tester PASS after diagnostic classification |
| `native-action-device-postrun/SEALED_MANIFEST.json` | `33f406874038ac22fd25da285466cfde198ee5eb3de07650f241e3daa8b98946`; supplement seal; original device seal preserved |
| `native-action-device-postrun/RAW_ERROR_AUDIT.json` | Additive A/B error counts, positions and original raw hashes; original seals unchanged |
| `native-action-review/RESULT.json` | `dcee8c9fab59f29ff99a921a108f40c4e958f4410a2420bad2ba1eb7dcc66c82`; execution eligibility, not device PASS |
| `native-action-device-test/RESULT.json` → `prior_chain` | Hash-bound identity/peer initial tests, reviews, final device results and native initial package evidence |

Build/package reproduction uses a new output directory and the same prepared
inputs described in [R24 project Python ABI](ROS2_PROJECT_PYTHON_ABI.md), plus
the fixed identity/native Action source and SDK ABI bindings. The maintained
drivers are `integrations/ros2/target/build_actionbag_identity.py --inputs-root
ROOT --output NEW_IDENTITY_BUILD --build-action`,
`build_actionbag_peer_query.py` (see its `--help` for bound identity-build input),
and `tests/ros2_target/run_actionbag_native.py prepare --inputs-root ROOT
--identity-build IDENTITY_BUILD --query-build QUERY_BUILD --output NEW_PACKAGE`.
Packaging does not perform identity provisioning or device communication.

The actual independent execution entry was
`python -X utf8 -B ROOT/build/r25-native-actionbag/native-action-device-test/execute_device.py`.
It is a retained, once-only, reviewed local evidence driver, not a reusable
installed command. Do not replay its release/creation receipts. A new native
window requires fresh reviewed peer receipts, exact already-admitted identities,
new owned paths, matched platform/package hashes and exclusive device ownership.
The maintained runner's `routes --peer-evidence FRESH_EVIDENCE --output NEW_ROUTES`
and `run --package PACKAGE --routes NEW_ROUTES --identity-result IDENTITY_RESULT
--output NEW_WINDOW` preserve failures and enforce these checks.

The original 08:16 UTC tester gate was temporarily held for the raw Reader
errors. After the independent post-run classification, the 08:25 UTC additive
final tester supplement restores PASS for the same frozen 14-file scope;
original seals remain intact. No source/oracle/deadline change or device rerun
was used to close that hold. Documentation is ready for this scoped stage;
Main must still complete graph/PRE_COMMIT and publication gates.

Historical tester assertion/counting errors and infrastructure failures remain
in their original evidence; none is rewritten to PASS. Root/W1/W2 dirty work and
stopped R22 files remain protected. Python launcher, CLI overlay and generated
Action Python types are separate ongoing work, not delivered by this stage.

## Capability matrix and publication

| Capability | Status and evidence boundary |
| --- | --- |
| Fixed native identity and peer directory | SUPPORTED for the exact R25 identity on A/B: native/normal APL, DATASYNC only, token restoration, one local/online directory query per fresh run, SDK buffer release and private local UDID export. Old probe identity remains DENIED. |
| Native C++ Action | PARTIAL: five requests, four accepted, one rejected; two succeeded, one canceled, one aborted; UUID correlation, feedback and concurrent server goals pass in one actual paired window with application readiness barriers. The recorded optional unique-flow failure recovers through the existing RMW original-QoS retry; it is not an error-free startup. |
| Native transport path | SUPPORTED for that bounded run: actual project RMW/Fast DDS maps and real SoftBus provider, fake_sdk=0, builtin/intraprocess=0, DataSharing OFF; bidirectional SDK send/ingress and writer/sequence matches. This is not production qualification. |
| Discovery | PARTIAL: actual V1 metatraffic/user locators in the same SPDP DATA and peer ingress pass; successful Action functionally exercises endpoint discovery. No dedicated SEDP wire oracle, general graph or reconnect test in R25. |
| Pub/sub and service | PARTIAL: Action feedback/results and PAIR/TAIL Trigger readiness service work in this scenario. Prior dated general pub/sub/service results retain their own scope; R25 adds no general CLI topic/service acceptance. |
| Serialization and message size | PARTIAL: fixed Fibonacci Action payloads in R25 and fixed String CDR on A/B in R24. R21 small/66k/burst String remains Host TCP-backed fake-DSoftBus evidence, not native large-message acceptance. |
| Unique network flow endpoints | PARTIAL: this OPTIONALLY_REQUIRED attempt does not provide unique flows; the existing RMW retries with original QoS on the same custom Transport. STRICTLY_REQUIRED is UNVERIFIED / not executed, not a claim of permanent incompatibility. |
| QoS and failure recovery | UNVERIFIED beyond the scenario's existing profile. No all-QoS, status-topic, unbarriered Action, reconnect, packet-loss or crash-recovery claim. R22 remains STOPPED / UNVERIFIED. |
| Resource lifecycle | PARTIAL: public Participant deletion, pump join, local runtime close, original token restore, child exit 0 and owned Action directory cleanup pass without forced signals. SDK global callback drain/epoch/quiescence and same-process repeated Participant lifetimes are not established here. |
| Project Python ABI | SUPPORTED only for R24 no-Node imports, fixed String CDR and help/resource queries. Full ABI and NumPy API coverage remain UNVERIFIED. |
| Project Python Node / ordinary CLI Action | UNVERIFIED on target. Separate token-first Python launcher repair has Host evidence/review only; CLI overlay CLI-01 repair passed independent Host retesting and awaits formal review; Action Python types have build evidence only. These ongoing changes are excluded from this native 14-file stage. |
| rosbag | PARTIAL only for earlier R19/R20 offline local String SQLite persistence. Ordinary record/play over native DSoftBus, rosbag2_py and remote replay remain UNVERIFIED. |
| Unsupported execution choices in this stage | Session API, builtin UDP/TCP fallback and generic ROS provider substitution are excluded. Daemon mode and multiple successful Participant lifetimes per process are not qualified. |

Implementation PRESENT; build PASS; HOST_TESTED PASS; RK3588A and cross-device
business/native-path tests PASS within the stated scenario. The two Error lines
per board are classified as the existing optional unique-flow recovery. Main
PRE_COMMIT, R25 commit/push and CI are
PENDING; overall native ROS 2 feature completeness remains NOT VERIFIED.
Last confirmed publication is R24 `d8b0fdfecaa654183c57d4b973cc936234080667`
on existing `origin/codex/flow-encoding-published-20261014`; its 06:12:24 UTC
CI snapshot was NOT_RUN (zero runs/checks/status contexts).
