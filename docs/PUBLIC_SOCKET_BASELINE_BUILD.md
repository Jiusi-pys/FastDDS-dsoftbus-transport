# Public Socket native DDS baseline checkpoint

This is a historical checkpoint. Its preserved `MDDS_*` path and option names
bind the original run; current public package names and build options are listed in
[NAMING](NAMING.md) and [INSTALL](INSTALL.md).

Status: **`PRE-COMMIT SNAPSHOT: LOCAL_NATIVE_DDS_BASELINE_HOST_VERIFIED / CHECKPOINT_READY`**
Artifact: `.artifacts/new-public-socket-dds-checkpoint/run-20260927T004706Z/resume-20260927T005155Z`

This pre-commit snapshot is authorized for one local current-branch commit
under `LOCAL_NATIVE_DDS_BASELINE_COMMIT`. It does not authorize merge, push,
device execution, SDK activation, release, or a QualificationProfile. A future
commit hash is intentionally not recorded in this pre-commit document.

## Reusable Host build recipe

Use a fresh Windows VS2022 x64 Release build directory with CMake, the existing
Fast DDS 3.6.2.0 CONFIG installation, and declared external checkouts. The
tested fixed upstream inputs are Fast DDS `343f155c36b561db3d9f047a65d866ce0f3da300`
and the authorized `communication_dsoftbus` source checkout
`5518313ee70e79f788fc4d9f9a9310c8b24827cc`; the repository gitlink remains
`dd6037ce496828133bb9af89b37e2d08232a10d9` and is not silently substituted.

Representative configure flags from `DECLARED_DEPENDENCIES.json` are:

```powershell
cmake -S <source> -B <build> -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_CONFIGURATION_TYPES=Release -DFASTDDS_DSOFTBUS_FASTDDS_MODE=CONFIG `
  -DBUILD_TESTING=ON -DBUILD_DSOFTBUS_TRANSPORT_FACADE=ON `
  -DBUILD_DSOFTBUS_PROVIDER_ADAPTER_PHASE0=ON -DBUILD_DSOFTBUS_HOST_PROVIDER_LOOP=ON `
  -DBUILD_DSOFTBUS_PUBLIC_SOCKET_FAKE_TESTS=ON -DBUILD_PUBLIC_SOCKET_DDS_CONTRACT_TESTS=ON `
  -DBUILD_PUBLIC_SOCKET_DDS_EXPERIMENT=OFF -DBUILD_PUBLIC_SOCKET_RUNTIME_HOST_TESTS=ON `
  -DBUILD_PUBLIC_SOCKET_RUNTIME_PROBE=OFF -DBUILD_FASTDDS_DSOFTBUS_EXPERIMENT=ON `
  -DFASTDDS_DSOFTBUS_EXPERIMENT_NATIVE=OFF -DFASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP=OFF `
  -Dfastdds_DIR=<host>/share/fastdds/cmake -Dfastcdr_DIR=<host>/lib/cmake/fastcdr `
  -Dfoonathan_memory_DIR=<host>/share/foonathan_memory/cmake `
  -DFASTDDS_SOURCE_DIR=<Fast-DDS> -DDSOFTBUS_SOURCE_DIR=<communication_dsoftbus>
cmake --build <build> --config Release
ctest --test-dir <build> -C Release --output-on-failure
```

The exact build used CMake 4.4.3, MSVC 19.44.35228.0, Python 3.13.9 and the
external host packages declared in the artifact. Set the Windows PowerShell
module path needed by the `Get-FileHash` tests. Host build/test commands,
wrong-variant gates and Python-only analyzer registrations are repository-local
evidence; external orchestration, HDC/device operations, target builds, SDK
activation and private grant runners are outside this recipe.

## Target configuration inputs (not executed in this checkpoint)

The target recipes are `cmake/PublicSocketRuntimeProbe.cmake` and
`cmake/PublicSocketDdsExperiment.cmake`. Use a fresh OHOS/API23 build with the
declared target Fast DDS/Fast CDR/foonathan CONFIG packages, the authorized
Socket source headers, and an absolute `MDDS_PUBLIC_SOCKET_LIBRARY` pointing
to the audited platform DSO input. This is distinct from SDK-bundled headers
or a firmware-source mapping. Do not package that DSoftBus system DSO.

Enable `BUILD_DSOFTBUS_PUBLIC_SOCKET_SHIM` plus exactly the requested isolated
`BUILD_PUBLIC_SOCKET_RUNTIME_PROBE` or `BUILD_PUBLIC_SOCKET_DDS_EXPERIMENT`.
Disable all Fake/Host contract variants and `BUILD_TESTING` for this package.
Supply `MDDS_RUNTIME_TOKEN_BRIDGE` (a newly built compatible C bridge) and
`MDDS_RUNTIME_SOURCE_SHA256` (the exact source receipt digest). The existing
`experiments/fastdds_socket/TokenBridge.cpp/.h` source requires the declared
platform `__h` toolchain/AccessToken headers and libraries; app-side Fast DDS
uses `__n1`. No ABI macro substitution is supported. Build only the chosen
`public_socket_runtime_probe` or `public_socket_dds_experiment` target.
These CMake targets specify `$ORIGIN`; dependencies and deployment identity
still require an explicit manifest and separate runtime authorization.

The build examples use placeholders: replace them with quoted absolute paths.
No SDK, system library, historical device package or build cache is distributed.
Task-bound grant/revoke launchers and private device configs stay in local
artifacts, rather than becoming an implicitly authorized generic runner.
The promoted V4 collector is DDS-specific (`mdds_dds_<run>_<side>`/DDS_MAIN),
not a universal loader probe. Source-header to firmware/full ABI equivalence,
SDK cancellation/drain and original-event identity remain unproved.

## Verified candidate scope

- Candidate frozen files: 345/345 SHA-256 matches before and after testing;
  final candidate adds only the narrowly scoped `.gitattributes` LF delivery
  rule and final documentation.
- Fresh Release CONFIG configure/build passed. Canonical full CTest passed
  `420/420`, exit 0, 144.58 seconds.
- The initial `418/420` result retained two PowerShell `Get-FileHash`
  environment failures; restoring the Windows PowerShell module path preceded
  a complete `420/420` rerun. The canonical console receipt also discloses that
  `LastTest.log` was later overwritten by discovery, so no reconstructed raw
  transcript is claimed.
- Final affected post-review helper gate passed `3/3`; it is additional
  affected evidence, not a replacement full suite.

## Boundaries and provenance

The current checkpoint reuses prior R3 ASan/device evidence by matching source
bytes; it does not claim new ASan, target, or device execution. The versioned
code remains default `NativeBackendUnavailable`; P0/PF-01/PF-02 remain
`BLOCKED / NOT QUALIFIED`, P4 is not released, and SPDP/SDK drain/full
TransportInterface limits remain. Existing real DDS windows and 20 formal sample
chains remain bounded small-DDS evidence, not full QoS/topology or production
qualification.

Seven reusable helpers plus three analyzer tests are promoted with recorded
provenance. The authorized external header checkout is `communication_dsoftbus`
at `5518313...`; the unchanged repository gitlink remains `dd6037...`. These are
different provenance objects and the gitlink is not silently updated. Local raw
artifacts and external installed dependencies are evidence, not clone contents.

The candidate review reports no unresolved blocking finding. Remaining Main-owned
technical gates include actual index/mode/blob/tree verification, graph and
allowlist checks, and the final staged-tree review. Historical `NOT_COMMITTED`
labels in older package sections remain historical and are not rewritten as a
global status.

Evidence index: [Public Socket evidence index](PUBLIC_SOCKET_EVIDENCE_INDEX.json);
current raw receipts are under the artifact path above and are not expected in a
clone.
