# Changelog

All notable changes to AnyX360 are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). The version is in the
[VERSION](VERSION) file; builds add the git commit as build metadata (`0.1.0+0b3cb8d`).

## [Unreleased]

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

[Unreleased]: https://github.com/danielbarretoes/AnyX360/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/danielbarretoes/AnyX360/releases/tag/v0.1.0
