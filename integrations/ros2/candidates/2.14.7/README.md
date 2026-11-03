# Explicit ROS214 maintenance candidate

Fast DDS release `v2.14.7` is pinned to
`010b908dc3bb2b6f2818d61ac8db462d01b79a1f`; its official Fast CDR submodule
points to `v2.2.8`, `1bc9c919311ffce555b445560bf7ae062a4e1c72`.
The existing custom factory interface schema remains `21405`. It is not the
dependency release number. Every candidate consumer checks the actual selected
Fast DDS/Fast CDR headers and consumes the exact installed release.

Use the existing single entry on a fresh unlinked source directory inside the
repository build isolation:

```text
python integrations/ros2/prepare_fastdds_source.py --source <absolute-fresh-source> --version 2.14.7
```

The default entry remains 2.14.5. The candidate checks original official Git blob
and checkout byte hashes separately, maps only the enumerated per-file EOL forms,
applies the pinned integration patch and verifies all eleven modified source files
and six copied project headers. Preparation refuses drift or repeated application.
`FDDST_MAINTENANCE_PREPARED.json` records actual raw inputs, EOL mappings and outputs.

All five previous requirements remain: factory/configuration, required endpoint/XML
guards, original DDS deletion/teardown observation, residual/native-prefix lifetime,
and the TopicDescription RTTI key function. None is claimed upstream-absorbed.
The port preserves the candidate's statistics construction, participant handle,
SQL limits and copy_attributes changes. Old 2.14.5 transforms/manifests are retained.

Build Fast CDR, Fast DDS and the selected integration into new independent prefixes.
Host and target integration selection is explicit:

```text
-DFASTDDS_DSOFTBUS_ROS214_VERSION=2.14.7
-Dfastrtps_DIR=<candidate-dds-prefix>/share/fastrtps/cmake
-Dfastcdr_DIR=<candidate-cdr-prefix>/lib/cmake/fastcdr
```

Supply all other dependency directories explicitly. On API23 use the existing
`integrations/ros2/target/RosTargetBuildOptions.cmake` as `CMAKE_PROJECT_INCLUDE`
for dependency builds so OHOS shared libraries have SONAMEs. This is a project
build adapter, not an SDK/sysroot modification. Inspect DT_NEEDED for absolute
build-machine paths before accepting the static package.

Rebuild unchanged RMW and typesupport against these prefixes. Message generation
must select both Fast RTPS and introspection C/C++ support; a single Fast RTPS
delegate cannot satisfy the unchanged RMW TypeObject registration path. Preserve
IDL and generator/source identities and actual generation commands. On Windows
use short child names within repository build isolation to keep generated object
paths within the compiler's path limit; do not create drive-root build trees.

The default Fast DDS 3.6 path and legacy 2.14.5 selection do not switch. Candidate
metadata extends the existing installed preflight; it still refuses production
qualification. No SDK/DSO/source dependency trees, private identities or original
logs are distributed by this directory. Device execution, P4/PF closure, product
budgets and release licensing remain separate gates.

## Final candidate evidence (2026-10-07)

The canonical final tester receipt is
`build/ros214-security-20261007/FINAL_TESTER_RESULT.json`. Its test-time snapshot
records 27/27 frozen candidate source hashes and 6/6 unrelated hashes; those hashes
bind the tested source tree before this documentation-only update. The offline
fixture preflight is 31/31, while installed-prefix positive/negative cases and ABI
guards are reported separately. Maintenance
candidate checks 3/3, Host CTest 14/14, candidate ASan 14/14, targeted parser/
RTTI/factory/observer checks, Host QoS/graph/service close 20 writes/20 takes, both
reaccept branches, and a relocated consumer `PASS_CLOSED_RUNTIME` 20 writes/20
takes. The corrected `final-run-moved-r4b-closed` trace uses the moved `r4b/runtime`
ROS prefix, excludes original task-build/runtime/install directories from child PATH,
and directly records moved DDS/CDR origins. The earlier moved run remains PARTIAL
because it loaded the original CDR runtime. The standalone consumer used the required
project build isolation; no claim is made that it was outside the repository.

The API23 r4 ROS consumer is `PASS_STATIC_ONLY`: it uses target ament_index_cpp 1.8.2,
unchanged RMW/typesupport and candidate Fast DDS/CDR prefixes, with no stale Host
ament directory and no absolute `C:` DT_NEEDED. No device execution or SDK/platform
DSO body distribution occurred. The initial Fast CDR supporting run covered 13 normal
cases; malformed sequence lengths were not tested. Final ASan dependencies were not
instrumented and TSan was not run. Busy handling defers coordinator actions only;
the final diagnostic capture being Busy still makes close fail; this is a local
diagnostic branch, not an SDK `CloseBusy` method. This candidate does not establish SDK epoch/drain,
production readiness, P4/PF closure or a QualificationProfile.

## Metadata-only R2 delta

The final R2 correction changed only patch serialization and the bound digest to
`a74a796925b435e40dfae4ac18863e8c8e280788a8b0368b0b3ba23000b5016b`. Raw source,
EOL maps and runtime code were unchanged. The final delta preserved 24/24 compiled
inputs, 11/11 prepared outputs and 6/6 copied headers, with maintenance 3/3,
official blob delivery PASS and staged diff check PASS. The earlier cached-whitespace
failure remains retained history; no waiver was used.

## CVE-2026-63340 mitigation and malformed CDR (2026-10-07)

The statement above that "malformed sequence lengths were not tested" describes
the candidate package as it was frozen. A later bounded Host task
(`CANDIDATE_SECURITY_MITIGATION_FROM_3BE5492`) closed that specific gap and
verified the official configuration-led mitigation for CVE-2026-63340 on this
candidate. Full record:
[docs/CVE_2026_63340_HOST_VERIFICATION_20261007.md](../../../../docs/CVE_2026_63340_HOST_VERIFICATION_20261007.md).

This did not change the candidate. No prepared source file, patch, receipt or
manifest in this directory was modified — only this README's evidence statement
was extended. The security work uses an isolated `SECURITY=ON` rebuild of the
same prepared source, and the candidate default stays `SECURITY=OFF`.

In short: an isolated Host Fast DDS 2.14.7 with `SECURITY=ON`, the builtin
PKI-DH / Access-Permissions / AES-GCM-GMAC plugins and a `SIGN` discovery
profile refused an unauthenticated peer at the plugin authentication gate in
both directions (11/11 scenarios, receipts under
`build/candidate-security-mitigation-20261007/`); a malformed-CDR suite over the
real generated `std_msgs` typesupport passed 19/19, and a test-owned
`IPayloadPool` showed the advisory's `0x7FFFFFFF` reservation request does reach
`RTPSReader::reserve_cache` for a resizable reader. The advisory still lists no
patched version: the mitigation is configuration, and it is a Host result, not a
device or production result.

One qualification belongs with that, and it is a negative result rather than a
footnote: the matrix does **not** attribute the refusal to
`discovery_protection_kind`. A single-knob probe that flips **only**
`allow_unauthenticated_participants`, leaving discovery protection at `SIGN`,
reproduces the same discovered-participant count as the fully unmitigated
control, so that field — not the protection setting — is what the measured
observable follows. What is verified is the candidate security profile as
configured, taken as the whole mitigation the advisory names, not that the named
protection setting is the one doing the work. The verification document carries
the three-configuration comparison.
