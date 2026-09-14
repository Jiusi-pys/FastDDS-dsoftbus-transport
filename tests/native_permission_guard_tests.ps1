param(
    [string]$EvidenceDirectory = (Join-Path $PSScriptRoot '../.artifacts/tdd/permissions-guard'),
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunLabel = 'test'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../scripts/NativePermissionGuard.ps1')
$script:checks = 0
$script:failures = 0
$script:lines = New-Object 'System.Collections.Generic.List[string]'

function Check([string]$Name, [bool]$Expected, [scriptblock]$Action) {
    $script:checks++
    try {
        $result = @(& $Action)
        if ($result.Count -ne 1 -or $result[0].Passed -isnot [bool]) { throw 'Invalid result contract.' }
        if ($result[0].Passed -ne $Expected) { throw ('Unexpected result: ' + $result[0].Code) }
        if (($result[0] | ConvertTo-Json -Compress) -match 'unrelated_identity|OTHER_PERMISSION') {
            throw 'Result leaked unrelated identity data.'
        }
        $line = 'PASS ' + $Name
    } catch {
        $script:failures++
        $line = 'FAIL ' + $Name + ': ' + $_.Exception.Message
    }
    $script:lines.Add($line)
    Write-Output $line
}

function Record([string]$Name, [UInt64]$Token, [string[]]$Permissions = @()) {
    [ordered]@{ processName = $Name; APL = 1; version = 1; tokenId = $Token; tokenAttr = 0;
        dcaps = @(); permissions = @($Permissions); nativeAcls = @() }
}
function Json($Value) { ConvertTo-Json -InputObject $Value -Depth 32 -Compress }
function Snapshot([string]$Current, [string]$Phase = 'Granted', [UInt64]$Token = 671088739, [string]$Baseline = $script:baseline) {
    Test-NativePermissionSnapshot -BaselineJson $Baseline -CurrentJson $Current -RequirePhase $Phase -ExpectedToken $Token
}
$script:other = Record 'unrelated_identity' 671088738 @('OTHER_PERMISSION')
$script:project = Record 'fastdds_dsoftbus_probe' 671088739 @('ohos.permission.DISTRIBUTED_DATASYNC')
$script:emptyProject = Record 'fastdds_dsoftbus_probe' 671088739
$script:baseline = Json @($script:other)
$script:granted = Json @($script:other, $script:project)
$script:revoked = Json @($script:other, $script:emptyProject)
$script:owner = @{ run = 'run-1'; board = 'board-A'; name = 'fastdds_dsoftbus_probe'; baselineSha256 = ('a' * 64); expectedToken = 671088739 }
function Ownership([string]$Value = (Json $script:owner), [UInt64]$Token = 671088739) {
    Test-NativePermissionOwnership -OwnershipJson $Value -Run 'run-1' -Board 'board-A' -BaselineSha256 ('a' * 64) -ExpectedToken $Token
}

# Synthetic fixtures only: never import a real device token file in this suite.
$fixtureDirectory = Join-Path ([IO.Path]::GetTempPath()) ('fastdds-native-guard-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixtureDirectory)
$utf8 = New-Object System.Text.UTF8Encoding($false)
foreach ($item in @{ baseline = $script:baseline; granted = $script:granted; revoked = $script:revoked }.GetEnumerator()) {
    [IO.File]::WriteAllText((Join-Path $fixtureDirectory ($item.Key + '.json')), $item.Value, $utf8)
}
$fixtureHashesBefore = @(Get-ChildItem -LiteralPath $fixtureDirectory -File | Get-FileHash | Select-Object Path, Hash)

Check 'initial absent' $true { Snapshot $script:baseline 'Absent' 0 }
Check 'grant exact permission' $true { Snapshot $script:granted }
Check 'revoke keeps token' $true { Snapshot $script:revoked 'Revoked' }
Check 'already granted snapshot' $true { Snapshot $script:granted 'Granted' 671088739 $script:granted }
Check 'grant existing revoked identity' $true { Snapshot $script:granted 'Granted' 671088739 $script:revoked }
Check 'empty baseline rejected by downstream reader' $false { Snapshot (Json @($script:project)) 'Granted' 671088739 '[]' }
Check 'empty current array rejected by downstream reader' $false { Snapshot '[]' 'Absent' 0 '[]' }
Check 'object order and record order do not change contents' $true {
    $reordered = [ordered]@{ nativeAcls=@(); permissions=@('OTHER_PERMISSION'); dcaps=@(); tokenAttr=0;
        tokenId=671088738; version=1; APL=1; processName='unrelated_identity' }
    Snapshot (Json @($script:project, $reordered))
}
Check 'non-project added' $false { Snapshot (Json @($script:other, $script:project, (Record 'third' 671088740))) }
Check 'non-project removed' $false { Snapshot (Json @($script:project)) }
Check 'non-project permission changed' $false { Snapshot (Json @((Record 'unrelated_identity' 671088738 @()), $script:project)) }
Check 'non-project unknown metadata changed' $false {
    $changed = Record 'unrelated_identity' 671088738 @('OTHER_PERMISSION'); $changed.metadata = 'new'
    Snapshot (Json @($changed, $script:project))
}
Check 'unchanged extra fields accepted' $true {
    $extra = Record 'unrelated_identity' 671088738 @('OTHER_PERMISSION'); $extra.metadata = @{ a = @('v', 2) }
    Snapshot (Json @($extra, $script:project)) 'Granted' 671088739 (Json @($extra))
}
Check 'project benign extra field accepted' $true {
    $p = Record 'fastdds_dsoftbus_probe' 671088739 @('ohos.permission.DISTRIBUTED_DATASYNC'); $p.note = 'diagnostic'
    Snapshot (Json @($script:other, $p))
}
Check 'project extra field cannot disappear on update' $false {
    $p = Record 'fastdds_dsoftbus_probe' 671088739; $p.note = 'diagnostic'
    Snapshot $script:granted 'Granted' 671088739 (Json @($script:other, $p))
}
Check 'duplicate project records' $false { Snapshot (Json @($script:other, $script:project, $script:project)) }
Check 'duplicate baseline project records' $false { Snapshot $script:granted 'Granted' 671088739 (Json @($script:project, $script:project)) }
Check 'project token colliding with another identity rejected' $false {
    Snapshot (Json @($script:other, (Record 'fastdds_dsoftbus_probe' 671088738 @('ohos.permission.DISTRIBUTED_DATASYNC')))) 'Granted' 671088738
}
Check 'invalid non-project schema stops a later token writer' $false {
    $broken = Record 'unrelated_identity' 671088738 @('OTHER_PERMISSION'); $broken.permissions = $null
    Snapshot (Json @($broken, $script:project)) 'Granted' 671088739 (Json @($broken))
}
Check 'non-project array order change is still content change' $false {
    $before = Record 'unrelated_identity' 671088738 @('OTHER_PERMISSION'); $before.metadata = @(1, 2)
    $after = Record 'unrelated_identity' 671088738 @('OTHER_PERMISSION'); $after.metadata = @(2, 1)
    Snapshot (Json @($after, $script:project)) 'Granted' 671088739 (Json @($before))
}
Check 'baseline project token cannot change' $false {
    Snapshot $script:granted 'Granted' 671088739 (Json @($script:other, (Record 'fastdds_dsoftbus_probe' 671088827)))
}
Check 'wrong expected token' $false { Snapshot $script:granted 'Granted' 671088827 }
Check 'grant expected token zero rejected' $false { Snapshot $script:granted 'Granted' 0 }
Check 'absent expected token must be zero' $false { Snapshot $script:baseline 'Absent' 671088739 }
Check 'missing project when granted' $false { Snapshot $script:baseline }
Check 'project present when absent' $false { Snapshot $script:granted 'Absent' 0 }
Check 'revocation cannot mean deleted record' $false { Snapshot $script:baseline 'Revoked' }
Check 'granted does not satisfy revoked' $false { Snapshot $script:granted 'Revoked' }
Check 'revoked does not satisfy granted' $false { Snapshot $script:revoked }

foreach ($invalid in @('', 'null', '{}', '{"records":[]}', '[', '[1]', '[null]', '[] false', '[] , "data": []', '[{},]', '/*x*/[]',
    '[{} {}]', '[{"x" "y"}]', '[{"x":01}]', '[{"x":truefalse}]', '[{"x":1,}]', '[{"x":NaN}]')) {
    $fixture = $invalid
    Check ('reject invalid current ' + $script:checks) $false { Snapshot $fixture }
    Check ('reject invalid baseline ' + $script:checks) $false { Snapshot $script:granted 'Granted' 671088739 $fixture }
}
Check 'duplicate JSON property rejected' $false { Snapshot ($script:granted.Replace('"APL":1', '"APL":1,"APL":1')) }
Check 'escaped duplicate JSON property rejected' $false { Snapshot ($script:granted.Replace('"APL":1', '"APL":1,"\u0041PL":1')) }
Check 'unknown schema property casing rejected' $false { Snapshot ($script:granted.Replace('"APL":', '"apl":')) }
Check 'duplicate keys in nested unknown metadata rejected' $false {
    Snapshot ($script:granted.Replace('"processName":"fastdds_dsoftbus_probe"', '"metadata":{"x":1,"x":2},"processName":"fastdds_dsoftbus_probe"'))
}
Check 'JSON escapes and punctuation are values not grammar' $true {
    $other = Record 'unrelated_identity' 671088738 @('OTHER_PERMISSION'); $other.metadata = 'quotes: " and braces: [{},:] and slash: \'
    Snapshot (Json @($other, $script:project)) 'Granted' 671088739 (Json @($other))
}
foreach ($field in @('processName', 'APL', 'version', 'tokenId', 'tokenAttr', 'dcaps', 'permissions', 'nativeAcls')) {
    $key = $field
    Check ('missing required field ' + $key) $false {
        $p = Record 'fastdds_dsoftbus_probe' 671088739 @('ohos.permission.DISTRIBUTED_DATASYNC'); $p.Remove($key)
        Snapshot (Json @($script:other, $p))
    }
}
foreach ($mutation in @(
    @{ key='APL'; value=2 }, @{ key='APL'; value='normal' }, @{ key='APL'; value=1.5 },
    @{ key='version'; value=2 }, @{ key='tokenAttr'; value=1 }, @{ key='tokenId'; value='671088739' },
    @{ key='tokenId'; value=0 }, @{ key='tokenId'; value=4294967296 }, @{ key='tokenId'; value=$true },
    @{ key='permissions'; value='ohos.permission.DISTRIBUTED_DATASYNC' }, @{ key='permissions'; value=$null },
    @{ key='permissions'; value=@('ohos.permission.DISTRIBUTED_DATASYNC', 'ohos.permission.DISTRIBUTED_SOFTBUS_CENTER') },
    @{ key='permissions'; value=@('ohos.permission.DISTRIBUTED_DATASYNC', 'ohos.permission.DISTRIBUTED_DATASYNC') },
    @{ key='dcaps'; value=@('AT_CAP') }, @{ key='nativeAcls'; value=@('ANY_ACL') },
    @{ key='permissionFlags'; value=@('unexpected') }
)) {
    $change = $mutation
    Check ('reject mutated grant ' + $script:checks) $false {
        $p = Record 'fastdds_dsoftbus_probe' 671088739 @('ohos.permission.DISTRIBUTED_DATASYNC'); $p[$change.key] = $change.value
        Snapshot (Json @($script:other, $p))
    }
}
Check 'exact ownership' $true { Ownership }
Check 'uppercase hash accepted as same digest' $true {
    $o = $script:owner.Clone(); $o.baselineSha256 = ('A' * 64); Ownership (Json $o)
}
Check 'pre-grant ownership zero token' $true { $o = $script:owner.Clone(); $o.expectedToken = 0; Ownership (Json $o) 0 }
foreach ($mutation in @(
    @{ key='run'; value='run-2' }, @{ key='board'; value='board-B' },
    @{ key='name'; value='dsoftbus_service' }, @{ key='baselineSha256'; value=('b' * 64) },
    @{ key='expectedToken'; value=671088827 }, @{ key='expectedToken'; value='671088739' },
    @{ key='baselineSha256'; value='not-a-sha256' }, @{ key='run'; value='RUN-1' }, @{ key='expectedToken'; value=-1 }
)) {
    $change = $mutation
    Check ('reject wrong ownership ' + $change.key) $false { $o = $script:owner.Clone(); $o[$change.key] = $change.value; Ownership (Json $o) }
}
foreach ($field in @('run', 'board', 'name', 'baselineSha256', 'expectedToken')) {
    $key = $field
    Check ('reject missing ownership ' + $key) $false { $o = $script:owner.Clone(); $o.Remove($key); Ownership (Json $o) }
}
Check 'invalid ownership JSON' $false { Ownership '{' }
Check 'array ownership rejected' $false { Ownership (Json @($script:owner)) }
Check 'duplicate ownership key rejected' $false { Ownership ((Json $script:owner).Replace('"run":"run-1"', '"run":"run-1","run":"run-1"')) }

# P1: match the actual nativetoken file reader, not only the write-API input checks.
# 671088xxx = version 1/native type 1; 805306xxx = version 1/shell type 2.
Check 'P1 valid foreign shell token' $true {
    $foreign = Record 'unrelated_identity' 805306466 @('OTHER_PERMISSION')
    Snapshot (Json @($foreign, $script:project)) 'Granted' 671088739 (Json @($foreign))
}
foreach ($token in @(1, 536871010, 939524194, 2147483648, 2818572386)) {
    $invalidToken = $token
    Check ('P1 foreign token rejected ' + $invalidToken) $false {
        $foreign = Record 'unrelated_identity' $invalidToken @('OTHER_PERMISSION')
        Snapshot (Json @($foreign, $script:project)) 'Granted' 671088739 (Json @($foreign))
    }
}
Check 'P1 shell token cannot be the project identity' $false {
    $p = Record 'fastdds_dsoftbus_probe' 805306467 @('ohos.permission.DISTRIBUTED_DATASYNC')
    Snapshot (Json @($script:other, $p)) 'Granted' 805306467
}
foreach ($token in @(1, 536871011, 805306467, 2818572387)) {
    $invalidToken = $token
    Check ('P1 ownership token must be native ' + $invalidToken) $false {
        $o = $script:owner.Clone(); $o.expectedToken = $invalidToken; Ownership (Json $o) $invalidToken
    }
}
foreach ($case in @(
    @{ name = ('a' * 256); valid = $true }, @{ name = ('a' * 257); valid = $false },
    @{ name = ([string][char]0x4e2d * 85); valid = $true },
    @{ name = ([string][char]0x4e2d * 86); valid = $false },
    @{ name = ([string][char]0x4e2d * 100); valid = $false }
)) {
    $nameCase = $case
    Check ('P1 processName UTF8 bytes ' + [Text.Encoding]::UTF8.GetByteCount($nameCase.name)) $nameCase.valid {
        $foreign = Record $nameCase.name 671088738 @('OTHER_PERMISSION')
        Snapshot (Json @($foreign, $script:project)) 'Granted' 671088739 (Json @($foreign))
    }
}
Check 'P1 NUL process name cannot masquerade as absent project' $false {
    $foreign = Record ('fastdds_dsoftbus_probe' + [char]0 + 'foreign') 671088738
    $json = Json @($foreign)
    Snapshot $json 'Absent' 0 $json
}
Check 'P1 NUL property name cannot alias an official key' $false {
    $json = $script:baseline.Replace('"APL":1', '"APL":1,"processName\u0000foreign":"alias"')
    Snapshot $json 'Absent' 0 $json
}
foreach ($key in @('permissions', 'nativeAcls', 'dcaps')) {
    $arrayKey = $key
    $limit = 256; if ($arrayKey -eq 'dcaps') { $limit = 1024 }
    foreach ($case in @(
        @{ value = ('a' * $limit); valid = $true },
        @{ value = ('a' * ($limit + 1)); valid = $false },
        @{ value = ([string][char]0x4e2d * ([int][Math]::Floor($limit / 3) + 1)); valid = $false },
        @{ value = ('prefix' + [char]0 + 'suffix'); valid = $false }
    )) {
        $stringCase = $case
        Check ('P1 array element ' + $arrayKey + ' case ' + $script:checks) $stringCase.valid {
            $foreign = Record 'unrelated_identity' 671088738 @('OTHER_PERMISSION'); $foreign[$arrayKey] = @($stringCase.value)
            Snapshot (Json @($foreign, $script:project)) 'Granted' 671088739 (Json @($foreign))
        }
    }
    foreach ($count in @(8192, 8193)) {
        $arrayCount = $count
        Check ('P1 reader array count ' + $arrayKey + ' ' + $arrayCount) ($arrayCount -eq 8192) {
            $foreign = Record 'unrelated_identity' 671088738 @('OTHER_PERMISSION'); $foreign[$arrayKey] = @('x') * $arrayCount
            Snapshot (Json @($foreign, $script:project)) 'Granted' 671088739 (Json @($foreign))
        }
    }
}
foreach ($badString in @('\uD800', '\uDC00', '\uD800x', '\u0000')) {
    $escaped = $badString
    Check ('P1 escaped invalid Unicode/NUL ' + $script:checks) $false {
        $json = $script:baseline.Replace('"APL":1', ('"APL":1,"note":"' + $escaped + '"'))
        Snapshot $json 'Absent' 0 $json
    }
}
Check 'P1 valid surrogate pair and escaped backslash accepted' $true {
    $json = $script:baseline.Replace('"APL":1', '"APL":1,"note":"\uD83D\uDE00\\uD800"')
    Snapshot $json 'Absent' 0 $json
}
Check 'P1 literal unpaired surrogate rejected before UTF8 replacement' $false {
    $json = $script:baseline.Replace('"APL":1', ('"APL":1,"note":"' + [char]0xD800 + '"'))
    Snapshot $json 'Absent' 0 $json
}
foreach ($size in @(1024000, 1024001)) {
    $byteSize = $size
    Check ('P1 JSON exact byte-size boundary ' + $byteSize) ($byteSize -eq 1024000) {
        $json = $script:baseline + (' ' * ($byteSize - [Text.Encoding]::UTF8.GetByteCount($script:baseline)))
        Snapshot $json 'Absent' 0 $json
    }
}
Check 'P1 JSON limit uses UTF8 bytes not UTF16 length' $false {
    $json = $script:baseline.Replace('"APL":1', ('"APL":1,"note":"' + ([string][char]0x4e2d * 341334) + '"'))
    Snapshot $json 'Absent' 0 $json
}

$fixtureHashesAfter = @(Get-ChildItem -LiteralPath $fixtureDirectory -File | Get-FileHash | Select-Object Path, Hash)
Check 'all fixture files unchanged' $true {
    [pscustomobject]@{ Passed = ((Json $fixtureHashesBefore) -ceq (Json $fixtureHashesAfter)); Code = 'fixture_hashes' }
}
$summary = 'checks={0} failures={1}; scope=pure snapshot/ownership only; device/service/scheduler=NOT_TESTED' -f $script:checks, $script:failures
$script:lines.Add($summary)
Write-Output $summary
[void](New-Item -ItemType Directory -Path $EvidenceDirectory -Force)
[IO.File]::WriteAllLines((Join-Path ([IO.Path]::GetFullPath($EvidenceDirectory)) ($RunLabel + '.log')), $script:lines, $utf8)
# Retain this uniquely named synthetic fixture directory for reproduction; no device data is present.
if ($script:failures -ne 0) { exit 1 }
exit 0
