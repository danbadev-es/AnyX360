# Builds the pose bridge into build\pose_bridge\dist\pose_bridge\pose_bridge.exe:
#   1. a venv (build\pose_bridge\venv) with the pinned packages, installed with pip --require-hashes
#   2. the MediaPipe pose models (build\pose_bridge\models), checked against their SHA-256
#   3. PyInstaller, one folder (pose_bridge.spec)
#   4. the bundle's licenses (build\pose_bridge\licenses)
#   5. the bundle's self-test (no camera)
#
#   powershell -ExecutionPolicy Bypass -File tools\pose_bridge\build_bridge.ps1 [-Python <python.exe>]
#
# The packages' wheels are for Python 3.14 on Windows x64 (requirements.txt): -Python names one when
# the py launcher doesn't find it. Reconfigure CMake afterwards to add the bridge's tests to ctest.
param([string]$Python = "")
$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$here = "$root\tools\pose_bridge"
$out = "$root\build\pose_bridge"
$venv = "$out\venv"
$venvPython = "$venv\Scripts\python.exe"

# Pinned models: https://ai.google.dev/edge/mediapipe/solutions/vision/pose_landmarker
$models = [ordered]@{
  lite = "59929e1d1ee95287735ddd833b19cf4ac46d29bc7afddbbf6753c459690d574a"
  full = "5134a3aad27a58b93da0088d431f366da362b44e3ccfbe3462b3827a839011b1"
}
$modelUrl = "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_{0}/float16/1/pose_landmarker_{0}.task"

function Step($text) { Write-Host "== $text" }
function Check($what) { if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)" } }

New-Item -ItemType Directory -Force $out, "$out\models" | Out-Null

Step "Python"
if (-not $Python) {
  $Python = (& py -3.14 -c "import sys; print(sys.executable)" 2>$null)
  if (-not $Python) { throw "Python 3.14 not found: install it, or pass -Python <python.exe>" }
}
$version = (& $Python -I -c "import sys; print('%d.%d' % sys.version_info[:2])")
if ($version -ne "3.14") { throw "$Python is Python $version; the pinned wheels are for 3.14" }
Write-Host "   $Python"

Step "venv and packages (pip --require-hashes)"
if (-not (Test-Path $venvPython)) { & $Python -I -m venv $venv; Check "venv" }
& $venvPython -I -m pip install --disable-pip-version-check --quiet --require-hashes --only-binary=:all: -r "$here\requirements.txt"
Check "pip install"

Step "pose models"
foreach ($name in $models.Keys) {
  $file = "$out\models\pose_landmarker_$name.task"
  $expected = $models[$name]
  if ((Test-Path $file) -and (Get-FileHash $file -Algorithm SHA256).Hash.ToLower() -eq $expected) {
    Write-Host "   ${name}: present"
    continue
  }
  $part = "$file.part"
  curl.exe -sSfL -o $part ($modelUrl -f $name)
  Check "downloading the $name model"
  $actual = (Get-FileHash $part -Algorithm SHA256).Hash.ToLower()
  if ($actual -ne $expected) {
    Remove-Item $part
    throw "the $name model's SHA-256 is $actual, expected $expected"
  }
  Move-Item -Force $part $file
  Write-Host "   ${name}: downloaded, SHA-256 checked"
}

Step "PyInstaller"
$env:POSE_BRIDGE_BUILD = $out
& $venvPython -I -m PyInstaller --noconfirm --clean --log-level WARN --distpath "$out\dist" --workpath "$out\work" "$here\pose_bridge.spec"
Check "PyInstaller"
Remove-Item Env:POSE_BRIDGE_BUILD

Step "licenses"
& $venvPython -I "$here\collect_licenses.py" "$out\distributions.txt" "$out\licenses" "$out\models" "$out\dist\pose_bridge"
Check "collecting the licenses"

Step "self-test"
$exe = "$out\dist\pose_bridge\pose_bridge.exe"
& $exe --self-test
Check "the bundle's self-test"

$size = (Get-ChildItem -Recurse -File "$out\dist\pose_bridge" | Measure-Object -Sum Length).Sum
Write-Host ("Pose bridge built: {0} ({1:N0} MB)" -f $exe, ($size / 1MB))
