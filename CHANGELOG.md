# Changelog

All notable changes to AnyX360 are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). The version is in the
[VERSION](VERSION) file; builds add the git commit as build metadata (`0.1.0+0b3cb8d`).

## [Unreleased]

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

[Unreleased]: https://github.com/danielbarretoes/AnyX360/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/danielbarretoes/AnyX360/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/danielbarretoes/AnyX360/releases/tag/v0.1.0
