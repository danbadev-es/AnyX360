# Mortal Kombat Komplete Edition: target notes

These are working notes for the fourteenth title, NetherRealm's fighting game (2011; the Komplete
Edition, 2012). Every value below comes from its `default.xex`, read with `x360tool xex-info`; the
full dumps are in [games/mk/notes/](../games/mk/notes/).

| Item | Value |
|---|---|
| Developer | NetherRealm Studios (Warner Bros. Interactive Entertainment, 2012) |
| Engine | Unreal Engine 3, NetherRealm's (`MK9Game-Retail.exe`), as Gears of War 3 |
| Title ID / media ID | `575207FD` / `6579DAFA`, version 0.0.0.8, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 1,263 files, 6.76 GiB) and 17 downloadable content packages. No title update in the copy |
| XEX | `default.xex`: AES-encrypted with the retail key, **basic** compression, base `0x82000000`, entry `0x8236E580` |
| Image | 21.38 MiB: 16.12 MiB code (`.text`), 1.31 MiB data, 3.94 MiB read-only |
| XDK | 2.0.20353; links D3D9, D3DX9, XGRAPHC, XAPILIB, XNET, XONLINE, XPARTY, XMP, XHV2, XAVATAR, XAUD, XAUDIO2, XAPOBA, XMCORE, LIBCPMT and VCOMP (OpenMP); compiler 16.0.11886 |
| Imports | `xboxkrnl.exe` and `xam.xex`: 363 and 242 import records ([list](../games/mk/notes/imports.txt)) |

The game's data are UE3 packages (`Asset\*.xxx`: characters `CHAR_*`, stages `BGND_*`,
fatalities, intros, the interface), `Config`, `Localization` and WMV videos (`Movies`: the logos and
the story mode's cinematics, played by the XDK's XMedia, linked into the executable).

## The copy
The Games on Demand package (`575207FD\00007000`) holds the Komplete Edition's disc, which already
carries the four downloadable fighters (`CHAR_Skarlet`, `CHAR_Kenshi`, `CHAR_Rain`,
`CHAR_FreddyKrueger`, with their fatalities, voices and interface) and the Klassic costumes, as
well as the bosses (`CHAR_Goro`, `CHAR_Kintaro`, `CHAR_ShaoKahn`). Next to it, 17 content packages
of type 2 (`00000002`):

| Package | What it holds |
|---|---|
| Skarlet, Kenshi, Rain, Freddy Krueger | Each fighter's packages, texts in six languages and an `.ini` that unlocks it (`[Player.DownloadContentUnlocked]`) |
| Kombat Pass | `OnlinePass__Xe-MK9Game.ini` (`bValidOnlinePass=TRUE`) |
| Kombat Pass + All Character Costumes | Eight alternate costumes' packages (`CHAR_CyberZero_C`, `CHAR_Cyrax_C`, `CHAR_Jade_C`, `CHAR_Kitana_C`, `CHAR_Noob_D`, `CHAR_Sektor_C`, `CHAR_Smoke_D`, `CHAR_SubZero_F`), texts and their unlocks |
| Mileena, Sub-Zero, Jade, Scorpion, Reptile, Ermac, Kitana Classic Costume | An `.ini` alone (the costume's unlock; its package is on the disc) |
| Goro, Kintaro, Shao Kahn, "GORO TEST" | Not official: packages that make the bosses playable (`IsDLC=goro`, content ID 6001); "GORO TEST" is an English-only copy of "Goro" |

The 33 avatar items (`00009000`) aren't used.

## Recompiler configuration
[config.toml](../games/mk/config.toml), [switch_tables.toml](../games/mk/switch_tables.toml):
- **Register save/restore helpers** from find_helpers.py; **setjmp/longjmp** `82FE1340`/`82FE1020`,
  checked by hand (the CRT's, as Modern Warfare 3's, Black Ops II's and Forza Motorsport 4's).
- **Jump tables**: 389, from the dataflow detector; 24 table-driven `bctr` left.
- **One function given by hand** (`82CF05B0`, an HRESULT map): a leaf function without a `.pdata`
  entry whose switch counts down CTR with `bdzf`; its last conditional branch targets the next
  instruction, which the analyser took for its end (as Black Ops II's and Resident Evil 6's).
- **The other "ERROR" branches** in the recompiler's output (about 75) are in functions it makes of
  the 8 bytes of exception data that precede each function with a handler (and of the rest of that
  function after them): nothing calls them, and the real function, from `.pdata`, is complete.
- No instruction was missing.

## Bring-up
- **Kernel object headers**: at start the game makes a pool of worker threads, each waiting on a
  semaphore it got with `ObReferenceObjectByHandle`, and waits on it as the XDK's
  `WaitForSingleObject` does: the object type at body-8 (in the object header the console's
  object manager puts before every object) gives the offset of the object to wait on. The
  runtime's events, semaphores, mutants and timers had no header, and the threads crashed on a
  null type. They now have one (`X_OBJECT_HEADER`: pointer count, handle count, type, flags), for
  every title.
- **XAPI fibers**: `SwitchToFiber` (`82BDC380`) is a branch to the stack-switching assembly
  (`82BDCC80`) the recompiler can't follow, as in Forza Motorsport 4. The game switches fibers in
  a list of latent tasks the main loop ticks (`82AFAB28`, from `829D06C8`); the runtime's host
  fibers run them ([games/mk/src/fibers.cpp](../games/mk/src/fibers.cpp) hooks
  `ConvertThreadToFiber` `82BDC240`, `ConvertFiberToThread` `82BDC2D0`, `DeleteFiber` `82BDC320`
  and `SwitchToFiber`). The executable has no `CreateFiber`.
- **Pacing**: the game presents every vblank (60 FPS on the console) and its fights count frames,
  so it runs at 60 only (`vblanks_per_frame = 1`, `fixed_fps = 60`); with the default vblank of
  120 Hz the attract mode's fight ran at double speed.
- Asked for and missing, harmlessly: `NetDll_XNetGetDebugXnAddr`, xam ordinals 80, 36 and 1164 by
  ordinal, the `UPDATE:` drive (no title update).

## Status (2026-10-30)
- **Boot and menus**: the legal screen, the logo videos (WMV, XMedia in the executable), the
  title screen ("PULSA START", about 40 s in on a cold shader cache), the main menu (1 JUGADOR: Kombate, Modo Historia, Torre de los Retos, Entrenamiento, Xbox LIVE, Extras,
  Opciones), the attract mode's fights. The profile's save (`MK9PSET`, 64 KB) is created and
  written, and the 17 downloadable content packages are mounted at start (`DLC0:` to `DLC16:`).
- **Arcade ladder** (Kombate > Torneo): the character select, the difficulty, the ladder's tower,
  the versus screen, the loading screens and the fights, with rounds, "FINISH HIM", wins and
  flawless victories, the Koins and the next fight. The scripted player (mashing attacks on
  Novato) won the first six fights (Ermac, Freddy Krueger, Sektor, Jax, Kung Lao, Rain...) and
  then kept losing to Kenshi, so the bosses and the ladder's end haven't been seen.
- **Local versus, two players**: in the main menu RB switches the header to 2 JUGADORES (LB back):
  Modo Versus > Uno contra uno; at the character select the second player presses START to join
  and A to choose. A whole match (two rounds, "FINISH HIM", the victory) and the "¿Continuar?"
  menu after it (Revancha, Seleccionar un nuevo luchador, Volver al menú principal; the loser's
  controller chooses): Revancha loads the rematch on a new stage.
- **Tag versus** (Modo Versus > Dos contra dos), checked briefly: each side picks two fighters
  (Scorpion and Liu Kang against Sub-Zero and Kung Lao), and the tag fight loads and plays with
  both partners' health bars. Players 3 and 4 pressed START at the character select with their
  own pads; whether they joined as their own players (four controllers) or players 1 and 2 picked
  both fighters wasn't made out. A tag match wasn't played to its end, nor the tag ladder.
- **Komplete Edition fighters**: Freddy Krueger, Kenshi and Rain were the ladder's opponents
  (Freddy and Kenshi also in the attract mode). The character select's first page shows the 28
  original fighters; its other page (where the Komplete fighters and the unofficial bosses would
  be) hasn't been looked at, nor has Skarlet been seen.
- **Frame rate**: 60 FPS in the menus and the fights (33-35 on the title screen's first seconds
  and in loading screens while shaders are compiled the first time).
- **Audio**: FMOD with XMA (music, announcer, effects).
- **Not tried yet**: Story mode, the Challenge Tower, Training, Test Your Might and the other
  tests, the Krypt, the costumes and Klassic skins, the unofficial boss packages (Goro, Kintaro,
  Shao Kahn), the tag ladder and a whole tag match. The latent tasks that use fibers haven't been
  seen running (no "guest fiber" in the logs so far).
- **First appearances**: on a cold shader cache a fight skips a few draws a frame for a moment
  while Xenia creates their pipelines (up to 11 a frame in the first fight); the release's shader
  cache, collected while testing, covers what was played.

## Known issues
- The attract mode's first fight on a cold cache showed two fighters (Freddy Krueger, Kenshi) in
  black and white stripes, at double speed (before the 60 Hz pacing); in the ladder both looked
  right. Not looked into further.
- Xbox LIVE is gone (online fights, the Kombat Pass's online features, leaderboards).

## Routes (scripted input)
With `[players] p1 = { device = "pad" }, p2 = { device = "pad2" }` in the sandbox's
settings.toml (player 2 is connected by its scripted presses):

- **Ladder**: `ANYX360_INPUT=45:START,55:A,62:A,70:A,78:DOWN,80:DOWN,83:A,100:X~0.35x4000,100.15:A~0.35x4000,100.3:Y~1.3x1100`
  presses START at the title, chooses Kombate, Torneo, Scorpion and Novato, and mashes attacks
  from the first fight on (about 100 s).
- **Versus**: `ANYX360_INPUT=45:START,50:RB,55:A,61:A,67:START@1,71:A@1,74:A,90:X~0.35x200,90.15:A~0.35x200,90.3:Y~1.3x50,92:B~2.5x25@1,93:LLEFT/0.5~4x15@1,180:A@1`
  switches to 2 JUGADORES (from 1 JUGADOR: in one later run on the same profile the menu didn't
  start there, and the presses opened Extras), chooses Modo Versus and Uno contra uno; player 2
  joins and takes Sub-Zero, player 1 Scorpion; player 1 wins the match (about 150 s) and player 2
  chooses Revancha at 180 s.
- **Tag versus** (with `p3`/`p4` given `pad3`/`pad4`): `ANYX360_INPUT=45:START,50:RB,55:A,59:DOWN,61:A,66:START@1,68:START@2,70:START@3,74:A,76:A@1,78:A@2,80:A@3,84:A,86:A@1,110:X~0.35x150,110.15:A~0.35x150`
  chooses Dos contra dos; the tag fight starts at about 110 s.

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `575207FD`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\575207FD\00007000\<file>" extracted\mk\game
build\tools\x360tool\release\x360tool.exe xex-extract extracted\mk\game\default.xex extracted\mk\default.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\mk\default.bin
cd games\mk
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\mk\game\default.xex switch_tables.toml
mkdir recomp
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release mk
```

XenonRecomp writes into `out_directory_path` (`recomp`), which must exist and be empty before it
runs. To play a developer build with the downloadable content, the packages go, extracted, in
`<user>\content\00000002\<package file name>\` (the launcher does it).
