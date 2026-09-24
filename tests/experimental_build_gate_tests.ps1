param([string]$OutputDirectory = '.artifacts/integration-experiment-20260921/build-gate')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
if (-not $out.StartsWith((Join-Path $root '.artifacts') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Gate outputs must be below repository .artifacts'
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
$cmake = [IO.File]::ReadAllText((Join-Path $root 'CMakeLists.txt'))
if ($cmake -notmatch 'option\(BUILD_FASTDDS_DSOFTBUS_EXPERIMENT\s+"[^"]*"\s+OFF\)') {
    throw 'Explicit experiment option is missing or not default OFF'
}
$disabled = @('BUILD_INTERFACE_PROBE','BUILD_DSOFTBUS_DIRECTORY','BUILD_DSOFTBUS_NATIVE_OWNER',
    'BUILD_DSOFTBUS_SEND_QUEUE','BUILD_DSOFTBUS_INPUT_CHANNELS','BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS',
    'BUILD_DSOFTBUS_RUNTIME_SEND_PATH','BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION','BUILD_DSOFTBUS_PROCESS',
    'BUILD_DSOFTBUS_TRANSPORT_FACADE','BUILD_SOFTBUS_CAPABILITY_PROBE','BUILD_NATIVE_PERMISSION_PROBE','BUILD_TESTING')
$common = @('-S',$root,'-G','Visual Studio 17 2022','-A','x64','-DMDDS_FASTDDS_MODE=SOURCE_PROBE') +
    @($disabled | ForEach-Object { '-D' + $_ + '=OFF' })
& cmake @common -B (Join-Path $out 'default') *> (Join-Path $out 'default.log')
if ($LASTEXITCODE -ne 0) { throw 'Default configure failed' }
$cache = [IO.File]::ReadAllText((Join-Path $out 'default/CMakeCache.txt'))
if ($cache -notmatch 'BUILD_FASTDDS_DSOFTBUS_EXPERIMENT:BOOL=OFF') { throw 'Experiment enabled by default' }
if (Test-Path -LiteralPath (Join-Path $out 'default/experiments/fastdds_socket')) { throw 'Experimental subdirectory entered without opt-in' }
$savedErrorPreference = $ErrorActionPreference
try {
    # Windows PowerShell represents expected native stderr as ErrorRecords.
    # Preserve them in the log and assert the real process exit below.
    $ErrorActionPreference = 'Continue'
    & cmake @common -B (Join-Path $out 'source-rejected') -DBUILD_FASTDDS_DSOFTBUS_EXPERIMENT=ON *> (Join-Path $out 'source-rejected.log')
    $rejectedExit = $LASTEXITCODE
} finally { $ErrorActionPreference = $savedErrorPreference }
if ($rejectedExit -eq 0) { throw 'Experiment accepted SOURCE_PROBE fake binding' }
if ([IO.File]::ReadAllText((Join-Path $out 'source-rejected.log')) -notmatch 'requires MDDS_FASTDDS_MODE=CONFIG') {
    throw 'Wrong failure: real Fast DDS requirement was not enforced'
}
'PASS: default OFF; no experimental target without opt-in; SOURCE_PROBE explicitly rejected'
