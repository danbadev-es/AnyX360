# Roadmap

Milestones are measured by what a game does, not by lines of code. The first target was
**Gears of War 3** ([GEARS3.md](GEARS3.md)), released to testers as 0.1.0. The second is
**Dance Central 3** ([DANCECENTRAL3.md](DANCECENTRAL3.md)), a Kinect game played with a webcam, and
the third **Kinect Adventures** ([KINECTADVENTURES.md](KINECTADVENTURES.md)).

## Gears of War 3

### Phase 0: Tooling (done)
- [x] Repository layout, CMake + clang-cl + Ninja build, `build.bat`
- [x] `x360tool iso-*`: XDVDFS extraction (stripped xiso, XGD2, XGD3)
- [x] `x360tool xex-info`: XEX2 headers, execution info, imports, static SDK libraries, page map
- [x] `x360tool xex-extract`: AES-128 (retail or devkit key) + none/basic/LZX decompression → PE image,
      plus `*.imports.txt` and `*.sections.txt`
- [x] End-to-end tests on synthetic fixtures (`ctest`)

### Phase 1: Inventory (done)
- [x] Extract the game (came as an extracted disc folder in a 7z, not an ISO)
- [x] `xex-info` on `default.xex`: title ID, version, compression, XDK build
- [x] `xex-extract`: decrypt with the retail key; PE image and sections verified
- [x] Kernel and XAM imports with names: 163 + 117 functions, 13 variables
- [x] Function count estimate: 41,369 in `.pdata`
- [x] Target version: base disc executable, no Title Update

### Phase 2: CPU recompilation (done)
- [x] Fork XenonRecomp and XenosRecomp into `thirdparty/` and build them with this tree
- [x] Locate `__savegprlr_14` / `__restgprlr_14`, FPR and VMX helpers
- [x] Jump tables: dataflow detector in the XenonAnalyse fork
- [x] Function boundaries for leaf functions containing switches
- [x] Implement the missing instruction kinds
- [x] Recompile the full executable and link it against the runtime
- [ ] Unit-test the new instructions against Xenia's PPC test suite (XenonTests)

### Phase 3: Boot (done)
- [x] Loader, threads, TLS, dispatcher objects, memory, files, content and profiles
- [x] The game reaches its main loop, with a log of unimplemented imports

### Phase 4: Graphics (done)
- [x] Rendering through Xenia's Direct3D 12 back end, with a persistent shader and pipeline cache
- [x] Menus, campaign and Horde render

### Phase 5: Audio, input, saves (done)
- [x] XMA decoding and XAudio2 output
- [x] XInput controllers; keyboard and mouse
- [x] Saves and settings in `%LOCALAPPDATA%`

### Phase 6: PC features (0.1.0)
- [x] 720p to 4K output, render scale, FSR/CAS upscaling, FXAA, 30 or 60 FPS, VSync
- [x] Local split screen and private Horde
- [ ] MSAA/SMAA, anisotropic filtering, configurable FOV
- [ ] LAN multiplayer (a stretch goal; Xbox Live is gone)

## One release for every game (0.4.0, done)
- [x] One launcher, `AnyX360.exe`, with a catalog of the games the build has (`games_catalog.h`)
- [x] A gallery of the player's games with each game's dashboard icon (XDBF, `x360tool xex-icon`)
      or a cover they choose; games recognised by title ID from an archive or a folder
- [x] `.7z`, `.rar` and `.zip` archives
- [x] One release folder and zip with every game, one README, one set of terms

## Dance Central 3

### Phase A: More than one game (done)
- [x] A shared host entry point and game descriptor; per-game CMake (`game.json`)
- [x] Per-game launcher text and terms; preparing a game from its folder; title ID checks
- [x] `make_dist.ps1 -Game <id>`

### Phase B: Boot (done)
- [x] XenonRecomp: devkit key fallback, new instructions, dataflow fixes for missed switches
- [x] Milo's main loop behind a deliberate null store (`milo_hooks.cpp`)
- [x] Missing XAM and kernel imports, AES for the encrypted songs
- [x] The title boots, renders at 60 FPS and reaches the main menu

### Phase C-D: The virtual Kinect (done)
- [x] The NUI API named from its call sites (`tools/scripts/nui_recon.py`, `notes/nui_api.md`)
- [x] Skeleton frames and image streams in the XDK layouts; the title's own texture headers
- [x] Scripted and mouse sources; live scripts for driving the game
- [x] Menus, a song and its scoring with the scripted player (dancing scores, standing still doesn't)

### Phase E-F: The webcam (done, in testing)
- [x] The pose bridge protocol, process supervision and tests with a fake producer
- [x] The pose bridge: MediaPipe, Kinect joints and axes, play space, filtering, tracking, images
- [x] Its build: pinned and hash-checked packages, pinned models, PyInstaller, licenses, self-test
- [x] F2 Kinect tab, F1 Kinect line
- [ ] Verified by people with webcams: left and right, feel, scoring, latency, two players, pause

### Next
- [x] The pause gesture: the Kinect Guide gesture opens the system UI and the title pauses
- [ ] Voice commands from a microphone (speech recognition)
- [x] Other Kinect titles on the same virtual sensor (Kinect Adventures, 0.5.0)

## Kinect Adventures (0.5.0)

### Games on Demand and DLC (done)
- [x] `x360tool content-info`, `god-extract` (SVOD), `stfs-extract` (STFS), with synthetic fixtures
- [x] The launcher converts a Games on Demand copy and installs its DLC; skips title updates and
      other games' content

### Boot and play (done, in testing)
- [x] Recompiled whole (new instructions, 243 jump tables); boots at 60 FPS
- [x] Its NUI library (2.0.11775) bound to the virtual sensor; identity answered
- [x] Waitable notification listeners, DLC content names and long paths, avatars that fail cleanly
- [x] Rallyball and River Rush played by the scripted player; Kinect Guide gesture pauses
- [ ] Reflex Ridge, Space Pop and 20,000 Leaks played by a person
- [ ] Avatars

## A better virtual Kinect (0.5.0)
- [x] Stability: a frame that fails is skipped, a short miss holds the pose, the source polled off
      the sensor's lock, a slow model load isn't taken for a hang
- [x] The webcam's tilt measured (body, then floor) and corrected; set by hand in F2
- [x] Joints from the image landmarks, per-joint filters with prediction, the distance filtered on
      its own, planted feet held, joint state hysteresis, clipping from the webcam's view
- [x] A benchmark on noisy synthetic landmarks, with limits in the tests
- [x] Frames on the bridge's pace with capture time stamps; F2 shows the image with the skeleton
      and where the latency goes
- [ ] Verified by people: the precision against the 30-60 % of steps recognised before
- [ ] One pose model run when only one player plays (the second person is looked for every frame)
