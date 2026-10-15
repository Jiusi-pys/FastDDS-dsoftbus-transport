# R19 ROS 2 target rosbag preparation

Evidence snapshot: 2026-10-15, after pluginlib canonical final at 23:19:16 UTC.
This records build/configuration prerequisites. **Actual rosbag recording,
reading or replay over native DSoftBus is not yet verified.** The workspace
source baseline is published R18 `48c01f2597bf16db1ed35fbd79cc5c32826f0ab8`.
Preparation sources and receipts below live in the original checkout's ignored
`build/r19-rosbag-preparation/`; current class_loader/pluginlib build and install
outputs live in its sibling `build/r19next1/`. These are local evidence, not
artifacts included in the remote Git repository by this documentation commit.

## Current pluginlib static qualification

The fresh `pluginlib-sdk-closure-fix1` SDK23 AArch64 `c++_shared` build has
**PASS_POST_REVIEW_FINAL_STATIC_PLUGINLIB_A** after the initial successful
continuation, independent agentic review and final static replay/ELF checks.
W3's tested baseline was clean detached
`5e96a3aba1af8185a05ed195f114128557344ac8`. No production source changed in
this static continuation. **R19-PT1 remains OPEN overall**; qualification is
limited to this named build and static installation.

The continuation ran seven authorized commands, all exit 0: object dry-run,
one ordinary compilation, actual Ninja dependency collection, link dry-run,
one actual link, ELF inspection and one install. Original successful configure,
compiler-ID and ABI evidence was inherited, not rerun. The resulting object has
669 actual dependency paths. The link reproducer has 22 regular members,
20 bound input files and four selected libunwind archive members. Full ordered
arguments, response selection, actual linker trace, SDK inputs and generated
install mappings were checked; this is an enumerated input closure, not a
claim about all possible process DLLs or SDK files.

The built executable and installed
`build/r19next1/install/pluginlib/lib/pluginlib/list_plugins` are both 391,200
bytes, SHA256 `73e0beb073790cf4203fd7591f9c005a9490f6919e93b4a80872f162530b945a`.
They are ELF64 little-endian AArch64 executables with interpreter
`/lib/ld-musl-aarch64.so.1`, no RPATH/RUNPATH, and exactly eight NEEDED entries:
`libament_index_cpp.so`, `libclass_loader.so`, `libconsole_bridge.so.1.0`,
`librcpputils.so`, `librcutils.so`, `libtinyxml2.so.6`, `libc++_shared.so`,
`libc.so`. All 29 installed files and 15 directories match the exact
source/generated-byte mappings and membership. pluginlib itself is an
INTERFACE target; no `libpluginlib.so` or real test-plugin DSO is claimed.

The original `pluginlib-next-test` **A_BLOCKED** result and its 152-file seal
remain unchanged. Its GATES expected the TinyXML2 alias spelling in the export.
The separately reviewed FIX1 uses CMake 3.28.3 source behavior, actual source,
trace and generated export to distinguish the exact ordered source/build list
`ament_index_cpp::ament_index_cpp;class_loader::class_loader;rcutils::rcutils;rcpputils::rcpputils;tinyxml2::tinyxml2`
from the exact generated/installed list
`ament_index_cpp::ament_index_cpp;class_loader::class_loader;rcutils::rcutils;rcpputils::rcpputils;tinyxml2`.
The last item is the original imported target, not permission to accept an
arbitrary bare library. Target type, alias identity, location, header/library
hashes, includes, SONAME and actual link path remain exact. The continuation
qualifies only static build/install; it does not convert the original failure
to PASS or validate a pluginlib-first consumer directory scope.

Independent post-test review found no blocking findings and no source fix was
required. Final testing replayed the complete sealed raw command, dependency,
tar/ar, ELF, install and correction evidence, then ran two fresh bound
`llvm-readelf --file-header --program-headers --dynamic` commands, one each on
the built and installed executable. Both exited 0 with empty stderr and the
same 4,813-byte stdout as the initial run. It performed zero new configure,
compile, link, install, target or device executions. Collector-only failures,
original helper bytes and narrow corrections remain sealed. Raw-byte hashes
are canonical; LF-normalized mappings explain changes but never replace the
raw seals. The exact known gcc-toolchain unused warning is explicitly classified
and retained; flags were not changed and the warning was not suppressed.

For application/transport behavior: BUILDS=true; HOST_TESTED=false,
DEVICE_TESTED=false, INTEGRATION_TESTED=false, VERIFIED=false. The four
positive/nine negative consumer configurations, real plugin loading/unloading,
pluginlib-first/new-scope behavior, rosbag I/O and target/device execution are
NOT_RUN. `list_plugins` compile/link success does not establish general
TinyXML2 6 alias/API compatibility. The final tester gate is
**PASS_FOR_DOCS_ONLY_PLUGINLIB_STATIC_EVIDENCE**.

The preceding class_loader documentation commit
`5e96a3aba1af8185a05ed195f114128557344ac8` was published to
`origin/codex/flow-encoding-published-20261014`; CI at 21:47:50 UTC was NOT_RUN
(zero workflow/check runs/status contexts). Main's 23:18:07 UTC precheck still
observed that remote SHA. This new pluginlib stage has not been committed or
pushed. Main graph/staging/PRE_COMMIT/publication/CI gates remain pending.

## Earlier class_loader static qualification (21:37 UTC snapshot)

The fresh `class_loader-sdk-closure-fix1` SDK23 AArch64 `c++_shared` target
passed initial configure/build/link/install, independent agentic review (no
actionable findings), then a fresh independent final static recheck:
**CLASS_LOADER_TARGET_INPUT_BUILD_INSTALL_FINAL_STATIC_PASS**. W3's tested
baseline was clean detached `473312f2ab3c6bf9edcad65e4504c82226f41ee3`.
This closes R19-PT1 only for this named fresh build/static-install subset;
**R19-PT1 remains OPEN overall**. No production source changed in this stage.

Final testing reused the unchanged successful build: zero new configure,
compile, link or install commands. Its new bound `llvm-readelf -h -d` exited
0 with empty stderr. The installed artifact is original-checkout
`build/r19next1/install/class_loader/lib/libclass_loader.so`, 268,680 bytes,
SHA256 `21123fb3e235a4ec3bfc15b638e71706986bcf9ba560458bb19100e0bdc59b64`.
It is ELF64 little-endian AArch64 DYN, SONAME `libclass_loader.so`, with no
RPATH/RUNPATH. NEEDED is exactly `libconsole_bridge.so.1.0`, `librcpputils.so`,
`librcutils.so`, `libc++_shared.so` and `libc.so`.

The final replay matched 7470 plan records (7437 unique paths), 7479 protected
files, 64 SDK absences, 320 old output members, 112 current outputs and all 27
installed files. Four ordinary AArch64 objects have 617 unique actual compiler
inputs. Three actual link archives contain 11/15/21 members; original paths,
ordered library selection, scripts, archive members and complete arguments
match. Canonical `FULL_ARGUMENT_CHECKS.json` retains ABI `--trace`, giving
44/48/53 ordered tokens. These are enumerated input closures, not a proof of
all Windows process DLL inputs or every possible SDK file.

Source, raw trace and installed public dependencies agree exactly on
`console_bridge;rcpputils`. The export set is `class_loader`, the generated
filename is `class_loaderExport.cmake`, and the imported target is
`class_loader::class_loader`. All installed bytes, generated origins, target
location/SONAME/include path and the empty ament marker were independently
checked. Windows `.bat` setup and prefix-resource metadata are qualified only
as generated bytes; usable OpenHarmony activation is not established.

The earlier class_loader run remains **BLOCKED_ACTUAL_COMPILER_AND_IMPLICIT_LINK_INPUTS**;
identical DSO bytes do not retroactively qualify its missing implicit-header
and link-input provenance. The fresh sequence had two configure attempts:
the first missing-directory setup failure exited 1 with zero generated files;
the second exited 0 after the declared setup repair. Three collector failures
(EXPORT-set/filename confusion, correction indentation, and an invented public
vendor dependency) were corrected against frozen source/trace contracts without
weakening the original GATES. The review's checker errors and final tester's
first 44/48-versus-43/47 count assertion failure remain preserved. Full ordered
argument equality passed separately; 43/47 excludes `--trace` and is not the
canonical complete comparison. See the sealed review/final reports for these
corrections and disclosed historical timing gaps.

BUILDS=true; HOST_TESTED=false, DEVICE_TESTED=false, INTEGRATION_TESTED=false,
VERIFIED=false for application/transport behavior. pluginlib, consumer probes,
rosbag, target execution and HDC were NOT_EXECUTED. Final tester's
**PASS_FOR_DOCS_ONLY_CLASS_LOADER_STATIC_EVIDENCE** grants only the next
documentation stage; Main graph/staging/pre-commit/publication/CI remain pending
for this update. Native communication and overall ROS 2 remain NOT VERIFIED.

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
| TinyXML2 vendor metadata installation | PARTIAL / metadata-only initial PASS | Earlier parser-FIX1 execution installed 15 files with phony build steps; no new native TinyXML2 compilation or consumer/API qualification. |
| Fresh class_loader SDK23 build and static install | SUPPORTED / BUILDS, final static PASS | Named fresh target only; review and final replay qualify inputs/ELF/27 install files. Overall R19-PT1 OPEN. |
| Fresh pluginlib SDK23 build and static install | SUPPORTED / BUILDS, final static PASS | Named build only; 669 actual dependencies, built/installed ELF and 29 installed files; overall R19-PT1 OPEN. |
| class_loader/pluginlib consumers and runtime | UNVERIFIED / NOT_RUN | Planned four positive/nine negative consumer cases, pluginlib-first directory scope and real plugin loading/unloading remain unexecuted. |
| Fixed String typed Writer/Reader with SQLite, no compression | UNVERIFIED / PLANNED | Next bounded rosbag consumer; no actual rosbag I/O evidence yet. |
| Plugin XML/ament-index/typesupport resource discovery | UNVERIFIED | Required beyond ELF checks before a plugin/rosbag runtime claim. |
| Full rosbag CLI / rosbag2_transport / arbitrary types / compression | UNVERIFIED / outside first planned acceptance | No completeness claim from the fixed-type path. |
| Generic/loaned/zero-copy recording or replay | UNVERIFIED | Not measured by these library/configuration tests. |
| Native discovery/reconnect, pub/sub, service/action and QoS under recording | UNVERIFIED for R19 | No R19 runtime or DSoftBus communication window. Historical other-stage results are separate. |
| Serialization, large/fragmented messages, concurrency and resource release under rosbag load | UNVERIFIED | Fixed-type future test must establish its own bounds and cleanup. |
| RK3588A rosbag or cross-device record/replay | UNVERIFIED / NOT_RUN | Device-tested loader rejection is a separate R18 claim. |
| Session API fallback | UNSUPPORTED by project policy | Only DSoftBus Socket API may carry transport traffic. |

The dependency design targets tinyxml2_vendor 0.9.2, class_loader 2.7.1 and
pluginlib 5.4.6 before rosbag2 storage/cpp/SQLite 0.26.11. The selected TinyXML2
6 export is bare `tinyxml2`, while pluginlib expects `tinyxml2::tinyxml2`.
The adapter binds the exact target location/hash/includes/SONAME for the
qualified static pluginlib build. The broader API consumer probes, repeated/
nested/sibling scopes and negative controls remain unexecuted. The bounded
list_plugins compile/link does not establish general TinyXML2 6 alias/API
compatibility or TinyXML2 9 compatibility.
pluginlib provides an INTERFACE target and a `list_plugins` executable; absence
of `libpluginlib.so` is not itself a failure.

The original design review's R19-N1 identified missing SDK metadata/platform
inputs. FIX1 freezes 371 inputs, adds unknown-input stop controls and removes
duplicate package-dir arguments. Its independent review closes R19-N1
**FOR_DESIGN only**. The plan/review's then-pending wrapper-final status remains
an historical snapshot; the later canonical wrapper final is separately bound.
The later 15-file TinyXML2 metadata installation and fresh class_loader and
pluginlib static qualifications are scoped above. They do not qualify general
alias consumer/API compatibility, plugin loading or rosbag tests.

Actual native communication still requires the complete current project-only
permission record and a supported lossless restore route. Conditional user
authorization does not remove these prerequisites on either board; no
grant/revoke/token switch/reload/security write is part of R19 preparation.
At 21:51:22 UTC HDC listed both boards USB Connected; this is visibility
only, not new device execution. Current DATASYNC state is NOT_QUERIED.
Automatic approval review rejected the full-token-registry route; it was not retried. R18's two-board main-load and
missing-identity rejection PASS proves only that diagnostic, not communication.
Overall ROS 2 over native DSoftBus: **NOT VERIFIED**.

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
| `next-dependency-execute-parser-fix1-test/RESULT.json` (TinyXML2 metadata only; old class_loader BLOCKED) | `ab38227492a05fce1a89870767e3f2e00423286b0e2a1d32bc1e78497336baa3` |
| `next-dependency-sdk-rebuild-setup-fix1-test/RESULT.json` | `c98d915eeb024cad63f74c0e48a3021f2202852643b9c2b2a3607689d28adf60` |
| `next-dependency-sdk-rebuild-setup-fix1-test/EVIDENCE_MANIFEST.json` (372 files) | `5260da8b2e4a8ee0aeb931addcab67f189b50f7405c7b0487a05ddc9308e9612` |
| `next-dependency-sdk-rebuild-post-review/RESULT.json` | `71311a4697bffd0f2ed1579115db2146fd64f141722aa210175535014884d25e` |
| `next-dependency-sdk-rebuild-post-review/EVIDENCE_MANIFEST.json` (11 files) | `fc44b93839607181bc1c69dccc719ca253ad8eca54c780cbdaed3d037aa9a5f1` |
| `next-dependency-sdk-rebuild-final-test/RESULT.json` | `2caadd9edf86e7994c321cb51ee6e31b53a24e88212c425cd96d277d761a3a39` |
| `next-dependency-sdk-rebuild-final-test/REPORT.md` | `fbe74fb5845622832120a233868c5993f3185eb258a84f8d27c3dc0249bec96a` |
| `next-dependency-sdk-rebuild-final-test/EVIDENCE_MANIFEST.json` (130 files) | `8706146e475c8cd3f13fe6ef41d27c131f6afd06b76ce412ef3495b092097718` |
| `pluginlib-next-test/RESULT.json` (original A_BLOCKED) | `2a9f41111396cf0d105b72f398f6ae16fbf5c519d1b5e93a1f4300772bf9a2b0` |
| `pluginlib-next-test/EVIDENCE_MANIFEST.json` (152 files) | `12a95d3ad2f6488242013137b5c793b62a1f6423a9760c8434b262549402641f` |
| `pluginlib-export-contract-fix1/CONTRACT.json` (reviewed continuation contract) | `c7f9c158bd07be3584fd84eb5a478fb1b6bf6491d17133d90cd4350eab76af05` |
| `pluginlib-export-contract-review/RESULT.json` | `df36581309e26ee59ea9129ff9e170919752d31b5aab8501be89def4a72b4fb1` |
| `pluginlib-export-contract-fix1-test/RESULT.json` | `b50d8afd1cae0006936e0c883f8b1d7f79c939e241ec580518b1c87d83f39d0b` |
| `pluginlib-export-contract-fix1-test/EVIDENCE_MANIFEST.json` (226 files) | `9e253c9e5da886a04de90ea53ae3a9d3fdcab22abd0a89c670f09eb265fa4678` |
| `pluginlib-export-contract-fix1-test/INSTALLED_MANIFEST.json` (29 installed files) | `f0e2206c804a33e839d9049261192667cacfdb4b5f9ffc20ce32449ae5b4af0b` |
| `pluginlib-post-test-review/RESULT.json` | `256516e572e83da68fb433f427876fb9d09d29bcbd8264f196bbb262174148a2` |
| `pluginlib-post-test-review/EVIDENCE_MANIFEST.json` (6 files) | `b160636cbc4e0043f93be26d200ebf0c986a6ea43e5536ccd4f458a322ff46e4` |
| `pluginlib-final-test/RESULT.json` | `f457823264e4223e04d2bc2489cf98678b7f950fad9c15eb58f725beb568b104` |
| `pluginlib-final-test/REPORT.md` | `508db2cd4f4bc5d2a56488ea04f34f5086668d0dd9034209a55ab3c47f229605` |
| `pluginlib-final-test/EVIDENCE_MANIFEST.json` (38 files) | `7e50cb07d8033039fdf463d3ec5439dc74ff748bba9fa3817ffa7d5df6c906e5` |
| `class-loader-main-publication/PUBLICATION_RESULT.json` (preceding stage) | `5696caaa65d095a85cc5c3907827ec62801458bba7732c3fd4df2c009fe305b3` |
| `class-loader-main-publication/CI_RESULT.json` (21:47:50 UTC NOT_RUN) | `4b5d4b65c533eb74121973eb18451b455fd7cb6b46110ba65459aa34a0fd7ef3` |
| `PLUGINLIB_PUBLICATION_IDENTITY_PRECHECK.json` (read-only remote identity) | `fb3c64d496a5677b3d8b1e78d6cd46de9f3cd831c2ac2abd5e7ce55cf3362c8a` |
| `pluginlib-board-readiness/RESULT.json` (visibility only) | `0f7ced5f4d3ca6c5f85ba5be30be6a2bf64e4636655b87d269c6ecfa259a5d79` |

This document records bounded preparation evidence. Its documentation readiness
does not grant an implementation, native communication or overall product gate.
