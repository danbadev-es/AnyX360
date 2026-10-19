# Kinect Adventures: target notes

These are working notes for the third title, Kinect Adventures, the game that came with the
Kinect. Every value below comes from its `Default.xex`, read with `x360tool xex-info`. The full
dump is in [games/ka/notes/xex-info.txt](../games/ka/notes/xex-info.txt).

| Item | Value |
|---|---|
| Developer | Good Science Studio (Microsoft Game Studios) |
| Title ID / media ID | `4D5308ED` / `6D571F26`, version 0.0.0.3, disc 1 of 1 |
| Source used | A **Games on Demand** package: an STFS header plus 19 SVOD data files holding the disc image, and one DLC package |
| XEX | AES-encrypted with the retail key, basic compression, base `0x82000000`, entry `0x831B1D80` |
| Image | 29.9 MiB: 18.5 MiB code (`.text`), 7.6 MiB data, 2.8 MiB read-only |
| Functions in `.pdata` | 47,877 |
| XDK | 2.0.11775 (launch era); links NUI 2.0.11775 and the XDK's audio, network and party libraries |
| Imports | `xboxkrnl.exe`: 184 functions; `xam.xex`: 167 functions ([list](../games/ka/notes/imports.txt)) |
| Target version | Base executable and the "Exclusive Pre-order Pack" DLC; no Title Update |

## Games on Demand
The copy is not a disc folder. `4D5308ED\00007000\<hash>` is an STFS "LIVE" header (content type
0x7000) and `<hash>.data\Data0000`–`Data0018` hold the disc's GDFX image in SVOD form: data blocks
of 0x800 bytes with a hash block every 0x198, in files of 0xA290000 bytes. `x360tool` reads it:

- `content-info <package>` shows a package's header (type, title, volume) and its files.
- `god-extract <header> <out>` maps the GDFX sectors to the data files (Xenia's `BlockToOffset`,
  the EGDF and plain layouts) and extracts the disc's files.
- `stfs-extract <package> <out>` extracts an STFS package (the DLC) by following its block chains.

The launcher does the same on "Add game": it recognises a Games on Demand archive or folder by its
`<TitleID>\00007000\` header, converts it to a folder, and puts the DLC (content type 2) in the
game's content folder (`%LOCALAPPDATA%\AnyX360\KinectAdventures\content\00000002\`). A Title
Update and other games' demos found next to it are skipped.

## Sections
The section list is in [games/ka/notes/sections.txt](../games/ka/notes/sections.txt). Code lives
in `.text` (`0x82330000`–`0x835B0018`), `PSFD00`, `_TEXT` and `.embsec_`.

## Recompiler configuration
- **Register save/restore helpers**: found with `tools/scripts/find_helpers.py`; the addresses are
  in [config.toml](../games/ka/config.toml).
- **Jump tables**: the fork's dataflow detector finds 243; 20 table-driven `bctr` stay unresolved,
  listed at the end of [switch_tables.toml](../games/ka/switch_tables.toml).
- **Instructions**: the game needed `ldux`, `vsrab`, `vrlh`, `vsububm`, `vsubsbs` and a second
  form of `vpkd3d128`, added to the fork.

## Kinect
The NUI library is linked into the executable, as in Dance Central 3, but an earlier build
(2.0.11775). The same generic virtual sensor ([runtime/src/nui](../runtime/src/nui)) replaces it at
its API boundary. The bindings are in [games/ka/src/nui_bindings.cpp](../games/ka/src/nui_bindings.cpp);
the names, signatures and evidence are in [nui_api.md](../games/ka/notes/nui_api.md).

- **Same frame layouts as Dance Central 3**: `NUI_SKELETON_FRAME` with six 0x1C0-byte skeletons,
  `NUI_IMAGE_FRAME` with the texture after it.
- **Images**: color (640×480, 32-bit), depth with player indices (320×240) and a small player map
  (80×60). The formats are chosen in the library's `ImageStreamOpen` code rather than a table.
- **Smoothing**: the game runs the SDK's skeleton filter (`NuiTransformSmooth`, double exponential
  with a 5 cm jitter radius) on every frame. The bridge filters already, so it leaves the frame as
  it is (`nui::TransformSmooth`): filtering twice only added lag.
- **Identity**: the game asks the library to identify each player. The request is accepted and
  answered with the next skeleton frame: done, nobody enrolled. Refused, the game asked again and
  showed "IDENTIFYING" for good.
- **The Kinect Guide gesture** (the left arm held out and down at 45 degrees for 1.5 s) opens the
  system UI, as on the console: the game pauses and shows its pause menu. It is recognised by the
  virtual sensor, not the title.

The scripted player has the whole-body moves the activities need: `jump`, `crouch`,
`lean_left/right`, `step_left/right`, `walk_in/out`, `hands_up`, `arms_out` and `flap`
(`synthetic_source.cpp`).

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `4D5308ED`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\4D5308ED\00007000\<hash>" extracted\ka\game
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\4D5308ED\00000002\<hash>" extracted\ka\dlc
cd games\ka
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\ka\game\Default.xex switch_tables.toml
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat
```

XenonRecomp writes into `out_directory_path` from `config.toml` (`games\ka\recomp`), and that
folder must be empty before it runs: files left from an earlier run break the link. To play the
DLC in a developer build, copy `extracted\ka\dlc` to
`%LOCALAPPDATA%\AnyX360\KinectAdventures\content\00000002\<package file name>\`.

## Bring-up notes
- **XEX import names**: the import table packs `"xboxkrnl.exe\0\0E\0xam.xex\0"`; names are aligned
  to 4 bytes, which the reader didn't do (it took `E` for a library).
- **Notification listeners must be waitable**: the game waits on its listener handle with
  `NtWaitForMultipleObjectsEx`. A handle that isn't a kernel object made the wait return at once and
  the thread spun. Listeners are now events, set when a notification is queued.
- **XMsg**: two more XAM app messages (`XEnableGuestSignin`, `XTitleGetDeploymentType`).
- **DLC**: content names are 42 characters (they were cut at 41), marketplace content is shared by
  every player, and content paths use long-path names (the DLC's are over 260 characters).
- **Avatars**: unimplemented imports return 0, which the avatar calls read as success with nothing
  written, and Reflex Ridge crashed reading asset data that wasn't there. The avatar system now
  fails to initialise, as Xenia's default does, and its calls say there's nothing; the game plays
  with its stand-ins.
- **Pacing**: two vblanks per frame (`GameDescriptor::vblanks_per_frame`): 60 FPS.

## Status (2026-10-20)
- **Boot, menus, DLC**: the game boots to its title screen and the menus work with the scripted
  player's hand cursor (hold over an item to choose it). The DLC shows up.
- **Rallyball**: serving and hitting score.
- **River Rush**: a jump starts it, leaning steers, and pins are collected.
- **Reflex Ridge and Space Pop**: they load and wait at their start ("grab bars", "flap your arms
  to start"). The scripted player hasn't found the pose that starts them; this needs a person.
- **20,000 Leaks**: not tried yet.
- **Pause**: the Kinect Guide gesture pauses an activity; "Leave activity" goes back to the
  activity select.
- **To verify with a real webcam and a person**: the start of Reflex Ridge and Space Pop, jumping
  and ducking in Reflex Ridge, 20,000 Leaks, two players.
- **Not supported**: avatars (the game uses its stand-ins), voice, online play, photos uploaded
  to Kinect Share.
