# Pure, fail-closed snapshot checks for PLAN section 14 (Windows PowerShell 5.1+).
# No device access, JSON writes, ownership creation, scheduling, or permission IPC.
# A Passed result is about supplied snapshots only, never a lock or proof of runtime grants.
# Callers MUST check .Passed, bind ownership to a protected baseline file's SHA256,
# serialize board operations, and separately verify the service's permission state.
# Decode input file bytes with UTF8Encoding(false, true), not replacement-fallback
# Get-Content: a string-only guard cannot recover malformed original bytes or a
# stripped BOM. Preserve the exact text for the file-size check (no trimming).
# Reader limits below are pinned to security_access_token 53287abcc13b418c091154fdb629c37dcfa34ce6:
# nativetoken.c:GetFileBuff/GetTokenList and nativetoken_json_oper.c:Get*FromJson.
# Required exact keys, nonempty strings, unique identities, integer version=1 and
# explicit array types remain intentional fail-closed restrictions beyond that C reader.

function Get-NativePermissionUtf8Length {
    param([string]$Value)
    if ($Value.IndexOf([char]0) -ge 0) { throw 'JSON_UNICODE_INVALID' }
    try {
        $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
        return $utf8.GetByteCount($Value)
    } catch { throw 'JSON_UNICODE_INVALID' }
}

function Assert-NativePermissionJsonString {
    param([string]$Token)
    # Validate escapes before ConvertFrom-Json can replace an unpaired surrogate.
    # The JSON lexer has already checked each escape's lexical form. Match escaped
    # backslashes as a unit, so "\\uD800" is text, not an unpaired Unicode escape.
    if (-not $Token.Contains('\')) { return }
    $escapes = [regex]::Matches($Token, '\\(?:u(?<code>[0-9a-fA-F]{4})|.)')
    for ($i = 0; $i -lt $escapes.Count; $i++) {
        if (-not $escapes[$i].Groups['code'].Success) { continue }
        $code = [Convert]::ToInt32($escapes[$i].Groups['code'].Value, 16)
        if ($code -eq 0 -or ($code -ge 0xDC00 -and $code -le 0xDFFF)) { throw 'JSON_UNICODE_INVALID' }
        if ($code -ge 0xD800 -and $code -le 0xDBFF) {
            if ($i + 1 -ge $escapes.Count -or $escapes[$i + 1].Index -ne $escapes[$i].Index + 6 -or
                -not $escapes[$i + 1].Groups['code'].Success) { throw 'JSON_UNICODE_INVALID' }
            $low = [Convert]::ToInt32($escapes[$i + 1].Groups['code'].Value, 16)
            if ($low -lt 0xDC00 -or $low -gt 0xDFFF) { throw 'JSON_UNICODE_INVALID' }
            $i++
        }
    }
}

function ConvertFrom-NativePermissionJson {
    param([string]$Json, [ValidateSet('Array', 'Object')][string]$RootKind)
    # GetFileBuff rejects > MAX_JSON_FILE_LEN, not >=. This is UTF-8 file bytes.
    if ((Get-NativePermissionUtf8Length $Json) -gt 1024000) { throw 'JSON_TOO_LARGE' }
    # ConvertFrom-Json silently accepts repeated same-case keys on Windows PowerShell.
    # Validate JSON grammar and decoded key uniqueness before materializing its objects.
    $pattern = '\G[ \t\r\n]*(?:(?<str>"(?:[^"\\\x00-\x1f]|\\(?:["\\/bfnrt]|u[0-9a-fA-F]{4}))*")|(?<value>true|false|null|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)|(?<punct>[{}\[\]:,]))'
    $lexer = New-Object System.Text.RegularExpressions.Regex($pattern)
    $stack = New-Object 'System.Collections.Generic.List[object]'
    $offset = 0
    $started = $false
    while ($offset -lt $Json.Length) {
        $match = $lexer.Match($Json, $offset)
        if (-not $match.Success) {
            if ($Json.Substring($offset) -match '\A[ \t\r\n]*\z') { break }
            throw 'JSON_INVALID'
        }
        $offset = $match.Index + $match.Length
        $token = $match.Groups['str'].Value + $match.Groups['value'].Value + $match.Groups['punct'].Value
        $isString = $match.Groups['str'].Success
        if ($isString) { Assert-NativePermissionJsonString $token }
        if ($stack.Count -eq 0) {
            if ($started) { throw 'JSON_INVALID' }
            if (($RootKind -eq 'Array' -and $token -cne '[') -or
                ($RootKind -eq 'Object' -and $token -cne '{')) { throw 'JSON_ROOT_INVALID' }
            $started = $true
        } else {
            $frame = $stack[$stack.Count - 1]
            if ($token -ceq '}' -or $token -ceq ']') {
                if (($token -ceq '}' -and ($frame.Kind -cne '{' -or $frame.State -notin @('FirstKey', 'End'))) -or
                    ($token -ceq ']' -and ($frame.Kind -cne '[' -or $frame.State -notin @('FirstValue', 'End')))) {
                    throw 'JSON_INVALID'
                }
                $stack.RemoveAt($stack.Count - 1)
                continue
            }
            if ($token -ceq ',') {
                if ($frame.State -cne 'End') { throw 'JSON_INVALID' }
                if ($frame.Kind -ceq '{') { $frame.State = 'Key' } else { $frame.State = 'Value' }
                continue
            }
            if ($token -ceq ':') {
                if ($frame.State -cne 'Colon') { throw 'JSON_INVALID' }
                $frame.State = 'Value'
                continue
            }
            if ($frame.State -in @('FirstKey', 'Key')) {
                if (-not $isString) { throw 'JSON_INVALID' }
                $key = (ConvertFrom-Json -InputObject ('{"key":' + $token + '}') -ErrorAction Stop).key
                # Case aliases are rejected too: PSObject property access is case-insensitive.
                if (-not $frame.Names.Add($key)) { throw 'JSON_DUPLICATE_KEY' }
                $frame.State = 'Colon'
                continue
            }
            if ($frame.State -notin @('Value', 'FirstValue')) { throw 'JSON_INVALID' }
            $frame.State = 'End'
        }
        if ($token -ceq '{' -or $token -ceq '[') {
            if ($stack.Count -ge 64) { throw 'JSON_DEPTH_UNSUPPORTED' }
            $state = 'FirstValue'
            if ($token -ceq '{') { $state = 'FirstKey' }
            $names = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
            $stack.Add(@{ Kind = $token; State = $state; Names = $names })
        } elseif (-not ($isString -or $match.Groups['value'].Success)) {
            throw 'JSON_INVALID'
        }
    }
    if (-not $started -or $stack.Count -ne 0) { throw 'JSON_INVALID' }
    # A wrapper preserves [] and single-item arrays without pipeline enumeration.
    $parsed = ConvertFrom-Json -InputObject ('{"data":' + $Json + '}') -ErrorAction Stop
    [pscustomobject]@{ Data = $parsed.data }
}

function Test-NativePermissionInteger {
    param($Value, [UInt64]$Minimum = 0, [UInt64]$Maximum = 4294967295)
    # Numeric strings, booleans and fractional/exponent JSON numbers are not schema integers.
    if ($Value -isnot [int] -and $Value -isnot [long] -and $Value -isnot [uint32] -and $Value -isnot [uint64]) { return $false }
    return ($Value -ge $Minimum -and $Value -le $Maximum)
}

function Test-NativePermissionTokenId {
    param($Value, [switch]$AllowShell)
    # cJSON valueint is signed int; > INT32_MAX can clamp and change its type bits.
    if (-not (Test-NativePermissionInteger $Value 1 2147483647)) { return $false }
    $type = ([int64]$Value -shr 27) -band 3
    return ($type -eq 1 -or ($AllowShell -and $type -eq 2))
}

function ConvertTo-NativePermissionCanonical {
    param([AllowNull()]$Value)
    if ($null -eq $Value) { return 'null' }
    if ($Value -is [System.Management.Automation.PSCustomObject]) {
        [string[]]$keys = @($Value.PSObject.Properties.Name)
        [Array]::Sort($keys, [StringComparer]::Ordinal)
        $parts = foreach ($key in $keys) {
            (ConvertTo-Json -InputObject $key -Compress) + ':' + (ConvertTo-NativePermissionCanonical $Value.$key)
        }
        return '{' + ($parts -join ',') + '}'
    }
    if ($Value -is [Array]) {
        $parts = foreach ($element in $Value) { ConvertTo-NativePermissionCanonical $element }
        return '[' + ($parts -join ',') + ']'
    }
    return (ConvertTo-Json -InputObject $Value -Depth 64 -Compress)
}

function Assert-NativePermissionRecords {
    param([AllowEmptyCollection()][object[]]$Records)
    # GetTokenList rejects []; AtlibInit then clears the persistent file.
    if ($Records.Count -eq 0) { throw 'SCHEMA_INVALID' }
    $required = @('processName', 'APL', 'version', 'tokenId', 'tokenAttr', 'dcaps', 'permissions', 'nativeAcls')
    $names = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    $tokens = New-Object 'System.Collections.Generic.HashSet[uint64]'
    foreach ($record in $Records) {
        if ($record -isnot [System.Management.Automation.PSCustomObject]) { throw 'SCHEMA_INVALID' }
        $keys = @($record.PSObject.Properties.Name)
        foreach ($key in $required) { if ($keys -cnotcontains $key) { throw 'SCHEMA_INVALID' } }
        if ($record.processName -isnot [string] -or $record.processName.Length -eq 0 -or
            (Get-NativePermissionUtf8Length $record.processName) -gt 256 -or
            -not (Test-NativePermissionInteger $record.APL 1 3) -or
            -not (Test-NativePermissionInteger $record.version 1 1) -or
            -not (Test-NativePermissionTokenId $record.tokenId -AllowShell) -or
            -not (Test-NativePermissionInteger $record.tokenAttr)) { throw 'SCHEMA_INVALID' }
        if (-not $names.Add($record.processName) -or -not $tokens.Add([UInt64]$record.tokenId)) { throw 'DUPLICATE_IDENTITY' }
        foreach ($key in @('dcaps', 'permissions', 'nativeAcls')) {
            # The file reader uses MAX_MALLOC_SIZE=8192, NOT the write API's
            # MAX_DCAPS_NUM=32 / MAX_PERM_NUM=1024 (attr.maxStrNum is unused).
            if ($record.$key -isnot [Array] -or $record.$key.Count -gt 8192) { throw 'SCHEMA_INVALID' }
            $maxBytes = 256
            if ($key -ceq 'dcaps') { $maxBytes = 1024 }
            foreach ($entry in $record.$key) {
                if ($entry -isnot [string] -or $entry.Length -eq 0 -or
                    (Get-NativePermissionUtf8Length $entry) -gt $maxBytes) { throw 'SCHEMA_INVALID' }
            }
        }
        if ($record.processName -ceq 'fastdds_dsoftbus_probe') {
            if (-not (Test-NativePermissionTokenId $record.tokenId) -or $record.APL -ne 1 -or
                $record.tokenAttr -ne 0 -or $record.dcaps.Count -ne 0 -or $record.nativeAcls.Count -ne 0) {
                throw 'PROJECT_SCHEMA_INVALID'
            }
            foreach ($key in $keys) {
                if ($required -cnotcontains $key -and $key -match '(?i)perm|acl|cap|apl|token|grant|privileg|role|(^|_)(uid|gid)($|_)') {
                    throw 'PROJECT_SCHEMA_UNSUPPORTED'
                }
            }
            if ($record.permissions.Count -gt 1 -or
                ($record.permissions.Count -eq 1 -and $record.permissions[0] -cne 'ohos.permission.DISTRIBUTED_DATASYNC')) {
                throw 'PROJECT_SCHEMA_INVALID'
            }
        }
    }
}

function New-NativePermissionGuardResult {
    param([bool]$Passed, [string]$Code, [UInt64]$TokenId, [string]$Phase)
    [pscustomobject]@{ Passed = $Passed; Code = $Code; ProcessName = 'fastdds_dsoftbus_probe'; TokenId = $TokenId; Phase = $Phase }
}

function Get-NativePermissionFailureCode {
    param([string]$Message)
    # Never return parser errors (which can echo JSON contents), names, paths or foreign tokens.
    $codes = @('JSON_INVALID', 'JSON_ROOT_INVALID', 'JSON_DUPLICATE_KEY', 'JSON_DEPTH_UNSUPPORTED', 'JSON_UNICODE_INVALID', 'JSON_TOO_LARGE',
        'SCHEMA_INVALID', 'DUPLICATE_IDENTITY', 'PROJECT_SCHEMA_INVALID', 'PROJECT_SCHEMA_UNSUPPORTED',
        'NON_PROJECT_CHANGED', 'PROJECT_METADATA_CHANGED', 'TOKEN_MISMATCH', 'PHASE_MISMATCH', 'OWNERSHIP_MISMATCH')
    if ($codes -ccontains $Message) { return $Message }
    return 'INVALID_INPUT'
}

function Test-NativePermissionSnapshot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$BaselineJson,
        [Parameter(Mandatory)][AllowEmptyString()][string]$CurrentJson,
        [Parameter(Mandatory)][ValidateSet('Absent', 'Granted', 'Revoked')][string]$RequirePhase,
        [Parameter(Mandatory)][UInt64]$ExpectedToken
    )
    try {
        if (($RequirePhase -eq 'Absent' -and $ExpectedToken -ne 0) -or
            ($RequirePhase -ne 'Absent' -and -not (Test-NativePermissionTokenId $ExpectedToken))) { throw 'TOKEN_MISMATCH' }
        $baseline = (ConvertFrom-NativePermissionJson $BaselineJson 'Array').Data
        $current = (ConvertFrom-NativePermissionJson $CurrentJson 'Array').Data
        Assert-NativePermissionRecords -Records $baseline
        Assert-NativePermissionRecords -Records $current
        $baselineProject = @($baseline | Where-Object { $_.processName -ceq 'fastdds_dsoftbus_probe' })
        $currentProject = @($current | Where-Object { $_.processName -ceq 'fastdds_dsoftbus_probe' })
        foreach ($project in @($baselineProject) + @($currentProject)) {
            if ([UInt64]$project.tokenId -ne $ExpectedToken) { throw 'TOKEN_MISMATCH' }
        }
        [string[]]$before = @($baseline | Where-Object { $_.processName -cne 'fastdds_dsoftbus_probe' } |
            ForEach-Object { ConvertTo-NativePermissionCanonical $_ })
        [string[]]$after = @($current | Where-Object { $_.processName -cne 'fastdds_dsoftbus_probe' } |
            ForEach-Object { ConvertTo-NativePermissionCanonical $_ })
        [Array]::Sort($before, [StringComparer]::Ordinal)
        [Array]::Sort($after, [StringComparer]::Ordinal)
        if ($before.Count -ne $after.Count) { throw 'NON_PROJECT_CHANGED' }
        for ($i = 0; $i -lt $before.Count; $i++) {
            if ($before[$i] -cne $after[$i]) { throw 'NON_PROJECT_CHANGED' }
        }
        if ($RequirePhase -eq 'Absent') {
            if ($currentProject.Count -ne 0) { throw 'PHASE_MISMATCH' }
        } else {
            if ($currentProject.Count -ne 1) { throw 'PHASE_MISMATCH' }
            $count = $currentProject[0].permissions.Count
            if (($RequirePhase -eq 'Granted' -and $count -ne 1) -or
                ($RequirePhase -eq 'Revoked' -and $count -ne 0)) { throw 'PHASE_MISMATCH' }
            if ($baselineProject.Count -eq 1) {
                # Only permissions may change for a previously recorded project identity.
                $old = $baselineProject[0] | Select-Object -Property * -ExcludeProperty permissions
                $new = $currentProject[0] | Select-Object -Property * -ExcludeProperty permissions
                if ((ConvertTo-NativePermissionCanonical $old) -cne (ConvertTo-NativePermissionCanonical $new)) {
                    throw 'PROJECT_METADATA_CHANGED'
                }
            }
        }
        New-NativePermissionGuardResult $true 'PASS' $ExpectedToken $RequirePhase
    } catch {
        New-NativePermissionGuardResult $false (Get-NativePermissionFailureCode $_.Exception.Message) $ExpectedToken $RequirePhase
    }
}

function Test-NativePermissionOwnership {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$OwnershipJson,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Run,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Board,
        [Parameter(Mandatory)][AllowEmptyString()][string]$BaselineSha256,
        [Parameter(Mandatory)][UInt64]$ExpectedToken
    )
    try {
        if ([string]::IsNullOrWhiteSpace($Run) -or [string]::IsNullOrWhiteSpace($Board) -or
            $BaselineSha256 -notmatch '\A[0-9a-fA-F]{64}\z' -or
            ($ExpectedToken -ne 0 -and -not (Test-NativePermissionTokenId $ExpectedToken))) { throw 'OWNERSHIP_MISMATCH' }
        $owner = (ConvertFrom-NativePermissionJson $OwnershipJson 'Object').Data
        $keys = @($owner.PSObject.Properties.Name)
        foreach ($key in @('run', 'board', 'name', 'baselineSha256', 'expectedToken')) {
            if ($keys -cnotcontains $key) { throw 'SCHEMA_INVALID' }
        }
        if ($owner.run -isnot [string] -or $owner.board -isnot [string] -or $owner.name -isnot [string] -or
            $owner.baselineSha256 -isnot [string] -or -not (Test-NativePermissionInteger $owner.expectedToken) -or
            $owner.run -cne $Run -or $owner.board -cne $Board -or $owner.name -cne 'fastdds_dsoftbus_probe' -or
            $owner.baselineSha256 -notmatch '\A[0-9a-fA-F]{64}\z' -or $owner.baselineSha256 -ine $BaselineSha256 -or
            [UInt64]$owner.expectedToken -ne $ExpectedToken) { throw 'OWNERSHIP_MISMATCH' }
        New-NativePermissionGuardResult $true 'PASS' $ExpectedToken 'Ownership'
    } catch {
        New-NativePermissionGuardResult $false (Get-NativePermissionFailureCode $_.Exception.Message) $ExpectedToken 'Ownership'
    }
}
