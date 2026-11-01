# Makes a release in one command, from the main checkout on its main branch (docs/RELEASING.md):
#
#    1. merges -Branch (--ff-only), when given;
#    2. copies games\<id>\recomp* from -FromWorktree (the recompiled sources aren't in git);
#    3. edits VERSION, the README's version badge and CHANGELOG.md ([Unreleased] becomes
#       "## [X] - <date>" with -Summary under it, and the compare links);
#    4. commits "Release X: <summary>" and tags it (git tag -a vX -m "AnyX360 X");
#    5. puts docs\IDEAS.md (the user's own notes, never committed) aside: a copy and its SHA-256 are
#       kept, then git checkout docs/IDEAS.md, so the release isn't "dirty";
#    6. checks the build's game list (every games\*\game.json has its recompiled sources, and every
#       <TARGET>_PPC_DIR in build\CMakeCache.txt is the folder its config.toml names), then
#       build.bat release at BelowNormal priority with -Jobs parallel jobs;
#    7. runs every build\tests\Release\test_*.exe and build\Release\test_launcher_core.exe;
#    8. tools\package\make_dist.ps1 -NoBuild -OutDir <DistDir>;
#    9. checks that the "Packaged ..." line names every game in games\*\game.json and that their
#       executables are in the release's bin\ (a stale GOW3_PPC_DIR once left Gears of War 3 out of
#       several releases without an error);
#   10. moves the player's installs (games\*, logs) from the previous release folders in <DistDir>
#       into the new one, then deletes those folders and their zips; refuses while a process runs
#       from one of them;
#   11. puts IDEAS.md back and checks its SHA-256 (also when a step fails after step 5);
#   12. computes the launcher's terms hash (for sandboxes' terms_accepted.txt);
#   13. with -Push: pushes the main branch and the tag (agents never pass -Push);
#   14. with -AnyxGames: makes those games' .anyx backups (tools\scripts\make_anyx.ps1) and checks that
#       its report says "password found in the report files: no".
#
#   powershell -ExecutionPolicy Bypass -File tools\scripts\release.ps1 -Version 0.33.0 -Summary "..."
#       [-Branch <branch>] [-FromWorktree <worktree>] [-Date yyyy-MM-dd] [-DistDir <folder>]
#       [-AnyxGames id,id -PasswordNote <note.md>] [-Push] [-WhatIf]
#       [-SkipBuild] [-SkipTests] [-Jobs 6] [-WorkDir <folder>]
#
# Every step is logged (<WorkDir>\release-<version>.log); the first failure stops the release with a
# message saying which step failed and what is left to do. -WhatIf runs the read-only checks and
# prints what each step would do, changing nothing. -SkipBuild/-SkipTests are for dry runs only.
param(
  [Parameter(Mandatory)] [string]$Version,
  [Parameter(Mandatory)] [string]$Summary,
  [string]$Branch = '',
  [string]$FromWorktree = '',
  [string]$Date = (Get-Date -Format 'yyyy-MM-dd'),
  [string]$DistDir = '',
  [string]$AnyxGames = '',
  [string]$PasswordNote = '',
  [string]$AnyxOut = '',
  [switch]$Push,
  [switch]$WhatIf,
  [switch]$SkipBuild,
  [switch]$SkipTests,
  [int]$Jobs = 6,
  [string]$WorkDir = '',
  [string]$MainBranch = 'main',
  [string]$Remote = 'origin',
  # The commit message's last line (empty: none).
  [string]$Trailer = 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\lib.ps1"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $DistDir) { $DistDir = Join-Path $root 'dist' }
if (-not $AnyxOut) { $AnyxOut = Join-Path $root 'anyx' }
if (-not $WorkDir) { $WorkDir = Join-Path $env:TEMP 'anyx360-release' }
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$logFile = Join-Path $WorkDir "release-$Version.log"
$repoUrl = 'https://github.com/danielbarretoes/AnyX360'

function Log([string]$text) {
  $line = "[$(Get-Date -Format 'HH:mm:ss')] $text"
  Write-Host $line
  Add-Content -LiteralPath $logFile -Value $line -Encoding UTF8
}
function Fail([string]$text) { throw $text }

# git in the checkout; its output as lines; a non-zero exit fails the step.
function Invoke-Git {
  $out = & git.exe -C $root @args
  if ($LASTEXITCODE -ne 0) { Fail "git $($args -join ' ') failed (exit $LASTEXITCODE)" }
  return $out
}

$script:stepFailed = $null
$script:current = ''
# Runs one step. -ReadOnly steps (checks) also run with -WhatIf; the others print what they'd do.
function Step([int]$n, [string]$name, [string]$what, [scriptblock]$action, [switch]$ReadOnly) {
  $script:current = "$n. $name"
  if ($WhatIf -and -not $ReadOnly) { Log "WHATIF step $n ($name): would $what"; return }
  Log "step $n ($name): $what"
  & $action
  Log "step $n ($name): done"
}

# ---- text edits -------------------------------------------------------------------------------
function Read-Text([string]$path) {
  $bytes = [IO.File]::ReadAllBytes($path)
  $bom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
  $text = [Text.UTF8Encoding]::new($false).GetString($bytes, $(if ($bom) { 3 } else { 0 }), $bytes.Length - $(if ($bom) { 3 } else { 0 }))
  return [pscustomobject]@{ Text = $text; Bom = $bom; Crlf = $text.Contains("`r`n") }
}
function Write-Text($file, [string]$path) {
  [IO.File]::WriteAllText($path, $file.Text, [Text.UTF8Encoding]::new($file.Bom))
}
# Replaces exactly one occurrence (LF-written $old/$new follow the file's line endings).
function Replace-Once($file, [string]$old, [string]$new, [string]$what) {
  if ($file.Crlf) { $old = $old.Replace("`n", "`r`n"); $new = $new.Replace("`n", "`r`n") }
  $count = ([regex]::Matches($file.Text, [regex]::Escape($old))).Count
  if ($count -ne 1) { Fail "$what`: expected exactly one '$($old.Trim())', found $count" }
  $file.Text = $file.Text.Replace($old, $new)
}

# ---- the game list ----------------------------------------------------------------------------
# Each game's targets and the folder its config names: anyx360_add_game(<id>) -> config.toml,
# anyx360_add_game_executable(<id> <target> ... CONFIG <file>), anyx360_add_title_module(<target>
# <name> CONFIG <file>) -> <TARGET>_<NAME>.
function Get-GameTargets {
  $targets = @()
  foreach ($json in Get-ChildItem "$root\games\*\game.json") {
    $dir = $json.Directory.FullName
    $id = $json.Directory.Name
    $cmake = Get-Content -Raw "$dir\CMakeLists.txt"
    $entries = @()
    if ($cmake -match "anyx360_add_game\($id\b") { $entries += , @($id, 'config.toml', 'game') }
    foreach ($m in [regex]::Matches($cmake, 'anyx360_add_game_executable\(\s*(\w+)\s+(\w+)[^)]*?CONFIG\s+([^\s)]+)')) {
      $entries += , @($m.Groups[2].Value, $m.Groups[3].Value, 'executable')
    }
    foreach ($m in [regex]::Matches($cmake, 'anyx360_add_title_module\(\s*(\w+)\s+(\w+)[^)]*?CONFIG\s+([^\s)]+)')) {
      $entries += , @("$($m.Groups[1].Value)_$($m.Groups[2].Value)", $m.Groups[3].Value, 'module')
    }
    foreach ($e in $entries) {
      $config = Join-Path $dir $e[1]
      $line = Select-String -LiteralPath $config -Pattern '^out_directory_path *= *"([^"]*)"' | Select-Object -First 1
      if (-not $line) { Fail "$config has no out_directory_path" }
      $targets += [pscustomobject]@{
        Game = $id; Target = $e[0]; Kind = $e[2]; Config = $config
        PpcDir = [IO.Path]::GetFullPath((Join-Path $dir $line.Matches[0].Groups[1].Value))
      }
    }
  }
  return $targets
}

function Test-Catalog([switch]$AfterBuild) {
  $targets = Get-GameTargets
  $problems = @()
  foreach ($t in $targets) {
    if (-not (Test-Path (Join-Path $t.PpcDir 'ppc_config.h'))) {
      $problems += "$($t.Game): $($t.Target)'s recompiled sources aren't in $($t.PpcDir) (the build would skip it)"
    }
  }
  # Overrides in the build's cache: each must be the folder the config names.
  $cache = Join-Path $root 'build\CMakeCache.txt'
  if (Test-Path $cache) {
    foreach ($m in Select-String -LiteralPath $cache -Pattern '^([A-Z0-9_]+)_PPC_DIR(:[A-Z]+)?=(.*)$') {
      $name = $m.Matches[0].Groups[1].Value.ToLower()
      $value = $m.Matches[0].Groups[3].Value
      $t = $targets | Where-Object { $_.Target -eq $name } | Select-Object -First 1
      if (-not $t) { $problems += "build\CMakeCache.txt: $($name.ToUpper())_PPC_DIR=$value names no target of games\*"; continue }
      $cached = [IO.Path]::GetFullPath($value.Replace('/', '\')).TrimEnd('\')
      if ($cached -ne $t.PpcDir.TrimEnd('\')) {
        $problems += "build\CMakeCache.txt: $($name.ToUpper())_PPC_DIR=$value, but $($t.Config) names $($t.PpcDir): remove the entry (cmake -U $($name.ToUpper())_PPC_DIR) or fix it"
      }
    }
  }
  if ($problems) { Fail ("the game list is incomplete:`n  " + ($problems -join "`n  ")) }
  Log "  $(@($targets | Select-Object -ExpandProperty Game -Unique).Count) games, $($targets.Count) recompiled targets, all present"
}

# Runs a program at BelowNormal priority (its children inherit it: the compilers of a build), its
# output into <log> and <log>.err; the exit code.
function Invoke-Low([string]$file, [string]$arguments, [string]$log, [hashtable]$environment = @{}) {
  $saved = @{}
  foreach ($k in $environment.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, [string]$environment[$k]) }
  try {
    $start = @{ FilePath = $file; WorkingDirectory = $root; NoNewWindow = $true; PassThru = $true
                RedirectStandardOutput = $log; RedirectStandardError = "$log.err" }
    if ($arguments) { $start.ArgumentList = $arguments }
    $p = Start-Process @start
    $null = $p.Handle  # keeps the exit code readable
    try { $p.PriorityClass = 'BelowNormal' } catch { }
    $p.WaitForExit()
    return $p.ExitCode
  } finally {
    foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
  }
}

# ---- preconditions ----------------------------------------------------------------------------
$ideas = Join-Path $root 'docs\IDEAS.md'
$ideasBackup = Join-Path $WorkDir "IDEAS_$Version.md"
$ideasHash = ''
$ideasAside = $false
$newDist = Join-Path $DistDir "AnyX360-$Version"
$exitCode = 0
Log "release $Version ($Date) from $root$(if ($WhatIf) { ' [WhatIf: nothing is changed]' })"
try {
  Step 0 'preconditions' 'check the version, the branch and the working tree' -ReadOnly {
    if ($Version -notmatch '^\d+\.\d+\.\d+$') { Fail "-Version must be X.Y.Z, not '$Version'" }
    if (-not $Summary.Trim()) { Fail '-Summary is empty' }
    $branchNow = (Invoke-Git rev-parse --abbrev-ref HEAD | Select-Object -First 1).Trim()
    if ($branchNow -ne $MainBranch) { Fail "the checkout is on '$branchNow', not '$MainBranch'" }
    if (Invoke-Git tag --list "v$Version") { Fail "the tag v$Version exists already" }
    $old = (Get-Content "$root\VERSION" -TotalCount 1).Trim()
    if ([version]$old -ge [version]$Version) { Fail "VERSION is ${old}: $Version isn't newer" }
    $script:oldVersion = $old
    # Tracked changes other than IDEAS.md would end up in the release (or block the merge).
    $dirty = @(Invoke-Git status --porcelain --untracked-files=no | Where-Object { $_ -and $_.Substring(3).Trim() -ne 'docs/IDEAS.md' })
    if ($dirty) { Fail "uncommitted changes: $($dirty -join '; ')" }
    if ($AnyxGames -and -not $PasswordNote) { Fail '-AnyxGames needs -PasswordNote (the note holding the archives'' password)' }
    if ($PasswordNote -and -not (Test-Path -LiteralPath $PasswordNote)) { Fail "-PasswordNote: $PasswordNote is missing" }
    if (Test-Path $newDist) { Fail "$newDist exists already" }
    Log "  $old -> $Version on $MainBranch; dist folder $DistDir"
  }

  Step 0 'IDEAS.md copy' 'keep a copy of docs\IDEAS.md and its SHA-256 (step 5 checks it out, step 11 puts it back)' {
    if (Test-Path $ideas) {
      Copy-Item -LiteralPath $ideas $ideasBackup -Force
      $script:ideasHash = (Get-FileHash -LiteralPath $ideas -Algorithm SHA256).Hash
      Set-Content -LiteralPath "$ideasBackup.sha256" -Value $script:ideasHash -Encoding ascii
      Log "  copy: $ideasBackup (SHA-256 $($script:ideasHash))"
    } else { Log '  no docs\IDEAS.md' }
  }

  if ($Branch) {
    Step 1 'merge' "merge $Branch into $MainBranch (--ff-only)" { Invoke-Git merge --ff-only $Branch | ForEach-Object { Log "  $_" } }
  }

  if ($FromWorktree) {
    Step 2 'recompiled sources' "copy games\<id>\recomp* from $FromWorktree" {
      if (-not (Test-Path "$FromWorktree\games")) { Fail "$FromWorktree has no games folder" }
      foreach ($dir in Get-ChildItem "$FromWorktree\games\*\recomp*" -Directory) {
        $target = Join-Path $root "games\$($dir.Parent.Name)\$($dir.Name)"
        if (Test-Path $target) { Log "  $($dir.Parent.Name)\$($dir.Name): already here"; continue }
        & robocopy $dir.FullName $target /E /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { Fail "robocopy of $($dir.FullName) failed ($LASTEXITCODE)" }
        Log "  $($dir.Parent.Name)\$($dir.Name): copied"
      }
    }
  }

  Step 3 'release edits' "set VERSION, the README badge and CHANGELOG.md to $Version" {
    $readme = Read-Text "$root\README.md"
    Replace-Once $readme "version-$oldVersion-2ea44f" "version-$Version-2ea44f" 'README.md'
    $log = Read-Text "$root\CHANGELOG.md"
    $after = $log.Text.Replace("`r`n", "`n")
    $i = $after.IndexOf("## [Unreleased]`n")
    if ($i -lt 0) { Fail 'CHANGELOG.md has no "## [Unreleased]" heading' }
    if ($after.Substring($i + 16).TrimStart().StartsWith('## [')) { Fail 'CHANGELOG.md: [Unreleased] is empty' }
    $text = $Summary.Trim()
    if (-not $text.EndsWith('.')) { $text += '.' }
    Replace-Once $log "## [Unreleased]`n`n" "## [Unreleased]`n`n## [$Version] - $Date`n`n$text`n`n" 'CHANGELOG.md'
    Replace-Once $log "[Unreleased]: $repoUrl/compare/v$oldVersion...HEAD`n" `
      "[Unreleased]: $repoUrl/compare/v$Version...HEAD`n[$Version]: $repoUrl/compare/v$oldVersion...v$Version`n" 'CHANGELOG.md links'
    Write-Text $readme "$root\README.md"
    Write-Text $log "$root\CHANGELOG.md"
    [IO.File]::WriteAllText("$root\VERSION", "$Version`n", [Text.UTF8Encoding]::new($false))
  }

  Step 4 'commit and tag' "commit 'Release ${Version}: $($Summary.TrimEnd('.'))' and tag v$Version" {
    Invoke-Git add -- VERSION README.md CHANGELOG.md | Out-Null
    # The message from a file: a summary with quotes survives it.
    $message = "Release ${Version}: $($Summary.Trim().TrimEnd('.'))`n"
    if ($Trailer) { $message += "`n$Trailer`n" }
    $messageFile = Join-Path $WorkDir "commit-$Version.txt"
    [IO.File]::WriteAllText($messageFile, $message, [Text.UTF8Encoding]::new($false))
    Invoke-Git commit -q -F $messageFile | Out-Null
    Invoke-Git tag -a "v$Version" -m "AnyX360 $Version" | Out-Null
    Log "  $((Invoke-Git log -1 --format='%h %s') -join '')"
  }

  Step 5 'IDEAS.md out' 'git checkout docs/IDEAS.md (the release must not be dirty)' {
    if ((Invoke-Git status --porcelain -- docs/IDEAS.md)) {
      Invoke-Git checkout -- docs/IDEAS.md | Out-Null
      $script:ideasAside = $true
    }
    $left = @(Invoke-Git status --porcelain --untracked-files=no)
    if ($left) { Fail "the working tree isn't clean: $($left -join '; ')" }
  }

  Step 6 'game list' 'check every game''s recompiled sources and the build cache''s *_PPC_DIR' -ReadOnly { Test-Catalog }

  if ($SkipBuild) { Log 'step 6 (build): skipped (-SkipBuild)' }
  else {
    Step 6 'build' "build.bat release at BelowNormal, $Jobs jobs" {
      $buildLog = Join-Path $WorkDir "build-$Version.log"
      $code = Invoke-Low $env:ComSpec "/d /s /c `"`"$root\build.bat`" release`"" $buildLog @{ CMAKE_BUILD_PARALLEL_LEVEL = "$Jobs" }
      if ($code -ne 0) { Fail "build.bat release failed (exit $code): see $buildLog" }
      Log "  log: $buildLog"
      Test-Catalog
    }
  }

  if ($SkipTests) { Log 'step 7 (tests): skipped (-SkipTests)' }
  else {
    Step 7 'tests' 'run build\tests\Release\test_*.exe and build\Release\test_launcher_core.exe' {
      $tests = @(Get-ChildItem "$root\build\tests\Release\test_*.exe") + @(Get-Item "$root\build\Release\test_launcher_core.exe" -ErrorAction SilentlyContinue)
      if ($tests.Count -lt 2) { Fail 'no test programs in build\tests\Release (build first)' }
      foreach ($t in $tests) {
        $testLog = Join-Path $WorkDir "$($t.BaseName)-$Version.log"
        $code = Invoke-Low $t.FullName '' $testLog
        if ($code -ne 0) { Fail "$($t.Name) failed (exit $code): see $testLog" }
        Log "  $($t.Name): passed"
      }
    }
  }

  Step 8 'package' "tools\package\make_dist.ps1 -NoBuild -OutDir $DistDir" {
    $distLog = Join-Path $WorkDir "dist-$Version.log"
    # Its output as UTF-8 (titles such as Diablo III's have non-ASCII characters).
    $command = "[Console]::OutputEncoding = [Text.Encoding]::UTF8; & '$root\tools\package\make_dist.ps1' -NoBuild -OutDir '$DistDir'"
    $code = Invoke-Low 'powershell.exe' "-NoProfile -ExecutionPolicy Bypass -Command `"$command`"" $distLog
    $script:distOutput = Get-Content -LiteralPath $distLog -Encoding UTF8
    if ($code -ne 0) { Fail "make_dist.ps1 failed (exit $code): see $distLog and $distLog.err" }
    $script:distOutput | Where-Object { $_ -match '^(Packaged |Zip: )' } | ForEach-Object { Log "  $_" }
  }

  Step 9 'catalog check' 'check the release has every game in games\*\game.json' {
    $packaged = @($script:distOutput | Where-Object { $_ -match '^Packaged ' }) | Select-Object -Last 1
    if (-not $packaged) { Fail 'make_dist printed no "Packaged ..." line' }
    if ($packaged -notmatch [regex]::Escape("Packaged $newDist (")) {
      Fail "the release folder isn't $newDist (a version with +commit means HEAD isn't the tag or the tree is dirty): $packaged"
    }
    $missing = @()
    foreach ($json in Get-ChildItem "$root\games\*\game.json") {
      $info = Get-Content -Raw -Encoding UTF8 $json.FullName | ConvertFrom-Json
      $exes = @($info.exe)
      if ($info.executables) { $exes = @($info.executables.PSObject.Properties | ForEach-Object { $_.Value }) }
      # Exact, or with its non-ASCII characters as wildcards (should the output's encoding change).
      $loose = '(?i)' + (($info.title.ToCharArray() | ForEach-Object { if ([int]$_ -gt 127) { '.{1,3}' } else { [regex]::Escape([string]$_) } }) -join '')
      if (-not $packaged.Contains($info.title) -and $packaged -notmatch $loose) { $missing += "$($info.id) ($($info.title)) isn't in the Packaged list" }
      foreach ($exe in $exes) { if (-not (Test-Path "$newDist\bin\$exe.exe")) { $missing += "$($info.id): bin\$exe.exe is missing" } }
    }
    if ($missing) { Fail ("the release lacks games:`n  " + ($missing -join "`n  ")) }
    Log "  all $(@(Get-ChildItem "$root\games\*\game.json").Count) games are in $newDist"
  }

  Step 10 'installs' "move the players' installs from the previous release folders in $DistDir, then delete those" {
    # Older releases by their names' versions (never by path: 8.3 and long forms of one folder differ).
    $old = @(Get-ChildItem -LiteralPath $DistDir -Directory -Filter 'AnyX360-*' | Where-Object {
        $_.Name -match '^AnyX360-(\d+\.\d+\.\d+)(\+.*)?$' -and [version]$Matches[1] -lt [version]$Version })
    $others = @(Get-ChildItem -LiteralPath $DistDir -Directory -Filter 'AnyX360-*' | Where-Object { $_.Name -ne "AnyX360-$Version" -and $old.Name -notcontains $_.Name })
    if ($others) { Log "  left alone (not older than $Version): $($others.Name -join ', ')" }
    foreach ($dir in $old) {
      $prefix = $dir.FullName.TrimEnd('\') + '\'
      $running = @(Get-CimInstance Win32_Process -Property ProcessId, Name, ExecutablePath |
        Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) })
      if ($running) { Fail "$($dir.Name) is in use ($(($running | ForEach-Object { "$($_.Name) $($_.ProcessId)" }) -join ', ')): close it and run the rest by hand (docs/RELEASING.md)" }
    }
    foreach ($dir in $old) {
      foreach ($game in @(Get-ChildItem -LiteralPath "$($dir.FullName)\games" -Directory -ErrorAction SilentlyContinue)) {
        $target = Join-Path $newDist "games\$($game.Name)"
        if (Test-Path $target) { Fail "$target exists already: $($game.FullName) not moved" }
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Move-Item -LiteralPath $game.FullName -Destination $target
        Log "  moved $($dir.Name)\games\$($game.Name)"
      }
      if (Test-Path "$($dir.FullName)\logs") {
        if (-not (Test-Path "$newDist\logs")) { Move-Item -LiteralPath "$($dir.FullName)\logs" -Destination "$newDist\logs" }
        else { Get-ChildItem -LiteralPath "$($dir.FullName)\logs" | Move-Item -Destination "$newDist\logs" -Force }
        Log "  moved $($dir.Name)\logs"
      }
      # Nothing of the player's may be deleted: no links left inside (a copy in place of its folder).
      $links = @(Get-ChildItem -LiteralPath $dir.FullName -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue)
      if ($links) { Fail "$($dir.Name) still holds links ($($links[0].FullName)...): not deleted" }
      Remove-Item -LiteralPath $dir.FullName -Recurse -Force
      if (Test-Path "$($dir.FullName).zip") { Remove-Item -LiteralPath "$($dir.FullName).zip" -Force }
      Log "  deleted $($dir.Name) and its zip"
    }
    if (-not $old) { Log '  no previous release folder' }
  }

  Step 11 'IDEAS.md back' 'put docs\IDEAS.md back and check its SHA-256' {
    if ($script:ideasHash) {
      Copy-Item -LiteralPath $ideasBackup $ideas -Force
      $now = (Get-FileHash -LiteralPath $ideas -Algorithm SHA256).Hash
      if ($now -ne $script:ideasHash) { Fail "docs\IDEAS.md's SHA-256 is $now, not $($script:ideasHash): the copy is $ideasBackup" }
      $script:ideasAside = $false
      Log "  restored, SHA-256 matches"
    }
  }

  Step 12 'terms hash' 'compute the launcher''s terms hash' -ReadOnly {
    $script:termsHash = Get-TermsHash "$root\build\launcher\launcher_terms.h"
    Set-Content -LiteralPath (Join-Path $WorkDir "terms_hash-$Version.txt") -Value $script:termsHash -Encoding ascii
    Log "  terms hash: $($script:termsHash)"
  }

  if ($Push) {
    Step 13 'push' "push $MainBranch and v$Version to $Remote" {
      Invoke-Git push $Remote $MainBranch | Out-Null
      Invoke-Git push $Remote "v$Version" | Out-Null
    }
  } else { Log 'step 13 (push): not asked (-Push)' }

  if ($AnyxGames) {
    Step 14 '.anyx' "make the .anyx backups of $AnyxGames into $AnyxOut" {
      $report = Join-Path $WorkDir "anyx-$Version"
      $anyxLog = Join-Path $WorkDir "anyx-$Version.log"
      $code = Invoke-Low 'powershell.exe' ("-NoProfile -ExecutionPolicy Bypass -File `"$PSScriptRoot\make_anyx.ps1`" -Games $AnyxGames " +
        "-Launcher `"$newDist\AnyX360.exe`" -GameBin `"$newDist\bin`" -Out `"$AnyxOut`" -PasswordNote `"$PasswordNote`" " +
        "-Report `"$report`" -TermsHash $($script:termsHash)") $anyxLog
      $lines = Get-Content -LiteralPath "$report\summary.txt" -Encoding UTF8 -ErrorAction SilentlyContinue
      $lines | Where-Object { $_ -match '\] (PASS|FAIL) ' } | ForEach-Object { Log "  $_" }
      if (-not ($lines | Where-Object { $_ -match 'password found in the report files: no$' })) {
        Fail "make_anyx's report doesn't say 'password found in the report files: no': see $report\summary.txt"
      }
      $failed = @($lines | Where-Object { $_ -match '\] FAIL ' })
      if ($code -ne 0 -or $failed) { Fail "make_anyx failed ($($failed.Count) games): see $report\summary.txt and $anyxLog" }
    }
  }
  Log "release $Version $(if ($WhatIf) { 'checked (WhatIf)' } else { 'done' })"
} catch {
  Log "FAILED at step $($script:current): $($_.Exception.Message)"
  $exitCode = 1
} finally {
  # IDEAS.md never stays checked out: back from its copy, whatever failed.
  if ($ideasAside -and $script:ideasHash -and (Test-Path $ideasBackup)) {
    Copy-Item -LiteralPath $ideasBackup $ideas -Force
    $now = (Get-FileHash -LiteralPath $ideas -Algorithm SHA256).Hash
    Log "docs\IDEAS.md put back from $ideasBackup ($(if ($now -eq $script:ideasHash) { 'SHA-256 matches' } else { 'SHA-256 DIFFERS' }))"
  }
  Log "log: $logFile"
}
exit $exitCode
