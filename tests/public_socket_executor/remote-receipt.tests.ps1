$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$source=Join-Path $PSScriptRoot '../../scripts/public_socket/shared_reattach/executor/run-window.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile([IO.Path]::GetFullPath($source),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Parse failure'}
# Definitions only. Never dot-source executor top-level admission/device actions.
foreach($name in @('Test-RemoteCommandReceipt','Invoke-Remote')){
    $node=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true)
    if($node){. ([scriptblock]::Create($node.Extent.Text))}
}
function Invoke-HdcLogged {param($Board,$Step,$NativeArguments) return $script:fixture}
$empty=Join-Path $PSScriptRoot 'fixtures/empty.stdout'
$scratch=Join-Path ([IO.Path]::GetTempPath()) ('mdds-offline-receipt-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$script:fixture=[pscustomobject]@{Root=$scratch;Text=(Get-Content -Raw -LiteralPath $empty);LocalExitCode=0;OutputBytes=0}
$caught=$null
try{Invoke-Remote -Board A -Step 'local-empty-repro' -CommandText 'LOCAL_STUB_ONLY' | Out-Null}catch{$caught=$_}
if(-not $caught){throw 'Empty collection incorrectly passed'}
if($caught.Exception.Message -notmatch 'REMOTE_CAPTURE_INCOMPLETE.*NULL_TEXT'){
    Write-Output $caught.Exception.Message
    Write-Output $caught.ScriptStackTrace
    throw 'RED: actual remote entry lacks explicit missing-evidence result'
}
Write-Output 'PASS actual Invoke-Remote rejects synthetic zero-byte/null receipt explicitly'
$cases=@(
    @{Name='null';Value=$null;Reason='NULL_TEXT'},
    @{Name='empty';Value='';Reason='EMPTY_TEXT'},
    @{Name='wrong-type';Value=@('text');Reason='INVALID_TEXT_TYPE'},
    @{Name='truncated';Value='MDDS_REMOTE_EX';Reason='REMOTE_EXIT_MARKER_MISSING_OR_DUPLICATE'},
    @{Name='unrelated';Value='legitimate unrelated text';Reason='REMOTE_EXIT_MARKER_MISSING_OR_DUPLICATE'},
    @{Name='valid';Value="body`nMDDS_REMOTE_EXIT=0";Reason='NONE'},
    @{Name='duplicate';Value="MDDS_REMOTE_EXIT=0`nMDDS_REMOTE_EXIT=0";Reason='REMOTE_EXIT_MARKER_MISSING_OR_DUPLICATE'}
)
foreach($case in $cases){
    $script:fixture=[pscustomobject]@{Root=$scratch;Text=$case.Value;LocalExitCode=0}
    $failed=$false
    try{Invoke-Remote -Board A -Step ('local-'+$case.Name) -CommandText 'LOCAL_STUB_ONLY' | Out-Null}catch{$failed=$true;if($_.Exception.Message -notmatch $case.Reason){throw}}
    if($failed -eq ($case.Reason -eq 'NONE')){throw "Unexpected result $($case.Name)"}
}
foreach($fixture in @([pscustomobject]@{Root=$scratch;LocalExitCode=0},[pscustomobject]@{Root=$scratch;Text='MDDS_REMOTE_EXIT=0';LocalExitCode=0;Cancelled=$true},[pscustomobject]@{Root=$scratch;Text='MDDS_REMOTE_EXIT=0';LocalExitCode=0;CollectionComplete=$false})){
    if((Test-RemoteCommandReceipt $fixture).status -cne 'INCOMPLETE'){throw 'Missing field/cancelled/open stream incorrectly passed'}
    $script:fixture=$fixture;$rejected=$false
    try{Invoke-Remote -Board A -Step 'local-incomplete' -CommandText 'LOCAL_STUB_ONLY' | Out-Null}catch{if($_.Exception.Message -notmatch 'REMOTE_CAPTURE_INCOMPLETE'){throw};$rejected=$true}
    if(-not $rejected){throw 'Actual entry accepted incomplete/cancelled command'}
}
$checked=Test-RemoteCommandReceipt ([pscustomobject]@{Text=$null;LocalExitCode=0;OutputBytes=0})
if($checked.local_exit -ne 0 -or $checked.output_bytes -ne 0){throw 'Upstream empty exit0 evidence lost'}
Write-Output 'PASS typed missing/empty/wrong/truncated/unrelated/valid/cancelled/unfinished inputs; upstream exit0 retained'
