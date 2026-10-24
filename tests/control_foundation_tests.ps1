param([Parameter(Mandatory=$true)][string]$RepoRoot,
      [Parameter(Mandatory=$true)][string]$TestDirectory,
      [Parameter(Mandatory=$true)][string]$ConfigExe,
      [string]$GuiExe='')
$ErrorActionPreference='Stop'
$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot).Path
$ConfigExe=(Resolve-Path -LiteralPath $ConfigExe).Path
$TestDirectory=[IO.Path]::GetFullPath($TestDirectory)
New-Item -ItemType Directory -Path $TestDirectory -Force | Out-Null
if (-not $GuiExe) {
 & (Join-Path $RepoRoot 'scripts/build_gui.ps1') -OutputDirectory $TestDirectory -Force
 if ($LASTEXITCODE -ne 0) { throw 'GUI build failed' }
} else {
 $GuiExe=(Resolve-Path -LiteralPath $GuiExe).Path
 [IO.File]::Copy($GuiExe,(Join-Path $TestDirectory 'monaka_bridge_control.exe'),$false)
 Write-Output 'Testing a byte-identical copy of the selected packaged GUI; native helper uses selected package path'
}
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$gui=Join-Path $TestDirectory 'monaka_bridge_control.exe'
$test=Join-Path $TestDirectory 'bridge_ui_tests.exe'
$fake=Join-Path $TestDirectory 'fixture-helper.exe'
$refs=Join-Path ${env:ProgramFiles(x86)} 'Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8'
& $compiler /nologo /target:exe "/out:$fake" (Join-Path $RepoRoot 'tests/control_helper_fixture.cs')
if ($LASTEXITCODE -ne 0) { throw 'Fixture helper build failed' }
& $compiler /nologo /target:exe "/out:$test" /reference:System.dll /reference:System.Core.dll /reference:System.Web.Extensions.dll /reference:System.Windows.Forms.dll "/reference:$refs/System.Xaml.dll" "/reference:$refs/WindowsBase.dll" "/reference:$refs/PresentationCore.dll" "/reference:$refs/PresentationFramework.dll" "/reference:$gui" (Join-Path $RepoRoot 'tests/control_foundation_tests.cs')
if ($LASTEXITCODE -ne 0) { throw 'Foundation test build failed' }
& $test $TestDirectory $ConfigExe $fake (Join-Path $RepoRoot 'config/bridge.example.json')
if ($LASTEXITCODE -ne 0) { throw 'Control foundation regression failed' }
