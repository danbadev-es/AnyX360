# Makes and verifies one .anyx backup per game from the user's own archives in raw\, one game at a time:
#   1. prepare: AnyX360.exe adds the archive into a sandbox release folder (its own LOCALAPPDATA);
#   2. record a SHA-256 tree of the prepared copy and its downloadable content;
#   3. export: AnyX360.exe --export <id> --to <Out>\<id>.anyx;
#   4. delete the sandbox; verify: import the .anyx into a fresh sandbox, compare the trees, check the
#      manifest and the listing (no saves, no $SystemUpdate);
#   5. smoke: start the game from the imported copy for a while (Kinect games with the scripted
#      player, never the webcam), screenshot its window by its exact title, stop it.
# The archives' password is read at run time from a note file (-PasswordNote: the line containing
# "Password for the encrypted ones", the second `quoted` value on it) and is never printed, written or
# put on a command line (the launcher reads it with --password-env). Only processes this script
# started are stopped (by PID). The last line of the report says whether the password appears in
# any file the script wrote ("password found in the report files: no"); release.ps1 checks it.
#
#   powershell -ExecutionPolicy Bypass -File tools\scripts\make_anyx.ps1 -Games dcc,fnk -Launcher <AnyX360.exe>
#     -GameBin <folder with the game exes> -Out <folder> -PasswordNote <note.md> [-Report <folder>]
#     [-TermsHash <hash>] [-Work C:\ax] [-NoSmoke]
#
# -GameBin may hold {id} (build\games\{id}\Release). 7-Zip is taken from the launcher's bin folder
# (a release's) unless -SevenZipDir says otherwise. The archives are the main checkout's raw\.
param(
  [Parameter(Mandatory)] [string]$Games,
  [Parameter(Mandatory)] [string]$Launcher,
  [Parameter(Mandatory)] [string]$GameBin,
  [Parameter(Mandatory)] [string]$Out,
  [Parameter(Mandatory)] [string]$PasswordNote,
  [string]$SevenZipDir = '',
  [string]$Report = '',
  [string]$DataRoot = '',
  [string]$Work = 'C:\ax',
  [switch]$NoSmoke,
  [string]$TermsHash = ''
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\lib.ps1"
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
# raw\ (the archives) and keys\ are git-ignored: the main checkout's, also when this runs from a worktree.
if (-not $DataRoot) { $DataRoot = $repo }
$data = $DataRoot
if (-not (Test-Path "$data\raw")) { throw "No raw folder in $data (give -DataRoot <main checkout>)" }
if (-not $SevenZipDir) { $SevenZipDir = Join-Path (Split-Path -Parent $Launcher) 'bin' }
if (-not (Test-Path "$SevenZipDir\7z.exe")) { throw "No 7z.exe in $SevenZipDir (give -SevenZipDir)" }
if (-not $Report) { $Report = Join-Path $env:TEMP 'anyx360_anyx_report' }
$report = $Report
New-Item -ItemType Directory -Force $Out, $Work, $report | Out-Null
$summary = Join-Path $report 'summary.txt'
function Log([string]$text) {
  $line = "[$(Get-Date -Format HH:mm:ss)] $text"
  Write-Output $line
  Add-Content -Path $summary -Value $line -Encoding UTF8
}

$archives = @{
  dcc = 'DCC-DLC-TU-XBLA.rar'; bo2 = 'CODBO2-DLC-TU-GOD.part1.rar'; mw3 = 'CODMW3-DLC-TU-GOD.rar';
  gow3 = 'Gears Of War 3.7z'; dc3 = 'DC3-DLC-TU-GOD.rar'; ks = 'KiSp-DLC-TU-GOD.rar';
  ka = 'KA-DLC-TU-GOD.rar'; kjr = 'KJR-DLC-TU-GOD.rar'; kp = 'KP-DLC-XBLA.rar'; fnk = 'FNK-DLC-TU-XBLA.rar';
  re6 = 'RE6-DLC-TU-GOD.part1.rar'; minecraft = 'Minecraft-X360-E-DLC-TU-XBLA.rar'; fm4 = 'FM4-DLC-GOD.part1.rar'; mk = 'MKCE-DLC-GOD.rar'; bo3 = 'CODBO3-DLC-TU-GOD.rar'; dc2 = 'DC2-DLC-TU-GOD.rar'; gh3 = 'GH3LOR-DLC-TU-GOD.rar'; d3 = 'D3ROSUEE-DLC-TU-GOD.rar'; halo3 = 'H3-DLC-TU-GOD.rar'; reach = 'HR-DLC-TU-GOD.rar'
}
$expectedDlc = @{ dcc = 1; bo2 = 8; mw3 = 26; gow3 = 0; dc3 = 20; ks = 2; ka = 1; kjr = 2; kp = 1; fnk = 8; re6 = 11; minecraft = 92; fm4 = 21; mk = 17; bo3 = 5; dc2 = 29; gh3 = 25; d3 = 1; halo3 = 5; reach = 5 }

# The password, never shown.
if (-not (Test-Path -LiteralPath $PasswordNote)) { throw "The password note is missing: $PasswordNote" }
$note = Get-Content -Raw -Encoding UTF8 -LiteralPath $PasswordNote
$line = ($note -split "`n" | Where-Object { $_ -match 'Password for the encrypted ones' } | Select-Object -First 1)
$quoted = if ($line) { [regex]::Matches($line, '`([^`]+)`') } else { @() }
if ($quoted.Count -lt 2) { throw "No password line in the note (a line with 'Password for the encrypted ones' and the password as its second value in backquotes)" }
$password = $quoted[1].Groups[1].Value
Log "password read: $($password.Length) characters"
$env:ANYX_ARCHIVE_PASSWORD = $password

function Run-Launcher([string]$Root, [string]$Local, [string]$Arguments, [string]$Result, [int]$Minutes) {
  if (Test-Path $Result) { Remove-Item $Result }
  $saved = $env:LOCALAPPDATA
  $env:LOCALAPPDATA = $Local
  try {
    $p = Start-Process -FilePath $Launcher -ArgumentList "--root `"$Root`" $Arguments --quit-when-done --result `"$Result`"" `
      -WindowStyle Minimized -PassThru
  } finally { $env:LOCALAPPDATA = $saved }
  if (-not $p.WaitForExit($Minutes * 60000)) {
    Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p.Id)" | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    return 'timeout'
  }
  if (Test-Path $Result) { return (Get-Content $Result -Raw -Encoding UTF8).Trim() }
  return 'no result'
}

# A sandbox release folder: 7-Zip and the key, the user's acceptance of the terms.
function New-Sandbox([string]$Dir) {
  if (Test-Path $Dir) { cmd /c rd /s /q "$Dir" | Out-Null }
  New-Item -ItemType Directory -Force "$Dir\root\bin", "$Dir\root\keys", "$Dir\local\AnyX360" | Out-Null
  Copy-Item "$SevenZipDir\7z.exe", "$SevenZipDir\7z.dll" "$Dir\root\bin\"
  Copy-Item "$data\keys\xex2_retail.key" "$Dir\root\keys\"
  # The songs' keys of the titles that need them (Dance Central 2 and 3), as a release has them.
  if (Test-Path "$data\keys\title_keys.txt") { Copy-Item "$data\keys\title_keys.txt" "$Dir\root\keys\" }
  # The terms accepted for this release (their hash, -TermsHash), else the user's acceptance.
  if ($TermsHash) { [IO.File]::WriteAllText("$Dir\local\AnyX360\terms_accepted.txt", $TermsHash) }
  else { Copy-Item "$env:LOCALAPPDATA\AnyX360\terms_accepted.txt" "$Dir\local\AnyX360\" }
}

# Relative path | size | SHA-256 of every file the backup takes ($SystemUpdate and junk left out).
function Tree([string]$Folder, [string]$Prefix) {
  if (-not (Test-Path -LiteralPath $Folder)) { return @() }
  $base = (Resolve-Path -LiteralPath $Folder).Path
  Get-ChildItem -LiteralPath $base -Recurse -File -Force | Where-Object {
    $rel = $_.FullName.Substring($base.Length + 1)
    -not ($rel -like '$SystemUpdate\*') -and ($_.Name -notin @('Thumbs.db', 'desktop.ini', '.DS_Store'))
  } | ForEach-Object {
    $rel = $_.FullName.Substring($base.Length + 1).Replace('\', '/')
    "$Prefix/$rel|$($_.Length)|$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower())"
  } | Sort-Object
}

function Full-Tree([string]$Dir, $Info) {
  $tree = @(Tree "$Dir\root\games\$($Info.id)\game" 'game')
  $dlc = "$Dir\local\AnyX360\$($Info.user_dir)\content\00000002"
  if (Test-Path $dlc) {
    foreach ($pack in Get-ChildItem -LiteralPath $dlc -Directory) { $tree += @(Tree $pack.FullName "content/$($pack.Name)") }
  }
  # The title update the game is played with (.anyx format version 2).
  $tu = "$Dir\local\AnyX360\$($Info.user_dir)\content\000B0000"
  if (Test-Path $tu) {
    foreach ($pack in Get-ChildItem -LiteralPath $tu -Directory) { $tree += @(Tree $pack.FullName "update/$($pack.Name)") }
  }
  return $tree | Sort-Object
}

try {
  foreach ($id in ($Games -split ',')) {
    $info = Get-Content -Raw -Encoding UTF8 "$repo\games\$id\game.json" | ConvertFrom-Json
    $archive = Join-Path "$data\raw" $archives[$id]
    $anyx = Join-Path $Out "$id.anyx"
    $started = Get-Date
    Log "== $id ($($info.title)) from $($archives[$id])"

    # 1. Prepared from the archive.
    $sandbox = Join-Path $Work $id
    New-Sandbox $sandbox
    $minutes = 20
    if ($id -in @('bo2', 'mw3', 'gow3', 're6', 'fm4', 'mk', 'bo3', 'dc3', 'dc2', 'gh3', 'd3', 'halo3', 'reach')) { $minutes = 90 }
    $prep = Run-Launcher "$sandbox\root" "$sandbox\local" "--archive `"$archive`" --password-env ANYX_ARCHIVE_PASSWORD" "$sandbox\prep.txt" $minutes
    Log "prepare: $prep"
    if (-not $prep.StartsWith('ok:')) { Log "FAIL $id prepare"; continue }
    $packs = @(Get-ChildItem -LiteralPath "$sandbox\local\AnyX360\$($info.user_dir)\content\00000002" -Directory -ErrorAction SilentlyContinue).Count
    Log "downloadable content packs: $packs (expected $($expectedDlc[$id]))"

    # 2. The tree of what the backup should hold.
    $tree = Full-Tree $sandbox $info
    $tree | Set-Content -Encoding UTF8 (Join-Path $report "$id.tree.txt")
    Log "files: $($tree.Count)"

    # 3. Exported.
    if (Test-Path $anyx) { Remove-Item $anyx }
    $export = Run-Launcher "$sandbox\root" "$sandbox\local" "--export $id --to `"$anyx`"" "$sandbox\export.txt" 60
    Log "export: $export"
    if (-not $export.StartsWith('ok:') -or -not (Test-Path $anyx) -or (Test-Path "$anyx.partial")) { Log "FAIL $id export"; continue }
    $size = (Get-Item $anyx).Length
    cmd /c rd /s /q "$sandbox" | Out-Null
    if (Test-Path $sandbox) { Log "WARN $id sandbox not fully removed" }

    # 4. Verified: imported into a fresh sandbox.
    $verify = Join-Path $Work "v-$id"
    New-Sandbox $verify
    $import = Run-Launcher "$verify\root" "$verify\local" "--archive `"$anyx`"" "$verify\import.txt" 60
    Log "import: $import"
    $after = Full-Tree $verify $info
    $differences = @(Compare-Object -ReferenceObject $tree -DifferenceObject $after)
    Log "tree after import: $($after.Count) files, $($differences.Count) differences"
    $listing = & "$SevenZipDir\7z.exe" l -slt "$anyx" | Select-String '^Path = ' | ForEach-Object { $_.Line.Substring(7) }
    $forbidden = @($listing | Where-Object { $_ -match '(^|[\\/])00000001([\\/]|$)|\$SystemUpdate|Thumbs\.db' }).Count
    $manifest = (& "$SevenZipDir\7z.exe" e -so "$anyx" manifest.json | Out-String) | ConvertFrom-Json
    $manifestOk = ($manifest.format -eq 'anyx') -and ($manifest.game.id -eq $id) -and ($manifest.game.version -eq $info.xex.version) -and
                  ($manifest.game.media_id -eq $info.xex.media_id) -and (@($manifest.content).Count -eq $expectedDlc[$id])
    # A game played with its title update carries it (format version 2, "update" key).
    if ($info.update) {
      $manifestOk = $manifestOk -and ($manifest.format_version -eq 2) -and ($manifest.update.package -eq $info.update.package) -and
                    ($manifest.update.version -eq $info.update.version)
    }
    Log "manifest: format $($manifest.format_version), version $($manifest.game.version), update $($manifest.update.version), media $($manifest.game.media_id), $(@($manifest.content).Count) packs, $(@($manifest.files).Count) files, ok=$manifestOk; forbidden entries: $forbidden"

    # 5. Smoke: the game from the imported copy.
    $smoke = 'skipped'
    if (-not $NoSmoke -and $import.StartsWith('ok:')) {
      $exe = Join-Path $GameBin.Replace('{id}', $id) "$($info.exe).exe"
      $shots = Join-Path $report "smoke-$id"
      New-Item -ItemType Directory -Force $shots | Out-Null
      # Avatar games without their own Avatar data (Doritos) use Kinect Sports': as if it were added too.
      $toc = "$env:LOCALAPPDATA\AnyX360\system\AvatarAssetPack.toc"
      if (-not (Test-Path "$verify\local\AnyX360\system\AvatarAssetPack.toc") -and (Test-Path $toc)) {
        New-Item -ItemType Directory -Force "$verify\local\AnyX360\system" | Out-Null
        Copy-Item $toc "$verify\local\AnyX360\system\"
      }
      $saved = $env:LOCALAPPDATA
      $env:LOCALAPPDATA = "$verify\local"
      if ($info.input -eq 'webcam') {
        Set-Content -Path "$shots\script.txt" -Value '3:enter' -Encoding ascii
        $env:ANYX360_NUI_SCRIPT = "@$shots\script.txt"
      }
      try {
        $game = Start-Process -FilePath $exe -PassThru -RedirectStandardOutput "$shots\out.log" -RedirectStandardError "$shots\err.log" `
          -ArgumentList "--game `"$verify\root\games\$id\game`" --user `"$verify\local\AnyX360\$($info.user_dir)`" --key `"$data\keys\xex2_retail.key`""
      } finally {
        $env:LOCALAPPDATA = $saved
        Remove-Item Env:ANYX360_NUI_SCRIPT -ErrorAction SilentlyContinue
      }
      Start-Sleep -Seconds 75
      if ($game.HasExited) {
        $smoke = "exited ($($game.ExitCode))"
      } else {
        $title = (Get-Process -Id $game.Id).MainWindowTitle
        $null = Save-GameWindow $title "$shots\75s.png" @([uint32]$game.Id)
        $smoke = "running, window '$title', screenshot $(Test-Path "$shots\75s.png")"
        Stop-Process -Id $game.Id -Force
        $game.WaitForExit(20000) | Out-Null
      }
      $errors = @(Select-String -Path "$shots\err.log", "$shots\out.log" -Pattern '=== CRASH|unhandled exception|fatal error' -CaseSensitive:$false -ErrorAction SilentlyContinue).Count
      $mounts = @(Select-String -Path "$shots\err.log", "$shots\out.log" -Pattern 'content' -ErrorAction SilentlyContinue).Count
      $smoke += ", error lines $errors, content lines $mounts"
    }
    Log "smoke: $smoke"
    cmd /c rd /s /q "$verify" | Out-Null

    $ok = $import.StartsWith('ok:') -and $differences.Count -eq 0 -and $manifestOk -and $forbidden -eq 0 -and $packs -eq $expectedDlc[$id]
    $verdict = 'FAIL'
    if ($ok) { $verdict = 'PASS' }
    Log ("{0} {1}: {2:N2} GB, {3} packs, version {4}, {5:N0} min" -f $verdict, $id, ($size / 1GB), $packs, $manifest.game.version, ((Get-Date) - $started).TotalMinutes)
  }
} finally {
  Remove-Item Env:ANYX_ARCHIVE_PASSWORD -ErrorAction SilentlyContinue
  # Nothing the script wrote holds the password.
  $leak = @(Get-ChildItem -LiteralPath $report -Recurse -File | Select-String -SimpleMatch $password -ErrorAction SilentlyContinue).Count
  Log "password found in the report files: $(if ($leak) { 'YES' } else { 'no' })"
}
