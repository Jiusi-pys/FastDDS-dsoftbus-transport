# Production foundation implementation plan

Task: `PRODUCTIONIZATION_FOUNDATION_FROM_FACCA428`; authorization:
`LOCAL_PRODUCTIONIZATION_IMPLEMENTATION`. Planning phase precedes implementation.
Baseline HEAD: `facca428ab9a15649f44f2bdf216e446c7d56d5d`.

1. Preserve unrelated dirty files, nested checkouts, private SDK/identity/raw evidence
   and historical failures. Scope this package to installed tooling and public docs.
2. Check actual pinned dependency artifacts and official advisories before coding.
   Record applicability and minimum maintenance/backport candidates; do not upgrade.
3. Add a stdlib Python, offline, read-only preflight to the existing three installation
   entry points (3.6 package, ROS 2 2.14 Host, ROS 2 2.14 OHOS). Generate path-free
   build metadata from actual imported libraries. Resolve binaries only from the
   installed prefix and explicit dependency/runtime inputs. Inspect headers/imports
   through an explicitly supplied LLVM readobj; never load a library or call SDK.
4. TDD: independent-prefix relocation, missing dependency/transitive DSO, wrong
   architecture/hash/version, unknown profile, missing identity, forged qualification,
   unsupported platform and missing platform-contract branches must discriminate.
   Valid installation inputs must remain separate from production eligibility.
5. Publish support contract, dependency applicability/ABI migration, a PF-01/PF-02
   delta referencing existing issues and four ELF identities, and proposed fault
   tests with expected observations. Product budgets remain NEEDS_PRODUCT_VALUE.
6. Independent tester -> agentic review -> fixes -> independent final tester ->
   docs_updater -> complete graph and pre-commit gates -> exactly one local commit.

No DDS/RTPS/RMW or native data-plane change, SDK/system mutation, device fault
injection, grant, new IPC/helper architecture, P4/PF release, QualificationProfile,
push or private upload. Device communication reruns are N/A for read-only tooling;
existing device evidence is cited within its original scope, never counted as new.

Risk: false readiness or accidental development-prefix fallback. Preflight must
fail closed; authority is never obtained from a configuration flag or local JSON.
GitNexus installation-file impact returned UNKNOWN (index one commit stale);
actual root include and independent ROS CMake install/export paths were checked.
No zero-caller result is treated as evidence of no impact. Regression scope:
installation, moved consumer, configuration/identity negatives and tool integration.
