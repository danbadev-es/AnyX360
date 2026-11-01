# Diablo III: Reaper of Souls – Ultimate Evil Edition: target notes

These are working notes for Blizzard's action role-playing game (2014). Every value below comes
from its executables, read with `x360tool xex-info`; the dumps are in
[games/d3/notes/](../games/d3/notes/).

| Item | Value |
|---|---|
| Developer | Blizzard Entertainment (2014) |
| Title ID / media ID | `394F07D4` / `30016504`, version 0.0.0.14, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 22 files, 7.3 GiB), its title update and one downloadable content package |
| XEX | `Default.xex` (`Default.exe`): AES-encrypted with the retail key, basic compression, base `0x82000000`, region `0x00FF0000` |
| Image | 20.9 MiB: 14.7 MiB code (`.text`), 1.9 MiB data, 4.3 MiB read-only |
| Functions in `.pdata` | 89,813 |
| XDK | 2.0.21256; links D3D9, XGRAPHC, XAPILIB, XNET, XONLINE, XPARTY, XMP, XHV2, XMIC, XAUDIO2, XAPOBA, XMCORE and XWMAD; compiler 16.0.11886 |
| Imports | `xboxkrnl.exe`: 187 functions and 15 variables; `xam.xex`: 180 ([list](../games/d3/notes/imports.txt)) |
| Target version | Patched with the title update (`394F07D4\000B0000\tu0000000e_00000000`, "Diablo III: RoS Title Update"): 0.0.0.14 to **0.0.2.14** |

The game's data are CRI archives (`CPKs\*.cpk`: the five acts, their caches, `Common.cpk`, and the
Spanish voices and texts of this copy, `esES_*.cpk`). The title update adds `Patch.cpk`,
`Patch2.cpk` and their Spanish versions.

## The copy
`raw\D3ROSUEE-DLC-TU-GOD.rar` (not encrypted) holds:

- `394F07D4\00007000\FCF8B2547580D01A52E0` and its data files: the Games on Demand package;
- `394F07D4\000B0000\tu0000000e_00000000`: the title update, an STFS package of type `000B0000`
  (`Default.xexp` and four CPKs, 6.3 MiB);
- `394F07D4\00000002\D555DE9AB7B934EA3544F971604EC2308F4BEF3439`: "Infernal Pauldrons (eKZ
  Release)", a license file (`config`: `Content "PreorderROS"`, and `Licenses\0`) for the
  pre-order shoulders. It is the only downloadable content package in the archive.

The launcher installs the title update and the package.

## Recompiler configuration
[config.toml](../games/d3/config.toml), [switch_tables.toml](../games/d3/switch_tables.toml):

- **From the title update**: the base, the patch and the patched executable; output
  `games/d3/recomp2`.
- **Register save/restore helpers** from `tools/scripts/find_helpers.py` on the patched image;
  **setjmp/longjmp** `83161AD0`/`83161DA0`, checked by hand (the CRT's, the same code as Black Ops
  II's). The other longjmp candidate, `82D80168`, is an ordinary function.
- **Jump tables**: 610 from the dataflow detector and 6 from the fixed patterns, and **five added
  by hand** (at the end of the file): switches through tables of byte offsets (`lbzx`, a shift,
  `ori r0,r0,0` padding, `add`, `mtctr`, `bctr`, as Black Ops III's) at `82410510`, `824E7E4C`,
  `824EC9B4`, `82B99A0C` and `830F5994`, which the detector leaves unresolved. The game ended at
  start on "indirect call to an address without recompiled code: 824E7F3C" (a case of `824E7E4C`).
  The one table-driven `bctr` left (`82748E20`) is a call through a table of function pointers.
- The 41 "switch case outside function" errors are in fragments the analyser makes of the code
  after three inline tables (`82DE2790`, `82DEFD00`, `82E36DFC`); the real functions, from `.pdata`,
  hold the whole switch.
- No instruction was missing.

## Bring-up
- **Hero names**: a new hero's name comes from the on-screen keyboard (`XamShowKeyboardUI`), which
  the runtime cancelled: the game then stays on the hero creation screen. For this game the
  keyboard now accepts its default text, a name the game proposes (`GameDescriptor::keyboard_ui_accepts`),
  so A on a class creates the hero at once.
- **Pacing**: the game presents every vblank (60 FPS on the console): `vblanks_per_frame = 1`; at
  the default 120 Hz vblank it presented 70-90 frames a second.
- **Local co-op**: each player is a profile; a second controller that is pressed makes the game ask
  that player to sign in (`XamShowSigninUI`), and the runtime signs them in as their slot's profile.
  Each player then chooses or creates their hero and joins on the shared screen, with their own
  saves (`players\<XUID>\content\00000001\d3save`).
- Asked for and missing, harmlessly: `XamGetLocaleEx`, `XNotifyDelayUI`, xam ordinals 80, 36 and
  1164 by ordinal.

## Status (2026-11-01)
- **Boot**: the intro cinematic (skippable), the legal screens, the save (`d3save`) created, the main
  menu and the hero creation screen (the six classes, either sex, hardcore).
- **Act I**: a new monk starts at the road to New Tristram, walks, meets the first zombies and
  kills them (experience shown), with the controller's attack on A and the potion on LB.
- **Saves**: the heroes are kept: after quitting, the next start offers player 2's sorceress in the
  hero selection, and player 1 goes back to the monk.
- **Local co-op**: two players (a monk and a sorceress) on the shared screen, both moving and
  fighting the zombies, with the multiplayer bonus; **four players** joined (player 3 created a witch
  doctor), each with their own HUD in a corner.
- **Title update**: the game runs from it (0.0.2.14); the pre-order shoulders' package is mounted
  (`XamContentCreateEx DLC1`); whether the shoulders appear wasn't checked.
- **Known issue: stalls**. During play the picture freezes for about 430 ms several times every few
  seconds (`[gpu] no frame presented for 430 ms` in the log; 30-40 frames a second on average). The
  render thread (`guest 31`, `82D1D368`) spends them in the D3D library's wait for room in the
  command ring (`82D5C5B0` → `82D4DED8`, polling the read pointer written back at device+0x2B10),
  while the command processor is mostly idle. Next: find what the command stream waits on there
  (a fence the CPU writes, or a register wait) in Xenia's command processor.
- **Not tried yet**: New Tristram itself and the quests after it, Adventure Mode (opened once the
  story has been finished), the town's vendors and the stash, the shoulders.

## Routes (scripted input)
With `[players] p1 = { device = "pad" }, p2 = { device = "pad2" }` in the sandbox's settings.toml
(players 3 and 4 with `pad3` and `pad4`), and `ANYX360_INPUT_CLOCK=frames:60`:

- **To the game**: `ANYX360_INPUT=20:START,22:A,30:START,32:A,40:START,42:A` (skips the cinematic
  and the legal screens; on a new save the hero creation screen comes up, where `LDOWN` moves
  between classes and A creates the hero; with heroes saved, the presses continue the last one).
- **Co-op**: `45:RS@1` connects player 2's controller (any button): the game signs player 2 in and
  shows their hero selection or creation; `A@1` chooses. Players 3 and 4 press START (`START@2`,
  `START@3`) once in the game, then A.
- **First fight**: from the start, `LUP/1.5` then `LRIGHT/7` (both players) reach the zombies on
  the road; A attacks.

## Reproducing
From the archive's contents (`<package>` is the folder that holds `394F07D4`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\394F07D4\00007000\FCF8B2547580D01A52E0" extracted\d3\game
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\394F07D4\000B0000\tu0000000e_00000000" extracted\d3\update
build\tools\x360tool\release\x360tool.exe xex-patch extracted\d3\game\Default.xex extracted\d3\update\Default.xexp extracted\d3\update\default_0.0.2.14.xex
build\tools\x360tool\release\x360tool.exe xex-extract extracted\d3\update\default_0.0.2.14.xex extracted\d3\default_patched.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\d3\default_patched.bin
cd games\d3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\d3\update\default_0.0.2.14.xex switch_tables.toml
(append the five tables added by hand, see above)
mkdir recomp2
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release d3
```

To play a development build, the update's files go in
`<user>\content\000B0000\tu0000000e_00000000\` and the downloadable content package, extracted
(`x360tool stfs-extract`), in `<user>\content\00000002\<package file name>\` (the launcher does
both).
