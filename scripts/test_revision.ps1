[CmdletBinding()]
param(
    [ValidateSet('Quick', 'Full', 'Audit')]
    [string]$Mode = 'Quick',
    [string]$ResultsDir = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$StartedUtc = (Get-Date).ToUniversalTime()
$RunId = $StartedUtc.ToString('yyyyMMdd-HHmmss')
if ([string]::IsNullOrWhiteSpace($ResultsDir)) {
    $ResultsDir = Join-Path $RepoRoot ("build/revision-test-results/{0}-{1}" -f $RunId, $Mode.ToLowerInvariant())
}
New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null

$StepResults = [System.Collections.Generic.List[object]]::new()
$FailedStep = $null

function Invoke-RevisionStep {
    param([string]$Name, [string]$FilePath, [string[]]$Arguments)

    $logPath = Join-Path $ResultsDir ((($Name -replace '[^A-Za-z0-9_.-]', '_')) + '.log')
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $global:LASTEXITCODE = 0
    $exitCode = 0
    $previousErrorActionPreference = $ErrorActionPreference
    Push-Location $RepoRoot
    try {
        $ErrorActionPreference = 'Continue'
        & $FilePath @Arguments *> $logPath
        if ($null -ne $LASTEXITCODE) { $exitCode = [int]$LASTEXITCODE }
    }
    catch {
        ($_ | Out-String) | Add-Content -Path $logPath
        $exitCode = 1
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
        Pop-Location
        $timer.Stop()
    }

    $StepResults.Add([pscustomobject]@{
        name = $Name
        result = $(if ($exitCode -eq 0) { 'PASS' } else { 'FAIL' })
        exit_code = $exitCode
        duration_ms = $timer.ElapsedMilliseconds
        log = $logPath
    })
    return ($exitCode -eq 0)
}

$BuildDir = Join-Path $RepoRoot ("build/revision-{0}" -f $Mode.ToLowerInvariant())
$SteamVr = if ($Mode -ne 'Quick') { 'ON' } else { 'OFF' }
$Steps = @(
    @{ Name='release-tooling'; File='python'; Args=@('scripts/test_release_v2.py') },
    @{ Name='release-tooling-optimized'; File='python'; Args=@('-O','scripts/test_release_v2.py') },
    @{ Name='protocol-v2-kit'; File='python'; Args=@('scripts/verify_protocol_v2.py') },
    @{ Name='upstream-integrity'; File='python'; Args=@('scripts/import_upstream.py') },
    @{ Name='cmake-configure'; File='cmake'; Args=@('-S','.','-B',$BuildDir,'-A','x64','-DBUILD_TESTING=ON',("-DMB_BUILD_STEAMVR={0}" -f $SteamVr)) },
    @{ Name='cmake-build'; File='cmake'; Args=@('--build',$BuildDir,'--config','Release') },
    @{ Name='ctest'; File='ctest'; Args=@('--test-dir',$BuildDir,'-C','Release','--output-on-failure') }
)

# Ordinary Quick/Full runs check product software; Audit keeps source/release history.
if ($Mode -ne 'Audit') {
    $AuditOnly = @('release-tooling', 'release-tooling-optimized', 'protocol-v2-kit', 'upstream-integrity')
    $Steps = @($Steps | Where-Object { $_.Name -notin $AuditOnly })
    $CTest = @($Steps | Where-Object { $_.Name -eq 'ctest' })[0]
    if ($null -eq $CTest) { throw 'Product CTest step is missing' }

    # Historical source byte/scope assertions, dormant research and mandatory
    # output-approval tests remain explicitly available via -Mode Audit.
    $AuditCTest = 'scope|dormancy|integrity|^world_approval|^offline_readiness|^evidence_lifetime|^integration_current_policy$|^common_world_|^trusted_hmd_|^trusted_live_|^old_context_null_read_reproduced$'
    $CTest['Args'] = @($CTest['Args']) + @('-E', $AuditCTest)
    if ($Mode -eq 'Quick') {
        $ProductSmoke = '^(health_async|coordinate_profiles|world_calibration|trajectory_calibration|mapping_contract|architecture_revision|rotation_only_downstream|bridge_core|bridge_edges|separate_process_udp|p1_runtime_contract|static_capability_propagation|config_transaction_process|measurement_target)$'
        $CTest['Args'] = @($CTest['Args']) + @('-R', $ProductSmoke)
    }
}

foreach ($step in $Steps) {
    if (-not (Invoke-RevisionStep -Name $step.Name -FilePath $step.File -Arguments $step.Args)) {
        $FailedStep = $step.Name
        break
    }
}

$head = ''
try { $head = ((& git -C $RepoRoot rev-parse HEAD 2>$null) | Out-String).Trim() } catch { }
$result = if ($null -eq $FailedStep) { 'PASS' } else { 'FAIL' }
$summary = [ordered]@{
    repo = 'MonakaVR/MonakaBridge'
    head = $head
    mode = $Mode
    validation_lane = $(if ($Mode -eq 'Audit') { 'HISTORICAL_SOURCE_POLICY_AUDIT' } else { 'PRODUCT_SOFTWARE' })
    source_scope_checks = $(if ($Mode -eq 'Audit') { 'INCLUDED' } else { 'NOT_RUN_SEPARATE_AUDIT' })
    approval_policy_migration = 'NOT_VERIFIED - Bridge issue #17; runtime policy unchanged'
    result = $result
    failed_step = $FailedStep
    started_utc = $StartedUtc.ToString('o')
    ended_utc = (Get-Date).ToUniversalTime().ToString('o')
    steamvr_build = $(if ($Mode -eq 'Full') { 'REQUESTED' } else { 'SKIPPED' })
    steamvr_runtime = 'NOT RUN'
    hardware_validation = 'NOT RUN'
    release_evidence = 'NOT RUN - run scripts/release_v2.py separately for source-bound v2 evidence'
    steps = $StepResults
}
$summaryPath = Join-Path $ResultsDir 'summary.json'
$summary | ConvertTo-Json -Depth 6 | Set-Content -Path $summaryPath -Encoding UTF8
Write-Host ("RESULT={0} repo=MonakaBridge mode={1} failed_step={2} summary={3}" -f $result, $Mode, $FailedStep, $summaryPath)
if ($result -eq 'FAIL') { exit 1 }
