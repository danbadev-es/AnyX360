# Builds the release for players: dist\AnyX360-<version>\ and a .zip of it. One folder for every game
# the build has (the games registered in the launcher's catalog, build\launcher\Release\
# games_catalog.h): the launcher (AnyX360.exe), each recompiled executable (with its .pdb, so crash
# reports name functions; a game can have several: game.json's "executables"), 7-Zip, the XEX key, each game's shader cache, the pose bridge when a game
# is played with a webcam, the games' front covers (from covers\, which git doesn't keep), the README,
# the terms of use (TERMS.txt; the launcher shows them in English or Spanish), the changelog and the
# third-party licenses - no game data: the launcher extracts or writes out the player's own copies
# (archives of any kind, disc images, packages) next to itself, or uses the folders of their copies.
#
# The games' facts come from games\<id>\game.json. README.txt is tools\package\README.txt with one
# section per game (tools\package\<id>\section.txt); tools\package\<id>\extra.ps1 (optional) adds a
# game's own files to the folder.
#
# <version> is the VERSION file (Semantic Versioning). A release - HEAD tagged v<version> with no
# uncommitted changes - is named just by it ("0.1.0"); anything else carries the commit as build
# metadata, like the executables ("0.1.0+0b3cb8d", "0.1.0+0b3cb8d.dirty").
#
#   powershell -ExecutionPolicy Bypass -File tools\package\make_dist.ps1 [-NoBuild] [-OutDir <folder>]
#
# Needs 7-Zip installed (its 7z.exe and 7z.dll are copied), keys\xex2_retail.key and, for webcam
# games, the pose bridge built by tools\pose_bridge\build_bridge.ps1.
param([switch]$NoBuild, [string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$launcherDir = "$root\build\launcher\Release"
if (-not $OutDir) { $OutDir = "$root\dist" }

function Build([string]$target) {
  & "$root\build.bat" release $target | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "Build of $target failed: run build.bat release $target to see why." }
}

# The launcher first: configuring the build writes the catalog of the games it has.
if (-not $NoBuild) { Build 'anyx360_launcher'; Build 'x360tool' }
$x360tool = "$root\build\tools\x360tool\Release\x360tool.exe"
if (-not (Test-Path $x360tool)) { throw "$x360tool is missing: build x360tool first" }
$catalog = "$launcherDir\games_catalog.h"
if (-not (Test-Path $catalog)) { throw "$catalog is missing: build anyx360_launcher first" }
$ids = [regex]::Matches((Get-Content $catalog -Raw), '\{L"([a-z0-9_]+)", L"') | ForEach-Object { $_.Groups[1].Value }
if (-not $ids) { throw "The catalog lists no games" }
$games = foreach ($id in $ids) {
  $info = Get-Content "$root\games\$id\game.json" -Raw | ConvertFrom-Json
  $info | Add-Member NoteProperty release "$root\build\games\$id\Release"
  # Every executable of the game: the one the launcher starts and those the game starts in turn
  # ("executables": {XEX: executable}), built with it.
  $exes = @($info.exe)
  if ($info.executables) { $exes = @($info.executables.PSObject.Properties | ForEach-Object { $_.Value }) }
  $info | Add-Member NoteProperty exes $exes
  $info
}
if (-not $NoBuild) { foreach ($game in $games) { Build $game.id } }

$sevenZip = "$env:ProgramFiles\7-Zip"
foreach ($file in '7z.exe', '7z.dll', 'License.txt') {
  if (-not (Test-Path "$sevenZip\$file")) { throw "7-Zip not found in ${sevenZip}: install it from https://www.7-zip.org" }
}
$key = "$root\keys\xex2_retail.key"
if (-not (Test-Path $key)) { throw "$key is missing" }
if (-not (Test-Path "$launcherDir\AnyX360.exe")) { throw "$launcherDir\AnyX360.exe is missing: build first" }
foreach ($game in $games) {
  foreach ($exe in $game.exes) {
    foreach ($file in "$exe.exe", "$exe.pdb") {
      if (-not (Test-Path "$($game.release)\$file")) { throw "$($game.release)\$file is missing: build first" }
    }
  }
}
# The English terms naming every game, as the launcher build filled them in.
$terms = "$root\build\launcher\terms_en.txt"
if (-not (Test-Path $terms)) { throw "$terms is missing: build anyx360_launcher first" }
# Webcam games need the pose bridge, shipped once.
$webcam = @($games | Where-Object { $_.input -eq 'webcam' })
$bridge = "$root\build\pose_bridge\dist\pose_bridge"
$bridgeLicenses = "$root\build\pose_bridge\licenses"
if ($webcam) {
  if (-not (Test-Path "$bridge\pose_bridge.exe")) { throw "The pose bridge isn't built: run tools\pose_bridge\build_bridge.ps1" }
  if (-not (Test-Path "$bridgeLicenses\README.txt")) { throw "The pose bridge's licenses are missing: run tools\pose_bridge\build_bridge.ps1" }
  # It ships only if its self-test passes (no camera is opened).
  cmd /c "`"$bridge\pose_bridge.exe`" --self-test >nul 2>&1"
  if ($LASTEXITCODE -ne 0) { throw "The pose bridge's self-test failed: run $bridge\pose_bridge.exe --self-test" }
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
  'Inter (OFL-1.1).txt'      = "$third\fonts\inter\LICENSE.txt"
}
foreach ($source in $licenses.Values) {
  if (-not (Test-Path $source)) { throw "License file missing: $source" }
}

$version = (Get-Content "$root\VERSION" -TotalCount 1).Trim()
$commit = (& git -C $root rev-parse --short=7 HEAD).Trim()
$dirty = [bool](& git -C $root status --porcelain --untracked-files=no)
$tag = (& git -C $root tag --points-at HEAD) -contains "v$version"
if ($dirty -or -not $tag) { $version += "+$commit" + $(if ($dirty) { '.dirty' } else { '' }) }
$dist = "$OutDir\AnyX360-$version"
if (Test-Path $dist) { throw "$dist already exists: delete it (and its .zip) to package this version again" }

New-Item -ItemType Directory "$dist\bin", "$dist\keys", "$dist\licenses" | Out-Null
Copy-Item "$launcherDir\AnyX360.exe" $dist
Copy-Item "$sevenZip\7z.exe", "$sevenZip\7z.dll" "$dist\bin"
Copy-Item $key "$dist\keys"
foreach ($entry in $licenses.GetEnumerator()) { Copy-Item $entry.Value "$dist\licenses\$($entry.Key)" }
if ($webcam) {
  Copy-Item $bridge "$dist\bin\pose_bridge" -Recurse
  Copy-Item $bridgeLicenses "$dist\licenses\pose_bridge" -Recurse
}
# Box art (covers\, not in git): the front cover of every catalog and upcoming game that has one,
# at 600x900 at most (x360tool cover).
$slugs = @($games | ForEach-Object { $_.cover }) +
  @((Get-Content "$root\tools\launcher\upcoming.json" -Raw | ConvertFrom-Json).games | ForEach-Object { $_.cover })
$slugs = @($slugs | Where-Object { $_ } | Select-Object -Unique)
New-Item -ItemType Directory "$dist\covers" | Out-Null
foreach ($slug in $slugs) {
  $source = @("$root\covers\$slug.jpg", "$root\covers\$slug.png") | Where-Object { Test-Path $_ } | Select-Object -First 1
  if (-not $source) { Write-Warning "No cover for $slug (covers\$slug.jpg): its tile will be a placeholder"; continue }
  & $x360tool cover $source "$dist\covers\$slug.jpg" | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "x360tool cover failed for $source" }
}
foreach ($game in $games) {
  foreach ($exe in $game.exes) { Copy-Item "$($game.release)\$exe.exe", "$($game.release)\$exe.pdb" "$dist\bin" }
  # Shaders and pipelines collected while testing: the launcher seeds the player's cache with them, so
  # a first play doesn't compile them during the game. shader_cache\<id> holds the ones merged from
  # every test run (tools\package\merge_shader_caches.py); else this machine's own cache of the game.
  $cache = "$root\shader_cache\$($game.id)"
  if (-not (Test-Path $cache)) { $cache = "$env:LOCALAPPDATA\AnyX360\$($game.user_dir)\cache\shaders" }
  if (Test-Path $cache) {
    New-Item -ItemType Directory "$dist\bin\shader_cache" -Force | Out-Null
    Copy-Item $cache "$dist\bin\shader_cache\$($game.id)" -Recurse
  }
  # The game's own files.
  $extra = "$PSScriptRoot\$($game.id)\extra.ps1"
  if (Test-Path $extra) { & $extra -Root $root -Dist $dist -Release $game.release }
}

# "a", "a and b", "a, b and c".
function Join-List([string[]]$items) {
  if ($items.Count -le 1) { return ($items -join '') }
  return (($items[0..($items.Count - 2)] -join ', ') + ' and ' + $items[-1])
}
# The upcoming games' too: their box art ships in covers\.
$upcoming = @((Get-Content "$root\tools\launcher\upcoming.json" -Raw | ConvertFrom-Json).games)
$owners = @(@($games) + $upcoming | ForEach-Object { $_.owners -split ', ' } | Select-Object -Unique)
$trademarks = @(@($games) + $upcoming | ForEach-Object { $_.trademarks } | Select-Object -Unique)
$components = @($games | ForEach-Object { $_.components } | Where-Object { $_ } | ForEach-Object { ($_ -split ' \(')[0] } |
  Select-Object -Unique)
$sizes = Join-List @($games | ForEach-Object { "$($_.title) about $($_.size_gb) GB" })
$webcamRequirements = ''
if ($webcam) {
  $webcamRequirements = "- For $(Join-List @($webcam | ForEach-Object { $_.title })), played with a webcam in place of " +
    "the Kinect:`n  a webcam (USB or built in, 720p at 30 FPS or better), a processor with 4 or more cores (the`n" +
    "  pose estimation runs on it) and 2-3 m (7-10 ft) of clear space in front of the webcam, in good light.`n"
}
$componentsNote = ''
if ($components) { $componentsNote = ", $(Join-List $components) in the pose bridge" }
$sections = ($games | ForEach-Object { (Get-Content "$PSScriptRoot\$($_.id)\section.txt" -Raw -Encoding UTF8).TrimEnd() }) -join "`n`n`n"

# Texts as UTF-8 with a BOM and Windows line endings, for Notepad.
$bom = New-Object Text.UTF8Encoding $true
function Write-Text([string]$text, [string]$target) {
  [IO.File]::WriteAllText($target, $text.Replace("`r`n", "`n").Replace("`n", "`r`n"), $bom)
}
$readme = (Get-Content "$PSScriptRoot\README.txt" -Raw -Encoding UTF8)
$readme = $readme.Replace('{SECTIONS}', $sections).Replace('{GAMES}', (Join-List @($games | ForEach-Object { $_.title })))
$readme = $readme.Replace('{SIZES}', $sizes).Replace("{WEBCAM_REQUIREMENTS}`n", $webcamRequirements).Replace("{WEBCAM_REQUIREMENTS}`r`n", $webcamRequirements)
$readme = $readme.Replace('{COMPONENTS_NOTE}', $componentsNote).Replace('{OWNERS}', (Join-List $owners))
$readme = $readme.Replace('{TRADEMARKS}', (Join-List $trademarks)).Replace('{VERSION}', $version)
# Lines that grew with the games' names wrap at 96 columns (list items continue under their text).
$wrapped = foreach ($line in ($readme -split "`r?`n")) {
  $indent = if ($line -match '^(\s*- |\s*)') { ' ' * $Matches[1].Length } else { '' }
  while ($line.Length -gt 96) {
    $cut = $line.LastIndexOf(' ', 96)
    if ($cut -le $indent.Length) { break }
    $line.Substring(0, $cut)
    $line = $indent + $line.Substring($cut + 1)
  }
  $line
}
Write-Text ($wrapped -join "`n") "$dist\README.txt"
Write-Text (Get-Content $terms -Raw -Encoding UTF8) "$dist\TERMS.txt"
Write-Text (Get-Content "$root\CHANGELOG.md" -Raw -Encoding UTF8) "$dist\CHANGELOG.md"

# Antivirus software may still be reading newly copied executables (the pose bridge's): try again.
for ($attempt = 1; ; $attempt++) {
  try {
    Compress-Archive -Path $dist -DestinationPath "$dist.zip" -Force
    break
  } catch {
    if ($attempt -ge 5) { throw }
    Start-Sleep -Seconds 5
  }
}
$size = (Get-Item "$dist.zip").Length / 1MB
"Packaged $dist ($(Join-List @($games | ForEach-Object { $_.title })))"
"Zip: $dist.zip ({0:N0} MB)" -f $size
