param([Parameter(Mandatory)][string]$WindowDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$taskRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\','/')
$windowRoot = [IO.Path]::GetFullPath($WindowDirectory)
if (-not $windowRoot.StartsWith($taskRoot+[IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {throw 'Query destination must belong to this task'}
if (-not (Test-Path -LiteralPath $windowRoot -PathType Container)) {throw 'Prepare the query window before launching'}
$receiptNames = @('launcher.originalcommand.json','launcher.stdout.log','launcher.stderr.log','launcher.child.rc','launcher.rc','launcher.receipt.json')
foreach ($name in $receiptNames) {
    if (Test-Path -LiteralPath (Join-Path $windowRoot $name)) {throw 'Refusing to overwrite query launcher evidence'}
}
$utf8 = [Text.UTF8Encoding]::new($false)
$enginePath = (Get-Process -Id $PID).Path
$childPath = Join-Path $PSScriptRoot 'run-window.ps1'
$childArguments = @('-NoLogo','-NoProfile','-NonInteractive','-File',$childPath,'-WindowDirectory',$windowRoot)
$started = [DateTime]::UtcNow.ToString('o')
$childExit = $null
$launcherExit = 1
$failure = $null
try {
    [IO.File]::WriteAllText((Join-Path $windowRoot 'launcher.originalcommand.json'), ([ordered]@{executable=$enginePath;arguments=$childArguments;cwd=(Get-Location).Path;invocation=$MyInvocation.Line;time_utc=$started} | ConvertTo-Json -Depth 5), $utf8)
    $savedErrorPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $enginePath @childArguments 1> (Join-Path $windowRoot 'launcher.stdout.log') 2> (Join-Path $windowRoot 'launcher.stderr.log')
        $childExit = $LASTEXITCODE
    } finally {$ErrorActionPreference = $savedErrorPreference}
    if ($null -eq $childExit) {throw 'Query child did not return an exit code'}
    $launcherExit = [int]$childExit
} catch {
    $failure = $_.Exception.Message
} finally {
    $childStatus = if ($null -eq $childExit) {'NOT_STARTED_OR_NO_EXIT_CODE'} else {[string]$childExit}
    [IO.File]::WriteAllText((Join-Path $windowRoot 'launcher.child.rc'), $childStatus, $utf8)
    [IO.File]::WriteAllText((Join-Path $windowRoot 'launcher.receipt.json'), ([ordered]@{started_utc=$started;finished_utc=[DateTime]::UtcNow.ToString('o');child_exit_code=$childExit;launcher_exit_code=$launcherExit;failure=$failure;synchronous=$true} | ConvertTo-Json -Depth 5), $utf8)
    [IO.File]::WriteAllText((Join-Path $windowRoot 'launcher.rc'), [string]$launcherExit, $utf8)
}
exit $launcherExit
