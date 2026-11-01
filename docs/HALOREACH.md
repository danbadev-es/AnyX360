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

## Status (2026-11-01)
- **Boot**: the first-run welcome screen (choose the Spartan's gender), the Armory (helmet,
  shoulders, chest... 5000 credits to start with) and the title menu (version 1.1: Start solo
  campaign, Main menu).
- **Campaign**: Start solo campaign, difficulty, the opening cinematic (Noble Team) and **Winter
  Contingency**'s gameplay (in the Warthog with Jun, the HUD and the tutorial prompts); the pause
  menu offers the last checkpoint, and credits are earned (5148).
- **Split screen**: two players in the **co-op campaign** (the lobby: "this party is local to this
  Xbox 360", Preparing map, Start co-op campaign) on Winter Contingency, each with their view and
  HUD, with each player signed in (`[players] sign_in = "at_start"` and a controller each).
- **Not tried yet**: a custom game, Firefight, three or four players, the map packs, Forge,
  Theater.
- **Xbox LIVE** is gone.

## Known issues
- The same stutter as Halo 3's: a pause of about 400 ms roughly once a second.

## Routes (scripted input)
On a new profile, with `ANYX360_INPUT_LIVE`: at about 75 s `A` (Male, to the Armory), `START`
(leave the Armory: the title menu), `A` (Start solo campaign), `A` (Normal): the cinematic, then
Winter Contingency's gameplay about 150 s later. Co-op: with two players signed in the title menu
offers Campaign: `A` (the lobby prepares the map), then `A` (Start co-op campaign): gameplay about
60 s later (co-op skips the cinematic).

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
