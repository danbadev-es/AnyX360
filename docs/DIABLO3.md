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
  (`XamContentCreateEx DLC1`).
- **New Tristram** (2026-11-02, a new crusader): the road, the zombies, the defence of the gate with
  the militia ("Kill the undead attacking the gates", a few hundred zombies) and the town past the
  gate ("You've reached a checkpoint", the Tristram militia), at 60 FPS with the stalls below.
- **The pre-order shoulders**: a new hero has the **Infernal Pauldrons** ("Cubrehombros
  infernales", legendary, 39 armour, a Patron reward) in the inventory (BACK, the shoulders slot);
  worn, they show on the hero in the game and in the main menu.
- **Adventure Mode**: Game settings (Ajustes de partida) offers it locked ("Complete Act V to
  unlock Adventure mode"), as on the console; Acts II-V are locked too on a new save. It needs the
  story played through (a person).
- **Stalls: not reproduced on their own; a player should confirm.** On the bring-up night (2026-11-01,
  03-06 h) the picture froze for about 430 ms several times every few seconds, with the render thread
  (`guest 31`, `82D1D368`) waiting for room in the command ring (`82D5C5B0` -> `82D4DED8`, polling the
  read pointer written back at device+0x2B10) and the command processor mostly idle. That log has the
  warning `the display returns no vblanks`, like all the paused logs of that night, in every game
  ([ARCHITECTURE.md](ARCHITECTURE.md#pauses-of-about-04-s-a-display-that-gives-no-vblanks)).

  Measured on 2026-11-02 with 0.33.0, on a display giving vblanks, other games and compilers running
  beside it, from a new hero on the road in Act I (five runs, 160 to 530 s of play). Four held 60 frames
  a second with a p99 of 19-21 ms, about 600 draws a frame and the GPU 45-58% busy, one of them for 530
  s without a stall. The other, the first, ran at a median of 46 frames a second (p99 48 ms, the GPU
  65-77% busy at 13-16 ms a frame) with Halo 3 and Kinect Joy Ride beside it; a later run beside Halo:
  Reach and Kinect Joy Ride held 60, so that difference isn't explained.
  The only stalls in play, 280 ms to 2.4 s, came in two of the runs at the moments another agent's Black
  Ops II started on the same machine (at over 700% CPU in the one run with a process log, 21:50:56),
  with the present call itself taking 280-490 ms. They stopped when it had started, except in one run
  that ended first. A copy of Mortal Kombat starting beside it caused none. Also seen: a hitch of 1.4-2 s
  as the game world loads, about 105 s in, before play.

  So the recurring freezes need one of those two outside causes in everything measured here. **Not
  covered:** none of these five runs reached an enemy (the script walks the road and attacks the air),
  and why another
  program's start holds the presents is unknown. If a player sees freezes alone on the machine, with
  no warning in the log, this needs looking at again, starting from the ring wait above.

  The one walk that reached the fights, the same day, was on a machine busy with a build at 6 jobs:
  to New Tristram at a median of 60 FPS, with 44 stalls over 300 ms in about 17 minutes (the longest
  2.7 s, at the gate's fight). So fights on a free machine are still unmeasured.
- **Not tried yet**: the quests after New Tristram, the town's vendors and the stash.

## The stop on an unreadable message (open)

On 0.36.0, hosting over Tailscale (relay, about 200 ms) with two local players and one remote, the host
stopped after about 46 minutes at 60 FPS, with 5.3 GB of RAM free; the other PC went back to the lobby. On
the main thread, the dispatcher `sub_826D6D18` handles a message of type 35 (case 31 of its switch):
`sub_82967408` compares the message's ID, then decodes its payload (at +12, size at +8). It returned 0, so
the game asserts (`sub_82CDF678`, line 2525) and `sub_82CDEAB0` stores `CDCDCDCD` to address 0. The log has
`RtlRaiseException C0000025 (ignored)` just before. One case so far: what type 35 is, and whether it came from
the other PC, isn't known.

`src/message_probe.cpp` logs each message that can't be read (`d3 message: type ... COULD NOT BE READ`) with
its ID, size and bytes; `ANYX360_D3_MESSAGE_TRACE=1` logs every message. Two scripted runs alone at the menu,
170 and 330 seconds, met no message, so these lines haven't been seen yet. Skipping such a message instead
of stopping is possible, at the risk of the PCs falling out of step: to decide once the message is known.

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
- **New Tristram**: the road from the start runs east below the town's wall: move east past the
  fence (`LDOWN/2`, then `LRIGHT/6`) to the road's first zombies, then north-east (`LUP/6` with
  `LRIGHT%70/6`) to the gate; A (`A~0.3x400`, LB for a potion now and then) holds the gate for
  about three minutes; then west and north (`LLEFT/3.5` with `LUP%45/3.5`) through it. The stick
  steps were found looking at the screen (`ANYX360_INPUT_LIVE`): from another spot they differ.
- **Inventory**: BACK opens it; the stick picks a slot (up and left: the shoulders), A equips.

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
