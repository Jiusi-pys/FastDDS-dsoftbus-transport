# R19 ROS 2 target rosbag preparation

Evidence snapshot: 2026-10-15, after pluginlib canonical final at 23:19:16 UTC.
This records build/configuration prerequisites. **Actual rosbag recording,
reading or replay over native DSoftBus is not yet verified.** The workspace
source baseline is published R18 `48c01f2597bf16db1ed35fbd79cc5c32826f0ab8`.
Preparation sources and receipts below live in the original checkout's ignored
`build/r19-rosbag-preparation/`; current class_loader/pluginlib build and install
outputs live in its sibling `build/r19next1/`. These are local evidence, not
artifacts included in the remote Git repository by this documentation commit.

## Current fresh pluginlib configuration qualification (2026-10-16 03:23 UTC)

The FIX1 fresh root-scope LANGUAGES NONE configuration has canonical final
**PASS_FINAL_READONLY_CONFIGURATION_EVIDENCE**. Its sole actual configure
ran at 02:58:26 UTC, exit 0 without retry. TinyXML2 is absent before pluginlib;
the observed route is ament, pluginlib, tinyxml2_vendor/FindTinyXML2, CONFIG
adapter, original TinyXML targets/alias and pluginlib export. This supplies
the order-specific prerequisite that Stage B's preloaded consumers could not.
It does not qualify arbitrary directory scopes or runtime plugin loading.

The earlier no-SDK configure exited 0 but correctly stopped on the missing-dl
diagnostic; its STOP and the earlier pretest coverage gap remain immutable.
FIX1 changes only isolated harness search context and strict assertions:
one exact SDK23 `native/sysroot/usr/lib/aarch64-linux-ohos` CMAKE_LIBRARY_PATH,
without SDK toolchain/compiler execution or preseeding lookup results. Actual
trace proves the unchanged rcutils helper resets `_lib`, calls find_library,
takes the success branch and appends the selected path with raw empty `_cfg`.
The complete legacy list is original bootstrap `Lib/librcutils.so` followed
once by that SDK `libdl.a`; modern `rcutils::rcutils` still exports literal `dl`.
The exact realpath and all eight archive bytes are bound. The empty archive
has no runtime or architecture-bearing code; matching its hash alone would
not distinguish another ABI's archive. No dlopen/libc behavior is verified.

Independent three-pass post-test review requested no source fix. Final testing
ran 03:20:35–03:21:40 UTC, exit 0, replaying 3,133 trace commands, seven explicit
process calls, 23 target properties and 13 generated files. Fresh before/after
guards covered 9,174 entries and all four checkouts. It ran no new configure,
compiler, linker, install, readelf or device command and had no final collector
failure. Earlier initial/review collector failures and exact corrections remain
sealed. Only the previously authorized exact unknown-OHOS diagnostic appeared;
the missing-dl warning was resolved, not exempted. The named harness P2 is
closed within this configuration scope. Full Python-import/native-DLL closure,
OS-wide I/O monitoring and independent internal child streams remain unproven.

Storage, SQLite/cpp, local Writer/Reader and native communication remain
unverified; R19-PT1 remains OPEN overall. The preceding documentation stage
was published as `53a13dfc5af752c03538a990bb0ae253238cfdd7` on the existing
`origin/codex/flow-encoding-published-20261014`; CI at 02:23:05 UTC was NOT_RUN.
This update still needs Main graph/PRE_COMMIT and commit/push/CI checks.
Following dated sections are historical snapshots; the current matrix applies.

## Earlier consumer and ament configuration qualification (2026-10-16 02:06 UTC snapshot)

Stage B is **PASS_POST_REVIEW_FINAL_STATIC_CONSUMER_EVIDENCE**: four
positive cases (positive, nested, siblings, installed_pluginlib), nine exact
negative cases, five ordinary objects/links and 15 metadata-only installed
files. Canonical final testing replayed all 57 original command receipts,
26 B ABI queries and 33 archives, then executed seven new bound readelf
inspections once each, all exit 0. It performed no new configure, compile,
link, install or target/device execution. Five ordinary ELF outputs retain
AArch64 identity, bound NEEDED inputs and no RPATH/RUNPATH. Source, input,
output/hash/mtime/membership and four-checkout guards passed.

The installed consumer finds TinyXML2 before pluginlib. Its export interface
ends in the actual imported `tinyxml2`, while the build alias remains
`tinyxml2::tinyxml2`; exact target identity/location/hash/include/SONAME checks
remain. Repeated/nested/sibling reports retain counts 1/2/3, installed case 1.
`plugin_api` was linked but not executed. XML references the deliberately
nonexistent `not_a_runtime_plugin`; the 15-file install proves metadata bytes
and origins, not plugin discovery/load/unload or usable target activation.

The two original B STOP packages remain unchanged. Initial CMake diagnostics
needed an exact 52-name unused-variable classification for three cases;
installed consumption retained its exact 27-name block. Later review found
that B and A had omitted their C ABI warning classification. Main's
`PLUGINLIB_C_ABI_POSTHOC_DIAGNOSTIC_REPLAY_DECISION.json` accepts only the
15 named existing C ABI jobs (13 B, one A, one class_loader), with exact raw
outputs. A compiler emission and a later CMake quotation are distinguished.
Combined compile/link output and exit 0 do not supply separate compiler
stderr/exit, which remain NOT_CAPTURED. Class_loader's different original
contract receives an assurance supplement, not a retroactive B-style violation.
No future-stage or other-warning waiver is granted.

The complete new initial replay, independent three-pass review (no findings)
and final replay provide current static evidence. They do not rewrite old
STOP/withdrawn PASS labels, A publication history or the failed overlapping
collector's uncertain loaded-source bytes. Missing final negative-case receipts
were derived in new roots from the original terminal audit, never backdated.
Trace-boundary/archive-kind/CRLF-oracle and collector corrections, failures and
exact source variants remain in the sealed reports. Raw hashes stay canonical.

The original ament helper splits Windows drive prefixes when target WIN32 is
false. The reproduction still found all six resources on its C: working drive;
it did not prove an old enumeration failure. The private copy changes only
`if(NOT WIN32)` to `if(NOT CMAKE_HOST_WIN32)`. Four post-review isolated script
runs cover intact prefixes/six resources, ordering/duplicates/skip options,
a Windows-hosted non-Windows branch model, and the exact invalid-argument
rejection. This is not real Linux or arbitrary-drive acceptance.

The private Config shim delegates to the original ament_cmake_core 2.5.5,
retains original extension paths and then includes that tested helper. The
original installed host files are unchanged. Three actual dummy configurations
ran once each with LANGUAGES NONE, Windows host and OHOS/aarch64 target:
root-first and child-first passed; requesting 2.5.6 EXACT correctly rejected
2.5.5, so the rejection assertions also exited 0. Repeated/child/sibling scopes,
normal/cache values, guards, six ordered resources and three original extension
registrations passed. Real host Python introspection/template generation ran
in the two positives; the negative entered no core/shim body or Python path.
No compiler, hooks, build, install or target execution occurred.

Independent post-test review required no source/oracle change. Canonical final
revalidated all three raw configurations and 4,786 trace rows without running
CMake again, including generated bytes/membership and the retained collector
repairs. The bounded 251-input binding is not a full Python import/native-DLL
or OS-I/O closure. No OS-wide process/file monitor ran; native Ninja queries
and individual Python streams/exits were not independently process-captured.
The exact unknown-OHOS diagnostic was classified; no upload was performed.

These gates qualify static consumer evidence and the three dummy real-ament
configurations only. R19-PT1 remains **OPEN overall**; runtime plugin loading,
rosbag and native DSoftBus/device integration remain **NOT VERIFIED**. The
preceding stage is published at `770d3e8b75b6bd7168acbc7a23f175ed7e51b7b1`
on `origin/codex/flow-encoding-published-20261014`, with the 2026-10-15
23:33:09 UTC CI snapshot NOT_RUN. This update still needs Main's graph,
PRE_COMMIT, commit/push and CI checks. The following A/class_loader sections
retain their dated historical claims; the current diagnostic supplement above
and capability matrix below govern the updated scope.

## Earlier pluginlib static qualification (2026-10-15 23:19 UTC snapshot)

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
| TinyXML2-preloaded static consumers | SUPPORTED / BUILDS, final static PASS | Four positive/nine negative cases; five objects/links and 15 metadata files; no target execution. |
| Host-aware ament prefix helper and private Config shim | SUPPORTED / HOST_CONFIGURATION_TESTED within exact cases | Four isolated scripts and three real-ament dummy configurations; host installation unchanged; no ROS build or full process-input closure. |
| Fresh pluginlib-first dependency discovery | SUPPORTED / HOST_CONFIGURATION_TESTED in one fixed root scope | FIX1 actual ament→pluginlib→vendor/module→CONFIG adapter order passed without TinyXML preloading; exact SDK dl data selection required; no runtime claim. |
| class_loader/pluginlib runtime | UNVERIFIED / NOT_RUN | No real plugin DSO discovery/load/unload or ClassLoader lifecycle execution. |
| rosbag2_storage build/static installation | UNVERIFIED / NOT_RUN | Fresh-order prerequisite qualified; seven original translation units still need complete build bindings and compiler/link/install evidence. |
| Fixed String typed Writer/Reader with SQLite, no compression | UNVERIFIED / PLANNED | Next bounded rosbag consumer; no actual rosbag I/O evidence yet. |
| Plugin XML/ament-index/typesupport resource discovery | PARTIAL | Metadata bytes and six host CMake resources checked; target runtime XML/plugin/typesupport discovery unverified. |
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
qualified static pluginlib build. The named preloaded consumer probes, repeated/
nested/sibling scopes and nine negative controls now have final static PASS.
The separate FIX1 case now establishes the specific fresh root-scope order.
Neither establishes general TinyXML2 6 API or TinyXML2 9 compatibility.
pluginlib provides an INTERFACE target and a `list_plugins` executable; absence
of `libpluginlib.so` is not itself a failure.

The original design review's R19-N1 identified missing SDK metadata/platform
inputs. FIX1 freezes 371 inputs, adds unknown-input stop controls and removes
duplicate package-dir arguments. Its independent review closes R19-N1
**FOR_DESIGN only**. The plan/review's then-pending wrapper-final status remains
an historical snapshot; the later canonical wrapper final is separately bound.
The later 15-file TinyXML2 metadata installation and fresh class_loader and
pluginlib static qualifications are scoped above. They do not qualify general
API compatibility, plugin loading or rosbag tests.

The read-only storage-readiness report was sealed while B final and wiring
were pending; those prerequisites and the subsequent fresh-order FIX1 now
have canonical final PASS above, without rewriting that report. Actual
rosbag2_storage 0.26.11 calls ament_cmake then pluginlib without preloading
TinyXML2. Next is a newly reviewed storage plan with all seven original
translation units, complete config/header/SDK/link inputs, and an install
oracle derived from the actual
DSO/generated scripts. The old 19-command sketch is not executable authority.
The 74 selected imported DSOs have AArch64 headers, not proven ABI/runtime
compatibility; 68 need new exact bindings. No storage configure/build ran.

Current storage defaults to `mcap`; the future fixed String local round trip
must explicitly choose `sqlite3`, with matching CDR serialization, close/reopen
and exact payload/topic/timestamp/count checks. It still needs storage, cpp
and the SQLite plugin. Source inspection supports a local single-board
component path without Node/Participant/native-identity selection; this is
not proof about all transitive DSO initializers. Missing permission restoration
material must not block all local component preparation. Actual runtime loads
must be checked, and unexpected Transport/identity activity must stop the run.
Cross-board recording/playback separately requires real ROS/DSoftBus tests.

Actual native communication still requires the complete current project-only
permission record and a supported lossless restore route. Conditional user
authorization does not remove these prerequisites on either board; no
grant/revoke/token switch/reload/security write is part of R19 preparation.
At 2026-10-16 02:47:09 UTC HDC listed both boards USB Connected; this is visibility
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
| `fresh-pluginlib-order-test/RESULT.json` | `db3f1be3a3bfd3be3b182da7dc434dd9cf2332e18cc80f21db51be972fe44a90` |
| `fresh-pluginlib-dl-investigation/RESULT.json` | `0a854ccf2f6b3142fad6d4a5b58052d077e6d868386c5890e11db002b9c5e3bb` |
| `fresh-pluginlib-dl-review/RESULT.json` | `e5e69dc9d664aa31bebbd21c6562e8536ffc7d74a6922dd540b29d2ef10823a6` |
| `fresh-pluginlib-order-dl-fix1-test/RESULT.json` | `c3d4d6565437209a5346ab0aebfa3cec3111c7f4231bd74fa16f53d884ab7aa5` |
| `fresh-pluginlib-order-dl-fix1-test/EVIDENCE_MANIFEST.json` (57 files) | `24439ca609930a092e2bcb6a47734b9b5fddd2346e659bf8cbff2c162ef7d4ce` |
| `fresh-pluginlib-order-dl-fix1-post-review/RESULT.json` | `39405bee9cc7433bbd47c4e01b66d669bdf6cec94ff37d18f71832b650f72d78` |
| `fresh-pluginlib-order-dl-fix1-post-review/EVIDENCE_MANIFEST.json` (13 files) | `cd9df01c1e88a752cf6f3a80185350b331e4e90e91de8b28f0ce6cbe6824577e` |
| `fresh-pluginlib-order-dl-fix1-final-test/RESULT.json` | `c650e1c71f290177d793d4a153f66800215cf821148ebeb4faa695cb89280106` |
| `fresh-pluginlib-order-dl-fix1-final-test/EVIDENCE_MANIFEST.json` (22 files) | `059e3980b21a6d630fc60a75fa4782a91f69645bc4a8a449804dc11e25f6fca7` |
| `consumer-wiring-main-publication/PUBLICATION_RESULT.json` | `8c380b18d2f98a40a7a9eed7b97415e635ce8b04bf9d594d7d2d80119479f868` |
| `consumer-wiring-main-publication/CI_RESULT.json` | `ec86dacd8c76ba4d8618393056b71f1e8e15591186270144b440e832aac541af` |
| `fresh-order-board-visibility/RESULT.json` | `e8df9fbfdd9f42bdc637d6e481887825a5410e3dd4ee244e341c436ef10192e1` |
| `pluginlib-consumers-diagnostic-complete-replay/RESULT.json` | `fbbe3ed5fb2d97a123715ca6d1d4f87ae6bb7ff7e4db4420865b44ab1c36feaa` |
| `pluginlib-consumers-diagnostic-complete-replay/EVIDENCE_MANIFEST.json` (93 files) | `5f2339e9382ece0e91e2e6fe7e7b2b1007bb8d2a8ba32e897de54d7040e4378a` |
| `pluginlib-consumers-post-test-review/RESULT.json` | `707913198befe682d7bddc7693e80a013abd82bc83dfef569651079f45a9f9f5` |
| `pluginlib-consumers-post-test-review/EVIDENCE_MANIFEST.json` (6 files) | `016f96180b07e0839cdfae09f275dcf9e0448769fb19af5eb1b6e843b6b1103b` |
| `pluginlib-consumers-final-test/RESULT.json` | `b1d7b2f72b576442a49e2ae24c385db62273b9edeca1faf17be721abf86caa5f` |
| `pluginlib-consumers-final-test/EVIDENCE_MANIFEST.json` (134 files) | `ef160fe1708c3ebc141732b9c1901ef45c6909cb5ded78d308e1e62347226169` |
| `ament-prefix-host-aware-final-test/RESULT.json` | `0fcdb6e2a1bf0754e074636acac5ba984725921e1752cbe84f190f319e6085f7` |
| `ament-prefix-host-aware-final-test/EVIDENCE_MANIFEST.json` (50 files) | `40f321725c27d50a2a06ec90b1960e9cba9eece31fc3a0075126867d3999d200` |
| `ament-prefix-wiring-test/RESULT.json` | `c197803515b1d22047f3dd3af7751c58e6838a42313ae34fde67367a6542ef38` |
| `ament-prefix-wiring-test/EVIDENCE_MANIFEST.json` (91 files) | `1d8cffc96d3f3c8974da490fd31005c4649c459a8b5072d1e3ab93ea089d1609` |
| `ament-prefix-wiring-post-review/RESULT.json` | `1fecce7603498f6c453fcb6d5c8128991d6d80f3b15cd81f321e508b7b5838db` |
| `ament-prefix-wiring-post-review/EVIDENCE_MANIFEST.json` (7 files) | `bc70619a004d735e68f16d636f5d4d31b5d6cf5bdb0b133ea357da9d8f7fc09b` |
| `ament-prefix-wiring-final-test/RESULT.json` | `19a8d595a6d380a3c6a7b8acf479a884f491b136627facce48a9731350cf324b` |
| `ament-prefix-wiring-final-test/EVIDENCE_MANIFEST.json` (35 files) | `620239f4a498c6a4db6d2b033bf89d687445bc81bd6ec7dc889bf43cb99ba27c` |
| `rosbag-storage-readiness/RESULT.json` | `874f1339a3a8a784543c19f2cf26911381922521c3be6ba416a818a808d5a912` |
| `rosbag-storage-readiness/EVIDENCE_MANIFEST.json` (7 files) | `adb3867ced981f71c1dbbc53b22b499798e82c5d74961c3908df44848e9252ef` |
| `PLUGINLIB_C_ABI_POSTHOC_DIAGNOSTIC_REPLAY_DECISION.json` | `d049c18db7acde37509fe47215b74cf44a5b1c6e46f1733bf7016a72b62d2881` |
| `pluginlib-main-publication/PUBLICATION_RESULT.json` | `4736445e9e8ca13c3a7cf26c704f93cc7c9f08885ba9dcbf35046b5b27663b6b` |
| `pluginlib-main-publication/CI_RESULT.json` | `482aca8d60606e0826a2fdd841e9ad74318e86d8112ec07fa0a68035cbe9be20` |
| `consumer-board-readiness-native-context/RESULT.json` | `002c89b1d937f2b2d6083752c62323d6cb1a6cdd896acf33675daeae4bf442d3` |
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
