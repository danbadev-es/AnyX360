# Minecraft: Xbox 360 Edition: target notes

These are working notes for the twelfth title, the fourth from Xbox Live Arcade: Minecraft: Xbox
360 Edition. It is played with its last title update, which carries most of the game. Every value
below comes from the updated `default.xex` (`x360tool xex-patch`, then `xex-info`); the full dump is
in [games/minecraft/notes/xex-info.txt](../games/minecraft/notes/xex-info.txt).

| Item | Value |
|---|---|
| Developer | 4J Studios / Mojang (Microsoft Studios, Xbox Live Arcade, 2012-2018) |
| Title ID / media ID | `584111F7` / `7CD33B56` |
| Source used | The **Xbox Live Arcade** package (content type `0xD0000`, 118 MB), its title update `tu00000001` (1.35 GB) and 92 downloadable content packs |
| Versions | The package's 0.0.0.1; the update makes 0.0.80.1, the version played |
| XEX (updated) | Not encrypted once patched, base `0x82000000`, entry `0x82B5FB50` |
| Image | 39 MiB (the base's is 43 MiB): 10.2 MiB code, 1 MiB data, 1.6 MiB read-only |
| XDK | 2.0.21119; links XUI, XAudio2, XACT3, QNet, Miles Sound System |
| Imports | `xboxkrnl.exe`: 148 functions; `xam.xex`: 123 functions ([list](../games/minecraft/notes/imports.txt)) |

The arcade package holds the original game; the update brings the 0.0.80.1 executable
(`default.xexp`), the textures, the sound banks (`Sound/Minecraft.msscmp`), the music, the
built-in skin packs and the mash-up packs' data (`res/DLC`). The downloadable content packs carry
the rest of each pack (the mash-up worlds and texture packs) and unlock it.

## Status (2026-10-29)

- **One player**: the 4J logo, the autosave notice, the main menu; "Play Game" asks about the
  offline profile ("Continue offline"); the Load, Create and Join tabs; a new world generates
  (25 s on an idle machine, 2-5 min on a busy one) and plays at 60 FPS: walking, mining, placing
  blocks, the inventory. The pause menu's "Save game" saves; the Load tab lists the world with its
  name and seed, and loads it.
- **Split screen, up to four players**: in a game, a second controller's START shows "Press START
  to join"; START again (within about 2 s) signs that player in (XamShowSigninUI) and splits the
  screen. Two and four players were tried: every player moves, mines and builds. Two views draw
  about 2,300 times a frame; on the busy test machine frames were uneven (mean 12.7 ms, p99 31 ms),
  and four views ran at about 37 FPS.
- **Downloadable content**: all 92 packs mount (skins, texture packs, mash-ups, battle and glide
  maps, bundles). The mash-up worlds appear in the Create tab; the Skyrim mash-up (its world and
  texture pack) was created and played. The other packs have not been played one by one.
- **Not available**: Xbox LIVE (online games, Join, the store, leaderboards).

## Recompiler configuration

- **The title update**: XenonRecomp applies `default.xexp` to the package's `default.xex`
  (`patch_file_path`, `patched_file_path` in [config.toml](../games/minecraft/config.toml)), as for
  Kinect Adventures. The update's image (0x2720000) is smaller than the base's (0x2B50000), which
  the patcher refused as an invalid executable until fork commit b5c67ca (the buffer now holds the
  larger of the two). The runtime and `x360tool xex-patch` share that patcher.
- **Register save/restore helpers** and **setjmp/longjmp** (`82B96480`, `82B95F50`: the CRT's, which
  also save v14-v31): found with `tools/scripts/find_helpers.py` on the patched image.
- **Jump tables**: 270, all from the fork's dataflow detector; none unresolved.
- **Instructions**: nothing new.

## Saves

Worlds are content packages of type 1 (`Save<date>.bin`, holding `savegame.dat`), and the game
keeps a list of them with their thumbnails in another package, `_MinecraftSaveInfo`. Two things were
needed:

- **The disposition of an asynchronous XamContentCreateEx**: Minecraft opens `_MinecraftSaveInfo`
  with an XOVERLAPPED and reads it only if the result's length says the package was opened (2) and
  not created (1). The runtime completed every request with a length of 0, so the game rebuilt the
  list, flagged it as changed, and started again: the Load tab spun forever. The overlapped's length
  is now the disposition, as on the console (all games).
- **Thumbnails**: XamContentSetThumbnail and XamContentGetThumbnail keep a package's thumbnail as
  `__thumbnail.png` in its folder. The world list stores each world's thumbnail, and the game keeps
  the world's seed and host options in that PNG's text chunks. The pictures themselves come out
  black (see Pending).

## Split screen

Nothing in the runtime had to change. With a second controller (or `players.p2` set to a pad):

1. In a game, player 2 presses START: the game shows "Press START to join".
2. START again within about 2 s: the game calls XamShowSigninUI(1, 0x1010001). The runtime signs
   the first free player in (`<user>\players\E000000012345679\` for player 2), the game adds the
   player and the screen splits.

Players 3 and 4 join the same way. A player not signed in at startup is fine; the profile is
created on the first join.

## Downloadable content

Minecraft finds each pack's details (its offer, its world, its texture pack) in
`res/DLC/X360ProductCodes.bin` by the pack's **display name** ("Skin Pack 2", and some names end in a
space, "Marvel Guardians of the Galaxy "). The content enumerations gave the folder name (the
package's file name), the lookup failed, and the game read through a null pointer while mounting the
second pack (`8280FB7C`, a load from address 0xA0). The launcher now keeps each pack's display name
where the runtime keeps those of the packages a title creates
(`<user>\content\Headers\00000002\<package>.header`), and `.anyx` backups carry them
(`content_names`). Packs installed by an earlier launcher have no name: add the content again. (The
runtime can't recover the name itself: the launcher keeps only the extracted files, not the
package's header.)

## Routes (scripted input)

Times are seconds after the first input poll (`ANYX360_INPUT`). The menus ignore presses while they
load, so the gaps are wide; on a busy machine use wider ones.

- **To the Load tab**: `25:A,35:A,45:DOWN,48:A` (autosave notice, Play Game, "Continue offline").
- **A new world**: then `60:RB,66:A,74:DOWN~0.7x6,80:A` (the Create tab, "Create new world", down to
  "Create new world" and A); in the world about 25 s later on an idle machine.
- **Load the first world**: `25:A,35:A,45:DOWN,48:A,62:A,72:A`.
- **Save**: in a game, `START`, then `DOWN~0.6x3` and `A` ("Save game"); a second save asks to
  overwrite (DOWN, A).
- **Split screen**: after loading, `105:START~1.5x2@1` (player 2 joins), then for example
  `135:LUP/3,135:LUP/3@1,143:RT/3,143:RT/3@1,150:LT~0.6x4,150:LT~0.6x4@1` (both walk, mine and
  place blocks). Players 3 and 4: `@2`, `@3`.
- **Syntax**: in a press, the repeat goes before the player: `START~1.5x2@1`. Written
  `START@1~1.5x2`, the repeat is read as part of the player number and dropped: player 2 pressed
  START once, and never joined.
- **Test settings**: give player 1 a pad (`[players] p1 = { device = "pad" }`). A keyboard-and-mouse
  player reads the keyboard globally, so typing in another program opened the game's menus during
  unattended runs.

## Pending

- **Thumbnails are black**: the 64x64 PNG the game makes for each world is black, so the Load tab
  shows a black square. The game draws it from a frame on the GPU (a resolve); not looked at yet.
- **Split screen frame rate**: two views run uneven on a busy machine (above); not measured on an
  idle one.
- **Downloadable content**: only the Skyrim mash-up was played; skins, texture packs and the
  battle and glide maps were mounted but not tried one by one.
- **Xbox LIVE**: not available (online games, the Join tab, the store, leaderboards).

## Reproducing

From the arcade package (`<package>` is the folder that holds `584111F7`):

```
build.bat
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\584111F7\000D0000\<file>" extracted\minecraft\game --verify
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\584111F7\000B0000\tu00000001_00000000" extracted\minecraft\update --verify
build\tools\x360tool\release\x360tool.exe xex-patch extracted\minecraft\game\default.xex extracted\minecraft\update\default.xexp extracted\minecraft\update\default_0.0.80.1.xex
cd games\minecraft
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\minecraft\update\default_0.0.80.1.xex switch_tables.toml
mkdir recomp
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release minecraft
```

To run a developer build, put the update's files in
`%LOCALAPPDATA%\AnyX360\Minecraft\content\000B0000\tu00000001_00000000\` (or
`<user>\content\000B0000\...` with `--user`). The downloadable content goes, extracted, in
`<user>\content\00000002\<package file name>\`, each with its display name in
`<user>\content\Headers\00000002\<package file name>.header` (the launcher does both).
