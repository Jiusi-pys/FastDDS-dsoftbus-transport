param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [switch]$EnableRxDrop,
    [string]$RestoredSourceReceipt = '',
    [string]$SdkRoot = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native',
    [string]$Prefix = '.artifacts/install/ohos-app',
    [string]$PlatformCompiler = '.artifacts/toolchains/clang-windows-bb5cdf-20250726/clang_windows-x86_64-bb5cdf-20250726/bin/clang++.exe',
    [string]$AccessTokenSource = '.artifacts/reference/security_access_token',
    [string]$TokenSetprocLibrary = '.artifacts/permissions/20260913T180236Z-corrected-preflight/A-libtokensetproc_shared.z.so',
    [string]$AccessTokenLibrary = '.artifacts/permissions/20260913T180236Z-corrected-preflight/A-libaccesstoken_sdk.z.so',
    [string]$PlatformCxxLibrary = '.artifacts/permissions/20260913T181632Z-abi-preflight/A-chipset-sdk-sp-libc++.so'
)

# Compile/package only: no deployment, SDK invocation, token mutation or qualification.
# Fast DDS is app ABI (__n1). Only the C/POD bridge uses platform ABI (__h).
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ExperimentalBuildInputs.ps1')
$experimentRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$experimentOutput = [IO.Path]::GetFullPath((Join-Path $experimentRoot $OutputDirectory))
$experimentAllowed = [IO.Path]::GetFullPath((Join-Path $experimentRoot '.artifacts')) + [IO.Path]::DirectorySeparatorChar
if (-not $experimentOutput.StartsWith($experimentAllowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be below the repository .artifacts directory.'
}

Push-Location $experimentRoot
try {
    $experimentSdk = (Resolve-Path -LiteralPath $SdkRoot).Path
    $experimentPrefix = (Resolve-Path -LiteralPath $Prefix).Path
    $experimentPlatformCompiler = (Resolve-Path -LiteralPath $PlatformCompiler).Path
    $experimentTokenSource = (Resolve-Path -LiteralPath $AccessTokenSource).Path
    $experimentPlatformLibraries = @($TokenSetprocLibrary, $AccessTokenLibrary, $PlatformCxxLibrary) | ForEach-Object {
        (Resolve-Path -LiteralPath $_).Path
    }
    $experimentCmake = (Get-Command cmake -ErrorAction Stop).Source
    $experimentNinja = Join-Path $experimentSdk 'build-tools/cmake/bin/ninja.exe'
    $experimentBridge = Join-Path $experimentOutput 'libmdds_experiment_token_bridge.so'
    $experimentBuild = Join-Path $experimentOutput 'build'
    $experimentPackage = Join-Path $experimentOutput 'package'
    New-Item -ItemType Directory -Path $experimentOutput -Force | Out-Null
    if (Test-Path -LiteralPath (Join-Path $experimentOutput 'build-manifest.json')) { throw 'Refusing to overwrite an existing build manifest; use a new OutputDirectory' }
    $experimentExports = Join-Path $experimentOutput 'token-bridge.exports'
    $experimentBridgeDepfile = Join-Path $experimentOutput 'token-bridge.d'
    $experimentCmakeQuery = Join-Path $experimentBuild '.cmake/api/v1/query/client-mdds-inputs'
    New-Item -ItemType Directory -Path $experimentCmakeQuery -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $experimentCmakeQuery 'cmakeFiles-v1'), '', [Text.UTF8Encoding]::new($false))
    '{ global: MddsTokenBridgeVerify; MddsTokenBridgeGetSelfToken; MddsTokenBridgeSetSelfToken; local: *; };' |
        Set-Content -LiteralPath $experimentExports -Encoding ASCII
    $experimentCompileArguments = @('--target=aarch64-linux-ohos', "--sysroot=$experimentSdk/sysroot",
        '-std=c++17', '-shared', '-fPIC', '-fvisibility=hidden', '-fvisibility-inlines-hidden',
        '-Wall', '-Wextra', '-stdlib=libc++', '-nostdlib++', '-MD', '-MF', $experimentBridgeDepfile,
        ('-I' + (Join-Path $experimentTokenSource 'interfaces/innerkits/token_setproc/include')),
        ('-I' + (Join-Path $experimentTokenSource 'interfaces/innerkits/accesstoken/include')),
        'experiments/fastdds_socket/TokenBridge.cpp') + @($experimentPlatformLibraries) + @(
        '-Wl,--allow-shlib-undefined', '-Wl,-soname,libmdds_experiment_token_bridge.so',
        "-Wl,--version-script=$experimentExports", '-o', $experimentBridge)
    $experimentCmakeArguments = @('-S', $experimentRoot, '-B', $experimentBuild, '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$experimentNinja",
        "-DCMAKE_TOOLCHAIN_FILE=$experimentSdk/build/cmake/ohos.toolchain.cmake", '-DOHOS_ARCH=arm64-v8a',
        '-DCMAKE_BUILD_TYPE=Release', '-DMDDS_FASTDDS_MODE=CONFIG',
        "-Dfastdds_DIR=$experimentPrefix/share/fastdds/cmake",
        "-Dfastcdr_DIR=$experimentPrefix/lib/cmake/fastcdr",
        "-Dfoonathan_memory_DIR=$experimentPrefix/lib/foonathan_memory/cmake",
        '-DBUILD_FASTDDS_DSOFTBUS_EXPERIMENT=ON', '-DMDDS_EXPERIMENT_NATIVE=ON',
        ('-DMDDS_EXPERIMENT_RX_DROP=' + $(if ($EnableRxDrop) { 'ON' } else { 'OFF' })),
        "-DMDDS_EXPERIMENT_TOKEN_BRIDGE=$experimentBridge", '-DBUILD_TESTING=OFF',
        '-DBUILD_INTERFACE_PROBE=OFF', '-DBUILD_SOFTBUS_CAPABILITY_PROBE=OFF',
        '-DBUILD_NATIVE_PERMISSION_PROBE=OFF', '-DBUILD_DSOFTBUS_DIRECTORY=OFF',
        '-DBUILD_DSOFTBUS_NATIVE_OWNER=OFF', '-DBUILD_DSOFTBUS_SEND_QUEUE=OFF',
        '-DBUILD_DSOFTBUS_INPUT_CHANNELS=OFF', '-DBUILD_DSOFTBUS_APPLICATION_ATTACHMENTS=OFF',
        '-DBUILD_DSOFTBUS_RUNTIME_SEND_PATH=OFF', '-DBUILD_DSOFTBUS_NATIVE_SEND_EXECUTION=OFF',
        '-DBUILD_DSOFTBUS_PROCESS=OFF', '-DBUILD_DSOFTBUS_TRANSPORT_FACADE=OFF')
    $experimentBuildArguments = @('--build', $experimentBuild, '--target', 'fastdds_socket_experiment', '--parallel', '3')
    $experimentDependencies = @(
        @{ input = Join-Path $experimentPrefix 'lib/libfastdds.so.3.6.2.0'; name = 'libfastdds.so.3.6' },
        @{ input = Join-Path $experimentPrefix 'lib/libfastcdr.so.2.3.5'; name = 'libfastcdr.so.2' },
        @{ input = Join-Path $experimentPrefix 'lib/libfoonathan_memory-0.7.4.so'; name = 'libfoonathan_memory-0.7.4.so' },
        @{ input = Join-Path $experimentSdk 'llvm/lib/aarch64-linux-ohos/libc++_shared.so'; name = 'libc++_shared.so' })
    $experimentInputs = @('scripts/BuildFastDdsSocketExperiment.ps1', 'scripts/ExperimentalBuildInputs.ps1', 'CMakeLists.txt', 'cmake/FastDDSBinding.cmake',
        'src/V1Address.cpp', 'include/mdds/dsoftbus/V1Address.hpp', 'include/mdds/dsoftbus/EndpointDirectory.hpp',
        'probes/token_first_entry.cpp', 'probes/token_first_entry.hpp',
        'Fast-DDS/examples/cpp/hello_world/HelloWorld.hpp',
        'Fast-DDS/examples/cpp/hello_world/HelloWorldCdrAux.hpp',
        'Fast-DDS/examples/cpp/hello_world/HelloWorldCdrAux.ipp',
        'Fast-DDS/examples/cpp/hello_world/HelloWorldPubSubTypes.cxx',
        'Fast-DDS/examples/cpp/hello_world/HelloWorldPubSubTypes.hpp',
        'Fast-DDS/examples/cpp/hello_world/HelloWorldTypeObjectSupport.cxx',
        'Fast-DDS/examples/cpp/hello_world/HelloWorldTypeObjectSupport.hpp',
        'communication_dsoftbus/interfaces/kits/transport/socket.h',
        'communication_dsoftbus/interfaces/kits/transport/trans_type.h',
        'communication_dsoftbus/interfaces/kits/bus_center/softbus_bus_center.h',
        $experimentExports, $experimentPlatformCompiler, $experimentCmake, $experimentNinja,
        (Join-Path $experimentSdk 'llvm/bin/clang++.exe'),
        (Join-Path $experimentSdk 'build/cmake/ohos.toolchain.cmake'))
    $experimentInputs += @(Get-ChildItem -LiteralPath 'experiments/fastdds_socket' -Recurse -File | Select-Object -ExpandProperty FullName)
    # The real experiment now links the frozen lifecycle cores. Include their
    # implementations and transitive internal/public headers, not just V1Address.
    $experimentInputs += @(Get-ChildItem -LiteralPath 'src','include/mdds/dsoftbus','cmake' -Recurse -File | Select-Object -ExpandProperty FullName)
    $experimentInputs += @($experimentPlatformLibraries) + @($experimentDependencies | ForEach-Object { $_.input })
    $experimentInputs += @(Get-ChildItem -LiteralPath (Join-Path $experimentTokenSource 'interfaces/innerkits/accesstoken/include') -Filter '*.h' -File | Select-Object -ExpandProperty FullName)
    $experimentInputs += Join-Path $experimentTokenSource 'interfaces/innerkits/token_setproc/include/token_setproc.h'
    if (-not [string]::IsNullOrWhiteSpace($RestoredSourceReceipt)) {
        $experimentInputs += (Resolve-Path -LiteralPath $RestoredSourceReceipt).Path
    }
    $experimentHashes = @($experimentInputs | Sort-Object -Unique | ForEach-Object {
        $experimentHash = Get-FileHash -LiteralPath $_ -Algorithm SHA256
        @{ path = $experimentHash.Path; sha256 = $experimentHash.Hash.ToLowerInvariant() }
    })
    # Source identity begins before any compiler invocation. A restored tree has
    # explicit receipt provenance, never an invented clean Git checkout.
    if (-not [string]::IsNullOrWhiteSpace($RestoredSourceReceipt)) {
        $receiptPath = (Resolve-Path -LiteralPath $RestoredSourceReceipt).Path
        $sourceRoot = [IO.Path]::GetFullPath($experimentRoot).TrimEnd('\', '/')
        $sourcePrefix = $sourceRoot + [IO.Path]::DirectorySeparatorChar
        if ($receiptPath.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Restored source receipt must be outside the source tree' }
        $receipt = [IO.File]::ReadAllText($receiptPath) | ConvertFrom-Json
        if ($receipt.schema -ne 1 -or $receipt.kind -cne 'RESTORED_PROJECT_SOURCE') { throw 'Unsupported restored source receipt schema/kind' }
        if ($receipt.source_root -isnot [string] -or -not [IO.Path]::IsPathRooted($receipt.source_root) -or
            -not [IO.Path]::GetFullPath($receipt.source_root).TrimEnd('\', '/').Equals($sourceRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Restored source receipt root mismatch' }
        if ($receipt.baseline_head -isnot [string] -or $receipt.baseline_head -cnotmatch '^[0-9a-f]{40}$') { throw 'Invalid restored baseline HEAD' }
        foreach ($digestField in @('checkpoint_inventory_sha256', 'overlay_manifest_sha256')) {
            if ($receipt.$digestField -isnot [string] -or $receipt.$digestField -cnotmatch '^[0-9a-f]{64}$') { throw "Invalid restored receipt digest: $digestField" }
        }
        if (($null -eq $receipt.files) -or @($receipt.files).Count -eq 0) { throw 'Restored source receipt has no files' }
        $receiptFilesSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        if ((Get-Item -LiteralPath $sourceRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Restored source root is a reparse point' }
        foreach ($receiptFile in $receipt.files) {
            $relativePath = $receiptFile.path
            if ($relativePath -isnot [string] -or [string]::IsNullOrWhiteSpace($relativePath) -or
                $relativePath.Contains('\') -or $relativePath.Contains(':') -or [IO.Path]::IsPathRooted($relativePath)) { throw 'Invalid restored source relative path' }
            $segments = $relativePath.Split('/')
            foreach ($segment in $segments) {
                if ([string]::IsNullOrWhiteSpace($segment) -or $segment -in @('.', '..') -or $segment -match '[. ]$' -or
                    $segment.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) { throw 'Unsafe restored source path segment' }
            }
            if (-not $receiptFilesSeen.Add($relativePath)) { throw "Duplicate restored source path: $relativePath" }
            if ($receiptFile.sha256 -isnot [string] -or $receiptFile.sha256 -cnotmatch '^[0-9a-f]{64}$') { throw "Invalid restored source file digest: $relativePath" }
            $receiptInput = $sourceRoot
            foreach ($segment in $segments) {
                $receiptInput = Join-Path $receiptInput $segment
                if (-not (Test-Path -LiteralPath $receiptInput)) { throw "Missing restored source input: $relativePath" }
                if ((Get-Item -LiteralPath $receiptInput).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Restored source input traverses a reparse point: $relativePath" }
            }
            if (-not (Test-Path -LiteralPath $receiptInput -PathType Leaf)) { throw "Restored source input is not a file: $relativePath" }
            $receiptInputHash = (Get-FileHash -LiteralPath $receiptInput -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($receiptInputHash -cne $receiptFile.sha256) { throw "Restored source input hash mismatch: $relativePath" }
        }
        $experimentSourceState = [ordered]@{
            head = $receipt.baseline_head; provenance = 'RESTORED_RECEIPT_VERIFIED';
            receiptPath = $receiptPath;
            receiptSha256 = (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant();
            receipt = $receipt
        }
    } else {
        # Empty status/diff is valid; a failed Git command is never a valid empty record.
        $experimentGitHead = @(git rev-parse HEAD)
        if ($LASTEXITCODE -ne 0 -or $experimentGitHead.Count -ne 1 -or $experimentGitHead[0] -cnotmatch '^[0-9a-f]{40}$') { throw 'Cannot establish source HEAD' }
        $experimentGitStatus = @(git status --porcelain=v1 -uall)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot establish source dirty status' }
        $experimentGitDiff = @(git diff --binary HEAD)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot establish source diff' }
        $experimentGitUntracked = @(git ls-files --others --exclude-standard)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot establish untracked source inventory' }
        $experimentSourceState = [ordered]@{
            head = $experimentGitHead[0];
            status = $experimentGitStatus;
            trackedDiff = ($experimentGitDiff -join "`n");
            untracked = @($experimentGitUntracked | Sort-Object | ForEach-Object {
                $sourceHash = Get-FileHash -LiteralPath $_ -Algorithm SHA256
                @{ path = $_; sha256 = $sourceHash.Hash.ToLowerInvariant() }
            })
        }
    }
    $experimentSourceStatePath = Join-Path $experimentOutput 'source-state.json'
    [IO.File]::WriteAllText($experimentSourceStatePath, ($experimentSourceState | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    $experimentSourceStateHash = (Get-FileHash -LiteralPath $experimentSourceStatePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $experimentManifest = [ordered]@{ schemaVersion = 2; scope = 'UNQUALIFIED_EXPERIMENT_COMPILE_ONLY';
        startedAt = [DateTime]::UtcNow.ToString('o'); inputs = $experimentHashes;
        sourceState = $experimentSourceStatePath; sourceStateSha256 = $experimentSourceStateHash;
        platformCompiler = $experimentPlatformCompiler; bridgeArguments = $experimentCompileArguments;
        cmake = $experimentCmake; configureArguments = $experimentCmakeArguments; buildArguments = $experimentBuildArguments;
        bridgeExit = $null; configureExit = $null; buildExit = $null; artifacts = @();
        dependencyInputs = $null;
        runtime = 'NOT_RUN'; qualification = 'NOT_QUALIFIED' }
    $experimentManifestPath = Join-Path $experimentOutput 'build-manifest.json'
    try {
        # Windows PowerShell 5 treats native stderr as ErrorRecords. Warnings
        # must reach the log without terminating the pipeline before the actual
        # process exit is captured; the saved exit still governs every stage.
        $experimentStepPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $experimentPlatformCompiler @experimentCompileArguments 2>&1 | Tee-Object -FilePath (Join-Path $experimentOutput 'bridge-build.log')
            $experimentManifest.bridgeExit = $LASTEXITCODE
        } finally { $ErrorActionPreference = $experimentStepPreference }
        if ($experimentManifest.bridgeExit -ne 0) { throw "Bridge compilation failed: $($experimentManifest.bridgeExit)" }
        try {
            $ErrorActionPreference = 'Continue'
            & $experimentCmake @experimentCmakeArguments 2>&1 | Tee-Object -FilePath (Join-Path $experimentOutput 'configure.log')
            $experimentManifest.configureExit = $LASTEXITCODE
        } finally { $ErrorActionPreference = $experimentStepPreference }
        if ($experimentManifest.configureExit -ne 0) { throw "Experiment configure failed: $($experimentManifest.configureExit)" }
        try {
            $ErrorActionPreference = 'Continue'
            & $experimentCmake @experimentBuildArguments 2>&1 | Tee-Object -FilePath (Join-Path $experimentOutput 'build.log')
            $experimentManifest.buildExit = $LASTEXITCODE
        } finally { $ErrorActionPreference = $experimentStepPreference }
        if ($experimentManifest.buildExit -ne 0) { throw "Experiment build failed: $($experimentManifest.buildExit)" }
        # Capture compiler-reported prerequisites from this completed build,
        # including system headers (-MD), rather than guessing header closure.
        $experimentDepsStdout = Join-Path $experimentOutput 'ninja-deps.stdout.txt'
        $experimentDepsStderr = Join-Path $experimentOutput 'ninja-deps.stderr.txt'
        try {
            $ErrorActionPreference = 'Continue'
            $experimentDepsLines = @(& $experimentNinja -C $experimentBuild -t deps 2> $experimentDepsStderr)
            $experimentDepsExit = $LASTEXITCODE
        } finally { $ErrorActionPreference = $experimentStepPreference }
        $experimentDepsText = ($experimentDepsLines -join "`n") + "`n"
        [IO.File]::WriteAllText($experimentDepsStdout, $experimentDepsText, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $experimentOutput 'ninja-deps-command.json'),
            (@{command=@($experimentNinja,'-C',$experimentBuild,'-t','deps');exitCode=$experimentDepsExit} | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
        $experimentNativeDeps = Read-MddsNinjaDependencies -Text $experimentDepsText -ExitCode $experimentDepsExit
        if (-not (Test-Path -LiteralPath $experimentBridgeDepfile -PathType Leaf)) { throw 'Bridge compiler did not produce its dependency file' }
        $experimentBridgeDeps = @(Read-MddsCompilerDepfile -Text ([IO.File]::ReadAllText($experimentBridgeDepfile)))
        $experimentReplyRoot = Join-Path $experimentBuild '.cmake/api/v1/reply'
        $experimentIndex = Get-ChildItem -LiteralPath $experimentReplyRoot -Filter 'index-*.json' -File |
            Sort-Object LastWriteTimeUtc,Name | Select-Object -Last 1
        if (-not $experimentIndex) { throw 'CMake File API did not produce an index' }
        $experimentReply = [IO.File]::ReadAllText($experimentIndex.FullName) | ConvertFrom-Json
        $experimentCmakeFileReply = $experimentReply.reply.'client-mdds-inputs'.'cmakeFiles-v1'.jsonFile
        if ([string]::IsNullOrWhiteSpace($experimentCmakeFileReply) -or [IO.Path]::GetFileName($experimentCmakeFileReply) -cne $experimentCmakeFileReply) { throw 'Missing/invalid CMake cmakeFiles-v1 reply' }
        $experimentCmakeReplyPath = Join-Path $experimentReplyRoot $experimentCmakeFileReply
        $experimentCmakeDeps = @(Read-MddsCmakeInputObject -Text ([IO.File]::ReadAllText($experimentCmakeReplyPath)))
        Copy-Item -LiteralPath $experimentCmakeReplyPath -Destination (Join-Path $experimentOutput 'cmake-inputs.json')
        $experimentDependencyHashes = @(
            Get-MddsDependencyHashes -Paths $experimentNativeDeps.Paths -BaseDirectory $experimentBuild
            Get-MddsDependencyHashes -Paths $experimentBridgeDeps -BaseDirectory $experimentRoot
            Get-MddsDependencyHashes -Paths $experimentCmakeDeps -BaseDirectory $experimentRoot
        )
        # Retain explicit link libraries/tools and the broader source inventory.
        # Reject changes in those prehashed files during compilation.
        $experimentAllHashes = @(Get-MddsDependencyHashes -Paths (@($experimentHashes | ForEach-Object { $_.path }) +
            @($experimentDependencyHashes | ForEach-Object { $_.path })) -BaseDirectory $experimentRoot)
        $experimentHashByPath = @{}; foreach ($inputHash in $experimentAllHashes) { $experimentHashByPath[$inputHash.path]=$inputHash.sha256 }
        foreach ($beforeHash in $experimentHashes) {
            if ($experimentHashByPath[$beforeHash.path] -cne $beforeHash.sha256) { throw "Prehashed build input changed during compilation: $($beforeHash.path)" }
        }
        $experimentManifest.inputs = $experimentAllHashes
        $experimentManifest.dependencyInputs = [ordered]@{
            status='CAPTURED';ninjaCommandExit=$experimentDepsExit;ninjaTargetCount=$experimentNativeDeps.Targets.Count;
            ninjaStdout=$experimentDepsStdout;ninjaStderr=$experimentDepsStderr;bridgeDepfile=$experimentBridgeDepfile;
            cmakeIndex=$experimentIndex.FullName;cmakeFilesReply=$experimentCmakeReplyPath;
            appPrerequisiteCount=$experimentNativeDeps.Paths.Count;bridgePrerequisiteCount=$experimentBridgeDeps.Count;
            cmakeInputCount=$experimentCmakeDeps.Count;uniqueInputCount=$experimentAllHashes.Count;
            scope='COMPILER_REPORTED_INCLUDES_INCLUDING_SYSTEM_HEADERS_CMAKE_INPUTS_EXPLICIT_TOOLS_AND_LINK_LIBRARIES';
            capturePhase='AFTER_SUCCESSFUL_BUILD_WITH_PREHASHED_INPUT_STABILITY_CHECK';
            limitation='NOT_HERMETIC_OS_OR_IMPLICIT_LINKER_ENVIRONMENT_CLOSURE'
        }
        New-Item -ItemType Directory -Path $experimentPackage -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $experimentBuild 'experiments/fastdds_socket/fastdds_socket_experiment') -Destination $experimentPackage
        Copy-Item -LiteralPath $experimentBridge -Destination $experimentPackage
        foreach ($experimentDependency in $experimentDependencies) {
            Copy-Item -LiteralPath $experimentDependency.input -Destination (Join-Path $experimentPackage $experimentDependency.name)
        }
        $experimentManifest.artifacts = @(Get-ChildItem -LiteralPath $experimentPackage -File | ForEach-Object {
            $experimentHash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
            @{ path = $experimentHash.Path; sha256 = $experimentHash.Hash.ToLowerInvariant() }
        })
        Write-Output "compile_only=PASS runtime=NOT_RUN qualification=NOT_QUALIFIED manifest=$experimentManifestPath"
    } finally {
        $experimentManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $experimentManifestPath -Encoding UTF8
    }
} finally {
    Pop-Location
}
