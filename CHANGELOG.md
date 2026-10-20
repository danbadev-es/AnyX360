# Changelog

All notable changes to AnyX360 are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). The version is in the
[VERSION](VERSION) file; builds add the git commit as build metadata (`0.1.0+0b3cb8d`).

## [Unreleased]

### Added

- **Call of Duty: Modern Warfare 3 multiplayer** (in testing): `default_mp.xex` is recompiled as a
  second program, `ModernWarfare3MP.exe`, which the campaign's main menu starts and whose Main
  Menu entry goes back. Its menus, split screen (players 2-4 join with controllers, as guests or
  with their profiles), the split screen lobby and a Team Deathmatch on Seatown work. Xbox LIVE
  asks to sign in to Xbox LIVE and System Link finds no games. In the campaign's program, Special
  Ops in split screen: Survival on Resistance with two players.
- **Several executables per game**: game.json's `"executables"` names the program recompiled from
  each XEX of a game (`anyx360_add_game_executable` in `cmake/Game.cmake`); they build with the
  game and the release packages all of them. `GameDescriptor::xex_name` is the XEX a program
  loads (`ExLoadedCommandLine` and `XexGetModuleHandle` follow it).
- **A title starts its other executables**: `XamLoaderLaunchTitle` starts the program recompiled
  from the XEX it names, with the same game, user folder and key, and hands it the launch data
  (`<user>\launch_data.bin`, in Xenia's layout); the launcher's log keeps receiving the output.
  A title without one ends as before.
- `XamShowGameVoiceChannelUI` (a system UI that isn't shown).
- The recompiler: `vmaxsb`.
- When no frame has been presented for ten seconds, the thread dump also prints the host call
  stack of the GPU's command processor, to see what it stopped on.
- **Call of Duty: Black Ops II** (in testing): the single-player executable from a Games on Demand
  copy, with keyboard and mouse or a controller, at 60 FPS. The opening videos, the menus, the
  settings and saves, and the first mission (Pyrrhic Victory: its loading video, opening and
  gameplay) work.
- Media verification (`XamMediaVerificationCreate`, `Close`, `Verify`, `FailedBlocks`): nothing to
  verify on the hard disk; `Verify`'s callback runs as a user APC. `XexGetProcedureAddress` returns
  these exports, which titles look up by ordinal (before, it reported every ordinal missing, which
  the XDK takes for success: Black Ops II waited forever for the callback).
- Incremental SHA (`XeCryptShaInit/Update/Final` and the SHA-256, SHA-384 and SHA-512 ones),
  `MmQueryStatistics` and `XGetVideoCapabilities`.
- The recompiler: `vrfip128`.
- **Call of Duty: Black Ops II multiplayer and Zombies** (in testing): `default_mp.xex` is
  recompiled as a second program, `BlackOps2MP.exe`, which the campaign's Multiplayer and Zombies
  start (the launch data tell it which) and whose Main menu goes back to the campaign. Local split
  screen matches with two to four players and with bots, Zombies (TranZit alone and with two
  players, Grief with two) and switching between the three modes work. Xbox LIVE needs Xbox LIVE,
  System Link asks for a network, and the downloadable maps need the Title Update, which isn't
  supported.
- **xam.xex loads**: `XexLoadImage` returns the handles of `xam.xex` and `xboxkrnl.exe`, and
  `XexGetProcedureAddress` the party exports titles look up by ordinal (`XamPartyGetUserList`, an
  empty party; `XamPartySendGameInvites`, `XamPartySetCustomData`, `XamPartyGetBandwidth`,
  `XamShowPartyUI`, `XamShowCommunitySessionsUI`). Every title's XDK party library loads xam.xex
  at start; the failed load left its calls "not implemented" without output, and Black Ops II read
  an uninitialised party size and crashed after each Zombies game.

### Changed

- `XamLoaderGetLaunchData` copies what fits into the buffer and succeeds, and
  `XamLoaderGetLaunchDataSize` refuses a null pointer, as in Xenia.
- **The tick count advances every millisecond**, as the console's does (`GetTickCount`; it
  followed Windows', which moves in 15.6 ms steps). Black Ops II's multiplayer limits frames with
  it and ran at 40 FPS; it now runs at 60. Modern Warfare 3's split screen, 35-57 FPS before, held
  60 in a two-player test. Any game that times itself with `GetTickCount`.
- **Local sessions** behave as on a console offline: `XSessionCreate` gives a host its session's
  ID, address and key (Black Ops II's Local lobby took the empty ID for a failure: "you must have
  an active network connection"), `XSessionModify` succeeds (Grief), `XNetQosListen`,
  `XNetRegisterKey` and `XNetUnregisterKey` succeed instead of reporting the network down,
  `XNetQosServiceLookup` returns an empty result and `XNetLogonGetMachineID` reports "not logged
  on", as in Xenia. Games that host sessions: Modern Warfare 3's split screen, Gears of War 3's
  co-op.

### Fixed

- **A lost GPU device when a 2D copy of a 3D texture failed to load**: the texture cache destroyed
  the copy at once although the GPU was still to write it, and the device was lost; the game
  stopped behind an error box (Modern Warfare 3, about one Survival load in four). The copy now
  stays with its texture and loads again on its next use. Any game whose shaders sample a 3D
  texture as 2D.
- **`XamUserReadProfileSettings`**: a title-specific setting that was never written has no data
  pointer, as in Xenia. Black Ops II checks the signature of every blob it gets a pointer to and
  read past the end of an empty one.

## [0.10.0] - 2026-10-20

The players' names and the games' language from the launcher, a faster mouse, and fixes from
players' notes.

### Added

- **Players and language** in the launcher (the link with player 1's name in the header): the four
  players' names, which every game shows (their gamertags), and the language the games are told to
  use, **Windows' by default** (before: always English). A copy that has that language starts in
  it: Gears of War 3 comes up in Spanish on a Spanish Windows.
- **A response curve for mouse look** (F2 > Controls > Response): slow movements turn the camera
  faster, against the games' own stick curves. New defaults: sensitivity 1.5, aiming 1.0 (the games
  slow aiming themselves). A settings file still at the old defaults takes the new ones; values you
  chose are kept.
- Xenia's GPU options in a game's `settings.toml` (`[xenia]`, advanced), and the log names the GPU
  and the Direct3D 12 features where vendors differ: for finding the Gears of War 3 title screen
  artefact on an AMD RX 570 (`docs/GEARS3.md`).
- The launcher takes `--pad "<seconds>:<button>,..."` for unattended tests of its controller
  navigation.
- [docs/ANYX.md](docs/ANYX.md): the `.anyx` extension evaluated (a backup format for your own
  prepared copies; not implemented).

### Fixed

- **The mouse wasn't captured in fullscreen** until a click when the cursor was elsewhere (another
  monitor) or after switching to fullscreen. It is now captured as soon as the game is in front,
  starting in the middle, and the hidden cursor no longer reaches the screen's edges.
- **The launcher's progress bar seemed to restart**: before the percent was known, a segment swept
  across again and again. It now breathes in place, and the percent never goes back (a password
  retried during an extraction started it from the beginning).
- **The launcher's controller did nothing** until the window had been activated (a launcher opened
  behind another window): it is now in front and read from the start, with a smaller stick dead zone
  and the button hints as soon as a controller is connected.
- **Kinect: the webcam's roll is no longer measured.** It followed the player's lean and a lopsided
  floor and turned the picture of a level camera. The pitch (a webcam pointing down) is still
  measured; the roll is set by hand in F2 (0 unless changed).

## [0.9.0] - 2026-10-20

Call of Duty: Modern Warfare 3.

### Added

- **Call of Duty: Modern Warfare 3** (in testing): the single-player executable from a Games on
  Demand copy or the disc's files, with keyboard and mouse or a controller, at 60 FPS. The
  opening videos, the menus, the settings and saves, the campaign's intro level and Black Tuesday
  (the briefing, New York, the firefight) work. Multiplayer is a separate program, not recompiled.
- I/O completion routines (`ReadFileEx`, `WriteFileEx`) run as user APCs at the thread's next
  alertable wait (`SleepEx(..., TRUE)`, alertable `WaitFor*`), which returns "user APC". Before,
  a title that waited for them that way waited forever.
- Kernel semaphores (`KeInitializeSemaphore`, `KeReleaseSemaphore`), `KeResumeThread`, DPCs
  (`KeInsertQueueDpc`, run at once on the calling thread, as in Xenia), `KeRaiseIrqlToDpcLevel`,
  `KfLowerIrql`, `KeTryToAcquireSpinLockAtRaisedIrql`, `vswprintf`, `XamShowMessageBoxUI`,
  `XamContentGetDeviceName`, `XamLoaderSetLaunchData` (read back by the title itself),
  `XAudioGetVoiceCategoryVolumeChangeMask`, `NetDll_WSAResetEvent` and `NetDll_WSACloseEvent`.
- `GameDescriptor::main_thread_processor`: the hardware thread the title's main thread starts on.
  The console uses 0; the default stays 1, which the earlier games were tested with.
- The recompiler: `vpkd3d128` and `vupkd3d128` with the 2:10:10:10 type.

### Fixed

- **`XSetThreadProcessor` moves the thread**: the thread now reads its new processor number at
  PCR+0x10C, as on the console (before, only the KTHREAD's affinity changed, and every thread kept
  its creator's number). Games that keep data per processor shared it between threads.
- **The recompiler switches on the index register's low word** (in games recompiled from this
  version on): an `rlwinm` whose mask wraps around fills the high word, and a switch on the whole
  register jumped outside the host's jump table (Modern Warfare 3, five minutes into the
  campaign).
- `XamShowMessageBoxUIEx` read its result pointer from the wrong argument (it has one more than
  `XamShowMessageBoxUI`): it wrote the pressed button to an unrelated address and filled the
  result as if it were the overlapped structure.

## [0.8.0] - 2026-10-20

Doritos Crash Course and Kinect Sports, with Xbox Avatars made from the console's own avatar data;
and the AnyX360 logo.

### Added

- **Doritos Crash Course** (in testing): the Xbox Live Arcade obstacle race, with a controller or
  the keyboard. The menus, the country and level choice and the levels work: the player's avatar
  runs, jumps, falls into the water and comes back at the checkpoint. Finishing a level hasn't
  been tried yet.
- **Kinect Sports** (in testing): Rare's sports, played with the webcam, from a Games on Demand
  copy or the disc's files. The menus, picking the main player (yourself or one of the game's
  guests), the stadium and the sport menu work; Bowling starts and the player's avatar follows the
  body. The sports haven't been played through by a person yet.
- **Xbox Avatars.** On the console the avatars' parts come from the system; AnyX360 now builds
  them from the console's avatar asset pack, which the Kinect Sports disc carries: the body (its
  skeleton from the game's own avatar library, shaped to the avatar's height and weight in Kinect
  Sports), its clothes in their colours, its hairstyle and its face (eyes, eyebrows, mouth, facial hair). Descriptions are read
  and written in the console's format, so the avatars the games describe (Kinect Sports' guests,
  Doritos Crash Course's crowd) keep their looks. The players get the default avatar.
- The launcher installs the avatar asset pack when it adds a game that has one, for every avatar
  game: Doritos Crash Course, which doesn't carry it, gets its avatars once Kinect Sports is added.
- **The AnyX360 logo.** The launcher shows the mark beside its name, the whole logo while no game
  has been added yet, and the mark with the terms of use, sharp at any display scale. The launcher
  and every game have it as their icon: in File Explorer, the title bar, the taskbar and Alt+Tab.
- The kernel's LZX decompressor (`LDICreateDecompression`, `LDIDecompress`,
  `LDIResetDecompression`, `LDIDestroyDecompression`): XMemDecompress in the 2010 XDKs and the
  avatar library unpack their data with it.
- `x360tool avatar-pack`, `avatar-unpack` and `avatar-model` list and unpack the avatar asset
  pack and decode its parts (meshes, material parameters, textures, OBJ). The notes are in
  [docs/AVATARS.md](docs/AVATARS.md).
- The recompiler: `stvebx`, `vadduhs`, `vandc`, `vavgsw`, `vminsw`, `vsl`, `vsrb`, `vsro`,
  `vsubuwm` and `vsubuws`.

### Fixed

- **A title that sets up the GPU's command ring a second time** (a second Direct3D device) stopped
  on a black screen: the command processor kept its old read pointer and ran past the new commands.
- **GPU callbacks to the CPU never finished**: Direct3D's handler clears the bit of the processor
  it runs on, and the interrupt for processor 1 ran as another one, so the GPU waited forever.
- The free space reported for the hard drive is no longer a multiple of 4 GiB: Kinect Sports
  compares only its low 32 bits and said there was no room to save.
- **The recompiler finds more jump tables** (in games recompiled from this version on). A table
  placed right after its dispatch may send cases back to code before it; a bounds check that
  belongs to another switch no longer spoils the next one; and a table of offsets with no bounds
  check (the compiler knew the range) is dispatched on the target address. Doritos Crash Course
  had 68 jump tables left unresolved, now 2 (both calls through function pointers).
- `XamInputGetKeystroke` and `XamInputGetKeystrokeEx` report no keystroke instead of being
  missing.
- `XamAvatarInitialize` reads the title's coordinate system from its second argument.

## [0.7.1] - 2026-10-20

### Fixed

- **Fruit Ninja Kinect closed when a round started with its DLC installed** (the launcher installs
  it by default). Loading a DLC texture, the game's image library tries it as a JPEG first and
  gives up through libjpeg's error path, which leaves with `longjmp`. The game's `setjmp` and
  `longjmp` now run as the host's; recompiled as guest code, `longjmp` could not go back to where
  `setjmp` was called.
- `tools/scripts/find_helpers.py` finds `setjmp`/`longjmp` that have no unwind entry or copy their
  buffer's address to another register first, as Fruit Ninja Kinect's do. It also lists
  candidates in Kinect Adventures and Gears of War 3, whose recompiled code doesn't use them yet.

## [0.7.0] - 2026-10-20

Fruit Ninja Kinect, the first Xbox Live Arcade game, and progress that is finally saved.

### Added

- **Fruit Ninja Kinect** (in testing): the Xbox Live Arcade game, played with the webcam. The
  title screen's calibration, the menus, and whole Arcade and Classic rounds (fruit, bombs, the
  special bananas, combos, the results) play with the scripted test player; its eight DLC packs
  are installed by the launcher. Notes in [docs/FRUITNINJAKINECT.md](docs/FRUITNINJAKINECT.md).
- Xbox Live Arcade titles: the deployment type and the content volume say "downloaded"; the
  marketplace screens open and close at once (the license is always full).
- Files the title expands itself as it reads them (XMemCompress, `0FF512ED`) are reported as
  compressed.
- The scripted test player's `hand_y_<cm>` action.

### Fixed

- **Games' progress in the profile was never saved**: the profile settings were read and written
  with the wrong layout, so no title-specific setting (a game's saved options and progress) ever
  reached the disk. They are saved in `%LOCALAPPDATA%\AnyX360\<game>\profile`.
- **The recompiler**: a function whose last instruction could fall through returned early instead
  of going on into the next function (the analysis ends a function where a branch lands on the
  next address). This applies to games recompiled from this version on; Fruit Ninja Kinect had
  about 230 such functions, one of them in its audio engine.
- The virtual Kinect fills each body's user index as the console does (`XUSER_INDEX_NONE` unless a
  signed-in player is bound to it): games that pair players with bodies by it found nobody.
- `ObOpenObjectByName`, `XSessionLeaveLocal`, `XSessionWriteStats` and `XamWriteGamerTile`, which
  failed or weren't there.
- A path relative to no device (some games try one before `game:\`) is no longer a warning in the
  log.

## [0.6.0] - 2026-10-20

A new launcher that takes games in any format, a Kinect debug window for a second monitor, and the
in-game panels redone.

### Added

- **A new launcher**, drawn with Direct2D in the style of the Xbox 360's last dashboard (charcoal,
  flat panels, Xbox green): the games you added as their box art in a row, the selected one's art
  blurred behind the window, Play and a ⋯ menu (change or restore the cover, logs, prepare again,
  remove); a **Compatible games** drawer with this release's games and the coming ones; overlays
  instead of dialogs; files dropped on the window; a dark title bar and an icon. It is driven by
  the mouse, the keyboard or an **Xbox controller** (A, B, X, Y, Start, d-pad and stick), read
  only while the launcher is in front.
- **Box art**: the release ships the front covers of its games and of the coming ones
  (`covers\`, from the publishers' artwork, shown only to identify the games); a full wraparound
  scan or a front page alone (`x360tool cover`).
- **Any format**: an archive of any kind 7-Zip opens (7z, zip, rar and rar5 in parts, `.001`
  splits, tar, gz, bz2, xz, zst, cab, wim; a split set from any part), a disc image (`.iso`), a
  Games on Demand or **Xbox Live Arcade** package, a folder, or archives inside archives (a disc
  image in a .rar, a .tar in a .gz). What a file is comes from its content, not its name.
  **Password-protected archives** ask for the password. Downloadable content is recognised by its
  package header wherever it is, and a pack added on its own goes to its game.
- **The Kinect debug window** (F3, or from F2): the camera's image with every body's skeleton, the
  depth image, the play space from above and from the side, the main player's joints, the
  sensor's state and the last 10 seconds of timings, in a window of its own for a second monitor.
  It doesn't take the game's focus, runs on its own thread and reopens where it was.
- `x360tool stfs-extract --verify`: every block's SHA-1 and every hash table checked.
- `launcher_core` (the launcher's logic, without a window) with its own tests; `AnyX360.exe
  --screenshot` draws any state of the launcher into a PNG, and `--quit-when-done --result` runs a
  job unattended.

### Changed

- **The settings panel (F2)** is a sidebar of pages in titled sections, with switches and
  segmented choices, in **Inter** and a dark theme; the performance overlay (F1) too.
- **Sharp at any display scale**: the games are per-monitor DPI aware, so their window, the F1 and
  F2 panels and the fonts are drawn at the screen's own resolution instead of stretched by Windows.
  The window size ("resolution") is in the screen's real pixels, and it keeps its size on a
  monitor with another scale.
- Long paths in the launcher and the games (a release deep in a folder tree).
- XDVDFS and STFS file names are read as Windows-1252; STFS packages with two copies of each hash
  table follow the current one through every level.

### Fixed

- **With F2 open, Dance Central 3 ran at 20 FPS**: the panel repainted between the game's frames
  and filled the swap chain's queue. It is now drawn with the game's frames (60 FPS with it open).
- A cancel while 7-Zip was starting didn't stop it.
- Removing a game said "Checking the game…".

## [0.5.0] - 2026-10-20

Third game: Kinect Adventures. The virtual Kinect is steadier, quicker and more precise, and
corrects a tilted webcam.

### Added

- **Kinect Adventures** (title `4D5308ED`, base executable and its "Exclusive Pre-order Pack"),
  recompiled whole. It boots at 60 FPS; its menus, Rallyball and River Rush were played with the
  scripted player. Reflex Ridge and Space Pop load; a person has yet to try them.
- **Games on Demand copies**: `x360tool content-info`, `god-extract` (SVOD disc images) and
  `stfs-extract` (STFS packages). The launcher converts a Games on Demand copy, from an archive or
  a folder, into a game folder and installs the game's downloadable content; title updates and
  other games' content next to it are skipped.
- **The Kinect Guide gesture**: the left arm out and down at 45 degrees for 1.5 s opens the system
  UI, as on the console. Dance Central 3 and Kinect Adventures pause and show their menus.
- **The webcam's tilt**: a webcam pitched down or rolled is measured, from the player's body while
  they stand still and then from the floor under their feet (which also gives the webcam's height),
  and every joint is levelled. F2 shows it; it can be set by hand.
- **F2 > Kinect**: the color image the game gets with the skeleton over it and each joint's state,
  where the latency goes (waiting, pose model, mapping, images, reaching the game), the frames the
  bridge dropped. The bridge's log gets a summary every 30 s.
- A benchmark of the pose bridge on noisy synthetic landmarks (`tools/pose_bridge/benchmark.py`),
  with limits in its tests.
- The scripted player's whole-body moves: jump, crouch, lean, step, walk in and out, hands up,
  arms out, flap.
- `ANYX360_NUI_BRIDGE`: attach a pose bridge started by hand.

### Changed

- Joint precision, measured with the benchmark against 0.4.0: lag 60-76 ms → 3-17 ms; error while
  dancing 71 → 48 mm (200 → 50 mm with the webcam 15 degrees down); a reach towards the camera
  84 → 33 mm; a planted foot's jitter 31 → 17 mm. Each joint's x and y come from the image
  landmarks, the body's distance is filtered on its own, each joint has its own filter with a
  little prediction, and planted feet are held still. Walking towards the webcam lags more
  (55 → 95 ms).
- Skeleton frames follow the pose bridge's: a frame reaches the game 1-3 ms after the bridge
  publishes it instead of up to 33 ms, stamped with the camera's capture time.
- The play space is measured only with the whole body in view at 1.2-4.5 m, from a second of
  frames, on the measured floor.
- Joint states don't flicker between tracked and inferred; a body cut off by the webcam's own view
  is reported as clipped (the game asks the player to step back); two players crossing keep their
  IDs.
- Kinect Adventures' own skeleton smoothing is skipped: the bridge filters already, and filtering
  twice only added lag.
- Images aren't converted again when the webcam has no new frame, and per-frame buffers are reused.
- Pose bridge protocol 1.1 (tilt, timings, the streams the game uses).

### Fixed

- Pose bridge: changing camera or mirroring no longer freezes the skeleton; a slow model load
  isn't taken for a hang; a frame that fails is skipped instead of ending the bridge; a person
  missed for a moment keeps their pose instead of vanishing.
- The game's Kinect calls no longer wait while the bridge's frame is copied or the bridge starts.
- A new tilt estimate no longer moves the player across the play space.
- Downloadable content: 42-character package names (they were cut to 41), shared by every player;
  folders deeper than 260 characters.
- Notification listeners can be waited on (Kinect Adventures spun on one).
- Avatar calls fail cleanly instead of claiming success: Reflex Ridge no longer crashes loading.
- Kinect Adventures' identity request is answered, so "IDENTIFYING" goes away.
- XEX import libraries whose names are padded (`x360tool xex-info`).

### Known issues

- With the F2 panel open during a Dance Central 3 song the game runs at 20 FPS.
- Kinect Adventures has no avatars, and Reflex Ridge and Space Pop haven't been started by the
  scripted player.

## [0.4.0] - 2026-10-19

One release for every game, with a launcher that shows the player's games as a gallery.

### Added

- One release, `AnyX360-<version>.zip`, with every game and one launcher, `AnyX360.exe`. Its
  catalog of games is generated from each game's `game.json`, so a new game only needs its own
  folder under `games/` and a section of the release README (`tools/package/<id>/section.txt`).
- The launcher's gallery: a tile per game the player added, with the game's dashboard icon (read
  from `default.xex`) over its color, or a cover the player chooses (right click → Change cover).
  Right click also plays, opens the game's logs, prepares it again or removes it from the library.
- "Add game" recognises which game a copy is by its title ID, from an archive (only `default.xex`
  is extracted to check it) or a folder, and says which games the release plays when it isn't one
  of them.
- `x360tool xex-icon`: writes a title's dashboard icon (the XDBF title image).

### Changed

- Games added from an archive are kept in `games\<id>\game`, logs in `logs\<id>`. A copy prepared
  with a 0.3.0 package is added again with "Add game" → "From a game folder" (its `game` folder).
- The terms of use (version 1.2) name every game and are accepted once for the launcher.
- `make_dist.ps1` packages every game the build has (no `-Game`); the pose bridge is included once
  when a game is played with a webcam.
- The extraction progress bar only moves forward.

## [0.3.0] - 2026-10-19

### Changed

- Launcher: the game can be prepared from a `.rar` or `.zip` archive as well as a `.7z`. The game
  is found inside whatever folders the archive has, so a Gears of War 3 disc and a Dance Central 3
  copy both work as they come.

## [0.2.0] - 2026-10-17

Second target: Dance Central 3 (title 373307D9, base disc executable), the Kinect dance game,
played with a webcam.

### Added

- Dance Central 3, recompiled whole. It boots, plays its intro, menus and songs at 60 FPS, and
  scores what the player dances.
- Virtual Kinect: the Kinect (NUI) library linked into the game is replaced at its API by a sensor
  on the host. It produces skeleton frames, color and depth image streams and the camera's
  elevation. Its players come from the webcam, the mouse or scripted poses.
- Webcam pose bridge (`tools/pose_bridge`):
  - MediaPipe pose estimation for up to two players, turned into Kinect skeletons, color and depth
    images.
  - The game starts it, restarts it if it fails and ends it on exit.
  - Its build uses pinned, hash-checked packages and models and runs a self-test.
- F2 Kinect tab:
  - the sensor's state, the camera's frame rate and latency, and a preview of the skeletons;
  - the source, the webcam, mirroring, field of view, smoothing, players and the pose model;
  - "Recentre on me" and "Restart camera".
- F1 overlay: a Kinect line.
- Kernel and XAM:
  - AES (`XeCryptAesKey`, `XeCryptAesEcb`, `XeCryptAesCbc`), for the encrypted songs;
  - more user, content, input, UI and networking functions;
  - the Kinect Guide shows as system UI, and the title pauses for it.
- Launcher:
  - prepares a game from the folder of the player's copy (used in place), as well as from a `.7z`;
  - checks the title ID and the game's required files.
- Packaging: `make_dist.ps1 -Game <id>`. Dance Central 3's package ships the pose bridge and its
  licenses, plus a README on setting up the webcam.
- Testing aids:
  - `ANYX360_NUI_SCRIPT`, scripted players, also from a file read while the game runs;
  - `ANYX360_NUI_SOURCE`, `ANYX360_NUI_TRACE` and `ANYX360_POSE_BRIDGE`;
  - `tools/scripts/find_helpers.py` and `nui_recon.py`.

### Changed

- All games share one host entry point and per-game build files (`games/<id>/game.json`).
- Terms of use 1.1: each game's name, owners and trademarks, and how a webcam's images are
  handled.
- The launcher's text depends on the game (a controller or keyboard game, or a webcam game).

### Fixed

- The display vblank thread spun when the monitor was in power saving.
- XenonRecomp: executables encrypted with the devkit key, and more instructions.
- XenonAnalyse: switches indexed by a function's result, and tables read across unconditional
  branches.

## [0.1.0] - 2026-10-16

First version distributed to testers. Target: Gears of War 3 (title 4D5308AB, base disc
executable), the player's own copy.

### Added

- Static recompilation of the whole Gears of War 3 executable with forked XenonRecomp and
  XenonAnalyse (jump tables, function bounds, missing instructions).
- Kernel and XAM reimplementation (memory, threads, dispatcher objects, files, content and
  profiles, notifications, local sockets) so the title boots, plays the campaign and private Horde.
- Rendering through Xenia's Direct3D 12 GPU back end, with a persistent shader and pipeline cache.
- XMA audio decoding and XAudio2 output, paced by the output device.
- Display options: 720p to 4K window, render scale 1x-3x, AMD FSR/CAS or bilinear upscaling,
  FXAA, fullscreen, 30 or 60 FPS, VSync.
- Settings panel (F2), performance overlay (F1) and persistent settings (`settings.toml`).
- Keyboard and mouse controls with Gears-style bindings and raw mouse look; Xbox controllers.
- Local split screen for up to four players, with per-player profiles and saves.
- Window title read from the game's own XDBF data.
- Frame timing instrumentation (`ANYX360_FRAMETIMES`, `tools/scripts/frametimes.py`).
- Windows launcher in English and Spanish: terms of use, extraction of the player's `.7z`, game
  logs and error reporting.
- Distribution packaging (`tools/package/make_dist.ps1`) with a README, the terms of use,
  third-party licenses and this changelog.
- Versioning: `VERSION`, version shown in the launcher, the game log and the F2 panel, and Windows
  file version information in the executables.

### Fixed

- Unstable frame pacing: precise vblank locked to the display's refresh, dispatcher waking only
  the waits a signal satisfies, the overlay no longer forcing presents on the UI thread.
- Crash after about five minutes of campaign (`NetDll_inet_addr` read its string from the wrong
  argument).
- Bink video artifacts, audio distortion and a 30 FPS cap.

[Unreleased]: https://github.com/danielbarretoes/AnyX360/compare/v0.10.0...HEAD
[0.10.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.9.0...v0.10.0
[0.9.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.7.1...v0.8.0
[0.7.1]: https://github.com/danielbarretoes/AnyX360/compare/v0.7.0...v0.7.1
[0.7.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/danielbarretoes/AnyX360/releases/tag/v0.1.0
