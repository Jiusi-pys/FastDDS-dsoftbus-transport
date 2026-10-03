param(
    [Parameter(Mandatory)][string]$GuardSource,
    [Parameter(Mandatory)][string]$Baseline,
    [Parameter(Mandatory)][string]$Current,
    [Parameter(Mandatory)][string]$Run,
    [Parameter(Mandatory)][ValidateSet('A','B')][string]$Side,
    [Parameter(Mandatory)][UInt64]$Token,
    [Parameter(Mandatory)][string]$Output
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if (Test-Path -LiteralPath $Output) {throw 'Recovery guard output already exists'}
. $GuardSource
$utf8=[Text.UTF8Encoding]::new($false,$true)
$before=[IO.File]::ReadAllBytes($Baseline)
$after=[IO.File]::ReadAllBytes($Current)
$sha=(Get-FileHash -LiteralPath $Baseline -Algorithm SHA256).Hash.ToLowerInvariant()
$owner=@{run=$Run;board=$Side;name='fastdds_dsoftbus_probe';baselineSha256=$sha;expectedToken=$Token}|ConvertTo-Json
$ownership=Test-NativePermissionOwnership -OwnershipJson $owner -Run $Run -Board $Side -BaselineSha256 $sha -ExpectedToken $Token
$snapshot=Test-NativePermissionSnapshot -BaselineJson ($utf8.GetString($before)) -CurrentJson ($utf8.GetString($after)) -RequirePhase Revoked -ExpectedToken $Token
$byteMatch=$sha -ceq (Get-FileHash -LiteralPath $Current -Algorithm SHA256).Hash.ToLowerInvariant()
$result=@{run=$Run;side=$Side;ownership=$ownership;snapshot=$snapshot;byte_match=$byteMatch;permission_write='NOT_PERFORMED';Passed=($ownership.Passed -and $snapshot.Passed -and $byteMatch)}
[IO.File]::WriteAllText($Output,($result|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
if(-not $result.Passed){exit 1}
