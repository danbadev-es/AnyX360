# Call of Duty: Black Ops III: target notes

These are working notes for the fourteenth title, the third Call of Duty. The Xbox 360 version
has **no campaign**: multiplayer and Zombies only.
Every value below comes from its executables, read with `x360tool xex-info`; the full dumps are in
[games/bo3/notes/](../games/bo3/notes/).

| Item | Value |
|---|---|
| Developer | Treyarch (Activision, 2015) |
| Title ID / media ID | `4156091D` / `33B8D0AD`, version 0.0.0.5, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 999 files, 5.36 GiB), one Title Update and 5 downloadable content packages |
| XEX | `default.xex` (`cod_xenon_mp_Final.pe`, multiplayer) and `default_zm.xex` (`cod_xenon_zm_Final.pe`, Zombies): AES-encrypted with the retail key, **basic** compression, base `0x82000000` |
| Image (updated) | multiplayer 41.44 MiB (9.56 MiB code: `.text`, `BINK`, `RADCODE`, `.interpr`; 30.25 MiB data); Zombies 40.81 MiB |
| Functions | multiplayer 51,170; Zombies 50,931 |
| XDK | 2.0.21256; links D3D9, XGRAPHC, XAPILIB, XNET, XONLINE, XHV2, XAUDIO2, XAPOBA, XMCORE, XMP, XGSVCEP, XHTTP, XAUTH and LIBCPMT; compiler 16.0.11886 |
| Imports | multiplayer: `xboxkrnl.exe` 156 functions and 12 variables, `xam.xex` 123 functions ([list](../games/bo3/notes/imports.txt)); Zombies about the same ([list](../games/bo3/notes/imports_zm.txt)) |
| Target version | Both executables patched with the Title Update (`tu00000005_00000000`): 0.0.0.5 to **0.0.8.5** |

## The copy
`raw\CODBO3-DLC-TU-GOD.rar` holds `4156091D\00007000` (the Games on Demand package, 34 data files),
`000B0000\tu00000005_00000000` (the Title Update, 429 files, 109.5 MiB: `default.xexp`,
`default_zm.xexp`, patch fast files and packs for every language) and `00000002` (5 packages):

| Package | Name | Content |
|---|---|---|
| `A92CACF8...41` | Compatibility Pack 1 | 620 files, 1.78 GiB: the multiplayer maps' fast files again |
| `4FD64D1A...41` | Compatibility Pack 2 | 115 files, 32.9 MiB: multiplayer and Zombies patches, `compatpack2.ff`, `dlccompat2.id` |
| `D0288B47...41` | Awakening | 195 files, 1.20 GiB: Crucible, Rise, Skyjacked, Splash (`mp_waterpark`) and Der Eisendrache (`zm_castle`), `dlc1.id` |
| `2CCE693B...41` | The Giant | 50 files, 606 MiB: `zm_factory`, `dlc0zm.id` |
| `A685E111...41` | 15 Vials Of Liquid Divinium + 2 Bonus Vials | one 6-byte file (`LQD002.txt`) |

The game's data are fast files (`*.ff`, one set per language: `en_`, `es_`, `fr_`, `ge_`, `it_`,
`ms_`, `po_`, `ru_`, `bp_`...), packs (`*.xpak`), sound banks (`snd\`) and videos (`video\`).

**Multiplayer needs the Compatibility Packs.** Without the downloadable content the title screen
leads to "To play multiplayer you need to download the new required content. It is free. You
must be signed in to Xbox LIVE", then "No network connection detected", and back: the console
version shipped incomplete and these packs were its free day-one download. With the five packages
installed (`<user>\content\00000002\<package>`, where the launcher puts them) the game mounts them
(`dlc1:` to `dlc5:`) and multiplayer opens. So the launcher installs the copy's downloadable
content as it does for the other games (game.json has no `"downloadable_content": false`).

## Recompiler configuration
- Both executables are recompiled **from the Title Update** (`patch_file_path` in
  [config.toml](../games/bo3/config.toml) and [config_zm.toml](../games/bo3/config_zm.toml);
  outputs `recomp2` and `recomp_zm`). The base executables weren't recompiled on their own.
- **Register save/restore helpers** from find_helpers.py on the patched images. **setjmp/longjmp**
  multiplayer `82A60780`/`82A11500`, Zombies `82A52740`/`82A03720`, checked by hand: the CRT's, as
  Black Ops II's (setjmp first calls through a global when it is set; longjmp restores what setjmp
  saved). The other longjmp candidate of each (`8270D0F8`, `826FFA68`) is a function with a
  switch.
- **Jump tables**: 167 in each from the dataflow detector, and one added by hand to each
  ([switch_tables.toml](../games/bo3/switch_tables.toml), at the end): a switch through a table of
  byte offsets (`lbzx` without a shift, `ori r0,r0,0` in place of it) in `sub_8221CC88` (Zombies:
  the same function at `8221C158`), which the detector doesn't recognise. Its `bctr` was taken for an
  indirect call, and a match with bots ended a minute in with "indirect call to an address without
  recompiled code: 8221CED0". The 6 table-driven `bctr` left in each are tail calls through tables
  of function pointers (vtables and struct arrays), correct as indirect calls.
- **Instructions**: `vcmpgtsb` (Bink's code), added to the fork (branch `anyx360-bo3`).
- The 26 `// ERROR` branches in the output are the same kind Black Ops II's has: SEH scope tables
  in front of CRT functions (`.pdata` begin minus 8, which the analyser also took for functions)
  and exception funclets jumping into their parent. None was reached.

## Bring-up
- **`MmSetAddressProtect`**: the engine's hunk allocator sets the protection of every block it hands
  out, and passes 0 for the multiplayer and Zombies hunks. The runtime took 0 for "no access" and
  the first write into the block faulted at start (`sub_826AF068`, Zombies). Protections that don't
  name exactly one access (no access, read-only, read/write or their execute forms) are now refused
  and change nothing, as Xenia does (the console's kernel validates them the same way).
- Still asked for and missing, harmlessly: `XamGetLocaleEx` and `XGetDisplaySize` (stubs answering
  0, as Xenia's `XGetDisplaySize`), xam ordinals 1164, 80 and 36 by ordinal.
- Threads: the game places them itself (`XSetThreadProcessor`); the main thread starts on hardware
  thread 0, as Black Ops II's (`GameDescriptor::main_thread_processor`).
- **Pacing**: one vblank per frame: 60 FPS in the menus, 58-60 in a multiplayer match and in
  Zombies (dips to 42-55 while a level streams in).

## Status (2026-10-30)
- **Boot**: the logos (Activision, Treyarch), the title screen ("Press A to start"), the first-run
  brightness and safe area screens, the profile and offline stats saves (`content\00000001`), the
  LOCAL menu (Multiplayer, Free Run, Zombies, LAN, Online). Text and voices follow the profile's
  language (the copy has all of them).
- **Multiplayer** (scripted pad): Multiplayer > Local opens the lobby (Start match, Game setup,
  Create a class, Specialists, Scorestreaks); Start match loads Team Deathmatch on **Combine**,
  the class choice, then the match: the player moves, aims and fires, with the HUD, minimap and
  audio, at 60 FPS. Game setup offers Change map, Change game mode, Edit game rules and **Adjust
  bots**: each team's size (bots fill the places) and their difficulty. A Team Deathmatch on
  Combine with three players a side (the player and five bots, Professional) plays at 52-60 FPS:
  the bots move, fight and score, the kill feed and the score follow, and the player dies and
  respawns (two and a half minutes played).
- **Zombies** (its own program, `BlackOps3ZM.exe`): Zombies in the LOCAL menu closes the window and
  opens it (about a minute to the ZOMBIES menu); the program also starts directly. Play opens the
  lobby (Change map, Gobblegum, Weapon kits) on **Shadows of Evil**; Start match loads it: the
  opening, round 1, zombies coming through the windows, points, the knife and the pistol, doors to
  buy, at 56-60 FPS. When the player goes down alone the game ends ("Game over. You survived 1
  round") with the after-game report. A person also played it on the test machine.
- **Split screen: not available in this version.** The engine of both executables is built for one
  local player: the table that maps controllers to local clients (`8306E808`, read by
  `sub_82650120` in multiplayer and `sub_82646ED8` in Zombies) has a single entry, and a button
  event from any other controller is dropped (`sub_822E26C8`) before the menus see it. A second
  controller, signed in or not, can't join the lobby ("1 player (2 max.)" and "(4 max.)" are the
  party's sizes for LAN and online play, not local players). Split screen isn't something the
  runtime can add: the game has no second local player.
  Tried before finding that: player 2 pressing START, A or X in the LOCAL menu and in the
  multiplayer and Zombies lobbies, signed in on demand or at start (`[players] sign_in`); the
  game reads the pad (`ANYX360_INPUT_TRACE`) but its menus never get the press (a probe on the Lua
  `IsControllerBeingUsed` saw player 1's presses only). Also tried: answering the game's query
  for a keyboard on each port (`XamInputGetCapabilities` with flag 2) "not connected", as Xenia
  does (the runtime answers any query with the pad); nothing changed, so the runtime was left as
  it was.
- **Title Update**: required (both programs are recompiled from 0.0.8.5); the game mounts it as
  `update:` and reads its patch fast files and packs.
- **Downloadable content**: required for multiplayer (above). The Giant and Der Eisendrache (Zombies)
  and Awakening's multiplayer maps are installed; whether their maps can be chosen and played
  wasn't checked yet.
- Not tried: Free Run, Zombies past round 1 with the scripted player (it dies in round 1 standing
  still), other multiplayer modes and maps, a match to its end, the Zombies' Multiplayer choice
  back to `BlackOps3.exe`.

## Routes (scripted input)
With a user folder that has been through the first-run screens (times from the first input poll):

```
Multiplayer match (Team Deathmatch, Combine):
ANYX360_INPUT=20:START,30:START,40:START,80:A,92:A,104:A

Zombies from the LOCAL menu (Shadows of Evil), the Zombies program's script as ANYX360_INPUT_NEXT:
ANYX360_INPUT=20:START,30:START,40:START,80:DOWN,82:DOWN,84:A
ANYX360_INPUT_NEXT=60:A,70:A,80:A

BlackOps3ZM.exe started directly (Shadows of Evil, playing at about 110 s):
ANYX360_INPUT=60:A,70:A,80:A
```

START skips the logos and passes the title screen; A picks Multiplayer, Start match and the
first class (the match starts at about 125 s). The first time, the brightness and safe area
screens come before the LOCAL menu: add an A for each (about 12 s apart) before the menu's A.
A match with bots (three a side), from the same user folder:

```
ANYX360_INPUT=20:START,30:START,40:START,80:A,92:DOWN,95:A,98:DOWN,100:DOWN,102:DOWN,104:A,108:RIGHT,109:RIGHT,110:RIGHT,112:DOWN,113:RIGHT,114:RIGHT,115:RIGHT,120:B,123:B,126:UP,129:A,150:A,156:A,162:A
```

Game setup (DOWN, A), Adjust bots (three DOWNs, A); RIGHT three times raises Black Ops' team size,
DOWN and RIGHT three times CDP's; B twice back to the lobby, UP to Start match, A; A takes the
first class (the class screen comes at 140-155 s, so A is pressed three times).

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `4156091D`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\4156091D\00007000\949045A1A693FEF769A9" extracted\bo3\game
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\4156091D\000B0000\tu00000005_00000000" extracted\bo3\update
build\tools\x360tool\release\x360tool.exe xex-patch extracted\bo3\game\default.xex extracted\bo3\update\default.xexp extracted\bo3\update\default_0.0.8.5.xex
build\tools\x360tool\release\x360tool.exe xex-patch extracted\bo3\game\default_zm.xex extracted\bo3\update\default_zm.xexp extracted\bo3\update\default_zm_0.0.8.5.xex
build\tools\x360tool\release\x360tool.exe xex-extract extracted\bo3\update\default_0.0.8.5.xex extracted\bo3\default_patched.bin --key keys\xex2_retail.key
build\tools\x360tool\release\x360tool.exe xex-extract extracted\bo3\update\default_zm_0.0.8.5.xex extracted\bo3\default_zm_patched.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\bo3\default_patched.bin
python -I tools\scripts\find_helpers.py extracted\bo3\default_zm_patched.bin
cd games\bo3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\bo3\update\default_0.0.8.5.xex switch_tables.toml
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\bo3\update\default_zm_0.0.8.5.xex switch_tables_zm.toml
mkdir recomp2 recomp_zm
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config_zm.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release bo3
```

XenonRecomp writes into `out_directory_path` from each configuration, and that folder must exist
and be empty before it runs. `build.bat release bo3` builds both programs (`BlackOps3.exe` and
`BlackOps3ZM.exe`). To play a development build, the update's files go in
`<user>\content\000B0000\tu00000005_00000000` and each downloadable content package, extracted
(`x360tool stfs-extract`), in `<user>\content\00000002\<package file name>` (the launcher does
both).
