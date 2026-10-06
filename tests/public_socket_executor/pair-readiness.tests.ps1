param([string]$ScriptPath=(Join-Path $PSScriptRoot '../../scripts/public_socket/shared_reattach/executor/run-window.ps1'))
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $ScriptPath),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Formal source syntax invalid'}
$definition=$ast.Find({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Assert-PairReady'},$true)
if(-not $definition){throw 'Pair readiness entry missing'}
. ([ScriptBlock]::Create($definition.Extent.Text))
$prep=$ast.Find({param($n) $n -is [System.Management.Automation.Language.TryStatementAst] -and $n.Body.Extent.Text.Contains("Step 'tools-before'")},$true)
if(-not $prep -or $prep.Body.Statements[0].Extent.Text.Trim() -cne 'Assert-PairReady'){throw 'Pair check must precede formal deployment preparation'}
$cfg=[pscustomobject]@{platform=[pscustomobject]@{A=[pscustomobject]@{parameters=@([pscustomobject]@{name='kernel';value='unit kernel'},[pscustomobject]@{name='const.product.name';value='unit product'})};B=[pscustomobject]@{parameters=@([pscustomobject]@{name='kernel';value='unit kernel'},[pscustomobject]@{name='const.product.name';value='unit product'})}}}
function Invoke-Remote([string]$Board,[string]$Step,[string]$CommandText){
 $script:reads.Add($Board+'/'+$Step)
 if($Board -ceq 'B' -and $script:case -ceq 'missing'){throw 'REMOTE_CAPTURE_INCOMPLETE: B/pair-ready NULL_TEXT'}
 $value=if($CommandText -ceq 'uname -a'){'unit kernel'}elseif($CommandText -ceq 'param get const.product.name'){'unit product'}else{throw 'Unexpected read-only command'}
 if($Board -ceq 'B' -and $script:case -ceq 'wrong'){$value='wrong platform'}
 return [pscustomobject]@{Text=($value+"`nMDDS_REMOTE_EXIT=0`n")}
}
foreach($script:case in @('complete','missing','wrong')){
 $script:reads=[Collections.Generic.List[string]]::new();$continued=$false;$failure=$null
 try {Assert-PairReady;$continued=$true}catch{$failure=$_.Exception.Message}
 if($continued -ne ($script:case -ceq 'complete')){throw "Pair guard outcome wrong: $script:case"}
 if($script:case -ceq 'complete' -and $script:reads.Count -ne 4){throw 'Both sides need both exact readonly checks'}
 if($script:case -cne 'complete' -and (-not $failure -or $continued)){throw 'Failed B must stop before preparation mutations'}
 Write-Output ('PASS actual pair guard '+$script:case+' READ_ONLY; no deployment/SDK/process')
}
