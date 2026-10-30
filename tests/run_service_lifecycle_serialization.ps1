param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$testOutput = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$fixture = Join-Path $testOutput 'sa01_service_fixture.exe'
$harness = Join-Path $testOutput 'service_lifecycle_tests.exe'
& $compiler /nologo /utf8output /target:exe "/out:$fixture" (Join-Path $PSScriptRoot 'service_lifecycle_fixture.cs')
if ($LASTEXITCODE -ne 0) { throw 'Lifecycle fixture compilation failed' }
& $compiler /nologo /utf8output /target:exe "/out:$harness" (Join-Path $sourceRoot 'ui/service_lifecycle_control.cs') (Join-Path $PSScriptRoot 'service_lifecycle_serialization_tests.cs')
if ($LASTEXITCODE -ne 0) { throw 'Lifecycle harness compilation failed' }
& $harness $fixture (Join-Path $testOutput 'cases')
if ($LASTEXITCODE -ne 0) { throw 'Lifecycle serialization tests failed' }
