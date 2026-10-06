# Dependency security and maintenance candidates — 2026-10-06

This is a dependency inventory/applicability assessment against official public
advisories, not a full repository security scan or a device exploit verification.
The original 2.14.5 rows below are retained as dated baseline assessment. A later
source-bound maintenance candidate is recorded in the 2026-10-07 section below;
none of the integration patches is counted as a security fix. Advisory/version
applicability and target exploitability are separate.

## Source-bound ROS 2 maintenance candidate (2026-10-07)

The candidate uses official Fast DDS `v2.14.7` at commit
`010b908dc3bb2b6f2818d61ac8db462d01b79a1f` and Fast CDR `v2.2.8` at commit
`1bc9c919311ffce555b445560bf7ae062a4e1c72` ([Fast DDS release](https://github.com/eProsima/Fast-DDS/releases/tag/v2.14.7),
[Fast CDR release](https://github.com/eProsima/Fast-CDR/releases/tag/v2.2.8)).
The private factory interface schema remains `21405`; it is not a dependency version.
The legacy 2.14.5 default remains available and its historical rows are not rewritten.

Other candidate inputs are bound from the actual CMake caches, imported targets,
headers and version artifacts used by each build. Host TinyXML2, Asio, foonathan_memory,
MSVC Release CRT and system/runtime inputs are kept distinct from the API23 target
toolchain, libc++ and platform inputs; no Host version is inferred from the historical
target TinyXML2 row. Where an exact supplier artifact, loaded scope or advisory mapping
is not bound by those receipts it remains UNKNOWN and requires the release SBOM.
The Host candidate selected inputs record MSVC `19.44.35228.0`, Asio `1.28.1`,
TinyXML2 `10.0.0`, foonathan_memory `0.7.4`, Fast DDS `2.14.7` and Fast CDR `2.2.8`.
The API23 target selected inputs record ament_index_cpp `1.8.2`, Asio `1.34.2`,
TinyXML2 `6.0.0` (the TargetBridge major-6 assertion), foonathan_memory `0.7.4`,
Fast DDS `2.14.7` and Fast CDR `2.2.8`. Supplier binary/source equivalence, loaded
firmware ABI, complete SBOM and advisory coverage remain UNKNOWN; these version facts
do not imply bundling or a new dependency upgrade.
The separate Fast DDS 3.6 baseline regression remains a distinct package and passed
`134/134`; that result is not evidence that the 3.6 dependency is maintenance-fixed
by the 2.14 candidate.

Candidate preparation verified official raw/blob inputs, applicable EOL maps and the
integration patch. Eleven upstream source files were modified by the candidate patch
and six project headers were hash-bound; CMake input and raw/blob/EOL receipts are in
the committed `integrations/ros2/candidates/2.14.7/UPSTREAM.json` and the generated
`FDDST_MAINTENANCE_PREPARED.json` preparation receipt. The final source-hash receipt
is the separate 27-file task-time freeze.
Five retained requirements remain transplanted: factory/configuration, endpoint/XML
guards, DDS deletion/teardown observation, residual/native-prefix lifetime, and the
TopicDescription RTTI key. These are integration changes, not upstream security fixes.

The candidate matrix contains 20 newly source-bound repair rows:

| Advisory | Candidate repair commit | Applicability / evidence state |
| --- | --- | --- |
| CVE-2026-102681 | `e787e2f1781ed015e740fd5417832343355720b3` | Fix included; common RTPS/discovery/QoS reachability remains open |
| CVE-2026-45092 | `54a733d4d4a30c744e3412e9553f657783f7e950` | Security-only condition excluded by current SECURITY=OFF build |
| CVE-2025-67108 | `be882b5357e663434c134b9ae2e6ee6be1b45c7f` | Security-only condition excluded by current SECURITY=OFF build |
| CVE-2025-65865 | `54a733d4d4a30c744e3412e9553f657783f7e950` | Fix included; RTPS/discovery/QoS reachability remains open |
| CVE-2025-63829 | `54a733d4d4a30c744e3412e9553f657783f7e950` | Fix included; RTPS/discovery/QoS reachability remains open |
| CVE-2026-45093 | `54a733d4d4a30c744e3412e9553f657783f7e950` | Fix included; retained mixed TCP reachability remains open |
| CVE-2026-45094 | `f76ca8a6cba1f212db7cb0935eb2de32a3bae39c` | Security-only condition excluded by current SECURITY=OFF build |
| CVE-2026-45095 | `f76ca8a6cba1f212db7cb0935eb2de32a3bae39c` | Security-only condition excluded by current SECURITY=OFF build |
| CVE-2026-45096 | `f76ca8a6cba1f212db7cb0935eb2de32a3bae39c` | Security-only condition excluded by current SECURITY=OFF build |
| CVE-2026-22590 | `1384c646ece0556e710df56d4dceeea6c0f281ff` | Fix included; RTPS/discovery/QoS reachability remains open |
| CVE-2026-22591 | `d96f0ebf3244019f58a305494be47c0618369374` | Fix included; SQL/filter regression selected |
| CVE-2025-65016 | `104a71d467ac4ebd5b1d3037a3b09afb2775d48f` | Fix included; RTPS/discovery/QoS reachability remains open |
| CVE-2025-64438 | `65d5ef3b4fb1b67c4bf4ceb52bd0c85f95102f56` | Fix included; RTPS/discovery/QoS reachability remains open |
| CVE-2025-62799 | `554a55ffedbe141e542b0d96efbf2947304a1ac1` | Fix included; RTPS/discovery/QoS reachability remains open |
| CVE-2025-62603 | `8e94c7af7c2da05af6a3ce7c31f7e74e4ce91239` | Security-only condition excluded by current SECURITY=OFF build |
| CVE-2025-62602 | `8e94c7af7c2da05af6a3ce7c31f7e74e4ce91239` | Security-only condition excluded by current SECURITY=OFF build |
| CVE-2025-64098 | `8e94c7af7c2da05af6a3ce7c31f7e74e4ce91239` | Security-only condition excluded by current SECURITY=OFF build |
| CVE-2025-62601 | `8e94c7af7c2da05af6a3ce7c31f7e74e4ce91239` | Security-only condition excluded by current SECURITY=OFF build |
| CVE-2025-62600 | `8e94c7af7c2da05af6a3ce7c31f7e74e4ce91239` | Security-only condition excluded by current SECURITY=OFF build |
| CVE-2025-62599 | `8e94c7af7c2da05af6a3ce7c31f7e74e4ce91239` | Security-only condition excluded by current SECURITY=OFF build |

The 14 earlier historical fixed-range rows remain dated baseline records and were
not newly executed as candidate repairs. CVE-2026-63340 remains deployment-dependent
with no fixed version; standalone 3.6 remains separately unrelieved by this candidate.
No advisory is dismissed solely because DSoftBus is underneath RTPS.
The matrix marks the repair source as included where applicable; SECURITY_OFF rows
mean the security-specific paths were not compiled or executed in this candidate.
The source-preservation receipt records 100 rows, 82 unique files and 10 repair
ancestors; these are provenance counts, not exploit-test coverage.

R2 then reviewed a metadata-only patch serialization correction. The final patch
digest is `a74a796925b435e40dfae4ac18863e8c8e280788a8b0368b0b3ba23000b5016b`;
raw source, EOL maps and runtime code remained unchanged. Final delta evidence
reports 24/24 compiled inputs, 11/11 prepared outputs and 6/6 copied headers
unchanged, maintenance `3/3`, official blob delivery PASS and staged diff check PASS.
The earlier cached-whitespace failure remains historical evidence; no whitespace
waiver was used.

## Candidate verification boundary

The canonical final tester receipt is `build/ros214-security-20261007/FINAL_TESTER_RESULT.json`:
preflight 31/31, maintenance checks 3/3, Host CTest 14/14, candidate ASan CTest
14/14, targeted parser selection PASS, RTTI/factory/observer checks PASS, Host
QoS/graph/service close 20 writes/20 takes, both Host reaccept branches PASS, and a
relocated consumer `PASS_CLOSED_RUNTIME` 20 writes/20 takes after prefix migration;
the corrected `final-run-moved-r4b-closed` trace used the moved `r4b/runtime` ROS
prefix, excluded original task-build/runtime/install directories from child PATH,
and directly recorded moved Fast DDS/CDR origins. The earlier moved run remains
PARTIAL because it loaded the original CDR runtime. The parser run covers
nine unique selected cases (four Builtin and five SQL); the direct five SQL cases
overlap four earlier CTest executions. The initial Fast CDR supporting receipt covers
13 normal cases; malformed sequence-length testing was not executed. ASan dependencies
were not instrumented and TSan was not run.

The API23 target bridge and ROS static consumer chain passed a fresh r4 configuration
using only target ROS/ament inputs and candidate Fast DDS/CDR prefixes: ELF64/AArch64,
no stale Host ament directory and no absolute `C:` DT_NEEDED. This is static evidence
only; there was no HDC, target execution, SDK/platform DSO body distribution or device
runtime proof. Unchanged upstream RMW and typesupport were rebuilt from official source;
application IDL/generated-typesupport provenance and upstream Gen 3.3.3 outputs remain
bound in the candidate receipts. The standalone consumer used the required project
build isolation; no claim is made that it was outside the repository.
The official unchanged RMW source set (239 raw files) and typesupport source set
(74 raw files) were rebuilt; these counts are source-preservation evidence, not a
new RMW implementation or a substitute for device execution.

The final diagnostic capture being Busy still makes close fail; this is a verified
local diagnostic branch, not an SDK `CloseBusy` method or an SDK drain guarantee.
First-party release licensing,
platform SDK/DSO rights, SBOM closure, product `NEEDS_PRODUCT_VALUE` decisions and
PF-01/PF-02 remain open. This candidate is not production-ready.

## Historical 2.14.5 release inputs (2026-10-06 baseline)

| Delivery | Observed inputs / source of truth | Security implication |
| --- | --- | --- |
| ROS 2 target | fastrtps CONFIG/header 2.14.5; base ebcb5c05; Fast CDR installed config.h/config-version 2.2.5; TinyXML2 header 6.0.0; foonathan_memory CONFIG 0.7.4; SDK API23 6.1.0.32 / Clang15.0.4 / shared libc++ | fastdds-build cache SECURITY=OFF, installed HAVE_SECURITY=0; SDK binary closure/vendor source equivalence unverified |
| ROS 2 Host | Patched fastrtps 2.14.5; Fast CDR 2.2.5 imported RELWITHDEBINFO artifact, MSVC Release DLL CRT | Host selected configuration recorded honestly; not relabeled as an imported RELEASE CDR package |
| Standalone delivery | Fast DDS CONFIG 3.6.2.0; Fast CDR CONFIG 2.3.5; foonathan_memory 0.7.4; shared Release | Separate ABI, not a substitute for ROS 2 2.14 maintenance |
| Other build/runtime inputs | Asio source header 1.34.2 inspected; target installed TinyXML2 6.0.0; exact per-ROS-package package.xml set required from external distribution | Some distinct builds use distinct external Asio/ROS inputs; do not infer every artifact used this source checkout. Bind compiler/source inputs in release SBOM |
| OpenSSL | Host dependency resolution reports 3.3.2; target SECURITY OFF and no selected OpenSSL version in target build cache | Host configure discovery is not proof of target linkage or all OpenSSL patch status; exact linked/loaded scope needs separate SBOM/advisory check |

The [official supported-release table](https://github.com/eProsima/Fast-DDS/blob/master/RELEASE_SUPPORT.md)
currently lists 2.14.7 for 2.14 LTS (standard support ended September 2026;
security/critical support to May 2029 subject to its stated policy). The same table
lists 3.6.2, while a newer advisory names 3.6.3 as patched; verify the candidate tag
and patch presence rather than treating either string as release evidence.

DSoftBus authenticated transport can change attacker access, but bytes still reach
Fast DDS RTPS/CDR processing. A peer, mixed native UDP/TCP transport or discovery
input may exercise parsers. No row is dismissed solely because DSoftBus is underneath.
Current SECURITY=OFF can exclude security-only compiled paths for that build; turning
security on for production reopens those paths and must include the maintenance fixes.

## Official Fast DDS applicability matrix

Official repository security-advisory API returned 36 public advisories in one
page (per_page=100) on this audit. All are listed; broad upper-bound ranges are
interpreted conservatively by version, not as confirmed target exploit traces.
FIXED_VERSION denotes upstream release coverage, not an independently executed exploit test.

| Advisory | 2.14.5 assessment | Official patched releases/range | Reachability basis / uncertainty |
| --- | --- | --- | --- |
| [GHSA-p4mg-89mm-2mjf](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-p4mg-89mm-2mjf) | FIXED_VERSION | >= 2.0.0 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2026-102681](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-8g7x-c9fg-g6mh) | AFFECTED_VERSION | 2.6.13, 2.14.7, 3.2.6, 3.6.3 | RTPS/discovery/QoS/parser input may reach Fast DDS; exploit on target not executed |
| [CVE-2026-45092](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-mvxp-mfh8-hhv5) | AFFECTED_VERSION / CONDITIONAL_UNREACHABILITY | 2.6.12, 2.14.7, 3.2.5, 3.4.3, 3.6.2 | Current target SECURITY=OFF / HAVE_SECURITY=0; not a repair or future production exclusion |
| [CVE-2026-45097](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-93m4-rx65-p7rj) | OUTSIDE_2X_RANGE | 3.2.5, 3.4.3, 3.6.2 | Official range starts at 3.0; standalone 3.6.2 fixed-version candidate |
| [CVE-2026-63340](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-wv5q-wgv8-qh6v) | AFFECTED / DEPLOYMENT_DEPENDENT | None (mitigation proposed) | RTPS fragment allocation remains relevant; no fixed version advertised |
| [CVE-2025-67108](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-7j73-5rxp-jxg2) | AFFECTED_VERSION / CONDITIONAL_UNREACHABILITY | 2.6.12, 2.14.7, 3.2.5, 3.6.3 | Current target SECURITY=OFF / HAVE_SECURITY=0; not a repair or future production exclusion |
| [CVE-2025-65865](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-cxgg-pw23-rxhm) | AFFECTED_VERSION | 2.6.12, 2.14.7, 3.2.5, 3.4.3, 3.6.2 | RTPS/discovery/QoS/parser input may reach Fast DDS; exploit on target not executed |
| [CVE-2025-63829](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-64vf-hp4r-h8mx) | AFFECTED_VERSION | 2.6.12, 2.14.7, 3.2.5, 3.4.3, 3.6.2 | RTPS/discovery/QoS/parser input may reach Fast DDS; exploit on target not executed |
| [CVE-2026-45093](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-3733-fqj8-39fx) | AFFECTED_VERSION / UNKNOWN_REACHABILITY | 2.6.12, 2.14.7, 3.2.5, 3.4.3, 3.6.2 | RTCP TCP path; BYTES alone does not discharge retained mixed-TCP requirement |
| [CVE-2026-45094](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-rg6h-2jch-hwjx) | AFFECTED_VERSION / CONDITIONAL_UNREACHABILITY | 2.6.12, 2.14.7, 3.2.5, 3.4.3, 3.6.2 | Current target SECURITY=OFF / HAVE_SECURITY=0; not a repair or future production exclusion |
| [CVE-2026-45095](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-r4gh-qh7w-jxqf) | AFFECTED_VERSION / CONDITIONAL_UNREACHABILITY | 2.6.12, 2.14.7, 3.2.5, 3.4.3, 3.6.2 | Current target SECURITY=OFF / HAVE_SECURITY=0; not a repair or future production exclusion |
| [CVE-2026-45096](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-j2w9-582m-j57r) | AFFECTED_VERSION / CONDITIONAL_UNREACHABILITY | 2.6.12, 2.14.7, 3.2.5, 3.4.3, 3.6.2 | Current target SECURITY=OFF / HAVE_SECURITY=0; not a repair or future production exclusion |
| [CVE-2026-22590](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-7r7h-hwfj-q626) | AFFECTED_VERSION | 2.6.12, 2.14.6, 3.2.4, 3.3.1, 3.4.2 | RTPS/discovery/QoS/parser input may reach Fast DDS; exploit on target not executed |
| [CVE-2026-22591](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-7577-rf2r-j88m) | AFFECTED_VERSION | 2.6.12, 2.14.7, 3.2.4, 3.4.3 | RTPS/discovery/QoS/parser input may reach Fast DDS; exploit on target not executed |
| [CVE-2025-65016](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-7r5r-66jr-gwrw) | AFFECTED_VERSION | 2.6.11, 2.14.6, 3.2.4, 3.3.1, 3.4.2 | RTPS/discovery/QoS/parser input may reach Fast DDS; exploit on target not executed |
| [CVE-2025-64438](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-j562-pw24-5ggx) | AFFECTED_VERSION | 2.6.11, 2.14.6, 3.2.4, 3.3.1, 3.4.1 | RTPS/discovery/QoS/parser input may reach Fast DDS; exploit on target not executed |
| [CVE-2025-62799](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-cg8v-g888-pjh6) | AFFECTED_VERSION | 2.6.11, 2.14.6, 3.2.4, 3.3.1, 3.4.1 | RTPS/discovery/QoS/parser input may reach Fast DDS; exploit on target not executed |
| [CVE-2025-62603](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-2p8v-f7wf-pf6v) | AFFECTED_VERSION / CONDITIONAL_UNREACHABILITY | 2.6.11, 2.14.6, 3.2.4, 3.3.1, 3.4.1 | Current target SECURITY=OFF / HAVE_SECURITY=0; not a repair or future production exclusion |
| [CVE-2025-62602](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-j2ff-qhr5-3x3x) | AFFECTED_VERSION / CONDITIONAL_UNREACHABILITY | 2.6.11, 2.14.6, 3.2.4, 3.3.1, 3.4.1 | Current target SECURITY=OFF / HAVE_SECURITY=0; not a repair or future production exclusion |
| [CVE-2025-64098](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-q7x8-678p-c6jp) | AFFECTED_VERSION / CONDITIONAL_UNREACHABILITY | 2.6.11, 2.14.6, 3.2.4, 3.3.1, 3.4.1 | Current target SECURITY=OFF / HAVE_SECURITY=0; not a repair or future production exclusion |
| [CVE-2025-62601](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-vjq3-347v-67xg) | AFFECTED_VERSION / CONDITIONAL_UNREACHABILITY | 2.6.11, 2.14.6, 3.2.4, 3.3.1, 3.4.1 | Current target SECURITY=OFF / HAVE_SECURITY=0; not a repair or future production exclusion |
| [CVE-2025-62600](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-hvm8-mm7f-m6hc) | AFFECTED_VERSION / CONDITIONAL_UNREACHABILITY | 2.6.11, 2.14.6, 3.2.4, 3.3.1, 3.4.1 | Current target SECURITY=OFF / HAVE_SECURITY=0; not a repair or future production exclusion |
| [CVE-2025-62599](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-fc3f-wcj5-5cph) | AFFECTED_VERSION / CONDITIONAL_UNREACHABILITY | 2.6.11, 2.14.6, 3.2.4, 3.3.1, 3.4.1 | Current target SECURITY=OFF / HAVE_SECURITY=0; not a repair or future production exclusion |
| [CVE-2025-24807](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-w33g-jmm2-8983) | FIXED_VERSION | 2.6.10, 2.10.7, 2.14.5, 3.0.2, 3.1.2, 3.2.0 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2024-30259](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-qcj9-939p-p662) | FIXED_VERSION | 2.14.1 / 2.13.5 / 2.10.4 / 2.6.8 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2024-30258](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-53xw-465j-rxfh) | FIXED_VERSION | 2.14.1 / 2.13.5 / 2.10.4 / 2.6.8 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2024-28231](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-9m2j-qw67-ph4w) | FIXED_VERSION | v2.14.0 / v2.13.4 / v2.12.2 / v2.10.4 / v2.6.8 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2023-50716](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-5m2f-hvj2-cx2h) | FIXED_VERSION | v2.13.0 / v2.12.2 / v2.11.3 / v2.10.3 / v2.6.7 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2023-50257](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-v5r6-8mvh-cp98) | FIXED_VERSION | (FastDDS) 2.10.3 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2023-39948](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-x9pj-vrgf-f68f) | FIXED_VERSION | >= v2.10.0 / v2.6.5 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2023-39949](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-3jv9-j9x3-95cg) | FIXED_VERSION | 2.9.1 / 2.6.5 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2023-42459](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-gq8g-fj58-22gm) | FIXED_VERSION | 2.12.0 / 2.11.3 / 2.10.3 / 2.6.7 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2023-39947](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-mf55-5747-c4pv) | FIXED_VERSION | >= 2.11.1 / 2.10.2 / 2.9.2 / 2.6.6 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2023-39946](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-j297-rg6j-m7hx) | FIXED_VERSION | >= 2.11.1 / 2.10.2 / 2.9.2 / 2.6.6 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2023-39945](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-2rq6-8j7x-frr9) | FIXED_VERSION | 2.11.0 / 2.10.2 / 2.9.2 / 2.6.5 | Upstream range excludes 2.14.5; no new runtime proof |
| [CVE-2023-39534](https://github.com/eProsima/Fast-DDS/security/advisories/GHSA-fcr6-x23w-94wp) | FIXED_VERSION | >= 2.10.0 / 2.9.2 / 2.6.5 | Upstream range excludes 2.14.5; no new runtime proof |

For standalone 3.6.2.0, official patched 3.6.2 rows are FIXED_VERSION candidates;
CVE-2026-102681 and CVE-2025-67108 name 3.6.3, while CVE-2026-63340 has no fixed
version. Version comparison alone is insufficient where the advisory/code range
is ambiguous or an integration build includes patches not reflected in the tag.

## Other dependencies and minimum candidates

| Dependency | Actual applicability / uncertainty | Minimum concrete candidate and verification |
| --- | --- | --- |
| Fast CDR 2.2.5 (ROS) | Official public advisory API returned no repository advisories; absence is UNKNOWN security coverage, not a clean bill. Later upstream fixes include UB in array methods and sequence-length checks | [2.2.8](https://github.com/eProsima/Fast-CDR/releases/tag/v2.2.8) within existing minor line; inspect PR314/329 and bounded malformed sequence/array/optional regressions. 2.2.7 does not include all these fixes |
| Fast CDR 2.3.5 (standalone) | [2.3.6](https://github.com/eProsima/Fast-CDR/releases/tag/v2.3.6) repairs array UB/optional state; sequence checks listed in 2.4.0 rather than declaring 2.3.6 sufficient | Minimal patch-line 2.3.6 plus audited sequence-length backport if required; alternatively evaluate [2.4.0](https://github.com/eProsima/Fast-CDR/releases/tag/v2.4.0), with its new Cdr context/strict bool API and ABI/wire tests |
| foonathan_memory 0.7.4 | No full official advisory/SBOM closure established here; allocator runtime and build options matter | Retain 0.7.4 until official patch applicability/ABI evidence exists; allocation-failure/resource regressions required; UNKNOWN, not unaffected |
| TinyXML2 6.0.0 | Old pinned major enforced by TargetBridge static_assert; full upstream issue/advisory coverage UNKNOWN | Evaluate same-major official fixes/backports first; changing to a current major requires explicit header/parser/symbol ABI and malformed XML regression. No fictitious fixed version asserted |
| Asio / ROS / OpenSSL / libc++ / vendor DSOs | Exact artifact/source/loaded scope and complete supplier security advisories are not uniformly bound; UNKNOWN items remain release blockers for their use | Per-release SBOM from actual imported/module/source inputs, supplier advisories and patch hashes; no blind dependency download or bundling |

## Historical version-bound migration proposal (2026-10-06; superseded by candidate)

1. ROS 2: [2.14.6](https://github.com/eProsima/Fast-DDS/releases/tag/v2.14.6)
   is the first 2.14 patch for the ten fixes listed in its release. It is insufficient
   for later partition/filter/security/fragment advisories. The minimum current
   2.14 maintenance candidate is [2.14.7](https://github.com/eProsima/Fast-DDS/releases/tag/v2.14.7)
   with each applicable advisory's patch presence confirmed. Alternative: backport
   the relevant 2.14.6/2.14.7 upstream security commits onto 2.14.5 with exact source
   and patch receipts; do not rename the result as unmodified 2.14.7.
2. Rebase only existing factory/endpoint/XML, teardown, residual and topic-RTTI
   integration patches. Existing scripts assert 2.14.5/file hashes; none can simply
   be run against another version. Regenerate receipts and version-bound hook ABI
   probe, then rebuild Fast DDS, bridge, ROS typesupport/consumer and install exports
   coherently. Original RMW implementation stays unchanged. Same patch line aims at
   upstream ABI compatibility but the private factory hook ABI is not guaranteed.
3. Rebuild with the same AArch64 Clang/shared libc++ (and separately MSVC /MD),
   inspect SONAME/exports/imports/header macros and generated manifest; compare
   integration hook layout/symbols. Never mix Fast DDS 2.14/3.6 or CDR minor families
   in one process. Run relocated CONFIG consumer/preflight, first-entry negative,
   entity deletion/Runtime close, bridge ownership and malformed-input Host tests.
4. Regression requires RTPS discovery/partition, RELIABLE GAP/HEARTBEAT/fragment,
   content filter, CDR malformed lengths/arrays/optionals, resource failures, mixed
   transport and security ON/OFF as applicable; then targeted RK3588A cross-device
   startup/continuity/close. Old device acceptance does not qualify new binaries.
   Fault injection and new platform release need separate authorization and budgets.
5. CVE-2026-63340 is deployment-dependent and lists no fixed version. Official
   mitigation is DDS Security discovery SIGN/ENCRYPT for the unauthenticated case.
   Current target SECURITY=OFF cannot claim that mitigation. Evaluate security
   configuration and fixed-payload/resource bounds with the product owner;
   authorized malicious peers and retained mixed transports still need analysis.
   Do not change RTPS/QoS semantics or claim DSoftBus authentication suffices.

The proposal above records the pre-candidate plan. The 2026-10-07 candidate now has
source-bound 2.14.7/2.2.8 receipts, but supplier/SBOM/maintenance approval and
dependency security qualification remain unverified.

## OpenSSL conditional inventory

Official GitHub advisory endpoints for Fast CDR, foonathan/memory, TinyXML2,
Asio and OpenSSL returned zero public rows. Fast CDR/TinyXML2/foonathan security
pages were checked; zero rows are not proof of absence. OpenSSL publishes its
[own vulnerability index](https://www.openssl-library.org/news/vulnerabilities-3.3/),
which supplies the following 3.3 version comparisons. Linked/loaded feature reachability
remains UNKNOWN here; target SECURITY=OFF is not a global OpenSSL exclusion.

| CVE | 3.3.2 version state | First 3.3 repair |
| --- | --- | --- |
| CVE-2026-28387 | AFFECTED_VERSION | 3.3.7 |
| CVE-2026-28388 | AFFECTED_VERSION | 3.3.7 |
| CVE-2026-28389 | AFFECTED_VERSION | 3.3.7 |
| CVE-2026-28390 | AFFECTED_VERSION | 3.3.7 |
| CVE-2026-31789 | AFFECTED_VERSION | 3.3.7 |
| CVE-2026-31790 | AFFECTED_VERSION | 3.3.7 |
| CVE-2025-15467 | AFFECTED_VERSION | 3.3.6 |
| CVE-2025-15468 | AFFECTED_VERSION | 3.3.6 |
| CVE-2025-66199 | AFFECTED_VERSION | 3.3.6 |
| CVE-2025-68160 | AFFECTED_VERSION | 3.3.6 |
| CVE-2025-69418 | AFFECTED_VERSION | 3.3.6 |
| CVE-2025-69419 | AFFECTED_VERSION | 3.3.6 |
| CVE-2025-69420 | AFFECTED_VERSION | 3.3.6 |
| CVE-2025-69421 | AFFECTED_VERSION | 3.3.6 |
| CVE-2026-22795 | AFFECTED_VERSION | 3.3.6 |
| CVE-2026-22796 | AFFECTED_VERSION | 3.3.6 |
| CVE-2025-9230 | AFFECTED_VERSION | 3.3.5 |
| CVE-2025-9231 | AFFECTED_VERSION | 3.3.5 |
| CVE-2025-9232 | AFFECTED_VERSION | 3.3.5 |
| CVE-2024-12797 | AFFECTED_VERSION | 3.3.3 |
| CVE-2024-13176 | AFFECTED_VERSION | 3.3.3 |
| CVE-2024-9143 | AFFECTED_VERSION | 3.3.3 |
| CVE-2024-6119 | FIXED_VERSION | 3.3.2 |
| CVE-2024-5535 | FIXED_VERSION | 3.3.2 |
| CVE-2024-4741 | FIXED_VERSION | 3.3.1 |
| CVE-2024-4603 | FIXED_VERSION | 3.3.1 |

The minimum listed 3.3 repair set is 3.3.7. However, the [official lifecycle](https://www.openssl-corporation.org/lifecycle.html)
places 3.3 at EOL since 2026-04-09. For a linked production dependency evaluate
supported 3.5 LTS at a current security patch level; validate exports, providers,
certificate/CRL/CMS/TLS behavior and Fast DDS security configuration. No upgrade
or ABI compatibility assumption is made in this package.
