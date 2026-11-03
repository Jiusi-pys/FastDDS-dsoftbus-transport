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
$optionsRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$optionsOutput = [IO.Path]::GetFullPath((Join-Path $optionsRoot $OutputDirectory))
$optionsAllowedRoot = [IO.Path]::GetFullPath((Join-Path $optionsRoot '.artifacts')) + [IO.Path]::DirectorySeparatorChar
if (-not $optionsOutput.StartsWith($optionsAllowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be below this repository .artifacts directory.'
}

Push-Location $optionsRoot
try {
    $optionsCompiler = (Resolve-Path -LiteralPath $PlatformCompiler).Path
    $optionsSdk = (Resolve-Path -LiteralPath $SdkRoot).Path
    $optionsTokenSource = (Resolve-Path -LiteralPath $AccessTokenSource).Path
    $optionsLibraries = @($TokenSetprocLibrary, $AccessTokenLibrary, $PlatformCxxLibrary) | ForEach-Object {
        (Resolve-Path -LiteralPath $_).Path
    }
    $optionsSources = @('probes/softbus_options_probe.cpp', 'probes/token_first_entry.cpp',
        'probes/softbus_options_contract.cpp')
    $optionsIncludes = @('communication_dsoftbus/interfaces/kits/transport',
        'communication_dsoftbus/interfaces/inner_kits/transport',
        'communication_dsoftbus/interfaces/kits/bus_center', 'communication_dsoftbus/interfaces/kits/common',
        (Join-Path $optionsTokenSource 'interfaces/innerkits/token_setproc/include'),
        (Join-Path $optionsTokenSource 'interfaces/innerkits/accesstoken/include'))
    foreach ($optionsInput in @($optionsSources) + @($optionsIncludes)) {
        if (-not (Test-Path -LiteralPath $optionsInput)) { throw "Missing build input: $optionsInput" }
    }
    New-Item -ItemType Directory -Path $optionsOutput -Force | Out-Null
    $optionsBinary = Join-Path $optionsOutput 'softbus_options_probe'
    # Fixed optimized negative option-stage diagnostic. This build step never
    # invokes the provider or changes permissions on a device.
    $optionsArguments = @('--target=aarch64-linux-ohos', "--sysroot=$optionsSdk/sysroot", '-O2', '-std=c++17',
        '-Wall', '-Wextra', '-stdlib=libc++', '-nostdlib++')
    $optionsArguments += @($optionsIncludes | ForEach-Object { '-I' + $_ })
    $optionsArguments += $optionsSources + $optionsLibraries + @('-ldl', '-Wl,--allow-shlib-undefined', '-o', $optionsBinary)
    $optionsInputs = @($optionsSources) + @('probes/token_first_entry.hpp', 'probes/softbus_options_contract.hpp',
        'scripts/BuildOptionsProbe.ps1',
        'communication_dsoftbus/interfaces/kits/transport/socket.h',
        'communication_dsoftbus/interfaces/kits/transport/trans_type.h',
        'communication_dsoftbus/interfaces/inner_kits/transport/inner_socket.h') + @($optionsLibraries) + @($optionsCompiler)
    $optionsHashes = @($optionsInputs | ForEach-Object {
        $optionsHash = Get-FileHash -LiteralPath $_ -Algorithm SHA256
        @{ path = $optionsHash.Path; sha256 = $optionsHash.Hash.ToLowerInvariant() }
    })
    $optionsManifest = [ordered]@{ schemaVersion = 1; scope = 'COMPILE_ONLY';
        startedAt = [DateTime]::UtcNow.ToString('o'); compiler = $optionsCompiler;
        arguments = $optionsArguments; inputs = $optionsHashes; compileExit = $null; artifact = $null }
    $optionsManifestPath = Join-Path $optionsOutput 'build-manifest.json'
    $optionsManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $optionsManifestPath -Encoding UTF8
    & $optionsCompiler @optionsArguments
    $optionsCompileExit = $LASTEXITCODE
    $optionsManifest.compileExit = $optionsCompileExit
    if ($optionsCompileExit -eq 0) {
        $optionsHash = Get-FileHash -LiteralPath $optionsBinary -Algorithm SHA256
        $optionsManifest.artifact = @{ path = $optionsHash.Path; sha256 = $optionsHash.Hash.ToLowerInvariant() }
    }
    $optionsManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $optionsManifestPath -Encoding UTF8
    Write-Output "platform_compile_exit=$optionsCompileExit manifest=$optionsManifestPath"
    exit $optionsCompileExit
} finally {
    Pop-Location
}
