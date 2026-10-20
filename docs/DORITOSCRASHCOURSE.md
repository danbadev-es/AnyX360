# Doritos Crash Course: target notes

These are working notes for the fifth title, the second from Xbox Live Arcade: Doritos Crash Course.
Every value below comes from its `default.xex`, read with `x360tool xex-info`. The full dump is in
[games/dcc/notes/xex-info.txt](../games/dcc/notes/xex-info.txt).

| Item | Value |
|---|---|
| Developer | Wanako Games / Behaviour Interactive (Microsoft Studios, Xbox Live Arcade, 2010) |
| Title ID / media ID | `58410A71` / `261A2859`, version 0.0.0.2 |
| Source used | The **Xbox Live Arcade** package (content type `0xD0000`), one DLC pack and a Title Update |
| XEX | AES-encrypted with the retail key, LZX compression, base `0x82000000`, entry `0x823E1790` |
| Image | 18 MiB: 8.4 MiB code (`.text`), 8 MiB data, 1.6 MiB read-only |
| XDK | 2.0.11626 (June 2010); links XAVATAR, XCAM, XHV2, XAudio2 and XMCORE; no NUI |
| Imports | `xboxkrnl.exe`: 153 functions; `xam.xex`: 92 functions ([list](../games/dcc/notes/imports.txt)) |
| Target version | The base package; no Title Update |

The game's own version string is "Doritos Avatar Crash Course"; its title screen shows the
*Crash Course 2* logo. A side-scrolling obstacle course for up to four players, with a controller.

## Recompiler configuration
- **Register save/restore helpers** and **setjmp/longjmp** (`825DAC40`, `825DAF10`, the same CRT
  code as Fruit Ninja Kinect's; the game calls them): found with `tools/scripts/find_helpers.py`,
  in [config.toml](../games/dcc/config.toml).
- **Jump tables**: 562 found by the fork's dataflow detector. This game needed two more shapes:
  tables right after the dispatch whose cases also go back to code before it (the bounds check
  of the switch before was taken for this one's), and tables of byte offsets with no bounds check
  at all, which have no known length: those are written with `dispatch = "address"` and the
  recompiler switches on the target address, calling anything else. The two left (`8282B478`,
  `8282D1E0`) are calls through tables of function pointers, correct as indirect calls.
- **Instructions**: nothing new.

## Status (2026-10-20)
- **Boot and menus**: the Behaviour logo, the title screen, the main menu, the country and level
  selection work with the pad. There is no Xbox LIVE: the level screen says the leaderboard can't
  be read, and starting a level asks to sign in (B continues offline).
- **Levels**: a level loads (the loading screen shows the controls), the race timer runs, the
  camera follows the player and checkpoints are reached. 60 FPS.
- **The player's character is invisible.** Everyone in this game is an Xbox Avatar: the player
  (the controller's, a random or the profile's avatar) and the opponents. The avatar library is
  linked into the game, but its models, textures and animations come from the console's system
  software through `XamAvatarGetAssets`/`XamAvatarLoadAnimation`, not from the game. With the
  avatar system unavailable (as in Xenia) the game runs without any; answered "available", it
  asks for the metadata and assets and still has nothing to draw. Showing avatars needs the
  console's avatar data and an implementation of the system's asset loading: not done.
- **Not tried**: the DLC pack, local multiplayer, the Title Update (not supported).

## Reproducing
From the arcade package (`<package>` is the folder that holds `58410A71`):

```
build.bat
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\58410A71\000D0000\<file>" extracted\dcc\game --verify
cd games\dcc
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\dcc\game\default.xex switch_tables.toml
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat
```

XenonRecomp writes into `out_directory_path` from `config.toml`, and that folder must exist and be
empty before it runs.
