# SECURITY=ON ROS closure attempt — 2026-10-07

Task: `SECURITY_ON_ROS_CLOSURE_FROM_A885A37`, baseline
`a885a3781ac3e022c7186da6a1420ad20c2255ca` (parent `3be54925…`, tree `b1327da2…`),
branch `claude/dsoftbus-transport-20260924`.

This document records what was actually executed. It does **not** claim the
ordinary-ROS-under-security closure, because that closure did not pass. The exact
failure and the control that localises it are in section 5.

## 1. What was built in this round

| Artifact | State |
| --- | --- |
| OpenSSL 3.5.9 | Released tag `openssl-3.5.9` (annotated tag object `d0ca66a1abe52545f14eca635c648932fcde5615`, peeled commit `45e844fa2a14ec92d146bd8f5778ac130b6625fb`), built `VC-WIN64A` and installed to a private prefix inside the task build tree |
| Fast DDS 2.14.7 SECURITY=ON | Built from a **fresh** upstream tree with the project's security overlay (`prepare_fastdds_source.py --security-mode ON`), linked against that OpenSSL 3.5.9, installed to a new prefix |
| Dependency install binding | `scripts/bind_dependency_install.py` produced `share/fddst-dependency-binding/fastrtps.json` in that prefix (435 headers + 7 configuration files hash-bound, `publisher_trust: UNVERIFIED`) |
| ROS 2 C++ consumers | Unchanged upstream `rosidl_typesupport_fastrtps_c/_cpp`, `std_msgs`, `example_interfaces`, `rmw_fastrtps_shared_cpp`, `rmw_fastrtps_cpp` recompiled against the ON prefix; `rmw_fastrtps_cpp.dll` imports that `fastrtps-2.14.dll` |
| Project bridge | `integrations/ros2` recompiled against the ON prefix |
| Ordinary ROS application | Unchanged `examples/ros2_transport` recompiled; loads the ON dependency and the ON bridge (`FDDST_LOADED` names both and their prefixes) |
| API23 target | Fast DDS 2.14.7 SECURITY=ON cross-built with a target OpenSSL 3.5.9; target bridge linked |

The Fast CDR 2.2.8 prefix and every earlier artifact from the previous round are
retained untouched.

## 2. Preflight input binding (the specific gap this round was asked to close)

The previous round's `--fastdds-include` was a free-floating override: any header
tree that happened to define `HAVE_SECURITY 1` would pass. The checker now
verifies a **declared, content-bound** input instead:

* the manifest's `fastrtps` dependency carries a `source_binding` naming a binding
  record relative to the selected binary's prefix root;
* the record binds the selected library path and SHA-256, the installed
  `include` root, every installed header under it and every installed
  `share/fastrtps/cmake` file, plus the build cache digest and its `SECURITY`
  setting;
* `--fastdds-include` is accepted **only as an equality assertion** against that
  bound root, never as an override;
* `HAVE_SECURITY` must be exactly `0` or `1`, and the record's build setting must
  agree with the installed header.

`binding record` proves content correspondence. It is **not** publisher
attestation and the checker says so in its own output
(`SECURITY_SOURCE_BINDING_VERIFIED … publisher trust remains unverified`, plus
`SECURITY_RUNTIME_UNPROVEN`). `production_ready` stays false and no
QualificationProfile is produced regardless of the declared mode.

### Materialized cases (real files, real installed prefix)

`build/security-on-ros-closure-20261007/run_preflight_binding_cases.py`, receipt
`receipts/PREFLIGHT_BINDING_CASES.json` — **8/8**:

| Case | Result |
| --- | --- |
| Bound install exactly as installed | exit 0, `SECURITY_SOURCE_BINDING_VERIFIED` + `SECURITY_COMPILED_IN` |
| Relocated dependency and package prefixes, identical content | exit 0 — relocation is legitimate |
| Foreign include tree, byte-identical content | refused, `SECURITY_SOURCE_BINDING_INCLUDE_MISMATCH` |
| Installed `fastrtps/config.h` modified inside the declared root | refused, `SECURITY_SOURCE_BINDING_HEADER_MISMATCH` |
| Same version and basename, actual SECURITY=OFF library | refused, `SECURITY_SOURCE_BINDING_STALE` + `DEPENDENCY_VERSION_OR_ABI_MISMATCH` |
| Stale binding record | refused, `SECURITY_SOURCE_BINDING_STALE` |
| Deleted binding record | refused, `SECURITY_SOURCE_BINDING_STALE` |
| Manifest whose dependency declares no binding | refused, `SECURITY_SOURCE_BINDING_MISSING` |

The unit suite (`tests/install_preflight_tests.py`) is 50 cases and passes.

## 3. Fragment geometry with real, non-zero fragment sizes

The previous round observed the reader reservation with `fragment_size = 0`, which
short-circuits the geometry helper. `tests/ros2_host/security_candidate/secure_probe/
fragment_geometry_tests.cpp` exercises it with the values the wire can actually
carry. Receipt: `receipts/FRAGMENT_GEOMETRY_RESULT.json` — **14/14**.

* `fragment_size` 1, 2 and 3 (each representable in the RTPS `fragmentSize`
  field) are **refused** — `CacheChange_t::calculate_required_fragmented_payload_size`
  requires at least 4 bytes to hold a fragment index.
* A payload one byte above the overflow guard is refused; at the guard it is not.
* The advisory's `0x7FFFFFFF` payload with a real 65500-byte fragment size passes
  the geometry, and the computed minimum is always clamped up to the payload size
  — the helper is therefore **not** a bound on a large sample, only a guard on
  degenerate fragment sizes and on the overflow range.
* The reader still asks its payload pool for the raw sample size
  (`requests=[2147483647]`), which is unchanged from the previous round.

The test-owned pool records and refuses requests above a small ceiling. That
ceiling is a test safety control and an observation device, never a product limit,
and it is not evidence that the library refuses the input.

## 4. Ordinary ROS under security — the positive case did not pass

The positive case drives the unchanged `rclcpp` application through the project's
own factory, Transport and opaque-byte Host seam, two independent processes, dual
QoS, with the builtin PKI-DH / Access-Permissions / AES-GCM-GMAC plugins active
and a `SIGN` discovery profile. The access-control grants are exact per-endpoint
grants with `<default>DENY</default>`; no wildcard grant was used to make the
graph or the services pass.

The security configuration is genuinely active and enforcing. Evidence from the
run's own logs:

* `FDDST_LOADED module=fastrtps-2.14.dll path=…\host-fastdds-secon-prefix\bin\…`
  — the ON dependency, from the intended prefix;
* plugin-sourced refusals, e.g.
  `[SECURITY Error] rq/fdds_…/transport_A/get_type_descriptionRequest topic not found in allow rule (Permissions.cpp:1245)`.
  This is the access-control plugin refusing a local reader because the enclave's
  grant list did not yet name that topic. The topic set was corrected and the
  refusal disappeared — the plugin, not the harness, decided.

Result: **the application exits with status 4 and no ROS samples are exchanged.**

## 5. Where it stops, and the control that localises it

After the grant list was corrected, no security-layer refusal remains. The
remaining failure is a Fast DDS transport/locator interaction that appears only in
the SECURITY=ON build:

```text
[RTPS_PARTICIPANT Warning] Could not create the specified receiver resource for
  'Invalid_locator:[_]:0'.  (createReceiverResources)
[RTPS_PARTICIPANT Error] No unicast locators to create unique flows
  (createAndAssociateReceiverswithEndpoint)
[DATA_READER Error] Problem creating associated Reader
```

The participant's user transport is configured with exactly one metatraffic
unicast locator and one default unicast locator (`integrations/ros2/Bridge.cpp`).
A SECURITY=ON participant creates additional builtin endpoints, and one of the
receiver-resource requests arrives with a `kind == 0` locator, after which the
reader cannot be associated and the application aborts.

**Control.** The identical harness, consumer and modes run against the retained
SECURITY=OFF prefixes: `run_pair.py --mode dual` reports
`pass=True`, errors `[]`, `A {writes: 10, takes: 10}`, `B {writes: 10, takes: 10}`
— 20 writes / 20 takes, `FDDST_HOST_IO … sends=N ingress=N`, RuntimeClose and
`FDDST_LOADED` all satisfied. So the harness, the transport, the opaque-byte Host
seam and the application are sound; the failure is introduced by the security
build, not by the test setup.

This is reported as **BLOCKED**, not as a limitation-after-the-fact pass. Fixing
it needs a change to the project factory/transport assembly (or an upstream
answer) that this round did not land, and inventing a locator workaround would
change Transport behaviour without evidence that it is correct.

## 6. What was not done

* **The negative ROS cases were not run to completion.** The plain-peer and
  mixed-dependency cases depend on a positive case that reaches discovery; with
  the positive case blocked at participant/reader association, their results would
  not be attributable.
* **Directed ASan covers the receive and parse paths but not the bridge or the
  ROS closure.** Fast CDR and Fast DDS (SECURITY=ON) were rebuilt with
  `/fsanitize=address` appended to the default MSVC flags, and the two
  receive-path probes were rebuilt against them and re-run:
  fragment geometry 14/14 and bounded payload pool 6/6, with **zero
  AddressSanitizer reports**. The first ASan attempt replaced the default MSVC
  flags instead of extending them and failed to link a third-party Boost symbol;
  that attempt is retained as history. The project bridge could **not** be
  sanitized: `integrations/ros2/CMakeLists.txt` requires an actual selected
  RELEASE shared binary and rejects a sanitizer build at configure. The ROS
  closure was not re-run under ASan. TSan remains NOT_RUN. Receipts:
  `receipts/ASAN_RESULT.json`, `receipts/ASAN_FRAGMENT_GEOMETRY_RESULT.json`,
  `receipts/ASAN_BOUNDED_PAYLOAD_POOL_RESULT.json`.
* **No device, HDC, deployment, grant or target execution occurred.** The API23
  result is a static build/link audit only: the target bridge is ELF64/AArch64,
  has no absolute `C:` `DT_NEEDED`, and links `libssl.so.3` / `libcrypto.so.3`.

## 7. Status

```text
SECURITY_PREFLIGHT_SOURCE_BINDING_VERIFIED   (8/8 materialized cases, real prefix)
MALFORMED_FRAGMENT_GEOMETRY_TESTED           (14/14, real non-zero fragment sizes)
API23_SECURITY_STATIC_ONLY                   (ELF64/AArch64, no absolute host paths)
SECURITY_ON_ROS_HOST_INTEGRATION             BLOCKED (see section 5)
SECURITY_ON_ROS_NEGATIVES                   NOT_RUN (dependent on the above)
DIRECTED_ASAN                               RECEIVE_AND_PARSE_PATHS_ONLY (0 reports)
DEVICE_SECURITY_PROFILE_NOT_RUN
NOT_PRODUCTION_QUALIFIED
```

Not claimed: `CVE_2026_63340_UPSTREAM_FIXED`, `ALL_SECURITY_ISSUES_CLOSED`,
`PRODUCTION_READY`, and — importantly — **not** the ordinary-ROS security closure.
Product budgets remain `NEEDS_PRODUCT_VALUE`; PF-01/PF-02, licensing, SBOM closure
and the platform SDK contract remain open.

---

# Resolution — 2026-10-07, task `SECURE_ROS_RECEIVER_RESOURCE_CLOSURE_FROM_2405DED`

The historical failure above is retained unedited. This section records the
resolution. Baseline `2405dedcf2f2d0be8e7764e09f808ad4cf72948f`, parent
`5320df046bff65eb41d584e511e5744286f194cf`, tree `7848621f34c24df20c81e29e5521b2b04635ca64`.

## The first real rejection point

The formatted `Invalid_locator:[_]:0` was **not** the cause and did **not** come
from an extra secure builtin endpoint. It is the result of Fast DDS's own
`ApplyMutation` / `applyLocatorAdaptRule` loop, which rewrites the locator after
`BuildReceiverResources` has already failed for a reason the mutation loop then
hides. The real first rejection is recorded in the failed run's own stdout:

```text
ROS_STARTUP_EVENT ... operation=FacadeDecision_FacadeError_result
  value_type=LOCAL_STATUS value=10
```

`FacadeError` 10 is `InsufficientReceiverCapacity`
(`include/fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp`), which is emitted only
by `src/DSoftBusTransport.cpp:204`:

```cpp
if (capacity < frozen_.maxMessageSize) { Diagnose({FacadeError::InsufficientReceiverCapacity, "open_input"}); return false; }
```

## The capacity candidate was correct

The value `capacity` is not an arbitrary number. It is derived in two steps:

```cpp
// upstream RTPSParticipantImpl::createReceiverResources
uint32_t max_receiver_buffer_size =
        is_secure() ? std::numeric_limits<uint16_t>::max() : (std::numeric_limits<uint32_t>::max)();

// upstream NetworkFactory::BuildReceiverResources
uint32_t max_recv_buffer_size = std::min(transport->max_recv_buffer_size(), receiver_max_message_size);
```

The upstream comment states the reason: a secure `ReceiverResource` needs an
auxiliary buffer to decrypt the message, so it caps what it will accept at
`uint16` max. The project transport reported `max_recv_buffer_size() == 65536`
because the bridge built `DSoftBusTransportDescriptor config(65536)`.

So for any participant with DDS Security, `capacity = min(65536, 65535) = 65535`,
and the transport's own rule `capacity >= maxMessageSize` could **never** be
satisfied. Every input channel failed to open, every locator was then mutated, and
the reader could not be associated.

Chain, in order: `is_secure()` true → `max_receiver_buffer_size = 65535` →
`capacity = min(65536, 65535) = 65535` → `65535 < 65536` →
`InsufficientReceiverCapacity` → no receiver resource → `applyLocatorAdaptRule`
mutates the valid locator → `No unicast locators to create unique flows` →
`Problem creating associated Reader`.

## The minimal repair

`integrations/ros2/Bridge.cpp`: the descriptor's maximum message size is
`65535`, not `65536`. The transport's rule — never accept a receiver buffer
smaller than the largest message the descriptor promises — is correct and was
left alone; what was wrong was the descriptor promising one byte more than a
secure participant can ever offer. `65535` still exceeds Fast DDS's 65500
no-fragment limit that the descriptor overrides, not a lower bound it has to clear, and the
opaque-byte link's own `max_message_size=65536` was deliberately left as an upper
bound. `DSoftBusTransport::Init`'s other constraints (`limits.max_record_bytes`
equality, `InvalidLimits`, `UnsupportedNoFragmentRequest`, the V1 directory and
locator semantics) are unchanged.

RED → GREEN on the same path: before, the run above; after, case 01 below.

## Ordinary ROS under security, executed

`build/security-on-ros-closure-20261007/run_ros_security_closure.py`, receipts in
`receipts/ros-closure/` — **4/4**.

| Case | Result |
| --- | --- |
| 01 secure dual pair | `pass=True`, `errors=[]`, A `{writes: 10, takes: 10}`, B `{writes: 10, takes: 10}` — 20 writes / 20 takes, real graph, real service request/response, original DDS deletion, RuntimeClose, normal exit; `FDDST_HOST_IO … send_errors=0` with non-zero sends and ingress on both sides, so the opaque-byte Host seam carried real records both ways (per-run counters are not pinned here; each run regenerates its own case directory) |
| 02 unauthenticated peer (one side with security disabled) | refused — no match, no samples |
| 03 mixed dependency (ON application and RMW with the SECURITY=OFF dependency) | refused in the Windows loader (`STATUS_ENTRYPOINT_NOT_FOUND`) before any project code runs; this shows the mix is refused but does **not** exercise the project's own build-mode gate |
| 04 secure pair, real fragmented sample | `pass=True`, `errors=[]`, 20 writes / 20 takes with `ROS_FRAGMENT_TAKE bytes=70029/70032 exact=1` |

Case 04's payload is above the transport's 65535 message ceiling, so the writer
must fragment and the reader must reassemble under security; the takes carry
`exact=1`. A 200000-byte payload was tried first and did reassemble exactly
(`bytes=200029 exact=1` for all five ordinals) but exceeded the run's time budget,
so the smaller size is used to keep the case inside it.

Dispositions are kept separate. Evidence classes are reported separately and
not collapsed: the passing runs carry **0** authentication/access-control error
messages and **47 `SECURITY_CRYPTO` lines per side**, i.e. the AES-GCM-GMAC
plugin is being driven on every run. The plugins were **active** (with security
enforced, a missing or inactive plugin makes the RMW refuse to create the
participant), discovery protection remains `SIGN`, and topic-level
`data_protection_kind` remains `NONE` — so **business payloads are not encrypted**
and discovery signing is not payload encryption. No wildcard topic grant was used
to make the graph or the services pass; the enclave grants remain exact
per-endpoint grants with `<default>DENY</default>`.

## Status after this task

```text
SECURITY_ON_ROS_HOST_INTEGRATION_VERIFIED
LOCAL_COMMITTED
DEVICE_SECURITY_PROFILE_NOT_RUN
NOT_PRODUCTION_QUALIFIED
```

Still open, unchanged by this task: CVE-2026-63340 has no upstream fix; an
authorized, matched peer presenting a large legitimate `sampleSize` remains an
uncovered resource risk; PF-01/PF-02, licensing, SBOM closure and the platform SDK
contract are untouched; product budgets remain `NEEDS_PRODUCT_VALUE`. Target
results remain static-only and no device was run.

---

# Validation completion — 2026-10-07, task `SECURE_ROS_VALIDATION_COMPLETION_FROM_CB9AD6E`

Baseline `cb9ad6e6d1fd92c709b23ef978a512a470c31a77`. Four items only.

## Log preservation

`run_ros_security_closure.py` and `run_security_matrix.py` now allocate a fresh
`attempt-*`/`case-*` directory per invocation, so a later run cannot delete an
earlier result. `tests/ros2_host/security_candidate/attempt_log_tests.py` proves
it: against the old code its assertion that the first run's raw bytes survive
fails, and it passes against the new code. Running scenario 00 twice into the same
`--out` leaves two `attempt-*` directories, each with its own `case-*` and
`SECURITY_MATRIX_RESULT.json`, both 1/1.

The **historical RED logs were not retained** — they were overwritten before this
task. No recovered artifact is claimed. A new RED was produced this round from an
isolated copy of commit `2405ded` (`git archive 2405ded` into
`red-prefix-source`, bridge rebuilt into `red-bridge-prefix`) and is labelled
`RE_PRODUCED_THIS_ROUND` in `RED_REPRODUCED_20261007.json`. It reproduces the
whole chain: `FacadeError 10 InsufficientReceiverCapacity`, then `9 InvalidLocator`,
then `Applied mutation until: Invalid_locator:[_]:0`, then `No unicast locators to
create unique flows`.

## Facade capacity boundary

`tests/transport_facade_tests.cpp` gained `FCD-08` with the matrix below, built in
a **new** tree (`facade-build`) against the standalone Fast DDS 3.6 CONFIG package
— legitimate because the rule under test lives in project code and is
version-independent. Result: 22 groups, 0 failures.

| descriptor max M | offered capacity C | accepted |
| --- | --- | --- |
| 65535 | 65534 | no |
| 65535 | 65535 | yes |
| 65535 | 65536 | yes |
| 65536 | 65535 | no |
| 65536 | 65536 | yes — the general-Transport control |

The refusal is asserted as exactly `InsufficientReceiverCapacity` *after* a
successful `init(nullptr, M)`, with rollback checked (`open_channels` unchanged,
the original local still open, the competing local not) and the pre-existing
receiver still delivering exact bytes. The last row is the control that the
Transport is **not** globally locked to 65535: 65536 remains legal when every
other condition holds.

## A / B / C within one SECURITY=ON build

* **A** — development control: the same ON binaries as B, plugins off through an
  explicit flag that refuses to combine with a keystore or a plain side. The
  driver requires the enclave line and the crypto lines to be **absent** for A and
  **present** for B, so A cannot be mistaken for a fallback.
* **B** — the legal secure configuration; 20 writes / 20 takes, graph, service,
  DDS deletion, RuntimeClose, normal exit, and a real fragmented sample.
* **C** — security explicitly required with the governance material corrupted. The
  plugin refuses at the real plugin: `Input data has not PKCS7 S/MIME format`
  (`Permissions.cpp:356`), `Participant is not allowed with its own permissions
  file.`, `Error while configuring security plugin.` The ON dependency is loaded,
  no loader code appears, and no business take occurs.

**C terminates abnormally.** After refusing the configuration the processes exit
`0xC0000005` `STATUS_ACCESS_VIOLATION`, and under ASan this is an
`access-violation` inside `fastrtps-2.14.dll` in
`SequenceNumber_t::operator+=` while unwinding the failed
`create_participant` — no project frame is present, so it is an upstream
robustness defect on the invalid-configuration path, not a bridge defect. The
driver **records** the exit codes rather than requiring a clean one, and the
sanitizer report is recorded rather than hidden. "Not a loader failure" excludes
only the loader codes; it does not claim C exits cleanly.

## Bridge / ROS under ASan

`cmake/Ros214TestAsan.cmake` is a new **test-only** profile. The production guards
in `cmake/Ros214Candidate.cmake` and the installed preflight are untouched — the
three existing CMake files only gain lines. The profile requires native MSVC x64,
`/MD`, Release-only configuration, the explicit 2.14.7 candidate, `HAVE_SECURITY 1`,
and it **verifies actual instrumentation** by running `dumpbin /imports` and
requiring `clang_rt.asan_dynamic-x86_64.dll`. It derives `dumpbin` from the
selected compiler because the Visual Studio generator does not populate
`CMAKE_DUMPBIN`. Pointing it at the uninstrumented prefix fails configure with
`dependency is not instrumented: fastrtps`, so the check is a real gate.

Instrumented and exercised: Fast DDS 2.14.7 (SECURITY=ON, Release-only), Fast CDR
2.2.8 (Release-only), the project runtime, `fastdds_dsoftbus_ros2_host_bridge.dll`
and `ordinary_ros2_transport_consumer.exe`. The ASan runtime had to be placed
beside the instrumented modules; on `PATH` alone the children exited silently.

Results, with the loaded Fast CDR verified byte-identical (`4ee7e28b…`) to the
ASan build:

| Case | Sanitizer |
| --- | --- |
| 01 secure dual pair, 20 writes / 20 takes, RuntimeClose | 0 reports |
| 04 secure fragmented sample, reassembled exactly | 0 reports |
| C invalid governance | 6 report lines — the upstream access-violation above |

**Still uninstrumented**, so this is not a memory-safety proof for the whole
stack: `rmw_fastrtps_cpp`, `rmw_fastrtps_shared_cpp`, the `rosidl` typesupport
libraries, `std_msgs`, `example_interfaces`, and the rest of the upstream ROS
runtime (`rclcpp`, `rcl`, `rcutils`, `rmw` and their peers). TSan was not run.

---

# Failed-construction module-lifetime Host closeout — 2026-10-09

This append-only Host checkpoint uses baseline
`bfcd1af9e4a1306b36f234822af39883c6d8b524`. It preserves earlier raw reports and
scope labels. The final independent tester reports `PASS`; review R1 reports no
actionable findings. The accepted scope is
`HOST_VERIFIED / INDEPENDENT_REVIEW_CLOSED / NOT_PRODUCTION_QUALIFIED`.

The earlier control was `PUBLIC_API / NO_BRIDGE_DESCRIPTOR` through the patched
factory; it was not a pure upstream-only result. The current
defect under test is project hook/module lifetime ordering during participant
creation and local QoS cleanup. Earlier fault-range and unmapped-Bridge attribution
remain inferred where they were not directly measured. No generic upstream
`SequenceNumber_t` fix is claimed.

The four retained work-in-progress inputs and new real Bridge QoS test/CMake entry
were exercised from the frozen six-file source set. One successful preparation per
HookState/process is reserved permanently/CAS once, with one existing owned
`LoadLibrary` reference transferred to the retired slot. There is no Windows PIN,
failed-slot unload site, arbitrary `FreeLibrary`, or plugin-wide module-path
accumulation. Runtime states remain `0=true`, `2=false`, `1=uncertain`; state 3
retains owner and module. Static dependencies remain mapped, so this is not complete
SDK resource cleanup or static-destructor ordering proof. Normal unload was tested
in a fresh process after relevant QoS strong/weak holders were released; arbitrary
surviving-holder hot unload is unverified.
The real Bridge QoS test explicitly destroyed strong and weak holders with mapped
state still `1`, while the normal fresh-process path reached mapped state `0`.

Later legitimate owned references do not consume the transferred owned reference;
reloading the same image does not unload it while that transferred reference is held.
There is no Windows PIN claim and no guarantee permitting arbitrary external borrowed
`FreeLibrary`. The mapping retains code, statics and dependencies, but does not prove
static-object or destructor ordering.

Source proof binds base Hook `e3ed8570ea23e1bf829d9c7d1cd38a115d6384196ae94e0927e9b6d7251cad1d`
to the declared ON overlay `497e4f6fe4648e4e877b1258e9d472452187972bac3f81eacce34d30d7118c22`,
and selected DDS artifact `00dd63e842f5b72e0487163d78e73b6f896f0c5690055807c6d1e42339b73176`
equals the compiled output. Preparation 2.14.5 and 2.14.7 passed; prior RED/GREEN
records remain preserved, with no additional crash replay claimed.

## Final Host evidence

| Case | Result | Evidence boundary |
| --- | --- | --- |
| ON failed construction / local close | PASS | cleanup result 0, return 1, mapped 1, repeat preparation refused |
| ON resource-complete error | PASS | Runtime execution error: cleanup result 2, return 0, mapped 1, repeat preparation refused; result 2 is not a clean pass |
| ON fresh-process PublicDelete | PASS | cleanup result 0 after holders released |
| OFF failed/PublicDelete controls | PASS | Both modes passed |
| ON security cases 01/04 | PASS | Secure 20/20 graph/service close and secure 70000-byte fragment |
| ON security case 02 | EXPECTED REFUSAL | Unauthenticated peer rejected |
| ON security case 03 | EXPECTED LOADER REFUSAL | Windows `0xc0000139` before project code; not a project gate failure |
| C malformed governance | EXPECTED REFUSAL | Both children exit 4; PKCS7/S-MIME rejection, Runtime cleanup attempted, no business takes, zero sanitizer lines |

The malformed governance input was ASCII-invalid test-only signature text followed by
Windows CRLF. The signed permissions `.p7s` remained unchanged and both permission
hashes were verified. This test-only corruption and raw material are not distributed.

ASan covered the explicitly selected Fast DDS/CDR/project runtime/Bridge/application
and lifetime test. ROS/RMW/typesupport and the remaining ROS runtime were not
instrumented; TSan was not run. Prior startup PATH and dependency-order failures
remain environment evidence, not product RED. Earlier raw reports and old failures
remain unchanged.

The API23 target was not rebuilt or run because this is a behavior-equivalent OHOS
branch for this Host-only lifetime delta; the current Host delta itself was not
rebuilt for API23. No HDC/device operation occurred. Defaults remain
production-disabled; PF/P4 and QualificationProfile status are unchanged.

---

# Secure candidate target validation — 2026-10-10

This append-only record covers `SECURE_CANDIDATE_TARGET_VALIDATION_FROM_FD7F65D`
from baseline `fd7f65d34d499d9fd9f830342aac8755d98db5b1`. It does not rewrite
the earlier Host failures or their raw reports.

The formal target candidate is Fast DDS 2.14.7 (`010b908dc3bb2b6f2818d61ac8db462d01b79a1f`),
Fast CDR 2.2.8 (`1bc9c919311ffce555b445560bf7ae062a4e1c72`) and OpenSSL 3.5.9,
built for SDK API23, Clang 15.0.4, AArch64, shared libc++, Release,
`SECURITY=ON`, without ASan. `TARGET_R6_TESTER_RESULT.json` records build exit
0, ELF64/AArch64 closure, 111 package members, installed consumer/header identity,
and 65 affected static regression checks. This static build receipt itself
contains no HDC deployment or target execution and target ASan was not run in
that receipt. Separately, the S3 device window below executed the actual r6
package on the boards.

The final tester gate is `PASS` with `Commit gate: PASS`; the 35-file source
freeze has no drift and the independent review is `INDEPENDENT_REVIEW_CLOSED`
with no actionable findings. Device receipts are scoped and retain exact source
attribution: S1 normal is `device-S1-r7` using target-r4,
`PASS_SCOPED_S1` (20/20 dual-QoS writes/takes and restoration); the fragment
window is `device-fragment-r3` using target-r5, `PASS_SCOPED_FRAGMENT` (A10/B10
native fragments and 20 application markers); S2 is `device-S2-r4` using
target-r5, `PASS_SCOPED_S2_NEGATIVE` (real authentication rejection, zero
application delivery, expected exit 4); and S3 is `device-S3-r5` using target-r6,
`PASS_SCOPED_S3_FAILED_CONSTRUCTION` (pre-listener governance/plugin failure,
exit 4, no AV/signal/timeout, complete PID-bound capture ACKs). S2 retains
`WINDOW_FAILED` cleanup rather than a normal `Close(0)` claim; S3 retains no
normal listener/READY/application-delete claim.

All four windows passed byte, metadata, process and permission restoration
guards. These results do not establish SDK drain/epoch/hot-unload guarantees,
full platform ABI coverage, production readiness, P4/PF closure or distribution
authority. Private PKI is test input; discovery signing was observed, while
payload encryption was not claimed. S1-r6, fragment-r1, S2-r3 and S3-r4 remain
historical failures and are not relabeled.

The reviewed implementation deltas are diagnostic and observation-path changes:
the structured security trace routes SECURITY/CRYPTO information and all warning/
error records to stderr while preserving business stdout; native observation
keeps a shared 8192-record metadata cap, structural full-range `DATA_FRAG` records and a
bounded DATA-only prefix without introducing a protocol change. S2 admits the
full 111-entry load before listener/connect release and requires real queued
cleanup completion. S3 observes the SDK-free post-`NODE_CREATE` failure path
with exact PID-bound READY/ACK and `CAPTURE_COMPLETE` controls, preserving exit
4 and the default-off/non-production boundary. The selected Host ASan evidence
does not instrument prebuilt ROS; artifact versions remain source-bound, and
historical failure reasons and original reports remain unchanged.

```text
Implementation: PRESENT (frozen 35-file scoped package)
Target build: PASS (API23/AArch64, SECURITY=ON, Release, non-ASan)
Host/local affected checks: PASS
Device cases: PASS_SCOPED only, with the boundaries above
Independent review: CLOSED
Commit gate: PASS (tester evidence)
Production/P4/PF qualification: NOT VERIFIED
```
