# Production foundation acceptance — 2026-10-06

This record closes the first local productionization package from baseline
`facca428ab9a15649f44f2bdf216e446c7d56d5d`. It covers relocatable installation,
dependency and identity preflight, and public documentation. It does not issue a
production profile, qualify P4/PF, or declare `PRODUCTION_READY`.

## Result

| Area | Result | Evidence boundary |
| --- | --- | --- |
| Implementation | PRESENT | Installed read-only `install_preflight.py`, CMake installation wiring, support/security/PF documents |
| Host and core builds | PASS | Fresh Release Host/package install, core Release install and relocated core external consumer configure/build |
| OHOS target build | PASS | Configured target build and relocated installation artifacts; no device execution |
| Offline contract tests | PASS, 30/30 | `python -B tests/install_preflight_tests.py` |
| Naming boundary | PASS | `tests/naming_configuration_tests.py` |
| Relocated Host preflight | PASS | Explicit dependency/runtime inputs only; `INSTALL_INPUTS_VALID`, `EXPERIMENTAL_ONLY`, `PLATFORM_CONTRACT_UNVERIFIED`; `production_ready=false` |
| Relocated core preflight | PASS | Explicit core runtime input and system runtime inputs |
| Native installed template | PASS_REFUSAL, exit 2 | Correct refusal because legal external identity/platform/runtime closure is absent |
| Negative preflight cases | PASS, expected exit 2 | Ambiguous/missing libraries, wrong ABI, unknown profile, malformed identity, missing module/forward, equal ports, forged production request and unsupported templates |
| Device/data-plane integration | N/A | No HDC, SDK business call, grant, fault injection or ROS/V1/reaccept rerun was authorized |
| Production qualification | NOT ISSUED | P0/PF/P4, platform contract, supplier lifetime/drain mechanisms, approved product values and QualificationProfile remain open |

The final tester receipt is `build/production-foundation-20261006/final-tester/FINAL_TESTER_RESULT.json`.
It reports commit-gate PASS for this tooling-only package. The initial 22-contract
receipt, stale-input failures, review red/green records and old discriminator log
remain historical evidence; the final 30-contract run is the canonical post-review
result. Review R3 found no actionable findings after the R1/R2 repairs
(`build/production-foundation-20261006/review/AGENTIC_REVIEW_R3.md`).

## Public contract delivered

The first support contract records the intended tuple: RK3588A KaihongOS/OpenHarmony
6.1.0.31, SDK API 23 / native 6.1.0.32, AArch64, Clang 15.0.4 and shared libc++;
ROS 2 Jazzy with the original RMW and Fast DDS 2.14.5 / Fast CDR 2.2.5; and the
separate Host ABI Fast DDS 3.6.2.0 / Fast CDR 2.3.5. The initial candidate is
`BYTES_NATIVE`; RAW profiles, other topologies and other platform tuples remain
unverified. Normal startup, offline, reconnect, close, ID reuse, resource limits,
fault recovery and safe stop are mapped to existing evidence and explicit missing
platform/product mechanisms in [PRODUCTION_SUPPORT_CONTRACT_V1.md](PRODUCTION_SUPPORT_CONTRACT_V1.md).

Recovery, send/close, load and resource values are `NEEDS_PRODUCT_VALUE` where no
approved value exists. Historical 1-second, 5-second, 100-cycle and 72-hour values
are not promoted to product limits.

## Dependency and PF boundaries

[DEPENDENCY_SECURITY_20261006.md](DEPENDENCY_SECURITY_20261006.md) records the
actual 2.14.5, Fast CDR, foonathan_memory, TinyXML2, Asio and conditional OpenSSL
inputs against official advisories. It distinguishes fixed, affected, conditional,
unknown and deployment-dependent rows. No version was upgraded. The minimum
maintenance candidates and ABI/regression scope are proposed there; CVE-2026-63340
has no fixed version in the cited advisory and is not dismissed because DSoftBus is
the lower transport.

[PF_TARGET_INPUT_DELTA_20261006.md](PF_TARGET_INPUT_DELTA_20261006.md) reuses the
four existing target ELF/Build IDs and PF issue numbers. It adds only the still
missing evidence for payload last use, native call end, owner/channel last use,
callback/work settlement and pre-effect original-event identity. Async send and
completion callbacks remain candidates only; names do not imply drain or cancel
guarantees. Supplier mechanisms, product choices and local evidence gaps remain
separate, and no SDK or platform policy was changed.

## Next validation group

The next authorized production work should bind product budgets first, then select
the smallest real discriminator: a same-process stalled native send with concurrent
close; old event versus replacement numeric ID before native mutation; changed-network
offline/reconnect; permission or peer restart; one resource-exhaustion class; and
malformed RTPS/CDR plus reliability/fragment/filter coverage after maintenance
selection. Expected observations and disqualifying outcomes are listed in the support
contract. Device fault injection and new platform releases require separate authority.

## Release boundary

The package is suitable for a local documentation/tooling commit when the parent gate
confirms the final tester, review, graph and privacy checks. It is not a public binary
release: private identities, tokens, grants, SDK DSOs, raw device logs and developer
paths are excluded. A first-party release-license decision and distributable vendor
library/source-provenance decision remain open. `PRODUCTION_READY`, `P4_PASS` and
`QualificationProfile` remain unissued.
