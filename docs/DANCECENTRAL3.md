# Dance Central 3: target notes

These are working notes for the second title, Dance Central 3, a game played only with the Kinect.
Every value below comes from its `default.xex`, read with `x360tool xex-info`. The full dump is in
[games/dc3/notes/xex-info.txt](../games/dc3/notes/xex-info.txt).

| Item | Value |
|---|---|
| Engine | Harmonix Milo (`ham_xbox_h.exe`; "ham" is Dance Central's internal name) |
| Title ID / media ID | `373307D9` / `2BE86B86`, version 0.0.0.5, disc 1 of 1 |
| Source used | Extracted disc folder (2375 files, 6.2 GB): `default.xex`, `gen\main_xbox.hdr` + 10 ARK archives, Kinect identity and speech data |
| XEX | AES-encrypted with the **devkit** key (a re-packed dump), basic compression, base `0x82000000`, entry `0x827FC450` |
| Image | 16.1 MiB: 10.2 MiB code, 2.4 MiB data, 3.6 MiB read-only |
| Functions in `.pdata` | 49,865 |
| XDK | 2.0.21173, compiler C1/C2 16.0.11886; links NUI, NUIAUD, NUISP and FITNESS (Kinect), XHV2 and XMIC |
| Imports | `xboxkrnl.exe`: 185 functions + 13 variables; `xam.xex`: 154 functions ([list](../games/dc3/notes/imported_functions.txt)) |
| Middleware | Bink video (`BINK*`), RAD, libjpeg, Ogg Vorbis (song audio, decoded on the CPU; songs are AES-encrypted) |
| Target version | Base disc executable; no Title Update |

## Sections
The section list is in [games/dc3/notes/sections.txt](../games/dc3/notes/sections.txt). Code lives
in `.text` (`0x822D0000`–`0x82CE8EFC`), `BINK` and `RADCODE`.

## Recompiler configuration
- **Executable**: when the retail key doesn't give a PE image, XenonRecomp's loader now falls back
  to the devkit key (a change in the fork). x360tool and the runtime already did this.
- **Register save/restore helpers**: each pattern in XenonRecomp's README occurs exactly once.
  `tools/scripts/find_helpers.py` finds them; the addresses are in
  [config.toml](../games/dc3/config.toml).
- **setjmp/longjmp**: there are no candidates. libjpeg is linked, but without its setjmp error path.
- **Jump tables**: the fork's dataflow detector finds 180. Six table-driven `bctr` stay unresolved;
  they are listed at the end of [switch_tables.toml](../games/dc3/switch_tables.toml).

## Kinect: a virtual sensor at the NUI API

The skeleton tracking is not a system service. The NUI library is linked into the executable and
reads raw depth images from the camera driver (`PsCamDeviceRequest`). AnyX360 doesn't emulate the
camera. It replaces the library at its API boundary and feeds a virtual sensor instead:

- [games/dc3/src/nui_bindings.cpp](../games/dc3/src/nui_bindings.cpp) overrides the entry points
  with strong `sub_X` definitions (`X360_GUEST_FUNCTION` in `x360/hle.h`). A wrong address fails
  to link.
- The names, signatures and evidence for each entry point are in
  [nui_api.md](../games/dc3/notes/nui_api.md). They were recovered with
  `tools/scripts/nui_recon.py`, using Harmonix's profiling scopes, the arguments and the callers.
- The virtual sensor ([runtime/src/nui](../runtime/src/nui), `x360/nui.h`) is generic: another
  Kinect title only needs its own bindings.

| Range | What |
|---|---|
| `0x823BF000`–`0x823CD000` | Harmonix's Kinect layer; its init `0x823C53A0` wraps each NUI call in a named profiling scope |
| `0x82800000`–`0x82870000` | NUI library (1782 functions); about 48 entry points are called from the game |

### The virtual sensor
- **Skeletons**: a 30 Hz clock produces `NUI_SKELETON_FRAME`s in the XDK layout, big-endian.
  - It sets the title's skeleton event.
  - A body keeps its slot while it stays in view.
  - Up to `players` bodies are tracked. When the title chooses with `SetTrackedSkeletons`, its two
    choices are tracked instead.
- **Images**: the color stream (640×480, UYVY) and the depth stream (320×240, with player indices)
  live in guest physical memory.
  - The title's own D3D library writes the texture headers (`XGSetTextureHeaderEx`).
  - After every write the GPU's copy is invalidated.
- **Sources** (`[kinect] source`):
  - **`webcam`**: the pose bridge.
  - **`mouse`**: the right hand follows the cursor, for menus.
  - **`synthetic`**: scripted poses for testing.
  - **`none`**.
- **Elevation, identity, audio and speech**:
  - The camera elevation is kept and tilts the floor plane.
  - Nobody is enrolled for face identity.
  - Echo cancellation and speech recognition report failure, so the game turns voice off.

### The webcam: the pose bridge
The bridge is [tools/pose_bridge](../tools/pose_bridge/README.md), a Python program packaged with
PyInstaller. The game starts it in a job object, which kills it if the game dies, and restarts it
with backoff if it fails. The two sides share memory under the protocol in
[x360/nui_bridge.h](../runtime/include/x360/nui_bridge.h): a seqlocked set of three slots, plus a
control block that carries the game's settings.

The bridge works in four stages:

1. MediaPipe's Pose Landmarker estimates up to two people.
2. Their metric 3D poses are placed in front of the webcam, turned into the Kinect's 20 joints and
   axes, and moved to where a Kinect expects a player (the play space).
3. Each joint is filtered (One-Euro).
4. The color and depth images are synthesized from the webcam image and the segmentation masks.

### Settings and tools
- **F2 > Kinect** shows:
  - the sensor's state;
  - the camera's frame rate, pose time and latency;
  - the skeletons as the sensor sees them.

  It also changes the source, the camera, mirroring, field of view, smoothing, players and the
  pose model, and has "Recentre on me" and "Restart camera".
- **F1** adds a Kinect line.
- **Debugging environment variables**:

| Variable | Effect |
|---|---|
| `ANYX360_NUI_SCRIPT` | Scripted player instead of the configured source: `"5:enter,8:menu_right,12:swipe_left,20:dance/60"`. `@<file>` reads a file and keeps reading what is appended, so a game can be driven live; actions are listed in `synthetic_source.cpp` |
| `ANYX360_NUI_SOURCE` | `webcam`, `mouse`, `synthetic` or `none` for this run |
| `ANYX360_NUI_TRACE` | Logs the NUI calls |
| `ANYX360_POSE_BRIDGE` | The command that starts the bridge (e.g. the venv's `python.exe -I ...\pose_bridge.py --synthetic`); the game appends its arguments |

**Automated runs never open the webcam.** They use `ANYX360_NUI_SCRIPT`, `ANYX360_NUI_SOURCE=none`
or a bridge in `--synthetic` mode.

## Reproducing
The game folder is used where it is: `extracted\dc3\game` is a directory junction to it, and only
`default.xex` is copied to `extracted\dc3\` for the recompiler.

```
mkdir extracted\dc3
copy "<game folder>\default.xex" extracted\dc3\
mklink /J extracted\dc3\game "<game folder>"
build.bat
cd games\dc3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\dc3\default.xex switch_tables.toml
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat
powershell -ExecutionPolicy Bypass -File tools\pose_bridge\build_bridge.ps1
```

XenonRecomp writes into `out_directory_path` from `config.toml`, and that folder must be empty
before it runs. To regenerate, point `out_directory_path` at a new folder, or pass
`-DDC3_PPC_DIR=<folder>` to CMake.

## Bring-up notes
- **Main loop through an exception**: Milo's `App::Run` (`0x82311A68`) installs an
  unhandled-exception filter, then stores to address 0 on purpose; the filter (`0x82311A40`) runs
  the main loop. There is no guest exception dispatch, so `games/dc3/src/milo_hooks.cpp` calls the
  filter directly.
- **Switches the analyser missed**: `XGetLocale`'s result indexes a 37-entry table in Milo's
  startup. XenonAnalyse took the call's result for a constant, so the switch became an indirect
  call whose cases ran as separate functions. Calls now end the dataflow.
- **Songs are encrypted**: they are read through `XeCryptAesKey`/`XeCryptAesCbc`. Without them the
  loading screen waits forever.
- **Pacing**: the title presents on every vblank, so the vblank runs at the console's 60 Hz. The
  game runs at 60 FPS only (`GameDescriptor::fixed_fps`).

## Status (2026-10-17)
- **Booting and menus**: the game boots and plays its intro. The menus work with the scripted
  player, the mouse or the bridge.
  - The right arm selects with a swipe.
  - The left-hand items (back, options) work with the left arm.
  - The tutorials, the song list and the difficulty select all work.
- **A song (Perform, "Stereo Love", Easy)**:
  - Dancing (the scripted routine), the moves scored 10,802 before the song's free-scoring
    section.
  - Standing still, they scored 0 over the same stretch.
  - Both runs then got the same 31,800 in that section, which scores whoever is in view.
  - The dancing run finished with 42,602.
- **Pause**: an Xbox controller's Start pauses a song. A second press switches the menus to
  controller mode. The in-song menu gesture, which the Rehearse tutorial shows as the right arm up
  at 45°, didn't trigger with the scripted skeleton at any angle tried. Neither did the left arm
  down at 45°.
- **Webcam**: the bridge runs end to end in synthetic mode, both from the build and from the
  packaged distribution. The game tracks the dancer, renders its depth silhouette, and restarts
  the bridge when it is killed.
- **To verify with a real webcam and a person**:
  - left and right;
  - how the menus and the scoring feel;
  - the pause gesture;
  - latency;
  - two players;
  - the camera permission on a clean PC.
- **Not supported**: voice commands (no microphone support) and online features.
