# N1 completion record

N1 is complete within its **SDK-free NativeOwnerTable model and public build
target** scope. The accompanying pre-N2 source/probe baseline is preserved with
component-specific review evidence; this does not certify every historical
probe as a newly verified feature. The production facade still reports
`NativeBackendUnavailable`. Native communication, DDS discovery/samples, RMW,
and same-process same-name Participant hot rebuilding are not complete.

## Frozen evidence and scope

- Baseline HEAD: `1636043cbcc4b871d8969f9ed122b11f1d007885`.
- Reviewed/tested candidate tree: `f66b109dcc8aecee912d7a25e2d3ad527d3c316f`.
- Candidate manifest: 87 paths, SHA-256
  `274813FDCA6353440DE86FCF9FC1B25D929F4C22953082BE518E5751F4DFBABF`.
- Fast DDS: `343f155c36b561db3d9f047a65d866ce0f3da300`, version 3.6.2.0.
- DSoftBus: `dd6037ce496828133bb9af89b37e2d08232a10d9`.
- Final candidate tar SHA-256:
  `B34D928A0E48EB0BBB62980BBF94E504C60C0A0E3B3ADAD78A073FA0E3AEAB6E`.

The candidate was reconstructed from Git archives and pristine pinned upstream
trees, not a copy of dirty worktree sources. All 87 files matched literal
candidate bytes before and after final testing. Only documentation changed
after those tests; the 83 non-document files are pinned below.

The initial export was CRLF-expanded by the machine's `core.autocrlf=true`;
working files and Git blobs were LF. Its normalized text checks passed, but it
was not byte-identical. Final exports used command-local
`git -c core.autocrlf=false -c core.eol=lf archive --output=<tar> <tree>`
and binary extraction. No global Git setting changed. Archive hashes describe
the recorded exports, not a guarantee that later archives have identical
metadata or timestamps.

The candidate tree is a local review identity and may not exist in a fresh
clone. Reproduce using the committed checkout and its submodule gitlinks.
The five maintained documentation files are not runtime inputs.

## Independent verification

Canonical final test report:
`.artifacts/verification/n1-completion-final-20260914T1455Z/RESULT.md`,
SHA-256 `6D02D99C2724C378865EB56377B6017BEEAA30192FBE545DD96E3B187637167A`.

Independent closure review:
`.artifacts/review/n1-completion/CODE_REVIEW.md`,
SHA-256 `0C9C393AF1942AF5B0B643372AE10B66733421B51A19FDEF6A3B828F9DD8507D`.
No source blocker was found. The review mapped all 81 local component/build/test
files plus root CMake and ignore configuration to exact-hash prior reviews or
direct gap inspection. The review map SHA-256 is
`24627092BC719CDBC0DA70E63870385CB4CE33E1EDBD88A79E9DEC089AC2B4E9`.

| Final execution, 2026-09-14 14:58:29–15:06:39 UTC | Result |
| --- | --- |
| SOURCE_PROBE Debug build / CTest | PASS, 23/23 |
| SOURCE_PROBE Release build / CTest | PASS, 23/23 |
| Actual CONFIG Release facade build / CTest | PASS, 24/24 |
| N1 options and actual SOURCE/CONFIG consumers | PASS, 9/9 |
| Attachment options / independent CONFIG consumer | PASS, 7/7 and 1/1 |
| Fast DDS binding / facade option suites | PASS, 32/32 and 4/4 |
| LC harness / runner / lifecycle binding | PASS, 80/80, 42/42, 169/169 |
| Record pair / record binding / v1 compatibility | PASS; 130/130; 16/16 |
| N1 model repeat | PASS, 20 process runs, 9/9 groups each |
| SDK23 public N1 target compile/link/readelf | PASS, ELF64 LE AArch64 PIE |
| Exact source identity before/after | PASS, 87/87 |

All canonical commands returned 0. SDK output SHA-256:
`60E9479900E37629D7AEEF14B01ABC1A0CBCAFF5D4BE199C972F103428C94168`.
NEEDED libraries were `libfastdds.so.3.6`, `libfastcdr.so.2`,
`libfoonathan_memory-0.7.4.so`, `libc++_shared.so`, and `libc.so`.
This is cross-build/link evidence, not board execution or a valid board RUNPATH.

The pre-documentation graph gate was reconciled with 18 disjoint temporary-index
batches: all 87 paths, 3,221 distinct indexed symbols, no partial/truncated batch.
The canonical index stayed unchanged. The all-scope report also identified two
excluded dirty paths. Sixteen CMake/configuration/export/PowerShell paths had
no mapped symbols; source inspection and real configuration/consumer tests
cover that bounded gap. Zero indexed affected processes is not proof of zero
runtime risk. Main refreshes and reconciles the final documentation tree before
commit; the above counts belong to the reviewed candidate, not later docs.

Raw logs (`.artifacts/tdd/native-owner/n1final-*.log`), old device observations,
exports and detailed review receipts are local, ignored evidence and are not
shipped. The identities, method, results and source inventory here remain
available in a fresh checkout. Historical permission/record/lifecycle probes
retain their own operational restrictions; their ignored deployment recipes
are not claimed self-contained or newly device-verified.

## Declared external inputs

These are prerequisites, not dependencies rebuilt by this N1 snapshot:

- Windows, Visual Studio 2022 Build Tools, MSVC 19.44.35228, CMake 4.4.3,
  Node.js and Ninja available to the build process.
- Host Fast DDS/Fast CDR/foonathan CONFIG packages:
  `.artifacts/install/host` in the original workspace.
- SDK23 native 6.1.0.32, Clang 15.0.4:
  `C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native`.
- APP-ABI OHOS CONFIG packages: `.artifacts/install/ohos-app`.
- Use a short checkout/build path. The initial deep export exceeded MSVC/CMake
  object path limits. Moving to a fresh short export resolved that setup failure.
- Start a VS developer PowerShell. CONFIG is Release-only; do not silently map
  Debug to the Release imported package. The host Fast CDR config is under
  `lib/cmake/fastcdr`, not `share/fastcdr/cmake`.

The original root for relative paths below is
`C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport`.
SDK-relative rows use the SDK native root above. Main independently re-hashed
these external files and matched all eight tester-pinned compiler/toolchain/
library hashes; two CONFIG entry points were additionally recorded.

| External file | SHA-256 |
| --- | --- |
| `SDK/llvm/bin/clang++.exe` | `7E54A846E4299AFD89F4048D05D594C8DB0CB4A050DDAD681E90DF669FC405EC` |
| `SDK/build/cmake/ohos.toolchain.cmake` | `0CE9943DF04C192725B41CD70DBB259EDD21FA9FA8D78DEEE0D6254F4D6FEB18` |
| `.artifacts/install/host/share/fastdds/cmake/fastdds-config.cmake` | `9EF5E310C78912ABB0B0E6FBF82FA2C0235CE485B7285BBD7F6E9F7519F40E3E` |
| `.artifacts/install/ohos-app/share/fastdds/cmake/fastdds-config.cmake` | `9EF5E310C78912ABB0B0E6FBF82FA2C0235CE485B7285BBD7F6E9F7519F40E3E` |
| `.artifacts/install/host/bin/fastdds-3.6.dll` | `AE1B520769CD1EF988871A4C485D1730D4B2EC6D1A79A36E6157753DB212BAE6` |
| `.artifacts/install/host/bin/fastcdr-2.3.dll` | `416571B0376585AEA7E09ACA6FDE3D75F55FDBAF6F0AEE484783EDED781E3344` |
| `.artifacts/install/host/bin/foonathan_memory-0.7.4.dll` | `0654DEC8DC29B91D5D063A9953D55107178A97DD5788A946ED20431F99ACE656` |
| `.artifacts/install/ohos-app/lib/libfastdds.so.3.6.2.0` | `DE48D43011E4E9F2ABD86370AF947BEE6D31E013DD1BF587D9EDFD38DCA9201D` |
| `.artifacts/install/ohos-app/lib/libfastcdr.so.2.3.5` | `A535CE90E019A05D88322A6E6B8AD007ED700C8ED0B6AD1839FF0381734F7AF0` |
| `.artifacts/install/ohos-app/lib/libfoonathan_memory-0.7.4.so` | `4DA6AC0DB3C9084E9A31B244C9ECE636C47C92513ABD878525798637DBDE1E4C` |

The inherited committed `.codex/agents/tester.toml` contains an invalid
unescaped Windows path in a TOML basic string. Its worktree correction is
excluded from this baseline alongside personal/IDE/tool changes. Consequently,
fresh-checkout repository-local Codex tester orchestration is **not** claimed
working; independent agent orchestration was an external validation input.
This does not affect the CMake/Node commands below. No device permission or
global configuration was changed for N1 completion.

## Reproduce the maintained N1 matrix

Initialize the two pinned submodules in a fresh checkout. Provide the external
packages above; change the prefix variables if they are installed elsewhere.
Run in a new PowerShell process so environment changes remain process-local.
Every native command must succeed before continuing.

```powershell
$repoRoot = (Resolve-Path '.').Path
& 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64
Set-Location $repoRoot
$hostPrefix = (Resolve-Path '.artifacts/install/host').Path
$ohosPrefix = (Resolve-Path '.artifacts/install/ohos-app').Path
$sdkPrefix = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native'
$env:PSModulePath = [Environment]::GetEnvironmentVariable('PSModulePath', 'Machine')
$env:Path = "$hostPrefix/bin;$env:Path"
$hostArgs = @(
  "-Dfastdds_DIR=$hostPrefix/share/fastdds/cmake",
  "-Dfastcdr_DIR=$hostPrefix/lib/cmake/fastcdr",
  "-Dfoonathan_memory_DIR=$hostPrefix/share/foonathan_memory/cmake",
  "-DCMAKE_PREFIX_PATH=$hostPrefix"
)

cmake -S . -B .artifacts/n1-debug -G 'Visual Studio 17 2022' -A x64 -DMDDS_FASTDDS_MODE=SOURCE_PROBE -DBUILD_TESTING=ON
cmake --build .artifacts/n1-debug --config Debug -j 4
ctest --test-dir .artifacts/n1-debug -C Debug --output-on-failure

cmake -S . -B .artifacts/n1-release -G 'Visual Studio 17 2022' -A x64 -DMDDS_FASTDDS_MODE=SOURCE_PROBE -DBUILD_TESTING=ON
cmake --build .artifacts/n1-release --config Release -j 4
ctest --test-dir .artifacts/n1-release -C Release --output-on-failure

cmake -S . -B .artifacts/n1-config -G 'Visual Studio 17 2022' -A x64 -DCMAKE_CONFIGURATION_TYPES=Release -DMDDS_FASTDDS_MODE=CONFIG -DBUILD_TESTING=ON -DBUILD_DSOFTBUS_TRANSPORT_FACADE=ON @hostArgs
cmake --build .artifacts/n1-config --config Release -j 4
ctest --test-dir .artifacts/n1-config -C Release --output-on-failure

node tests/native_owner_build_tests.cjs "$hostPrefix"
node tests/application_attachments_build_tests.cjs "$hostPrefix"
node tests/fastdds_binding_configuration_tests.cjs "$hostPrefix"
node tests/transport_build_configuration_tests.cjs "$hostPrefix"

cmake -S tests/cmake/application_attachments_consumer -B .artifacts/n1-consumer -G 'Visual Studio 17 2022' -A x64 -DCMAKE_CONFIGURATION_TYPES=Release @hostArgs
cmake --build .artifacts/n1-consumer --config Release -j 4
ctest --test-dir .artifacts/n1-consumer -C Release --output-on-failure

node tests/lifecycle_probe_harness_tests.cjs
node tests/lifecycle_probe_runner_tests.cjs
node tests/lifecycle_run_binding_tests.cjs
node tests/record_pair_harness_tests.cjs
node tests/record_run_binding_tests.cjs
node tests/record_v1_compatibility_tests.cjs
for ($iteration = 0; $iteration -lt 20; $iteration++) {
  & .artifacts/n1-debug/Debug/native_owner_table_tests.exe
  if ($LASTEXITCODE -ne 0) { throw 'N1 repetition failed' }
}
```

These fresh SOURCE/CONFIG roots enable the public
`mdds_dsoftbus_native_owner` STATIC target by default. Its
`BUILD_DSOFTBUS_NATIVE_OWNER` option follows `BUILD_DSOFTBUS_DIRECTORY`
only when first creating a cache. Model executable/CTest registration requires
`BUILD_TESTING=ON`. The option suites exercise disabled, library-only,
tools-only, invalid-dependency and real public-consumer configurations.

Build only the tested N1 target with SDK23; do not run an ARM executable on
Windows or interpret this command as deployment:

```powershell
$ohosArgs = @(
  "-DCMAKE_TOOLCHAIN_FILE=$sdkPrefix/build/cmake/ohos.toolchain.cmake",
  "-DCMAKE_MAKE_PROGRAM=$sdkPrefix/build-tools/cmake/bin/ninja.exe",
  "-Dfastdds_DIR=$ohosPrefix/share/fastdds/cmake",
  "-Dfastcdr_DIR=$ohosPrefix/lib/cmake/fastcdr",
  "-Dfoonathan_memory_DIR=$ohosPrefix/lib/foonathan_memory/cmake",
  '-DOHOS_ARCH=arm64-v8a', '-DCMAKE_BUILD_TYPE=Release',
  '-DMDDS_FASTDDS_MODE=CONFIG', '-DBUILD_TESTING=ON',
  '-DBUILD_DSOFTBUS_DIRECTORY=ON', '-DBUILD_DSOFTBUS_NATIVE_OWNER=ON',
  '-DBUILD_INTERFACE_PROBE=OFF', '-DBUILD_SOFTBUS_CAPABILITY_PROBE=OFF',
  '-DBUILD_NATIVE_PERMISSION_PROBE=OFF', '-DBUILD_DSOFTBUS_SEND_QUEUE=OFF',
  '-DBUILD_DSOFTBUS_INPUT_CHANNELS=OFF', '-DBUILD_DSOFTBUS_APPLICATION_ATTACHMENTS=OFF',
  '-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=OFF', '-DBUILD_DSOFTBUS_TRANSPORT_FACADE=OFF'
)
cmake -S . -B .artifacts/n1-sdk23 -G Ninja @ohosArgs
cmake --build .artifacts/n1-sdk23 --target native_owner_table_tests -j 4
& "$sdkPrefix/llvm/bin/llvm-readelf.exe" -h -d .artifacts/n1-sdk23/native_owner_table_tests
certutil -hashfile .artifacts/n1-sdk23/native_owner_table_tests SHA256
```

The Machine module path is required for the recorded CTest environment, but
the outer narrowed shell cannot necessarily load `Get-FileHash`; use the
explicit successful hash command rather than treating a wrapper's exit 0 as
proof of all its commands. Initial path/module/configuration setup failures
are retained, not counted as passing source behavior tests.

## Remaining boundaries and commit gate

Independent final N1 implementation/execution gate: **PASS**. Documentation
and Main's final index/graph/Git checks are separate required pre-commit steps.
Only the exact N1 baseline commit opens the user-requested N2 sequencing gate;
N2 implementation is not part of this record.

Device verification is **N/A for SDK-free N1**, not a Transport device PASS.
The 64 owner/application model cycles are not Participant rebuilds. The first
version must still pass actual same-process same-name Participant teardown and
recreation without renaming or restarting the process. SDK callback quiescence,
native send/receive, SPDP, SEDP, samples and RMW remain unverified; overall
Transport readiness is **NOT VERIFIED**.

## Exact non-document source/configuration inventory

SHA-256 values below are LF Git blob bytes. These 83 paths were unchanged
through final testing and documentation synchronization; 81 are local component/
build/test files, plus root CMake and ignore configuration. A review receipt
for a historical probe is not new runtime or device validation.

| Path | SHA-256 |
| --- | --- |
| `.gitignore` | `61D9DA4C375EA86A3292BE1C26FA71B8ABC9F6AD246D7EA6203D479B95B0D9C2` |
| `cmake/FastddsBinding.cmake` | `A4CF813B08A09F651E462DE8B12F089A79332A20A5FFD2B5646E606FA307E4DA` |
| `cmake/ProbeFastddsConfig.cmake` | `623CB807B971CC9DC67FE56586DC922A7F8AE9E05EFA4D1685387B673A445F8C` |
| `CMakeLists.txt` | `B1D712850A2B40EE20D02FE92F7E128675D4879744A5B7399622A2BA7E88E408` |
| `include/mdds/dsoftbus/ApplicationAttachments.hpp` | `57DEE23AC5DDC4093A1380ABD1830B1BC3DE3CAEB9D4F3C064AD6DB0A0BEB5E2` |
| `include/mdds/dsoftbus/DSoftBusSenderResource.hpp` | `8591094C6F75310256396B74EDB8A687D727C86A837F3B575D9268284107C55B` |
| `include/mdds/dsoftbus/DSoftBusTransport.hpp` | `89C3143EF77C76F7D5B93D39CE766B571C1E57A0DB0C11598F6A36249B714C94` |
| `include/mdds/dsoftbus/DSoftBusTransportDescriptor.hpp` | `A86AF98C95700445B82D40CE95282B5AA7A8FAA46FF6D830A3698378ABF31DF6` |
| `include/mdds/dsoftbus/EndpointDirectory.hpp` | `6BE1F788FD99074BC5AA6FA2BE2B02CCFC6B22B8BBB99DAFEF1E7B10AB3CCD00` |
| `include/mdds/dsoftbus/InputChannels.hpp` | `74B797EED9F3F23CEBC70F8EE10F64A00AF2A36BA8BFCA04BC46658F86EF50F6` |
| `include/mdds/dsoftbus/NativeOwnerTable.hpp` | `859E5CCBD6DF38BEB0FCF5910F8E6E0FF26BCF430A843E8305AE0D38298571DC` |
| `include/mdds/dsoftbus/RuntimeSendPath.hpp` | `2B16ACD0B1321B4EB6DF3BD9338FA91B36EFF40EA1B650375D6C0235BD6B1C78` |
| `include/mdds/dsoftbus/SendQueue.hpp` | `449967D484A09AB173C04FCB538CD8F1924E66A5049D0B786C210D42436B7AB4` |
| `probes/fastdds_interface_probe.cpp` | `0721817989BE532738414F62AB3F3A11DAE170EF83192AC8C1EE8DC4CEBFF8CE` |
| `probes/fastdds_interface_probe.hpp` | `F8317D59C5593C840FA5B41FE280C97C4DA01AEE77A3595C27A0DBAA155AAFF6` |
| `probes/native_permission_flow.cpp` | `76ADDBA0D3FB41509BACE6C5283D238F716B58CB39F3675DF38C48E56D713B11` |
| `probes/native_permission_flow.hpp` | `29E06171A238D52D9B22DC8AD1279BFD08BA92E2FBDC959EACE6937C438C5AFF` |
| `probes/native_permission_probe.cpp` | `63AF21736D2E347525A5D324E13C58DF78FBE8CE253256416EA70BC10361399F` |
| `probes/softbus_capability_probe.cpp` | `2BB981ACD6452CBDB2EA7DD8F0C81AB554BB284B2D69D57AF2AA397D53A0D73B` |
| `probes/softbus_lifecycle_contract.cpp` | `C705C79D78570EBD5C8EEC97DFF3AF8A8F39880BB2382926A4933F6E42F8F4D0` |
| `probes/softbus_lifecycle_contract.hpp` | `EAC76DDF7B5437A16728C8A26608327D76D90C80B7BCE6C85125EDB627BFF992` |
| `probes/softbus_lifecycle_probe.cpp` | `918F716A92300606EF7362022ABB3F52E469EDE35843446FF6E452EEA29B8F1E` |
| `probes/softbus_record_contract.cpp` | `2A1BB8C3F5AFD5C588724C5584B6C6D086A0DA866595B3D9B7E135BB572D08DF` |
| `probes/softbus_record_contract.hpp` | `3402F27516B6901655675F8CDF896ECE9FBFBD2CB4BDC104DE435D484CF7A857` |
| `probes/softbus_record_probe.cpp` | `536E3128B5FE34B1F8264BC5E2DC949B45EAD1CAF71A83D0419C32511A5B51A5` |
| `probes/token_first_entry.cpp` | `6BE90A5AFEA8F672E0E4D123E153278B954AAC777FC6101684590809A2B2C2F0` |
| `probes/token_first_entry.hpp` | `227EF2E135ABE697029581BE1E8F326DFCFB1148473259C1EE7DB608F47967E4` |
| `scripts/LifecycleProbeHarness.cjs` | `3D8CD9F93151929DB2E27B24E97525D07D066A4EBECF0AE81152E7743C40024A` |
| `scripts/LifecycleProbeRunner.cjs` | `E36C5FAF1ECD8B7BCC80F30F47381CBC51B4193B5ED9231CA5A4F77BD565B740` |
| `scripts/LifecycleRunBinding.cjs` | `1CD42044B14DE303EFF5394D67AE27E054F5ED5363F5B73A1DA9C45A6BB26692` |
| `scripts/NativePermissionGuard.ps1` | `58AE70D5E7842F0600E39ED33A612A2DFE409F7DEF41A259E134C2F5614E6AAD` |
| `scripts/RecordPairHarness.cjs` | `7A752F1B3F3AF1D0571B22228B4B5F2BFB3BED58B3EC60C9F97932A36A44E072` |
| `scripts/RecordRunBinding.cjs` | `5CE2B14FF78ACED4DDCA3B0A85045FCBE90A150B7A9968ACD659AC2D52511A94` |
| `scripts/RunBindingCore.cjs` | `026FFD3C393FBF837E9C1E1252E5E6279309F5DD1E91912CCF61752DC09F1548` |
| `src/ApplicationAttachments.cpp` | `6507240D7449C1C619FD939960B7DC7F89D51596C30E01F6A2E69661003E33B5` |
| `src/DSoftBusSenderResource.cpp` | `3E3F3C18A05C82984A67175DDC67494646E184E468848AC957A91198D832899B` |
| `src/DSoftBusTransport.cpp` | `46ADDEAC8A7EA6D1B975FABEAA844D83619AAC944BF7B25A5EC5852BC7A04D25` |
| `src/DSoftBusTransportDescriptor.cpp` | `D1CC76C1C635BB898D9AC8813A439A50566AE77AB646CB1DCE94A5BD8C56EF87` |
| `src/EndpointDirectory.cpp` | `FC683284E7C738E1A3AAD5878A67A3846760E2065C71393859EFF8507BF33557` |
| `src/InputChannels.cpp` | `416D7A295542574AA0DAEA36422D4E1AD775B4AFA33124CB1B21634328839027` |
| `src/NativeOwnerTable.cpp` | `B1F2AC18FC29FBE1368AE417AB1985D057491D8456FEE91952CDD551FE310AFB` |
| `src/RuntimeSendPath.cpp` | `8B06E84C2EC575CBC46A5B5A8A826F699A7476ECCB0BC580589D5331BAB1FAD4` |
| `src/SendQueue.cpp` | `FBD412D50B1CF0705D0C1F7B1B16A691E796D50C7AB749F36DD7F54B8496592A` |
| `tests/application_attachments_build_tests.cjs` | `7105237DBAF6A65BF1094BA99A2AB4BE1D2C75DB69A304AEE43785A23AEBF2C2` |
| `tests/application_attachments_stack.hpp` | `0749E3D716A7773E1E3478DC8FAB1023658B48FC6294C41F6A51008F68F04C43` |
| `tests/application_attachments_tests.cpp` | `B9587EBD395A7CBFB075207BA8C0509EAE31FBBC94383AF2B55A38A374D642E0` |
| `tests/check_capability.cmake` | `9F8FF011669320AC840BCF897A6F6F222B9F026CD4086D435198DBE450D52E98` |
| `tests/cmake/application_attachments_consumer/CMakeLists.txt` | `6E9FC43CA90F63B052FF28850F538A2AF33DD1587BDB6F76257807DDE1FA964F` |
| `tests/cmake/application_attachments_consumer/main.cpp` | `528FEB01221A673354C20B824ED429B7B9CD03DDCE0B4B4D9F11CEBCC0357F6B` |
| `tests/cmake/directory_consumer/CMakeLists.txt` | `CB3DE93F17523E4241C909874335DB8E45F941016189771EF4BC681B7DB216F8` |
| `tests/cmake/preloaded_fastdds_consumer/binding_macros.cpp` | `642E6FA9F21F1829CF0BB3D8CF05A57D68843258D7B8340656EE5350BAB30791` |
| `tests/cmake/preloaded_fastdds_consumer/CMakeLists.txt` | `410F41A2C1B07BC1889E06E043C8880495DEB124075A93778345B2B324F16B4D` |
| `tests/cmake/preloaded_fastdds_consumer/wrapper/CMakeLists.txt` | `6A9B9C1D5EEB5F4237C314AFD5604AAD705C331FEA65F241CA21EE82123162C7` |
| `tests/cmake/real_fastdds_consumer/CMakeLists.txt` | `1680A5A3A4A3C99FA1B29A56AE0BC516DD65C2FECD691A64FC8DF534708AC651` |
| `tests/cmake/real_fastdds_consumer/main.cpp` | `2FBC00EFA0A09254909B11589749213F44101A66064BC28A38416685ABAC1E8F` |
| `tests/cmake/transport_facade_consumer/CMakeLists.txt` | `0E101D0B1B7F762E39E5997E95B958BC4BAFF67BE4D9DE20DE69BEE1C24D2C9F` |
| `tests/cmake/transport_facade_consumer/main.cpp` | `6891FBAA3872BD85C469984064C0589E8EDD67897E9BDD1AC0B750CB3C303E40` |
| `tests/endpoint_directory_tests.cpp` | `928D235A3CEA4EF9525FE20D3D7C082EAF7FA108B5F02AF594E02312C88AAE63` |
| `tests/fastdds_binding_configuration_tests.cjs` | `BBE16B1ABD57F28A5FFF9CF5058F50463F24C54A183C2C209F1B767C77DD0D35` |
| `tests/fastdds_interface_tests.cpp` | `CF5CD458C0FB6EA68AFCD84E96060D621584F1736FDBA136CA5C4DA2B54DE2E5` |
| `tests/input_channels_tests.cpp` | `3F0F319DBE262B2141CB9546C424E18B13647D2A0680ED735DE972D1128E8421` |
| `tests/lifecycle_probe_harness_tests.cjs` | `5A73F67760CBFF35EFDE15906C9B63232F5FB979D6A245BE8F6EC006B7C95442` |
| `tests/lifecycle_probe_runner_tests.cjs` | `5FEA3431525C5D396353DB690FF3592DCE64834D2384E73969A45A8401B38D17` |
| `tests/lifecycle_run_binding_tests.cjs` | `1DAF758F7823B50339A041C68AD03B428F4F26CDBC00BDE0E3C65EEC891A8337` |
| `tests/locator_wire_tests.cpp` | `B25A2138E262BCDA11F59B52AA3E988AA2CE034AD56525CED30E30033AFE8814` |
| `tests/native_owner_build_tests.cjs` | `B77DE7B2EEA65ADA3DF01B7A4F2C54E18356CC3F9B328E0360BF9449A4036458` |
| `tests/native_owner_table_tests.cpp` | `53B1F47300681AEF0694C7A10855F81C08E2E6C59EE86CEB6E829A75D1B230CF` |
| `tests/native_permission_guard_tests.ps1` | `6996D6B7BD041BD30FC08EDECB43C9DF713AABAE876CE74951C85DFC617F9D42` |
| `tests/native_permission_tests.cpp` | `F460A66DD1D4C7429635D308B2E4363E2D78F7EA45709C6AA8CC5BBC98B27499` |
| `tests/record_pair_harness_tests.cjs` | `C001FA6A51D1646CF1F03FFD0CAF6B61F457038DDBAACF8226E324922B560400` |
| `tests/record_run_binding_tests.cjs` | `FCC0FEE1085CD1D2CF95EA863A17D0530D8A8FC05B295A101F37C59CA06D907F` |
| `tests/record_v1_compatibility_tests.cjs` | `8910CC217BD901520E6A6FCB00609D995D04D8C2F78478FC526B793AABB709F5` |
| `tests/run_binding_fixtures.cjs` | `2EAD7E0821CE8C0F4D4545DB6FB28B43E97EF83CE4390DD56992200FBDB3F8D9` |
| `tests/runtime_send_path_tests.cpp` | `BD8B302523E902F31E2FD3F40BA14DBC21FBC8DCCE7374AA7FC1A965B897B88F` |
| `tests/send_queue_tests.cpp` | `C0398F585D73D67F7DBC6C75437A37159C73BCBB81A9CA4C999A4B8B90BDF3C1` |
| `tests/softbus_fixture.cpp` | `CE67A3B565628977E3AACBBB7777B73145246B5728C2F6D9F167D90FFE4691A9` |
| `tests/softbus_lifecycle_contract_tests.cpp` | `52AFA797C7E466F9C020720E66F03918F64E030FC0F32FE1A88D0B72E3A7F77A` |
| `tests/softbus_lifecycle_subprocess_tests.cjs` | `A9E19F3C546CA4AAD430C51AA7A8DDD34969669D9D66105F836AE30EF29D9422` |
| `tests/softbus_record_contract_tests.cpp` | `F48C46D987802FC80168434EA510707AD08060E29A9FD648426E56C786814577` |
| `tests/token_first_entry_tests.cpp` | `57A6E2AE3A81934403844E45194491598F0A297D4B4F71BC1950A8C2064B39AA` |
| `tests/transport_build_configuration_tests.cjs` | `D4B96EA7CC02F4B76DE06469A11C2916BA0B6900AF22208C52F05E84CC2E2235` |
| `tests/transport_facade_selector_cases.hpp` | `5747D16802116BF5EF02FD4B9636D78B8BEDED1959F442CC2ADBA0D741A6AB60` |
| `tests/transport_facade_tests.cpp` | `CB8830BAD206962D52AC1F52EF63C9BAE426A9DA359114BCB0EBD81879903874` |
