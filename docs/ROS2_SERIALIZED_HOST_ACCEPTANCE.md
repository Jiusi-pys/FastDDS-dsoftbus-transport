# Typed serialized publication Host acceptance (R17 FIX1)

R17 qualifies two typed serialized publication overloads in a finite two-process
ROS exchange on the existing Windows `default-2145` Host package: Fast DDS
2.14.5, SECURITY OFF / STATISTICS OFF. The FIX1 candidate is PRESENT, BUILDS and
HOST_TESTED; independent review found no actionable findings and final tester
Commit gate is PASS for this narrow Host source/tooling stage. Main's graph,
PRE_COMMIT, commit, push and CI remain separate pending receipts at this
documentation snapshot. ROS 2 over native DSoftBus remains NOT VERIFIED;
DEVICE_TESTED=false, native/cross-device acceptance BLOCKED.

The six-file candidate is based on
`8b29b872b95607d6d9580ae67e1d6df6aebc5efb`: four new standalone consumer/oracle/test
files and two modified shared runner/test files. No Transport, DDS/RTPS, RMW,
security-policy, dependency or shared capture-validator implementation changes.
The [R15 lifecycle](ROS2_LIFECYCLE_HOST_ACCEPTANCE.md) and
[R16 managed publisher](ROS2_LIFECYCLE_PUBLISHER_HOST_ACCEPTANCE.md) scenarios
receive complete regression through the affected runner; their earlier records
remain historical evidence.

## What was executed

A and B each create one application typed publisher and one application typed
subscription. ROS auxiliary endpoints also exist; these are not claims of only
two total DDS endpoints per process. Both application endpoints use actual
reliable, volatile, KeepLast(10) QoS, and intra-process delivery is disabled.
Files coordinate START/release/close only. ROS data crosses the TCP-backed Host
fake DSoftBus Socket seam, not the native OpenHarmony DSoftBus Socket API.

Two sequential request/reply exchanges use `std_msgs::msg::String`, with 17-byte
then 4096-byte bodies. The complete application strings also contain run, role
and exchange ID. Each side serializes through `rclcpp::Serialization<Message>`
into an owned `SerializedMessage(8192)`. Exchange 1 calls the typed publisher's
`publish(const rclcpp::SerializedMessage&)`; exchange 2 calls
`publish(const rcl_serialized_message_t&)`. Each buffer remains alive through
the synchronous call. There is no handwritten CDR, ordinary `publish(Message)`
fallback, capacity reset, reserve-after-serialization or business retry.

B validates each typed request callback before publishing the corresponding
serialized reply. A waits for each typed reply before starting the next exchange.
Across both processes there are four successful serializations, four publication
attempts, four returns and four typed callback receives: two wrapper calls and
two raw calls. Each process publishes twice and receives twice. Public writer
GIDs are distinct and stable, and each received `MessageInfo.publisher_gid`
must match the opposite process's actual writer. The independent oracle
reconstructs full expected payload bytes and checks schema, run/side/PID, local
sequence, roles, exchanges, overloads and callback ordering. It does not infer
identities from timestamps or accept totals alone.

This is finite typed serialized semantic interoperability. It is not a claim of
wire-byte identity, DDS sequence correlation, arbitrary message sizes,
fragmentation acceptance, generic APIs, loans, zero-copy or lifecycle-managed
serialized publication. The bounded drain and final progress step are not
perpetual absence-of-extra-message guarantees.

## Preserved initial failure and the version-specific fix

The original R17 actual run `c5d261aa51080285` remains FAIL. A PID 53260 and B
PID 18232 both exited 4. A reported `serialized_bounds`; B reported
`bounded_wait_expired`. No SERIALIZE, PUBLISH_ATTEMPT, RETURN or RECEIVE business
records were emitted. Numeric before/after capacities and length were not
captured before the original combined assertion. The exact failing conjunct is
**NOT_CAPTURED_NOT_RETROACTIVELY_INFERRED**. Later values must not be backfilled
into this failed run. Its 146-file evidence seal remains unchanged; the initial
R15/R16 regressions passed separately and did not make the R17 window PASS.

Read-only investigation found a concrete defect in the test assumption that
reported capacity must remain 8192. The sealed exact-version
`rmw_fastrtps_cpp` 8.4.3 `rmw_serialize.cpp` conditionally resizes, then assigns
both `buffer_length` and `buffer_capacity` to `data_length`. rclcpp 28.1.16
`size()`/`capacity()` expose those fields. This supports a plausible failure
mechanism but does not identify the uncaptured original failed conjunct.

FIX1 keeps strict `capacity_before == 8192`, requires
`capacity_after == serialized_length`, and retains
`full_application_payload_bytes < serialized_length <= 8192`. PRE diagnostics
precede the initial-capacity check; POST diagnostics follow successful
serialization and precede the combined bound check. If serialization throws,
POST values are not invented. The API-reported equality is a **pinned RMW 8.4.3
field contract**, not a portable capacity rule or evidence that physical heap
allocation shrank. No middleware field is overwritten to satisfy the test.

The final new run independently captured before=8192 and after=length:

| Direction / overload | 17-byte body, wrapper | 4096-byte body, raw |
| --- | --- | --- |
| A request: serialized length and reported post-capacity | 70 | 4149 |
| B reply: serialized length and reported post-capacity | 68 | 4147 |

These lengths include serialized application strings, not only their body
bytes. They belong to the new run. Version-tagged source and installed metadata
support the expectation; actual loaded-module hashes bind the runtime, but
neither establishes complete source-to-DLL reproducibility. Investigation and
FIX1 reasoning are preserved under
`build/r17-publication-api/failure-research/{REPORT.md,FIX1_PROPOSAL.md,OFFICIAL_SOURCES.json}`.

## Capability matrix

| Capability | Status | Boundary |
| --- | --- | --- |
| Typed serialized wrapper and raw publication to typed callbacks | Supported in the bounded Host scenario | Both overloads in both directions; four exact semantic deliveries |
| Serialization capacity/length observation | Supported for pinned RMW 8.4.3 | Strict before/after/length checks; not physical shrink or a general ROS contract |
| Message-size coverage | Partial | Only 17/4096 body-byte cases, not arbitrary sizes or large fragmented messages |
| Original lifecycle and managed-publisher scenarios | Host regression PASS | Both affected existing scenarios freshly rebuilt and executed |
| Lifecycle-managed serialized publication | Unverified | R17 uses ordinary typed publishers; R16 used two ordinary managed overloads |
| GenericPublisher / GenericSubscription serialized path | Unverified | Dynamic type-support loader prerequisite identified in planning; no generic runtime failure or success observed |
| Typed or RMW loan capability | Unverified | No actual capability query/borrow/return acceptance; DataSharing OFF does not prove all publisher loans unsupported |
| GenericSubscription `handle_loaned_message` | Not implemented in inspected exact-version source | Narrow source finding only; does not mean all generic or loan APIs are unsupported |
| Zero-copy, concurrency, restart/reconnect, security-enabled publication | Unverified | Outside this bounded default-profile workload |
| Native DSoftBus / RK3588A / cross-device | BLOCKED, unverified | No target entries in latest HDC discovery; owner permission blocker unresolved |

The generic planning prerequisite is specific: the exact-version loader uses an
ament package prefix to form an absolute Windows type-support DLL path. The
qualified runtime DLL copies and base ROS package index are different prefixes;
PATH priority alone does not establish the future generic module choice. No
generic loader was executed or failed here. A future generic stage needs a
coherent, sealed package-index/type-support configuration and actual module
verification. No such overlay or loader modification is included.

Loan evidence must distinguish allocator fallback, actual RMW borrowing and
end-to-end zero-copy. The inspected DDS loan path does not require DataSharing
ON, so the Host route's DataSharing-OFF restriction cannot establish universal
loan unavailability. The exact-version generic loaned receive method throws
UnimplementedError, while the ordinary serialized generic callback path is
implemented. These are different interfaces. See the read-only
`build/r17-publication-api/design/REPORT.md`; its planning facts are not runtime
loan or generic acceptance.

## Independent verification and final runtime facts

Independent FIX1 initial testing passed three fresh consumers and one actual
window per R15/R16/R17 scenario, including R17 run `8ef7f044e2346902`,
A 46660 / B 57480, both exit 0. Its 136-file evidence manifest is preserved.
Independent Tier-2 agentic review found NO_ACTIONABLE_FINDINGS; no review-driven
source change followed. Review independently ran the models/parsers, replayed
the sealed windows, rejected 42 adversarial cases and detected three meaningful
oracle weakenings. One earlier mutant experiment produced fixture errors and
is retained but not counted as a semantic mutation kill. Offline reanalysis
is not another actual ROS run.

The final tester freshly configured/built all three C++17 consumers once using
MSVC x64 Release `/MD`. Twenty-five model methods passed (17 prior plus eight
serialized), including 36 barrier subcontrols; 60 related parser tests passed
(31 receive, 7 I/O, 15 analyzer, 6 native-named analyzer, 1 control-file test).
These are synthetic/offline counts, not native execution. Each final actual
runtime window executed once, with zero business retries:

| Final window | R15 | R16 | R17 |
| --- | --- | --- | --- |
| Run | `1c18b7aeed97e48b` | `9eddc65b2b4c3154` | `f9333cf6b0dc4fd7` |
| A / B PID | 73376 / 77212 | 34700 / 69784 | 24196 / 53532 |
| Business | 10 changes, 12 states/available snapshots, 20 events, 11 callbacks, 2 finalized | 6 changes, 7 states, 4 probes, 8 attempts, 4 receives, 12 events, 6 callbacks | 2 exchanges; 4 serializations/attempts/returns/receives |
| Current selected START modules A / B | 12 / 10 | 16 / 14 | 10 / 10 |
| Ports, exclusive rebind PASS | 50098 / 50099 | 55612 / 55613 | 65508 / 65510 |

All six children exited 0, both polls were None at each current module barrier,
no forced kill or timeout occurred, and runtime close/pump join completed.
Sequential module observations remain non-atomic and are not tamper protection.
R17's actual writer GIDs are A `010f6944845e3aee0000000000000803` and B
`010f69441cd18f500000000000000803`, both `rmw_fastrtps_cpp`.

| Final accounting | R15 A / B | R16 A / B | R17 A / B |
| --- | --- | --- | --- |
| Ingress | 150 / 209 | 92 / 133 | 52 / 53 |
| Admitted = charged = refunded | 150 / 204 | 78 / 133 | 52 / 53 |
| Adapter rejected | 0 / 5 | 14 / 0 | 0 / 0 |
| Send errors | 0 / 0 | 0 / 0 | 0 / 0 |

R15 B's five and R16 A's fourteen rejected callbacks retain exact original
identity, entered/returned records, AdapterPhase branch 9, InvalidState status 5,
Closing phase 4 and `bridge_called=0`. They were pre-Bridge rejections, not
charged receive records. `full_host` and connection/counter reconciliation PASS
does not imply every ingress was admitted or identify rejected payloads.
Payload causality remains NOT_OBSERVED. R17's zero rejections do not explain
earlier failures or rejections.

Every final process retains two DDS Error lines:

```text
[RTPS_PARTICIPANT Error] No unicast locators to create unique flows -> Function eprosima::fastrtps::rtps::RTPSParticipantImpl::createAndAssociateReceiverswithEndpoint
[DATA_READER Error] Problem creating associated Reader -> Function eprosima::fastdds::dds::DataReaderImpl::enable
```

Each R15/R16 process and R17 A also records one EOF and one SOCKET_ERROR
WSA10004 receive termination; R17 B records two EOFs. Complete timestamped
errors/decisions/terminations remain in stderr and
`final-test/RUNTIME_CLOSE_AND_REJECTION_DETAILS.json`. Business PASS is not a
zero-error-stream or strict unique-flow capability claim.

## Commands, artifacts and evidence

All `build/...` paths below are relative to outer project root
`C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`, not nested W3.
Local ignored evidence is not promised in a fresh clone. Canonical records are
under `build/r17-publication-api/final-test`:

- `r15/r16/r17-configure.intent.json` and corresponding `*-build.intent.json`,
  result/stdout/stderr files record each exact tool command and outcome (the
  names use `r15-`, `r16-`, `r17-` prefixes).
- `*-RUNTIME_PREDECLARATION.json`, `*-runtime-launch.intent.json` and each
  `*-runtime/result.json` preserve accepted commands, full environment, child
  arguments, modules, hashes, deadlines and actual results.
- `SERIALIZED_API_OBSERVATIONS.json` and
  `SERIALIZED_NUMERIC_DIAGNOSTIC_CHECK.json` bind four PRE/POST diagnostic pairs
  to successful serialization receipts; `*-COMPILER_INPUTS.json` lists actual
  compiler reads. Selected serializer/publisher inline headers are prebound;
  other transitive headers are explicitly not all prebound.

The final snapshot is `build/r17f/source`; fresh build directories are
`build/r17f/r15`, `r16` and `r17`. Configure uses Visual Studio 17 2022/x64,
Release, `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL`, installed Host
`build/r13pf/dp`, DDS `build/g2a1/host214-install`, base ROS and pixi packages.
The actual R17 build command is:

```powershell
& 'C:/pixi_ws/.pixi/envs/default/Library/bin/cmake.exe' --build 'C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport/build/r17f/r17' --config Release --target ros2_serialized_acceptance -- /m:1 /nr:false
```

Runtime uses `C:/pixi_ws/.pixi/envs/default/python.exe -B` with the snapshot
`tests/ros2_host/run_lifecycle_acceptance.py`, fresh `--exe`, published
`--bridge`/`--host-prefix build/r13pf/dp`,
`--fastdds-prefix build/g2a1/host214-install`,
`--ros-prefix build/ros2-target/h3/runtime`, pixi prefix, fresh output and
`--scenario serialized`. Exact paths are absolute in the receipt. RMW is
`rmw_fastrtps_cpp`, domain 201. The wrapper/runner bounds remain 80/65 seconds;
R17's application has a 50-second outer deadline with shorter bounded waits.
The full PATH and environment removals/overrides are recorded, not inferred
from the current shell.

| Artifact | SHA-256 |
| --- | --- |
| `build/r17f/r17/Release/ros2_serialized_acceptance.exe` | `c95e241400b0682aba3fc049324213e8dca2c4292244d3908499248b3f96208a` |
| `build/r17f/r15/Release/ros2_lifecycle_acceptance.exe` | `22141e717f5a7af3e32789d2807592f90e89e742af7a3830b399bed46694b939` |
| `build/r17f/r16/Release/ros2_lifecycle_publisher_acceptance.exe` | `184ac78b24201071e45f046f0863575e00ea4d5650c23ad6b9b64770829fdfb4` |
| `build/r13pf/dp/bin/fastdds_dsoftbus_ros2_host_bridge.dll` | `08945ea94381ac7264ad07c8c0818c1e717d847284d2402bdabd37c948c87e63` |
| `build/g2a1/host214-install/bin/fastrtps-2.14.dll` | `60408a27139ce59bf9aaa92c080e799d98f1531b7318dfbf0abf7308e7d555a4` |
| `build/ros2-target/h3/runtime/bin/fastcdr-2.2.dll` | `badcea3777c7a6b23961310dc5857e07c064d94af9babe52d2c097d8842d8c71` |

Actual selected ROS/RMW/type-support module identities are in each runtime
barrier/input map. Final guards passed for 716 source files, 1208 dependency
records, 45 package files and three selected tools (Python/CMake/CTest), not
the entire compiler/OS. The 1165 protected evidence entries across fourteen
manifests, including original FAIL, FIX1 initial PASS and review, remain intact.

| Receipt under `build/r17-publication-api` | SHA-256 |
| --- | --- |
| `implementation-fix1/SOURCE_FREEZE.json` | `10cabe53d778b2e84ab4352c8a2bd64487e1fe81a89660e287cb6a4da65bcd43` |
| `code-review/REVIEW.json` | `38dbb7c0b7ef90cb953bc9dbed5bc179364a71d593059298ba3f5936b8791330` |
| `final-test/FINAL_TEST_RESULT.json` | `a164d4f2b339eea7010b62d96feecee39443c3ab1fce8015bb48902572cb6000` |
| `final-test/EVIDENCE_MANIFEST.json` (141 entries) | `d780cb00814db8788bb1c4ef6e1465fe8716eb1ba47b4d4c4396c0e01c9d6d8f` |
| `final-test/SEAL_RECHECK.json` (separate supplemental receipt) | `249ba245d9b0eefc290ae1a0c87cf8cfcb26b9a854348d2d1dd9d4d8ce5726ac` |

## Device and unresolved project acceptance

Main's read-only `hdc list targets -v` ran from 2026-10-15T14:12:16.172931Z to
14:12:20.664865Z, exit 0, empty stdout/stderr, no observed target entries. It
does not diagnose physical disconnection. There were no board commands, state
changes or permission rechecks. The last known DISTRIBUTED_DATASYNC DENIED
observation remains the 2026-10-14T21:51:07Z record for both boards.

R14's strict 20 writes / 19 takes FAIL and ten opaque frames per side FAIL remain
unchanged. This default-profile serialized API result neither reruns nor
supersedes that secure workload or visibility limit. Native/device/cross-device
acceptance remains BLOCKED, and no aggregate completion percentage is claimed.
