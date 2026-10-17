# Dance Central 3's own files in a distribution (make_dist.ps1 runs this): the pose bridge, built by
# tools\pose_bridge\build_bridge.ps1, as bin\pose_bridge\ (where the game looks for it), and its
# licenses as licenses\pose_bridge\.
param([string]$Root, [string]$Dist, [string]$Release)
$ErrorActionPreference = 'Stop'
$bridge = "$Root\build\pose_bridge\dist\pose_bridge"
$licenses = "$Root\build\pose_bridge\licenses"
if (-not (Test-Path "$bridge\pose_bridge.exe")) {
  throw "The pose bridge isn't built: run tools\pose_bridge\build_bridge.ps1"
}
if (-not (Test-Path "$licenses\README.txt")) {
  throw "The pose bridge's licenses are missing: run tools\pose_bridge\build_bridge.ps1"
}
# It ships only if its self-test passes (no camera is opened).
cmd /c "`"$bridge\pose_bridge.exe`" --self-test >nul 2>&1"
if ($LASTEXITCODE -ne 0) { throw "The pose bridge's self-test failed: run $bridge\pose_bridge.exe --self-test" }
Copy-Item $bridge "$Dist\bin\pose_bridge" -Recurse
Copy-Item $licenses "$Dist\licenses\pose_bridge" -Recurse
