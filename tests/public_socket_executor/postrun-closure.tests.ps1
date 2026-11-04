param([string]$ScriptPath=(Join-Path $PSScriptRoot '../../scripts/public_socket/shared_reattach/executor/run-window.ps1'))
$ErrorActionPreference='Stop'
$tokens=$null; $errors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $ScriptPath),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Production PowerShell syntax invalid'}
$gate=@($ast.FindAll({param($n) $n -is [System.Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.Contains("status='NOT_RUN_COLLECTION_UNCONFIRMED'")},$true))
if($gate.Count -ne 1){throw 'Exact production postrun closure gate not found'}
$condition=$gate[0].Clauses[0].Item1.Extent.Text
$cases=@('all_closed','nested_recv_unknown','nested_analysis_unknown','capture_unknown','business_unknown')
foreach($case in $cases){
    $runResult=[pscustomobject]@{collectionCleanup='HDC_STREAMS_CLOSED';loads=[pscustomobject]@{A=[pscustomobject]@{collectorClosed=$true;loadChainClosed=$true;collectionCleanup='HDC_STREAMS_CLOSED'}};controls=@()}
    if($case -in @('nested_recv_unknown','nested_analysis_unknown')){$runResult.loads.A.loadChainClosed=$false}
    if($case -eq 'capture_unknown'){$runResult.loads.A.collectorClosed=$false}
    if($case -eq 'business_unknown'){$runResult.collectionCleanup='UNCONFIRMED'}
    $blocked=[bool](Invoke-Expression $condition)
    if($blocked -ne ($case -ne 'all_closed')){throw "Closure gate mismatch: $case"}
}
Write-Output 'PASS actual postrun gate: nested recv/analysis, capture and business closure; no HDC'
