# Stores a regression run as the baseline: <Run>\<id>\summary.json -> tools\regress\baseline\<id>.json,
# with a note (the release, the machine's state) and machine_unsteady when any game of the run had its
# probe menu above 300 ms p99 (then compare.ps1 counts no performance flag against it). Screenshots
# aren't stored (they are game images): the baseline keeps their times and brightness only.
#
#   powershell -ExecutionPolicy Bypass -File tools\regress\set_baseline.ps1 -Run <Out> [-Games gow3,mk] [-Note "..."]
param(
  [Parameter(Mandatory)] [string]$Run,
  [string]$Games = 'all',
  [string]$Note = '',
  [string]$Baseline = "$PSScriptRoot\baseline"
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Baseline | Out-Null
$all = @(Get-ChildItem -LiteralPath $Run -Directory | Where-Object { Test-Path "$($_.FullName)\summary.json" })
$unsteady = @($all | Where-Object { (Get-Content -Raw "$($_.FullName)\summary.json" | ConvertFrom-Json).steady -eq $false }).Count -gt 0
foreach ($dir in $all) {
  if ($Games -ne 'all' -and ($Games -split ',') -notcontains $dir.Name) { continue }
  $s = Get-Content -Raw -Encoding UTF8 "$($dir.FullName)\summary.json" | ConvertFrom-Json
  if (@($s.notes | Where-Object { $_ -like 'run failed*' })) { "$($dir.Name): the run failed, not stored"; continue }
  $s | Add-Member -Force NoteProperty machine_unsteady $unsteady
  $s | Add-Member -Force NoteProperty baseline_note $Note
  $s | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Baseline "$($dir.Name).json") -Encoding UTF8
  "$($dir.Name): stored$(if ($unsteady) { ' (machine unsteady)' })"
}
