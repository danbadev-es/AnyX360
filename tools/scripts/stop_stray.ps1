# Lists, and with -Stop stops, the processes whose executable or command line mentions a folder, and
# their descendants: shells and games an agent left behind that keep a worktree (or a sandbox)
# locked. A process's current folder can't be read from outside, so a shell that only cd'ed there
# (its command line doesn't name it) isn't found: give agents' shells absolute paths.
#
#   powershell -ExecutionPolicy Bypass -File tools\scripts\stop_stray.ps1 -Path <folder> [-Stop] [-Exclude <pid>,...]
#
# The folder is matched as a path prefix, case-insensitively, in its long form, its 8.3 short form
# (C:\Users\DANIEL~1\...) and with forward slashes (Git Bash: /c/Users/...), so a bash started in it is
# found too. This process and its parents (the shell running it) are never listed or stopped, nor
# the -Exclude IDs. Without -Stop nothing is touched. Exit code: 0 when nothing (more) matches.
param(
  [Parameter(Mandatory)] [string]$Path,
  [switch]$Stop,
  [int[]]$Exclude = @()
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Path)) { throw "No such folder: $Path" }
$full = (Resolve-Path -LiteralPath $Path).Path.TrimEnd('\')
if ($full.Length -le 3) { throw "Refusing a drive root ($full): give a project folder" }

# The spellings of the folder a command line can carry.
$forms = [System.Collections.Generic.List[string]]::new()
$forms.Add($full)
$fso = New-Object -ComObject Scripting.FileSystemObject
try { $short = $fso.GetFolder($full).ShortPath; if ($short -and $short -ne $full) { $forms.Add($short.TrimEnd('\')) } } catch { }
foreach ($f in @($forms)) {
  $forms.Add($f.Replace('\', '/'))
  # Git Bash / MSYS: /c/Users/...
  if ($f -match '^([A-Za-z]):\\(.*)$') { $forms.Add('/' + $Matches[1].ToLower() + '/' + $Matches[2].Replace('\', '/')) }
}
$forms = @($forms | Select-Object -Unique)

# This script's process and its ancestors: never touched.
$all = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, Name, ExecutablePath, CommandLine, CreationDate)
$byId = @{}
foreach ($p in $all) { $byId[[int]$p.ProcessId] = $p }
$protected = [System.Collections.Generic.HashSet[int]]::new()
$id = $PID
while ($id -and $byId.ContainsKey($id) -and $protected.Add($id)) { $id = [int]$byId[$id].ParentProcessId }
foreach ($e in $Exclude) { [void]$protected.Add($e) }

function Mentions([string]$text) {
  if (-not $text) { return $false }
  foreach ($f in $forms) {
    $i = $text.IndexOf($f, [StringComparison]::OrdinalIgnoreCase)
    while ($i -ge 0) {
      # A path prefix: the folder itself or something under it, not C:\a\worktree2 for C:\a\worktree.
      $next = $i + $f.Length
      if ($next -ge $text.Length -or '\/"'' '.Contains($text[$next])) { return $true }
      $i = $text.IndexOf($f, $next, [StringComparison]::OrdinalIgnoreCase)
    }
  }
  return $false
}

$found = @($all | Where-Object { -not $protected.Contains([int]$_.ProcessId) -and ((Mentions $_.ExecutablePath) -or (Mentions $_.CommandLine)) })
# And their descendants (a shell's running command, a game's second executable).
$ids = [System.Collections.Generic.HashSet[int]]::new()
foreach ($p in $found) { [void]$ids.Add([int]$p.ProcessId) }
do {
  $added = $false
  foreach ($p in $all) {
    if ($ids.Contains([int]$p.ParentProcessId) -and -not $protected.Contains([int]$p.ProcessId) -and $ids.Add([int]$p.ProcessId)) {
      $found += $p; $added = $true
    }
  }
} while ($added)
if (-not $found) { "No process mentions $full"; exit 0 }
foreach ($p in $found) {
  $line = $p.CommandLine
  if ($line -and $line.Length -gt 160) { $line = $line.Substring(0, 157) + '...' }
  "{0,7} {1,-24} started {2:yyyy-MM-dd HH:mm}  {3}" -f $p.ProcessId, $p.Name, $p.CreationDate, $line
}
if (-not $Stop) { "$($found.Count) process(es) mention $full; run again with -Stop to stop them"; exit 1 }
$left = 0
foreach ($p in $found) {
  try { Stop-Process -Id $p.ProcessId -Force -ErrorAction Stop; "stopped $($p.ProcessId) $($p.Name)" }
  catch { if (Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue) { "could not stop $($p.ProcessId) $($p.Name): $($_.Exception.Message)"; $left++ } }
}
exit $(if ($left) { 1 } else { 0 })
