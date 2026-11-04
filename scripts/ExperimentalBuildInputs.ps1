# Build-manifest parsers only. No compiler, SDK, deployment or Git invocation.
function Read-MddsNinjaDependencies {
    param([Parameter(Mandatory)][string]$Text, [int]$ExitCode = 0)
    if($ExitCode -ne 0){throw "Ninja dependency command failed: $ExitCode"}
    $paths=[Collections.Generic.List[string]]::new();$targets=[Collections.Generic.List[object]]::new();$current=$null
    foreach($line in [regex]::Split($Text,'\r?\n')) {
        if([string]::IsNullOrWhiteSpace($line)){continue}
        if($line -match '^(.+): #deps ([0-9]+), deps mtime ([0-9]+) \(([^)]+)\)$') {
            if($current -and $current.paths.Count -ne $current.expected){throw "Incomplete Ninja dependencies: $($current.target)"}
            if($Matches[4] -cne 'VALID'){throw "Non-current Ninja dependency record: $($Matches[4])"}
            $current=[pscustomobject]@{target=$Matches[1];expected=[int]$Matches[2];mtime=$Matches[3];paths=[Collections.Generic.List[string]]::new()}
            $targets.Add($current);continue
        }
        if($line -match '^    (.+)$' -and $current){$current.paths.Add($Matches[1]);$paths.Add($Matches[1]);continue}
        throw "Unrecognized Ninja dependency output: $line"
    }
    if(-not $current -or $current.paths.Count -ne $current.expected -or $paths.Count -eq 0){throw 'Missing or incomplete Ninja dependency records'}
    return [pscustomobject]@{Paths=@($paths.ToArray());Targets=@($targets.ToArray())}
}
function Read-MddsCompilerDepfile {
    param([Parameter(Mandatory)][string]$Text)
    # Clang -MD emits one make-style rule. Only a colon followed by whitespace
    # is the target separator; the colon in a Windows drive is not one.
    $flat=[regex]::Replace($Text,'\\\r?\n',' ').Trim()
    if($flat -match '[\r\n]'){throw 'Expected exactly one compiler dependency rule'}
    $separator=[regex]::Match($flat,'(?<!\\):[ \t]+')
    if(-not $separator.Success){throw 'Compiler dependency rule separator missing'}
    $body=$flat.Substring($separator.Index+$separator.Length)
    $paths=[Collections.Generic.List[string]]::new();$word=[Text.StringBuilder]::new()
    for($i=0;$i -lt $body.Length;$i++) {
        $c=$body[$i]
        if([char]::IsWhiteSpace($c)) {
            if($word.Length){$paths.Add($word.ToString());$null=$word.Clear()};continue
        }
        if($c -eq '\' -and $i+1 -lt $body.Length) {
            $next=$body[$i+1]
            if([char]::IsWhiteSpace($next) -or $next -in @('\','#',':')){$null=$word.Append($next);$i++;continue}
            # Clang may retain ordinary Windows directory separators.
        }
        if($c -eq '$' -and $i+1 -lt $body.Length -and $body[$i+1] -eq '$'){$i++}
        $null=$word.Append($c)
    }
    if($word.Length){$paths.Add($word.ToString())}
    if(-not $paths.Count){throw 'Compiler dependency rule has no prerequisites'}
    return $paths.ToArray()
}
function Read-MddsCmakeInputObject {
    param([Parameter(Mandatory)][string]$Text)
    $value=$Text | ConvertFrom-Json
    if($value.kind -cne 'cmakeFiles' -or $value.version.major -ne 1 -or
        -not [IO.Path]::IsPathRooted([string]$value.paths.source) -or -not $value.inputs -or -not @($value.inputs).Count){throw 'Invalid CMake cmakeFiles-v1 reply'}
    foreach($input in $value.inputs) {
        if(-not ($input.path -is [string]) -or [string]::IsNullOrWhiteSpace($input.path)){throw 'Missing CMake input path'}
        # File API documents source-tree paths as relative to paths.source;
        # generated/external CMake input paths are absolute.
        if([IO.Path]::IsPathRooted($input.path)){$input.path}
        else{[IO.Path]::Combine([string]$value.paths.source,[string]$input.path)}
    }
}
function Get-MddsDependencyHashes {
    param([Parameter(Mandatory)][string[]]$Paths,[Parameter(Mandatory)][string]$BaseDirectory)
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($input in $Paths) {
        if([string]::IsNullOrWhiteSpace($input)){throw 'Empty build input path'}
        $absolute=if([IO.Path]::IsPathRooted($input)){[IO.Path]::GetFullPath($input)}else{[IO.Path]::GetFullPath([IO.Path]::Combine($BaseDirectory,$input))}
        if(-not (Test-Path -LiteralPath $absolute -PathType Leaf)){throw "Build input is not an existing file: $absolute"}
        if($seen.Add($absolute)) {
            $hash=Get-FileHash -LiteralPath $absolute -Algorithm SHA256 -ErrorAction Stop
            [pscustomobject]@{path=$hash.Path;sha256=$hash.Hash.ToLowerInvariant()}
        }
    }
}
