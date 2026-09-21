param([Parameter(Mandatory=$true)][string]$RepoRoot,
      [string]$GuiAssembly = '', [string]$ConfigFile = '')
$ErrorActionPreference = 'Stop'
if (-not $GuiAssembly) { $GuiAssembly = Join-Path $RepoRoot 'build-gui/monaka_bridge_control.exe' }
if (-not $ConfigFile) { $ConfigFile = Join-Path $RepoRoot 'build/test-config.json' }
$assembly = [Reflection.Assembly]::LoadFrom($GuiAssembly)
$type = $assembly.GetType('MonakaBridge.SteamVrControlWpf')
$read = $type.GetMethod('Read', [Reflection.BindingFlags]'NonPublic,Static')
$arguments = New-Object object[] 1
$arguments[0] = $ConfigFile
$config = $read.Invoke($null, $arguments)
if ($config['mappings'] -isnot [Collections.IList]) { throw 'Mapping collection type mismatch' }
if ($config['mappings'].Count -ne 2) { throw 'Expected saved test mappings' }
$translation = $config['mappings'][0]['world']['translation']
if ($translation -isnot [Collections.IList] -or $translation.Count -ne 3) { throw 'World translation collection mismatch' }
$profile = $config['profiles']['pico']
if (-not $profile['approved']) { throw 'Approval read failed' }
Write-Output 'PASS migrated WPF config reader: mappings, nested transforms and profile; interactive UI NOT RUN'
