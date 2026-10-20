# Call of Duty: Black Ops II: target notes

These are working notes for the eighth title, the second Call of Duty after Modern Warfare 3. Every
value below comes from its `default.xex`, read with `x360tool xex-info`. The full dump is in
[games/bo2/notes/xex-info.txt](../games/bo2/notes/xex-info.txt).

| Item | Value |
|---|---|
| Developer | Treyarch (Activision, 2012) |
| Title ID / media ID | `415608C3` / `457AD131`, version 0.0.0.14, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 399 files, 7.33 GiB), 8 DLC packages, 30 avatar items and a Title Update; the Latin American Spanish release |
| XEX | `default.pe`: AES-encrypted with the retail key, **basic** compression, base `0x82000000`, entry `0x825A5A20` |
| Image | 36.44 MiB: 6.12 MiB code (`.text`, `.interpr`, `BINK` and `RADCODE`), 28.88 MiB data, 1.44 MiB read-only |
| Functions | 35,246 (17,328 in `.pdata`) |
| XDK | 2.0.21119; links D3D9, XGRAPHC, XAPILIB, XNET, XONLINE, XPARTY, XMP, XHV2, XAUDIO2, XAPOBA, XMCORE and LIBCPMT; compiler 16.0.11886 |
| Imports | `xboxkrnl.exe`: 156 functions and 12 variables; `xam.xex`: 96 functions ([list](../games/bo2/notes/imports.txt)) |
| Target version | The single-player executable of the base game; no Title Update |

## The executables
The game folder holds two executables with the same title ID, media ID and version:

| | `default.xex` | `default_mp.xex` |
|---|---|---|
| Modes | The campaign (and Strike Force) | Multiplayer and Zombies |
| PE name | `default.pe` | `default_mp.pe` |
| File size | 9,228,288 bytes | 10,379,264 bytes |
| Base / entry | `0x82000000` / `0x825A5A20` | `0x82000000` / `0x8269D6F0` |
| Image | 36.44 MiB (code 6.12, data 28.88, read-only 1.44) | 41.69 MiB (code 7.06, data 33.06, read-only 1.56) |
| Import records | xam 192, xboxkrnl 324 | xam 252, xboxkrnl 387 |
| Libraries | see above | the same plus XCAM, XHTTP and XAUTH; D3D9LTCG and LIBCMT instead of D3D9 and LIBCPMT |
| Other | | title workspace `0x100000` |

Its header is in [games/bo2/notes/xex-info-mp.txt](../games/bo2/notes/xex-info-mp.txt). Only
`default.xex` is recompiled. Its main menu offers Multiplayer, Campaign and Zombies: Multiplayer
calls `XamLoaderLaunchTitle("default_mp.xex", 0)`; Zombies first passes 1000 bytes with
`XamLoaderSetLaunchData`, then launches the same executable. Both end the process for now.

The game's data are fast files (`*.ff`; the Zombies maps are `zm_*.ff` in the same folder), image
packs (`*.ipak`; `base.ipak` is 2.6 GB), sound banks (`stream\*.sabs`, `load\*.sabl`), videos in
WebM (`*.webm`: the opening and the loading videos) and Bink (`*.bik`), and `weapons.clump`.

The package's other content: 8 DLC packages (content type 2): two licences of 57 KB ("Season Pass"
and "Premium Content 1") and six content packages of 129 MB to 2.13 GB (presumably the map packs);
30 avatar items (content type `0x9000`) and a Title Update (`000B0000`, 51 files, 85.74 MiB:
`default.xexp`, `default_mp.xexp`, patch fast files and `patch_all.ipak`). The game asks for
`update:\patch.ff` and `update:\frontend_patch.ff`, which aren't mounted. None of them is used yet.

## Recompiler configuration
- **Register save/restore helpers** and **setjmp/longjmp** (`826CA660`, `826CAAF0`, the same CRT
  code as Modern Warfare 3's, apart from a global they read and their calls; 18 calls to setjmp, 12
  to longjmp): [config.toml](../games/bo2/config.toml). find_helpers.py's other longjmp candidate,
  `8258D760`, is an ordinary function.
- **Jump tables**: 222, all from the dataflow detector. The 11 table-driven `bctr` left are tail
  calls through tables of function pointers in `.data`, correct as indirect calls.
- **One function given by hand**: `sub_82539CB8`, a leaf function with a switch whose other cases
  count down CTR with `bdz`. The last `bdz` targets the next instruction, which the analyser took
  for an empty block: it cut the function there and the switch's default case fell outside it. Of
  the 16 conditional branches to the next instruction in the image, only this one cut a function.
- **Instructions**: `vmaxsb` and `vrfip128`, added to the fork (branch `anyx360-bo2`).
- All four code sections are recompiled, including `.interpr` (26 KiB) and the RAD tools' `RADCODE`.

## Bring-up
- **Unset profile blobs**: at boot the game reads eight profile settings and verifies the
  signature (SHA-256 and RSA, its own libtomcrypt) of each title-specific blob it is given a
  pointer to. A blob never written came back as a pointer with length 0, and the game hashed
  `length - 64` bytes: a crash after a few seconds. A setting with no value now has no data
  pointer, as in Xenia.
- **Media verification**: before loading each level the Stream thread calls
  `XamMediaVerificationVerify`, found by ordinal with `XexGetProcedureAddress` rather than
  imported, then sleeps alertably until its callback has run. The XDK's wrapper takes a missing
  export for success, so the game slept forever on a black screen after "Start level". The four
  media verification exports now exist (a copy on the hard disk has nothing to verify; the
  callback runs as a user APC, an overlapped is completed), and `XexGetProcedureAddress` returns
  them: their guest address is a word of padding in the title's code whose function table slot
  calls the export. Gears of War 3 looks them up too, with an overlapped instead of a callback.
- **Kernel exports**: the incremental SHA functions (`XeCryptShaInit/Update/Final` and the
  SHA-256, SHA-384 and SHA-512 ones, the context in guest memory), `MmQueryStatistics` and
  `XGetVideoCapabilities`.
- **Threads**: the game places them itself (`XSetThreadProcessor`): Database on hardware thread 0,
  Server on 1, Stream and JQ Worker 1 on 2, JQ Worker 2 on 3, JQ Worker 3 and Sound Mix on 4,
  Backend on 5. The main thread starts on 0, as on the console
  (`GameDescriptor::main_thread_processor`, as for Modern Warfare 3); 1 wasn't tried.
- **Pacing**: one vblank per frame: 60 FPS in the menus and the level.
- Still asked for and missing, harmlessly: xam ordinals 1164 (`XamVoiceSetMicArrayIdleUsers`),
  80 and 36 (`NetDll_XNetStartupEx`, `NetDll_WSAStartupEx`), by ordinal as well, and
  `XexLoadImage("xam.xex")`.

## Status (2026-10-20)
- **Boot and menus** (scripted pad): the opening videos (Dolby, Treyarch, the intro), the story
  cinematic, the title screen ("Press START"), the first-run setup (welcome, brightness, sound
  settings, safe area), the main menu, Campaign, New Story and the difficulty choice, the briefing
  voice-over over the globe and the mission briefing for Pyrrhic Victory (Angola 1986) with its
  tutorial box. The settings (profile settings) and the save (`savegame.svg`, `savegame.foo`) are
  written.
- **Campaign**: Pyrrhic Victory loads with its video (Woods' interview), plays its in-engine
  opening (Savimbi's convoy) and hands over: the scripted player follows the convoy, aims, fires,
  picks up ammunition, reaches checkpoints, dies (it walks into the open) and restarts from the
  checkpoint; the HUD, compass, objectives and audio (XMA) work. 57-60 FPS, with stalls of up to
  1.5 s around loading. A scripted session of ten minutes in the level (moving and firing in a
  loop) ran without a crash and reached the next objective.
- The text and voices are Latin American Spanish: the copy has only those sound banks.
- **Known issues**:
  - Multiplayer and Zombies start `default_mp.xex`, which isn't recompiled: the process ends.
  - The Title Update isn't applied (`update:\` isn't mounted).
  - One long session slowed to a few frames per second for about 20 s, nine minutes in, as the
    action moved on (a helicopter, new effects); the same route again, with the shaders cached,
    didn't. Possibly the host compiling new shaders, or another program loading the machine.
  - Not tried: the rest of the campaign, Strike Force missions, split screen, the DLC, long
    sessions; whether Modern Warfare 3's occasional black screen at start happens here (not seen
    in about twenty starts).

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `415608C3`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\415608C3\00007000\<file>" extracted\bo2\game
build\tools\x360tool\release\x360tool.exe xex-extract extracted\bo2\game\default.xex extracted\bo2\default.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\bo2\default.bin
cd games\bo2
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\bo2\game\default.xex switch_tables.toml
mkdir recomp
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release bo2
```

XenonRecomp writes into `out_directory_path` from `config.toml`, and that folder must exist and be
empty before it runs.

**Scripted route** to the first level (a new user folder; times from the first input poll, with a
warm shader cache):

```
ANYX360_INPUT=15:START,23:START,31:START,50:LRIGHT/0.5,54:A,60:A,66:A,72:A,80:A,86:A,92:A,135:A,141:A,150:A
```

START skips the opening videos and the story cinematic and passes the title screen; A answers the
welcome box, the brightness, sound settings and safe area screens, picks Campaign and New Story and
takes the default difficulty (Regular). The briefing voice-over then plays over the globe and the
route waits for it to end (skipping it with A never reached the briefing in testing); A closes the
briefing's tutorial box and A starts the level. After the loading video and the opening cinematic
the player has control at about 285 s. To move and fire, for example:
`285:LUP/12,290:RT/0.4,293:RT/0.4,299:LT/3,300:RT/0.3,305:LUP/15`.
