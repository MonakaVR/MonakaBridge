[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ArtifactPath, [switch]$Apply)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Get-ArtifactSha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
$releasePath = (Resolve-Path -LiteralPath $ArtifactPath).Path
if (-not (Test-Path -LiteralPath $releasePath -PathType Container)) { throw 'Artifact directory required' }
$releaseParent = Split-Path -Parent $releasePath
if ([string]::IsNullOrWhiteSpace($releaseParent) -or $releasePath -eq [IO.Path]::GetPathRoot($releasePath)) { throw 'Unsafe artifact root' }
# Reject reparse points in the resolved target, its ancestors, and descendants.
$ancestorPath = $releasePath
while (-not [string]::IsNullOrWhiteSpace($ancestorPath)) {
    if (((Get-Item -LiteralPath $ancestorPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse path forbidden' }
    $nextPath = Split-Path -Parent $ancestorPath
    if ($nextPath -eq $ancestorPath) { break }
    $ancestorPath = $nextPath
}
$entries = @(Get-ChildItem -LiteralPath $releasePath -Recurse -Force)
if (@($entries | Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 }).Count) { throw 'Reparse entry forbidden' }
$manifestPath = Join-Path $releasePath 'artifact-manifest.json'
$digestPath = Join-Path $releasePath 'artifact-manifest.sha256'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema -ne 'monaka-common-world-artifact-1' -or $manifest.activation_default -ne $false) { throw 'Invalid artifact contract' }
if ((Get-Content -LiteralPath $digestPath -Raw).Trim() -ne (Get-ArtifactSha256 $manifestPath)) { throw 'Manifest hash mismatch' }
$allowedFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($row in $manifest.files) {
    if ($row.path -match '(^/|\\|:|(^|/)\.\.(/|$))') { throw 'Unsafe manifest path' }
    if ($row.path -match '(^|/)(state|logs|reports|config)(/|$)|\.(rev|lock|tmp)$') { throw 'Mutable state/config/logs must never be removed as artifact' }
    $filePath = [IO.Path]::GetFullPath((Join-Path $releasePath $row.path))
    if (-not $filePath.StartsWith($releasePath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escaped artifact' }
    if (-not $allowedFiles.Add($filePath)) { throw 'Duplicate artifact file' }
    if ((Get-Item -LiteralPath $filePath).Length -ne $row.size -or (Get-ArtifactSha256 $filePath) -ne $row.sha256) { throw 'Artifact changed; rollback refused' }
}
$null = $allowedFiles.Add($manifestPath); $null = $allowedFiles.Add($digestPath)
foreach ($entry in $entries) {
    if (-not $entry.PSIsContainer -and -not $allowedFiles.Contains($entry.FullName)) { throw 'Unmanifested file; persistent/config/log protection refusal' }
}
foreach ($required in @('bin/monaka_common_world_runtime.exe','bin/monaka_common_world_preflight.exe','templates/production.activation.json','dependency-inventory.json')) {
    if (-not $allowedFiles.Contains((Join-Path $releasePath $required))) { throw 'Required artifact entry missing' }
}
if ($Apply) {
    # The absolute recursive target above is verified as the exact explicit artifact.
    Remove-Item -LiteralPath $releasePath -Recurse -Force
}
[pscustomobject]@{ artifact=$releasePath; applied=[bool]$Apply; persistent_state='PRESERVED outside artifact'; configuration='PRESERVED outside artifact'; logs_reports='PRESERVED outside artifact' } | ConvertTo-Json
