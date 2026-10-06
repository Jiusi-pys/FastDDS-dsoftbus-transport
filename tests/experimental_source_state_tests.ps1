# Execute the actual source-identity construction block, without SDK/compiler calls.
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = [IO.File]::ReadAllText((Join-Path $repo 'scripts/BuildFastDdsSocketExperiment.ps1'))
$begin = $source.IndexOf('    # Source identity begins')
if ($begin -lt 0) { $begin = $source.IndexOf('    $experimentSourceState =') }
$end = $source.IndexOf('    $experimentSourceStatePath =', $begin)
if ($begin -lt 0 -or $end -lt $begin) { throw 'Actual source construction block not found' }
$construction = [scriptblock]::Create($source.Substring($begin, $end-$begin))
function git {
    $script:sourceCalls++
    $global:LASTEXITCODE = 0
    if ($script:sourceCalls -eq $script:failCall) { $global:LASTEXITCODE = 128; return }
    if ($args[0] -eq 'rev-parse') {
        if ($script:badHead) { return 'invalid-head' }
        return ('f' * 40)
    }
    # A clean checkout may legitimately have empty status/diff/untracked output.
}
$failures = 0
foreach ($case in @('head-error','status-error','diff-error','untracked-error','bad-head','success')) {
    $script:sourceCalls = 0; $script:badHead = $case -eq 'bad-head'
    $script:failCall = (@('head-error','status-error','diff-error','untracked-error').IndexOf($case)) + 1
    $experimentSourceState = $null; $thrown = $false
    try { . $construction } catch { $thrown = $true }
    $pass = if ($case -eq 'success') {
        -not $thrown -and $sourceCalls -eq 4 -and $experimentSourceState.head -ceq ('f' * 40)
    } else { $thrown }
    Write-Output "source_state_case=$case pass=$pass scope=HOST_ONLY_NO_SDK"
    if (-not $pass) { $failures++ }
}
if ($failures) { exit 1 }
exit 0
