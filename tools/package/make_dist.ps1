# Builds a Gears of War 3 distribution for testers: dist\AnyX360-GearsOfWar3-<version>\ and a .zip
# of it. It holds the launcher, the recompiled executable (with its .pdb, so crash reports name
# functions), 7-Zip, the XEX key, the shader cache, README/LEEME, the terms of use (TERMS/TERMINOS,
# also shown by the launcher), the changelog and the third-party licenses - no game data: the
# launcher extracts the player's own disc archive (.7z) next to itself.
#
# <version> is the VERSION file (Semantic Versioning). A release - HEAD tagged v<version> with no
# uncommitted changes - is named just by it ("0.1.0"); anything else carries the commit as build
# metadata, like the executables ("0.1.0+0b3cb8d", "0.1.0+0b3cb8d.dirty").
#
#   powershell -ExecutionPolicy Bypass -File tools\package\make_dist.ps1 [-NoBuild]
#
# Needs 7-Zip installed (its 7z.exe and 7z.dll are copied) and keys\xex2_retail.key.
param([switch]$NoBuild)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$release = "$root\build\games\gow3\Release"
$launcher = "AnyX360 Gears of War 3.exe"

if (-not $NoBuild) {
  foreach ($target in 'gow3', 'gow3_launcher') {
    & "$root\build.bat" release $target | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Build of $target failed: run build.bat release $target to see why." }
  }
}

$sevenZip = "$env:ProgramFiles\7-Zip"
foreach ($file in '7z.exe', '7z.dll', 'License.txt') {
  if (-not (Test-Path "$sevenZip\$file")) { throw "7-Zip not found in ${sevenZip}: install it from https://www.7-zip.org" }
}
$key = "$root\keys\xex2_retail.key"
if (-not (Test-Path $key)) { throw "$key is missing" }
foreach ($file in $launcher, 'GearsOfWar3.exe', 'GearsOfWar3.pdb') {
  if (-not (Test-Path "$release\$file")) { throw "$release\$file is missing: build first" }
}

# Licenses of the third-party code in the executables (BSD/MIT notices, LGPL texts).
$third = "$root\thirdparty"
$licenses = [ordered]@{
  'Xenia.txt'                = "$third\xenia\LICENSE"
  'XenonRecomp.txt'          = "$third\XenonRecomp\LICENSE.md"
  'FFmpeg.txt'               = "$third\xenia\third_party\FFmpeg\LICENSE.md"
  'FFmpeg (LGPL-2.1).txt'    = "$third\xenia\third_party\FFmpeg\COPYING.LGPLv2.1"
  'libmspack (LGPL-2.1).txt' = "$third\XenonRecomp\thirdparty\libmspack\libmspack\COPYING.LIB"
  'Dear ImGui.txt'           = "$third\xenia\third_party\imgui\LICENSE.txt"
  'AMD FidelityFX FSR.txt'   = "$third\xenia\third_party\FidelityFX-FSR\license.txt"
  'AMD FidelityFX CAS.txt'   = "$third\xenia\third_party\FidelityFX-CAS\LICENSE.txt"
  'FXAA.txt'                 = "$third\xenia\third_party\fxaa\LICENSE"
  'DirectX-Headers.txt'      = "$third\xenia\third_party\DirectX-Headers\LICENSE"
  'toml++.txt'               = "$third\xenia\third_party\tomlplusplus\LICENSE"
  'fmt.txt'                  = "$third\xenia\third_party\fmt\LICENSE"
  'xxHash.txt'               = "$third\xenia\third_party\xxhash\LICENSE"
  'Snappy.txt'               = "$third\xenia\third_party\snappy\COPYING"
  'utfcpp.txt'               = "$third\xenia\third_party\utfcpp\LICENSE"
  'SIMDe.txt'                = "$third\XenonRecomp\thirdparty\simde\COPYING"
  '7-Zip.txt'                = "$sevenZip\License.txt"
}
foreach ($source in $licenses.Values) {
  if (-not (Test-Path $source)) { throw "License file missing: $source" }
}

$version = (Get-Content "$root\VERSION" -TotalCount 1).Trim()
$commit = (& git -C $root rev-parse --short=7 HEAD).Trim()
$dirty = [bool](& git -C $root status --porcelain --untracked-files=no)
$tag = (& git -C $root tag --points-at HEAD) -contains "v$version"
if ($dirty -or -not $tag) { $version += "+$commit" + $(if ($dirty) { '.dirty' } else { '' }) }
$dist = "$root\dist\AnyX360-GearsOfWar3-$version"
if (Test-Path $dist) { throw "$dist already exists: delete it (and its .zip) to package this version again" }

New-Item -ItemType Directory "$dist\bin", "$dist\keys", "$dist\licenses" | Out-Null
Copy-Item "$release\$launcher" $dist
Copy-Item "$release\GearsOfWar3.exe", "$release\GearsOfWar3.pdb" "$dist\bin"
Copy-Item "$sevenZip\7z.exe", "$sevenZip\7z.dll" "$dist\bin"
Copy-Item $key "$dist\keys"
foreach ($entry in $licenses.GetEnumerator()) { Copy-Item $entry.Value "$dist\licenses\$($entry.Key)" }
# Shaders and pipelines collected while testing: the launcher seeds the player's cache with them.
$cache = "$env:LOCALAPPDATA\AnyX360\GearsOfWar3\cache\shaders"
if (Test-Path $cache) { Copy-Item $cache "$dist\bin\shader_cache" -Recurse }

# Texts in English and Spanish, UTF-8 with a BOM and Windows line endings for Notepad.
$bom = New-Object Text.UTF8Encoding $true
function Write-Text([string]$source, [string]$target) {
  $text = (Get-Content $source -Raw -Encoding UTF8).Replace('{VERSION}', $version).Replace('{GAME}', 'Gears of War 3')
  [IO.File]::WriteAllText($target, $text.Replace("`r`n", "`n").Replace("`n", "`r`n"), $bom)
}
Write-Text "$PSScriptRoot\gow3\README.txt" "$dist\README.txt"
Write-Text "$PSScriptRoot\gow3\LEEME.txt" "$dist\LEEME.txt"
Write-Text "$root\tools\launcher\terms_en.txt" "$dist\TERMS.txt"
Write-Text "$root\tools\launcher\terms_es.txt" "$dist\TERMINOS.txt"
Write-Text "$root\CHANGELOG.md" "$dist\CHANGELOG.md"
Write-Text "$root\CHANGELOG.es.md" "$dist\CHANGELOG.es.md"

Compress-Archive -Path $dist -DestinationPath "$dist.zip"
$size = (Get-Item "$dist.zip").Length / 1MB
"Packaged $dist"
"Zip: $dist.zip ({0:N0} MB)" -f $size
