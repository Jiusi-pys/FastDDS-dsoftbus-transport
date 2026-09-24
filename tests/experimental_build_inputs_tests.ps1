param([string]$ArtifactDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repo 'scripts/ExperimentalBuildInputs.ps1')
if(-not $ArtifactDirectory){$ArtifactDirectory=Join-Path $repo ('.artifacts/build-input-tests/'+[Guid]::NewGuid().ToString('N'))}
$ArtifactDirectory=[IO.Path]::GetFullPath($ArtifactDirectory)
New-Item -ItemType Directory -Path $ArtifactDirectory -ErrorAction Stop | Out-Null
$failures=0;$cases=0
function Check([string]$Name,[scriptblock]$Action){
    $script:cases++;$pass=$false;$errorText=''
    try{$pass=[bool](& $Action)}catch{$errorText=$_.Exception.Message}
    Write-Output "build_input_case=$Name pass=$pass scope=HOST_ONLY_NO_SDK error=$errorText"
    if(-not $pass){$script:failures++}
}
function Reject([scriptblock]$Action){try{& $Action | Out-Null;return $false}catch{return $true}}
Check 'ninja-windows-space' {
    $r=Read-MddsNinjaDependencies -Text "C:/work dir/app.o: #deps 2, deps mtime 123 (VALID)`r`n    C:/work dir/main.cpp`r`n    C:/SDK/include file.h`r`n`r`n"
    $r.Targets.Count -eq 1 -and $r.Paths.Count -eq 2 -and $r.Paths[0] -ceq 'C:/work dir/main.cpp' -and $r.Paths[1] -ceq 'C:/SDK/include file.h'
}
Check 'ninja-failure' {Reject {Read-MddsNinjaDependencies -Text "a: #deps 1, deps mtime 2 (VALID)`n    x" -ExitCode 1}}
Check 'ninja-stale' {Reject {Read-MddsNinjaDependencies -Text "a: #deps 1, deps mtime 2 (STALE)`n    x"}}
Check 'ninja-count-mismatch' {Reject {Read-MddsNinjaDependencies -Text "a: #deps 2, deps mtime 2 (VALID)`n    x"}}
Check 'depfile-escaped-drive-space-continuation' {
    $text='C:/out\ file.so: C:/src/main.cpp C:/Program\ Files/sdk.h \' + "`r`n" + ' C:\dir\plain.h C:/hash\#name.h C:/cash$$name.h'
    $r=@(Read-MddsCompilerDepfile -Text $text)
    ($r -join '|') -ceq 'C:/src/main.cpp|C:/Program Files/sdk.h|C:\dir\plain.h|C:/hash#name.h|C:/cash$name.h'
}
Check 'depfile-missing-rule' {Reject {Read-MddsCompilerDepfile -Text 'C:/src/a.cpp'}}
Check 'depfile-second-rule-rejected' {Reject {Read-MddsCompilerDepfile -Text "a.o: one.h`nb.o: two.h"}}
Check 'cmake-source-relative-and-absolute' {
    $json=@{kind='cmakeFiles';version=@{major=1};paths=@{source='C:/source tree';build='C:/out tree'};inputs=@(@{path='CMakeLists.txt'},@{path='C:/SDK/import.cmake';isExternal=$true},@{path='C:/out tree/CMakeFiles/system.cmake';isGenerated=$true})}|ConvertTo-Json -Depth 5
    $r=@(Read-MddsCmakeInputObject -Text $json)
    $r.Count -eq 3 -and ($r[0] -replace '\\','/') -ceq 'C:/source tree/CMakeLists.txt' -and $r[1] -ceq 'C:/SDK/import.cmake' -and $r[2] -ceq 'C:/out tree/CMakeFiles/system.cmake'
}
Check 'cmake-reply-error' {Reject {Read-MddsCmakeInputObject -Text '{"error":"unsupported kind"}'}}
Check 'cmake-missing-inputs' {Reject {Read-MddsCmakeInputObject -Text '{"kind":"cmakeFiles","version":{"major":1},"paths":{"source":"C:/source"}}'}}
Check 'missing-input-rejected' {Reject {Get-MddsDependencyHashes -Paths @('missing/header.h') -BaseDirectory $ArtifactDirectory}}
Check 'existing-input-dedup-hash' {
    $path=Join-Path $ArtifactDirectory 'space name.h';[IO.File]::WriteAllText($path,'owned fixture',[Text.UTF8Encoding]::new($false))
    $r=@(Get-MddsDependencyHashes -Paths @($path,'space name.h') -BaseDirectory $ArtifactDirectory)
    $r.Count -eq 1 -and $r[0].sha256 -ceq (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
Check 'directory-input-rejected' {Reject {Get-MddsDependencyHashes -Paths @($ArtifactDirectory) -BaseDirectory $ArtifactDirectory}}
Write-Output "build_input_summary cases=$cases failures=$failures scope=HOST_ONLY_NO_SDK"
if($failures){exit 1};exit 0
