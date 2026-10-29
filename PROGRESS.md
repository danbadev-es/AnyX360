# Forza Motorsport 4: where the port stands (branch fm4)

Remove this file (and games/fm4/src/probe.cpp, and its line in games/fm4/CMakeLists.txt) when the
port is finished. Status and technical notes: docs/FORZAMOTORSPORT4.md.

## Reached
Logos, title ("Press A to start"), profile save, brightness, Top Gear intro video, then the first
race's loading screen (ForzaUI::IELoading, "Bienvenido a Forza Motorsport 4", Bernese Alps), where
it stays forever. Nothing is read after the Alps audio zones; the track's `bin.zip` and the car
(`Media\cars\fer_f142_10.zip`, the 458 Italia) are never opened; no export is stuck.

## Findings (2026-10-29, after the rebase onto 0.24.1)
- The 21 table-driven `bctr` the analyser leaves (scratchpad `bctr.py`) are all tail calls through
  member-function-pointer tables (`this` adjusted, `lwzx` from a table, `bctr`): correct as indirect
  calls. Ruled out.
- `pow` dominating thread 17: the loading screen's colour fade, `822A7308` (start time at
  `this+3792`, duration 2 s at `+3800`, timer `8236DA38`) → `8243F330` (gamma, `pow`). The fade
  completes (now − start ≫ 2 s); it just keeps being recomputed every frame. Not the blocker.
- **The blocker so far**: `IELoading::Update` = `825482D0` (`this` = 40D0FD80 in the probe run).
  It holds a pending UI object at `this+352` (3E3E8990) and asks `826460F8(obj)` each frame; that
  returns 1 (busy) forever, so the screen never notifies (virtual +64, message 94) nor clears
  +352. `826460F8` calls the UI object at `obj+120` (`8299CB18`, with the name pointer stored at
  `[0x82DDF1D8]`, set at run time) about `obj+132`, then a manager's virtual +200 and `824AB458`
  (to bool): most likely a UI script/animation query ("is the clip still playing"). +352 is a
  common field of every UI screen (stores at 824C1F94, 824EDEF0... `stw r3,352(r31)`).
- Earlier ruled out: DLC (same without it), Kinect (same with/without the sensor), button presses,
  slowness (9 minutes), unimplemented imports.

## Findings (2026-10-29, second session)
- The p3 probe crashed (it read the name pointer as a string); probe.cpp now reads guest memory
  safely.
- `826460F8`'s query is about **`GAME:\Media\UI\Videos\IE_Welcome_Video.wmv`**: a list of video
  paths with a "playing" flag (entries of 32 bytes: a string, the flag at +28), kept by a listener
  of event 13 (`824ABBE8`: 13/0 a video started, 13/1 it ended). `CWMVMovie` (vtable 82159DBC,
  update = slot 7 `8298FB40`, started = slot 9 `8298FE60`) posts 13/1 when the facade's next-frame
  call (`82985E78` → XMediaFacade export 6) returns `0x16660026` (end of stream; `0x166600E8` is
  "not ready yet").
- IELoading (the screen behind the welcome video) waits for that flag, and the end does come, but
  late and at a varying time (145 s to 350 s into the run): the video decodes slower than real
  time, the XMedia workers (threads starting at `881EE7A0`) being created and ended per frame.
  Right after the end the race load starts: the NUI thread (`827C1338`), the worker `82A68968`,
  CareerRaceModes.xml, the job thread `82CE1700`, the Alps audio zones. After that, nothing
  more is opened (being checked with a ten-minute run).

## Next step (exact)
1. Probe `826460F8` again to print the queried name (`[0x82DDF1D8]` string) and `obj+132`
   (probe.cpp already does it; the last run p3 printed nothing: check its build log first,
   `scratchpad\fm4\build9.log`).
2. Find what makes that UI query true: the UI animation/clip system (8299CB18, 824AB458) — e.g. a
   clip driven by a video (`Media\UI\videos\LoadingAnim*.wmv` never opened), or a timeline that
   needs an event. Also look at `CContentInstaller` (`823CD428` starts `CContentInstaller::
   InstallThreadFunc` = `823C7618` through the CThread helpers 82943F10/829434E8; never started in
   these runs) and the DB functions `IsCarInstalled`, `IsEnvironmentOnDisc`, `IsCarOnDisc`.
3. Then the single-player list: career/World Tour with saving, Free Play, garage/upgrades/tuning,
   paint editor, storefront, replays, Rivals offline, disc-2 cars, DLC.

## Setting up again
- Junctions: `thirdparty\xenia` and `thirdparty\XenosRecomp` → the main checkout's; and
  `thirdparty\XenonRecomp` → `build\XenonRecomp-fm4` (a clone at branch `anyx360-fm4`, ef84908,
  also in the main checkout's submodule repository), whose `thirdparty\{fmt,libmspack,simde,
  tiny-AES-c,tomlplusplus,xxHash}` must be junctions to the main checkout's
  `thirdparty\XenonRecomp\thirdparty\*` (removed at the end of the session: recreate them).
- Game data: `extracted\fm4\game` (god-extract) and `extracted\fm4\default.bin` (xex-extract with
  the retail key) are in this worktree; the sandbox `C:\axfm4\user` holds the 21 content packages
  (`content\00000002\<pkg>` + `content\Headers\00000002\<pkg>.header`) and `settings.toml`
  (`[players] p1 = { device = "pad" }`). Delete both when done.
- Scripts in the session scratchpad (`...\scratchpad\fm4\`): `build.cmd`/`lowrun.ps1` (BelowNormal
  builds, -DANYX360_GAMES=fm4), `run.ps1` (`-Seconds -Keys -Name -CaptureMs -Extra @{...}`,
  frames via ANYX360_CAPTURE_DIR, stops only its own PID), `sheet.py` (contact sheets),
  `xref.py`, `bctr.py`, `prepare.ps1` (re-extraction).
- Route: fresh `C:\axfm4\user\content\00000001`, `ANYX360_INPUT=28:A,40:A,60:A~15x6`; loading
  screen at about 125 s.
- test_launcher_core passes again (FM4's required files moved to the copy's root: "Media/UI.zip"
  collided in case with the test's "media/level.bin" when FM4 was the catalog's only game).
