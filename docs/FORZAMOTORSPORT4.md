# Forza Motorsport 4: target notes

These are working notes for the thirteenth title, Turn 10's racing game. Every value below comes
from its `default.xex`, read with `x360tool xex-info`; the full dumps are in
[games/fm4/notes/](../games/fm4/notes/).

| Item | Value |
|---|---|
| Developer | Turn 10 Studios (Microsoft Studios, 2011) |
| Title ID / media ID | `4D530910` / `42C67824`, version 0.0.0.15, disc 1 of 2 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 4,074 files, 7.48 GiB), the second disc's content as four packages and 17 downloadable content packs; the Spanish release. No title update in the copy |
| XEX | `default.exe`: AES-encrypted with the retail key, **basic** compression, base `0x82000000`, entry `0x8275F9F0` |
| Image | 15.31 MiB: 10.69 MiB code (`.text`, `PAGELK`, `_TEXT`), 1.31 MiB data, 3.31 MiB read-only |
| XDK | 2.0.20764; links D3D9, D3DX9, XGRAPHC, XNET, XONLINE, XMP, XHV2, XPARTY, NUI, NUIHNDL, ST, MFPLAT, XAUDIO2, XFFB and LIBCMT |
| Imports | `xboxkrnl.exe` and `xam.xex`: 427 and 346 import records ([list](../games/fm4/notes/imports.txt)) |
| Its own DLLs | `XMediaFacade_default.xex` (base `0x88000000`: the XDK's WMV decoder, for the videos) and `SpeechFacade_default.xex` (`0x89000000`: Kinect voice commands) |

The game's data are zip archives (`Media\UI.zip`, `Media\tracks\<track>\bin.zip`...), `.fsb`
sound banks, WMV videos (`Media\UI\videos`) and `Database.xmplr`.

## The copy
The Games on Demand package holds the first disc. The second disc (on the console, "Content
Install") comes as four content packages of type 2 (`4d53091000000001` to `...04`: the
1950-2000, 2001-2005 and 2006-Current model year car packs and the Autovista car pack, 2.85 GB),
which the launcher installs as downloadable content, with the 17 packs of the copy (the monthly
car packs, the Porsche Expansion, the Launch Track Pack...). The game mounts each as
`DLCRoot<n>:` at start and reads its tunable settings (`media\dlczips\...`). The 22 avatar items
(`00009000`) aren't used.

## Recompiler configuration
- **Register save/restore helpers** from find_helpers.py; **setjmp/longjmp** `82768B30`/`82768700`,
  checked by hand (the CRT's, as Modern Warfare 3's). The other longjmp candidates are ordinary
  functions.
- **Jump tables**: 394, from the dataflow detector; 14 table-driven `bctr` left (not looked at:
  nothing has reached them).
- **Instructions**: `lvxl128` and `stvlxl128` (64 of each), the least-recently-used forms of
  `lvx128` and `stvlx128`: added to the fork (branch `anyx360-fm4`).
- **The DLLs** ([config_xmedia.toml](../games/fm4/config_xmedia.toml),
  [config_speech.toml](../games/fm4/config_speech.toml)): recompiled with their own helpers and
  jump tables (26 and 20) and a `symbol_prefix` (`xmedia_`, `speech_`), new in the fork: the
  entry point, the helpers and the function mappings get the prefix, so they link next to the
  game's. See [Title DLLs](#title-dlls).

## Title DLLs
The game loads both DLLs at start (`XexLoadImage("game:\xmediafacade_default.xex")`) and, when
one can't be loaded, stops with the disc error. The runtime now loads a title's own DLLs that are
recompiled into its executable (`GameDescriptor::modules`, `anyx360_add_title_module` in
[cmake/Game.cmake](../cmake/Game.cmake)):

- The DLL's sources are compiled with the **title's** `ppc_config.h`, so an indirect call in the
  DLL or in the title looks a DLL function up where the title's table layout puts it: the
  function table simply goes on past the title's code (the slots for `0x88000000` are at
  `0x8EA10000`, those for `0x89000000` at `0x90A10000`, in the XEX heaps where nothing else is
  allocated).
- `XexLoadImage` maps the image at its base, fills those slots, resolves the DLL's imports, makes
  a loader entry (the handle) and runs the entry point (`DLL_PROCESS_ATTACH`).
  `XexGetProcedureAddress` reads the DLL's XEX export table (security info + 0x160);
  `XexGetModuleHandle` finds it by name.

With it the videos play (the logos, the attract video, the Top Gear introduction). SpeechFacade
isn't loaded while the game is told there's no Kinect (below).

## Bring-up
- **Kernel**: `NtSignalAndWaitForSingleObjectEx` (the video decoder's threads),
  `XeKeysObscureKey` and `XeKeysAesCbcUsingKey` (the profile save is encrypted with an obscured
  key; without them the first save was written in the clear and the next start hung on "Saving
  content"), `MmCreateKernelStack`, `MmDeleteKernelStack`, `KeSetCurrentStackPointers`,
  `ObCreateObject` (objects of the game's own types: it crashed on a null object).
- **ExCreateThread flag 0x80**: the caller gets the KTHREAD, not a handle (as Xenia): the game's
  audio thread is created suspended with it and started with `KeResumeThread`.
- **The utility drive**: the XDK's `XMountUtilityDrive` opens `\Device\Harddisk0\Partition0`
  (now a file with nothing behind it, as Xenia's null device), asks `FileFsDeviceInformation` and
  mounts `\Device\Harddisk0\Cache1`.
- **No Kinect**: `GameDescriptor::kinect_sensor = false`: no sensor is reported
  (`XamNuiGetDeviceStatus`, `PsCamDeviceRequest`). The title screen then says "Press A to start"
  instead of offering Kinect; told a sensor was there, the game also waited for its camera.
- `XamXStudioRequest` fails (as Xenia), `XamGetActiveDashAppInfo` gives zeros.
- Still missing, harmlessly so far: `EtxProducerRegister`; xam ordinals 442, 520, 80, 36, 1164 and
  xboxkrnl 642-649 by ordinal.

## Status (2026-10-29)
- **Works**: the legal screen, the Turn 10 and Microsoft Studios logos (videos), the title screen
  and its attract video, A at the title, the profile save ("Saving content", `ForzaProfile`,
  encrypted), the first-run brightness screen, the Top Gear introduction video with subtitles,
  then the first race's loading screen ("Welcome to Forza Motorsport 4", a lap of the Bernese
  Alps). 60 FPS, music and voices (XMA).
- **Stops at the first race's loading screen.** The screen animates for good (left 9 minutes),
  nothing more is read (the track's `bin.zip` is never opened; the last files are the Alps audio
  zones), and no thread is stuck in an export: the game thread waits each frame for the frame
  event, the main thread runs its loop (much of it in the CRT's `pow`, from `8287AE48`), every
  worker waits. Not the downloadable content (the same without it), not Kinect, not a button
  press. Next steps: find what the main loop's loading state waits for (`822DADD8` →
  `822A7308` → `8243F330`), and whether a GPU fence or XMA state is involved.
- Not reached yet: the menus after the first race, careers, Free Play, the garage, upgrades,
  tuning, the paint editor, the storefront, replays, Rivals, the downloadable cars and tracks.
- **Kinect** (Autovista, head tracking, voice) and **split screen**: later.

## Routes (scripted input)
On a new profile (an empty `<user>\content\00000001`), with `[players] p1 = { device = "pad" }`:
`ANYX360_INPUT=28:A,40:A,60:A~15x6` presses A at the title (about 28 s; earlier presses fall on
the logos), accepts the brightness screen, and the introduction plays by itself; the loading
screen comes at about 125 s.

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `4D530910`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\4D530910\00007000\<file>" extracted\fm4\game
build\tools\x360tool\release\x360tool.exe xex-extract extracted\fm4\game\default.xex extracted\fm4\default.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\fm4\default.bin
cd games\fm4
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\fm4\game\default.xex switch_tables.toml
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\fm4\game\XMediaFacade_default.xex switch_tables_xmedia.toml
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\fm4\game\SpeechFacade_default.xex switch_tables_speech.toml
mkdir recomp recomp_xmedia recomp_speech
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config_xmedia.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config_speech.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release fm4
```

The DLLs' helpers come from `find_helpers.py` on their images (`--base 0x88000000`,
`--base 0x89000000`). To play a developer build, the content packages go, extracted, in
`<user>\content\00000002\<package file name>\` (the launcher does it).
