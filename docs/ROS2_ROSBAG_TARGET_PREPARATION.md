# R19 ROS 2 target rosbag preparation

Evidence snapshot: 2026-10-15, after wrapper canonical final at 18:46:48 UTC.
This records build/configuration prerequisites. **Actual rosbag recording,
reading or replay over native DSoftBus is not yet verified.** The workspace
source baseline is published R18 `48c01f2597bf16db1ed35fbd79cc5c32826f0ab8`.
Preparation sources, binaries and receipts below live in the original checkout's
ignored `build/r19-rosbag-preparation/`; they are local evidence, not artifacts
included in the remote Git repository by this documentation commit.

## Qualified standalone libraries

Pinned sources are yaml-cpp 0.8.0, console_bridge commit `0828d846` (package
version 1.0.2), and SQLite 3.37.2. Independent initial execution completed each
native configure/build/install. The selected OHOS build helper supplies SONAME
flags; SQLite uses a minimal standalone CMake build. No Linux system-name
substitution or DDS/RTPS behavior change is part of this work.

| Installed target library | Static identity | SHA256 |
| --- | --- | --- |
| `opt/yaml_cpp_vendor/lib/libyaml-cpp.so.0.8.0` | AArch64 ELF; SONAME `libyaml-cpp.so.0.8` | `013bf126a92d304fe5a7dbd856e90f920fc5b5a45ff323f303d78ab10aa6a7a8` |
| `opt/console_bridge_vendor/lib/libconsole_bridge.so.1.0` | AArch64 ELF; SONAME `libconsole_bridge.so.1.0` | `e01d7637d977d5f0cb9a3e77c66809e7ffccf72f50ba62edd777432b3eda23db` |
| `opt/sqlite3_vendor/lib/libsqlite3.so` | AArch64 ELF; SONAME `libsqlite3.so`; NEEDED `libc.so` | `a6e423cfb0120b7c8b1b3e6cd80c318481007ccaea6b519d069d7bc31637d4e7` |

Independent vendor code review found no blocking findings for this scope.
Post-review final static validation ran three fresh readelf commands (each exit
0), with zero rebuilds/reinstalls and preserved source/artifact identities.
It did not execute these libraries on a host or board. Full SDK/sysroot closure
and implicit linker resolution were not attested by that standalone stage.
The original yaml SONAME failure, console filename collector error and object
path-separator fixture errors remain in their original sealed records.

## Wrapper configuration contract

The copied three vendor wrappers and isolated acceptance guard validate the
already-built target libraries. They are not an installed public security API.
The frozen configuration requires these four explicit `CMAKE_PREFIX_PATH`
entries, in order, where P is the original checkout's
`build/r19-rosbag-preparation/vendor-wrapper-execution/install`:

1. `P`
2. `P/opt/console_bridge_vendor`
3. `P/opt/yaml_cpp_vendor`
4. `P/opt/sqlite3_vendor`

Actual imported location, hash, headers, type and relevant config/SONAME metadata
are checked. SQLite's stock MODULE lookup may select an inner CONFIG; the
adversarial priority case proves that the wrong actual location is rejected.
SQLite's imported type is UNKNOWN_LIBRARY, with SONAME separately established
by ELF evidence. The YAML extras path was adapted to the actual `lib/cmake`
location. The SDK's actual root modes are PROGRAM NEVER, other searches ONLY.

Initial wrapper execution stopped after a successful console configure because
the SDK read `oh-uni-package.json` without a frozen binding. Its BLOCKED result
remains valid. An additive FIX1 bound the metadata and audited already-recorded
trace inputs, retaining the original configure as explicitly REUSED. This does
not retroactively attest the metadata bytes at the original read instant.
The remaining eight wrapper commands exited 0; wrapper builds were registration/
no-op steps, not another native library compilation. The metadata is outside
the 222 recorded Ninja regeneration dependencies, so it has separate guards.

Initial FIX1 executed 19 consumer configurations once: three expected successes
(exit 0), and sixteen expected rejections (exit 1 with specific diagnostics).
Per wrapper the negative cases cover missing opt payload, wrong source hash,
unrelated imported candidate, forced vendor rebuild, and aggregate-prefix-only
use; the sixteenth is SQLite CONFIG priority. Their expected rejection is a
successful negative test, not an unreported build failure. The recorded trace
does not enter the vendor download branch; this is not an OS network sandbox
or complete transitive Python module provenance claim.

Formal independent code review found no actionable/blocking finding and required
no source fix. Canonical final independently replayed all 19 immutable raw
configuration receipts, checked 4799 current bindings before/after, and verified
the 99-file aggregate. It performed **zero new configure/build/install/fixture/
target/device executions**. Result: PASS_SCOPED_CONFIGURATION_CONTRACT_FINAL;
**R19-V1 CLOSED only for this frozen explicit-four-prefix/target-identity scope**.

The 57 native payload files and wrapper registration files form that 99-file
aggregate. SATISFIED vendor handling did not generate vendor opt/loader hooks
or vendor-package markers. Existing pkg-config files retain their original
library-install paths. Thus aggregate-only lookup, arbitrary relocation and
setup-only runtime loading are not supported by this configuration contract.
This result is a build prerequisite, not ROS_PACKAGE_READY or rosbag runtime PASS.

## Capability matrix and remaining work

Here SUPPORTED applies only to the named tested behavior. PARTIAL identifies a
narrow qualified subset. UNSUPPORTED identifies an explicit contract/policy
limit; UNVERIFIED does not mean inherently impossible.

| Capability | Status | Reason / next evidence |
| --- | --- | --- |
| Three pinned native standalone library builds | SUPPORTED / BUILDS, static PASS | Independent build, review and final static recheck; no runtime qualification. |
| Four-prefix wrapper discovery and exact imported targets | SUPPORTED / HOST_CONFIGURATION_TESTED PASS | Three positive and sixteen negative cases; final independent raw replay. |
| Complete ROS vendor integration | PARTIAL | Configuration and aggregate bound; vendor opt/loader hooks absent. |
| Aggregate-prefix-only discovery in this frozen harness | UNSUPPORTED | Explicitly rejected by contract; four prefixes required. |
| Arbitrary relocation, setup-only library loading | UNVERIFIED | pkg-config retains original paths; no loader acceptance. |
| TinyXML2 vendor alias, class_loader, pluginlib | UNVERIFIED / implementation preparation | FIX1 design reviewed; stage configure/build/API/resource probes not yet run. |
| Fixed String typed Writer/Reader with SQLite, no compression | UNVERIFIED / PLANNED | Next bounded rosbag consumer; no actual rosbag I/O evidence yet. |
| Plugin XML/ament-index/typesupport resource discovery | UNVERIFIED | Required beyond ELF checks before a plugin/rosbag runtime claim. |
| Full rosbag CLI / rosbag2_transport / arbitrary types / compression | UNVERIFIED / outside first planned acceptance | No completeness claim from the fixed-type path. |
| Generic/loaned/zero-copy recording or replay | UNVERIFIED | Not measured by these library/configuration tests. |
| Native discovery/reconnect, pub/sub, service/action and QoS under recording | UNVERIFIED for R19 | No R19 runtime or DSoftBus communication window. Historical other-stage results are separate. |
| Serialization, large/fragmented messages, concurrency and resource release under rosbag load | UNVERIFIED | Fixed-type future test must establish its own bounds and cleanup. |
| RK3588A rosbag or cross-device record/replay | UNVERIFIED / NOT_RUN | Device-tested loader rejection is a separate R18 claim. |
| Session API fallback | UNSUPPORTED by project policy | Only DSoftBus Socket API may carry transport traffic. |

The next dependency design targets tinyxml2_vendor 0.9.2, class_loader 2.7.1 and
pluginlib 5.4.6 before rosbag2 storage/cpp/SQLite 0.26.11. The selected TinyXML2
6 export is bare `tinyxml2`, while pluginlib expects `tinyxml2::tinyxml2`.
The proposed adapter must bind the exact target location/hash/includes/SONAME,
then prove the used API compile/link subset, repeated/nested/sibling scope and
negative controls. It cannot establish general TinyXML2 9 compatibility.
pluginlib provides an INTERFACE target and a `list_plugins` executable; absence
of `libpluginlib.so` is not itself a failure.

The original design review's R19-N1 identified missing SDK metadata/platform
inputs. FIX1 freezes 371 inputs, adds unknown-input stop controls and removes
duplicate package-dir arguments. Its independent review closes R19-N1
**FOR_DESIGN only**. The plan/review's then-pending wrapper-final status remains
an historical snapshot; the later canonical wrapper final is separately bound.
Current implementation preparation does not imply that the proposed alias,
new builds, plugins or rosbag tests already pass.

Actual native communication still requires the complete current project-only
permission record and a supported lossless restore route. Conditional user
authorization does not remove these prerequisites; no permission changes are
part of R19 preparation. Overall ROS 2 over native DSoftBus: **NOT VERIFIED**.

## Local evidence ledger

Paths below are relative to the ignored preparation directory named above.
Raw command argv, environment, stdout/stderr, exit status and preserved failures
remain in the corresponding sealed test directories. These hashes identify
local evidence; remote readers need the separately retained files to recheck it.

| Receipt | SHA256 |
| --- | --- |
| `vendor-library-final-test/RESULT.json` | `96820ee89aa63109fa010dd41a8375f8b230147cdaf940fd3eaacf86b3453f7f` |
| `vendor-library-final-test/EVIDENCE_MANIFEST.json` (92 files) | `e540ebd0316c2f68f71befdc0111a9b7e0733f8ab1adf08e65399e067a18f0d2` |
| `wrapper-initial-test/RESULT.json` (original BLOCKED) | `dde88dbb988a0aa4581bd97ae85250b07f93afd7797a5d438f88047a61cecdc6` |
| `wrapper-input-fix1-test/RESULT.json` | `8a18d47a04e25a6582e131daa0d400d5989f462b35584012ac0126b076ceda76` |
| `wrapper-code-review/RESULT.json` | `95a4af18fab0f0fe59fa4f3a97cd71ef3c495bc48326d60fc1f2960d0b5368f4` |
| `wrapper-final-test/RESULT.json` | `6e2973c298eed035337723a481fe6487c76c72ad6ee02da922e5c458a9843174` |
| `wrapper-final-test/EVIDENCE_MANIFEST.json` (100 files) | `bff1a7a4e4401c433dbf992710666864ebc8ca2545b5233266cca64a916943d5` |
| `wrapper-final-test/AGGREGATE_MANIFEST.json` (99 files) | `2b8c7cdd08dfe9c8eacf20e22a7101c823856f524c3caca76cf40ddc42bc0ed6` |
| `next-dependency-plan-fix1/PLAN.json` | `b4ea4909aae6461423e3bb04dfdba3ad352996860f267f469356fe0aa1fb6c58` |
| `next-dependency-fix1-review/RESULT.json` | `8b75deabd227b59c87223d7c7e9270e1d7aa39bc1a1bad61e22b0fd552cebc27` |

This document records bounded preparation evidence. Its documentation readiness
does not grant an implementation, native communication or overall product gate.
