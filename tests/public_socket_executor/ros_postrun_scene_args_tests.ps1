# Execute actual canonical assignment statements under Windows PowerShell 5.1.
# Pure local arguments only: no SDK/HDC, private inputs, or device scope.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source=Get-Content -Raw -LiteralPath (Join-Path $repo 'scripts/public_socket/shared_reattach/executor/run-window.ps1')
$assignments=@()
foreach($prefix in @('    $nativeReaccept =','    $sceneArgs =','    [string[]]$routeArgs =')){
    $lines=@($source -split "`r?`n" | Where-Object {$_.StartsWith($prefix)})
    if($lines.Count -ne 1){throw 'Actual source assignment missing or ambiguous'}
    $assignments+=$lines[0]
}
$logic=[scriptblock]::Create($assignments -join "`n")
$capture=Join-Path $PSScriptRoot 'ps_scene_argv_echo.py'
foreach($native in @($false,$true)){
    foreach($route in @($false,$true)){
        foreach($order in @('outgoing-first','accepted-first')){
            $row=@{}
            if($native){$row.bounded_native_reaccept=@{order=$order}}
            if($route){$row.route_lifecycle_native=@{contract='A_ETH1_ONE_RETURN'}}
            $cfg=($row|ConvertTo-Json -Depth 3)|ConvertFrom-Json
            . $logic
            if($routeArgs -isnot [string[]]){throw 'Route splat must be a typed string array'}
            $nativeOutput=& python $capture @sceneArgs @routeArgs
            if($LASTEXITCODE -ne 0){throw 'Native argv oracle failed'}
            $actual=$nativeOutput | ConvertFrom-Json
            $expected=@(if($native){'--order';$order}else{'--mode';'dual'};if($route){'--route-lifecycle-native'})
            if(($actual|ConvertTo-Json -Compress) -cne ($expected|ConvertTo-Json -Compress)){throw 'Actual source argument sequence mismatch'}
            if($route -and $actual.Count -ne 3){throw 'Route flag split into characters'}
            if(-not $route -and $routeArgs.Count -ne 0){throw 'Unrequested route flag introduced'}
        }
    }
}
Write-Output ('PASS ACTUAL_SOURCE_SCENE_ARG_CASES=8 ENGINE='+$PSVersionTable.PSVersion.ToString()+' SDK=0 HDC=0')
