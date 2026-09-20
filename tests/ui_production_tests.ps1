param([Parameter(Mandatory=$true)][string]$RepoRoot,
      [Parameter(Mandatory=$true)][string]$TestDirectory,
      [Parameter(Mandatory=$true)][string]$ConfigExe)
$ErrorActionPreference='Stop'
& (Join-Path $RepoRoot 'scripts/build_gui.ps1') -OutputDirectory $TestDirectory -Force
if ($LASTEXITCODE -ne 0) { throw 'GUI build failed' }
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$gui=Join-Path $TestDirectory 'monaka_bridge_control.exe'
$test=Join-Path $TestDirectory 'bridge_ui_tests.exe'
& $compiler /nologo /target:exe /optimize+ "/out:$test" /reference:System.dll /reference:System.Core.dll /reference:System.Web.Extensions.dll "/reference:$gui" (Join-Path $RepoRoot 'tests/ui_production_tests.cs')
if ($LASTEXITCODE -ne 0) { throw 'UI regression build failed' }
& $test $TestDirectory $ConfigExe (Join-Path $RepoRoot 'config/bridge.example.json')
if ($LASTEXITCODE -ne 0) { throw 'UI production regression failed' }
