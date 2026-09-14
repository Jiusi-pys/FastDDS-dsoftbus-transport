# N2 completion record

Date: 2026-09-14 UTC. This record synchronizes the repository with the final
N2 evidence. It does not authorize a commit.

## Status and boundary

N2 is `IMPLEMENTED / BUILDS / HOST_TESTED / SDK23 ABI LINK/ELF TESTED /
REVIEWED` for an SDK-free injected synchronous egress model. The model reuses
N1 ownership, `RuntimeSendPath`, `OwnedSendRecord`, and the existing sender
queue. It does not implement or claim a native DSoftBus provider, Socket
operation, SDK `Send`, DDS/RTPS integration, RMW, or device transport.

The N1 prerequisite is committed at HEAD `2c28e9325b941a2b8c0f198b0ddd82f90c4b073e`,
tree `8147a261e437cb3a62b59ccd7c9a3f456be93bd5`. N2 is six working-tree files
and remains uncommitted in this pre-commit evidence snapshot. The user has
authorized a standalone N2 model commit; this authorization does not imply
whole-Transport verification. Root `CMakeLists.txt` intentionally does not
expose N2; the isolated entry below is an ignored execution artifact.

## Frozen six-file identity

The final closure2 `certutil` receipt matches the initial tester and review:

| File | SHA-256 |
| --- | --- |
| `include/mdds/dsoftbus/NativeSendExecution.hpp` | `DA321FADC8D23D501B8300CA646C298A1A44E12EF1831DCE761709688FCD706C` |
| `src/NativeSendExecution.cpp` | `13FF4F94DEF10E74391FF40142C407BD89F86F8E6E56FD027D1A10F572EB5B54` |
| `src/NativeSendExecutionInternal.hpp` | `C627954F89C2C1E54B826B345F05D7E0C20FC2BD9F0A38ADF21AF8D8AF088397` |
| `tests/native_send_execution_tests.cpp` | `F0F628DE71AF705E8C909854BB97C438C19BA670747B9B096387C2F4358E5F54` |
| `include/mdds/dsoftbus/NativeOwnerTable.hpp` | `2E58F57537B487C95039D94C64FFDC9F538AFBDA8967E91033DC8409A4BEAC6D` |
| `src/NativeOwnerTable.cpp` | `ACCCC6AD856D3B35D2AB4016182125A5EDAA70828113F54FC84CD6CEC4C4AB5C` |

Canonical final tester: `.artifacts/n2-execution/final-tester/RESULT.md`,
SHA-256 `3AC3B952B424D3EDFF1A16D584463ABA89F369D9E0A37527316B32D3955612F7`.
Review: `.artifacts/n2-execution/code-review/REVIEW.md`, SHA-256
`227E9479AC4CA318B1B89BD51AE66D554ED47F69819DE2B13E1CAB8CA6D660D7`, no
actionable findings. Initial receipt is
`.artifacts/n2-execution/initial-tester/RESULT.md`.

## Contract recorded by implementation/review

- One original `NativeState` owns whole-runtime and per-owner ceilings,
  including externally retained senders; no second queue or worker exists.
- Private registration/Attach/Take boundaries prevent public runtime getters,
  raw completion fabrication, or caller-created pairing.
- Revoke, taken, admitted, and provider-entered windows remain distinct;
  admission checks identity/configuration/generation/deadline and does not call
  provider code under the authoritative mutex.
- Settlement happens after the actual synchronous return or unwind. Positive,
  zero/negative, and exception outcomes preserve observed facts; no fabricated
  SDK error or cancellation claim is made.
- Close/revoke and construction rollback preserve ownership, capacity, and
  shutdown ordering. The documented unsupported cases are concurrent use of
  one Invocation/move/destruction and provider borrow escape.

These are model invariants, not product throughput, RSS, latency, or SDK
capacity guarantees.

## Final execution evidence

| Scope | Result | Evidence |
| --- | --- | --- |
| SOURCE Debug/Release full build + CTest | PASS, 24/24 each | `.artifacts/tdd/native-owner/n2final-source-debug*.log`, `n2final-release*.log` |
| CONFIG facade Release build + CTest | PASS, 25/25 | `n2final-config*.log` |
| N2 repeat | PASS, 20 runs × 10 groups | `n2final-repeat20.log` |
| Configuration regressions | PASS: N1 9/9, attachments 7/7, binding 32/32, facade 4/4 | `.artifacts/tdd/native-owner/n2final-closure2-*.log` |
| SDK23 APP arm64 link/readelf/hash | PASS; Clang 15.0.4; ELF AArch64; executable SHA `677EA367BC12EBB6D1B77F13A3C1A33AD214B51D24A65C16FB715C6EB00A8763` | `n2final-sdk23-{configure,build,readelf,hash}.log` |

Host CTest used process-local Machine `PSModulePath`; the CONFIG run used an
absolute host DLL `PATH`. No global environment or Git configuration change
is evidence. SDK root is
`C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native`, `OHOS_ARCH=arm64-v8a`.
Host dependencies use `host/lib/cmake/fastcdr` and
`host/share/foonathan_memory/cmake`; SDK dependencies use
`ohos-app/lib/cmake/fastcdr` and `ohos-app/lib/foonathan_memory/cmake`.

## Reproducible isolated entry

Location: `.artifacts/n2-execution/build/CMakeLists.txt` (ignored; do not add a
new product source file). Its exact contents are:

```cmake
cmake_minimum_required(VERSION 3.22)
project(n2_isolated LANGUAGES C CXX)
include(CTest)
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_subdirectory("${REPO}" core)
add_library(n2_execution STATIC "${REPO}/src/NativeSendExecution.cpp")
target_link_libraries(n2_execution PUBLIC mdds_dsoftbus_native_owner mdds_dsoftbus_runtime_send_path)
target_compile_features(n2_execution PUBLIC cxx_std_17)
add_executable(native_send_execution_tests "${REPO}/tests/native_send_execution_tests.cpp")
target_link_libraries(native_send_execution_tests PRIVATE n2_execution)
add_test(NAME dsoftbus.native_send_execution COMMAND native_send_execution_tests)
set_tests_properties(dsoftbus.native_send_execution PROPERTIES TIMEOUT 30)
```

SOURCE Debug/Release uses Visual Studio 17 2022 x64 with `-S
.artifacts/n2-execution/build`; CONFIG uses `MDDS_FASTDDS_MODE=CONFIG`, host
prefix `.artifacts/install/host`, `fastdds_DIR=<prefix>/share/fastdds/cmake`,
`fastcdr_DIR=<prefix>/lib/cmake/fastcdr`, and
`foonathan_memory_DIR=<prefix>/share/foonathan_memory/cmake`. SDK uses Ninja,
the SDK toolchain, `MDDS_FASTDDS_MODE=CONFIG`, prefix
`.artifacts/install/ohos-app`, and `foonathan_memory_DIR=<prefix>/lib/foonathan_memory/cmake`.
The exact command receipts are the `n2final-closure2-*` and `n2final-sdk23-*`
logs above.

Representative reproducible PowerShell commands (all use `BUILD_TESTING=ON`):

Run from the repository root in a fresh PowerShell process. Materialize the
isolated CMake file above first. The host/OHOS package prefixes are external
prerequisites, not generated by these commands; see [N1_COMPLETION.md](N1_COMPLETION.md)
for their recorded preparation and hashes. Stop on any nonzero native command
exit code; use fresh build directories instead of inheriting unrelated caches.

```powershell
$n2Repo=(Get-Location).Path
& 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64
Set-Location $n2Repo
$b='.artifacts/n2-execution/final-tester/build-debug'
cmake -S .artifacts/n2-execution/build -B $b -G 'Visual Studio 17 2022' -A x64 -DBUILD_TESTING=ON -DMDDS_FASTDDS_MODE=SOURCE -DCMAKE_INSTALL_PREFIX=.artifacts/install/host
cmake --build $b --config Debug --parallel 4
powershell -NoProfile -Command "`$env:PSModulePath=[Environment]::GetEnvironmentVariable('PSModulePath','Machine'); ctest --test-dir '$b' -C Debug --output-on-failure"
$b='.artifacts/n2-execution/final-tester/build-release'
cmake -S .artifacts/n2-execution/build -B $b -G 'Visual Studio 17 2022' -A x64 -DBUILD_TESTING=ON -DMDDS_FASTDDS_MODE=SOURCE -DCMAKE_CONFIGURATION_TYPES='Release'
cmake --build $b --config Release --parallel 4
powershell -NoProfile -Command "`$env:PSModulePath=[Environment]::GetEnvironmentVariable('PSModulePath','Machine'); ctest --test-dir '$b' -C Release --output-on-failure"
$prefix=(Resolve-Path '.artifacts/install/host').Path
$b='.artifacts/n2-execution/final-tester/build-config'
cmake -S .artifacts/n2-execution/build -B $b -G 'Visual Studio 17 2022' -A x64 -DBUILD_TESTING=ON -DMDDS_FASTDDS_MODE=CONFIG -DCMAKE_CONFIGURATION_TYPES=Release "-Dfastdds_DIR=$prefix/share/fastdds/cmake" "-Dfastcdr_DIR=$prefix/lib/cmake/fastcdr" "-Dfoonathan_memory_DIR=$prefix/share/foonathan_memory/cmake" "-DCMAKE_PREFIX_PATH=$prefix" -DBUILD_DSOFTBUS_TRANSPORT_FACADE=ON
cmake --build $b --config Release --parallel 4
powershell -NoProfile -Command "`$env:PSModulePath=[Environment]::GetEnvironmentVariable('PSModulePath','Machine'); `$env:Path='$prefix/bin;'+`$env:Path; ctest --test-dir '$b' -C Release --output-on-failure; exit `$LASTEXITCODE"
$sdk='C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native'; $prefix=(Resolve-Path '.artifacts/install/ohos-app').Path; $b='.artifacts/n2-execution/final-tester/build-sdk-final'
cmake -S .artifacts/n2-execution/build -B $b -G Ninja "-DCMAKE_MAKE_PROGRAM=$sdk/build-tools/cmake/bin/ninja.exe" "-DCMAKE_TOOLCHAIN_FILE=$sdk/build/cmake/ohos.toolchain.cmake" -DOHOS_ARCH=arm64-v8a -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DMDDS_FASTDDS_MODE=CONFIG "-Dfastdds_DIR=$prefix/share/fastdds/cmake" "-Dfastcdr_DIR=$prefix/lib/cmake/fastcdr" "-Dfoonathan_memory_DIR=$prefix/lib/foonathan_memory/cmake" "-DCMAKE_PREFIX_PATH=$prefix"
cmake --build $b --target native_send_execution_tests native_owner_table_tests --parallel 4
& "$sdk/llvm/bin/llvm-readelf.exe" -h -d "$b/native_send_execution_tests"
```

TDD evidence is retained at `.artifacts/tdd/native-owner/n2-red-run.log`,
`n2-red-full-run.log`, `n2-controller-red.log`, and `n2-expanded-run.log`.
The reviewed candidate graph is `.artifacts/n2-execution/graph-initial/candidate.json`:
exactly six files and 204 symbols, with no `partial` or `truncated` marker;
candidate tree is `3b643e3887ca4f74278b81aec460cbcec46046d9`.

## Not run / remaining gates

Native Socket/API operation, actual SDK Send, callbacks/receiver integration,
DDS/RTPS/SPDP/SEDP, RMW, deployment, RK3588A execution, cross-device
transport, and same-name Participant hot rebuild are **NOT RUN**. Device is
N/A only for this SDK-free model slice, never a device PASS. The first-version
same-name hot-rebuild requirement remains mandatory; 64 model cycles do not
substitute for it. Earlier `n2final-source-hashes-before/after.log` files are
invalid diagnostics (missing `Get-FileHash`) and remain historical, excluded
from evidence. The scoped N2 model gate is PASS; whole Transport remains
`NOT VERIFIED`. The user has authorized the standalone N2 model commit; this
record remains a pre-commit evidence snapshot and does not imply whole-
Transport verification or provide a commit SHA.
