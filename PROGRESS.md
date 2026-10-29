# Mortal Kombat Komplete Edition port: progress (remove before the final report)

Branch: worktree-agent-a1973fa1f7628ed93 (worktree .claude/worktrees/agent-a1973fa1f7628ed93), off main 9bef313.

## Setup done
- Junctions thirdparty\xenia, XenonRecomp, XenosRecomp -> main checkout's (remove as links only at the end:
  `[System.IO.Directory]::Delete(path, $false)`, then `git checkout -- thirdparty`). `git status` needs
  `--ignore-submodules=all` while they exist.
- `extracted\mk\pkg\575207FD\{00007000,00000002}` from raw\MKCE-DLC-GOD.rar; `extracted\mk\game` (god-extract,
  1263 files); `extracted\mk\default.bin` (xex-extract, main's keys\xex2_retail.key).
- Build: `build\` configured with `-DANYX360_GAMES=mk`. Scratchpad helpers in
  `<scratchpad>\mk\`: bld.bat (vcvars + cmake --build), bldw.ps1 (below-normal priority, waits),
  run.ps1 (sandbox run + screenshots), install_dlc.py (DLC into the sandbox), dis.py (capstone
  disassembly, pylib\), pdata.py, classify.py, xref.py, errors.py.
- Sandbox: LOCALAPPDATA=C:\axmk -> user dir C:\axmk\AnyX360\MortalKombat (settings.toml: p1 pad, p2 pad2;
  17 DLC installed in content\00000002 with Headers).

## Facts
- default.xex = MK9Game-Retail.exe 0.0.0.8, media 6579DAFA, XDK 2.0.20353, VCOMP (OpenMP) linked.
  Movies are WMV (XMCORE statically linked; no title DLLs). No title update in the copy.
- setjmp 82FE1340 / longjmp 82FE1020 (CRT, same as MW3/BO2/FM4). 389 jump tables, 24 table bctr left.
- Recompiler ERRORs: all but two are junk functions made from the 8-byte exception data before
  functions with handlers (nothing calls them). Real: 82CF05B0 (HRESULT map, bdzf; given by hand,
  size 0x25C) and SwitchToFiber 82BDC380 (b to asm 82BDCC80). Fiber functions:
  ConvertThreadToFiber 82BDC240, ConvertFiberToThread 82BDC2D0, DeleteFiber 82BDC320; no CreateFiber
  found. User: 82AFAB28 (latent task list at 834246BC, ticked by 829D06C8). games/mk/src/fibers.cpp
  written (FM4 style) but NOT yet in CMakeLists.

## DLC (17 packages, content type 2)
Skarlet, Kenshi, Rain, Freddy Krueger (fighters), Kombat Pass (OnlinePass ini), Kombat Pass + All
Character Costumes, 7 Klassic costume keys (ini only: Mileena, Sub-Zero, Jade, Scorpion, Reptile,
Ermac, Kitana), and Goro, Kintaro, Shao Kahn, "GORO TEST" (playable-boss mods, not official: ini
`IsDLC=goro`, DownloadContentID 6001). Komplete disc already has CHAR_FreddyKrueger etc.

## Status / next
- [x] xex-info, xex-extract, find_helpers, XenonAnalyse, XenonRecomp (games/mk/recomp)
- [ ] build, first boot
