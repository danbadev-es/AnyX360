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

## Avatars
Everyone in this game is an Xbox Avatar: the player (the profile's avatar, or a random one), the
opponents and the crowd. The avatar library is linked into the game, but the avatars' parts come
from the console's system software (`XamAvatarGetAssets`), which AnyX360 provides from the
console's **avatar asset pack** ([AVATARS.md](AVATARS.md)). This game doesn't carry the pack;
discs of avatar games do (Kinect Sports): the launcher installs it when such a game is added, and
a game that has one copies it on its first run, to `%LOCALAPPDATA%\AnyX360\system`. Without it
the game runs, with an invisible player.

The game's avatar library (2.0.11626) is older than Kinect Sports': it has no shaping of the
skeleton by height and weight, so the system gives every avatar the library's default skeleton
(`825F7EE0`). Its menus and first level build 16 avatars, each from its own description: the
crowd keeps its looks (hats, shades, watches and rings included).

## Status (2026-10-20)
- **Boot and menus**: the Behaviour logo, the title screen, the main menu, the country and level
  selection work with the pad. There is no Xbox LIVE: the level screen says the leaderboard can't
  be read, and starting a level asks to sign in (B continues offline).
- **Levels**: a level loads (the loading screen shows the controls), the player's avatar runs,
  jumps and falls into the water, comes back at the last checkpoint, and the game offers "Stuck?
  Press RB to skip ahead". The race timer runs and the camera follows. 60 FPS. The scripted pad
  got a third of the way through the first level (a person finishes levels).
- **Not tried**: finishing a level (the results), the DLC pack, local multiplayer, the Title
  Update (not supported).

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
