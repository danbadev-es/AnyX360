# Gears of War 3: target notes

Working notes for the first title. All values below come from the retail `default.xex`
(`x360tool xex-info`; full dump in [games/gow3/notes/xex-info.txt](../games/gow3/notes/xex-info.txt)).

| Item | Value |
|---|---|
| Engine | Unreal Engine 3 (`GearGame-Xbox360-Shipping.pe`) |
| Title ID / media ID | `4D5308AB` / `02EEE060`, version 0.0.0.7, disc 1 of 1 |
| Source used | Extracted disc folder (2537 files, 7.3 GB) in a 7z, not an ISO |
| XEX | AES-encrypted (retail key), **basic** compression (not LZX), base `0x82000000`, entry `0x82CE8EB0` (`_xstart`) |
| Image | 21.7 MiB: 13.3 MiB code, 5.7 MiB data, 2.7 MiB read-only |
| Functions in `.pdata` | 41,369 (leaf functions without a frame are not listed there) |
| XDK | 2.0.20500 (D3D9LTCG, XAUDIO2, XMP, XHV2, XONLINE…); compiler C1/C2 16.0.11886 |
| Imports | `xboxkrnl.exe`: 163 functions + 13 variables; `xam.xex`: 117 functions ([list](../games/gow3/notes/imported_functions.txt)) |
| Middleware in its own sections | Bink video (`BINK*`), RAD (`RAD*`) |
| Target version | Base disc executable; no Title Update |

## Sections
See [games/gow3/notes/sections.txt](../games/gow3/notes/sections.txt). Code lives in `.text`
(`0x822A0000`–`0x82FC91CC`), `BINK` and `RADCODE`.

## Recompiler configuration (done)
- **Register save/restore helpers**: each byte pattern from the XenonRecomp README occurs exactly
  once in `.text`. The addresses are in [config.toml](../games/gow3/config.toml).
- **setjmp/longjmp**: no setjmp-shaped function found (scan for a function storing r14–r31 and f14–f31
  through r3). Probably unused. The only candidate, `0x82CCC3F0`, calls `RtlUnwind` and looks like C++
  exception unwinding; still to confirm in Ghidra.
- **Jump tables**: the stock XenonAnalyse finds **0**. This compiler schedules switch sequences
  differently (e.g. `lis, rlwinm, addi, lwzx`), pads them with `ori r0,r0,0` and places absolute tables
  inline right after the `bctr`. The AnyX360 fork adds a dataflow detector. It finds **108** jump tables
  (4 of them unguarded switches with inline tables). The remaining 19 table-driven `bctr` sites were
  inspected and are indirect calls, not switches: a 256-entry bytecode dispatcher at `0x822DC8xx` and
  struct-of-function-pointer calls.
- **Function boundaries**: leaf functions containing a switch are not in `.pdata`, and the stock
  analyser stopped at their `bctr` (215 "switch case outside function" errors). Fixed in the fork.

## Instruction coverage (done for the game code)
The first run reported 5,190 unimplemented instructions of ~45 kinds that Sonic Unleashed never used:
`bdzf`, update-form loads/stores (`stfsu`, `lfsu`, `sthu`, `lhzu`…), 16-bit vector ops (`vsrah`, `vslh`,
`vsubshs`, `vcmpgtsh`…), saturating packs, `vsel128`, `eqv`, `subfze`, `addme`, `addc`, `vcfpuxws128`,
`blrl`, and float16_2 `vpkd3d128`/`vupkd3d128`. In addition `vcmpbfp128` (271 sites) emitted a
debug trap. All are implemented in the fork.

**Remaining:** 36 `mulchw.` decodes, all in the `BINK` section: an opcode-4 encoding the disassembler
reads as a PPC405 instruction. Bink is better replaced by a native decoder anyway (it only plays
`.bik` movies), so that code should never run.

## Why UE3 helps
- **Game logic is UnrealScript bytecode** inside the packages, run by the engine's VM. Only native
  engine code is recompiled; script behaviour comes for free once the VM runs.
- UE3's structure (RHI, audio device, file manager, threading) is widely documented, so engine
  subsystems can be recognised from their strings and call patterns.

## Known challenges
- **Threading**: the engine uses the Xenon hardware threads for rendering, audio, streaming and task
  graphs. Synchronisation primitives in the HLE have to be exactly right.
- **Rendering**: deferred lighting, heavy eDRAM resolves, post-processing. We replace the Xenon RHI
  with D3D12 so resolves become ordinary render-target copies.
- **Streaming**: seamless level streaming from disc. The VFS has to be fast and fully asynchronous.
- **Online**: Xbox Live for the 360 is gone. The scope is campaign, local split-screen and Horde
  offline.

## Reproducing (from the 7z in `Raw/`)
```bat
build.bat
"C:\Program Files\7-Zip\7z.exe" x "Raw\Gears Of War 3.7z" -oextracted\gow3
set T=build\tools\x360tool\Release\x360tool.exe
%T% xex-info    "extracted\gow3\Gears Of War 3\AP7kB\default.xex"
%T% xex-extract "extracted\gow3\Gears Of War 3\AP7kB\default.xex" extracted\gow3\basefile.bin
cd games\gow3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe "..\..\extracted\gow3\Gears Of War 3\AP7kB\default.xex" switch_tables.toml
mkdir recomp
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat
```
`recomp/` must be empty before regenerating: XenonRecomp does not delete output files from an earlier
run whose function split was different, which leads to duplicate symbols at link time.

## Distribution
Since 0.4.0 one release holds every game: `powershell -ExecutionPolicy Bypass -File
tools\package\make_dist.ps1` builds the games and the launcher, then creates
`dist\AnyX360-<version>\` and a zip of it. The version is `0.4.0` for a tagged release, or
`0.4.0+<commit>` otherwise (see Versioning in the README). The release contains no game data. It
holds:
- `AnyX360.exe`, the launcher (`tools/launcher`).
- `bin\`: each game's executable and its .pdb, 7-Zip, each game's shader cache
  (`bin\shader_cache\<id>`) and, for the webcam games, the pose bridge.
- `keys\` and `licenses\` (third-party licenses).
- `README.txt` (`tools/package/README.txt` with each game's `tools/package/<id>/section.txt`),
  `TERMS.txt` and `CHANGELOG.md`.

What the launcher does:
- Shows its text in English or Spanish. Windows' display language picks one; a link in the footer
  switches, and the choice is remembered.
- Shows the terms of use (`tools/launcher/terms_en.txt`, `terms_es.txt`, naming every game) and
  requires accepting them. They are asked again whenever either text changes.
- Shows the games the player added as a gallery: each game's dashboard icon, read from its
  `default.xex` (`x360tool xex-icon` reads the same XDBF resource), or a cover the player chose.
- Adds a game from an archive (.7z, .rar, .zip) or a folder: it finds `default.xex`, recognises
  the game by its title ID (only `default.xex` is extracted to check an archive), checks the copy
  and the free space, and extracts it to `games\<id>\game\` with a progress bar; a folder is used
  where it is (`games\<id>\location.txt`).
- Starts the game without a console window, writing its output to `logs\<id>\last_run.log`.
- If the game exits with an error, shows the launcher again and points to the log.

## Status (2026-10-16)
- Boots through the Epic/Microsoft logos (Bink), title screen, brightness setup and every menu.
- **Campaign works**: New campaign → difficulty → Act I Prologue "Troubled Past" (`SP_Nightmare_*`):
  in-engine cinematics with subtitles and voices, then gameplay.
- **Private Horde works**: Horde → Private → Start Game → map selection (START) → character setup
  (START) → Checkout loads and plays (movement, aiming, firing, HUD).
- **Audio**: XMA decoded by Xenia's XmaDecoder (FFmpeg fork), output through XAudio2.
- Shaders and pipelines are cached in `<user>/cache/shaders`, so later runs start faster.
- Display: 60 FPS by default (vblank at 120 Hz; the game presents every second vblank), window
  1080p with AMD FSR upscaling. Options: `--resolution 720|1080|1440|2160`, `--scale 1-3` (native
  render scale; 2x is GPU-heavy), `--upscale fsr|cas|bilinear`, `--fxaa`, `--fps 30|60`,
  `--fullscreen`; F11 / Alt+Enter toggle fullscreen. The window title is the XEX's own title name
  ("AnyX360 - Gears of War 3", from the SPA/XDBF resource).
- **Settings panel (F2)**, an ImGui window over the game (F2 or Esc closes it):
  - Display: window size, fullscreen, upscaling, FXAA, VSync, 30/60 FPS, and the render scale, which
    needs a restart ("Restart now" relaunches the game with the saved settings).
  - Controls: mouse sensitivity, aiming sensitivity, invert Y, smoothing and anti dead zone.
  - Players: the device of each player.
  - Performance: frame-interval graphs and the vblank state.

  While the panel is open the game gets no keyboard or mouse input and the cursor is free.
- **Settings file** `<user>\settings.toml` (`%LOCALAPPDATA%\AnyX360\GearsOfWar3`). Precedence:
  defaults, then the file, then command-line flags. Flags apply to that session only and are saved
  only if the same setting is then changed in the panel. An invalid file is renamed to
  `settings.toml.bad` and the defaults are used.
- Performance overlay (top-left, **F1** toggles, remembered): guest FPS and frame time, frame and
  present interval graphs, the process's VRAM use (DXGI budget), host RAM (working set and commit)
  and guest memory in use.

### Keyboard and mouse
Player 1 uses the keyboard and mouse by default, player 2 the first Xbox controller. Devices are
assigned in the F2 panel's Players tab or with `[players]` in settings.toml. To play alone with a
controller, set player 1 to it.

The mouse is the right stick. Raw mouse input is read on its own thread, and the stick deflection
is the mouse speed over `smoothing_ms`, times the sensitivity (the aiming sensitivity while LT is
held), with a radial anti dead zone so slow movements still turn the camera. The cursor is captured
(clipped and hidden) while the window is active; Alt+Tab or F2 frees it.

| Pad | Keys (default; `[input.bindings]` overrides) |
|---|---|
| Left stick | W A S D |
| Right stick | Mouse |
| A (cover, roadie run, confirm) | Space, Left Shift, Enter |
| B (melee, back) | V, Backspace |
| X (use, pick up, revive) | E |
| Y (point of interest) | F |
| LB (Tac-Com) / RB (reload) | Q / R |
| LT (aim) / RT (fire) | Right / left mouse button |
| LS / RS (scope zoom) | C / middle mouse button |
| Back / Start | Tab / Esc |
| D-pad (weapons) | Arrows; 1 2 3 4 = left, right, down, up; wheel up = right, wheel down = left |

### Split screen
Player 2 (and 3, 4) joins in the main menu by pressing Start with their device. The party then shows
the player as "not signed in" and the campaign runs in split screen. To join with a profile
(name, saves under `<user>\players\<XUID>`), sign the player in at the title screen. Use the F2
Players tab or `players.sign_in = "at_start"`. Gears returns to the title screen ("Sign-In Status
Changed") when the number of signed-in players changes later. Player 1's profile and saves keep
their original paths.

### Frame pacing
Campaign gameplay at 60 FPS on a 60 Hz display, windowed. Measured with `ANYX360_FRAMETIMES` and
`tools/scripts/frametimes.py`, over the same 60 s of the prologue:

| | Before (3d015f5) | Now |
|---|---|---|
| Vblank lateness p50 / p99 | 606 / 1537 µs | 7 / 300 µs |
| VdSwap interval sd / p99 | 1.40 / 20.2 ms | ~1.0 / ~19.0 ms |
| Present interval sd / p99 (GPU thread) | 1.01 / 18.9 ms | 0.8 / 18.4 ms |
| UI-thread presents with the overlay shown | 120/s (60 repaints) | 60/s (new frames only) |
| Present phase in the display refresh | drifts ~1.3 ms every 30 s | held at 0.50 (p5–p95 0.44–0.56) |
| Frames shown for exactly one refresh | not measured; periodic judder while drifting | 100% |

What changed:
- The vblank follows an exact QPC schedule with a high-resolution waitable timer.
- The vblank is locked to the display refresh when the rates are compatible, steering presents to
  the middle of the refresh.
- The dispatcher wakes only the waits a signal satisfies.
- The overlay no longer forces UI-thread presents at the monitor rate (F1 never reached it).
- The GPU command processor queue is thread-safe.
- Thread priorities follow the game's KeSetBasePriorityThread (Win32-like increments: -1..2,
  16/17 for its time-critical threads).
- The process opts out of power throttling.
- VSync (on by default) removes tearing in fullscreen.

In-engine cinematics, the intro and Bink videos run at 30 FPS or less by design. The main thread
waits for the render thread with zero-length KeDelayExecutionThread calls (about a million per
second), as it does on the console; this keeps one CPU core busy.

### How the campaign starts (for reference)
The front end calls `GearEngine.SetCheckpoint` (the chapter's start checkpoint) and travels to
`GearGame_P?game=GearGameContent.GearGameSP?chapter=N?listen`. `GearEngine::LoadMap` then reads the
chapter's data (`MapName`, e.g. `sp_nightmare_p` for chapter 0) and streams its levels into
`GearGame_P` — but only if the base `LoadMap` returned no error. `?listen` makes the campaign a listen
server for co-op, so a failing socket layer ("Error initializing network layer.") silently left the
player in the empty `GearGame_P`. Sockets are emulated locally for this reason (`xam_net.cpp`).

### Known issues
- The Horde pre-wave counter stays at 0:30 (it may wait for the command post to be built; unverified).
- An intermittent crash while the async loader creates objects (corrupt link in the object hash
  chains, `sub_8230F098`); seen loading Horde maps, not reproducible on demand.

### Analysis notes
- `GNatives` 0x833CDFD8 (`EX_VirtualFunction` 0x822CD110, `EX_FinalFunction` 0x822CD180).
- `FFrame`: `Object` +0x14, `Code` +0x18. `UObject`: `Outer` +0x28, `Name` +0x2C, `Class` +0x34.
- `GWorld` 0x83429828; `GearEngine` checkpoint fields: pending action +1864, `CurrentCheckpoint` +1880.
- Name entries are packed `[be32 index << 1][be32 hash next][text]` from 0x40130000.

## Other GPUs (an AMD RX 570)
On an AMD RX 570 (4 GB) the title screen's skull, before Start, showed an artefact; the main menu
was fine. It doesn't happen on the NVIDIA GPU these notes were made on, and the render target path
is the same on both (Xenia uses render target views on AMD and NVIDIA alike), so it comes from a
Direct3D 12 feature or driver difference not identified yet. Two aids for finding it on that PC:
- **The log** (`logs\gow3\last_run.log`) now names the GPU and the features where vendors differ:
  `GPU: <name> (vendor ...); alpha blend factor ..., unaligned BC textures ..., ROV ..., binding tier ...`.
- **Xenia's options in `settings.toml`**, without rebuilding, in a `[xenia]` section (each value as
  TOML; an unknown name is reported in the log). Candidates for this kind of artefact:

  ```toml
  [xenia]
  half_pixel_offset = false              # pixel centres (default true)
  gamma_render_target_as_unorm16 = false # how gamma render targets are stored
  async_shader_compilation = false       # no frames drawn with shaders still compiling
  render_target_path_d3d12 = "rov"       # the other render target path (risky on AMD)
  ```

  With `ANYX360_CAPTURE_DIR=<folder>` the frames are saved as BMP every 5 s, for comparing.

## Debugging switches
All opt-in through environment variables read by the runtime:

| Variable | Effect |
|---|---|
| `ANYX360_INPUT` | Scripted pad input: `"25:START,35:A,170:START/0.3,50:START@1"` (`seconds:button[/hold][@player]`, player 0-3, default 0; buttons START BACK A B X Y UP DOWN LEFT RIGHT LB RB LT RT LUP LDOWN LLEFT LRIGHT). A player with scripted input counts as connected |
| `ANYX360_INPUT_TRACE` | Logs every change of each player's virtual pad |
| `ANYX360_FRAMETIMES` | Writes every vblank, VdSwap, GPU swap, present and display vblank to a CSV; summarise with `tools/scripts/frametimes.py <csv> [--from s] [--to s]` |
| `ANYX360_OVERLAY` / `ANYX360_NO_OVERLAY` | Show the performance overlay from the start / no ImGui at all (A/B tests) |
| `ANYX360_FPS_SWITCH` | Changes the frame rate at given times: `"30:30,60:60"` (`seconds:fps`) |
| `ANYX360_SETTINGS_TEST` | Applies panel actions at given times: `"20:resolution=1440,25:upscale=cas,30:fps=30,35:fxaa=1,40:fullscreen=1,45:vsync=0,50:scale=2,55:restart"`; also `panel=1/0`, `tab=<name>`, `stats=1`, `lock=0/1`, `signin=<player>`, `signout=<player>` |
| `ANYX360_VBLANK_SPIN_US` | How long the vblank thread spins before each tick (default 300) |
| `ANYX360_NO_DISPLAY_LOCK` | Free-running vblank instead of locking it to the display |
| `ANYX360_DISPATCH_SAFETY_MS` | Interval of the dispatcher's missed-wakeup re-check for infinite waits (default 250, 0 disables) |
| `ANYX360_CAPTURE_DIR` | Saves the guest frame as BMP every 5 s |
| `ANYX360_DUMP_AT` | Thread dumps at the given seconds (`"95,100"`) |
| `ANYX360_MEMSCAN` | With `ANYX360_DUMP_AT`: logs guest memory holding that text (ASCII or UTF-16BE) |
| `ANYX360_TRACE` | Logs the exports a thread calls after a time: `"11:75"` (`FFFF` = all threads) |
| `ANYX360_HOOK` | Logs indirect calls to guest functions: `"8252A040,..."` |
| `ANYX360_LOG_FILES` | Logs file opens, writes and info changes |
| `ANYX360_ZPD` | Occlusion query mode: `fake`, `fast`, `fast-alt`, `strict` |
| `ANYX360_XENIA_LOG_LEVEL` | Xenia's log level |
| `ANYX360_UE3_TRACE` | UnrealScript call trace: `"<seconds>[,filter]"` (first call of each `Class => Function`, or every call matching the filter) plus logging hooks on map, checkpoint and package loading |
| `ANYX360_MEMDUMP` | With `ANYX360_DUMP_AT`: dumps guest memory, `"addr,length"` in hex |
| `ANYX360_MEMSCAN=0x...` | Searches a big-endian 32-bit value instead of text |
| `ANYX360_WATCH_WRITE` | Hardware write watchpoint on a guest address (logs the writing function) |
| `ANYX360_CAPTURE_MS` | With `ANYX360_CAPTURE_DIR`: capture interval in ms (default every 5 s) |
| `ANYX360_AUDIO_WATCHDOG` | Thread dumps when audio mixing stalls for 70 ms |
| `ANYX360_NO_SOCKETS` | Disables the local socket layer and the network address |

The runtime also accepts `--user <folder>` to run with a clean profile, saves and settings.
