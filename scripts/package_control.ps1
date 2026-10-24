param([Parameter(Mandatory=$true)][string]$ConfigExe,
      [Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
function Get-Sha256([string]$Path) {
 $inputFile=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
 try { return [BitConverter]::ToString($sha.ComputeHash($inputFile)).Replace('-','').ToLowerInvariant() }
 finally { $sha.Dispose();$inputFile.Dispose() }
}
$repo=Split-Path -Parent $PSScriptRoot
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Use a new package directory; existing artifacts are never overwritten.' }
if (-not (Test-Path -LiteralPath $ConfigExe -PathType Leaf)) { throw 'Build the current native config helper first.' }
New-Item -ItemType Directory -Path $output | Out-Null
& (Join-Path $PSScriptRoot 'build_gui.ps1') -OutputDirectory $output -Force
if ($LASTEXITCODE -ne 0) { throw 'GUI build failed' }
$helper=Join-Path $output 'monaka_bridge_config.exe'
Copy-Item -LiteralPath $ConfigExe -Destination $helper
$generation= & $helper (Join-Path $output 'not-created.json') transaction-version
if ($LASTEXITCODE -ne 0 -or $generation -ne 'monaka-config-transaction-v1') { throw 'Native helper transaction generation mismatch' }
$helperHash=Get-Sha256 $helper
$profile=[ordered]@{schema='monaka.bridge.installation.v1';name='Selected Bridge installation';config='config/bridge.json';health='config/bridge.json.status.json';config_helper='monaka_bridge_config.exe';helper_sha256=$helperHash;read_only=$true}
[IO.File]::WriteAllText((Join-Path $output 'installation.example.json'),($profile | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $repo 'docs/bridge-control-foundation.md') -Destination (Join-Path $output 'README.md')
$sources=[ordered]@{}
foreach ($name in @('ui/control_models.cs','ui/installation_control.cs','ui/steamvr_control_gui_main.cs','ui/steamvr_control_wpf.cs','ui/steamvr_control_tray.cs','scripts/build_gui.ps1','scripts/package_control.ps1','tools/bridge_config.cpp','src/mapping/config_transaction.cpp','dependencies/mbip-p1.lock.json')) {
 $sources[$name]=Get-Sha256 (Join-Path $repo $name)
}
$files=[ordered]@{}
Get-ChildItem -LiteralPath $output -File | Sort-Object Name | ForEach-Object { $files[$_.Name]=Get-Sha256 $_.FullName }
$manifest=[ordered]@{schema='monaka.bridge.control-toolset.v1';source_commit=(& git -C $repo rev-parse HEAD);source_dirty=[bool](& git -C $repo status --porcelain);helper_generation=$generation;native_helper_build_provenance='Supplied binary; correlate hash with native build log';source_sha256=$sources;files_sha256=$files;lifecycle='unsupported';hardware_validation='NOT_RUN'}
[IO.File]::WriteAllText((Join-Path $output 'control-toolset-manifest.json'),($manifest | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output 'PASS packaged native WPF GUI and transaction helper; no config, service, driver or Python dependency installed'
