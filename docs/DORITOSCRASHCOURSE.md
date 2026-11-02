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
| Target version | 0.0.2.2: the base package patched with its title update (since 0.17.0), with its DLC |

The game's own version string is "Doritos Avatar Crash Course"; its title screen shows the
*Crash Course 2* logo. A side-scrolling obstacle course for up to four players, with a controller.

## The title update (since 0.17.0)
The archive holds the game's title update in the console's form,
`58410A71\000B0000\tu00000002_00000000`: an STFS "LIVE" package of content type 0x000B0000, "Doritos
Crash Course Title Update", 8.8 MB, with `default.xexp` (a delta patch from 0.0.0.2 to 0.0.2.2),
`patch.pak` (4.6 MB of game data the game reads from `update:\`) and the avatar awards.

The update is a rebuild with a later XDK, 2.0.21173 (the base: 2.0.11626): the code moved
everywhere (entry `0x82265618`, image 18.75 MiB) and it has 33 more imports (sockets and XNet
helpers, the audio ducker, `XamContentFlush`, `XamTaskSchedule` and others), all in the runtime.

- **Recompiled from the update.** `config.toml` names the base, the patch and the patched
  executable; the register helpers and setjmp/longjmp (`824E41C0`/`824E4490`, the base's pair
  matched by instruction shape) are the patched image's. XenonAnalyse finds 614 jump tables, 2
  unresolved (calls through tables of function pointers).
- **Avatars**: the avatar library is 2.0.21173 now, but its default skeleton builder is the same
  code: `82506768` (the base's `825F7EE0`), matched by instruction shape, one match, checked over 16
  instructions. The players and crowd are built from Kinect Sports' asset pack as before.
- **Saves.** The update saves the player's progress (`dcc_game_progress`, a 4 KB encrypted save) from
  the title screen on (the base game didn't save there), and on the next start reads it back with an overlapped
  `ReadFile` on a file opened for asynchronous I/O, which must answer `ERROR_IO_PENDING`. The
  runtime completed the read at once and said so; the game took that as a failed read, said "Could
  not find device" and saved again and again. Reads on asynchronous files now answer
  `STATUS_PENDING` (already complete, as Xenia does), and a wait on a file returns at once. The
  runtime also keeps each save's display name (`content\Headers\<type>\<file>.header`, as Xenia
  keeps its headers) for the enumerators, which had been giving the file name, and delivers
  `XOVERLAPPED` completion routines as user APCs (the game enumerates its saves with one).
- **At start** the game patches the copy's `default.xex` (cached), mounts the update as `update:\`
  and reads `patch.pak` from there. Without it, it ends at once saying where the update goes; the
  launcher installs it from the archive (or the `tu00000002_00000000` package alone).

The addresses in the sections below are the base executable's (0.0.0.2) unless said otherwise.

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
the game runs, with an invisible player (on the title update since 0.34.0: see "Without any avatar
pack" below).

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
- **Not tried**: finishing a level (the results), the DLC pack, local multiplayer.

## Status with the title update (2026-10-28)
Prepared by the launcher from the archive (the copy, its DLC pack and the update; Kinect Sports'
avatar data beside it) and played on the build recompiled from 0.0.2.2 with the scripted pad: the
logos, the title screen, the save made at the first start and read back at the next (the save
menu, country and level selection), level 1 started offline (B on the sign-in question) with the
player's avatar running, jumping and the timer going, "Stuck? Press RB to skip ahead". The game
reads `patch.pak` from `update:\` (over 100 reads). Started without the update, it ends at once
with the message.

Scripted pad: `15:A,25:A,35:A,45:A,55:A,65:A,72:B,90:LRIGHT/50` and `A/0.5` every 3 s to jump.

## Finishing a level (2026-11-02)
Tried on release 0.33.0 from `dcc.anyx` with a scripted controller, live (`ANYX360_INPUT_LIVE`):
the main menu (Single Player is the top-left tile: `LEFT`, `LEFT` from where the cursor starts,
then `A`), USA, "USA 1: Safety First" (`A`, then `B` on the sign-in question), the level. Holding
right with a jump every 2 s (`LRIGHT/400`, `A/0.4~2x200`) the avatar ran the first conveyors and
ramps, past the silver time (1:25) at 1:33 near the first water gap, then fell into the water
again and again at the raised blocks after it, and the "RB to skip ahead" offer moved it on only
to the next fall: 4:33 in, still a third of the way. **Finishing a level needs a person** (timed
jumps over gaps the camera shows); nothing went wrong in the game (60 FPS, no error lines, the
avatar, the timer, the checkpoints and the respawns all worked).

## Audio (2026-11-01)
A player heard the sound late, with cuts (0.29.0). What the investigation found:

- **Two audio engines.** The game creates two XAudio2 engines, so the runtime has two render
  driver clients (callback `82530C60` for both, arguments `4001F820` and `4001F8A0`), each with its
  own host output. That is why the log shows 375 frames/s: 187.5 for each. The second one only ever
  submits silence. Minecraft has two clients too.
- **The host side keeps up.** Measured per client (a temporary probe, at 120 Hz and with a 6-job
  build running beside it): both mixed at 187.5 frames/s. Neither output ever had more than 2 of
  its 16 queued frames played and not yet replaced, so playback never ran dry. A mix callback takes
  0.15-0.4 ms on average and 3 ms at most: the game's XAudio2 mixes on its own threads and the
  callback hands over the finished frame. Each client's output, dumped to a file for 150 s
  (menus and the first level), has no silent gaps, no repeated frames and no sudden drops. So the
  cuts the player heard didn't come from the runtime's output on this PC. Before 0.32.0 the log's
  "playback ran dry" check timed all clients together, so it couldn't see one client starving
  while the other kept mixing.
- **Its frame rate.** The game presents every vblank: on the console it runs at 60 FPS. It had kept
  the runtime's default 120 Hz vblank, which suits titles that present every other vblank. So it
  ran at up to 120 FPS: 96-120 swaps/s here on a fast display. On a 60 Hz display it is held at 60
  by the presents (the player's log: 60.0 swaps/s), while its vblank interrupt still came 120
  times a second. The race timer kept time either way (30.5-30.7 s in 30 s), so the game has its
  own clock. It now has the console's 60 Hz vblank at a fixed 60 FPS (`vblanks_per_frame = 1`,
  `fixed_fps = 60`, as Guitar Hero III and Mortal Kombat): a steady 60.0 swaps/s, the timer
  unchanged.
- **Changes (0.32.0).** The game's sound now plays 43 ms sooner: 8 frames are queued ahead of
  playback instead of 16 (85 ms), with `GameDescriptor::audio_queued_frames`. Its mixing was never
  more than 2 frames late, which leaves 6 frames of slack. The runtime also warns for each client
  whose output plays every queued frame ("audio: client N ran dry"), so the next log from a PC
  with cuts will show whether the output starved.
- **Avatar pack.** Adding Halo: Reach replaced the shared avatar asset pack with its disc's older
  version (1), which the avatar system can't read. With it this game crashed on its first level
  (a null read in `sub_82877CC8`, thread 20/21), as it did with no pack at all. Since 0.32.0 an
  older pack never replaces a later one, and the launcher puts the latest one back before each
  game starts ([AVATARS.md](AVATARS.md)).
- **Without any avatar pack (since 0.34.0).** With no game that carries a version 2 pack (Kinect
  Sports, Mortal Kombat...), the avatar system now doesn't start for this game
  (`GameDescriptor::avatar_system_needs_pack`: `XamAvatarInitialize` fails, as for titles without
  avatars), and the game goes without avatars as the base game did; the log says `avatars: no
  avatar system without an avatar asset pack`. Given the system with empty assets, the version
  with its title update crashed in `sub_82877CC8` (+0x711, a null read on thread 21) about 10 s
  after starting.
  Checked on 2026-11-02 from a fresh import with no pack: no crash in seven runs of 130 to 460 s, the
  menus as with a pack, and in one run, with the save an earlier run made, level 1 loaded and ran (the timer counting, the camera following,
  "Stuck? Press RB", 60 FPS). The scripted press got no further than the start of the level there, and
  no player was seen on screen, so whether a person can play a level like that is **still to be
  checked**: the avatars come back as soon as Kinect Sports or Mortal Kombat is added.

## Reproducing
From the arcade package (`<package>` is the folder that holds `58410A71`):

```
build.bat
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\58410A71\000D0000\<file>" extracted\dcc\game --verify
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\58410A71\000B0000\tu00000002_00000000" extracted\dcc\update
build\tools\x360tool\release\x360tool.exe xex-patch extracted\dcc\game\default.xex extracted\dcc\update\default.xexp extracted\dcc\update\default_0.0.2.2.xex
cd games\dcc
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\dcc\update\default_0.0.2.2.xex switch_tables.toml
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat
```

XenonRecomp writes into `out_directory_path` from `config.toml` (`recomp5`), and that folder must
exist and be empty before it runs. A developer build needs the update's files in
`%LOCALAPPDATA%\AnyX360\DoritosCrashCourse\content\000B0000\tu00000002_00000000\`.
