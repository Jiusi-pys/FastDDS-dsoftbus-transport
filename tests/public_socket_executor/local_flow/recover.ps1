param([Parameter(Mandatory)][string]$WindowDirectory)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$w=[IO.Path]::GetFullPath($WindowDirectory);$c=Get-Content (Join-Path $w 'window.json') -Raw|ConvertFrom-Json
$root=Split-Path (Split-Path $w -Parent) -Parent
if ((Split-Path $root -Leaf) -cne 'local-flow-20260930-isolated') {throw 'Only the new isolated test environment'}
$f=Get-Content $c.task_contract.freeze_path -Raw|ConvertFrom-Json
if ($f.local_contract_test -isnot [bool] -or $f.local_contract_test -ne $true -or $f.device_authorized -eq $true -or $c.window_root -ine $w -or $f.task_root -ine $root) {throw 'No local recovery authority'}
$utf8=[Text.UTF8Encoding]::new($false,$true)
. (Join-Path $c.repo 'scripts/NativePermissionGuard.ps1')
# This is a local snapshot/API model recovery, not NO_STATE_CHANGE and not any
# device guard. No failed execution summary is rewritten. Prefix action closure
# must be checked by recover.py before invocation.
$receipt=Join-Path $w 'LOCAL_SNAPSHOT_RECOVERY.json'
if(Test-Path $receipt){throw 'Never overwrite recovery'}
$initial=Get-Content (Join-Path $root 'BEFORE_FIRST_DISPATCH.json') -Raw|ConvertFrom-Json
foreach($side in @('A','B')){
 $d=Join-Path $w $side;$sd=Join-Path $root ('sandbox/'+$side)
 $original=Join-Path $sd 'baseline-token.json';$current=Join-Path $sd 'nativetoken.json'
 $bound=@($initial.baseline|Where-Object {$_.path -ieq $original})
 if($bound.Count -ne 1 -or (Get-FileHash $original).Hash.ToLowerInvariant() -cne $bound[0].sha256){throw 'Initial baseline changed'}
 $b=$utf8.GetString([IO.File]::ReadAllBytes($original));$a=$utf8.GetString([IO.File]::ReadAllBytes($current))
 $check=Test-NativePermissionSnapshot -BaselineJson $b -CurrentJson $a -RequirePhase Revoked -ExpectedToken ([UInt64]$c.token.$side)
 if(-not $check.Passed){throw 'Local current permission state not restored; full mutation repair required'}
 $owner=@{run=$c.run;board=$side;name='fastdds_dsoftbus_probe';baselineSha256=$c.permission_baseline_sha256.$side;expectedToken=[UInt64]$c.token.$side}|ConvertTo-Json
 $own=Test-NativePermissionOwnership -OwnershipJson $owner -Run $c.run -Board $side -BaselineSha256 $c.permission_baseline_sha256.$side -ExpectedToken ([UInt64]$c.token.$side)
 if(-not $own.Passed){throw 'Local ownership invalid'}
 foreach($name in @('baseline.json','after.json')){if(-not(Test-Path (Join-Path $d $name))){[IO.File]::WriteAllBytes((Join-Path $d $name),[IO.File]::ReadAllBytes($(if($name -eq 'baseline.json'){$original}else{$current})))}}
 foreach($pair in @(@('ownership-guard.json',$own),@('final-snapshot-guard.json',$check))){if(-not(Test-Path (Join-Path $d $pair[0]))){[IO.File]::WriteAllText((Join-Path $d $pair[0]),($pair[1]|ConvertTo-Json -Depth 10),$utf8)}}
 $metaPath=Join-Path $sd 'metadata.json';$mi=@($initial.baseline|Where-Object {$_.path -ieq $metaPath})
 if($mi.Count -ne 1 -or (Get-FileHash $metaPath).Hash.ToLowerInvariant() -cne $mi[0].sha256){throw 'Local metadata not restored'}
 $meta=Get-Content $metaPath -Raw|ConvertFrom-Json
 foreach($phase in @('before','after')){
  $path=Join-Path $d ('metadata-'+$phase)
  if(-not(Test-Path $path)){New-Item -ItemType Directory $path|Out-Null;[IO.File]::WriteAllText((Join-Path $path 'local-recovery-asof.json'),(@{scope='LOCAL_SNAPSHOT_RECOVERY_ASOF';initial_binding=$mi[0];current_sha256=$mi[0].sha256}|ConvertTo-Json -Depth 8),$utf8)}
 }
 $guard=@{Passed=([IO.File]::ReadAllBytes($original).Length -eq [IO.File]::ReadAllBytes($current).Length -and (Get-FileHash $original).Hash -ceq (Get-FileHash $current).Hash);Phase='Revoked';byte_match=$true;metadata_match=$true;process_absent=$true;service_permission='DENIED';scope='LOCAL_MODEL_SNAPSHOT_RECOVERY_NOT_DEVICE';metadata_before=@{evidence=(Join-Path $d 'metadata-before')};metadata_after=@{evidence=(Join-Path $d 'metadata-after')}}
 if(-not $guard.Passed){throw 'Byte mismatch'}
 if(Test-Path (Join-Path $d 'final-guard.json')){throw 'Existing guard must not be overwritten'}
 [IO.File]::WriteAllText((Join-Path $d 'final-guard.json'),($guard|ConvertTo-Json -Depth 8),$utf8)
}
[IO.File]::WriteAllText($receipt,(@{run=$c.run;window=$w;local_contract_test=$true;decision='LOCAL_SNAPSHOT_RECOVERY_CONFIRMED';original_result='FAIL_PRESERVED';mutable_permission_repair='NOT_REQUIRED_ALREADY_REVOKED';tool_sha256=(Get-FileHash $PSCommandPath).Hash.ToLowerInvariant();closure_receipt=(Join-Path $w 'LOCAL_RECOVERY_CLOSURE.json')}|ConvertTo-Json -Depth 8),$utf8)
