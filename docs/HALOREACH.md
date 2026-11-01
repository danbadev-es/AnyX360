# Halo: Reach: target notes

These are working notes for Halo: Reach, Bungie's last Halo (2010), on the engine of Halo 3
([HALO3.md](HALO3.md)), whose port it followed: every fix Halo 3 needed applied here too. Every
value below comes from its executables, read with `x360tool xex-info`; the dumps are in
[games/reach/notes/](../games/reach/notes/).

| Item | Value |
|---|---|
| Developer | Bungie (Microsoft Game Studios, 2010) |
| Title ID / media ID | `4D53085B` / `25148C14`, version 0.0.0.7, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 132 files, 6.51 GiB), its title update and 5 downloadable content packages |
| XEX | `default.xex` (`halo3_cache_release.exe`, the engine's name kept): AES-encrypted with the retail key, basic compression, base `0x82000000` |
| Image | 26.81 MiB with the title update: 9.31 MiB code, 16.62 MiB data, 0.75 MiB read-only |
| Functions | 60,094 |
| XDK | 2.0.9328 (some libraries 2.0.8276-2.0.11551): XAPILIB, D3D9 (LTCG), D3DX9, XGRAPHC, XNET, XONLINE, XPARTY, XAUD, X3DAUD, XHV, XMP, LIBCPMT |
| Imports | `xboxkrnl.exe`: 188 functions; `xam.xex`: 112 ([list](../games/reach/notes/imports.txt)) |
| Target version | Patched with the title update (`000B0000\tu00000007_00000000`, "Halo: Reach LATAM Title Update #1"): 0.0.0.7 to **0.0.1.7** |
| Title DLLs | `WaveShell-Xbox.dll`, `WavesLibDLL.dll`, `waves\L360.dll`, `waves\Q10.dll` (other builds than Halo 3's), recompiled into the executable |

## The copy
`raw\HR-DLC-TU-GOD.rar` (not encrypted) holds:

- `4D53085B\00007000\7D65474D9E5C99346D6A` and its 42 data files: the Games on Demand package
  (the disc's files: `maps\*.map` for the campaign `m05`-`m70`, Firefight `ff*`, multiplayer and
  Forge World, `AvatarAssetPack`, `AvatarAwards`, Bink videos and the Waves DLLs);
- `4D53085B\000B0000\tu00000007_00000000`: the title update (1.4 MiB: `default.xexp` and a
  `.dllp` for each DLL);
- `4D53085B\00000002`: 5 packages: the Noble Map Pack, the Defiant Map Pack (two packages), the
  Anniversary Map Pack and "Bonus Content" (12 bytes);
- `4D53085B\00009000`: avatar items, not used.

## Recompiler configuration
- **From the title update**: [config.toml](../games/reach/config.toml); output
  `games/reach/recomp3`.
- **Register save/restore helpers** from find_helpers.py; **setjmp/longjmp** `829B47A0` /
  `8295FC90`, checked by hand: the same CRT code as Halo 3's.
- **Jump tables**: 5 from the fixed patterns, 290 from the dataflow detector, none left: this
  compiler puts its tables in `.rdata`, not after the `bctr`.
- **A function given by hand**: `8213A218` starts inside `8213A178`'s `.pdata` range (that
  function's last case falls through into it), and `8219E4F0` ends with a tail call to it.
- **The "ERROR" branches left** (about 130), as in Halo 3: exception funclets, the 8 bytes of
  exception data before functions with a handler, cases on padding.
- **The Waves DLLs**: as Halo 3's (`config_waveshell.toml`, `config_waveslib.toml`,
  `config_l360.toml`, `config_q10.toml`), at the same bases, from their own builds (version
  0.0.0.7); the update's `.dllp` patches change only their headers.

## Bring-up
Everything Halo 3 needed ([HALO3.md](HALO3.md#bring-up)) applied: I/O completion ports, the cache
partitions, the map signatures, the Waves DLLs (with `GameDescriptor::separate_xex_4k_heap`), the
playback controller, players 2-4's sign-in state. Reach needed besides:
- **Fibers**: XAPI's `SwitchToFiber` (`82960418`, a branch to `829604B0`) and its neighbours
  (`ConvertThreadToFiber` `829601F8`, `ConvertFiberToThread` `82960288`, `CreateFiber` `829602D8`,
  `DeleteFiber` `829603B8`) run on host fibers ([games/reach/src/fibers.cpp](../games/reach/src/fibers.cpp)).
- `XeCryptHmacShaInit`/`Update`/`Final` (HMAC-SHA-1), which Halo 3 doesn't import.

## Fixes after 0.31.0 (2026-11-01)
A player reported that the game and its videos ran several times too fast until gameplay, that
the first cinematic's voices overlapped, that two Spartans had only floating eyes for faces, and
that the co-op campaign in split screen stuttered.

- **Speed: the game is made for 30 FPS.** It presents every other vblank of the console's 60 Hz.
  Its menus, Bink videos and cinematics advance one step per frame. Gameplay keeps time on its
  own clock. The runtime's default vblank is 120 Hz (2 vblanks per frame for 60 FPS), so
  everything frame-driven ran twice as fast: at the title menu 120 swaps/s, and the opening
  cinematic reached Winter Contingency's gameplay **155 s** after Normal. With the 60 Hz vblank
  (the 30 FPS setting) it took **295 s**, with the cinematic's lines in step. The game now always
  runs at 30 FPS, as on the console (`vblanks_per_frame = 2`, `fixed_fps = 30`): the title menu
  presents at 30 swaps/s and the cinematic takes about 280 s. Halo 3 had the same problem and the
  same fix ([HALO3.md](HALO3.md#fixes-after-0310-2026-11-01)).
- **Faces: tessellated triangle strips.** The faces (Kat's, Jorge's, Carter's...) are tessellated
  triangle strips with an index buffer. Tessellation needs triangle lists, and Xenia's primitive
  processor converted only fans, line loops and quad lists. Every frame these draws failed
  (`PM4_DRAW_INDX(4909, 6, 0): Failed in backend`, and others with 5342, 4403 and 404 indices),
  leaving only the eyes, teeth and hair, which are other draws. Indexed strips are now converted
  to lists, keeping their winding and their primitive resets (xenia `anyx360-fixes`, `ee7661e`).
  The faces render in the briefing. This had nothing to do with the speed: they were missing at
  60 Hz too.
- **Split screen**: see Performance below.

## Performance (2026-11-01)
- **Split screen before (the player's 0.31.0 log, 60 FPS):** two players in the co-op campaign
  ran at 33-60 swaps/s, mostly 45-55, with a p99 frame time of 30-50 ms. The guest's
  `MAIN_THREAD` and `RENDER` threads were both at 95-99% of a core. The GPU was busy 50-63% (10-12
  ms per frame, 2400-3000 draws), and Xenia's command processor worked 50-69% of the time. So the
  title's own two threads were the limit, not the GPU or the command processor: rendering two
  views roughly doubles the render thread's work (35% of a core alone at 60 FPS). Uneven 40-60
  FPS was the stutter the player felt.
- **Split screen now (30 FPS, the console's rate):** the same co-op on Winter Contingency, both
  players walking and looking around: 30.0 swaps/s, p99 33.7 ms (two samples of 13 with a 55-75 ms
  maximum), `RENDER` 30-46%, `MAIN_THREAD` 15-32%, the GPU 8-10 ms per frame (23-31% busy), 1400-2850
  draws. There is room to spare, so no Xenia option was needed (Black Ops II's
  `d3d12_submit_on_primary_buffer_end=false` addresses a command processor limit, which this
  isn't).
- **One host core busy all the time:** the guest thread `NETWORK_RECEIVE` uses 90-100% of a core
  from the start, even with no sockets (`ANYX360_NO_SOCKETS`). It polls in a loop. It costs a
  core, not frame rate. System Link work on `xam_net` may change what it waits on.

## Status (2026-11-01)
- **Boot**: the first-run welcome screen (choose the Spartan's gender), the Armory (helmet,
  shoulders, chest... 5000 credits to start with) and the title menu (version 1.1: Start solo
  campaign, Main menu).
- **Campaign**: Start solo campaign, difficulty, the opening cinematic (Noble Team, the faces
  drawn since the fix above) and **Winter Contingency**'s gameplay (in the Warthog with Jun, the HUD and the tutorial prompts); the pause
  menu offers the last checkpoint, and credits are earned (5148).
- **Split screen**: two players in the **co-op campaign** (the lobby: "this party is local to this
  Xbox 360", Preparing map, Start co-op campaign) on Winter Contingency, each with their view and
  HUD, with each player signed in (`[players] sign_in = "at_start"` and a controller each).
- **Not tried yet**: a custom game, Firefight, three or four players, the map packs, Forge,
  Theater.
- **System Link** (with `[network] enabled`, each copy with its own profile): Y > System Link
  games hosts a party, and the other copy's roster lists it with "Join"; joining doesn't start
  yet (the game sends nothing). See [SYSTEMLINK.md](SYSTEMLINK.md#halo-reach).
- **Xbox LIVE** is gone.

## Known issues
- **30 FPS only**, as on the console: the 60 FPS setting doesn't apply to this game, because its
  menus, videos and cinematics would run twice as fast.
- The cinematics can drop below 30 FPS on a busy GPU (22-27 FPS in the briefing here, with the GPU
  at 33-52 ms per frame while another program used it).

## Routes (scripted input)
On a new profile, with `ANYX360_INPUT_LIVE`: at about 75 s `A` (Male, to the Armory), `START`
(leave the Armory: the title menu), `A` (Start solo campaign), `A` (Normal): the cinematic, then
Winter Contingency's gameplay about 280 s later (155 s before the 30 FPS fix). Co-op: with two
players signed in, the title menu offers Campaign: `A` (the lobby prepares the map), then `A`
(Start co-op campaign): the same cinematic, then gameplay about 300 s later. With
`[players] p1`/`p2` `device = "none"`, scripted input alone drives both players
(`ANYX360_INPUT=9999:BACK,9999:BACK@1` connects them).

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `4D53085B`):

```
build.bat
set T=build\tools\x360tool\release\x360tool.exe
%T% god-extract "<package>\4D53085B\00007000\7D65474D9E5C99346D6A" extracted\reach\game
%T% stfs-extract "<package>\4D53085B\000B0000\tu00000007_00000000" extracted\reach\update
%T% xex-patch extracted\reach\game\default.xex extracted\reach\update\default.xexp extracted\reach\update\default_0.0.1.7.xex
%T% xex-extract extracted\reach\update\default_0.0.1.7.xex extracted\reach\default_patched.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\reach\default_patched.bin
cd games\reach
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\reach\update\default_0.0.1.7.xex switch_tables.toml
mkdir recomp3
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
```

and the four DLLs as for Halo 3, then `build.bat release reach`. The update goes in
`<user>\content\000B0000\tu00000007_00000000\`, the downloadable content in
`<user>\content\00000002\<package file name>\` (the launcher does both).
