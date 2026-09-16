[CmdletBinding()]
param(
    [ValidateSet('Quick', 'Full')]
    [string]$Mode = 'Quick',
    [string]$ResultsDir = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$RunId = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
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
    Push-Location $RepoRoot
    try {
        & $FilePath @Arguments *> $logPath
        $exitCode = [int]$LASTEXITCODE
    }
    catch {
        ($_ | Out-String) | Add-Content -Path $logPath
        $exitCode = 1
    }
    finally {
        Pop-Location
        $timer.Stop()
    }
    $StepResults.Add([pscustomobject]@{
        name=$Name
        result=$(if ($exitCode -eq 0) { 'PASS' } else { 'FAIL' })
        exit_code=$exitCode
        duration_ms=$timer.ElapsedMilliseconds
        log=$logPath
    })
    return ($exitCode -eq 0)
}

$BuildDir = Join-Path $RepoRoot ("build/revision-{0}" -f $Mode.ToLowerInvariant())
$SteamVr = if ($Mode -eq 'Full') { 'ON' } else { 'OFF' }
$Steps = @(
    @{ Name='upstream-integrity'; File='python'; Args=@('scripts/import_upstream.py') },
    @{ Name='cmake-configure'; File='cmake'; Args=@('-S','.','-B',$BuildDir,'-A','x64','-DBUILD_TESTING=ON',("-DMB_BUILD_STEAMVR={0}" -f $SteamVr)) },
    @{ Name='cmake-build'; File='cmake'; Args=@('--build',$BuildDir,'--config','Release') },
    @{ Name='ctest'; File='ctest'; Args=@('--test-dir',$BuildDir,'-C','Release','--output-on-failure') }
)

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
    repo='MonakaVR/MonakaBridge'
    head=$head
    mode=$Mode
    result=$result
    failed_step=$FailedStep
    steamvr_build=$(if ($Mode -eq 'Full') { 'REQUESTED' } else { 'SKIPPED' })
    steamvr_runtime='NOT RUN'
    hardware_validation='NOT RUN'
    steps=$StepResults
}
$summaryPath = Join-Path $ResultsDir 'summary.json'
$summary | ConvertTo-Json -Depth 6 | Set-Content -Path $summaryPath -Encoding UTF8
Write-Host ("RESULT={0} repo=MonakaBridge mode={1} failed_step={2} summary={3}" -f $result, $Mode, $FailedStep, $summaryPath)
if ($result -eq 'FAIL') { exit 1 }
