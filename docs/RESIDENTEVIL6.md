# Resident Evil 6: target notes

These are working notes for the twelfth title, the first Capcom (MT Framework) game. Every value
below comes from its `default.xex`, read with `x360tool xex-info`. The full dump is in
[games/re6/notes/xex-info.txt](../games/re6/notes/xex-info.txt).

| Item | Value |
|---|---|
| Developer | Capcom (2012) |
| Title ID / media ID | `43430819` / `4EA1CC2E`, version 0.0.0.3, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`), 11 DLC packages and a Title Update; the European release (English, French, German, Italian, Spanish, Polish, Portuguese, Russian and Japanese text) |
| XEX | `BH6MasterRelease.exe`: AES-encrypted with the retail key, **basic** compression, base `0x82000000`, entry `0x83A74418` |
| Image | 30.25 MiB: 24.56 MiB code (`.text`), 1.31 MiB data, 4.38 MiB read-only |
| Functions | 285,476 |
| XDK | 2.0.21076; links D3D9, D3DX9, XGRAPHC, XAPILIB, XNET, XONLINE, XPARTY, XMP, XHV2, XHTTP, XAUDIO2, XAPOBA, XMCORE and LIBCPMT; compiler 16.0.11886 |
| Imports | `xboxkrnl.exe` and `xam.xex`: 345 and 242 import records ([list](../games/re6/notes/imports.txt)) |
| Target version | The executable patched with the Title Update (`TU_11K620P_000000C000000.00000000001G3`): 0.0.0.3 to **0.0.6.3**; the campaigns, one player (see [Title Update](#title-update)) |

The game's data are MT Framework archives (`nativeXenon\arc\X360\*.arc`), `nativeXenon\serial.srt`
and videos. The scope agreed for the port is the campaign with one player: no split screen, no
online play.

## Title Update
Since 0.18.0 the game is recompiled from its Title Update's executable, as the Kinect games are
([ANYX.md](ANYX.md), [KINECTADVENTURES.md](KINECTADVENTURES.md)). The update
(`TU_11K620P_000000C000000.00000000001G3`, the console cache's form of a `000B0000` package,
3.9 MB) holds `default.xexp` (0.0.0.3 to 0.0.6.3, an XEX of 30.96 MiB) and the update's message
files, `nativeXenon\update\mes_update_<language>.gmd`.

- **Recompiling**: `patch_file_path`/`patched_file_path` in config.toml, output `recomp3`.
  XenonAnalyse: 1,208 jump tables (4 table-driven `bctr` left). No new instructions; the new
  imports (`NetDll_XHttpStartup`/`Shutdown`, `XamGetToken`, `XamFreeToken`,
  `XampXAuthStartup`...) are logged stubs.
- **Addresses**: setjmp/longjmp `83AD6930`/`83AD6610` and the two functions given by hand,
  `83797B20` (the same 151 instructions) and `82502938` (the update added five instructions of its
  own at its start, so it is 0xB8 bytes now): [games/re6/notes/tu_addresses.md](../games/re6/notes/tu_addresses.md).
- **At start** the game patches `default.xex` (cached in `<user>\cache\patched`), mounts the update
  as `update:` and reads `UPDATE:\nativeXenon\update\mes_update_<language>.gmd` at the title
  screen. On a new profile the title screen now comes before the downloadable content check
  ("Comprobando descarga de contenido...") and the storage notice.
- **The DLC**: the launcher installs all 11 packs now (`content_needs_update` removed). Siege Game
  Mode (`11A6F0E4...`, `dlc001:`) and the 3-Mode Pack (`52B6C817...`, `dlc004:`) are mounted with
  the others, and the game reads Siege's menu archive (`dlc001:\arc\X360\MenuDlc_spa.arc`) at start;
  their modes are on the main menu (Extra Content), which opens after the prologue: not reached by
  the scripted player yet.

## Recompiler configuration
[config.toml](../games/re6/config.toml), [switch_tables.toml](../games/re6/switch_tables.toml):
- **Register save/restore helpers** from find_helpers.py; **setjmp/longjmp** (`83AD6930`,
  `83AD6610` in the update; `83A65250`, `83A64F30` on the disc), checked by hand: word for word Black Ops II's, apart from the global setjmp reads and
  the call longjmp makes. The other longjmp candidates are ordinary functions.
- **Jump tables**: 1,171, from the analyser.
- **Two functions given by hand** (`83797B20`, `82502938`; `83726460`, `824DDB90` on the disc): leaf functions without `.pdata`
  entries whose switch counts down CTR with `bdzf`; the last conditional branch targets the next
  instruction, which the analyser took for an empty block (as Black Ops II's `sub_82539CB8`).
- No instruction was missing: no recompiler change was needed.

## Bring-up
- **Kernel export**: `vsprintf`.
- **Region**: `XGetGameRegion` answered 0xFFFF (every region), and the game took the console for a
  Japanese one: its title screen said "Biohazard 6". It now answers the console region of the
  player's country (`GameDescriptor::game_region_from_country`, Xenia's table), for this game only.
- **Content aggregate enumerator**: the game lists its saves and its downloadable content with
  `XContentCreateAggregateEnumerator`, which it finds by ordinal (`XexGetProcedureAddress(xam,
  0x279)`), reads the enumerator's kernel structure (`XamGetPrivateEnumStructureFromHandle`) and
  enumerates through an XAM task (`XEnumerateCrossTitle`, message `0002000E`, completed with
  `XMsgCompleteIORequest`). Without them it reported its saves and content as damaged. The runtime
  now has the export, a structure for aggregate enumerators (other enumerators still have none),
  one `XCONTENT_DATA_AGGREGATE` per package, and completes the task's overlapped.
- **Function-table slots**: the host functions given to the game by `XexGetProcedureAddress` take
  a guest address in the title's code whose function-table slot calls them. RE6's padding words are
  all function starts, so the slot is found in a zero word instead (`loader.cpp`).
- **Pacing**: two vblanks per frame (`vblanks_per_frame = 2`): the game runs at 30 FPS, its rate on
  the console, in the menus, the cinematics and the prologue.
- Still asked for and missing, harmlessly: `NetDll_XNetGetSystemLinkPort`, and xam ordinals 80, 36
  and 1164 by ordinal.

## Status (2026-10-24)
- **Boot and menus**: the logos, the Xbox LIVE notice, the save prompt on a new profile (the
  save, `savedata.bin`, 24 KB, is created), the storage-device and autosave notice, the title
  screen ("PULSA START"), the brightness screen. On a profile with a save the game loads it
  ("Datos cargados correctamente") and goes on to the title. Until the prologue has been finished
  once, the game goes from the title straight to the prologue; the main menu comes after it.
- **The prologue** (Leon and Helena), played with the scripted pad and the prompt driver below:
  the opening cinematic, carrying Helena out of the room, the corridors and stairs, the first dial
  prompt (X), picking up and combining herbs in the item menu, healing Helena, the soldier's attack
  (an aimed shot, the RT dial and the stick prompt to break his grab), the building and its doors,
  the partner actions, the phone call and the alley with its zombies (melee, aimed shots, ammunition
  pick-ups, healing), the railing and the street full of zombies. Checkpoints, death ("Has
  muerto", continue from the last checkpoint), the pause menu and quitting to the title (the save
  is written, "Datos guardados correctamente") work. **Not reached yet:** the end of the prologue:
  the scripted player dies in the street horde after the railing (a person gets through it), so
  the main menu and Leon's first chapter haven't been seen.
- **Frame rate**: 30 FPS, the game's rate on the console (`vblanks_per_frame = 2`), in the menus,
  the cinematics and gameplay; short dips to 17-25 FPS while a new area or effect first appears
  (its shaders are being compiled), and to 24-28 in the busiest fights.
- **Saves**: the profile's save is created, written at checkpoints and on quitting, and read back
  on the next start (`XContentCreateAggregateEnumerator`, above). Within the first prologue,
  quitting and starting again begins the prologue again (from its start).
- **Audio**: music, voices and effects (XMA), and the Spanish voices from the copy's voice pack.
- **Earlier freeze**: in the first session, frames stopped about 70 s in and the process later
  ended with "Graphics device lost" (a D3D12 device removal, the GPU thread in the occlusion-query
  code). It hasn't happened again in about 60 runs since, several over 10 minutes; if it comes
  back, `ANYX360_ZPD` and the D3D12 debug layer (`[xenia] d3d12_debug = true`) are the next steps.

## Downloadable content
The Games on Demand package carries 11 DLC packages (content type 2) and a Title Update. With
the packages installed, the base game opens each with `XamContentCreateEx` (`dlc000:` to
`dlc008:`) at start-up and reads what it needs: with the Spanish language it plays the **Spanish
Voice Pack** (`s0771Stq_vspa.arc`, `uPlVoice00_vspa.arc` and others). The executable names the
other packs' files too: the Mercenaries stage packs (`stpacka.bin`, `stpackb.bin`: **Stage Map
Pack A and B**), the single stages (`stshopa/b/c.bin`: **Rail Yard**, **High Seas Fortress**,
**The Catacombs**) and the voice packs (`%s_v%s`: **German, French, Italian, Spanish**).
Two packs add modes the base executable doesn't have (none of their files' names are in it):
**Siege Game Mode** (`11A6F0E4...`) and the **3-Mode Pack** (Predator, Survivors, Onslaught:
`52B6C817...`), which came with the title update. Until 0.18.0 game.json named them in
`content_needs_update` and the launcher left them out; with the update (above) all 11 are installed. The Mercenaries stages themselves
weren't tried (Mercenaries opens after the campaigns).

## Scripted route
Two pieces: the scripted pad (`ANYX360_INPUT`, with `ANYX360_INPUT_CLOCK=frames:30` so a slow
load doesn't shift it) for the menus and the prologue's opening, and
[tools/scripts/re6_prompts.py](../tools/scripts/re6_prompts.py), which watches the window and answers
the timed prompts through `ANYX360_INPUT_LIVE`, then plays
[tools/scripts/re6_route.py](../tools/scripts/re6_route.py) once the first dial prompt is over.

1. **A new profile** (an empty `--user` folder): `ANYX360_INPUT=12:START,30:A,37:A,46:A,54:A`
   answers the LIVE notice, creates the save, and accepts the save and storage notices. Close the
   game at the title (the save is there from then on).
2. **The prologue**, on that profile:

```
set ANYX360_INPUT_CLOCK=frames:30
set ANYX360_INPUT_LIVE=<scratch>\live.txt
set ANYX360_INPUT=12:START,30:A,37:A,44:A,55:START,66:A,74:A,185:A~0.2x50,232:LUP/25,262:B~0.4x25,276:LUP/20,282:A~0.5x36
start ResidentEvil6.exe --game <game> --user <user>
python -I tools\scripts\re6_prompts.py build\games\re6\Release\ResidentEvil6.exe <scratch>\live.txt <scratch>\prompts.log 1200 tools\scripts\re6_route.py
```

The route accepts the notices, presses START at the title and A at the brightness screen (74 s),
lets the opening cinematic play, presses A through the first carry and B for Helena, and walks;
`re6_prompts.py` answers the first dial (X, at about 305 s) and from then on `re6_route.py`
carries Helena out, uses the herbs, heals her, fights the soldier (the RT dial and the stick
prompt are the driver's), goes through the building and, about 640 s in, runs the alley (LB turns
the camera to the objective, then walk) to the railing (about 700 s). The street horde after it
isn't scripted: the player dies there (about 790 s) and the game offers the last checkpoint.
Checked end to end on 2026-10-24 with a new profile, at 30 FPS throughout. On the updated build
(0.18.0, 2026-10-29) the same commands, on a profile made with step 1, played the opening
cinematic, the first dial (X, at about 310 s), carrying Helena, the building and the soldier's RT
dial (about 570 s) to the cinematic of Leon at about 640 s, at 30 FPS throughout (the screenshots
after that weren't taken: the window was minimized).

## Controls
Resident Evil 6 uses the standard keyboard and mouse defaults (the virtual controller of Gears of
War 3 and the Call of Duty games); a controller works as is.

| Action                         | Keyboard and mouse             | Controller         |
| ------------------------------ | ------------------------------ | ------------------ |
| Move / Camera                  | W A S D / Mouse                | Left / right stick |
| Ready weapon / Fire, melee     | Right / left mouse button      | LT / RT            |
| Run, action, doors, vault      | Space (also Left Shift, Enter) | A                  |
| Partner action                 | V (also Backspace)             | B                  |
| Pick up, reload                | E                              | X                  |
| Item menu (herbs)              | F                              | Y                  |
| Show the way                   | Q                              | LB                 |
| Health tablet                  | R                              | RB                 |
| L3 / R3                        | C / middle mouse button        | L3 / R3            |
| Weapons and items              | Arrows, 1-4, mouse wheel       | D-pad              |
| Pause / Options                | Esc / Tab                      | Start / Back       |

Dial prompts (a needle turning round a button) want the button while the needle is inside the
coloured arc; the stick prompt wants the left stick turned (W A S D in turn).

## Known issues
- The prologue hasn't been played to its end by the scripted player (see Status); the main menu,
  the campaigns after it, Mercenaries and Agent Hunt haven't been seen.
- Siege Game Mode and the 3-Mode Pack are installed and mounted, but their modes (on the main menu,
  after the prologue) haven't been tried.
- No split screen and no online play (Xbox LIVE is gone; the port's scope is one player).
  `NetDll_XNetGetSystemLinkPort` is still unimplemented.
- Stutters (17-25 FPS for a moment) the first time an area or effect appears, until the shader
  cache has it.

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `43430819`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\43430819\00007000\<file>" extracted\re6\game
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\TU_11K620P_000000C000000.00000000001G3" extracted\re6\update
build\tools\x360tool\release\x360tool.exe xex-patch extracted\re6\game\default.xex extracted\re6\update\default.xexp extracted\re6\update\default_0.0.6.3.xex
build\tools\x360tool\release\x360tool.exe xex-extract extracted\re6\update\default_0.0.6.3.xex extracted\re6\default_patched.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\re6\default_patched.bin
cd games\re6
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\re6\update\default_0.0.6.3.xex switch_tables.toml
mkdir recomp3
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release re6
```

XenonRecomp writes into `out_directory_path` (`recomp3`), which must exist and be empty before it
runs. To play, the update's files must be in
`<user>\content\000B0000\TU_11K620P_000000C000000.00000000001G3` (where the launcher installs
them); without them the game stops at once and says so. The two functions given by hand are in config.toml; XenonAnalyse's switch tables need no
edits.
