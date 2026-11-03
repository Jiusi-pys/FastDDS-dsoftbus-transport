param([Parameter(Mandatory)][string]$Builder, [Parameter(Mandatory)][string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
$source = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $Builder).Path)
$begin = $source.IndexOf('    # Source identity begins')
$end = $source.IndexOf('    $experimentSourceStatePath =', $begin)
if ($begin -lt 0 -or $end -lt $begin) { throw 'Actual source construction block not found' }
if ($end -gt $source.IndexOf('& $experimentPlatformCompiler @experimentCompileArguments')) { throw 'Source validation is not before compiler invocation' }
$construction = [scriptblock]::Create($source.Substring($begin, $end-$begin))
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('mdds-receipt-contract-' + [Guid]::NewGuid().ToString('N'))
$experimentRoot = Join-Path $testRoot 'source'
New-Item -ItemType Directory -Path $experimentRoot | Out-Null
New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $experimentRoot 'input.cpp'), 'sealed input', [Text.UTF8Encoding]::new($false))
$goodFileHash = (Get-FileHash -LiteralPath (Join-Path $experimentRoot 'input.cpp') -Algorithm SHA256).Hash.ToLowerInvariant()
function git {
    $script:sourceCalls++
    $global:LASTEXITCODE = 0
    if ($script:denyGit -or $script:sourceCalls -eq $script:failCall) { $global:LASTEXITCODE = 128; return }
    if ($args[0] -eq 'rev-parse') { if ($script:badHead) { return 'invalid-head' }; return ('f' * 40) }
}
$results = @()
$cases = @('valid-receipt-no-git','wrong-root','bad-baseline','bad-inventory','bad-overlay','wrong-file-hash','missing-file','parent-escape','absolute-file','duplicate-path','case-alias','backslash-path','empty-files','receipt-inside-source','schema-mismatch','trailing-dot-alias','junction-input',
    'legacy-head-error','legacy-status-error','legacy-diff-error','legacy-untracked-error','legacy-bad-head','legacy-success')
foreach ($case in $cases) {
    $script:sourceCalls = 0; $script:failCall = 0; $script:badHead = $false; $script:denyGit = $true
    $RestoredSourceReceipt = Join-Path $testRoot ($case + '.json')
    $receipt = [ordered]@{ schema=1; kind='RESTORED_PROJECT_SOURCE'; source_root=$experimentRoot;
        baseline_head=('f'*40); checkpoint_inventory_sha256=('a'*64); overlay_manifest_sha256=('b'*64);
        files=@([ordered]@{path='input.cpp';sha256=$goodFileHash;origin='CHECKPOINT'}) }
    switch ($case) {
        'wrong-root' { $receipt.source_root = $testRoot }
        'bad-baseline' { $receipt.baseline_head = 'bad' }
        'bad-inventory' { $receipt.checkpoint_inventory_sha256 = 'bad' }
        'bad-overlay' { $receipt.overlay_manifest_sha256 = 'bad' }
        'wrong-file-hash' { $receipt.files[0].sha256 = '0'*64 }
        'missing-file' { $receipt.files[0].path = 'missing.cpp' }
        'parent-escape' { $receipt.files[0].path = '../input.cpp' }
        'absolute-file' { $receipt.files[0].path = (Join-Path $experimentRoot 'input.cpp') }
        'duplicate-path' { $receipt.files += $receipt.files[0] }
        'case-alias' { $receipt.files += @{path='INPUT.CPP';sha256=$goodFileHash} }
        'backslash-path' { $receipt.files[0].path = 'folder\input.cpp' }
        'empty-files' { $receipt.files = @() }
        'receipt-inside-source' { $RestoredSourceReceipt = Join-Path $experimentRoot 'receipt.json' }
        'schema-mismatch' { $receipt.schema=2 }
        'trailing-dot-alias' { $receipt.files[0].path = 'input.cpp.' }
        'junction-input' {
            $target = Join-Path $testRoot 'outside'
            New-Item -ItemType Directory -Path $target | Out-Null
            Copy-Item -LiteralPath (Join-Path $experimentRoot 'input.cpp') -Destination $target
            $link = Join-Path $experimentRoot 'linked'
            try { New-Item -ItemType Junction -Path $link -Target $target -ErrorAction Stop | Out-Null }
            catch { throw ('Cannot execute required reparse fixture: ' + $_.Exception.Message) }
            $receipt.files[0].path = 'linked/input.cpp'
        }
    }
    if ($case.StartsWith('legacy-')) {
        $RestoredSourceReceipt = ''; $script:denyGit=$false
        $script:badHead = $case -eq 'legacy-bad-head'
        $script:failCall = @('legacy-head-error','legacy-status-error','legacy-diff-error','legacy-untracked-error').IndexOf($case)+1
    } else { [IO.File]::WriteAllText($RestoredSourceReceipt, ($receipt | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false)) }
    $experimentSourceState = $null; $thrown = $false; $errorText = ''
    try { . $construction } catch { $thrown=$true; $errorText=$_.Exception.Message }
    $expectedSuccess = $case -in @('valid-receipt-no-git','legacy-success')
    $pass = if ($case -eq 'valid-receipt-no-git') {
        -not $thrown -and $sourceCalls -eq 0 -and $experimentSourceState.head -ceq ('f'*40) -and
        $experimentSourceState.provenance -ceq 'RESTORED_RECEIPT_VERIFIED' -and $experimentSourceState.receiptSha256 -ceq (Get-FileHash -LiteralPath $RestoredSourceReceipt -Algorithm SHA256).Hash.ToLowerInvariant()
    } elseif ($case -eq 'legacy-success') { -not $thrown -and $sourceCalls -eq 4 -and $experimentSourceState.head -ceq ('f'*40) }
    elseif ($case.StartsWith('legacy-')) { $thrown }
    else { $thrown -and $sourceCalls -eq 0 }
    $results += [ordered]@{case=$case;pass=[bool]$pass;expectedSuccess=$expectedSuccess;thrown=$thrown;gitCalls=$sourceCalls;error=$errorText;scope='ACTUAL_SOURCE_CONSTRUCTION_BLOCK_BEFORE_COMPILER_NO_SDK'}
    Write-Output "restored_source_case=$case pass=$pass git_calls=$sourceCalls thrown=$thrown"
}
$results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'cases.json') -Encoding UTF8
[IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'fixture-root.txt'), $testRoot)
if (@($results | Where-Object { -not $_.pass }).Count) { exit 1 }
exit 0
