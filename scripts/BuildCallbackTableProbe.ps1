param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$SdkRoot = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native',
    [string]$PlatformCompiler = '.artifacts/toolchains/clang-windows-bb5cdf-20250726/clang_windows-x86_64-bb5cdf-20250726/bin/clang++.exe',
    [string]$AccessTokenSource = '.artifacts/reference/security_access_token',
    [string]$TokenSetprocLibrary = '.artifacts/permissions/20260913T180236Z-corrected-preflight/A-libtokensetproc_shared.z.so',
    [string]$AccessTokenLibrary = '.artifacts/permissions/20260913T180236Z-corrected-preflight/A-libaccesstoken_sdk.z.so',
    [string]$PlatformCxxLibrary = '.artifacts/permissions/20260913T181632Z-abi-preflight/A-chipset-sdk-sp-libc++.so'
)

# Compile only. No deployment, token changes, network setup, or SDK calls.
# AccessToken is a platform C++ API: use the original platform __h headers
# and matching library, never redefine an ABI namespace to force a link.
$ErrorActionPreference = 'Stop'
$probeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$probeOutput = [IO.Path]::GetFullPath((Join-Path $probeRoot $OutputDirectory))
$probeAllowedRoot = [IO.Path]::GetFullPath((Join-Path $probeRoot '.artifacts')) + [IO.Path]::DirectorySeparatorChar
if (-not $probeOutput.StartsWith($probeAllowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be below this repository .artifacts directory.'
}

Push-Location $probeRoot
try {
    $probeCompiler = (Resolve-Path -LiteralPath $PlatformCompiler).Path
    $probeSdk = (Resolve-Path -LiteralPath $SdkRoot).Path
    $probeTokenSource = (Resolve-Path -LiteralPath $AccessTokenSource).Path
    $probeLibraries = @($TokenSetprocLibrary, $AccessTokenLibrary, $PlatformCxxLibrary) | ForEach-Object {
        (Resolve-Path -LiteralPath $_).Path
    }
    $probeSources = @('probes/softbus_callback_table_probe.cpp', 'probes/token_first_entry.cpp')
    $probeIncludes = @('communication_dsoftbus/interfaces/kits/transport',
        'communication_dsoftbus/interfaces/kits/bus_center', 'communication_dsoftbus/interfaces/kits/common',
        (Join-Path $probeTokenSource 'interfaces/innerkits/token_setproc/include'),
        (Join-Path $probeTokenSource 'interfaces/innerkits/accesstoken/include'))
    foreach ($probeInput in @($probeSources) + @($probeIncludes)) {
        if (-not (Test-Path -LiteralPath $probeInput)) { throw "Missing build input: $probeInput" }
    }
    New-Item -ItemType Directory -Path $probeOutput -Force | Out-Null
    $probeBinary = Join-Path $probeOutput 'softbus_callback_table_probe'
    $probeArguments = @('--target=aarch64-linux-ohos', "--sysroot=$probeSdk/sysroot", '-std=c++17',
        '-Wall', '-Wextra', '-stdlib=libc++', '-nostdlib++')
    $probeArguments += @($probeIncludes | ForEach-Object { '-I' + $_ })
    $probeArguments += $probeSources + $probeLibraries + @('-ldl', '-Wl,--allow-shlib-undefined', '-o', $probeBinary)
    $probeInputs = @($probeSources) + @('probes/token_first_entry.hpp', 'probes/softbus_callback_table_oracle.hpp', 'scripts/BuildCallbackTableProbe.ps1',
        'communication_dsoftbus/interfaces/kits/transport/socket.h',
        'communication_dsoftbus/interfaces/kits/transport/trans_type.h') + @($probeLibraries) + @($probeCompiler)
    $probeHashes = @($probeInputs | ForEach-Object {
        $probeHash = Get-FileHash -LiteralPath $_ -Algorithm SHA256
        @{ path = $probeHash.Path; sha256 = $probeHash.Hash.ToLowerInvariant() }
    })
    $probeManifest = [ordered]@{ schemaVersion = 1; scope = 'COMPILE_ONLY';
        startedAt = [DateTime]::UtcNow.ToString('o'); compiler = $probeCompiler;
        arguments = $probeArguments; inputs = $probeHashes; compileExit = $null; artifact = $null }
    $probeManifestPath = Join-Path $probeOutput 'build-manifest.json'
    $probeManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $probeManifestPath -Encoding UTF8
    & $probeCompiler @probeArguments
    $probeCompileExit = $LASTEXITCODE
    $probeManifest.compileExit = $probeCompileExit
    if ($probeCompileExit -eq 0) {
        $probeHash = Get-FileHash -LiteralPath $probeBinary -Algorithm SHA256
        $probeManifest.artifact = @{ path = $probeHash.Path; sha256 = $probeHash.Hash.ToLowerInvariant() }
    }
    $probeManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $probeManifestPath -Encoding UTF8
    Write-Output "platform_compile_exit=$probeCompileExit manifest=$probeManifestPath"
    exit $probeCompileExit
} finally {
    Pop-Location
}
