param([Parameter(Mandatory)][string]$WindowDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = (Get-Location).Path
$windowRoot = [IO.Path]::GetFullPath($WindowDirectory)
$cfg = Get-Content -Raw -LiteralPath (Join-Path $windowRoot 'window.json') | ConvertFrom-Json
$taskRoot=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\','/')
if (-not $windowRoot.StartsWith($taskRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFullPath($cfg.window_root) -ine $windowRoot -or [IO.Path]::GetFullPath($cfg.repo) -ine $repoRoot) {throw 'Window/repository identity changed'}
$hdcPath = [string]$cfg.hdc_path
if (-not [IO.Path]::IsPathRooted($hdcPath)) {throw 'Explicit absolute HDC path required'}
$tokenFile = '/data/service/el0/access_token/nativetoken.json'
$packageRoot = [IO.Path]::GetFullPath($cfg.package)
$utf8Strict = [Text.UTF8Encoding]::new($false, $true)
$utf8 = [Text.UTF8Encoding]::new($false)
$expectedHash = @{A=[string]$cfg.permission_baseline_sha256.A; B=[string]$cfg.permission_baseline_sha256.B}
if (Test-Path -LiteralPath (Join-Path $windowRoot 'EXECUTION_STARTED')) {throw 'Refusing to reuse a window'}
if ($cfg.run -notmatch '^[0-9a-f]{16}$') {throw 'Invalid run'}
if ($cfg.eligible_for_executor_gate -ne $true) {throw 'Current two-board preflight not eligible'}
if ($cfg.scenario -notin @('SHARED_W1','SHARED_W2','F1M','INSTALLED_RUNTIME','ROS2_DSOFTBUS','DDS214_LAYER_BASELINE')) {throw 'Unauthorized scenario'}
foreach ($binding in @(@($cfg.build_manifest,$cfg.build_manifest_sha256),@($cfg.source_state,$cfg.source_state_sha256),@($cfg.preflight_config,$cfg.preflight_config_sha256),@($cfg.permission_helper.path,$cfg.permission_helper.sha256))) {
    if ((Get-FileHash -LiteralPath $binding[0]).Hash.ToLowerInvariant() -cne $binding[1]) {throw 'Frozen input changed'}
}
$currentPreflight = Get-Content -Raw -LiteralPath $cfg.preflight_config | ConvertFrom-Json
if ($currentPreflight.eligible_for_executor_gate -ne $true) {throw 'Current baseline not eligible'}
$frozenManifest=Get-Content -Raw -LiteralPath $cfg.build_manifest | ConvertFrom-Json
if ($cfg.scenario -ceq 'ROS2_DSOFTBUS') {
    if ($frozenManifest.scenario -cne 'ROS2_DSOFTBUS' -or $frozenManifest.main -cne 'ordinary_ros2_transport_consumer' -or $frozenManifest.abi.fastrtps -cne '2.14.5' -or $frozenManifest.abi.cpp_runtime -cne 'c++_shared') {throw 'ROS target build/ABI identity mismatch'}
} elseif ($cfg.scenario -ceq 'DDS214_LAYER_BASELINE') {
    if ($frozenManifest.scenario -cne 'DDS214_LAYER_BASELINE' -or $frozenManifest.main -cne 'fastdds_dsoftbus_dds214_baseline' -or $frozenManifest.abi.fastrtps -cne '2.14.5' -or $frozenManifest.abi.cpp_runtime -cne 'c++_shared') {throw 'Direct DDS target build/ABI identity mismatch'}
} elseif ($cfg.scenario -ceq 'INSTALLED_RUNTIME') {
    # The installed-runtime window must be backed by the experimental-runtime
    # library build, not by the isolated probe experiment.
    if (@($frozenManifest.configureArguments | Where-Object {$_ -ceq '-DFASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME=ON'}).Count -ne 1) {throw 'Not the installed experimental-runtime build'}
    if (@($frozenManifest.configureArguments | Where-Object {$_ -ceq '-DBUILD_PUBLIC_SOCKET_DDS_EXPERIMENT=ON'}).Count -ne 0) {throw 'Installed window must not use the isolated probe experiment build'}
} else {
    if (@($frozenManifest.configureArguments | Where-Object {$_ -ceq '-DBUILD_PUBLIC_SOCKET_DDS_EXPERIMENT=ON'}).Count -ne 1) {throw 'Not the isolated real DDS experiment'}
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
& python (Join-Path $PSScriptRoot 'task_contract.py') --check-window (Join-Path $windowRoot 'window.json')
if ($LASTEXITCODE -ne 0) {throw 'Task admission not complete; no device operations'}
foreach ($property in $cfg.shared_script_sha256.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $repoRoot ('scripts/'+$property.Name))).Hash.ToLowerInvariant() -cne $property.Value) {throw 'Shared guard/harness changed'}
}
foreach ($property in $cfg.executor_sha256.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $property.Name)).Hash.ToLowerInvariant() -cne $property.Value) {throw 'Executor changed after window freeze'}
}
foreach ($name in @('record-pair.cjs','bounded-output.cjs','shared-control.cjs')) {
    if ((Get-FileHash -LiteralPath (Join-Path $windowRoot $name)).Hash.ToLowerInvariant() -cne $cfg.executor_sha256.$name) {throw 'Window runner helper changed'}
}
if ((Get-FileHash -LiteralPath (Join-Path $windowRoot 'runner.cjs')).Hash.ToLowerInvariant() -cne $cfg.executor_sha256.'runner-template.cjs') {throw 'Runner changed after window freeze'}
if ((Get-FileHash -LiteralPath (Join-Path $windowRoot 'observe-native.cjs')).Hash.ToLowerInvariant() -cne $cfg.executor_sha256.'observe-native.cjs') {throw 'Collector changed after window freeze'}
if ($cfg.scenario -in @('SHARED_W1','SHARED_W2','F1M','INSTALLED_RUNTIME','ROS2_DSOFTBUS','DDS214_LAYER_BASELINE')) {
foreach ($name in @('route-binding-check.stdout.log','route-binding-check.stderr.log')) {
    if (Test-Path -LiteralPath (Join-Path $windowRoot $name)) {throw 'Refusing route check evidence overwrite'}
}
& python (Join-Path $PSScriptRoot 'route_binding.py') --check-window (Join-Path $windowRoot 'window.json') 1> (Join-Path $windowRoot 'route-binding-check.stdout.log') 2> (Join-Path $windowRoot 'route-binding-check.stderr.log')
if ($LASTEXITCODE -ne 0) {throw 'Frozen route binding check failed before device operations'}
}
foreach ($board in @('A','B')) {
    if ($cfg.remote.$board -notmatch ('^/data/local/tmp/mdds_[a-z]+_'+$cfg.run+'_'+$board+'$')) {throw 'Unbound deployment path'}
}
$packageFiles = @(Get-ChildItem -LiteralPath $packageRoot -File)
if ($packageFiles.Count -ne $cfg.package_artifacts.Count) {throw 'Package member count does not match the frozen list'}
foreach ($artifact in $cfg.package_artifacts) {
    if ((Get-FileHash -LiteralPath (Join-Path $packageRoot $artifact.name)).Hash.ToLowerInvariant() -cne $artifact.sha256) {throw 'Frozen package artifact changed'}
}
$mainMemberName = if ($cfg.PSObject.Properties['main_member']) {[string]$cfg.main_member} else {'public_socket_dds_experiment'}
if ((Get-FileHash -LiteralPath (Join-Path $packageRoot $mainMemberName)).Hash.ToLowerInvariant() -cne $cfg.main_sha256) {throw 'Wrong executable'}
# Load project code only after contract, dependency and guard hashes pass.
. (Join-Path $repoRoot 'scripts\NativePermissionGuard.ps1')
[IO.File]::WriteAllText((Join-Path $windowRoot 'EXECUTION_STARTED'), [DateTime]::UtcNow.ToString('o'), $utf8)

function Invoke-HdcLogged([string]$Board, [string]$Step, [string[]]$NativeArguments) {
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
    if ($NativeArguments.Count -ge 2 -and $NativeArguments[0] -ceq 'file' -and $NativeArguments[1] -in @('send','recv')) {
        $transferOut=Get-Content -Raw -LiteralPath (Join-Path $stepRoot 'stdout.log')
        $transferErr=Get-Content -Raw -LiteralPath (Join-Path $stepRoot 'stderr.log')
        if ($transferOut -match '(?m)^\[Fail\]' -or $transferErr -match '(?m)^\[Fail\]' -or $transferOut -match '(?m)^E001005\b' -or $transferErr -match '(?m)^E001005\b') {
            [IO.File]::WriteAllText((Join-Path $stepRoot 'file-transfer-result.json'),(@{status='REJECTED';scope='HDC_FILE_TRANSFER_DIAGNOSTIC';local_exit=$nativeExit;remote_command_exit='NOT_APPLICABLE';content_verified=$false}|ConvertTo-Json),$utf8)
            throw "HDC_FILE_TRANSFER_REJECTED: $Board/$Step; outer rc0 is not transfer success"
        }
        # Only the following remote hash/received-content checks prove delivery.
    }
    return [pscustomobject]@{ Root=$stepRoot; Text=(Get-Content -Raw -LiteralPath (Join-Path $stepRoot 'stdout.log')); LocalExitCode=$nativeExit; OutputBytes=(Get-Item -LiteralPath (Join-Path $stepRoot 'stdout.log')).Length }
}
function Test-RemoteCommandReceipt([AllowNull()][object]$Record) {
    $result=[ordered]@{status='INCOMPLETE';reason='MISSING_RECORD';scope='REMOTE_RETURN_MARKER_ONLY';local_exit=$null;remote_exit=$null;remote_exit_count=0;text_type='MISSING';output_bytes=$null}
    if($null -eq $Record){return [pscustomobject]$result}
    if($Record.PSObject.Properties['LocalExitCode']){$result.local_exit=$Record.LocalExitCode}
    if($Record.PSObject.Properties['OutputBytes']){$result.output_bytes=$Record.OutputBytes}
    if(-not $Record.PSObject.Properties['Text']){$result.reason='MISSING_TEXT';return [pscustomobject]$result}
    $text=$Record.Text
    if($null -eq $text){$result.reason='NULL_TEXT';return [pscustomobject]$result}
    $result.text_type=$text.GetType().FullName
    if($text -isnot [string]){$result.reason='INVALID_TEXT_TYPE';return [pscustomobject]$result}
    $result.output_bytes=[Text.Encoding]::UTF8.GetByteCount($text)
    if($Record.PSObject.Properties['OutputBytes']){$result.output_bytes=$Record.OutputBytes}
    if($text.Length -eq 0){$result.reason='EMPTY_TEXT';return [pscustomobject]$result}
    if(-not $Record.PSObject.Properties['LocalExitCode']){$result.reason='MISSING_LOCAL_EXIT';return [pscustomobject]$result}
    $result.local_exit=$Record.LocalExitCode
    if($Record.LocalExitCode -isnot [int] -or $Record.LocalExitCode -ne 0){$result.reason='LOCAL_EXIT_FAILED';return [pscustomobject]$result}
    if(($Record.PSObject.Properties['Cancelled'] -and $Record.Cancelled) -or ($Record.PSObject.Properties['CollectionComplete'] -and -not $Record.CollectionComplete)){$result.reason='COLLECTION_INCOMPLETE';return [pscustomobject]$result}
    $codes=[regex]::Matches($text,'(?m)^MDDS_REMOTE_EXIT=([0-9]+)\s*$')
    $result.remote_exit_count=$codes.Count
    if($codes.Count -ne 1){$result.reason='REMOTE_EXIT_MARKER_MISSING_OR_DUPLICATE';return [pscustomobject]$result}
    $result.remote_exit=$codes[0].Groups[1].Value
    if($result.remote_exit -cne '0'){$result.reason='REMOTE_EXIT_FAILED';return [pscustomobject]$result}
    $result.status='COMPLETE';$result.reason='NONE';return [pscustomobject]$result
}
function Invoke-Remote([string]$Board, [string]$Step, [string]$CommandText) {
    $wrapped = $CommandText + '; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc'
    $record = Invoke-HdcLogged -Board $Board -Step $Step -NativeArguments @('shell', $wrapped)
    $checked=Test-RemoteCommandReceipt $record
    $receipt=[ordered]@{board=$Board;step=$Step;command_evidence=(Join-Path $record.Root 'command.json');stdout_evidence=(Join-Path $record.Root 'stdout.log');stderr_evidence=(Join-Path $record.Root 'stderr.log');validation=$checked}
    [IO.File]::WriteAllText((Join-Path $record.Root 'remote-result.json'),($receipt|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
    if($checked.status -cne 'COMPLETE'){throw "REMOTE_CAPTURE_INCOMPLETE: $Board/$Step $($checked.reason)"}
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
    if ($record.Text -match 'fastdds_socket|public_socket|mdds_installed|ordinary_ros2|fastdds_dsoftb|native_permiss|softbus_recor') {throw "Experiment or permission process still present on $Board"}
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
function Assert-PairReady {
    # Exact registered target selection occurs in Invoke-Remote. Check both
    # platforms before any owned mkdir/send/chmod, then keep full per-side
    # baseline checks below: this cannot guarantee later HDC replies.
    foreach ($board in @('A','B')) {
        foreach ($name in @('kernel','const.product.name')) {
            $expected=@($cfg.platform.$board.parameters | Where-Object { $_.name -ceq $name })
            if ($expected.Count -ne 1) {throw "Pair platform identity not uniquely frozen: $board/$name"}
            $command=if ($name -ceq 'kernel') {'uname -a'} else {'param get '+$name}
            $record=Invoke-Remote -Board $board -Step ('pair-ready-'+$name) -CommandText $command
            $value=($record.Text -replace '(?m)^MDDS_REMOTE_EXIT=0\s*$','').Trim()
            if ($value -cne $expected[0].value) {throw "Pair platform identity mismatch: $board/$name"}
        }
    }
}
function Invoke-Permission([string]$Board,[string]$Step,[string]$Action) {
    $root=[string]$cfg.remote.$Board
    $loader=$root+':'+($cfg.system_library_dirs -join ':')
    return Invoke-Remote -Board $Board -Step $Step -CommandText ('env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH='+$loader+' '+$root+'/native_permission_probe --'+$Action+' --expected-token '+$cfg.token.$Board+' --preflight-confirmed')
}

$baselines = @{}; $beforeMetadata = @{}
try {
Assert-PairReady
foreach ($board in @('A','B')) {
    Assert-Platform -Board $board -Phase 'baseline'
    Invoke-Remote -Board $board -Step 'tools-before' -CommandText 'env -i PATH=/system/bin:/system/xbin /system/bin/sh -c ''for mdds_tool in cat readlink stat sha256sum mv env timeout; do command -v "$mdds_tool" || exit 3; done''' | Out-Null
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
    foreach ($property in $cfg.auxiliary_sha256.$board.PSObject.Properties) {
        $localAux=Join-Path $windowRoot ($board+'/aux/'+$property.Name)
        if ((Get-FileHash -LiteralPath $localAux).Hash.ToLowerInvariant() -cne $property.Value) {throw 'Auxiliary changed'}
        $remoteAux=$cfg.remote.$board+'/'+$property.Name
        Invoke-HdcLogged -Board $board -Step ('deploy-aux-'+$property.Name) -NativeArguments @('file','send',$localAux,$remoteAux) | Out-Null
        $record=Invoke-Remote -Board $board -Step ('hash-aux-'+$property.Name) -CommandText ('sha256sum '+$remoteAux)
        if ($record.Text -notmatch ('(?m)^'+$property.Value+'\s+'+[regex]::Escape($remoteAux)+'\s*$')) {throw 'Auxiliary deployment mismatch'}
    }
    Invoke-Remote -Board $board -Step 'helper-executable' -CommandText ('chmod 700 '+$cfg.remote.$board+'/native_permission_probe') | Out-Null
    if ($cfg.PSObject.Properties['rmw_debugger']) {
        Invoke-Remote -Board $board -Step 'debugger-executable' -CommandText ('chmod 700 '+$cfg.remote.$board+'/lldb-server && test -x '+$cfg.remote.$board+'/lldb-server') | Out-Null
    }
    $mainMember = if ($cfg.PSObject.Properties['main_member']) {[string]$cfg.main_member} else {'public_socket_dds_experiment'}
    Invoke-Remote -Board $board -Step 'executable' -CommandText ('chmod 700 '+$cfg.remote.$board+'/'+$mainMember+' && test -x '+$cfg.remote.$board+'/'+$mainMember) | Out-Null
    foreach ($file in $packageFiles) {
        $remoteFile = $cfg.remote.$board+'/'+$file.Name
        $record = Invoke-Remote -Board $board -Step ('hash-'+$file.Name) -CommandText ('sha256sum '+$remoteFile)
        $localHash = (Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()
        if ($record.Text -notmatch ('(?m)^'+$localHash+'\s+'+[regex]::Escape($remoteFile)+'\s*$')) {throw "Package mismatch $board/$($file.Name)"}
    }
}
} catch {
    $pregrantFailure=$_
    [IO.File]::WriteAllText((Join-Path $windowRoot 'window-summary.json'),(@{first_failure=$_.Exception.Message;failure_stage='PREFLIGHT_OR_DEPLOY';cleanup_failures=@();attempted_grants=@();permission_restore='NOT_REQUIRED_NO_GRANT';process_outcome='NO_BUSINESS_PROCESS_STARTED'} | ConvertTo-Json -Depth 8),$utf8)
    # This is the same append-only recovery-only path used after an interrupted
    # setup. It never re-enters launch, and success does not replace this failure.
    if ($cfg.scenario -ceq 'ROS2_DSOFTBUS') {
        $recoveryExit=$null
        $savedErrorPreference=$ErrorActionPreference
        try {
            $ErrorActionPreference='Continue'
            & python (Join-Path $taskRoot 'recovery/pregrant_recovery.py') --auto-window $windowRoot 1> (Join-Path $windowRoot 'recovery-auto.stdout.log') 2> (Join-Path $windowRoot 'recovery-auto.stderr.log')
            $recoveryExit=$LASTEXITCODE
        } finally {$ErrorActionPreference=$savedErrorPreference}
        [IO.File]::WriteAllText((Join-Path $windowRoot 'RECOVERY_PENDING.json'),(@{run=$cfg.run;original_first_failure=$pregrantFailure.Exception.Message;recovery_tool_exit=$recoveryExit;status='INDEPENDENT_RECOVERY_ACCEPTANCE_REQUIRED';business_allowed=$false}|ConvertTo-Json),$utf8)
    }
    throw $pregrantFailure
}
$attempted = [Collections.Generic.List[string]]::new()
$runResult = $null; $windowFailure = $null; $cleanupFailures = [Collections.Generic.List[string]]::new()
try {
    foreach ($board in @('A','B')) {
        Assert-Platform -Board $board -Phase 'adjacent'
        $adjacent = Invoke-Remote -Board $board -Step 'adjacent-baseline-hash' -CommandText ('sha256sum '+$tokenFile)
        if ($adjacent.Text -notmatch $expectedHash[$board]) {throw "Baseline changed before grant $board"}
        Assert-ProcessAbsent -Board $board -Step 'processes-pregrant'
        if ($cfg.scenario -in @('SHARED_W1','SHARED_W2','F1M','INSTALLED_RUNTIME','ROS2_DSOFTBUS','DDS214_LAYER_BASELINE')) {
        $attempted.Add($board)
        $grant = Invoke-Permission -Board $board -Step 'grant' -Action 'grant'
        if ($grant.Text -notmatch 'verification_permission=0' -or $grant.Text -notmatch 'result=PASS') {throw "Grant verification failed $board"}
        $grantedFile = Receive-Token -Board $board -Step 'granted'
        Guard-Token -Board $board -Phase Granted -Baseline $baselines[$board] -Current $grantedFile -Step 'granted-guard'
        }
    }
    # The data dispatcher is the single writer of DATA_ATTEMPT_STARTED: it runs at
    # the real first dispatch and records the exact intent receipt. Installed windows
    # therefore do not pre-create the marker here.
    if ($cfg.scenario -in @('SHARED_W1','SHARED_W2','F1M') -and (-not $cfg.PSObject.Properties['work_package'] -or $cfg.work_package -cne 'N10_OBSERVED_RETRY_AND_CONDITIONAL_N100')) {[IO.File]::WriteAllText((Join-Path $windowRoot 'DATA_ATTEMPT_STARTED'), [DateTime]::UtcNow.ToString('o'), $utf8)}
    [Console]::WriteLine('WINDOW launching reviewed immutable runner')
    & node (Join-Path $windowRoot 'runner.cjs') 1> (Join-Path $windowRoot 'runner.stdout.log') 2> (Join-Path $windowRoot 'runner.stderr.log')
    $runnerExit = $LASTEXITCODE
    [IO.File]::WriteAllText((Join-Path $windowRoot 'runner-exit-code.txt'), [string]$runnerExit, $utf8)
    if (Test-Path -LiteralPath (Join-Path $windowRoot 'result.json')) {$runResult = Get-Content -Raw -LiteralPath (Join-Path $windowRoot 'result.json') | ConvertFrom-Json}
    if ($runnerExit -ne 0) {throw "Runner failed rc=$runnerExit"}
    if (-not $runResult.pass -or $runResult.timedOut -or $runResult.A.remoteExit -ne 0 -or $runResult.B.remoteExit -ne 0) {throw 'Scenario exit contract failed'}
} catch {
    $windowFailure = $_.Exception.Message
    [IO.File]::WriteAllText((Join-Path $windowRoot 'window-failure.txt'), $windowFailure, $utf8)
} finally {
    $guardBoards = @($attempted.ToArray())
    foreach ($board in $guardBoards) {
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
            [IO.File]::WriteAllText((Join-Path $windowRoot ($board+'/final-guard.json')), (@{Passed=($byteMatch -and $metaMatch);Phase='Revoked';byte_match=$byteMatch;metadata_match=$metaMatch;metadata_before=$beforeMetadata[$board];metadata_after=$afterMetadata;inode_changed=($beforeMetadata[$board].inode -cne $afterMetadata.inode);timestamps_comparison='OBSERVED_NOT_REQUIRED_EQUAL_AFTER_API_WRITE';grant_scope=$cfg.scenario;grant_performed=($attempted.Contains($board));service_permission='DENIED';process_absent=$true} | ConvertTo-Json -Depth 8), $utf8)
            if (-not $byteMatch -or -not $metaMatch) {throw "Final restoration mismatch $board"}
        } catch {
            $cleanupFailures.Add($board+': '+$_.Exception.Message)
        }
    }
    [IO.File]::WriteAllText((Join-Path $windowRoot 'window-summary.json'), (@{first_failure=$windowFailure;cleanup_failures=@($cleanupFailures.ToArray());attempted_grants=@($attempted.ToArray());time_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 8), $utf8)
}
# Load evidence collection is deliberately after BOTH revokes/full guards.
# Missing evidence never replaces first_failure or restoration failures.
if ($cleanupFailures.Count -gt 0) {
    # All owned compensating revokes have already been attempted in finally.
    # Fail closed now: no more HDC hashes, file transfers, or platform reads.
    [IO.File]::WriteAllText((Join-Path $windowRoot 'load-collection-summary.json'),(@{status='NOT_RUN_GUARD_RESTORATION_FAILED';failures=@();permission_cleanup_failed=$true;first_failure=$windowFailure;cleanup_failures=@($cleanupFailures.ToArray());device_operations_after_failed_cleanup=0;load_scope='UNOBSERVED';sdk_internal_settlement='UNOBSERVED'} | ConvertTo-Json -Depth 8),$utf8)
    exit 1
}
if ($runResult -and ($runResult.collectionCleanup -ceq 'UNCONFIRMED' -or @($runResult.loads.PSObject.Properties | Where-Object {($_.Value.PSObject.Properties['collectorClosed'] -and $_.Value.collectorClosed -ne $true) -or ($_.Value.PSObject.Properties['loadChainClosed'] -and $_.Value.loadChainClosed -ne $true) -or $_.Value.collectionCleanup -ceq 'UNCONFIRMED'}).Count -gt 0 -or @($runResult.controls | Where-Object {$_.collector_closed -ne $true}).Count -gt 0)) {
    [IO.File]::WriteAllText((Join-Path $windowRoot 'load-collection-summary.json'),(@{status='NOT_RUN_COLLECTION_UNCONFIRMED';first_failure=$windowFailure;device_operations_after_failed_cleanup=0} | ConvertTo-Json),$utf8)
    exit 1
}
$loadFailures=[Collections.Generic.List[string]]::new()
foreach ($board in @('A','B')) {
    $loadRoot=Join-Path $windowRoot ($board+'/load')
    $captureNames=@('load-status.txt','load-exe.txt','load-cwd.txt','load-cmdline.txt','load-environ.txt','load-stat-before.txt','load-stat-after.txt','load-uptime.txt','load-maps.txt')
    $reusePreRelease=($cfg.scenario -in @('ROS2_DSOFTBUS','DDS214_LAYER_BASELINE') -and (Test-Path -LiteralPath $loadRoot -PathType Container))
    if (-not $reusePreRelease) {New-Item -ItemType Directory -Path $loadRoot -ErrorAction Stop | Out-Null}
    foreach ($artifact in $cfg.package_artifacts) {
        try {
            $remoteFile=$cfg.remote.$board+'/'+$artifact.name
            $record=Invoke-Remote -Board $board -Step ('postrun-hash-'+$artifact.name) -CommandText ('sha256sum '+$remoteFile)
            if ($record.Text -notmatch ('(?m)^'+$artifact.sha256+'\s+'+[regex]::Escape($remoteFile)+'\s*$')) {throw 'Loaded package path changed'}
        } catch {$loadFailures.Add($board+': '+$_.Exception.Message)}
    }
    try {Assert-Platform -Board $board -Phase 'postrun'} catch {$loadFailures.Add($board+': '+$_.Exception.Message)}
    if ($reusePreRelease) {
        # The ROS controller already needed these exact live files before it
        # released business. Preserve and revalidate them; do not overwrite.
        try {
            $pre=Get-Content -Raw -LiteralPath (Join-Path $windowRoot ($board+'/load-pre-release-summary.json')) | ConvertFrom-Json
            if ($pre.pass -ne $true -or $pre.run -cne $cfg.run -or $pre.side -cne $board) {throw 'Pre-release load admission was not complete'}
            if (@($pre.transfer).Count -ne $captureNames.Count) {throw 'Pre-release load file count mismatch'}
            foreach ($name in $captureNames) {
                $row=@($pre.transfer | Where-Object {$_.name -ceq $name})
                $local=Join-Path $loadRoot $name
                if ($row.Count -ne 1 -or $row[0].pass -ne $true -or -not (Test-Path -LiteralPath $local -PathType Leaf) -or
                    (Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash.ToLowerInvariant() -cne $row[0].sha256) {throw ('Pre-release load file drift: '+$name)}
            }
        } catch {$loadFailures.Add($board+': '+$_.Exception.Message)}
    } else {
        foreach ($name in $captureNames) {
            try {Invoke-HdcLogged -Board $board -Step ('collect-'+$name) -NativeArguments @('file','recv',($cfg.remote.$board+'/'+$name),(Join-Path $loadRoot $name)) | Out-Null}
            catch {$loadFailures.Add($board+'/'+$name+': '+$_.Exception.Message)}
        }
    }
    $collectorReceipt=Join-Path $windowRoot ($board+'/load-collector.json')
    if ($reusePreRelease) {
        if (-not (Test-Path -LiteralPath $collectorReceipt -PathType Leaf) -or
            -not (Test-Path -LiteralPath (Join-Path $loadRoot 'load-collector.json') -PathType Leaf) -or
            (Get-FileHash -LiteralPath $collectorReceipt -Algorithm SHA256).Hash -cne (Get-FileHash -LiteralPath (Join-Path $loadRoot 'load-collector.json') -Algorithm SHA256).Hash) {$loadFailures.Add($board+': pre-release collector receipt drift')}
    } elseif (Test-Path -LiteralPath $collectorReceipt) {Copy-Item -LiteralPath $collectorReceipt -Destination (Join-Path $loadRoot 'load-collector.json') -ErrorAction Stop}
    & python (Join-Path $PSScriptRoot 'load_origin.py') $windowRoot $board 1> (Join-Path $windowRoot ($board+'/load-analysis.stdout.log')) 2> (Join-Path $windowRoot ($board+'/load-analysis.stderr.log'))
    if ($LASTEXITCODE -ne 0) {$loadFailures.Add($board+': load identity incomplete')}
}
$finalFirstFailure=if ($windowFailure) {$windowFailure} elseif ($loadFailures.Count) {'LOAD_ORIGIN: '+$loadFailures[0]} else {$null}
$loadScope = if ($cfg.PSObject.Properties['simulated'] -and $cfg.simulated -is [bool] -and $cfg.simulated) {'LOCAL_MODELED_CAPTURE_NOT_DEVICE_EVIDENCE'} else {'LIVE_PROC_SNAPSHOT'}
[IO.File]::WriteAllText((Join-Path $windowRoot 'load-collection-summary.json'),(@{failures=@($loadFailures.ToArray());permission_cleanup_failed=($cleanupFailures.Count -ne 0);first_failure=$finalFirstFailure;business_first_failure=$windowFailure;load_scope=$loadScope;sdk_internal_settlement='UNOBSERVED'} | ConvertTo-Json -Depth 8),$utf8)
if ($windowFailure -or $cleanupFailures.Count -or $loadFailures.Count) {exit 1}
if ($cfg.scenario -ceq 'DDS214_LAYER_BASELINE') {
    $analysisTool=Join-Path $repoRoot 'scripts/public_socket/analyze_dds214.py'
    if ((Get-FileHash -LiteralPath $analysisTool).Hash.ToLowerInvariant() -cne $cfg.analyzer_sha256) {throw 'DDS214 analyzer drift'}
    $evidenceScope=if ($cfg.PSObject.Properties['simulated'] -and $cfg.simulated -is [bool] -and $cfg.simulated) {'local_model'} else {'device'}
    & python $analysisTool --run $cfg.run --scope $evidenceScope --a (Join-Path $windowRoot 'A.raw.stdout.log') --b (Join-Path $windowRoot 'B.raw.stdout.log') --a-stderr (Join-Path $windowRoot 'A.raw.stderr.log') --b-stderr (Join-Path $windowRoot 'B.raw.stderr.log') --output (Join-Path $windowRoot 'dds214-analysis.json') 1> (Join-Path $windowRoot 'dds214-analysis.stdout.log') 2> (Join-Path $windowRoot 'dds214-analysis.stderr.log')
    $ddsAnalysisExit=$LASTEXITCODE
    [IO.File]::WriteAllText((Join-Path $windowRoot 'dds214-analysis.rc'),[string]$ddsAnalysisExit,$utf8)
    if ($ddsAnalysisExit -ne 0) {
        [IO.File]::WriteAllText((Join-Path $windowRoot 'ANALYSIS_FAILURE.json'),(@{first_failure='DDS214_ANALYSIS_FAILED';run=$cfg.run;analyzer_exit=$ddsAnalysisExit;scope=$evidenceScope} | ConvertTo-Json),$utf8)
        exit 1
    }
}
if ($cfg.scenario -ceq 'ROS2_DSOFTBUS') {
    $rosAnalyzer=Join-Path $repoRoot 'scripts/public_socket/analyze_ros2.py'
    if ((Get-FileHash -LiteralPath $rosAnalyzer).Hash.ToLowerInvariant() -cne $cfg.analyzer_sha256) {throw 'Formal ROS analyzer changed after admission'}
    & python $rosAnalyzer --run $cfg.run --mode dual --a (Join-Path $windowRoot 'A.raw.stdout.log') --b (Join-Path $windowRoot 'B.raw.stdout.log') --output (Join-Path $windowRoot 'ros-analysis.json') 1> (Join-Path $windowRoot 'ros-analysis.stdout.log') 2> (Join-Path $windowRoot 'ros-analysis.stderr.log')
    $rosAnalysisExit=$LASTEXITCODE
    [IO.File]::WriteAllText((Join-Path $windowRoot 'ros-analysis.rc'),[string]$rosAnalysisExit,$utf8)
    if ($rosAnalysisExit -ne 0) {exit 1}
    if ($cfg.PSObject.Properties['simulated'] -and $cfg.simulated -is [bool] -and $cfg.simulated) {
        [IO.File]::WriteAllText((Join-Path $windowRoot 'ros-native-status.json'),'{"status":"NOT_RUN_LOCAL_MODEL","device_evidence":false}',$utf8)
    } else {
        $nativeAnalyzer=Join-Path $repoRoot 'scripts/public_socket/analyze_ros2_native.py'
        if ((Get-FileHash -LiteralPath $nativeAnalyzer).Hash.ToLowerInvariant() -cne $cfg.native_analyzer_sha256) {throw 'ROS native analyzer changed after admission'}
        & python $nativeAnalyzer --run $cfg.run --a (Join-Path $windowRoot 'A.raw.stdout.log') --b (Join-Path $windowRoot 'B.raw.stdout.log') --a-stderr (Join-Path $windowRoot 'A.raw.stderr.log') --b-stderr (Join-Path $windowRoot 'B.raw.stderr.log') --output (Join-Path $windowRoot 'ros-native-analysis.json') 1> (Join-Path $windowRoot 'ros-native-analysis.stdout.log') 2> (Join-Path $windowRoot 'ros-native-analysis.stderr.log')
        $nativeExit=$LASTEXITCODE
        [IO.File]::WriteAllText((Join-Path $windowRoot 'ros-native-analysis.rc'),[string]$nativeExit,$utf8)
        if ($nativeExit -ne 0) {
            [IO.File]::WriteAllText((Join-Path $windowRoot 'ANALYSIS_FAILURE.json'),(@{first_failure='ROS_NATIVE_SAMPLE_CORRELATION_FAILED';phase='POSTRUN_ANALYSIS';run=$cfg.run;analyzer_exit=$nativeExit;original_window_summary=(Join-Path $windowRoot 'window-summary.json')} | ConvertTo-Json -Depth 4),$utf8)
            exit 1
        }
    }
}
if ($cfg.PSObject.Properties['cycles'] -and $cfg.cycles -gt 1) {
    if ((Get-FileHash -LiteralPath (Join-Path $repoRoot 'scripts/public_socket/analyze_shared.py')).Hash.ToLowerInvariant() -cne $cfg.analyzer_sha256) {throw 'Formal analyzer changed after admission'}
    & python (Join-Path $repoRoot 'scripts/public_socket/analyze_shared.py') --case $cfg.scenario --cycles $cfg.cycles --require-send-policy selected_retirement_v1 --a (Join-Path $windowRoot 'A.raw.stdout.log') --b (Join-Path $windowRoot 'B.raw.stdout.log') --output (Join-Path $windowRoot 'cycle-analysis.json') 1> (Join-Path $windowRoot 'cycle-analysis.stdout.log') 2> (Join-Path $windowRoot 'cycle-analysis.stderr.log')
    $analysisExit=$LASTEXITCODE
    [IO.File]::WriteAllText((Join-Path $windowRoot 'cycle-analysis.rc'),[string]$analysisExit,$utf8)
    if ($analysisExit -ne 0) {exit 1}
}
[Console]::WriteLine('WINDOW PASS; both complete guards checked; SDK internal settlement remains UNOBSERVED')
exit 0
