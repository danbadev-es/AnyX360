# Kinect Party: target notes

These are working notes for the ninth title, the third from Xbox Live Arcade: Kinect Party. Every
value below comes from its `default.xex`, read with `x360tool xex-info`. The full dump is in
[games/kp/notes/xex-info.txt](../games/kp/notes/xex-info.txt).

| Item | Value |
|---|---|
| Developer | Double Fine Productions (Microsoft Studios, Xbox Live Arcade, 2012) |
| Title ID / media ID | `5841125C` / `2194D7E6`, version 0.0.0.3 |
| Source used | The **Xbox Live Arcade** package (content type `0xD0000`) and the "Kinect Party - Full Unlock" pack (content type 2) |
| XEX | AES-encrypted with the retail key, base `0x82000000`, entry `0x8233D018` |
| Image | 14 MiB: 10.9 MiB code (`.text` and Bink's), 1.2 MiB data, 1.9 MiB read-only |
| XDK | 2.0.21119 (February 2012); links NUI, NUISP (speech), NUIHNDL, NUIAUD, XUI, XAudio2, FMOD, Bink |
| Imports | `xboxkrnl.exe`: 211 functions; `xam.xex`: 145 functions ([list](../games/kp/notes/imports.txt)) |
| Engine | Double Fine's Buddha engine with Lua scripts, in `dfpf` packs (`360/Packs`) |

Kinect Party shows the camera picture with the players cut out and puts them in its "channels": 36
short activities with effects around the players (kung fu boards to break, a city to stomp as a
giant, space invaders to shoot...). The channels change on their own (auto play), and a channel
guide, the "hub", lets you pick one.

## Status (2026-10-20)

- **Boot**: the Microsoft Studios and Double Fine logos with the host, the loading screens, then
  the first channel (Kung Fu). 30 FPS (see Pending).
- **The camera picture and the cut-out**: the game cuts the players out of the colour picture with
  the depth and player index as the colour camera sees them (see Kinect). With the scripted player
  the picture is the virtual sensor's cartoon (a dim room, the body as coloured capsules); the game
  shows the player's shape over its own scenery, follows the arms and puts its effects on them.
- **Channels played with the scripted player**: Kung Fu (boards, jars and bricks come in to be
  broken), Monster Attack (the player as a giant in a black-and-white city, then the newspaper
  front page), Invaders (from the Full Unlock: the player has butterfly wings and shoots the
  invaders; the score goes up).
- **The channel guide (hub)**: START on a controller opens it (after the channel's photo recap).
  It is played with the hand: raise a hand to take control, hold it on the left or right arrow to
  scroll the stagehands carrying the channels, hold it on a stagehand to tune in (the camera goes
  "ON AIR" and the channel loads).
- **The Full Unlock**: works (see below). Without it the hub advertises "36 channels" and "Unlock
  the full game", and the locked channels play as a scrambled "trial mode" picture; with it every
  stagehand carries a channel and Invaders, a pack channel, plays.
- **Two players**: both are cut out (each in its own colour on the scripted source) and both break
  Kung Fu's objects ("POW" on the first player's arm with the second in view).
- **Language**: the game follows the profile's language (Spanish on a Spanish Windows: "CARGANDO...",
  "SALIR", "AUTO JUEGO").

### The channels

From the game's own table (`Data/Config/Default.cfg`, `ActivityLicenses`): the free channels need
no licence; the others need the bit of the pack that sells them, and the Full Unlock gives them all.

| Channel | Licence | Tried |
|---|---|---|
| Kung Fu | free | ✅ played with the scripted player: cut-out, arms followed, objects to break |
| Monster Attack | free | ✅ played (auto play after Kung Fu): the giant in the city, the newspaper recap |
| Dress Up, Balloons, Future Booth, Funhouse Mirror, Kaleidoscope | free | not tried |
| The Gilliams (15 micro-channels: bowling, chickens, monsters, salon, turkey...) | free | not tried |
| Invaders | `0x80` (Full Unlock) | ✅ played: butterfly wings, shooting, the score |
| Side Scroller, Ball Pit, Dance Party, Pigeon Chaser, Arkanoid, Hot Lava, Fireworks, Magic Hands, Seventies Video, Growing Planes, Fish Tank, Growing Garden, Blizzard, Tesla, Voxel Runner, Parrot, Video Booth, Grid World, Dubstep, Bath Time, Rain Cloud, Paint, Ribbon Dancer, Constellation, Building Blocks, Popcorn, Slit Scan | `0x8`-`0x40000000` (Full Unlock) | seen on the hub's stagehands; not played yet |
| The trial's scrambled channel | (no pack) | ✅ shown as "trial mode" with the player's coloured outline |

Nothing was seen failing: the untried channels are untried, not known broken.

## The Full Unlock

The pack (`5841125C\00000002\CFB557...`, 57 KB, one file `Ez.dlc`, an empty `DLCManifest`) is a
licence. The game enumerates its downloadable content at start-up, opens each pack as `DLC0:`,
`DLC1:`... with `XContentCreate` and keeps the licence mask it returns; each channel needs its bit
(`ActivityLicenses`), and bit `0x2` is "All Unlock". On a console the mask is the OR of the pack's
licence entries that apply; this copy's 16 entries all apply to any console and user
(ID `FFFFFFFFFFFFFFFF`) and add up to `0xFFFFFFFF`, which is what the runtime returns for any
content it opens. So nothing had to change: once the launcher installs the pack as any DLC
(`<user>\content\00000002\<package>\Ez.dlc`), every channel is open. Without it the game sees no
pack, `DLCTrialLicense` (`0x1`) applies and only the free channels play.

## Kinect

The NUI library is linked into the executable, a later build (2.0.21119) than any game before, and
sits in the middle of the image (`826A3000`-`826B4000` for the API). The same generic virtual sensor
([runtime/src/nui](../runtime/src/nui)) replaces it at its API boundary; the bindings are in
[games/kp/src/nui_bindings.cpp](../games/kp/src/nui_bindings.cpp), the names, signatures and
evidence in [nui_api.md](../games/kp/notes/nui_api.md). Most entry points are Fruit Ninja Kinect's
with addresses and offsets masked.

- **Images in the other camera's view**: this library offers the depth image as the colour camera
  sees it (`NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX_IN_COLOR_SPACE`, 320x240), and the game cuts its
  players out with it, then cleans the mask itself (depth fixing, blob shrink and grow, flicker
  reduction). The runtime now makes these images for any title: the virtual cameras share their
  optical centre, so the depth image is resampled along the colour camera's rays; the colour
  camera's wider view has no depth beyond the depth camera's edges (as with the Kinect's own
  registration).
- **The depth-to-colour mapping** (`NuiImageGetColorPixelCoordinatesFromDepthPixel`) is generic
  too: the library computes it from the camera's calibration tables, which it only reads when it
  starts the real camera. The game maps skeleton points to the screen through it (191 call sites:
  every effect that follows a hand). It is consistent with the images by construction: a point in
  view lands on the colour pixel where the virtual colour camera sees it, which is where the bridge
  warps the webcam picture to.
- **The rest** as in Fruit Ninja Kinect: the automatic tilt completes at once, the player in view
  is player 1 for the identity, the camera's exposure and white balance switches are kept. The game
  can ask for a 2x digital zoom from its scripts (a view area); the virtual camera has none and keeps
  the whole picture (logged once if it happens; not seen yet).

## Recompiler configuration

- **Register save/restore helpers**: found with `tools/scripts/find_helpers.py`; the addresses are
  in [config.toml](../games/kp/config.toml).
- **setjmp/longjmp**: the CRT's (`82495920`, `82495BF0`, the same code as Fruit Ninja Kinect's),
  called in 7 and 5 places, Lua's error handling among them. The third candidate find_helpers.py
  lists (`823237C0`) is a switch.
- **Jump tables**: 317 (2 fixed patterns, 315 dataflow). One, in the audio code (`823AE340`), is a
  switch that another switch on the same index dispatches to, with its table followed by code that
  is no case of it; the fork's analyser now accepts such a table when its length is that of an
  earlier bounds check on the same register.
- **Instructions**: `fnmadd`, and the vector unpacks of four float16s (241 places) and four shorts
  (5), and the pack of four shorts (5): `vupkd3d128`/`vpkd3d128` types 5 and 4. They were
  recompiled as traps, and the game stopped at its first scene. All in the fork, branch
  `anyx360-kp`.

## Routes (scripted player)

Times are seconds from the start; the scripted player is `ANYX360_NUI_SCRIPT` (a file with
`@<file>`, which can be appended to while the game runs). Loading takes 60-110 s on a first run
(shaders); Kung Fu starts at about 75-110 s.

- **Boot to Kung Fu and play it**: `3:enter`, then arm moves, e.g.
  `120:arm_right_60_30,124:wave/4,130:lower,135:dance/40`. The objects come in and break on the
  player's arms; after about 80 s the photo recap, then Monster Attack (auto play).
- **The hub with a controller**: `ANYX360_INPUT=130:START` (during Kung Fu): the photo recap, then
  the hub at about 150 s. With the scripted player in the hub (live script lines): `arm_right_70_30`
  raises the hand (the "raise your hand" sign goes); `arm_right_130_0` holds it on the left arrow and
  `arm_right_40_0` on the right one (the stagehands scroll); held about 2 s on a stagehand near the
  middle (`arm_right_60_0` to `arm_right_90_0`) it tunes in that channel (Invaders in the test run,
  after a scroll). The hand's mapping to the hub isn't charted finely yet.
- **Two players**: `3:enter,4:step_left,6:enter@1,7:step_right@1`, then each player's moves with
  `@1` for the second.

## Pending

- **30 FPS**: the game presents 30 frames a second, at boot, on the hub and in the channels, with
  one vblank per frame. Whether that is the game's own rate (the console's) or a limit here hasn't
  been measured yet (`ANYX360_FRAMETIMES`); the goal was 60 FPS on the menus.
- **Opening the hub without a controller**: START opens it, and the hub itself is played with the
  hand. On the console the hub is also reached by voice ("Xbox, ..." through NUISP, the speech
  library), which needs the Kinect's microphones: AnyX360 has none. The Kinect Guide gesture, both
  hands up, a raised hand and leaving the view don't open it. Without a controller the channels
  still change on their own (auto play). Next: find what else queues the hub in the scripts
  (`QueueHubWithSpeech`, `AdvanceActivity` in `data/script/ezactivities/activityhub.lua`, inside the
  compressed `Man_Script` pack), and whether the hub's "AUTO JUEGO" (auto play) chair, turned off,
  brings the hub back after every channel.
- **The channels not tried**: all but Kung Fu, Monster Attack and Invaders (table above). Their
  props are on the hub; a route per channel is the next step (scroll, then hold the hand on it).
- **At most 2 people**: the virtual sensor tracks two players (the pose model finds two people);
  the real Kinect put six in the picture.
- **The cut-out with a webcam**: not measured yet. The bridge's mask is MediaPipe Pose's
  segmentation at the inference size plus bone capsules, rendered into the 320x240 depth image and
  resampled into the colour camera's view; the game then cleans it itself. A person segmentation
  model at the colour resolution, edges refined against the colour picture and the capsules kept
  inside the mask are the planned improvement, to be kept only if it looks better in the game; the
  user's check with the webcam comes first.
- **Photos**: the game takes photos of the players and asks to share them ("Share is save");
  sharing needs Xbox Live. Not tried.
- **Speech**, **Xbox Live** (leaderboards, sharing, the marketplace): not available.

## Reproducing

From the arcade package (`<package>` is the folder that holds `5841125C`):

```
build.bat
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\5841125C\000D0000\<file>" extracted\kp\game --verify
build\tools\x360tool\release\x360tool.exe xex-extract extracted\kp\game\default.xex extracted\kp\default.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\kp\default.bin
cd games\kp
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\kp\game\default.xex switch_tables.toml
mkdir recomp4
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release kp
```

XenonRecomp writes into `out_directory_path` from `config.toml`, and that folder must exist and be
empty before it runs. To try the Full Unlock in a developer build, extract the pack from
`<package>\5841125C\00000002\` with `stfs-extract` to
`<user>\content\00000002\<package file name>\` (the launcher does it when it adds the game).
