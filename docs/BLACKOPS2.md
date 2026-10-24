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
| Target version | Both executables of the base game (single-player, and multiplayer with Zombies); no Title Update |

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

Its header is in [games/bo2/notes/xex-info_mp.txt](../games/bo2/notes/xex-info_mp.txt). Both are
recompiled, each into a program of its own (see [Multiplayer and Zombies](#multiplayer-and-zombies)).
The campaign's main menu offers Multiplayer, Campaign and Zombies: Multiplayer and Zombies both
pass 1000 bytes with `XamLoaderSetLaunchData` and call `XamLoaderLaunchTitle("default_mp.xex", 0)`;
the data tell the multiplayer executable which of the two to open.

The game's data are fast files (`*.ff`; the Zombies maps are `zm_*.ff` in the same folder), image
packs (`*.ipak`; `base.ipak` is 2.6 GB), sound banks (`stream\*.sabs`, `load\*.sabl`), videos in
WebM (`*.webm`: the opening and the loading videos) and Bink (`*.bik`), and `weapons.clump`.

The package's other content: 8 DLC packages (content type 2): two licences of 57 KB ("Season Pass"
and "Premium Content 1") and six content packages of 129 MB to 2.13 GB (presumably the map packs);
30 avatar items (content type `0x9000`) and a Title Update (`000B0000`, 51 files, 85.74 MiB:
`default.xexp`, `default_mp.xexp`, patch fast files and `patch_all.ipak`). The game asks for
`update:\patch.ff` and `update:\frontend_patch.ff`, which aren't mounted. The launcher installs the
DLC packages, but the base executables can't use them, and the Title Update isn't applied: see
[The DLC and the Title Update](#the-dlc-and-the-title-update).

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
- **Multiplayer** ([config_mp.toml](../games/bo2/config_mp.toml), [switch_tables_mp.toml](../games/bo2/switch_tables_mp.toml)):
  the helpers from find_helpers.py; setjmp `827D8600` and longjmp `827D9790`, checked by hand, word
  for word the campaign's (20 calls to setjmp, 12 to longjmp; the other longjmp candidate,
  `82130518`, is a function with a switch); 240 jump tables from the dataflow detector, the 14
  table-driven `bctr` left being tail calls through tables of function pointers (13 in `.data`, one
  in `.rdata` behind a null check); the campaign's `bdz` function, here `sub_8261D1D8`, given by
  hand. 40,654 functions (20,410 in `.pdata`); no instruction was missing. Its imports and sections
  are in [notes/imports_mp.txt](../games/bo2/notes/imports_mp.txt) and
  [notes/sections_mp.txt](../games/bo2/notes/sections_mp.txt).

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
  80 and 36 (`NetDll_XNetStartupEx`, `NetDll_WSAStartupEx`), by ordinal as well.
  `XexLoadImage("xam.xex")` works since the multiplayer bring-up (below).

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
  - The Title Update isn't applied (`update:\` isn't mounted).
  - One long session slowed to a few frames per second for about 20 s, nine minutes in, as the
    action moved on (a helicopter, new effects); the same route again, with the shaders cached,
    didn't. Possibly the host compiling new shaders, or another program loading the machine.
  - Not tried: the rest of the campaign, Strike Force missions, long sessions; whether Modern
    Warfare 3's occasional black screen at start happens here (not seen so far, in dozens of
    starts of both programs).

## Multiplayer and Zombies

### Two programs
`default_mp.xex` is recompiled on its own into `BlackOps2MP.exe` (target `bo2_mp`, built with
`bo2`; [src/main_mp.cpp](../games/bo2/src/main_mp.cpp)), next to `BlackOps2.exe`, as Modern
Warfare 3's multiplayer is ([MODERNWARFARE3.md](MODERNWARFARE3.md#switching-between-them) explains
how one program starts the other and hands it the launch data). game.json lists both
(`"executables"`); the launcher shows one tile and starts the campaign's, and the release ships
both. They share the title ID, the user folder, the profiles and the shader cache. Same
descriptor as the campaign: one vblank per frame (60 FPS), main thread on hardware thread 0.

### Switching between them
- The campaign's main menu: **Multiplayer** (UP from Campaign) or **Zombies** (DOWN) closes the
  window and opens `BlackOps2MP.exe` (about ten seconds), which opens the BLACK OPS II menu or the
  BLACK OPS II ZOMBIES menu as the launch data say. Both menus offer Xbox LIVE, System Link, Local
  and Options.
- In either menu, B (Main menu) opens a box with the other modes and Cancel (focused): Campaign
  starts `default.xex` again, which comes back at its title screen ("Press START") in about 25
  seconds; Zombies, from multiplayer, restarts the program in Zombies.
- The launcher closes when the game switches; the log keeps going to `logs\bo2\last_run.log`.

### What the multiplayer program needed
- **The Local lobby hosts a session**, even for split screen. `XSessionCreate` (XGI `0x000B0010`)
  succeeded without filling in the session's `XSESSION_INFO`, and the lobby's `createHostSession`
  task took the zero session ID for a failure: "You must have an active network connection to
  play Xbox LIVE or System Link games", and back to the menu. A host now gets a nonzero system link
  ID, the title's `XNADDR`, a key and a nonce, as a console does offline.
- Then `Com_Error` "Failed to start QoS listening on the server. party presence session
  0x80072742": `XNetQosListen` answered `WSAENETDOWN`. It succeeds now (nothing else is on the
  network to ask); `XNetQosServiceLookup` returns an empty result, `XNetRegisterKey` and
  `XNetUnregisterKey` succeed, and `XNetLogonGetMachineID` reports "not logged on", as in Xenia.
- **40 FPS**: multiplayer limits frames with `com_maxfps` (60: 16 ms a frame) while `r_vsync` is
  on, measured with `GetTickCount`. The runtime's tick count followed Windows', which moves in
  15.6 ms steps, so every other frame waited for a second step. The tick count now advances every
  millisecond, as the console's does: 60 FPS in the menus and 55-60 in a two-player match.
  Modern Warfare 3's split screen, which ran at 35-57, held 60 in a two-player test.
- **A crash at the end of a Zombies game** (both players down): the XDK's party library loads
  `xam.xex` with `XexLoadImage` and looks up six exports by ordinal; the load failed, every
  `XParty*` call returned "not implemented" without writing its output, and the game read an
  uninitialised user count. `XexLoadImage` now returns the handles of `xam.xex` and
  `xboxkrnl.exe`, and `XexGetProcedureAddress` returns the party exports (an empty party). After
  the game over both players are back in the Zombies lobby.
- **Grief**: choosing it changes the session (`XSessionModify`, XGI `0x000B0018`), which failed
  ("Error modifying the game"). It succeeds now.

### Status (2026-10-20, scripted pads)
- **Menus**: multiplayer and Zombies, at 60 FPS. Xbox LIVE needs an Xbox LIVE sign-in; System
  Link shows "you must have an active network connection".
- **Local split screen, multiplayer**: the Local lobby (Start match, Game setup, Adjust bots,
  Create a class, Scorestreaks) on Aftermath, Team Deathmatch. Player 2 presses A to join: with
  `sign_in = "at_start"` they are already signed in; with `"on_demand"` (the default) the game
  shows the sign-in screen and the runtime signs them in with their profile. Start match, each
  player picks a class, and both move, aim and fire, each with their own HUD and minimap: 55-60
  FPS. Four players (`@2`, `@3`) play the same way in four views at 37-51 FPS on the test
  machine.
- **Bots** (Adjust bots: allied and enemy bots, difficulty): two allied and three enemy bots
  fight and score; the killcam and respawn work, at about 60 FPS. This is the local version of
  Combat Training; Combat Training itself is an Xbox LIVE playlist.
- **Zombies**, from the campaign's menu: Local, then the map (Green Run) and its location: Bus
  Depot (TranZit), Town or Farm. TranZit with two players reaches round 1 with zombies, both
  players move and fire, and after both go down the game returns to the lobby with both players.
  TranZit alone starts too. In Town, Grief (it needs two players) plays with two: CDC against CIA.
  45-60 FPS with two players (below). The two views keep the game's black side bars, as on the
  console.
- **Switching**: campaign to multiplayer, campaign to Zombies, multiplayer to Zombies and back to
  the campaign work. The campaign route still reaches the first mission's gameplay.

### Split screen frame rate (2026-10-24)
A player reported 17 FPS in two-player Zombies on an RTX 5060 laptop (plugged in, render scale 1x,
1080p with FSR). The test machine is that laptop (Core i7-13620H: 6 performance and 4 efficiency
cores; RTX 5060 Laptop GPU).

**Measuring.** Two-player TranZit from the campaign's menu, then a scripted fight in the Bus Depot
(both players aim, fire, knife, turn and reload in a loop in round 1 until both are down), with the
per-thread CPU time and the GPU counters the runtime logs every 5 s. Runs of the same route differ
a lot (where the players look takes the draws from 2,300 to 3,900 a frame, and the laptop's clocks
follow its temperature), so the comparison was made inside each run: a temporary build switched
both changes below off and on every 15 s, and only the 5 s windows entirely in one state count.
Four runs, gameplay windows with at least 2,000 draws a frame:

| | As in 0.14.0 | Now |
|---|---|---|
| Frame rate (5 s windows) | 42.7 FPS (lowest 33.8) | 50.6 FPS (lowest 37.4) |
| Worst frames (p99) | 62 ms | 43 ms |
| Draws per frame | 3,400 | 3,500 |
| GPU Commands thread | 82% working, 17% asleep in WAIT_REG_MEM | 95% working |
| D3D12 Replay thread | - | 22% of a core |
| Game's main thread / render thread | 96% / 94% | 91% / 92% |
| GPU time per frame | 3.6-4.1 ms (about 20% busy) | the same |

Lighter moments of the same fight (2,300-2,500 draws) hold 58-60 FPS now; a two-player
multiplayer match (Aftermath) went from 34 to 60 FPS with the first version of these changes.

**What limited the frame** was not the GPU, busy a fifth of the time, but Xenia's single "GPU
Commands" thread, which turns the game's command buffers into Direct3D 12 work. It was busy the
whole frame:

- **Asleep in WAIT_REG_MEM**, 17-23% of its time: the game makes the GPU wait for values its CPU
  writes a moment later, and Xenia slept a millisecond (or more, with the timer's granularity)
  each time the value wasn't there yet. It now yields for the first millisecond and only then
  sleeps (Xenia option `wait_reg_mem_yield_us`, 1000; 0 is the old behaviour).
- **Replaying the recorded commands**: Xenia records Direct3D 12 commands into a list of its own
  and replayed them into the real command list at the end of each submission, on the same thread:
  15% of its time in the multiplayer match. A "D3D12 Replay" thread now replays them while they are
  still being recorded, and the command processor only waits for the tail at the end of the
  submission (Xenia option `d3d12_replay_thread`, on).

Both can be turned off in `settings.toml`'s `[xenia]` section if a game misbehaves; the other nine
games ran their regression routes with both on.

**What limits it now**, in the heavier windows, are three threads near a full core each: the GPU
Commands thread (95%: two thirds of it processing draws, then guest memory uploads, texture and
binding updates; about 10% presenting the frame, about 9% in Xenia's global lock, which the game's
threads also take when they write to memory the GPU reads, and about 7% waiting for the replay's
tail), the game's main thread (`guest 11`, 91%) and its render thread (`Backend`, 92%, part of it
in those write-watch faults). Holding 60 there needs the recompiled code to run faster as well as
the command processor. A busy machine costs more than all of this: with another program using ten
cores, the same fight ran at 36-38 FPS with or without the changes. The 17 FPS weren't reproduced;
the lowest 5 s window seen as in 0.14.0 was 33.8 FPS. A first session without the shader cache
plays as a later one (the pipelines compile in a few seconds).

**Tools** for the next round: the 5 s log line and F1 show each thread's CPU time and the GPU's
counters (`cpu:` and `gpu:` lines), `ANYX360_PROFILE` samples named threads and names their
hottest functions, and scripted input repeats presses (`~period x count`) for long fights (see
[GEARS3.md](GEARS3.md#debugging-switches)).

### Routes
Times from the first input poll, with an existing user folder; give the second program its
script with `ANYX360_INPUT_NEXT`.

Split screen match, from `BlackOps2MP.exe` started directly:

```
ANYX360_INPUT=40:DOWN,42:DOWN,44:A,60:A@1,68:A,90:A,90:A@1,95:A,95:A@1,105:LUP/5,105:LUP/5@1,111:RT/0.5,111:RT/0.5@1
```

DOWN, DOWN and A open Local; player 2's A joins (with on-demand sign-in, press it two or three
times: the sign-in screen takes one); A starts the match; A twice for each player takes the first
class. Four players: `60:A@1,62:A@2,64:A@3,72:A`, then A twice for each. Bots: in the lobby
`54:DOWN,56:DOWN,58:A` opens Adjust bots (RIGHT adds, DOWN moves to the next row), then B, UP,
UP and A start the match.

Zombies, from the campaign: the campaign route up to `72:A` reaches the main menu, then
`82:DOWN,86:A,92:A` picks Zombies (`82:UP,86:A,92:A` picks Multiplayer). The Zombies program
then takes:

```
ANYX360_INPUT_NEXT=14:DOWN,16:DOWN,18:A,40:A,52:A,60:A,76:A@1,80:A@1,84:A,90:A@1,94:A@1,100:A,150:LUP/4,150:LUP/4@1,155:RT/0.4,155:RT/0.4@1
```

DOWN, DOWN and A open Local; A picks Green Run, A its first location (Bus Depot) and A its mode
(TranZit). In the lobby player 2 joins with A; the first time, a tip box takes player 1's A
and the presses during it are lost, so player 2 presses again; A starts. Round 1 begins at about
150 s. Grief: `52:DOWN,56:A,62:A` (Town, then its first mode) instead of `52:A,60:A`.

From `BlackOps2MP.exe` started directly: `40:B,44:UP,47:A` switches to Zombies (give the Zombies
route above as `ANYX360_INPUT_NEXT`), and `40:B,44:UP,46:UP,48:A` goes back to the campaign.

### The DLC and the Title Update
The launcher leaves Black Ops II's content packages out (game.json `"downloadable_content":
false`; its result says how many). Installed by hand as the launcher installs other games' DLC
(`<user>\content\00000002\<package>`), the copy's six packages (the two licences aside) are
mounted (`dlc1:`, `dlc2:`...), but the base executables can't use them:

- At start each pack is asked for `dlc1.id` to `dlc5.id`. Nuketown 2025 (multiplayer) carries
  `dlc0.id` and Nuketown Zombies `dlczm0.id`, which version 0.0.0.14 never asks for: the Local
  map list keeps the 14 base maps.
- Uprising (Magma, Encore, Studio, Vertigo and Mob of the Dead) carries `dlc2.id`; the game then
  opens `snd_dlc2.<language>.pak`, which the pack doesn't have (its sounds are `.sabs` banks, the
  format of later versions), and rejects it: "The content you are trying to load is not from the
  correct region" when the multiplayer menu opens. A closes the box and the base game plays as
  usual.

The packs were made for the game as updated. The Title Update (`000B0000`) holds
`default.xexp`, `default_mp.xexp`, the patch fast files (the multiplayer program asks for
`update:\patch_mp.ff` and `update:\patch_ui_mp.ff`; not found, harmlessly) and `patch_all.ipak`.
Supporting it would take:

1. Recompiling the patched executables: XenonRecomp applies a `.xexp` (`patch_file_path`, and
   `patched_file_path` writes the patched XEX), then new jump tables, helpers, setjmp/longjmp and
   hand fixes for both (two new configurations).
2. The runtime loading the patched image (the patched XEX) instead of the base one.
3. Mounting `update:` on the Title Update's files.
4. The launcher keeping the Title Update (it leaves title updates out now) and requiring it; a
   copy without it would need the base executables. Then the DLC can be tried again, and
   installed again (`"downloadable_content"` back to true).

### Controls
As in Modern Warfare 3: player 1 on keyboard and mouse (or a controller: F2 > Players), players
2-4 on controllers. Move and look on W A S D and the mouse; aim and fire on the right and left
mouse buttons (LT, RT); jump on Space (A); crouch and prone on V (B); reload and use on E (X:
also buying, opening doors and reviving in Zombies); switch weapon on F (Y); tactical and lethal
grenades on Q and R (LB, RB); sprint on C (L3) and melee on the middle button (R3); scorestreaks
on the arrows and 1-4 (D-pad); pause on Esc (Start), scores on Tab (Back). In menus Space or Enter
accepts and V or Backspace goes back.

### Known issues
- The DLC isn't usable, so the launcher leaves it out (installed, Uprising shows a region error
  box at start: above).
- Four players run at 37-51 FPS on the test machine, and two players at 45-60 in heavy moments
  (Zombies especially: see the split screen frame rate above).
- Xbox LIVE and System Link aren't available (no network play).
- Not tried: Zombies with three or four players, Survival and the Farm location, Zombies'
  custom game options, other multiplayer modes and maps, a match played to its end, the Zombies
  box's Multiplayer choice.

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
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\bo2\game\default_mp.xex switch_tables_mp.toml
mkdir recomp_mp
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config_mp.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release bo2
```

XenonRecomp writes into `out_directory_path` from each configuration (`recomp`, `recomp_mp`), and
that folder must exist and be empty before it runs. `build.bat release bo2` builds both programs
(`BlackOps2.exe` and `BlackOps2MP.exe`); for the multiplayer helpers, run find_helpers.py on
`default_mp.xex` extracted the same way.

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
