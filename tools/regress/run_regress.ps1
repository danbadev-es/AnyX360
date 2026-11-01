# The regression battery (docs/REGRESSION.md): for each game, one at a time,
#   1. a sandbox (<Sandbox>\<id>: a release-like root and its own LOCALAPPDATA) and the game imported
#      into it from anyx\<id>.anyx by the release's launcher (AnyX360.exe --archive ... --quit-when-done);
#   2. the game started from the release's bin\ with its short scripted route (routes.json: ANYX360_INPUT
#      for pads, ANYX360_NUI_SCRIPT for the Kinect player), every player's device "none" so a pad on
#      this machine can't drive it, System Link off;
#   3. while it runs: the log's 5-second samples (frames: swaps/s, p99, max; cpu:; gpu:) timed from the
#      start, screenshots at the route's times by the window's exact title, among its own processes;
#   4. stopped by PID (its processes only), then a summary: <Out>\<id>\summary.json (the samples, each
#      phase's median frame rate and p99, the error lines, the screenshots, the DLC packs, the machine's
#      load and idle time before and after), and the sandbox deleted (only the report stays).
# Then tools\regress\compare.ps1 compares the run with tools\regress\baseline\.
#
#   powershell -ExecutionPolicy Bypass -File tools\regress\run_regress.ps1 [-Games gow3,mk|all]
#     [-Release <dist\AnyX360-X>] [-Anyx <folder of .anyx>] [-Out <folder>] [-Sandbox C:\axreg]
#     [-TermsHash <hash>] [-KeepSandbox] [-SkipImport]
#
# -Release is used read-only (its AnyX360.exe and bin\). -TermsHash: the terms the release's launcher
# shows (default: computed from the main checkout's build\launcher\launcher_terms.h; tools\scripts\
# release.ps1 logs it). -SkipImport reuses a kept sandbox (-KeepSandbox) for quick route work.
param(
  [string]$Games = 'all',
  [string]$Release = '',
  [string]$Anyx = '',
  [string]$Out = '',
  [string]$Sandbox = 'C:\axreg',
  [string]$TermsHash = '',
  [string]$DataRoot = '',
  # For Doritos' avatar pack (stfs-extract): the main checkout's build.
  [string]$X360Tool = '',
  [string]$Routes = "$PSScriptRoot\routes.json",
  [switch]$KeepSandbox,
  [switch]$SkipImport,
  [int]$ImportMinutes = 60
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\scripts\lib.ps1"
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
# keys\, anyx\, dist\ and build\ are git-ignored: the main checkout's (from a worktree, give -DataRoot).
if (-not $DataRoot) {
  $DataRoot = $repo
  $common = (& git -C $repo rev-parse --path-format=absolute --git-common-dir 2>$null)
  if ($LASTEXITCODE -eq 0 -and $common) { $DataRoot = Split-Path -Parent ($common.Trim().Replace('/', '\')) }
}
if (-not $Release) {
  $Release = Get-ChildItem "$DataRoot\dist" -Directory -Filter 'AnyX360-*' | Sort-Object LastWriteTime | Select-Object -Last 1 -ExpandProperty FullName
}
if (-not $Release -or -not (Test-Path "$Release\AnyX360.exe")) { throw "No release folder with AnyX360.exe (give -Release)" }
if (-not $Anyx) { $Anyx = "$DataRoot\anyx" }
if (-not $Out) { $Out = Join-Path $env:TEMP ("anyx360-regress\" + (Get-Date -Format 'yyyyMMdd-HHmm')) }
if (-not $X360Tool) { $X360Tool = "$DataRoot\build\tools\x360tool\Release\x360tool.exe" }
if (-not $TermsHash) { $TermsHash = Get-TermsHash "$DataRoot\build\launcher\launcher_terms.h" }
$routeSet = (Get-Content -Raw -Encoding UTF8 $Routes | ConvertFrom-Json).games
$ids = if ($Games -eq 'all') { @($routeSet.PSObject.Properties.Name) } else { @($Games -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
New-Item -ItemType Directory -Force $Out, $Sandbox | Out-Null
$runLog = Join-Path $Out 'run.log'
function Log([string]$text) {
  $line = "[$(Get-Date -Format 'HH:mm:ss')] $text"
  Write-Host $line
  Add-Content -LiteralPath $runLog -Value $line -Encoding UTF8
}

# Every game executable name of the catalog: other copies of them running now (other agents', the
# player's) load the machine, and are reported.
$gameExes = @()
foreach ($json in Get-ChildItem "$repo\games\*\game.json") {
  $info = Get-Content -Raw -Encoding UTF8 $json.FullName | ConvertFrom-Json
  $gameExes += $info.exe
  if ($info.executables) { $gameExes += @($info.executables.PSObject.Properties | ForEach-Object { $_.Value }) }
}
$gameExes = @($gameExes | Select-Object -Unique)

function Get-MachineState([uint32[]]$Own = @()) {
  $loads = @()
  for ($i = 0; $i -lt 3; $i++) {
    $loads += (Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average).Average
    Start-Sleep -Milliseconds 700
  }
  $os = Get-CimInstance Win32_OperatingSystem
  $others = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $gameExes -contains $_.ProcessName -and $Own -notcontains [uint32]$_.Id } |
    ForEach-Object { "$($_.ProcessName) $($_.Id)" })
  # The busiest other processes (CPU seconds over a second).
  $a = @{}; Get-Process | ForEach-Object { try { $a[$_.Id] = $_.TotalProcessorTime.TotalSeconds } catch { } }
  Start-Sleep -Seconds 1
  $busy = @(Get-Process | ForEach-Object {
      try { if ($a.ContainsKey($_.Id)) { [pscustomobject]@{ n = "$($_.ProcessName) $($_.Id)"; c = $_.TotalProcessorTime.TotalSeconds - $a[$_.Id] } } } catch { }
    } | Where-Object { $_.c -gt 0.25 -and $Own -notcontains [uint32]($_.n -split ' ')[-1] } | Sort-Object c -Descending | Select-Object -First 5 |
    ForEach-Object { '{0} {1:N0}%' -f $_.n, ($_.c * 100) })
  return [ordered]@{
    time = (Get-Date).ToString('s'); cpu_load = [math]::Round(($loads | Measure-Object -Average).Average, 1)
    free_memory_mb = [int]($os.FreePhysicalMemory / 1024); idle_seconds = [math]::Round((Get-IdleSeconds), 0)
    other_games = $others; busy_processes = $busy
  }
}

# Deletes a sandbox: links inside are removed as links first (never followed), then the rest.
function Remove-Sandbox([string]$dir) {
  if (-not (Test-Path -LiteralPath $dir)) { return }
  foreach ($link in @(Get-ChildItem -LiteralPath $dir -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue | Sort-Object { $_.FullName.Length } -Descending)) {
    if ($link.PSIsContainer) { [IO.Directory]::Delete($link.FullName, $false) } else { [IO.File]::Delete($link.FullName) }
  }
  cmd /c rd /s /q "`"$dir`"" | Out-Null
  if (Test-Path -LiteralPath $dir) { Log "  WARN: $dir not fully deleted" }
}

# The log's text so far (the game keeps it open for writing).
function Read-Shared([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return '' }
  $fs = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite')
  try { return (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Close() }
}

$framesRe = [regex]'frames: ([0-9.]+) swaps/s .*?p99 ([0-9.]+) max ([0-9.]+) ms, present'
$cpuRe = [regex]'cpu: ([0-9.]+) cores'
$gpuRe = [regex]'gpu: ([0-9.]+) ms GPU per frame'
$errorRe = [regex]'^\[E\]|=== CRASH|unhandled exception|fatal error'

function Median([double[]]$values) {
  if (-not $values -or $values.Count -eq 0) { return $null }
  $s = @($values | Sort-Object)
  $n = $s.Count
  if ($n % 2) { return $s[($n - 1) / 2] } else { return ($s[$n / 2 - 1] + $s[$n / 2]) / 2 }
}

$runInfo = [ordered]@{
  date = (Get-Date).ToString('s'); release = $Release; anyx = $Anyx; sandbox = $Sandbox; games = $ids
  computer = $env:COMPUTERNAME; results = [ordered]@{}
}
Log "regression run: $($ids -join ', ') with $Release into $Out"
foreach ($id in $ids) {
  $route = $routeSet.$id
  if (-not $route) { Log "== $id`: no route in routes.json, skipped"; continue }
  $info = Get-Content -Raw -Encoding UTF8 "$repo\games\$id\game.json" | ConvertFrom-Json
  $gameOut = Join-Path $Out $id
  if (Test-Path $gameOut) { Remove-Item -LiteralPath $gameOut -Recurse -Force }
  New-Item -ItemType Directory -Force $gameOut | Out-Null
  $summary = [ordered]@{
    id = $id; title = $info.title; date = (Get-Date).ToString('s'); release = $Release; route = $route
    import = $null; dlc = $null; update = $null; window_titles = @(); expected_title = $route.title
    shots = @(); samples = @(); phases = [ordered]@{}; errors = @(); error_count = 0; exit = $null
    machine_before = $null; machine_after = $null; steady = $null; notes = @()
  }
  $box = Join-Path $Sandbox $id
  $root = "$box\root"; $local = "$box\local"; $user = "$local\AnyX360\$($info.user_dir)"
  Log "== $id ($($info.title))"
  try {
    $summary.machine_before = Get-MachineState
    if ($summary.machine_before.other_games) { Log "  other game processes running: $($summary.machine_before.other_games -join ', ')" }

    # 1. Sandbox and import.
    if (-not $SkipImport) {
      Remove-Sandbox $box
      New-Item -ItemType Directory -Force "$root\bin", "$root\keys", "$local\AnyX360" | Out-Null
      Copy-Item "$Release\bin\7z.exe", "$Release\bin\7z.dll" "$root\bin\"
      Copy-Item "$DataRoot\keys\xex2_retail.key" "$root\keys\"
      if (Test-Path "$DataRoot\keys\title_keys.txt") { Copy-Item "$DataRoot\keys\title_keys.txt" "$root\keys\" }
      [IO.File]::WriteAllText("$local\AnyX360\terms_accepted.txt", $TermsHash)
      $archive = Join-Path $Anyx "$id.anyx"
      if (-not (Test-Path $archive)) { throw "no $archive" }
      $resultFile = "$box\import.txt"
      $started = Get-Date
      $saved = $env:LOCALAPPDATA; $env:LOCALAPPDATA = $local
      try {
        $p = Start-Process -FilePath "$Release\AnyX360.exe" -WindowStyle Minimized -PassThru `
          -ArgumentList "--root `"$root`" --archive `"$archive`" --quit-when-done --result `"$resultFile`""
      } finally { $env:LOCALAPPDATA = $saved }
      if (-not $p.WaitForExit($ImportMinutes * 60000)) {
        Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p.Id)" | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        throw "import timed out after $ImportMinutes min"
      }
      $result = if (Test-Path $resultFile) { (Get-Content -Raw -Encoding UTF8 $resultFile).Trim() } else { 'no result' }
      $summary.import = [ordered]@{ result = $result; seconds = [int]((Get-Date) - $started).TotalSeconds; anyx_bytes = (Get-Item $archive).Length }
      Log "  import ($($summary.import.seconds) s): $result"
      if (-not $result.StartsWith('ok:')) { throw "import failed: $result" }
    }
    $summary.dlc = @(Get-ChildItem -LiteralPath "$user\content\00000002" -Directory -ErrorAction SilentlyContinue).Count
    $summary.update = @(Get-ChildItem -LiteralPath "$user\content\000B0000" -Directory -ErrorAction SilentlyContinue).Count
    Log "  DLC packs: $($summary.dlc), title updates: $($summary.update)"

    # 2. The player's settings, the release's shader cache (as the launcher seeds it), the avatar data.
    New-Item -ItemType Directory -Force $user | Out-Null
    $settings = @('[display]', 'fullscreen = false', '', '[players]', 'sign_in = "on_demand"')
    for ($i = 1; $i -le 4; $i++) { $settings += "p$i = { device = `"none`", name = `"Player $i`" }" }
    $settings += @('', '[network]', 'enabled = false')
    if ($route.settings) { $settings += @('') + @($route.settings) }
    [IO.File]::WriteAllText("$user\settings.toml", ($settings -join "`n") + "`n")
    if (Test-Path "$Release\bin\shader_cache\$id") {
      New-Item -ItemType Directory -Force "$user\cache\shaders" | Out-Null
      Copy-Item "$Release\bin\shader_cache\$id\*" "$user\cache\shaders\" -Recurse -Force
    }
    if ($route.avatar_pack_from -and -not (Test-Path "$local\AnyX360\system\AvatarAssetPack.toc")) {
      # Doritos has no Avatar data of its own: the pack another game's disc carries, as the launcher
      # installs it (a version 2 table; this machine's own may be Reach's older one, which crashes it).
      $pack = "$box\avatar_pack"
      & "$Release\bin\7z.exe" e (Join-Path $Anyx "$($route.avatar_pack_from).anyx") 'game\AvatarAssetPack' "-o$pack" -y | Out-Null
      & $X360Tool stfs-extract "$pack\AvatarAssetPack" "$pack\out" | Out-Null
      if (Test-Path "$pack\out\AvatarAssetPack.toc") {
        New-Item -ItemType Directory -Force "$local\AnyX360\system" | Out-Null
        Move-Item "$pack\out\AvatarAssetPack.toc" "$local\AnyX360\system\"
        Log "  avatar pack from $($route.avatar_pack_from).anyx"
      } else { $summary.notes += "no avatar pack from $($route.avatar_pack_from).anyx" }
      Remove-Item -LiteralPath $pack -Recurse -Force -ErrorAction SilentlyContinue
    }

    # 3. The game with its route.
    $exe = "$Release\bin\$($info.exe).exe"
    $env1 = @{ LOCALAPPDATA = $local; ANYX360_NO_ERROR_BOX = '1' }
    if ($route.input) { $env1.ANYX360_INPUT = $route.input }
    if ($route.input_clock) { $env1.ANYX360_INPUT_CLOCK = $route.input_clock }
    if ($route.nui) {
      $scriptFile = Join-Path $gameOut 'nui_script.txt'
      [IO.File]::WriteAllText($scriptFile, ($route.nui -replace ';', ','))
      $env1.ANYX360_NUI_SCRIPT = "@$scriptFile"
    }
    $saved = @{}
    foreach ($k in $env1.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, $env1[$k]) }
    try {
      $game = Start-Process -FilePath $exe -PassThru -RedirectStandardOutput "$gameOut\stdout.log" -RedirectStandardError "$gameOut\stderr.log" `
        -ArgumentList "--game `"$root\games\$id\game`" --user `"$user`" --key `"$root\keys\xex2_retail.key`""
    } finally {
      foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
    }
    $null = $game.Handle
    $start = Get-Date
    $tracked = [System.Collections.Generic.HashSet[uint32]]::new()
    [void]$tracked.Add([uint32]$game.Id)
    Log "  started $($info.exe) ($($game.Id)), $($route.seconds) s"
    $shotTimes = @($route.shots | ForEach-Object { [double]$_ })
    $taken = @{}
    $seenLines = 0
    $title = $route.title
    $titles = [System.Collections.Generic.HashSet[string]]::new()
    $lastScan = [datetime]::MinValue
    while ($true) {
      $elapsed = ((Get-Date) - $start).TotalSeconds
      if ($elapsed -ge $route.seconds) { break }
      # The game's other executables (MW3/BO2 multiplayer, BO3 Zombies) are its children.
      if (((Get-Date) - $lastScan).TotalSeconds -ge 5) {
        $lastScan = Get-Date
        $all = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId)
        do {
          $added = $false
          foreach ($p in $all) { if ($tracked.Contains([uint32]$p.ParentProcessId) -and $tracked.Add([uint32]$p.ProcessId)) { $added = $true; Log "  child process $($p.ProcessId)" } }
        } while ($added)
        foreach ($t in [AnyX360Tools.Win]::Titles([uint32[]]@($tracked))) { if ($titles.Add($t)) { Log "  window '$t' at $([int]$elapsed) s" } }
      }
      # New samples, timed now (they come every 5 s).
      $lines = (Read-Shared "$gameOut\stderr.log") -split "`r?`n"
      for ($i = $seenLines; $i -lt $lines.Count - 1; $i++) {
        $m = $framesRe.Match($lines[$i])
        if ($m.Success) {
          $summary.samples += [pscustomobject][ordered]@{ t = [math]::Round($elapsed, 1); swaps = [double]$m.Groups[1].Value
            p99 = [double]$m.Groups[2].Value; max = [double]$m.Groups[3].Value; cpu_cores = $null; gpu_ms = $null }
          continue
        }
        $c = $cpuRe.Match($lines[$i])
        if ($c.Success -and $summary.samples.Count) { $summary.samples[-1].cpu_cores = [double]$c.Groups[1].Value; continue }
        $g = $gpuRe.Match($lines[$i])
        if ($g.Success -and $summary.samples.Count) { $summary.samples[-1].gpu_ms = [double]$g.Groups[1].Value }
      }
      $seenLines = [math]::Max($seenLines, $lines.Count - 1)
      foreach ($t in $shotTimes) {
        if ($elapsed -ge $t -and -not $taken.ContainsKey($t)) {
          $taken[$t] = $true
          # Without a route title: the game's window as titled now (Minecraft renames its window).
          if (-not $route.title) { $title = @([AnyX360Tools.Win]::Titles([uint32[]]@($tracked)) | Where-Object { $_.StartsWith('AnyX360 - ') }) | Select-Object -First 1 }
          $file = 'shot_{0:000}.png' -f [int]$t
          $ok = $false
          if ($title) { $ok = Save-GameWindow $title "$gameOut\$file" ([uint32[]]@($tracked)) }
          $lit = if ($ok) { [math]::Round([AnyX360Tools.Win]::Lit("$gameOut\$file"), 3) } else { $null }
          $summary.shots += [ordered]@{ t = [int]$t; file = $(if ($ok) { $file } else { $null }); lit = $lit }
          if (-not $ok) { Log "  no window for the screenshot at $t s" }
        }
      }
      if (-not @($tracked | Where-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue })) {
        $summary.exit = [ordered]@{ state = 'exited'; at = [int]$elapsed; code = $game.ExitCode }
        Log "  the game exited at $([int]$elapsed) s (code $($game.ExitCode))"
        break
      }
      Start-Sleep -Milliseconds 500
    }
    foreach ($t in $shotTimes) { if (-not $taken.ContainsKey($t)) { $summary.shots += [ordered]@{ t = [int]$t; file = $null; lit = $null } } }
    $summary.machine_after = Get-MachineState ([uint32[]]@($tracked))
    # 4. Stopped: only its own processes.
    foreach ($procId in $tracked) {
      if (Get-Process -Id $procId -ErrorAction SilentlyContinue) { Stop-Process -Id $procId -Force -ErrorAction SilentlyContinue }
    }
    if (-not $summary.exit) { $summary.exit = [ordered]@{ state = 'running'; at = $route.seconds; code = $null } }
    $game.WaitForExit(20000) | Out-Null
    $summary.window_titles = @($titles)
    if ($title -and -not $route.title) { $summary.notes += "window title found: $title" }

    # The log: error lines (numbers and addresses masked, so runs compare), and the phases.
    $text = (Read-Shared "$gameOut\stderr.log") + "`n" + (Read-Shared "$gameOut\stdout.log")
    $errors = @($text -split "`r?`n" | Where-Object { $errorRe.IsMatch($_) })
    $summary.error_count = $errors.Count
    $summary.errors = @($errors | ForEach-Object { ($_ -replace '0x[0-9A-Fa-f]+|\b[0-9A-Fa-f]{8,16}\b', '#') -replace '\d+', 'N' } | Select-Object -Unique | Select-Object -First 60)
    foreach ($phase in $route.phases.PSObject.Properties) {
      $from = [double]$phase.Value[0]; $to = [double]$phase.Value[1]
      $in = @($summary.samples | Where-Object { $_.t -gt $from -and $_.t -le $to + 1 })
      $summary.phases[$phase.Name] = [ordered]@{
        from = $from; to = $to; samples = $in.Count
        swaps_median = Median @($in | ForEach-Object { $_.swaps }); swaps_min = ($in | Measure-Object -Property swaps -Minimum).Minimum
        p99_median = Median @($in | ForEach-Object { $_.p99 }); p99_max = ($in | Measure-Object -Property p99 -Maximum).Maximum
        max_max = ($in | Measure-Object -Property max -Maximum).Maximum
      }
    }
    if ($route.probe -and $summary.phases[$route.probe] -and $null -ne $summary.phases[$route.probe].p99_median) {
      $summary.steady = $summary.phases[$route.probe].p99_median -le 300
      if (-not $summary.steady) { $summary.notes += "machine unsteady: $($route.probe) p99 median $($summary.phases[$route.probe].p99_median) ms > 300 ms" }
    }
    foreach ($ph in $summary.phases.Keys) {
      $v = $summary.phases[$ph]
      Log ("  {0}: {1} samples, {2} swaps/s median, p99 median {3} ms, max {4} ms" -f $ph, $v.samples, $v.swaps_median, $v.p99_median, $v.max_max)
    }
    Log "  errors: $($summary.error_count) lines; shots: $(@($summary.shots | Where-Object { $_.file }).Count)/$($shotTimes.Count)"
  } catch {
    $summary.notes += "run failed: $($_.Exception.Message)"
    Log "  FAILED: $($_.Exception.Message)"
    if ($game -and -not $game.HasExited) { Stop-Process -Id $game.Id -Force -ErrorAction SilentlyContinue }
  } finally {
    $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$gameOut\summary.json" -Encoding UTF8
    if (-not $KeepSandbox) { Remove-Sandbox $box }
    $runInfo.results[$id] = $(if ($summary.notes | Where-Object { $_ -like 'run failed*' }) { 'failed' } else { 'ran' })
    $game = $null
  }
}
$runInfo | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$Out\run.json" -Encoding UTF8
Log "done: $Out"
