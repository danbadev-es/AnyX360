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
  CareerRaceModes.xml, the job thread `82CE1700`, the Alps audio zones. After that nothing more
  is opened, allocated or queried for ten minutes (LOG_FILES + ANYX360_TRACE): neither the
  track's `bin.zip` nor the car's zip (`media\cars\fer_f142_10.zip`, the 458) is requested.
- Ruled out in this session: the main loop's mode flags (`822DADD8`'s object 40053530: +2080,
  +2812..2817, +2844, +3050, +3973, +3975, +4144 don't change after the race load starts); the
  job worker `82CE1700` (a timer service, idle); thread 22 (`828B96F0`, audio, 32 ms tick); Xenia
  GPU warnings (none, with `[xenia] log_file`); physical allocation failures (none logged).
- The main thread (17) spins at ~99 % with or without the native pow: its loop (`822DADD8`)
  recomputes the gamma ramp (`8243F330` → `8287AE48`, D3D SetGammaRamp `826E6750`) every
  iteration and yields (SwitchToThread). Not the blocker, but wasteful.
- The game reads dev parameters (`disablesplashintro`, `skiptutorials`, `track`, `launch`,
  `exhibition`, `pausephysicsonstreaming`, `streamsize`...; table near 0x820120E8) through
  829490B8/82949198; `loadcmdlinedottxt` names a file (default `GAME:\cmdline.txt`) — a way to
  launch a race directly and test the race loader outside the first-run flow.

- Export trace of every thread late in the stall (`ANYX360_TRACE=FFFF:280`): only critical
  sections, per-frame input/XUID polls, XNotifyGetNext, gamma (VdGetCurrentDisplayGamma), VdSwap,
  and on the game thread every frame XexGetProcedureAddress + **XamMediaVerificationFailedBlocks**
  (the media verification monitor `823BA2A0`: it polls the X_SECURITY_FAILURE_INFORMATION at
  monitor+16 and stops once total blocks > 0 and complete; the game also calls
  XamMediaVerificationVerify(100 blocks, no overlapped, no callback) again and again). Answering
  "checked = total = the blocks asked for" (tried, then reverted: not committed) did not end the
  stall in a 15-minute run.
- The welcome video's end (13/1) always comes at about the same point of the log (line ~11480),
  so the race load had several minutes in the 10- and 15-minute runs and still never asked for the
  track or the car. Add timestamps to the probe to know when.

## Findings (2026-10-29, third session) - THE BLOCKER WAS FIBERS
- The welcome video is on time: it starts at ~60.5 s and ends at ~129 s (69 s long, ASF header);
  the earlier "late" end was the loading screen arriving later in those runs. Not a problem.
- Kinect: ruled out again (forcing NuiInitialize to fail changes nothing). NuiInitialize at boot fails
  (8301000D: XamContentOpenFile can't open game:\Database.xmplr, as in Xenia); the race's head-tracking
  init (flags C0000820) succeeds. Harmless.
- The race start: the game thread's loop (822F74A0) asks the resource manager [0x82DC3820] for
  state 3 (v104/v108, state at +88; ready at 129.26 s), counts 82AABF00(..,1) down to 0, then posts
  the job 826D5B38 to a cooperative scheduler (8294D940, queue at game manager+112). That
  scheduler runs jobs on XAPI fibers: CreateFiber 82C82AD0, ConvertThreadToFiber 82C829F0,
  ConvertFiberToThread 82C82A80, DeleteFiber 82C82BB0, SwitchToFiber 82C82C10 -> assembly 82C85380
  (saves regs to the fiber block, loads the target's, ends in KeSetCurrentStackPointers). The
  recompiler left SwitchToFiber EMPTY ("ERROR 82C85380"), so no job ever ran.
- Fix: runtime/src/kernel/fibers.cpp (SwitchToGuestFiber/ForgetGuestFiber: each guest fiber on a
  host fiber with its own PPCContext; first entry loads r1/r14..r31 from the block and calls its LR)
  + games/fm4/src/fibers.cpp (hooks). With it the race start job runs, CGame::Init and
  CGame::LoadingUpdate run, the car audio loads. First try crashed (a converted thread's host fiber
  deleted when its block was reused): now host fibers are deleted only if created here and not running.
- Tools in scratchpad fm4b\: rtti.py (class -> vtables), dis.sh (disassembly of a recompiled function).

## Next step (exact)
1. Find the race setup that should follow the Alps audio: who requests the car and track loads
   after `826D5358` (the race-start job run on thread 21: Kinect manager `82A689F8`, `82A78F80`...)
   and what it waits for. Candidates: the Kinect manager / NUI thread (`827C1338` retries
   `827BBE58` every 30 ms forever without a sensor; the worker `82A68968` waits on its object's
   event at +17012), the DB install checks (`IsCarInstalled`, `IsEnvironmentOnDisc`,
   `IsCarOnDisc`; `CContentInstaller` at `823CD428` never starts).
2. Try a direct race launch with the dev parameters (`loadcmdlinedottxt`/`GAME:\cmdline.txt`,
   or ExLoadedCommandLine) to see whether the race loader works outside the first-run flow.
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
