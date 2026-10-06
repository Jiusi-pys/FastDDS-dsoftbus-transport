param([Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$repo=(Get-Location).Path
$source=Join-Path $repo 'scripts/public_socket/shared_reattach/executor/run-window.ps1'
$tokens=$null;$errors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Formal PowerShell source parse failed'}
$function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Invoke-HdcLogged'},$true)
if(-not $function){throw 'Formal transfer caller missing'}
. ([scriptblock]::Create($function.Extent.Text))
$windowRoot=[IO.Path]::GetFullPath($OutputRoot);New-Item -ItemType Directory -Path $windowRoot -ErrorAction Stop | Out-Null
$cfg=[pscustomobject]@{targets=[pscustomobject]@{A='UNIT_NOT_DEVICE'}}
$utf8=[Text.UTF8Encoding]::new($false)
$hdcPath='Invoke-InertTransferForTest'
function Invoke-InertTransferForTest {
    param([Parameter(ValueFromRemainingArguments)][string[]]$Arguments)
    $global:LASTEXITCODE=0
    if($script:injectFail){Write-Output '[Fail]Error opening file: no such file or directory, path:UNIT_INPUT'}
    else {Write-Output 'FileTransfer finish, Size:12, File count = 1, time:1ms rate:12.00kB/s'}
}
foreach($kind in @('rejected','accepted')){
    $script:injectFail=$kind -ceq 'rejected';$failed=$false
    try{Invoke-HdcLogged -Board A -Step $kind -NativeArguments @('file','send','UNIT_INPUT','UNIT_DESTINATION')|Out-Null}
    catch{if($_.Exception.Message -notlike 'HDC_FILE_TRANSFER_REJECTED:*'){throw};$failed=$true}
    if($failed -ne $script:injectFail){throw 'Outer rc0 overwrote file-transfer outcome'}
}
[ordered]@{scope='ACTUAL_FORMAL_POWERSHELL_FUNCTION_INERT_COMMAND_SEAM_NO_HDC';cases=2;pass=$true;source_sha256=(Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant();device_operations=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $windowRoot 'RESULT.json') -Encoding UTF8
Write-Output 'FILE_TRANSFER_REJECTION_PRESERVED; real HDC=0'
