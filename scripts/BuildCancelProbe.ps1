param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$SdkRoot = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native',
    [string]$PlatformCompiler = '.artifacts/toolchains/clang-windows-bb5cdf-20250726/clang_windows-x86_64-bb5cdf-20250726/bin/clang++.exe',
    [string]$AccessTokenSource = '.artifacts/reference/security_access_token',
    [string]$TokenSetprocLibrary = '.artifacts/permissions/20260913T180236Z-corrected-preflight/A-libtokensetproc_shared.z.so',
    [string]$AccessTokenLibrary = '.artifacts/permissions/20260913T180236Z-corrected-preflight/A-libaccesstoken_sdk.z.so',
    [string]$PlatformCxxLibrary = '.artifacts/permissions/20260913T181632Z-abi-preflight/A-chipset-sdk-sp-libc++.so'
)

# Compile only; no deployment, permission writes, token creation, or SDK calls.
# Platform __h C++ headers and matching AccessToken/libc++ libraries are used
# unchanged. Native Socket symbols are loaded from the board provider at runtime.
$ErrorActionPreference = 'Stop'
$cancelRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$cancelOutput = [IO.Path]::GetFullPath((Join-Path $cancelRoot $OutputDirectory))
$cancelAllowedRoot = [IO.Path]::GetFullPath((Join-Path $cancelRoot '.artifacts')) + [IO.Path]::DirectorySeparatorChar
if (-not $cancelOutput.StartsWith($cancelAllowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be below this repository .artifacts directory.'
}

Push-Location $cancelRoot
try {
    $cancelCompiler = (Resolve-Path -LiteralPath $PlatformCompiler).Path
    $cancelSdk = (Resolve-Path -LiteralPath $SdkRoot).Path
    $cancelTokenSource = (Resolve-Path -LiteralPath $AccessTokenSource).Path
    $cancelLibraries = @($TokenSetprocLibrary, $AccessTokenLibrary, $PlatformCxxLibrary) | ForEach-Object {
        (Resolve-Path -LiteralPath $_).Path
    }
    $cancelSources = @('probes/softbus_cancel_probe.cpp', 'probes/token_first_entry.cpp',
        'probes/softbus_cancel_contract.cpp', 'probes/softbus_probe_profile.cpp')
    $cancelIncludes = @('communication_dsoftbus/interfaces/kits/transport',
        'communication_dsoftbus/interfaces/kits/bus_center', 'communication_dsoftbus/interfaces/kits/common',
        (Join-Path $cancelTokenSource 'interfaces/innerkits/token_setproc/include'),
        (Join-Path $cancelTokenSource 'interfaces/innerkits/accesstoken/include'))
    foreach ($cancelInput in @($cancelSources) + @($cancelIncludes)) {
        if (-not (Test-Path -LiteralPath $cancelInput)) { throw "Missing build input: $cancelInput" }
    }
    New-Item -ItemType Directory -Path $cancelOutput -Force | Out-Null
    $cancelBinary = Join-Path $cancelOutput 'softbus_cancel_probe'
    # Fixed optimized diagnostic: producer overhead must not hide SDK pressure.
    # -O2 changes neither probe workload bounds nor the cancellation deadlines.
    $cancelArguments = @('--target=aarch64-linux-ohos', "--sysroot=$cancelSdk/sysroot", '-O2', '-std=c++17',
        '-Wall', '-Wextra', '-stdlib=libc++', '-nostdlib++')
    $cancelArguments += @($cancelIncludes | ForEach-Object { '-I' + $_ })
    $cancelArguments += $cancelSources + $cancelLibraries + @('-ldl', '-Wl,--allow-shlib-undefined', '-o', $cancelBinary)
    $cancelInputs = @($cancelSources) + @('probes/token_first_entry.hpp', 'probes/softbus_cancel_contract.hpp',
        'probes/softbus_probe_profile.hpp', 'scripts/BuildCancelProbe.ps1',
        'communication_dsoftbus/interfaces/kits/transport/socket.h',
        'communication_dsoftbus/interfaces/kits/transport/trans_type.h') + @($cancelLibraries) + @($cancelCompiler)
    $cancelHashes = @($cancelInputs | ForEach-Object {
        $cancelHash = Get-FileHash -LiteralPath $_ -Algorithm SHA256
        @{ path = $cancelHash.Path; sha256 = $cancelHash.Hash.ToLowerInvariant() }
    })
    $cancelManifest = [ordered]@{ schemaVersion = 1; scope = 'COMPILE_ONLY';
        startedAt = [DateTime]::UtcNow.ToString('o'); compiler = $cancelCompiler;
        arguments = $cancelArguments; inputs = $cancelHashes; compileExit = $null; artifact = $null }
    $cancelManifestPath = Join-Path $cancelOutput 'build-manifest.json'
    $cancelManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $cancelManifestPath -Encoding UTF8
    & $cancelCompiler @cancelArguments
    $cancelCompileExit = $LASTEXITCODE
    $cancelManifest.compileExit = $cancelCompileExit
    if ($cancelCompileExit -eq 0) {
        $cancelHash = Get-FileHash -LiteralPath $cancelBinary -Algorithm SHA256
        $cancelManifest.artifact = @{ path = $cancelHash.Path; sha256 = $cancelHash.Hash.ToLowerInvariant() }
    }
    $cancelManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $cancelManifestPath -Encoding UTF8
    Write-Output "platform_compile_exit=$cancelCompileExit manifest=$cancelManifestPath"
    exit $cancelCompileExit
} finally {
    Pop-Location
}
