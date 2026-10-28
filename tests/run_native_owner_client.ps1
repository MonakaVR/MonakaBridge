param([string]$Gui,[string]$Service,[string]$Config,[string]$OutputDirectory)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force $OutputDirectory|Out-Null
$out=[IO.Path]::GetFullPath($OutputDirectory)
Copy-Item -LiteralPath $Gui -Destination "$out/monaka_bridge_control.exe"
& "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe" /nologo /utf8output /target:exe "/out:$out/bridge_ui_tests.exe" /reference:System.dll /reference:System.Core.dll /reference:System.Web.Extensions.dll "/reference:$out/monaka_bridge_control.exe" (Join-Path $PSScriptRoot 'native_owner_client_tests.cs')
if($LASTEXITCODE -ne 0){throw 'Native owner client compiler failed'}
& "$out/bridge_ui_tests.exe" $Config $Service
exit $LASTEXITCODE
