# Compares a regression run (run_regress.ps1's <Out>\<id>\summary.json) with the stored baseline
# (tools\regress\baseline\<id>.json) and writes a report (Markdown, and the same as HTML) flagging:
#   - a phase whose median frame rate fell by more than -FpsDrop percent;
#   - a phase whose median p99 frame interval rose by more than -P99Rise percent and -P99RiseMs ms;
#   - a screenshot the baseline has and the run hasn't (or one that came out black);
#   - error lines the baseline doesn't have (numbers masked), a crash, a game that exited by itself;
#   - a different number of DLC packs, an import that failed.
# A run or a baseline made while the machine was unsteady (Mortal Kombat's or Gears' menus with a p99
# above 300 ms: the machine paused every game ~430 ms once a second on 2026-11-01 03-06 h) has its
# performance flags shown as warnings only.
#
#   powershell -ExecutionPolicy Bypass -File tools\regress\compare.ps1 -Run <Out> [-Baseline tools\regress\baseline]
#     [-Report <Out>\report.md] [-FpsDrop 10] [-P99Rise 50] [-P99RiseMs 15]
#
# Exit code 1 when anything is flagged (warnings don't count).
param(
  [Parameter(Mandatory)] [string]$Run,
  [string]$Baseline = '',
  [string]$Report = '',
  [double]$FpsDrop = 10,
  [double]$P99Rise = 50,
  [double]$P99RiseMs = 15
)
$ErrorActionPreference = 'Stop'
# Numbers with a decimal point whatever the system's language.
[Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::InvariantCulture
if (-not $Baseline) { $Baseline = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'baseline' }
if (-not $Report) { $Report = Join-Path $Run 'report.md' }
$runs = @(Get-ChildItem -LiteralPath $Run -Directory | Where-Object { Test-Path "$($_.FullName)\summary.json" })
if (-not $runs) { throw "No <id>\summary.json in $Run" }

function Load([string]$path) { Get-Content -Raw -Encoding UTF8 -LiteralPath $path | ConvertFrom-Json }
function Unsteady($s) { $s -and $s.steady -eq $false }
function Num($v, [string]$format = 'N1') { if ($null -eq $v) { '-' } else { ([double]$v).ToString($format) } }

$rows = @(); $details = @(); $flagged = 0; $runUnsteady = $false
foreach ($dir in $runs | Sort-Object Name) {
  $id = $dir.Name
  $s = Load "$($dir.FullName)\summary.json"
  if (Unsteady $s) { $runUnsteady = $true }
}
foreach ($dir in $runs | Sort-Object Name) {
  $id = $dir.Name
  $s = Load "$($dir.FullName)\summary.json"
  $bPath = Join-Path $Baseline "$id.json"
  $b = if (Test-Path $bPath) { Load $bPath } else { $null }
  $flags = @(); $warnings = @()
  $perfUnsteady = $runUnsteady -or ($b -and $b.machine_unsteady)
  function Perf([string]$text) { if ($perfUnsteady) { $script:warnings += "$text (machine unsteady: not counted)" } else { $script:flags += $text } }

  if (@($s.notes | Where-Object { $_ -like 'run failed*' })) { $flags += ($s.notes | Where-Object { $_ -like 'run failed*' }) }
  if ($s.exit -and $s.exit.state -eq 'exited') { $flags += "the game exited by itself at $($s.exit.at) s (code $($s.exit.code))" }
  if (-not $b) { $warnings += 'no baseline' }
  else {
    # A loaded machine on either side makes the frame rates a lower bound: said next to the flags.
    foreach ($side in @(@('baseline', $b.machine_before), @('this run', $s.machine_before))) {
      $m = $side[1]
      if ($m -and (@($m.other_games).Count -or $m.cpu_load -gt 80)) {
        $warnings += "$($side[0]) ran on a loaded machine (CPU $($m.cpu_load)%, other games: $(if (@($m.other_games).Count) { $m.other_games -join ', ' } else { 'none' }))"
      }
    }
    if ($null -ne $b.dlc -and $s.dlc -ne $b.dlc) { $flags += "DLC packs: $($s.dlc), baseline $($b.dlc)" }
    foreach ($p in $b.phases.PSObject.Properties) {
      $bp = $p.Value; $rp = $s.phases.($p.Name)
      if (-not $rp -or -not $rp.samples) { if ($bp.samples) { $flags += "phase $($p.Name): no samples (baseline $($bp.samples))" }; continue }
      if ($bp.swaps_median -and $rp.swaps_median -lt $bp.swaps_median * (1 - $FpsDrop / 100)) {
        Perf ("phase {0}: {1:N1} frames/s, baseline {2:N1} (-{3:N0}%)" -f $p.Name, $rp.swaps_median, $bp.swaps_median, (100 * (1 - $rp.swaps_median / $bp.swaps_median)))
      }
      if ($bp.p99_median -and $rp.p99_median -gt $bp.p99_median * (1 + $P99Rise / 100) -and $rp.p99_median - $bp.p99_median -gt $P99RiseMs) {
        Perf ("phase {0}: p99 {1:N1} ms, baseline {2:N1} ms" -f $p.Name, $rp.p99_median, $bp.p99_median)
      }
    }
    foreach ($shot in @($b.shots | Where-Object { $_.file })) {
      $mine = @($s.shots | Where-Object { $_.t -eq $shot.t }) | Select-Object -First 1
      if (-not $mine -or -not $mine.file) { $flags += "screenshot at $($shot.t) s missing" }
      elseif ($null -ne $mine.lit -and $mine.lit -lt 0.02 -and $shot.lit -ge 0.02) { $flags += "screenshot at $($shot.t) s is black (baseline wasn't)" }
    }
    $known = @($b.errors)
    $new = @($s.errors | Where-Object { $known -notcontains $_ })
    if ($new) { $flags += "new error lines ($($new.Count)): " + (($new | Select-Object -First 3) -join ' | ') }
  }
  if (@($s.errors | Where-Object { $_ -match '=== CRASH' })) { $flags += 'a crash in the log' }

  $flagged += $flags.Count
  $main = @($s.phases.PSObject.Properties)
  $cells = foreach ($p in $main) {
    $bp = if ($b) { $b.phases.($p.Name) } else { $null }
    '{0}: {1} ({2}) fps, p99 {3} ({4}) ms' -f $p.Name, (Num $p.Value.swaps_median), (Num $bp.swaps_median), (Num $p.Value.p99_median), (Num $bp.p99_median)
  }
  $verdict = if ($flags) { 'FLAGGED' } elseif ($warnings) { 'warn' } else { 'ok' }
  $rows += "| $id | $verdict | $($cells -join '<br>') | $($s.dlc) ($(if ($b) { $b.dlc } else { '-' })) | $($s.error_count) | $(@($s.shots | Where-Object { $_.file }).Count)/$(@($s.shots).Count) |"
  $details += "### $id - $($s.title)"
  $details += ''
  foreach ($f in $flags) { $details += "- **$f**" }
  foreach ($w in $warnings) { $details += "- $w" }
  foreach ($n in @($s.notes)) { $details += "- note: $n" }
  if ($s.machine_before) {
    $m = $s.machine_before
    $details += "- machine before: CPU $($m.cpu_load)%, idle $($m.idle_seconds) s, other games: $(if ($m.other_games) { $m.other_games -join ', ' } else { 'none' }); busy: $($m.busy_processes -join ', ')"
  }
  $shots = @($s.shots | Where-Object { $_.file } | ForEach-Object { "![$id $($_.t) s]($id/$($_.file))" })
  if ($shots) { $details += ''; $details += ($shots -join ' ') }
  $details += ''
}

$head = @(
  "# Regression report",
  '',
  "Run: $Run  ",
  "Baseline: $Baseline  ",
  "Thresholds: frame rate -$FpsDrop%, p99 +$P99Rise% and +$P99RiseMs ms. Values: run (baseline).",
  ''
)
if ($runUnsteady) { $head += '**Machine unsteady during this run** (a probe menu had p99 > 300 ms): performance differences are warnings only.'; $head += '' }
$head += "**$flagged flag(s).**"
$head += ''
$head += '| Game | Verdict | Phases | DLC | Error lines | Shots |'
$head += '|---|---|---|---|---|---|'
$md = ($head + $rows + @('', '## Details', '') + $details) -join "`n"
[IO.File]::WriteAllText($Report, $md, [Text.UTF8Encoding]::new($false))

# The same as a plain HTML page (screenshots as thumbnails).
$enc = { param($t) [Net.WebUtility]::HtmlEncode($t) }
$html = @('<!DOCTYPE html><html><head><meta charset="utf-8"><title>Regression report</title>',
  '<style>body{font-family:sans-serif;margin:2em}table{border-collapse:collapse}td,th{border:1px solid #bbb;padding:4px 8px;vertical-align:top}img{width:240px;margin:2px}.f{color:#b00;font-weight:bold}</style></head><body>')
foreach ($line in $md -split "`n") {
  if ($line -match '^# (.*)') { $html += "<h1>$(& $enc $Matches[1])</h1>" }
  elseif ($line -match '^## (.*)') { $html += "<h2>$(& $enc $Matches[1])</h2>" }
  elseif ($line -match '^### (.*)') { $html += "<h3>$(& $enc $Matches[1])</h3>" }
  elseif ($line -match '^\|---') { }
  elseif ($line -match '^\| Game') { $html += '<table><tr><th>Game</th><th>Verdict</th><th>Phases</th><th>DLC</th><th>Error lines</th><th>Shots</th></tr>' }
  elseif ($line -match '^\| ') {
    $c = $line.Trim('|').Split('|') | ForEach-Object { (& $enc $_.Trim()).Replace('&lt;br&gt;', '<br>') }
    $html += "<tr>$(($c | ForEach-Object { "<td>$_</td>" }) -join '')</tr>"
    if ($line -eq $rows[-1]) { $html += '</table>' }
  }
  elseif ($line -match '^- \*\*(.*)\*\*$') { $html += "<div class=f>$(& $enc $Matches[1])</div>" }
  elseif ($line -match '^- (.*)') { $html += "<div>$(& $enc $Matches[1])</div>" }
  elseif ($line -match '^!\[') { $html += ([regex]::Matches($line, '!\[[^\]]*\]\(([^)]+)\)') | ForEach-Object { "<a href=`"$($_.Groups[1].Value)`"><img src=`"$($_.Groups[1].Value)`"></a>" }) -join '' }
  elseif ($line.Trim()) { $html += "<p>$(& $enc ($line.Replace('**', '')))</p>" }
}
$html += '</body></html>'
[IO.File]::WriteAllText([IO.Path]::ChangeExtension($Report, '.html'), ($html -join "`n"), [Text.UTF8Encoding]::new($false))
"report: $Report ($flagged flag(s))"
exit $(if ($flagged) { 1 } else { 0 })
