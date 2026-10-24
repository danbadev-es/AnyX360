# Call of Duty: Modern Warfare 3: target notes

These are working notes for the seventh title and the first controller game since Gears of War 3:
Call of Duty: Modern Warfare 3. Every value below comes from its `default.xex`, read with
`x360tool xex-info`. The full dump is in [games/mw3/notes/xex-info.txt](../games/mw3/notes/xex-info.txt).

| Item | Value |
|---|---|
| Developer | Infinity Ward and Sledgehammer Games (Activision, 2011); IW engine 5 |
| Title ID / media ID | `415608CB` / `20344957`, version 0.0.0.8, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 187 files, 7.46 GiB), 26 DLC packs, 43 avatar items and a Title Update |
| XEX | `iw5sp_ship.exe`: AES-encrypted with the retail key, **basic** compression, base `0x82000000`, entry `0x82459E58` |
| Image | 35.2 MiB: 4.4 MiB code (`.text` and `BINK`), 29.5 MiB data, 1.25 MiB read-only |
| Functions | 36,494 (15,024 in `.pdata`) |
| XDK | 2.0.20500; links D3D9LTCG, XGRAPHC, XAPILIB, XNET, XONLINE, XPARTY, X3DAUDL, LIBCMT and the older XAUD and XHV 2.0.8276; compiler 16.0.11886 |
| Imports | `xboxkrnl.exe`: 160 functions and 13 variables; `xam.xex`: 98 functions ([list](../games/mw3/notes/imports.txt)) |
| Multiplayer XEX | `default_mp.xex` (`iw5mp_ship.exe`): same title ID, key and compression; entry `0x824DBAE0`; 31.6 MiB image (4.9 MiB code); 38,186 functions; imports 161 + 13 from `xboxkrnl.exe`, 101 from `xam.xex` ([info](../games/mw3/notes/xex-info_mp.txt), [imports](../games/mw3/notes/imports_mp.txt)) |
| Target version | Both executables of the base game; no Title Update |

The game folder holds two executables: `default.xex` (campaign and Special Ops) and
`default_mp.xex` (multiplayer). Each is recompiled into its own program, `ModernWarfare3.exe` and
`ModernWarfare3MP.exe`, and each starts the other with `XamLoaderLaunchTitle` (see
[Multiplayer](#multiplayer)). The game's data are IW fast files (`*.ff`), Bink videos (`*.bik`) and
`imagefile*.pak`/`packfile1.pak`.

The package's other content: 26 DLC packs (content type 2: the Collections, Elite drops and the
free maps, for multiplayer and Survival), 43 avatar items (content type `0x9000`) and a Title
Update (`000B0000`). None of them is used yet.

## Recompiler configuration
- **Register save/restore helpers** and **setjmp/longjmp** (`824C0AF0`, `824C1590`, the same CRT
  code as Kinect Sports'): [config.toml](../games/mw3/config.toml). The engine's error handling
  (`Com_Error`, from Quake III) leaves the frame with `longjmp`: 13 calls to `setjmp`, 46 to
  `longjmp`.
- **Jump tables**: 128 (5 from the fixed patterns, 123 from the dataflow detector). The 10
  table-driven `bctr` left are tail calls through tables of function pointers in `.data`, correct
  as indirect calls.
- **Instructions**: `vpkd3d128` and `vupkd3d128` with the 2:10:10:10 type (normals packed into a
  word), added to the fork.
- **Switches on the index register's low word**: the game clears a flag with
  `rlwinm r11,r11,0,23,21` (a mask that wraps around, which copies the word into the register's
  high half) before a switch. The bounds check only looks at the low word, but the generated
  `switch` used the whole register and jumped outside the host's jump table: a crash about five
  minutes into the campaign. The fork now switches on the low word.
- **Multiplayer** ([config_mp.toml](../games/mw3/config_mp.toml), output in `recomp_mp`): the
  register helpers from `find_helpers.py`; setjmp/longjmp at `82544510`/`825452B0`, checked by
  hand (word for word the campaign's, apart from relocated addresses); 156 jump tables (5 fixed
  patterns, 151 from the dataflow detector; 14 table-driven `bctr` left as indirect calls).
  `vmaxsb` (signed byte maximum), which only the multiplayer code uses, was added to the fork.

## Bring-up
- **Kernel exports**: kernel semaphores (`KeInitializeSemaphore`, `KeReleaseSemaphore`),
  `KeResumeThread` (the render and worker threads are created suspended), `KeInsertQueueDpc`
  (Direct3D signals an event from a DPC; run at once on the calling thread, as in Xenia), IRQL
  and spin lock variants, `vswprintf`, `XamShowMessageBoxUI`, `XamContentGetDeviceName`,
  `XamLoaderSetLaunchData`, `XAudioGetVoiceCategoryVolumeChangeMask`, `WSAResetEvent`,
  `WSACloseEvent`.
- **I/O completion routines**: the fast file loader (the "Database" thread) reads with
  `ReadFileEx` and sleeps alertably (`SleepEx(..., TRUE)`) until the completion routine has run.
  APCs were never delivered, so the loader waited forever before the first video. Completion
  routines now run as user APCs at the thread's next alertable wait.
- **`XSetThreadProcessor`**: the game places each thread on a hardware thread (Worker0 and
  Database on 1, Worker1 and Stream on 2, Backend and Cinematic on 3, Worker2 on 4, Server and the
  Bink decoding threads on 5) and keeps per-processor buffers for the models it builds, indexed by
  the number at PCR+0x10C. The runtime only changed
  the KTHREAD's affinity, so all threads kept their creator's number, shared the buffers and the
  first level crashed while loading (`sub_823CAB70`). The thread now reads its new number.
- **The main thread on hardware thread 0**, as on the console
  (`GameDescriptor::main_thread_processor`; the other games keep 1): on 1 it shared Worker0's
  buffers and crashed the same way.
- **Message boxes**: `XamShowMessageBoxUIEx` read its result from the wrong argument (it has one
  more than `XamShowMessageBoxUI`); fixed for every game.
- **Pacing**: one vblank per frame: 60 FPS in the menus and the levels.
- The copy used here is a Spanish disc: its files carry only Spanish, whatever language the
  game is told (`XGetLanguage`, the launcher's profile).

## Status (2026-10-20)
- **Played by a person**: the user tried it and reports that it runs well and looks right (how
  far they got isn't recorded).
- **Boot and menus** (scripted pad): the opening videos (Bink), the brightness and safe area
  screens, the content warning, the main menu (Special Ops, Campaign, Multiplayer), the campaign
  menu and the difficulty choice. The settings (profile settings) and the save (`savegame.svg`)
  are written.
- **Campaign**: a New Game loads the opening (its loading video with subtitles, the in-engine
  intro with Price and Yuri, the MW3 title card) and goes on to Black Tuesday: the briefing video,
  the in-engine opening in New York, then gameplay. The scripted player walks up the street
  toward the objective marker, fires, takes damage; allies and enemies fight, with the HUD,
  compass, effects, subtitles and audio (XMA). 60 FPS throughout; tested for nine minutes.
- **Known issues**:
  - About one start in fifteen stops on a black screen after the first frame: the render thread
    holds Direct3D's lock and waits for the swap it queued to be retired by the vblank interrupt
    handler (device+16848 to reach device+16840, `sub_820A0EE8`), which never happens; the fast
    file loader then waits for that lock and the main thread for the loader. A restart gets past
    it. Not understood yet; a race between the swap queue and the interrupt handler (on the
    console Direct3D can mask interrupts on its processor, which does nothing here) was the main
    suspect. Since multiplayer, a more likely one: the same symptoms came from a lost GPU device
    in Survival (see [Multiplayer](#known-issues)), fixed in the fork; if it happens again,
    Xenia's log (stdout, the launcher's `last_run.log`) says whether the device was lost.
    On 2026-10-24, with the machine busy (another game being built and tested), it hung in 6 of 10
    starts of 0.14.0 and 14 of 16 of 0.14.1's build, with no device lost: it depends on timing, and
    load makes it far more likely. Turning off the D3D12 replay thread (`[xenia]
    d3d12_replay_thread = false`) didn't avoid it (3 of 3 hung).
  - In the intro level the snowy trees look flat white against a black sky (possibly a
    rendering difference; not compared with a console).
  - Not tried: Special Ops missions, the DLC, later missions, saving mid-mission. Survival and
    split screen: see [Multiplayer](#multiplayer).

## Multiplayer

### Two programs
`default_mp.xex` is a program of its own, so it is recompiled on its own: `ModernWarfare3MP.exe`
(target `mw3_mp`, built with `mw3`), next to `ModernWarfare3.exe`. game.json lists both
(`"executables"`); the launcher shows one tile and starts the campaign's, and the release ships
both. They share the title ID, the user folder, the profile and the shader cache. The
multiplayer executable's descriptor is the campaign's (60 FPS, main thread on hardware thread 0).

### Switching between them
On the console, `XamLoaderLaunchTitle` ends the title and boots the XEX it names. Here:

- The campaign's main menu → Multiplayer (after a notice recommending the campaign first) calls
  `XamLoaderLaunchTitle("default_mp.xex", 0)` after `XamLoaderSetLaunchData` (1000 bytes). The
  runtime finds the executable recompiled from that XEX next to the running one, writes
  `<user>\launch_data.bin` (Xenia's LoaderData layout: host path, launch path, flags, data),
  starts it with the same `--game`, `--user` and `--key` and `--restarted-from <pid>`, and exits.
  The new process waits for the old one to exit, reads the launch data (`XamLoaderGetLaunchData`
  returns it) and deletes the file.
- Multiplayer's Main Menu calls `XamLoaderLaunchTitle("default.xex", 0)` the same way; the
  campaign's executable then skips the opening videos and shows its main menu.
- The window closes and another opens (a few seconds). The new process inherits the standard
  handles, so the launcher's `logs\mw3\last_run.log` keeps receiving both.
- The launcher waits for the process it started: when the campaign exits (code 0) to start
  multiplayer it closes, as after any normal exit, and the game keeps running.
- A launch path without a recompiled executable (or an empty one: back to the dashboard) ends the
  process, as before. Test hook: `ANYX360_INPUT_NEXT` becomes the next process's
  `ANYX360_INPUT`.

### Status (2026-10-20, scripted pads)
- **Menus**: Xbox LIVE, Split Screen, System Link, Elite, Theater, Store, Options and Main Menu,
  at 60 FPS. No imports were missing besides `XamShowGameVoiceChannelUI` (a system UI stub).
- **Online**: Xbox LIVE shows "you have to sign in to Xbox LIVE" and returns to the menu; System
  Link opens an empty game list (sockets are local; nothing else is on the network).
- **Split screen**: in the sign-in screen each player presses A in their quadrant; player 2
  joins as a guest ("Guest 2") or with their profile if signed in (F2 > Players), and each
  continues with A. The lobby (Start Match, Game Setup, Create a Class, Barracks) starts a Team
  Deathmatch on Seatown in about ten seconds. Each player picks a team (auto-assign) and a class
  and plays: both move, aim, fire and reload, each with their own HUD, compass and score. 35-57
  FPS during the match on the test machine, which was busy with other builds at the time. Three
  players (a third pad, `@2`) play the same way in three views.
- **Special Ops** (the campaign's executable): Split Screen joins player 2 the same way and offers
  Survival or Missions. Survival on Resistance plays in split screen (wave 1, a player downed and
  the revive prompt for the other) and alone.

### Routes
Multiplayer split screen match, from `ModernWarfare3MP.exe` with an existing user folder (times
from the first input poll):

```
ANYX360_INPUT=15:DOWN,18:A,22:A,24:A@1,28:A,30:A@1,40:A,56:A,57:A@1,63:A,64:A@1,67:A@1,71:A@1
```

DOWN and A open Split Screen; A for each player joins, then A for each continues; A starts the
match; A for each auto-assigns a team and A takes the first class (player 2's class menu needs
the later presses). The first time, a guest is told their progress won't be saved: Continue
without saving is the second choice (DOWN, A). A third player joins with `@2` the same way.

From the campaign's executable: the campaign route up to `72:A` (the content warning) reaches
the main menu, then `78:RIGHT,81:A,85:DOWN,88:A` picks Multiplayer and confirms past the notice;
give the multiplayer process its own script with `ANYX360_INPUT_NEXT`. `UP` and `A` in the
multiplayer menu go back to the campaign.

Special Ops split screen Survival, after the campaign route up to `72:A`:
`78:LEFT,81:A,88:A,98:DOWN,100:DOWN,102:DOWN,105:A,112:A@1,118:A,122:A@1,146:A,152:A,158:A`
(Special Ops, past the notice, Split Screen, both join and continue, Survival, the first map,
start). Alone: `78:LEFT,81:A,88:A,98:DOWN,100:DOWN,105:A,112:A,118:A,124:A,130:A`.

### Known issues
- **Fixed: loading a Survival map stopped** in 4 of 16 tries (alone and in split screen). The
  GPU's command processor stopped consuming the ring buffer, the render thread ("Backend")
  waited for it inside Direct3D (`sub_8209B5E8`, ring space) or for its worker jobs while holding
  a lock, and the fast file loader and the cinematic thread waited for that lock. Xenia's log
  (stdout) told why: `D3D12Texture: Failed to load 3D-as-2D texture data`, then `Graphics device
  lost`, whose error box (`xe::FatalError`) blocked the command processor (and took the focus:
  "input: cursor released" in the log). The texture cache loads a 2D copy of a 3D texture's first
  slice for shaders that sample it as 2D; the load fails when the CPU rewrites the memory while it
  loads (half the Survival loads), after it has recorded the GPU work that writes the copy, and
  the copy was then destroyed at once: the GPU wrote a released resource and the device was lost.
  The fork now keeps the copy with its texture and loads it again on its next use (thirdparty/xenia
  `anyx360-mp`): 6 of 6 loads then reached the wave, 3 of them with the failed load and none with
  a lost device (before: 4 of the 8 runs with the failed load lost it). The campaign's black
  screen at start may be the same; not verified. Since then the thread dump after 10 s without
  frames also prints the command processor's host stack.
- Split screen matches run below 60 FPS when the machine is busy (35-57 with two players, 35-50
  with three).
- The DLC maps aren't offered: the free Terminal pack, installed as the launcher installs DLC
  (`<user>\content\00000002\<package>`), is enumerated and mounted (`dlc1:`, `dlc2:`), but the map
  list keeps the 16 base maps. The packs probably need the Title Update (`update:\patch_mp.ff`),
  which isn't supported. Since 0.14.0 the launcher leaves the 26 packs out, as it does Black Ops
  II's (`"downloadable_content": false`); packs installed before stay, unused.
- Not tried: four players, a match played to its end, other modes, hosting a System Link game,
  the Special Ops missions.

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `415608CB`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\415608CB\00007000\<file>" extracted\mw3\game
build\tools\x360tool\release\x360tool.exe xex-extract extracted\mw3\game\default.xex extracted\mw3\default.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\mw3\default.bin
cd games\mw3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\mw3\game\default.xex switch_tables.toml
mkdir recomp3
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
```

The multiplayer executable the same way, with its own files:

```
build\tools\x360tool\release\x360tool.exe xex-extract extracted\mw3\game\default_mp.xex extracted\mw3\default_mp.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\mw3\default_mp.bin
cd games\mw3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\mw3\game\default_mp.xex switch_tables_mp.toml
mkdir recomp_mp
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config_mp.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release mw3
```

`build.bat release mw3` builds both executables. XenonRecomp writes into `out_directory_path` from
the configuration, and that folder must exist and be empty before it runs.

**Scripted route** to the first level (a new user folder; times from the first input poll, with a
warm shader cache):

```
ANYX360_INPUT=25:START,35:START,45:LRIGHT/0.5,48:A,52:LRIGHT/0.5,55:A,58:LRIGHT/0.5,61:A,66:START,72:A,78:A,84:A,92:DOWN,95:A,99:A,103:A
```

START twice skips the opening videos; the brightness and both safe area screens each need the
slider moved before A continues; A answers the content warning ("ask me later"), picks Campaign,
then DOWN and A start a New Game and A takes the default difficulty (Regular).
