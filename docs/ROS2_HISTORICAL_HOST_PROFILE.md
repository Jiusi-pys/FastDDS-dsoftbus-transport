# Historical Windows Host profile qualification (R13 P1)

As of 2026-10-15, the explicit `historical-2147-security-statistics` profile is
IMPLEMENTED, BUILDS and HOST_TESTED. Independent final testing reports
`SCOPE_QUALIFIED_FINAL_HOST_P1_PASS` and `Commit gate: PASS`. This covers real
ROS/DDS entities over the Windows opaque-byte simulated transport underneath
the DSoftBus adapter. It is not an OpenHarmony DSoftBus communication test.
DEVICE_TESTED=false; native/cross-device acceptance is BLOCKED; the overall
project remains NOT VERIFIED. Main's graph/pre-commit, commit, push and CI
receipts are separate and are not asserted by this document.

The candidate is based on `af8ad6ead40cf3707bb93fd9056cd3451a56b862` and freezes
13 implementation/test files. [ROS 2 Host integration](ROS2_HOST_INTEGRATION.md)
describes the existing Host seam; [the V1 plan](FASTDDS_DSOFTBUS_TRANSPORT_V1_PLAN.md)
retains the Transport-only boundary. Earlier default-profile service, parameter
and [Action acceptance](ROS2_ACTION_ACCEPTANCE.md) are separate evidence, not
historical-profile business workload results.

## Selection and package contract

| Selector | Dependency contract | Scope |
| --- | --- | --- |
| `default-2145` (default) | Exact Fast DDS 2.14.5; existing Fast CDR 2 requirement | Existing Windows Host behavior retained; tested here with SECURITY OFF / STATISTICS OFF |
| `historical-2147-security-statistics` | Exact Fast DDS 2.14.7 and Fast CDR 2.2.8; SECURITY ON / STATISTICS ON headers | Explicit MSVC x64, Release, `/MD` qualification against the original local dependency artifacts |

The historical build requires these CMake settings in addition to the usual
Host build dependencies:

| Setting | Required value |
| --- | --- |
| `FASTDDS_DSOFTBUS_HOST_PROFILE` | `historical-2147-security-statistics` |
| `FASTDDS_DSOFTBUS_HISTORICAL_FASTDDS_PREFIX` | Absolute original DDS installation prefix |
| `FASTDDS_DSOFTBUS_HISTORICAL_FASTCDR_PREFIX` | Absolute original CDR compile installation prefix |
| `FASTDDS_DSOFTBUS_HISTORICAL_FASTCDR_RUNTIME_DLL` | Absolute original runtime CDR DLL; its bytes must match the compile DLL |
| `fastrtps_DIR`, `fastcdr_DIR` | Package configuration directories belonging to those declared prefixes |
| `CMAKE_INSTALL_PREFIX` | Explicit separate destination, outside the DDS/CDR dependency prefixes |
| `CMAKE_CONFIGURATION_TYPES` | `Release` for the tested Visual Studio generator; single-configuration generators require `CMAKE_BUILD_TYPE=Release` |
| `CMAKE_MSVC_RUNTIME_LIBRARY` | `MultiThreadedDLL` |
| `DSOFTBUS_SOURCE_DIR` | Explicit existing authorized public Socket header tree |

In the qualification environment, the original prefixes are `build/r3ros/dp`
and `build/r3ros/cp`. The runtime CDR is
`build/r3ros/runtime/bin/fastcdr-2.2.dll`, a distinct physical file with the same
SHA-256 as `build/r3ros/cp/bin/fastcdr-2.2.dll`. The default `h3` runtime is not
a substitute. No dependency installation is overwritten.

The historical package binds the selected package roots, imported headers,
Release import libraries/DLLs and recorded dependency hashes. It exports
`FastDDSDSoftBusTransportRos2Host_PROFILE`, `*_FASTDDS_VERSION`,
`*_FASTCDR_VERSION`, `*_HISTORICAL_PREFIX_BOUND` and separate
`*_FASTCDR_COMPILE_DLL`, `*_FASTCDR_COMPILE_DLL_SHA256`,
`*_FASTCDR_RUNTIME_DLL`, `*_FASTCDR_RUNTIME_DLL_SHA256` variables, where `*`
is `FastDDSDSoftBusTransportRos2Host`. Installed consumers recheck the bound
dependencies. This profile is intentionally bound to absolute prefixes and
is not relocatable.

Before allocating its Owner or dereferencing the factory's C++ QoS object,
the historical Bridge checks the actual loaded factory path, Hook ABI 21405,
same-module security export and the explicitly selected runtime CDR path.
Forwarded factory exports are rejected. `FastddsDsoftbusHookSecurity` exposes
the Bridge's compiled `HAVE_SECURITY`. These checks do not attest the complete
statistics ABI: the original factory has no statistics-mode attestation export.
Configure-time hashes and path identity are not runtime protection against
replacement of bytes at the same path, nor full source-to-DLL reproducibility.

The historical descriptor advertises 65535 bytes; default remains 65536 and
the SDK envelope remains 65536. The original secure receiver's uint16 maximum,
NetworkFactory capacity selection and facade capacity guard support this
limit. Initial logs reported `InsufficientReceiverCapacity`; the numeric
65535 explanation is a source-chain inference, not a captured capacity value.
There is no DDS/RTPS/QoS semantic change, public FastddsHook edit or native
source change in this candidate.

Review finding R13-A1 showed that a Windows junction bypassed the original
lexical install-containment check, and CMake `REAL_PATH` alone did not resolve
it. The corrected historical branch rejects symlink/junction components at
the install prefix, declared DDS/CDR roots and their ancestors, including an
existing ancestor of a missing destination. Existing non-directory components
are rejected; ordinary existing/new separate destinations still work. The
default branch is unchanged. This is configure-time protection, not install
race protection or a recursive audit of existing destination children.

## Acceptance matrix

| Capability | Status | Evidence and limit |
| --- | --- | --- |
| Default compatibility and historical build/install | Supported, Host scope | Fresh builds, installs and 37 CTests per profile; default external C++17 consumer configure/build/run |
| Profile, dependency and install-alias rejection | Supported, bounded controls | Real configure/header/installed-package negatives plus separately counted synthetic cases below |
| Historical ROS discovery | Partially supported | One actual two-process entity-matching and service-ready run; zero business samples, requests and callbacks |
| Historical factory failure cleanup | Supported for one failure branch | One actual direct-DDS disabled duplicate-ID construction failure; exact cleanup oracle |
| Service/action, pub/sub data, serialization, large messages | Unverified in this profile | No historical business workload executed; earlier default results cannot fill this row |
| Reconnect, broad QoS, concurrency and resource stress | Unverified in this profile | Bounded discovery/cleanup is not these acceptance scenarios |
| Full statistics ABI and original A/04 equivalence | Unverified | Bound original modules observed; complete source-to-binary reproduction and original A/04 execution absent |
| DATA_FRAG-to-callback/payload causality | Unverified | No new DATA_FRAG acceptance; payload causality remains NOT_OBSERVED |
| Historical package relocation | Unsupported by design | Absolute dependency and runtime path binding |
| Native OpenHarmony / cross-device communication | BLOCKED, unverified | No R13 device execution; last permission observation was DENIED on both boards |

Final independent test counts are intentionally separate:

- Default and historical fresh build/install PASS, 37/37 CTests each. Six
  synthetic DLL models are included in each 37; they are not actual runtime
  module-identity observations.
- 60 parser/helper tests; 16 synthetic module configure controls; four pure
  diagnostic matcher models.
- 13 actual CMake project-entry configurations using wholly synthetic
  dependencies: nine expected negatives and four positives, no installation.
  These include junction, reverse dependency alias, ancestor, directory-boundary
  and default-profile controls and are not included in CTest.
- Ten real-dependency configure negatives, two shadow-header compile negatives,
  three copied installed-package metadata/runtime negatives, and one consumer
  rejection of the default profile.
- Separate Fast DDS 3.6.2.0 / CDR 2.3 shared-core regression: one
  `dsoftbus.transport_facade` CTest, 21 actual groups, original 15-second timeout.
  This is not evidence of historical 2.14.7 ABI or device behavior.

The actual ROS pair used original run `0123456789abcdef`, domain 201 and
enclaves A/B. A PID 54928 and B PID 38840 exited 0. Both reached entity matching
and service readiness, with no business send/request/callback. Actual live
captures contain 111 mapped modules per process, including the selected DDS,
runtime CDR, new Bridge, RMW, type support, libssl and libcrypto. Strict
`full_host` endpoint/counter reconciliation passed: A 96 and B 94 ingress
callbacks were admitted, charged and refunded; final refusals were zero.
Public deletion, runtime close, pump join and exclusive port rebind passed,
without forced termination. The final stderr error-pattern count is zero for
this run only. Initial FIX3 A's two pre-Bridge Closing/InvalidState refusals
remain recorded with unknown payload causality; final zeros do not explain them.

The direct-DDS probe PID 68416 exited 0. With autoenable disabled, a second
fixed participant ID failed with null/GUID error and the exact
`FAILED_CONSTRUCTION_QOS_FRAME` marker. One RuntimeClose returned 0, all five
SDK entry counters were zero, first-participant deletion and saved factory
QoS restore/readback succeeded, and local QoS objects ended before the module
barrier. The probe was freshly linked. Its sealed CMake source still references
three diagnostic headers in the older snapshot: compiler read tlogs confirm
those actual paths; old/final/original headers have equal hashes but are
distinct files. This does not establish complete ABI/source-to-DLL identity.
The earlier Listen-holder trigger was withdrawn before execution (zero runs)
because it could not reliably trigger factory FailedConstruction. The tracked
consumer's `fail-create` mode is not validated by this direct-DDS result.
ROS failed node construction, Listen failure and security-handshake failure
remain unverified.

## Commands, artifacts and evidence

All `build/...` paths below are relative to the outer Windows project root
`C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`, not the nested W2
worktree. Evidence is local and ignored by Git; the named manifests bind the
actual files rather than promising their presence in a fresh clone.

The final source snapshot is `build/r13pf/s`; default build/install are
`build/r13pf/db` and `build/r13pf/dp`; historical build/install are
`build/r13pf/hb` and `build/r13pf/hp`; historical consumer build is
`build/r13pf/hc`. Tested tools are VS2022 x64 Release `/MD` and CMake/CTest from
`C:/pixi_ws/.pixi/envs/default/Library/bin`; Python is
`C:/pixi_ws/.pixi/envs/default/python.exe`. Builds use one worker and disabled
MSBuild node reuse. The exact argv, working directories, environment, deadlines,
exit codes and stdout/stderr are preserved in:

- `build/r13-profile/final-test/{default,historical}-{configure,build,ctest,install}.intent.json`
  and their corresponding result/log files; consumer and negative-control
  `*.intent.json` records are in the same directory.
- `build/r13-profile/final-test-port-qualified/RUNTIME_PREDECLARATION.json`
  and `FAILURE_RUNTIME_PREDECLARATION.json`, plus `success-RESULT.json` and
  `failure-duplicate-id-RESULT.json`. These specify the actual runtime commands,
  module barriers and unchanged 30-second discovery / 30-second barrier /
  60-second outer success bound, and 15-second barrier / 30-second outer
  direct-DDS bound.

For example, the executed historical build uses `cmake --build build/r13pf/hb
--config Release -- /m:1 /nr:false`; the full configure record explicitly selects
the profile, original `dp`/`cp` roots, separate runtime CDR and install prefix.
The runtime order places original `dp/bin`, `r3ros/runtime/bin`, pixi libraries,
`cp/bin`, `rp/bin`, the new Bridge, original OpenSSL and base ROS on PATH;
AMENT uses `rp` then base ROS. Full records, rather than an abbreviated command
line, are the reproduction authority.

| Artifact (relative to outer root) | SHA-256 |
| --- | --- |
| `build/r13pf/dp/bin/fastdds_dsoftbus_ros2_host_bridge.dll` | `08945ea94381ac7264ad07c8c0818c1e717d847284d2402bdabd37c948c87e63` |
| `build/r13pf/hp/bin/fastdds_dsoftbus_ros2_host_bridge.dll` | `79bf2bf2c3036c8a2c6e08b2d78f247d57d2346739f8481caa2430abb270599b` |
| `build/r13pf/hc/Release/ros2_historical_profile_consumer.exe` | `6022e3a0b51813e36b9dd14db71ffc47f493a45b16f1a0d311c7ebb12e531689` |
| `build/r3ros/dp/bin/fastrtps-2.14.dll` | `ba897a450fce76634e29e48b312a1a5e9a7b1a8d286da97eda44ac80a1feb2d9` |
| `build/r3ros/runtime/bin/fastcdr-2.2.dll` | `16abecf30e86083c96406dd8e467b522b744fe1b55f0d09521ebaa625f8e0559` |

Canonical final evidence is
`build/r13-profile/final-test-port-qualified/FINAL_TEST_RESULT.json`, SHA-256
`185ec13c4bc660161385749187be11244d49b015b67f99f56c061f60c6293a27`.
Its 57-file manifest hash is
`fbcd2853e9ae991fdfa1910c912124832a9b1a6769f21d3ca49dd3ffc85a93fc`.
It incorporates the prior final-test build/control evidence: 304-file manifest
`28a44d130929594e5e85aeb4765565440554c53780a9d247357ec6c96c32785f`
and `final-test/RAW_BUILD_CONTROL_MANIFEST.json` containing 1320 raw build/control
files. `implementation/SOURCE_FREEZE_REVIEW_FIX.json` has hash
`8b5b97b3230799eec78a31b1d88430413dcaa332fee322c584893ac32da16950`.
Independent review closed R13-A1 in `review-fix/CLOSURE.json`, hash
`a3a5f6929cc7db1f4be0cd078d6f5fd1383661467b52f708dcd4b86410bbd33d`.
All these paths are under `build/r13-profile` unless fully written above.

Postguards cover 644 source files (including the 13 candidates), 35962 selected
dependency files, nine tools, 320 separate core-prefix files, 26 public policy
inputs, nine original probe inputs and the sealed prior evidence. Root/W1/W2
heads, branches and statuses were unchanged by final testing.

## Preserved failures and remaining boundaries

The older `final-test/FINAL_TEST_RESULT.json` remains BLOCKED. Its success port
precheck returned WinError 10013 on 60131/60132 before starting any ROS/DDS
children; neither planned runtime executed. Main's read-only port diagnostic
found Windows TCP exclusion 60090-60189, also covering failure ports 60141/60142.
One simultaneous exclusive-bind diagnostic succeeded for new ports 60931/60932
and 60941/60942. Main accepted revised plans changing only ports and output
paths. Source, binaries, policies, deadlines and oracles stayed unchanged;
each actual runtime then executed once and passed. See
`MAIN_PORT_EXCLUSION_DIAGNOSTIC.json`,
`MAIN_PORT_QUALIFIED_RUNTIME_ACCEPTANCE.json` and
`final-test-port-qualified/PLAN_DIFF.json`. This is an explicit environment
precondition correction, not a retry of a failed business run. No network
permission, exclusion or security setting was changed.

Three initial FAIL rounds remain under `initial-test`, `initial-test-fix1` and
`initial-test-fix2`: Windows/Winsock include ordering; a wrapped CMake diagnostic
matcher; and the historical receiver capacity mismatch. Their fixes and the
subsequent FIX3 evidence are preserved. INVALID_FIXTURE preparation attempts
(dependency selection, control prerequisites and core configuration) are not
production failures or passing controls. Final tlog collection and failure-plan
preparation errors are retained too. No failed record was relabeled PASS.

Offline policy prerequisites passed four signed-policy verifications and ten
certificate-chain/time checks against the configured local CA. They are not
device permission or runtime security acceptance. The original policy signs
discovery while user-data and metadata protection are NONE; SECURITY ON does
not imply payload encryption. No private-key read/hash diagnostics were taken.

The last device permission observation, 2026-10-14T21:51:07Z, recorded
`ohos.permission.DISTRIBUTED_DATASYNC` DENIED for `fastdds_dsoftbus_probe` on both
registered RK3588A boards: A `3e01ff55454d202020104033bf453b00`, B
`3e01ff55454d202020104433991c3b00`. It was not refreshed in R13. Device availability
and permission recovery require their own evidence; no deployment, permission
change or device test occurred here. Native build is N/A for this additive
Host-only source list; this does not constitute native build or communication
PASS. Original A/04 and DATA_FRAG windows remain NOT_RUN. No aggregate completion
percentage or complete ROS 2 support claim follows from this P1 qualification.
