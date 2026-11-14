# D5 bounded UA device path

## Status and boundary

D5 is PRESENT, BUILDS, HOST_TESTED, DEVICE_TESTED and bounded
INTEGRATION_TESTED PASS for the finite native profile. The final evidence
covers both Fast DDS 3.6.2 and 2.14.5, both logical RK3588A board roles,
same-ABI representations 0/2 and mixed-ABI representation 0. It is a device
deployment and cross-device validation of the bounded D1–D4 applications; it
is not full DDS-OPCUA, DDS-RPC, DDS-WEB or OMG conformance. The overall
standards product remains NOT VERIFIED and C3 remains BLOCKED.

D5 adds a separate OpenHarmony SDK23 arm64-v8a CMake wrapper. It reuses the
existing D1–D4 application sources above the public DDS API and installs the
existing C4 Socket Transport configuration callback. It does not change Fast
DDS, Fast CDR, RTPS, discovery, reliability, QoS or Transport semantics. The
Windows D1–D4 host guards remain intact. D5 uses the existing Socket Transport
with builtin UDP/SHM and data sharing disabled; DSoftBus is the inter-board
transport. Windows UA traffic uses byte-only SSH forwarding, with no protocol
parse, rewrite or DDS forwarding.

## Native lifecycle and scope

The public-DDS participant configurator is thread-local and lexically scoped:
the owning main thread installs it before participant creation, and the scope
outlives all participants. An empty scope preserves the prior explicit UDPv4
configuration; worker threads do not inherit it. Each native DDS application
or peer process owns the existing C4 DSoftBus Runtime, destroys its DDS
entities before joined Runtime close, and keeps the existing D1–D4 application
contracts. UA-only fixture processes retain their existing non-DDS lifecycle.
The native D4 peer uses one
participant with up to five separate writer/reader jobs and distinct full
Writer GUID entity IDs, preserving the existing Transport participant-view
bound.

The pinned open62541 source is 1.4.21. Its unchanged official POSIX Ethernet
event-loop provider is added by the native build wrapper because the
OpenHarmony CMake path omits it despite the compiler defining `__linux__`.
The source archive is verified in the retained build inputs; no upstream
patch or stub is used. Query and historizing remain enabled, and the existing
D4 finite service envelope/history limits remain in force.

The device runner uses private invocation trees under `/data/local/tmp`,
stop-file controls, and rollback to the prior accepted revision with matching
closure. For native DDS process collection and the D4 local-UA child
collection, normal stop waits 12 seconds, then bounds TERM/reap and KILL/reap
at five seconds each. D1's fixture and D2's client retain their existing
budgets. Any forced recovery or controller-cleanup failure is a failed case
even when the application returned zero. Actual numeric OS exits are recorded
separately from an uncollected/unknown exit; worker jobs are not counted as OS
processes.

## Build and acceptance recipe

Use a fresh output directory and the exact ABI version for each invocation:

```powershell
python -B scripts/standards/build_ua_device.py --version 3.6.2 --out NEW --ua-source PINNED_SOURCE --ua-archive PINNED_ARCHIVE --dds-prefix MATCHING_NATIVE_PREFIX --stock-prefix MATCHING_STOCK_PREFIX --toolchain SDK23_OHOS_TOOLCHAIN --clang SDK23_CLANG --platform-dir NATIVE_PLATFORM_LINK_PREFIX --client-manifest PINNED_ASYNCUA_MANIFEST
```

Repeat with `--version 2.14.5`. The builder records seven commands per ABI.
Then run the unfiltered canonical matrix:

```powershell
python -B scripts/standards/run_ua_device_acceptance.py --build-summary NATIVE36/summary.json --build-summary NATIVE214/summary.json --out NEW_EVIDENCE --hdc EXPLICIT_HDC_SHIM_OR_TOOL --ssh-map TRUSTED_MAP_JSON
```

The recorded run used an explicit HDC-to-SSH shim because live `hdc`
enumeration was empty. The logical aliases and LAN mapping, pinned host keys,
UDIDs, token/permission and naming-policy receipts are retained; no board
policy, token database, network or service configuration was changed.

## Evidence

The authoritative final device/native receipt is
[`TEST_RESULT.json`](../../build/d5-20261114/final-tester/TEST_RESULT.json),
with the full matrix in
[`device/summary.json`](../../build/d5-20261114/final-tester/device/summary.json)
and the independent census in
[`device/MAIN_CENSUS.json`](../../build/d5-20261114/final-tester/device/MAIN_CENSUS.json).
It records 12 lanes and 120 phases, 276 native plus 120 local-UA natural
OS-zero exits (396 total), 120 native jobs, zero forced cleanup and 104 unique
deployed paths. The controller's seven tests passed. Both fresh native ABI
builds passed seven commands each; the
shared regression passed 175 tests, the documentation guard passed 93 tests
with 7 declared digests, and one standalone host transport contract passed
per ABI. The broad host final receipt is
[`host-final-tester/TEST_RESULT.json`](../../build/d5-20261114/host-final-tester/TEST_RESULT.json).

The first setup failures remain retained diagnostics: a missing `IDL_CLANG`
environment and an unquoted dotted CMake definition. Corrected fresh runs
passed; no test assertion or timeout was weakened. Review closure is
[`REVIEW_CLOSURE.json`](../../build/d5-20261114/review/REVIEW_CLOSURE.json):
R1/R2 are resolved, with no actionable blocking findings. The final reports
remain scoped execution evidence; the global PRE_COMMIT decision belongs to
the main agent after documentation synchronization.

## Limits

D5 does not claim normative DDS-RPC generated-wire interoperability or full
DDS-OPCUA Complete behavior. The D4 custom v1 envelope has no TypeId prefix,
payload maximum 4096 bytes, 12 operations and four writer-bound session slots.
The independent asyncua fixture uses the finite Query/Browse/HistoryUpdate
extensions documented by D4. Physical Socket fault injection was not
performed; controller negative probes are local synthetic process probes.
The HDC shim is not a claim of direct USB HDC enumeration.

`BLOCKED != PASS`, `IMPLEMENTED != VERIFIED`, `BUILDS != TESTED`, and
`HOST_TESTED != DEVICE_TESTED`.
