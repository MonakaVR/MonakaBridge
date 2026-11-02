param([Parameter(Mandatory=$true)][string]$Gui,[Parameter(Mandatory=$true)][string]$Service,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$ownerRepo=Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out=[IO.Path]::GetFullPath($OutputDirectory)
Copy-Item -LiteralPath $Gui -Destination "$out/monaka_bridge_control.exe"
$refs=Join-Path ${env:ProgramFiles(x86)} 'Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8'
& "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe" /nologo /utf8output /target:exe "/out:$out/bridge_ui_tests.exe" /reference:System.dll /reference:System.Core.dll /reference:System.Web.Extensions.dll /reference:System.Windows.Forms.dll "/reference:$refs/System.Xaml.dll" "/reference:$refs/WindowsBase.dll" "/reference:$refs/PresentationCore.dll" "/reference:$refs/PresentationFramework.dll" "/reference:$out/monaka_bridge_control.exe" (Join-Path $ownerRepo 'tests/installation_owner_tests.cs')
if($LASTEXITCODE -ne 0){throw 'Owner acceptance compiler failed'}
& "$out/bridge_ui_tests.exe" $Service "$out/fixtures" (Join-Path $ownerRepo 'config/bridge.example.json')
exit $LASTEXITCODE
