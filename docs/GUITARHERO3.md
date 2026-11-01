# Guitar Hero III: Legends of Rock: target notes

These are working notes for Neversoft's rhythm game (2007), the oldest title ported so far. Every
value below comes from its executables, read with `x360tool xex-info`; the dumps are in
[games/gh3/notes/](../games/gh3/notes/).

| Item | Value |
|---|---|
| Developer | Neversoft Entertainment (Activision, RedOctane, 2007) |
| Title ID / media ID | `415607F7` / `2C5FAD73`, version 0.0.0.2, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 660 files, 3.4 GiB), Title Update #3 and 25 downloadable content packages |
| XEX | `default.xex` (`gh3_Final.exe`): AES-encrypted with the retail key, basic compression, base `0x82000000` |
| Image | 15.1 MiB: 8.0 MiB code (`.text`, `BINK`), 5.6 MiB data, 1.6 MiB read-only |
| Functions in `.pdata` | 29,207 (90,546 recompiled functions in all) |
| XDK | 2.0.6132; links D3D9, D3DX9, XGRAPHC, XAPILIB, XONLINE, XHV, XMP and XAUD (the older XAudio); compiler of that XDK |
| Imports | `xboxkrnl.exe`: 175 functions; `xam.xex`: 151 ([list](../games/gh3/notes/imports.txt)) |
| Target version | Patched with the title update (`TU_10LC1VN_0000008000000.00000000000O2`, "Title Update #3"): 0.0.0.2 to **0.0.3.2** |

The game's data are Neversoft's packages (`DATA\COMPRESSED\PAK\*.pak.xen`, the QB scripts in
`qb.pak.xen`), FMOD sound banks with XMA audio (`DATA\MUSIC\*.fsb.xen` for the songs,
`DATA\STREAMS`) and Bink videos (`DATA\MOVIES\BIK`).

## The copy
`raw\GH3LOR-DLC-TU-GOD.rar` (not encrypted) holds:

- `415607F7\00007000\<package>` and its data files: the Games on Demand package;
- `TU_10LC1VN_0000008000000.00000000000O2` at the root: the title update as the console's cache
  keeps it, an STFS "LIVE" package of type `000B0000` (`default.xexp` alone);
- `415607F7\00000002`: 25 downloadable content packages (2.5 GB extracted): the track packs
  (Foo Fighters, Velvet Revolver, Def Leppard, No Doubt, Muse, Motörhead, DragonForce, Classic Rock,
  Guitar Virtuoso, Boss Battle, Modern Metal, Isle of Wight, Warner/Reprise, Coldplay, Dropkick
  Murphys, the Companion Three Pack), Metallica's "Death Magnetic", and the singles (Dream On, the
  Halo theme, World of Warcraft, We Three Kings, the 4th of July single, "Ernten Was Wir Säen",
  "So Payaso", "Antisocial").

The launcher installs the title update and the 25 packages.

## Recompiler configuration
[config.toml](../games/gh3/config.toml), [switch_tables.toml](../games/gh3/switch_tables.toml):

- **From the title update**: the base, the patch and the patched executable; output
  `games/gh3/recomp3`.
- **Register save/restore helpers** from `tools/scripts/find_helpers.py` on the patched image;
  **setjmp/longjmp** `82787880`/`82787200`, checked by hand (setjmp calls through a global when
  it is set, then saves f14-f31 and r13-r31 through r3; longjmp restores them through r7). The
  other longjmp candidate, `824D4EE0`, is a function with a switch.
- **Jump tables inside the code**: this compiler (XDK 2.0.6132) puts a switch's table of absolute
  labels right after its `bctr`, in `.text`. XenonAnalyse's fixed patterns found the labels
  (836 tables) but didn't say where the table is, so the function analyser stopped at the table
  and cut the function there: 1,922 "switch case outside function" errors, and the code after
  each table became functions of their own. The fork's XenonAnalyse now writes `table` and
  `table_size` for tables inside a code section (817 here), as its dataflow detector does
  (XenonRecomp branch `anyx360-gh3d3`). No error is left.
- **One table fixed by hand** (`base = 0x8222C27C`): a 4-entry switch on `r11` whose fixed-pattern
  match took a `cmplwi` on `r4` further up (one label, the wrong register). Rerunning XenonAnalyse
  undoes it: fix that entry again (`r = 11`, `table_size = 0x10`, labels `8222C2A4`, `8222C2AC`,
  `8222C2B4`, `8222C2BC`).
- The one table-driven `bctr` left (`822215B0`) is a tail call through a function pointer. The 46
  `// ERROR` branches left in the output are in exception scopes; none was reached in the runs.
- No instruction was missing.

## Bring-up
- **Audio**: the game plays through FMOD on the XDK's older XAudio library (XAUD), which drives the
  XMA decoder through the kernel's `XMA*` functions (`XMAInitializeContext`, `XMAEnableContext`,
  `XMASetInputBuffer0Valid`...). They were unimplemented, and every menu and song was silent. They
  are now in the runtime ([xboxkrnl_audio.cpp](../runtime/src/hle/xboxkrnl_audio.cpp)), as Xenia's,
  on its XMA decoder; no other ported game imports them.
- **Pacing**: the game presents every vblank (60 FPS on the console): `vblanks_per_frame = 1`. It
  runs at 60 FPS in the menus and the songs.
- **Controller**: the game accepts a standard controller without a guitar. Its gamepad layout is
  the frets on **LT, LB, RB, RT and A** (green, red, yellow, blue, orange), seen lighting up during
  a song, and the strum on the D-pad. In the menus A selects, B goes back and the D-pad moves. No
  guitar emulation is needed.
- Asked for and missing, harmlessly: `XexGetProcedureAddress(xam, 36)`, `XNotifyDelayUI`.

## Status (2026-11-01)
- **Boot**: the Bink intro, the guitar safety notice, the title screen ("pulsa un botón"), the main
  menu (Carrera, Carrera coop., Part. rápida, Multijugador, Ensayo, Opciones, Xbox LIVE) at
  60 FPS. The save (`Progreso`) is created in `content\00000001`; texts follow the system language.
- **Quick Play**: the song list, the difficulty and a song (Slow Ride) start and play. With no one
  playing the song fails part-way, as on the console.
- **Practice** (Ensayo > Práctica: no failing): Slow Ride played whole to its results page
  ("0 de 477" notes, the page to restart, change speed or section, or quit).
- **Career**: naming the band, the difficulty, the hero (Axel Steel), the career's opening Bink
  cinematic and the first gig (Backyard Bash, Slow Ride) load and play. Passing the gig needs a
  player: the scripted inputs don't hit the notes.
- **Title update**: the game runs from it (0.0.3.2; the copy's 0.0.0.2 is patched at start).
- **Downloadable songs**: the 25 packages are mounted at start (`XamContentCreateEx`), the song list's
  "descargas" tab lists their songs (All My Life, The Pretender, This Is a Call, Slither...), and a
  downloaded song (All My Life, Foo Fighters) plays. The content check that refuses Dance Central's
  packages doesn't apply.
- **Two players**: with `[players] p1 = { device = "pad" }, p2 = { device = "pad2" }`,
  Multijugador asks each player to move their controller to their side ("Seleccionar mando"), then
  offers Duelo, Duelo Pro, Guerra (battle) and Modo cooperativo; Duelo with two controllers goes
  through both players' characters, the venue, the song and both difficulties, and the face-off
  plays with two highways.
- **Audio and sync**: with the XMA functions, the songs and menus have sound (the mix's peaks during
  a song 0.14-0.46); the audio clock runs at the 187.5 frames a second XAudio needs, with no
  dropouts logged. Whether the notes stay in time with the music over a whole song, and the
  latency, need a person listening: the scripted runs can't hear.
- **Not tried yet**: star power (and which button it is on a controller), co-op career, the battle
  mode, a whole career gig played to its end.

## Routes (scripted input)
With a user folder past the first start (times from the first input poll; with
`ANYX360_INPUT_CLOCK=frames:60` they count the game's frames, so a slow start doesn't shift them):

- **Main menu**: `ANYX360_INPUT=15:START,20:START,25:A,30:START,40:A` (skips the intro, the safety
  notice and the title screen; the menu is up at about 45 s).
- **Practice, Slow Ride to its results**: then `50:UP,51:UP,52:UP,53.5:A,57:A,61:A,64:A,67:A,70:A,73:A`
  (Ensayo, Práctica, Slow Ride, Guitarra, Medio, Tema entero, normal speed); the results come about
  four minutes later. The menu remembers the last entry chosen, so from a user folder that last
  went elsewhere the UP presses land on another entry.
- **Quick Play**: `DOWN,DOWN,A` from Carrera, then A on the song and A on the difficulty.
- **Downloaded song**: in the song list LB opens the "descargas" tab, then A on the song and on the difficulty.
- **Face-off**: Multijugador is DOWN x3 from Carrera; at "Seleccionar mando" player 2 presses DOWN
  (`DOWN@1`), then A and `A@1`; A for Duelo; A and `A@1` twice (characters, "Actuar"); A on the venue
  and the song; A and `A@1` on the difficulties.

## Reproducing
From the archive's contents (`<package>` is the folder that holds `415607F7`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\415607F7\00007000\<file>" extracted\gh3\game
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\TU_10LC1VN_0000008000000.00000000000O2" extracted\gh3\update
build\tools\x360tool\release\x360tool.exe xex-patch extracted\gh3\game\default.xex extracted\gh3\update\default.xexp extracted\gh3\update\default_0.0.3.2.xex
build\tools\x360tool\release\x360tool.exe xex-extract extracted\gh3\update\default_0.0.3.2.xex extracted\gh3\default_patched.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\gh3\default_patched.bin
cd games\gh3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\gh3\update\default_0.0.3.2.xex switch_tables.toml
(fix the entry base = 0x8222C27C by hand, see above)
mkdir recomp3
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release gh3
```

To play a development build, the update's files go in
`<user>\content\000B0000\TU_10LC1VN_0000008000000.00000000000O2\` and each downloadable content
package, extracted (`x360tool stfs-extract`), in `<user>\content\00000002\<package file name>\`
(the launcher does both).
