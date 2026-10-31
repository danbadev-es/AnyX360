# Dance Central 3: target notes

These are working notes for the second title, Dance Central 3, a game played only with the Kinect.
Every value below comes from its `default.xex`, read with `x360tool xex-info`. The full dump is in
[games/dc3/notes/xex-info.txt](../games/dc3/notes/xex-info.txt) (the title update's executable:
[xex-info_0.0.1.5.txt](../games/dc3/notes/xex-info_0.0.1.5.txt)).

| Item | Value |
|---|---|
| Engine | Harmonix Milo (`ham_xbox_h.exe`; "ham" is Dance Central's internal name) |
| Title ID / media ID | `373307D9` / `2BE86B86`, version 0.0.0.5, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 2375 files, 6.2 GB: `default.xex`, `gen\main_xbox.hdr` + 10 ARK archives, Kinect identity and speech data), its title update and 20 downloadable content packages |
| XEX | AES-encrypted with the retail key, basic compression, base `0x82000000`, entry `0x827FC450` (`0x827FDA00` in the update) |
| Image | 16.1 MiB: 10.2 MiB code, 2.4 MiB data, 3.6 MiB read-only |
| Functions in `.pdata` | 49,865 |
| XDK | 2.0.21173, compiler C1/C2 16.0.11886; links NUI, NUIAUD, NUISP and FITNESS (Kinect), XHV2 and XMIC |
| Imports | `xboxkrnl.exe`: 185 functions + 13 variables; `xam.xex`: 154 functions ([list](../games/dc3/notes/imported_functions.txt)) |
| Middleware | Bink video (`BINK*`), RAD, libjpeg, Ogg Vorbis (song audio, decoded on the CPU; songs are AES-encrypted) |
| Target version | The executable patched with the title update (`TU_0RJ61UP_000000K000000.0000000000085`): 0.0.0.5 to **0.0.1.5** (see [Title update](#title-update)) |

## The copy
Since 0.29.0 the port is made from a Games on Demand copy with its title update and downloadable
content. Until then it was made from a re-packed dump of the disc (the same version and media ID),
which the user has replaced.

- **The same game.** Recompiled with the same configuration, the Games on Demand copy's base
  executable gives the same code as the dump's, except in three functions and two imports: the dump
  had been patched to decrypt its songs' keys with `XeCryptAesKey`/`XeCryptAesCbc` and an AES state
  on the stack, where the retail executable uses the title's key slots (`XeKeysSetKey`,
  `XeKeysAesCbc`: [tu_addresses.md](../games/dc3/notes/tu_addresses.md#the-song-keys)). The runtime
  has those two now. The dump was also encrypted with the devkit key, and its allowed media and
  minimum system versions were changed.
- **The archive** (`DC3-DLC-TU-GOD.rar`, 7.8 GB) holds `373307D9\00007000\A9080A6703719ECBD565`
  (+ `.data`, 40 data files), the title update `TU_0RJ61UP_000000K000000.0000000000085` at its root
  (the console cache's form), 20 downloadable content packages in `373307D9\00000002\` and 10 avatar
  items in `373307D9\00009000\` (content type 0x9000: T-shirts, outfits, a helmet, a dance
  animation), which the launcher leaves out.

## Title update
The game is recompiled from its title update's executable, as Kinect Adventures and Resident
Evil 6 are ([ANYX.md](ANYX.md)). The update (`TU_0RJ61UP_000000K000000.0000000000085`, the console
cache's form of a `000B0000` package, "Dance Central 3 Title Update", 2.9 MB, 140 files) holds:

- `default.xexp`, a delta patch from 0.0.0.5 to 0.0.1.5. The patched image keeps the base's layout
  and size, but the code moved (Milo's start-up by `+0xA38`, the NUI library by `+0x15B0`) and the
  entry point is `0x827FDA00`. The imports are the same.
- `gen\patch_xbox.hdr` and `patch_xbox_0.ark`: Milo's patch archive, read from `UPDATE:`.
- `grammar_update\<language>\*.cfg`: voice command grammars (voice isn't supported).

How it is played:
- **Recompiling**: `patch_file_path`/`patched_file_path` in config.toml, output `recomp7`.
  XenonAnalyse finds 189 jump tables (2 table-driven `bctr` left), as on the base.
- **Addresses**: every address in `src/` was found again in the patched image by instruction shape
  ([games/dc3/notes/tu_addresses.md](../games/dc3/notes/tu_addresses.md)).
- **At start** the game patches `default.xex` (cached in `<user>\cache\patched`) and mounts the
  update as `update:`. Without it, it ends at once saying where the update goes. The launcher
  installs it from the archive (or the `TU_` file alone) and shows "Needs its title update" until
  it is there.

## Song keys
Each song (`.mogg`, Ogg Vorbis encrypted with AES-CTR) has its key derived through the console's
security engine: the game calls `XeKeysSetKey` on slot `0xE0` with a key from a table in its
executable (`0x82D13000`, 4 entries of 16 bytes; the entry is chosen by the song file's version:
12 and 13 the first, 14 to 16 the next three), then `XeKeysAesCbc` on one 16-byte block.

- **The table holds the keys as the console obscures them**, and the hypervisor turns them back.
  AnyX360 can't, and with the obscured keys every song stayed on its loading screen ("CARGANDO").
- **The real values** were in the re-packed disc dump the port was first made from: it had the two
  calls patched to software AES (`XeCryptAesKey`/`XeCryptAesCbc`) with the real keys in the same
  table. That dump survives in the user's `.anyx` backup of it; compared with the Games on Demand
  executable, the table is the only data that differs.
- **`keys\title_keys.txt`** (git-ignored, [keys/README.md](../keys/README.md)) maps each obscured
  key to its real value; `tools/scripts/title_keys.py` makes it from the two executables. The game
  opts in (`GameDescriptor::title_keys`): without the file it doesn't start, saying so, and a key the
  file lacks ends it with a message. The release copies the file next to the retail key.
- **Dance Central 2's table** (`0x82CA2C38`, 4 entries) holds the same four obscured keys, so the
  same file serves it.

## Sections
The section list is in [games/dc3/notes/sections.txt](../games/dc3/notes/sections.txt)
([the update's](../games/dc3/notes/sections_0.0.1.5.txt)). Code lives in `.text`
(`0x822D0000`–`0x82CE8EFC`, `0x82CEAF8C` in the update), `BINK` and `RADCODE`.

## Recompiler configuration
- **Executable**: the re-packed dump was encrypted with the devkit key, so XenonRecomp's loader
  falls back to it (a change in the fork); the Games on Demand copy uses the retail key.
- **Register save/restore helpers**: each pattern in XenonRecomp's README occurs exactly once.
  `tools/scripts/find_helpers.py` finds them; the addresses are in
  [config.toml](../games/dc3/config.toml).
- **setjmp/longjmp**: the CRT's (`0x82C64770`/`0x82C64340` in the update), configured since the
  update build. The XGraphics shader microcode assembler and the D3DX effect code use them for errors.
- **Jump tables**: the fork's dataflow detector finds 189. Two table-driven `bctr` stay unresolved;
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
- **Skeletons**: `NUI_SKELETON_FRAME`s in the XDK layout, big-endian, one for each of the pose
  bridge's frames (at most every 28 ms, at least every 50) or on a 30 Hz clock for the other
  sources.
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
2. Their poses are placed in front of the webcam (x and y from the image landmarks, depth from the
   metric pose) and turned into the Kinect's 20 joints and axes.
3. Each joint is filtered (One-Euro per joint, with prediction; planted feet held).
4. The webcam's tilt is corrected (level, or set by hand, by default; measured on request). The
   virtual sensor stands where the webcam is, or, if F2 says so, the scene is moved to where a
   Kinect expects a player (the play space). Legs out of the picture stand still under the player.
5. The color and depth images are synthesized from the webcam image and the segmentation masks.

The details and a benchmark are in [tools/pose_bridge/README.md](../tools/pose_bridge/README.md).

### Settings and tools
- **F2 > Kinect** shows:
  - the sensor's state;
  - the camera's frame rate and latency, and where it goes (waiting, pose, mapping, images,
    reaching the game);
  - the color image the game gets with the skeletons over it, each joint by its state;
  - the webcam's measured tilt and height.

  It also changes the source, the camera, mirroring, field of view, smoothing, players, the pose
  model, the tilt (by hand or measured) and where the game sees the player (where they are, or
  moved to the Kinect's spot, with "Recentre on me"), and has "Restart camera".
- **F1** adds a Kinect line.
- **Debugging environment variables**:

| Variable | Effect |
|---|---|
| `ANYX360_NUI_SCRIPT` | Scripted player instead of the configured source: `"5:enter,8:menu_right,12:swipe_left,20:dance/60"`. `@<file>` reads a file and keeps reading what is appended, so a game can be driven live; actions are listed in `synthetic_source.cpp` |
| `ANYX360_NUI_SOURCE` | `webcam`, `mouse`, `synthetic` or `none` for this run |
| `ANYX360_NUI_TRACE` | Logs the NUI calls |
| `ANYX360_POSE_BRIDGE` | The command that starts the bridge (e.g. the venv's `python.exe -I ...\pose_bridge.py --synthetic`); the game appends its arguments |
| `ANYX360_NUI_BRIDGE` | A channel name: the game creates it and doesn't start the bridge, so one started by hand (`--shm <name> --event <name>-frame`, e.g. in a debugger) attaches |

**Automated runs never open the webcam.** They use `ANYX360_NUI_SCRIPT`, `ANYX360_NUI_SOURCE=none`
or a bridge in `--synthetic` mode.

## Reproducing
From the archive extracted into `extracted\dc3\pkg` (the folder that holds `373307D9` and the `TU_`
file):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract extracted\dc3\pkg\373307D9\00007000\A9080A6703719ECBD565 extracted\dc3\game
build\tools\x360tool\release\x360tool.exe stfs-extract extracted\dc3\pkg\TU_0RJ61UP_000000K000000.0000000000085 extracted\dc3\update
build\tools\x360tool\release\x360tool.exe xex-patch extracted\dc3\game\default.xex extracted\dc3\update\default.xexp extracted\dc3\update\default_0.0.1.5.xex
build\tools\x360tool\release\x360tool.exe xex-extract extracted\dc3\update\default_0.0.1.5.xex extracted\dc3\default_patched.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\dc3\default_patched.bin
cd games\dc3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\dc3\update\default_0.0.1.5.xex switch_tables.toml
mkdir recomp7
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release dc3
powershell -ExecutionPolicy Bypass -File tools\pose_bridge\build_bridge.ps1
```

XenonRecomp writes into `out_directory_path` from `config.toml` (`recomp7`), and that folder must be
empty before it runs. To regenerate, point `out_directory_path` at a new folder, or pass
`-DDC3_PPC_DIR=<folder>` to CMake.

To play a development build:
- the update's files go in `<user>\content\000B0000\TU_0RJ61UP_000000K000000.0000000000085`
  (without `default_0.0.1.5.xex`);
- each downloadable content package, extracted (`x360tool stfs-extract`), in
  `<user>\content\00000002\<package file name>`;
- `keys\title_keys.txt` must be there ([Song keys](#song-keys)).

The launcher does the first two from the archive.

## Bring-up notes
- **Main loop through an exception**: Milo's `App::Run` (`0x823124A0` in the update, `0x82311A68`
  on the disc) installs an unhandled-exception filter, then stores to address 0 on purpose; the
  filter (`0x82312478`, `0x82311A40` on the disc) runs the main loop. There is no guest exception dispatch, so `games/dc3/src/milo_hooks.cpp` calls the
  filter directly.
- **Switches the analyser missed**: `XGetLocale`'s result indexes a 37-entry table in Milo's
  startup. XenonAnalyse took the call's result for a constant, so the switch became an indirect
  call whose cases ran as separate functions. Calls now end the dataflow.
- **Songs are encrypted**: their keys come through `XeKeysSetKey`/`XeKeysAesCbc` and the game's own
  AES code decrypts the audio. A wrong key, or a missing function, leaves the
  loading screen waiting forever ([Song keys](#song-keys)).
- **Pacing**: the title presents on every vblank, so the vblank runs at the console's 60 Hz. The
  game runs at 60 FPS only (`GameDescriptor::fixed_fps`).

## Downloadable content
The launcher installs the copy's 20 packages (content type 2) in `<user>\content\00000002\`: 11
single songs, 5 artist packs of two songs (Usher, Justin Bieber, Lady Gaga, Rihanna, Pitbull) and
4 import packs (Dance Central: 32 songs; its downloadable songs: 32; Dance Central 2: 44; its
downloadable songs: 32), about 160 songs. **None of them plays yet**:

- The game enumerates them (`XamContentAggregateCreateEnumerator`, 20 packages), but opens each
  through `XamContentCreateInternal`, which it looks up by ordinal (`0x271`). Without that ordinal
  answered it opens none and lists only the disc's songs: that is how the game ships now.
- With the ordinal answered (tried, not shipped), it mounts all 20, reads their `songs.dta`,
  rebuilds its song cache and lists their songs. Starting one (an import pack's "Poker Face", the
  single "What Makes You Beautiful", "Galang '05") shows "La descarga de contenido a la que has
  accedido ya no está disponible o el perfil de jugador propietario no ha iniciado sesión en Xbox
  LIVE" and goes back to the title screen; scrolling the list over them often crashes the game in
  its stream receiver (`sub_82629A48`, a song preview that was never opened). No file inside the
  packages is opened before that: the game refuses them without reading them.
- Not the cause (each tried): the license mask the open returns (all bits, or only bit 0, as the
  packages' headers give), the owner of the packages in the enumeration, enumerating them only for
  every player. The packages' own state in the game's content manager reaches "mounted". Next: the
  song's content check before its preload (`ContentMgr`, the message `content_read_failure`, sent
  from `82C1DFA4` when the preload fails).

The 10 avatar items in the archive (content type 0x9000) are left out by the launcher.

## Graphics
- **Render target path**: Xenia's default host render target path (`rtv`) blew the menus' glass
  panels out to white (the panel behind the main menu's picture, the song list's left and middle
  panels), the same on the 0.28.0 build of the disc copy. Xenia's rasterizer-ordered view path
  (`rov`), which packs the render targets exactly, draws them as on the console, so the game sets
  `render_target_path_d3d12 = "rov"` (`GameDescriptor::xenia_options`). It costs GPU time: during a
  song 6.4 ms a frame on an RTX 5060 Laptop (2.5 ms on `rtv`), at a steady 60 FPS; the first time
  each scene is drawn its pipelines are compiled (a few seconds at 37-50 FPS while a song loads). A
  player whose GPU can't keep up can go back with `[xenia] render_target_path_d3d12 = "rtv"` in
  `settings.toml`.

## Status with the title update (2026-10-31)
Played with the scripted player on the build recompiled from 0.0.1.5, from the Games on Demand
copy installed by the launcher, with a copy of the user's save:
- **The save** loads (no first-run tutorials) and keeps working: a song's score is written to it
  and shown in the song list afterwards.
- **Menus**: title screen, main menu, BAILAR, ACTUAR, the song list, difficulty and START.
- **A song to the end**: "Better Off Alone", Easy, dancing: 105,578 points and the results screen.
  Before the title keys every song stayed on its loading screen.
- **Pause**: the Kinect Guide gesture (`guide`) and a controller's Start both pause a song ("Now
  That We Found Love") and its menu opens; Continue goes back to it.
- **Downloadable content**: not playable yet ([above](#downloadable-content)).

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
  - Since 0.5.0 the virtual sensor recognises the Kinect Guide gesture (the left arm out and
    down at 45° for 1.5 s, measured against the torso) and opens the system UI, as the console
    does: the song pauses and its menu (Continue, Rehearse, Game guide…) appears. Verified with
    the scripted player.
- **0.5.0 check**: with the scripted player, "Better Off Alone" on Easy scored 121,314 after a
  minute of dancing, at 60 FPS. With the F2 panel open during a song the game runs at 20 FPS (the
  0.4.0 build too); closed, 60.
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
