param([Parameter(Mandatory)][string]$Driver,[Parameter(Mandatory)][string]$DriverScript,
      [Parameter(Mandatory)][string]$Python,[Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference='Stop'; Set-StrictMode -Version Latest
if(-not [IO.Path]::IsPathRooted($Driver) -or [IO.Path]::GetFileName($Driver) -cne 'fake-hdc.exe'){throw 'Explicit local PE fixture only'}
if((Get-FileHash $Driver).Hash.ToLowerInvariant() -cne 'db7ae6830317e94976954d34a874d801c81eb189b824a876742d598853f83bd3'){throw 'Unknown local shim'}
New-Item -ItemType Directory -Path $OutputRoot -ErrorAction Stop | Out-Null
$windowRoot=[IO.Path]::GetFullPath($OutputRoot);$hdcPath=$Driver;$utf8=[Text.UTF8Encoding]::new($false)
$cfg=[pscustomobject]@{targets=[pscustomobject]@{A='SIM-A';B='SIM-B'}}
$env:FAKE_HDC_PYTHON=$Python;$env:FAKE_HDC_SCRIPT=$DriverScript
$env:FAKE_HDC_RAW_LOG=Join-Path $windowRoot 'raw.log'
$env:FAKE_HDC_SANDBOX=Join-Path $windowRoot 'sandbox'
$env:FAKE_HDC_TEST_RUN='0000000000000001'
$env:FAKE_HDC_WINDOW_ROOT=$windowRoot
New-Item -ItemType Directory -Path (Join-Path $env:FAKE_HDC_SANDBOX 'platform') | Out-Null
[IO.File]::WriteAllText((Join-Path $env:FAKE_HDC_SANDBOX 'platform/libsoftbus_client.z.so'),'synthetic-library-bytes',$utf8)
$source=Join-Path $PSScriptRoot '../../scripts/public_socket/shared_reattach/executor/run-window.ps1'
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile([IO.Path]::GetFullPath($source),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Formal parser syntax invalid'}
foreach($name in @('Invoke-HdcLogged','Test-RemoteCommandReceipt','Invoke-Remote')){
    $node=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true)
    if(-not $node){throw "Missing actual formal function $name"}
    $definition=$node.Extent.Text
    if($name -ceq 'Invoke-HdcLogged'){$definition=$definition.Replace('function Invoke-HdcLogged','function Invoke-ActualHdcLogged')}
    . ([scriptblock]::Create($definition))
}
# Fault fixtures alter only the parser input after the actual PE/driver returns;
# original stdout/stderr/exit receipts remain untouched.
$script:fixtureMode='none'
function Invoke-HdcLogged {param($Board,$Step,$NativeArguments)
    $result=Invoke-ActualHdcLogged -Board $Board -Step $Step -NativeArguments $NativeArguments
    if($script:fixtureMode -ceq 'missing'){$result.Text=[regex]::Replace($result.Text,'(?m)^MDDS_REMOTE_EXIT=.*(?:\r?\n|$)','')}
    if($script:fixtureMode -ceq 'duplicate'){$result.Text+="`nMDDS_REMOTE_EXIT=0`n"}
    if($script:fixtureMode -ceq 'conflict'){$result.Text+="`nMDDS_REMOTE_EXIT=1`n"}
    [IO.File]::WriteAllText((Join-Path $result.Root 'parser-input.txt'),[string]$result.Text,$utf8)
    return $result
}
$cases=@(
 @{name='T1';command='sha256sum /system/lib64/platformsdk/libsoftbus_client.z.so';fault='none';reason='NONE';driver=0;remote='0'},
 @{name='T2';command='sha256sum /system/lib64/libc++.so';fault='none';reason='REMOTE_EXIT_FAILED';driver=0;remote='1'},
 @{name='T3';command='unsupported-command';fault='none';reason='HDC failure';driver=66;remote=$null},
 @{name='T4';command='sha256sum /system/lib64/platformsdk/libsoftbus_client.z.so';fault='missing';reason='REMOTE_EXIT_MARKER_MISSING_OR_DUPLICATE';driver=0;remote=$null},
 @{name='T5-duplicate';command='sha256sum /system/lib64/platformsdk/libsoftbus_client.z.so';fault='duplicate';reason='REMOTE_EXIT_MARKER_MISSING_OR_DUPLICATE';driver=0;remote=$null},
 @{name='T5-conflict';command='sha256sum /system/lib64/platformsdk/libsoftbus_client.z.so';fault='conflict';reason='REMOTE_EXIT_MARKER_MISSING_OR_DUPLICATE';driver=0;remote=$null}
)
$results=@();$failed=0
foreach($case in $cases){
 $script:fixtureMode=$case.fault;$errorText=$null
 try{Invoke-Remote -Board A -Step $case.name -CommandText $case.command | Out-Null}catch{$errorText=$_.Exception.Message}
 $root=Join-Path $windowRoot ('A/'+$case.name);$rc=[int](Get-Content -Raw (Join-Path $root 'exit-code.txt'))
 $raw=Get-Content -Raw (Join-Path $root 'stdout.log');$rawCodes=@()
 if($null -ne $raw){$rawCodes=@([regex]::Matches($raw,'(?m)^MDDS_REMOTE_EXIT=([0-9]+)\s*$'))}
 $pass=($rc -eq $case.driver)
 if($case.reason -ceq 'NONE'){$pass=$pass -and $null -eq $errorText}else{$pass=$pass -and $null -ne $errorText -and $errorText.Contains($case.reason)}
 if($null -ne $case.remote){$pass=$pass -and $rawCodes.Count -eq 1 -and $rawCodes[0].Groups[1].Value -ceq $case.remote}
 if($case.name -ceq 'T3'){$pass=$pass -and $rawCodes.Count -eq 0 -and ((Get-Content -Raw (Join-Path $root 'stderr.log')) -match 'FAKE_HDC_REJECT')}
 $results+=@{case=$case.name;pass=$pass;driver_exit=$rc;error=$errorText;raw_marker_count=$rawCodes.Count}
 if(-not $pass){$failed++}
}
[IO.File]::WriteAllText((Join-Path $windowRoot 'RESULT.json'),($results|ConvertTo-Json -Depth 5),$utf8)
Write-Output ($results|ConvertTo-Json -Depth 5)
if($failed){exit 1}
