# Black Ops III port: progress (temporary; removed before the final report)

## Setup (done)
- Worktree junctions: thirdparty\xenia, thirdparty\XenosRecomp -> main checkout's; thirdparty\XenonRecomp ->
  build\XenonRecomp-bo3 (a clone of main's submodule repo, branch anyx360-bo3 off ef84908, with its own
  thirdparty\* junctioned to main's thirdparty\XenonRecomp\thirdparty\*). Remove all as links only at the end.
- extracted\bo3\pkg: the archive; extracted\bo3\game (god-extract, 999 files), extracted\bo3\update (TU
  tu00000005_00000000, 429 files) + default_0.0.8.5.xex / default_zm_0.0.8.5.xex (xex-patch);
  extracted\bo3\default_patched.bin / default_zm_patched.bin (xex-extract).
- Two executables: default.xex = multiplayer (cod_xenon_mp_Final.pe, BlackOps3.exe, target bo3),
  default_zm.xex = Zombies (cod_xenon_zm_Final.pe, BlackOps3ZM.exe, target bo3_zm). Recompiled directly
  from the TU (0.0.0.5 -> 0.0.8.5): games\bo3\recomp2 (MP), recomp_zm (ZM). (games\bo3\recomp is stale: the
  first MP run without vcmpgtsb.)
- Build: scratchpad bo3\run_build.ps1 "<targets>" (BelowNormal, vcvars). Configured with ANYX360_GAMES=bo3.
- Sandbox: C:\axbo3\LocalAppData (LOCALAPPDATA); TU installed in
  C:\axbo3\LocalAppData\AnyX360\BlackOps3\content\000B0000\tu00000005_00000000.
- Runs: scratchpad bo3\run.ps1 -Seconds N -Out dir [-Exe BlackOps3ZM] [-Every 10] [-Env @{ANYX360_INPUT=..}]

## Recompiler
- XenonRecomp anyx360-bo3: 6d0e2cf vcmpgtsb (Bink code). Not yet pushed to main's submodule repo.
- ERROR sites (26 in MP): SEH headers (pdata begin - 8) and funclets, the same kind BO2 MP had: harmless so far.

## Status
- (building)
