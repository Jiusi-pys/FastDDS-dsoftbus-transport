param([Parameter(Mandatory)][string]$WindowDirectory)

$ErrorActionPreference = 'Stop'

Set-StrictMode -Version Latest

$repoRoot = (Get-Location).Path
$windowRoot = [IO.Path]::GetFullPath($WindowDirectory)
$taskRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\','/')
if (-not $windowRoot.StartsWith($taskRoot+[IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {throw 'Query window must belong to this task'}
$cfg = Get-Content -Raw -LiteralPath (Join-Path $windowRoot 'window.json') | ConvertFrom-Json
if ([IO.Path]::GetFullPath($cfg.window_root) -ine $windowRoot) {throw 'Query window destination changed'}
if ([IO.Path]::GetFullPath($cfg.repo) -ine [IO.Path]::GetFullPath($repoRoot)) {throw 'Query repository changed'}
$hdcPath = [string]$cfg.hdc_path
if (-not [IO.Path]::IsPathRooted($hdcPath)) {throw 'Explicit absolute HDC path required'}

$tokenFile = '/data/service/el0/access_token/nativetoken.json'

$packageRoot = [IO.Path]::GetFullPath($cfg.package)
if ($packageRoot -ine (Join-Path $windowRoot 'helpers')) {throw 'Query helper package belongs to another window'}
$utf8Strict = [Text.UTF8Encoding]::new($false, $true)

$utf8 = [Text.UTF8Encoding]::new($false)

$expectedHash = @{A=[string]$cfg.permission_baseline_sha256.A; B=[string]$cfg.permission_baseline_sha256.B}


if (Test-Path -LiteralPath (Join-Path $windowRoot 'EXECUTION_STARTED')) {throw 'Refusing to reuse a window'}

if ($cfg.run -notmatch '^[0-9a-f]{16}$') {throw 'Invalid run'}

if ($cfg.eligible_for_executor_gate -ne $true) {throw 'Current two-board preflight not eligible'}

$contractValidator=Join-Path (Split-Path -Parent $PSScriptRoot) 'executor/task_contract.py'

if ((Get-FileHash -LiteralPath $contractValidator).Hash.ToLowerInvariant() -cne $cfg.contract_validator_sha256) {throw 'Query contract validator changed'}

foreach ($name in @('task-contract-check.stdout.log','task-contract-check.stderr.log')) {

    if (Test-Path -LiteralPath (Join-Path $windowRoot $name)) {throw 'Refusing contract check evidence overwrite'}

}

# Bind the validator to the reviewed contract before executing Python code.
$proofContract = [IO.Path]::GetFullPath([string]$cfg.task_contract.freeze_path)
if ((Split-Path -Parent $proofContract) -ine $taskRoot -or (Get-FileHash -LiteralPath $proofContract).Hash.ToLowerInvariant() -cne $cfg.task_contract.freeze_sha256) {throw 'Contract bootstrap identity changed'}
$bootstrapFreeze = Get-Content -Raw -LiteralPath $proofContract | ConvertFrom-Json
# ---- bootstrap mode dispatch ------------------------------------------------
# StrictMode stays on. Optional contract fields are probed for existence before
# their value is read, so a device contract that legitimately omits the local
# fields cannot raise PropertyNotFoundException. No condition is shared between
# the two modes: each branch validates itself fully, so one branch can never mask
# an illegal state in the other. Nothing here converts a non-boolean into a
# permission.
function Read-ContractBool([object]$Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) { return $null }
    if ($property.Value -isnot [bool]) { throw "Contract field '$Name' must be a JSON boolean" }
    return [bool]$property.Value
}
function Read-ContractString([object]$Object, [string]$Name, [bool]$Required) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        if ($Required) { throw "Contract field '$Name' is required" }
        return $null
    }
    if ($property.Value -isnot [string] -or ([string]$property.Value).Length -eq 0) {
        throw "Contract field '$Name' must be a non-empty string"
    }
    return [string]$property.Value
}
if ([IO.Path]::GetFullPath([string]$bootstrapFreeze.task_root) -ine $taskRoot) {throw 'Execution contract bound to another task'}
$bootstrapLocal   = Read-ContractBool $bootstrapFreeze 'local_contract_test'
$bootstrapAdmit   = Read-ContractBool $bootstrapFreeze 'execution_admitted'
$bootstrapDeviceE = Read-ContractBool $bootstrapFreeze 'device_evidence'
$bootstrapDeviceA = Read-ContractBool $bootstrapFreeze 'device_authorized'
if ($null -eq $bootstrapLocal -or $bootstrapLocal -eq $false) {
    # Device mode. The admission rule is unchanged from the reviewed contract.
    if ($bootstrapDeviceE -eq $true) {throw 'Device contract may not carry local-test authority fields'}
    if ($bootstrapFreeze.scope -in @('INSTALLED_RUNTIME_DUAL_BOARD_CONSUMER','ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_ACCEPTANCE','DDS214_LAYER_BASELINE_ACCEPTANCE')) {
        if ($bootstrapDeviceA -ne $true -or $bootstrapFreeze.PSObject.Properties['local_driver'] -or $bootstrapFreeze.PSObject.Properties['local_test_basis']) {throw 'Installed device authorization/driver boundary'}
    } elseif ($bootstrapDeviceA -eq $true) {throw 'Device contract may not carry local-test authority fields'}
    if ($bootstrapAdmit -ne $true) {throw 'Execution contract not admitted'}
} else {
    # Local contract test mode. It claims no device authority of any kind and it
    # may not hold the device admission record.
    if ($bootstrapDeviceE -eq $true) {throw 'A local contract test may not claim device evidence'}
    if ($bootstrapDeviceA -eq $true) {throw 'A local contract test may not claim device authorization'}
    if ($bootstrapAdmit -eq $true) {throw 'A local contract test may not claim the device admission record'}
    $null = Read-ContractString $bootstrapFreeze 'local_test_basis' $true
    $localDriverProperty = $bootstrapFreeze.PSObject.Properties['local_driver']
    if ($null -eq $localDriverProperty -or $null -eq $localDriverProperty.Value) {throw 'Local contract test needs a declared local driver'}
    $localDriver = $localDriverProperty.Value
    if ((Read-ContractString $localDriver 'kind' $true) -cne 'LOCAL_FAKE_SANDBOX') {throw 'Unsupported local driver kind'}
    $localTestRoot  = [IO.Path]::GetFullPath((Read-ContractString $localDriver 'test_root' $true))
    $localSandbox   = [IO.Path]::GetFullPath((Read-ContractString $localDriver 'sandbox_root' $true))
    if (-not $localTestRoot.Equals($taskRoot, [StringComparison]::OrdinalIgnoreCase)) {throw 'Local test root must be this task root'}
    if (-not $localSandbox.StartsWith($localTestRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {throw 'Local sandbox root must live inside the local test root'}
    # Bind the device tool this window will actually execute, before any
    # subcommand is spawned. Selecting the real device tool is refused here.
    $driverName = [IO.Path]::GetFileName($hdcPath)
    if ($driverName -ieq 'hdc' -or $driverName -ieq 'hdc.exe') {throw 'Local contract test must not select the device tool'}
    if (-not $hdcPath.StartsWith($localTestRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {throw 'Local driver must live inside the declared local test root'}
    if (-not (Test-Path -LiteralPath $hdcPath -PathType Leaf)) {throw 'Local driver is missing'}
}

$bootstrapValidator = Join-Path $taskRoot 'executor/task_contract.py'
$validatorBindings = @($bootstrapFreeze.execution_files | Where-Object {
    $entryPath = [string]$_.path
    if (-not [IO.Path]::IsPathRooted($entryPath)) {$entryPath = Join-Path $repoRoot $entryPath}
    [IO.Path]::GetFullPath($entryPath) -ieq [IO.Path]::GetFullPath($bootstrapValidator)
})
if ($validatorBindings.Count -ne 1 -or (Get-FileHash -LiteralPath $bootstrapValidator).Hash.ToLowerInvariant() -cne $validatorBindings[0].sha256) {throw 'Validator not bound by reviewed execution closure'}
& python $contractValidator --check-query (Join-Path $windowRoot 'window.json') 1> (Join-Path $windowRoot 'task-contract-check.stdout.log') 2> (Join-Path $windowRoot 'task-contract-check.stderr.log')

if ($LASTEXITCODE -ne 0) {throw 'Query device contract not frozen; no device operations'}

if ($cfg.scenario -cne 'readonly-directory-query') {throw 'Unauthorized query mode'}

foreach ($binding in @(@($cfg.preflight_config,$cfg.preflight_config_sha256),@($cfg.permission_helper.path,$cfg.permission_helper.sha256),@($cfg.query_helper.path,$cfg.query_helper.sha256),@($cfg.helper_provenance,$cfg.helper_provenance_sha256))) {

    if ((Get-FileHash -LiteralPath $binding[0]).Hash.ToLowerInvariant() -cne $binding[1]) {throw 'Frozen input changed'}

}

$currentPreflight = Get-Content -Raw -LiteralPath $cfg.preflight_config | ConvertFrom-Json

if ($currentPreflight.eligible_for_executor_gate -ne $true) {throw 'Current baseline not eligible'}

foreach ($property in $cfg.shared_script_sha256.PSObject.Properties) {

    if ((Get-FileHash -LiteralPath (Join-Path $repoRoot ('scripts/'+$property.Name))).Hash.ToLowerInvariant() -cne $property.Value) {throw 'Shared guard/harness changed'}

}

foreach ($property in $cfg.executor_sha256.PSObject.Properties) {

    if ((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $property.Name)).Hash.ToLowerInvariant() -cne $property.Value) {throw 'Executor changed after window freeze'}

}

foreach ($board in @('A','B')) {

    if ($cfg.remote.$board -cne ('/data/local/tmp/mdds_dds_'+$cfg.run+'_'+$board)) {throw 'Unbound deployment path'}
}

$packageFiles = @(Get-ChildItem -LiteralPath $packageRoot -File)

if ($packageFiles.Count -ne 2) {throw 'Expected exactly two query/permission helpers'}

foreach ($artifact in $cfg.package_artifacts) {

    if ((Get-FileHash -LiteralPath (Join-Path $packageRoot $artifact.name)).Hash.ToLowerInvariant() -cne $artifact.sha256) {throw 'Frozen package artifact changed'}

}

# Load project code only after contract, dependency and guard hashes pass.
. (Join-Path $repoRoot 'scripts\NativePermissionGuard.ps1')
if ($bootstrapLocal -eq $true -or $bootstrapFreeze.scope -in @('INSTALLED_RUNTIME_DUAL_BOARD_CONSUMER','ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_ACCEPTANCE','DDS214_LAYER_BASELINE_ACCEPTANCE')) {
    & python $contractValidator --claim-query (Join-Path $windowRoot 'window.json')
    if ($LASTEXITCODE -ne 0) {throw 'Local test query claim refused'}
}
[IO.File]::WriteAllText((Join-Path $windowRoot 'EXECUTION_STARTED'), [DateTime]::UtcNow.ToString('o'), $utf8)



function Invoke-HdcLogged([string]$Board, [string]$Step, [string[]]$NativeArguments) {

    if ($Step -ceq 'directory-query' -and $bootstrapFreeze.scope -in @('ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_ACCEPTANCE','DDS214_LAYER_BASELINE_ACCEPTANCE')) {
        & python $contractValidator --admit-ros-query-dispatch (Join-Path $windowRoot 'window.json') --side $Board | Out-Null
        if ($LASTEXITCODE -ne 0) {throw 'Original ROS query claim refused at driver boundary; no dispatch'}
    }

    if ($NativeArguments.Count -lt 1) {throw 'Missing HDC subcommand'}

    $stepRoot = Join-Path $windowRoot ($Board+'/'+$Step)

    New-Item -ItemType Directory -Path $stepRoot -ErrorAction Stop | Out-Null

    $commandArguments = @('-t', [string]$cfg.targets.$Board) + $NativeArguments

    [IO.File]::WriteAllText((Join-Path $stepRoot 'command.json'), (@($hdcPath)+$commandArguments | ConvertTo-Json), $utf8)

    # Diagnostic redirection only. Persistent JSON is transferred with file recv.

    $savedErrorPreference = $ErrorActionPreference

    try {

        $ErrorActionPreference = 'Continue'

        & $hdcPath @commandArguments 1> (Join-Path $stepRoot 'stdout.log') 2> (Join-Path $stepRoot 'stderr.log')

        $nativeExit = $LASTEXITCODE

    } finally {$ErrorActionPreference = $savedErrorPreference}

    [IO.File]::WriteAllText((Join-Path $stepRoot 'exit-code.txt'), [string]$nativeExit, $utf8)

    if ($nativeExit -ne 0) {throw "HDC failure: $Board/$Step rc=$nativeExit"}

    return [pscustomobject]@{ Root=$stepRoot; Text=(Get-Content -Raw -LiteralPath (Join-Path $stepRoot 'stdout.log')) }

}

function Invoke-Remote([string]$Board, [string]$Step, [string]$CommandText) {

    $wrapped = $CommandText + '; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc'

    $record = Invoke-HdcLogged -Board $Board -Step $Step -NativeArguments @('shell', $wrapped)

    if ($null -eq $record.Text -or $record.Text -isnot [string] -or $record.Text.Length -eq 0) {throw "QUERY_RECEIPT_INCOMPLETE: $Board/$Step NULL_EMPTY_OR_INVALID_TEXT"}
    $codes = [regex]::Matches($record.Text, '(?m)^MDDS_REMOTE_EXIT=([0-9]+)\s*$')

    if ($codes.Count -ne 1 -or $codes[0].Groups[1].Value -ne '0') {throw "Remote failure: $Board/$Step"}

    return $record

}

function Receive-Token([string]$Board, [string]$Step) {

    $destination = Join-Path $windowRoot ($Board+'/'+$Step+'.json')

    if (Test-Path -LiteralPath $destination) {throw 'Refusing JSON overwrite'}

    Invoke-HdcLogged -Board $Board -Step ($Step+'-recv') -NativeArguments @('file','recv',$tokenFile,$destination) | Out-Null

    if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {throw 'No raw token file'}

    return $destination

}

function Guard-Token([string]$Board, [string]$Phase, [string]$Baseline, [string]$Current, [string]$Step) {

    $checked = Test-NativePermissionSnapshot -BaselineJson ($utf8Strict.GetString([IO.File]::ReadAllBytes($Baseline))) -CurrentJson ($utf8Strict.GetString([IO.File]::ReadAllBytes($Current))) -RequirePhase $Phase -ExpectedToken ([UInt64]$cfg.token.$Board)

    [IO.File]::WriteAllText((Join-Path $windowRoot ($Board+'/'+$Step+'.json')), ($checked | ConvertTo-Json -Depth 8), $utf8)

    [Console]::WriteLine("GUARD board=$Board phase=$Phase code=$($checked.Code)")

    if (-not $checked.Passed) {throw "Guard failed: $Board/$Step $($checked.Code)"}

}

function Read-Metadata([string]$Board, [string]$Step) {

    $command = "test -f $tokenFile && test ! -L $tokenFile && stat -c 'META=%F|%a|%u|%g|%s|%i|%Y|%Z' $tokenFile && ls -lZ $tokenFile && getenforce"

    $record = Invoke-Remote -Board $Board -Step $Step -CommandText $command

    $stat = [regex]::Match($record.Text, '(?m)^META=([^|\r\n]+)\|([0-9]+)\|([0-9]+)\|([0-9]+)\|([0-9]+)\|([0-9]+)\|([0-9]+)\|([0-9]+)\s*$')

    $label = [regex]::Match($record.Text, 'u:object_r:[a-zA-Z0-9_]+:s0')

    $enforcing = [regex]::Match($record.Text, '(?m)^(Enforcing|Permissive|Disabled)\s*$')

    if (-not $stat.Success -or -not $label.Success -or -not $enforcing.Success) {throw "Incomplete metadata: $Board/$Step"}

    $metadata = [pscustomobject]@{kind=$stat.Groups[1].Value;mode=$stat.Groups[2].Value;uid=$stat.Groups[3].Value;gid=$stat.Groups[4].Value;size=$stat.Groups[5].Value;inode=$stat.Groups[6].Value;mtime=$stat.Groups[7].Value;ctime=$stat.Groups[8].Value;label=$label.Value;enforcement=$enforcing.Groups[1].Value;evidence=$record.Root}

    if ($metadata.kind -cne 'regular file' -or $metadata.mode -cne '640' -or $metadata.uid -cne '0' -or $metadata.gid -cne '3020' -or $metadata.label -cne 'u:object_r:accesstoken_data_file:s0' -or $metadata.enforcement -cne 'Enforcing') {throw "Metadata mismatch: $Board/$Step"}

    return $metadata

}

function Assert-ProcessAbsent([string]$Board, [string]$Step) {

    $record = Invoke-Remote -Board $Board -Step $Step -CommandText 'ps -A'

    if ($record.Text -match 'fastdds_socket|public_socket|native_permiss|softbus_record|softbus_recor|mdds_installed') {throw "Experiment or permission process still present on $Board"}

}

function Assert-Platform([string]$Board, [string]$Phase) {

    $index=0

    foreach ($file in $cfg.platform.$Board.files) {

        $record=Invoke-Remote -Board $Board -Step ($Phase+'-platform-file-'+$index) -CommandText ('sha256sum '+$file.path)

        if ($record.Text -notmatch ('(?m)^'+$file.sha256+'\s+'+[regex]::Escape($file.path)+'\s*$')) {throw "Platform hash changed: $Board/$($file.path)"}

        $index++

    }

    $index=0

    foreach ($parameter in $cfg.platform.$Board.parameters) {

        $command=if ($parameter.name -ceq 'kernel') {'uname -a'} else {'param get '+$parameter.name}

        $record=Invoke-Remote -Board $Board -Step ($Phase+'-platform-param-'+$index) -CommandText $command

        $actual=($record.Text -replace '(?m)^MDDS_REMOTE_EXIT=0\s*$','').Trim()

        if ($actual -cne $parameter.value) {throw "Platform parameter changed: $Board/$($parameter.name)"}

        $index++

    }

    $server=$cfg.platform.$Board.service_server

    $record=Invoke-Remote -Board $Board -Step ($Phase+'-platform-server') -CommandText ('sha256sum '+$server.path)

    if ($record.Text -notmatch ('(?m)^'+$server.sha256+'\s+'+[regex]::Escape($server.path)+'\s*$')) {throw "Platform service changed: $Board"}

}

function Invoke-Permission([string]$Board,[string]$Step,[string]$Action) {

    $root=[string]$cfg.remote.$Board

    $loader=$root+':'+($cfg.system_library_dirs -join ':')

    return Invoke-Remote -Board $Board -Step $Step -CommandText ('env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH='+$loader+' '+$root+'/native_permission_probe --'+$Action+' --expected-token '+$cfg.token.$Board+' --preflight-confirmed')

}

function Assert-ExecutorTools([string]$Board) {

    $command='env -i PATH=/system/bin:/system/xbin /system/bin/sh -c ''for mdds_tool in cat readlink stat sha256sum mv env timeout; do command -v "$mdds_tool" || exit 3; done'''

    Invoke-Remote -Board $Board -Step 'declared-tool-availability' -CommandText $command | Out-Null

}



$baselines = @{}; $beforeMetadata = @{}

try {

foreach ($board in @('A','B')) {

    Assert-Platform -Board $board -Phase 'baseline'

    Assert-ExecutorTools -Board $board

    Assert-ProcessAbsent -Board $board -Step 'processes-before'

    $beforeMetadata[$board] = Read-Metadata -Board $board -Step 'metadata-before'

    $baselines[$board] = Receive-Token -Board $board -Step 'baseline'

    if ((Get-FileHash -LiteralPath $baselines[$board]).Hash.ToLowerInvariant() -cne $expectedHash[$board]) {throw "Unknown baseline $board"}

    Guard-Token -Board $board -Phase Revoked -Baseline $baselines[$board] -Current $baselines[$board] -Step 'baseline-guard'

    $ownership = @{run=$cfg.run;board=$board;name='fastdds_dsoftbus_probe';baselineSha256=$expectedHash[$board];expectedToken=[UInt64]$cfg.token.$board} | ConvertTo-Json

    [IO.File]::WriteAllText((Join-Path $windowRoot ($board+'/ownership.json')), $ownership, $utf8)

    $owned = Test-NativePermissionOwnership -OwnershipJson $ownership -Run $cfg.run -Board $board -BaselineSha256 $expectedHash[$board] -ExpectedToken ([UInt64]$cfg.token.$board)

    [IO.File]::WriteAllText((Join-Path $windowRoot ($board+'/ownership-guard.json')), ($owned | ConvertTo-Json), $utf8)

    if (-not $owned.Passed) {throw "Ownership check failed $board"}

    Invoke-Remote -Board $board -Step 'mkdir' -CommandText ('test ! -e '+$cfg.remote.$board+' && mkdir '+$cfg.remote.$board) | Out-Null

    foreach ($file in $packageFiles) {

        Invoke-HdcLogged -Board $board -Step ('deploy-'+$file.Name) -NativeArguments @('file','send',$file.FullName,($cfg.remote.$board+'/'+$file.Name)) | Out-Null

    }

    Invoke-Remote -Board $board -Step 'helper-executable' -CommandText ('chmod 700 '+$cfg.remote.$board+'/native_permission_probe') | Out-Null

    Invoke-Remote -Board $board -Step 'executable' -CommandText ('chmod 700 '+$cfg.remote.$board+'/softbus_record_probe && test -x '+$cfg.remote.$board+'/softbus_record_probe') | Out-Null

    foreach ($file in $packageFiles) {

        $remoteFile = $cfg.remote.$board+'/'+$file.Name

        $record = Invoke-Remote -Board $board -Step ('hash-'+$file.Name) -CommandText ('sha256sum '+$remoteFile)

        $localHash = (Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()

        if ($record.Text -notmatch ('(?m)^'+$localHash+'\s+'+[regex]::Escape($remoteFile)+'\s*$')) {throw "Package mismatch $board/$($file.Name)"}

    }

}

} catch {

    [IO.File]::WriteAllText((Join-Path $windowRoot 'window-summary.json'),(@{first_failure=$_.Exception.Message;failure_stage='PREFLIGHT_OR_DEPLOY';cleanup_failures=@();attempted_grants=@();permission_restore='NOT_REQUIRED_NO_GRANT';process_outcome='NO_BUSINESS_PROCESS_STARTED'} | ConvertTo-Json -Depth 8),$utf8)

    throw

}

$attempted = [Collections.Generic.List[string]]::new()

$windowFailure = $null; $cleanupFailures = [Collections.Generic.List[string]]::new()

try {

    foreach ($board in @('A','B')) {

        Assert-Platform -Board $board -Phase 'adjacent'

        $adjacent = Invoke-Remote -Board $board -Step 'adjacent-baseline-hash' -CommandText ('sha256sum '+$tokenFile)

        if ($adjacent.Text -notmatch $expectedHash[$board]) {throw "Baseline changed before grant $board"}

        Assert-ProcessAbsent -Board $board -Step 'processes-pregrant'

        $attempted.Add($board)

        $grant = Invoke-Permission -Board $board -Step 'grant' -Action 'grant'

        if ($grant.Text -notmatch 'verification_permission=0' -or $grant.Text -notmatch 'result=PASS') {throw "Grant verification failed $board"}

        $grantedFile = Receive-Token -Board $board -Step 'granted'

        Guard-Token -Board $board -Phase Granted -Baseline $baselines[$board] -Current $grantedFile -Step 'granted-guard'

    }

    foreach ($board in @('A','B')) {

        $root=[string]$cfg.remote.$board

        $loader=$root+':'+($cfg.system_library_dirs -join ':')

        if ($board -ceq 'A' -and $cfg.PSObject.Properties['work_package'] -and $cfg.work_package -ceq 'N10_OBSERVED_RETRY_AND_CONDITIONAL_N100') {
            & python (Join-Path (Split-Path -Parent $PSScriptRoot) 'executor/task_contract.py') --claim-query (Join-Path $windowRoot 'window.json')
            if ($LASTEXITCODE -ne 0) {throw 'Query quota not admitted'}
        }
        $identityOption=''
        if ($cfg.PSObject.Properties['identity_qualification'] -and $cfg.identity_qualification -eq $true) {$identityOption=' --check-identities --identity-output '+$cfg.private_identity_output.$board}
        $query=Invoke-Remote -Board $board -Step 'directory-query' -CommandText ('env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH='+$loader+' timeout 10 '+$root+'/softbus_record_probe --list-nodes'+$identityOption+' --expected-token '+$cfg.token.$board+' --preflight-confirmed')

        if ($query.Text -notmatch '(?m)^result=PASS scope=READONLY_ROUTE_QUERY .*cleanup=NO_SOCKET_RESOURCES token_restored=1 ') {throw "Read-only directory query failed $board"}

    }

} catch {

    $windowFailure = $_.Exception.Message

    [IO.File]::WriteAllText((Join-Path $windowRoot 'window-failure.txt'), $windowFailure, $utf8)

} finally {

    foreach ($board in $attempted) {

        try {

            $preRevoke = Receive-Token -Board $board -Step 'pre-revoke'

            # Guard full ownership before the compensating API write. A failed

            # grant may have left either of the two permitted permission states.

            try {Guard-Token -Board $board -Phase Granted -Baseline $baselines[$board] -Current $preRevoke -Step 'pre-revoke-granted-guard'}

            catch {Guard-Token -Board $board -Phase Revoked -Baseline $baselines[$board] -Current $preRevoke -Step 'pre-revoke-revoked-guard'}

            $revoked = Invoke-Permission -Board $board -Step 'revoke' -Action 'revoke'

            if ($revoked.Text -notmatch 'verification_permission=-1' -or $revoked.Text -notmatch 'result=PASS') {throw "Revoke verification failed $board"}

            $afterFile = Receive-Token -Board $board -Step 'after'

            Guard-Token -Board $board -Phase Revoked -Baseline $baselines[$board] -Current $afterFile -Step 'final-snapshot-guard'

            $afterMetadata = Read-Metadata -Board $board -Step 'metadata-after'

            Assert-ProcessAbsent -Board $board -Step 'processes-after'

            $byteMatch = (Get-FileHash -LiteralPath $afterFile).Hash -ceq (Get-FileHash -LiteralPath $baselines[$board]).Hash

            $metaBeforeSignature = $beforeMetadata[$board] | Select-Object kind,mode,uid,gid,label,enforcement | ConvertTo-Json -Compress

            $metaAfterSignature = $afterMetadata | Select-Object kind,mode,uid,gid,label,enforcement | ConvertTo-Json -Compress

            $metaMatch = $metaBeforeSignature -ceq $metaAfterSignature

            [IO.File]::WriteAllText((Join-Path $windowRoot ($board+'/final-guard.json')), (@{Passed=($byteMatch -and $metaMatch);Phase='Revoked';byte_match=$byteMatch;metadata_match=$metaMatch;metadata_before=$beforeMetadata[$board];metadata_after=$afterMetadata;inode_changed=($beforeMetadata[$board].inode -cne $afterMetadata.inode);timestamps_comparison='OBSERVED_NOT_REQUIRED_EQUAL_AFTER_API_WRITE';service_permission='DENIED';process_absent=$true} | ConvertTo-Json -Depth 8), $utf8)

            if (-not $byteMatch -or -not $metaMatch) {throw "Final restoration mismatch $board"}

        } catch {

            $cleanupFailures.Add($board+': '+$_.Exception.Message)

        }

    }

}

# Query outputs are already retained in the registered command receipts.

# Stop all device operations after any restoration failure.

# Private identity files belong only to this exact fresh query window. Collect
# after both permission guards restore, then remove only the verified inode.
# No raw identity bytes enter diagnostic stdout or the public package.
if ($cleanupFailures.Count -eq 0 -and $cfg.PSObject.Properties['identity_qualification'] -and $cfg.identity_qualification -eq $true) {
    $identityRows=@{}
    foreach ($board in @('A','B')) {
        try {
            $output=[string]$cfg.private_identity_output.$board
            $expectedOutput=[string]$cfg.remote.$board+'/local-udid.bin'
            if ($output -cne $expectedOutput) {throw 'Private identity path drift'}
            $probe=Invoke-Remote -Board $board -Step 'identity-file-metadata' -CommandText ('if test ! -e '+$output+' && test ! -L '+$output+'; then echo MDDS_IDENTITY_ABSENT=1; else test -f '+$output+' && test ! -L '+$output+" && stat -c 'MDDS_IDENTITY mode=%a uid=%u bytes=%s links=%h inode=%i' "+$output+' && sha256sum '+$output+" && stat -c 'MDDS_IDENTITY_PARENT uid=%u' "+$cfg.remote.$board+'; fi')
            if ($probe.Text -match '(?m)^MDDS_IDENTITY_ABSENT=1\s*$') {
                $identityRows[$board]=@{present=$false;remote_absent=$true;path=$output}
                if (-not $windowFailure) {throw 'Required private identity missing'}
                continue
            }
            if ($probe.Text -notmatch '(?m)^MDDS_IDENTITY mode=600 uid=([0-9]+) bytes=([1-9][0-9]?) links=1 inode=([1-9][0-9]*)\s*$') {throw 'Unsafe private identity metadata'}
            $identityUid=$Matches[1];$identitySize=[int]$Matches[2];$identityInode=$Matches[3]
            if ($identitySize -gt 64 -or $probe.Text -notmatch ('(?m)^MDDS_IDENTITY_PARENT uid='+[regex]::Escape($identityUid)+'\s*$')) {throw 'Identity owner or length mismatch'}
            if ($probe.Text -notmatch ('(?m)^([0-9a-f]{64})\s+'+[regex]::Escape($output)+'\s*$')) {throw 'Identity hash missing'}
            $identityHash=$Matches[1]
            $privateDir=Join-Path $windowRoot ($board+'/identity');New-Item -ItemType Directory -Path $privateDir -ErrorAction Stop | Out-Null
            $destination=Join-Path $privateDir 'local-udid.bin'
            Invoke-HdcLogged -Board $board -Step 'identity-file-receive' -NativeArguments @('file','recv',$output,$destination) | Out-Null
            $bytes=[IO.File]::ReadAllBytes($destination)
            if ($bytes.Length -ne $identitySize -or (Get-FileHash -LiteralPath $destination).Hash.ToLowerInvariant() -cne $identityHash -or $bytes -contains 0 -or $bytes -contains 10 -or $bytes -contains 13) {throw 'Private identity transfer mismatch'}
            Invoke-Remote -Board $board -Step 'identity-file-remove-owned' -CommandText ('test ! -L '+$output+' && stat -c %i '+$output+' | grep -qx '+$identityInode+' && sha256sum '+$output+' | grep -q '+$identityHash+' && rm '+$output+' && test ! -e '+$output+' && test ! -L '+$output) | Out-Null
            $identityRows[$board]=@{present=$true;remote_absent=$true;path=$output;local_path=$destination;bytes=$identitySize;sha256=$identityHash;mode='600';uid=$identityUid;inode=$identityInode;disposition='EXACT_BYTES_RECEIVED_AND_OWNED_EXPORT_REMOVED'}
        } catch {$cleanupFailures.Add($board+': identity artifact: '+$_.Exception.Message);break}
    }
    [IO.File]::WriteAllText((Join-Path $windowRoot 'IDENTITY_ARTIFACT_CLEANUP.json'), (@{run=$cfg.run;scope='PRIVATE_IDENTITY_EXPORT_ONLY';pass=($cleanupFailures.Count -eq 0);sides=$identityRows} | ConvertTo-Json -Depth 8),$utf8)
}
[IO.File]::WriteAllText((Join-Path $windowRoot 'window-summary.json'), (@{first_failure=$windowFailure;cleanup_failures=@($cleanupFailures.ToArray());attempted_grants=@($attempted.ToArray());time_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 8), $utf8)

if ($cleanupFailures.Count -gt 0) {

    [IO.File]::WriteAllText((Join-Path $windowRoot 'load-collection-summary.json'),(@{status='NOT_RUN_GUARD_RESTORATION_FAILED';first_failure=$windowFailure;cleanup_failures=@($cleanupFailures.ToArray());device_operations_after_failed_cleanup=0} | ConvertTo-Json -Depth 8),$utf8)

    exit 1

}

if ($windowFailure) {exit 1}

& python (Join-Path $PSScriptRoot 'route_query.py') $windowRoot 1> (Join-Path $windowRoot 'route-analysis.stdout.log') 2> (Join-Path $windowRoot 'route-analysis.stderr.log')

$analysisExit=$LASTEXITCODE

[IO.File]::WriteAllText((Join-Path $windowRoot 'route-analysis-exit.txt'),[string]$analysisExit,$utf8)

if ($analysisExit -ne 0) {

    [IO.File]::WriteAllText((Join-Path $windowRoot 'route-analysis-failure.json'),(@{first_failure='PUBLIC_DIRECTORY_CROSS_MATCH_FAILED';business_first_failure=$windowFailure;cleanup_failures=@()} | ConvertTo-Json),$utf8)

    exit 1

}

[Console]::WriteLine('READONLY DIRECTORY QUERY PASS; no Socket/DDS resources; both complete guards checked')

exit 0
