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
