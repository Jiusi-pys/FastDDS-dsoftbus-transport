# Offline installed-prefix preflight

## 2.14.7 candidate note (2026-10-07)

The installed preflight was exercised against the source-bound ROS 2 maintenance
candidate (Fast DDS 2.14.7 / Fast CDR 2.2.8, interface schema 21405). The final
candidate receipt reports 31/31 offline fixture/unit contracts. Separately, the
actual installed-prefix positive and materialized negative CLI cases, plus compiler
ABI/import guards, are recorded in the initial installed-prefix execution evidence;
the final source hashes confirm the relevant implementation remained unchanged. They
are not counted as the 31 fixture cases and are not device checks. The checker remains
read-only and cannot turn a candidate or a `production` request into production
eligibility; target validation remains static-only.

Python 3.11+ and an explicitly supplied LLVM `llvm-readobj` are required. The tool
is installed beside `install-manifest.json` under `share/<package>/` for each existing
3.6, ROS 2 Host and ROS 2 OHOS package. Run the installed copy outside the checkout.
The manifest records the actual configured dependency version, selected imported
artifact/configuration, binary SHA-256 and Windows import-library hash. It includes
no source/build path, board identity, token, grant, private artifact or old log.

Supply all legally obtained dependency DLL/DSO and import-library directories,
platform library closure, STL/CRT and system runtime files as explicit inputs.
The same configured versions must be provided; otherwise rebuild against the new
dependency set through the separate maintenance process. Nothing is downloaded or
silently borrowed from PATH, CMAKE_PREFIX_PATH, registry, SDK discovery or a working tree.

```powershell
python "$prefix/share/FastDDSDSoftBusTransport/install_preflight.py" `
  --prefix "$prefix" --config "$externalConfig" --readobj "$llvmReadobj" `
  --runtime-dir "$dependencyBin" --runtime-dir "$dependencyLib" `
  --runtime-dir "$explicitSystemRuntime"
```

For the ROS 2 OHOS installation use
`share/FastDDSDSoftBusTransportRos2Target/install_preflight.py`; for Host use
`share/FastDDSDSoftBusTransportRos2Host/install_preflight.py`. Paths are selected
by the caller and can move. The tool reads only files and invokes readobj for
static PE/ELF inspection. It never dlopens/executes a target library, calls HDC,
SDK or a grant helper, changes policy, or writes an output file. Capture stdout
externally if a receipt is needed.

## Inputs and result contract

Start from the installed `install_preflight.json` template. Its placeholders must
be replaced with external legal inputs; it intentionally cannot pass unchanged.
`capability` is `host`, `experimental` or `production`; `profile` accepts only
`BYTES_NATIVE` for the first public experimental baseline. RAW/unknown profiles
are rejected and their product requirements remain open. Platform system,
architecture, sdk_api and stl must match the installed manifest. Other firmware
tuples receive `PLATFORM_NOT_OBSERVED`; no tuple earns production eligibility.
For Windows Host set SDK API `N/A` and STL `MSVC_MD`; native identity is not required
for Host. Host traffic does not prove DSoftBus communication.

Public scalar inputs are namespace, domain, service_slot and initial_peer_slots.
Native selection additionally requires explicit Socket and identity C/POD libraries,
owned process/token/permission declarations and package-scoped service-name prefix.
Current token-first integration expects `fastdds_dsoftbus_probe`, normal APL and
`ohos.permission.DISTRIBUTED_DATASYNC`. These are declared prerequisites; the checker
does not determine actual grant, token ownership, pairing, permission restoration,
system ability/bundle policy or loaded module source/namespace.

ROS packages also require `runtime_config` pointing to the actual `.conf` input and
`module_files` mapping bridge_library/fastdds_module/fastcdr_module (plus native
softbus_module) to explicitly supplied local binaries. Module basenames must match
the actual runtime configuration, bridge must belong to the installation, dependencies
must match build hashes, and Socket module must match the explicit native input.
This mapping lets the offline checker inspect local copies while the `.conf` retains
device absolute paths. No private executor configuration/receipts are imported.
The first checker validates the ordinary startup field schema taken from Bridge.cpp
and TargetBridge.cpp; bounded harness/reaccept/route-control extension fields are
explicitly unsupported here, not silently passed through or requalified. Core 3.6
descriptor applications use the public JSON inputs rather than the ROS `.conf` schema.

| Status | Meaning |
| --- | --- |
| INSTALL_INPUTS_VALID | Installed files, exact bound dependency inputs, binary architecture/format/import closure and public scalar configuration pass static checks |
| RUNTIME_PREREQUISITE_MISSING | Missing/ambiguous library, wrong binary/hash/ABI, malformed/incomplete configuration or identity declaration |
| UNSUPPORTED_PROFILE | Unknown/unsupported profile or capability/component selection |
| PLATFORM_CONTRACT_UNVERIFIED | Missing supplier native lifetime/epoch/drain/source/platform contract and production qualification basis |
| EXPERIMENTAL_ONLY | No production permission or qualification is granted by this tool |

Statuses are cumulative. Valid experimental/Host inputs return exit 0 together
with EXPERIMENTAL_ONLY and PLATFORM_CONTRACT_UNVERIFIED. Invalid inputs or a production
request return exit 2. `production_ready` is always false, P4 is NOT_RELEASED and
QualificationProfile is NOT_GENERATED. Unknown keys, including production/qualification
flags, fail closed; editing a local manifest does not grant eligibility.

The audit recursively inspects explicit PE imports and ELF DT_NEEDED without using
loader fallback. Windows API-set imports are reported as unverified OS contracts
as described by [Microsoft's loader contract](https://learn.microsoft.com/en-us/windows/win32/apiindex/api-set-loader-operation);
they are not ordinary redistributable DLLs. File/hash and architecture checks cannot
prove complete C++/CRT ABI, symbol versions, loader namespace resolution, live device
policy or safety. These require target-specific execution and supplier contracts.

PE delay-load imports are listed separately as DELAY_LOAD_FEATURE_UNVERIFIED;
they are demand-loaded feature dependencies, not mandatory process-start imports.
Their names are checked/reported but their feature reachability and availability
cannot be qualified by this offline tool. A missing mandatory import remains an
error. INSTALL_INPUTS_VALID does not cover enabling an unverified delay-loaded feature.

Tests include relocated prefix, missing/transitive libraries, wrong artifact/format/ABI,
unsupported profile, missing identity, forged qualification and missing platform
contract. Test fixtures use synthetic captures; real static binary/installed CLI
execution is recorded independently. Neither constitutes a new device communication run.
