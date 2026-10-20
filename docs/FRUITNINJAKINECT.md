# Fruit Ninja Kinect: target notes

These are working notes for the fourth title and the first Xbox Live Arcade one: Fruit Ninja Kinect.
Every value below comes from its `default.xex`, read with `x360tool xex-info`. The full dump is in
[games/fnk/notes/xex-info.txt](../games/fnk/notes/xex-info.txt).

| Item | Value |
|---|---|
| Developer | Halfbrick Studios (Microsoft Studios, Xbox Live Arcade, 2010) |
| Title ID / media ID | `58410B79` / `5DF68240`, version 0.0.0.3 |
| Source used | The **Xbox Live Arcade** package (an STFS "LIVE" package, content type `0xD0000`), eight DLC packs and a Title Update |
| XEX | AES-encrypted with the retail key, LZX compression, base `0x82000000`, entry `0x821AE500` |
| Image | 9 MiB: 6.3 MiB code (`.text`), 1 MiB data, 1.3 MiB read-only |
| Functions | 32,125 |
| XDK | 2.0.20500 (February 2011); links NUI and ST 2.0.20500, XACT3, XAudio2 and XMCORE |
| Imports | `xboxkrnl.exe`: 153 functions; `xam.xex`: 106 functions ([list](../games/fnk/notes/imports.txt)) |
| Target version | The base package and its DLC; no Title Update |

## The arcade package
An Xbox Live Arcade game is one STFS package whose files are the game folder. `x360tool
stfs-extract` with `--verify` checks every block's hash and extracts it; the DLC packs (content type 2) are
STFS packages too. The launcher does the same on "Add game": it recognises the package by its
content type, extracts it as the game, puts the DLC in the game's content folder
(`%LOCALAPPDATA%\AnyX360\FruitNinjaKinect\content\00000002\`) and skips the Title Update.

The executable is told it runs from a download (`GameDescriptor::arcade`):
`XTitleGetDeploymentType` answers 2 and `XContentQueryVolumeDeviceType` the hard drive. The license
is always full, so the game is never a trial; the marketplace screens open and close at once.

## Compressed files
Most of the game's files (models, effects, fonts, sound banks, `particles.fnk`) are compressed with
XMemCompress for the title's own code to expand as it reads them: they start with `0FF512ED`, and
XAPI asks the file system whether a file is compressed (`NtQueryInformationFile` class 27,
`FileXctdCompressionInformation`). The answer now comes from the file's header; answered "no", the
game read compressed bytes and loaded `particles.fnk` again and again behind the Halfbrick logo.

## Recompiler configuration
- **Register save/restore helpers**: found with `tools/scripts/find_helpers.py`; the addresses are
  in [config.toml](../games/fnk/config.toml).
- **Jump tables**: the fork's dataflow detector finds 181; two table-driven `bctr` stay unresolved,
  listed at the end of [switch_tables.toml](../games/fnk/switch_tables.toml).
- **Instructions**: the game needed `cctph`, `cror`, `crorc`, `lwaux`, `vaddsbs`, `vavguh`,
  `vcmpgtsw(.)`, `vmaxsh`, `vminsh`, `vpkshss(128)` and `vpkswus128`, and the record forms of
  `vcmpgtub.` and `vcmpgtuh.` now set CR6; all added to the fork.
- **Fall-through between functions**: the analysis ends a function where a branch lands on the
  next address, and the path that didn't branch returned early instead of going on. In XACT, an
  infinitely looping sound (`823FB200`, `bne cr6,0x823FB248` right before 823FB248) never got its
  loop fields, and the game crashed some 20 s into a round. The fork now continues into the next
  function; about 230 functions of this game end that way.

## Kinect
The NUI library is linked into the executable, a later build (2.0.20500) than Kinect Adventures'
and Dance Central 3's, and sits low in the image (`821B2700`-`821D3000`). The same generic virtual
sensor ([runtime/src/nui](../runtime/src/nui)) replaces it at its API boundary. The bindings are in
[games/fnk/src/nui_bindings.cpp](../games/fnk/src/nui_bindings.cpp); the names, signatures and
evidence are in [nui_api.md](../games/fnk/notes/nui_api.md).

- **The same frame layouts** as the other two games. The game opens the depth stream with player
  indices (320x240) and draws the player's shadow behind the fruit from it; the color stream only
  when asked.
- **Players and profiles**: the game pairs each signed-in player with a body by the skeleton's user
  index. The virtual sensor now fills it as the XDK does (`XUSER_INDEX_NONE` for bodies nobody is
  bound to; the fully tracked bodies bound in order to the signed-in players), and FNK's start-up
  gets the system's "NUI binding changed" notification. Without them the main menu found no
  player and crashed.
- **Calibration**: on the title screen the game aims the camera so the top of the view is half a
  metre above the player's head. There is no motor: the automatic tilt finishes at once and the
  framing check passes, as the player is framed by their own webcam.
- **Slicing**: the blade follows the hand on a frame tied to the player's body (shoulders near the
  top of the screen); fast movements cut. The scripted player cuts with sudden arm moves, e.g.
  `arm_right_100_-70` then `arm_right_100_30` (a chop through the middle of the screen).

## Reproducing
From the arcade package (`<package>` is the folder that holds `58410B79`):

```
build.bat
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\58410B79\000D0000\<file>" extracted\fnk\game --verify
cd games\fnk
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\fnk\game\default.xex switch_tables.toml
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat
```

XenonRecomp writes into `out_directory_path` from `config.toml`, and that folder must exist and be
empty before it runs: files left from an earlier run break the link, and a missing folder makes it
stop without a message. To try the DLC in a developer build, extract each pack from
`<package>\58410B79\00000002\` with `stfs-extract` to
`%LOCALAPPDATA%\AnyX360\FruitNinjaKinect\content\00000002\<package file name>\`.

## Bring-up notes
- **`ObOpenObjectByName`**: the game's worker threads open a semaphore by name.
- **Compressed files**: see above.
- **The automatic tilt is asynchronous**: it returns "pending" (997) and completes an XOVERLAPPED
  that the game polls; answered "done" at once, the game never ran its completion and the title
  screen said "CALIBRATING" for good.
- **Saving**: the profile settings (`XUSER_PROFILE_SETTING`) were read and written with the wrong
  layout (the setting at +0x0C instead of +0x10): no title's progress was ever saved, in any game.
  Fixed; FNK keeps its progress and best scores in three 1000-byte title settings.
- **"Gamer profile unavailable"**: `XSessionLeaveLocal` and `XSessionWriteStats` now succeed
  (offline sessions are allowed; there are no leaderboards). `XamWriteGamerTile` (the title setting
  the player's gamer picture) is done, and called without an overlapped it leaves its result as the
  thread's last error (`KTHREAD+0x160`): the game reads that instead of the return value, and a
  stale error from an earlier call made it show the dialog.
- **Pacing**: one vblank per frame: 60 FPS.

## Status (2026-10-20)
- **Boot, calibration, menus, DLC**: the game boots to its title screen, calibrates, and the menus
  work by slicing their fruit. The eight DLC packs are found and mounted.
- **Arcade and Classic**: whole rounds play with the scripted player: fruit, bombs, the Freeze
  banana, combos, the final pomegranate, the results screen; the best score is kept between
  sessions.
- **To verify with a real webcam and a person**: Zen mode, Party mode (two players), the Kinect
  Tuner (Y), how the shadow and the blade feel with a real body.
- **Not supported**: leaderboards and anything else online, avatar awards, gamer pictures.
