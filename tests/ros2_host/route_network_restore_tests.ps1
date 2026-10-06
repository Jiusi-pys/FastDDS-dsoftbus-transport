param([string]$Executor=(Join-Path $PSScriptRoot '../../scripts/public_socket/shared_reattach/executor/run-window.ps1'))
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
# Extract only the pure normalizer. Never execute executor bootstrap or HDC.
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile([IO.Path]::GetFullPath($Executor),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Executor parse errors'}
$function=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-NormalizedRouteNetworkState'},$true))
if($function.Count -ne 1){throw 'Normalizer definition missing or ambiguous'}
. ([scriptblock]::Create($function[0].Extent.Text))
$link="3: eth1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP mode DEFAULT group default qlen 1000`n    link/ether 02:00:00:00:00:01 brd ff:ff:ff:ff:ff:ff"
$addr="3: eth1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 state UP`n    inet 192.0.2.10/24 brd 192.0.2.255 scope global eth1`n       valid_lft 300sec preferred_lft 200sec"
$routes="default via 192.0.2.1 dev eth1 proto dhcp metric 100 expires 40sec`n192.0.2.0/24 dev eth1 proto kernel scope link src 192.0.2.10"
$baseline=Get-NormalizedRouteNetworkState $link $addr $routes|ConvertTo-Json -Depth 8 -Compress
$checks=0
function Assert-Same([string]$L,[string]$A,[string]$R,[bool]$Expected) {
    $actual=Get-NormalizedRouteNetworkState $L $A $R|ConvertTo-Json -Depth 8 -Compress
    if(($actual -ceq $baseline) -ne $Expected){throw 'Normalized restoration assertion failed'}
    $script:checks++
}
Assert-Same $link ($addr -replace '300sec','290sec' -replace '200sec','190sec') ($routes -replace '40sec','30sec') $true
Assert-Same ($link -replace 'BROADCAST,MULTICAST,UP,LOWER_UP','LOWER_UP,UP,MULTICAST,BROADCAST') $addr $routes $true
Assert-Same ($link -replace ',UP,LOWER_UP',',LOWER_UP') $addr $routes $false
Assert-Same ($link -replace 'state UP','state DOWN') $addr $routes $false
Assert-Same ($link -replace 'mtu 1500','mtu 1400') $addr $routes $false
Assert-Same ($link -replace '02:00:00:00:00:01','02:00:00:00:00:02') $addr $routes $false
Assert-Same $link ($addr -replace '192.0.2.10/24','192.0.2.11/24') $routes $false
Assert-Same $link $addr ($routes -replace 'via 192.0.2.1','via 192.0.2.2') $false
Assert-Same $link $addr ($routes -replace 'metric 100','metric 101') $false
Assert-Same $link $addr ($routes -replace '192.0.2.0/24','192.0.3.0/24') $false
$refused=$false;try{Get-NormalizedRouteNetworkState $link 'no IPv4 address' $routes|Out-Null}catch{$refused=$true}
if(-not $refused){throw 'Missing IPv4 was accepted'};$checks++
Write-Output ("ROUTE_NETWORK_NORMALIZER checks="+$checks+" source=PURE_HOST_FIXTURE device_tested=0")
