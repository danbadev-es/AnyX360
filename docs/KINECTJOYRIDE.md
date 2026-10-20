# Kinect Joy Ride: target notes

These are working notes for the ninth title: Kinect Joy Ride (BigPark, 2010), a kart racer played
by holding an imaginary steering wheel. Every value below comes from its `default.xex`, read with
`x360tool xex-info`. The full dump is in [games/kjr/notes/xex-info.txt](../games/kjr/notes/xex-info.txt).

| Item | Value |
|---|---|
| Developer | BigPark (Microsoft Game Studios, 2010) |
| Title ID / media ID | `4D53093B` / `21ED97FD`, version 0.0.0.1, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 83 files, 1.42 GiB), two DLC packages, 26 avatar items and Title Update 3 |
| XEX | `wheelsf.exe`: AES-encrypted with the retail key, LZX compression, base `0x82000000`, entry `0x8270CED0` |
| Image | 26.88 MiB: 16.75 MiB code (`.text`), 6.75 MiB data, 3.38 MiB read-only |
| XDK | 2.0.11775 (the Kinect launch); links NUI (tail `0x4006`), NUISP, ST, XAVATAR, D3D9, XAudio2, XHV2 and the online libraries |
| Imports | `xboxkrnl.exe`: 174 functions and 13 variables; `xam.xex`: 145 functions ([list](../games/kjr/notes/imports.txt)) |
| Target version | The base game; no Title Update |

The disc also carries the console's avatar asset pack (`AvatarAssetPack`, 13.8 MB, as Kinect
Sports'), the game's avatar awards and a system update. The game's data are BigPark packages
(`*.bpk`): `data.bpk` (365 MB), the front end (`fe_whiteroom.bpk`), one per track and event
(`crimsongorge_*`, `perilouspeak_*`, `sunsetharbour_*`, `highjump_*`, `airtime*`, `demolition*`) and
one per car.

## Recompiler configuration
- **Register save/restore helpers** and **setjmp/longjmp** (`829FD5A0`, `829FD870`: word for word
  the CRT's in Kinect Sports; the other longjmp candidates are ordinary functions):
  [config.toml](../games/kjr/config.toml).
- **Jump tables**: 484 from the dataflow detector, 3 of them dispatched on the target address. The
  5 table-driven `bctr` left are calls through tables of function pointers, correct as indirect
  calls.
- **Instructions**, added to the fork (branch `anyx360-kjr`): `vrlw`/`vrlw128` (8 uses), and
  `vupkd3d128` with four half floats (type 5, 46 uses): without it the game hit a trap as soon as a
  race loaded.

## Kinect
The NUI library is 2.0.11775, one QFE before Kinect Adventures' and Kinect Sports' (`0x4006` against
`0x4007`), linked at `82CE47E8`-`82CFA000`. Each function the game calls (every `bl` or `b` into the
range from outside it) was matched to Kinect Sports' or Kinect Adventures' function by its
instructions with immediates and branch targets masked: the same code, only the addresses of the
library's globals differ. The bindings ([games/kjr/src/nui_bindings.cpp](../games/kjr/src/nui_bindings.cpp))
are theirs at these addresses; the table is in [notes/nui_api.md](../games/kjr/notes/nui_api.md).
The game enables and disables skeleton tracking with tail calls (`b`), which
`tools/scripts/nui_recon.py` doesn't list. The color camera's settings come in batches
(`82CEF788`/`82CF0480`): they are kept and read back.

The game's own Kinect layer (`827E0000`-`827F0000`) stays recompiled: the steering wheel, the
pushes, the leaning and the tricks are read from the skeleton by the game.

## Avatars
The drivers are Xbox Avatars. The avatar library is Kinect Sports' build (XAVATAR 2.0.11775): its
skeleton builder (`82CC8C30`, Kinect Sports' `82A84A00`) and `XAvatarGetAssets` (`82CC4188`) match
word for word, so the game runs with the avatar system (`GameDescriptor::avatar_system`) and gets
its avatars from the console's avatar asset pack ([AVATARS.md](AVATARS.md)), which its own disc
carries: on the first run it is copied to `%LOCALAPPDATA%\AnyX360\system`. The player is the
default avatar (the profile has none of its own); the game's rivals keep their looks (hats, gloves,
glasses, monocle, earrings). The avatars appear in the garage, the lobby, in the car and in the
photos' scenes.

## Downloadable content and avatar items
- **Kinect Joy Ride Chevy Volt** (61 KB, an unlock list: `UNLOCK_CAR_VOLT_STOCK`): works. The Volt
  (its car is on the disc) appears as the eleventh vehicle, "New", unlocked.
- **Chevrolet Car Pack** (15 MB: the Camaro, Corvette and Cruze in `archive.bpk`, a "resident"
  pack): needs Title Update 3. The base game mounts it and reads its manifest and unlock list, but
  never opens `archive.bpk` (`ANYX360_LOG_FILES=1`); the executable has no "resident" or
  "archive.bpk" string. The cars don't appear. game.json names it in `content_needs_update` and the
  launcher leaves it out, with its note.
- **The 26 avatar items** (content type `0x9000`: T-shirts, helmets, caps, gloves, shoes, the
  Daredevil outfit) are the game's avatar awards for the console's avatar editor. The game only
  enumerates content type 2 (`829E9810` calls `XContentCreateEnumerator(user, 0, 2, ...)`), so it
  doesn't use them; the launcher doesn't install them.
- **Title Update 3** (`default.xexp`, `update.bpk`): not supported, as for every title.

## Bring-up
- Nothing in the runtime was needed beyond the NUI bindings: the avatar system, LDI, content
  packages and saves (`title.dat`, `player.dat`) already worked for the earlier titles.
- **Pacing**: the game presents every second vblank (30 FPS, as on the console) and its clock
  counts vblanks. At two vblanks per frame with the 60 FPS setting (vblank at 120 Hz) it ran at
  60 FPS, but twice as fast: a race's timer went 20 s in 10 s of wall time, whatever the frame
  rate. It runs at 30 FPS only (`vblanks_per_frame` 2, `fixed_fps` 30: vblank 60 Hz): a race's
  timer then went 19.3 s in 19.2 s, at a steady 30 FPS.

## Status (2026-10-20)
- **Boot and title screen**: the intro, the title screen ("Pasa sobre el botón Empezar"), the hand
  cursor, the player "Controlando Player". The game speaks the console's language (Spanish here).
- **Menus**: the autosave notice, the welcome, the garage (fans, Vehicles, Play, Recent photos,
  achievements, settings, "Add me") with the player's avatar beside the car, the vehicle choice
  (eleven cars, the locked ones with the fans they need), the activity menu, the track choice
  (seven tracks), the how-to cards and the lobby ("¿Todo listo para jugar?"), the results, the fans
  earned and the session's best results, back to the menu. 30 FPS.
- **Races** (Pro Race, Curva del Faro, 3 laps, 8 cars): grabbing the wheel starts it (the lights,
  "¡YA!"); the player's avatar drives; the game steers with the player (with the wheel held level
  the car follows the track: the scripted player finished 5th and 7th of 8). Turning the wheel
  left and right turns the car (and into the fences at 45 degrees); pulling the hands back and
  pushing them out fires the turbo at the start and when the bar is charged; leaning in the air
  made tricks ("¡Bonita acrobacia!", the trick photo "Acrobalunático") and leaning on the ground a
  mini drift. The results list the times, the photos (the depth camera's silhouettes) and the fans
  (tokens, trick and drift bonus, place).
- **Battle Race** (Carrera de combate): starts, with items to pick up ("Prueba a utilizar tu
  objeto"); using an item wasn't tried.
- **Two players**: a second person walking in joins the lobby (one of the rivals, "Posh", becomes
  the guest's avatar; 1P and 2P buttons appear); with 2P the race is split screen, both steering,
  and finishes with both in the results. When the two stand closer than about a metre the game asks
  the left one to move left ("MUÉVETE A LA IZQUIERDA").
- **The other activities** (Stunt, Sprint, Smash, Trick, Xbox LIVE race) are offered in the
  activity menu; they weren't started. Xbox LIVE needs the online service, which is gone.
- **Saves**: fans, unlocks and the session are kept (`content\00000001\title.dat`, `player.dat`).

## Routes (scripted player)
The game was driven live (`ANYX360_NUI_SCRIPT=@<file>`, appending lines while it runs). The hand
cursor follows the right hand; `arm_right_<azimuth>_<elevation>` puts it on an item, which is chosen
after holding it there about 2 s. From a fresh start (loading times vary with the machine):

```
ANYX360_NUI_SCRIPT=3:enter,45:menu_right,45:arm_right_79_-24/0.3,51:arm_right_78_-37/0.3,56:lower
```

reaches the garage about 80 s in: Start on the title screen (`79_-24`), then Accept on the autosave
notice (`78_-37`). Then, as each screen comes (the aims marked * were computed from a fit of the
cursor's positions, not tried):

| Screen | Aim (`arm_right_...`) |
|---|---|
| Welcome (first run): Accept | `70_-41` |
| Garage: Play / Vehicles / Recent photos* | `72_-44` / `95_-42` / `54_-42` |
| Garage: achievements* / settings* / Add me* | `71_-9` / `66_-8` / `60_-7` |
| How-to card (first run): choose it | `78_-28` |
| Activity menu: Pro Race* / Stunt* | `78_-9` / `63_-9` |
| Activity menu: Battle Race / Sprint* | `76_-23` / `60_-22` |
| Activity menu: Xbox LIVE* / Smash* | `75_-33` / `57_-32` |
| Activity menu: How to play* / Trick*; Back | `73_-43` / `54_-41`; `106_18` |
| Track and vehicle choice: the card shown / next | `77_-29` / `51_-27` |
| Lobby: Play (one player) | `57_-30` |
| Lobby with two: 2P / 1P* | `56_-21` / `51_-34` |
| Results: Play again / Next | `59_-41` / `52_-36` |
| Fans tutorial (first time): Continue | `62_-40` |
| Fans earned: Play again* / New players* / Main menu | `64_-15` / `61_-29` / `58_-40` |

In the race:

```
wheel_0          grab the wheel: the race starts; held level, the car follows the track
boost            at the start, and whenever the turbo bar is charged
wheel_-35 ...    steer left; wheel_35 right; wheel_0 straight again
lean_back/1.2, lean_forward/1.2, lean_left/1.2, lean_right/1.2   repeated: tricks in the air, mini drifts
lower            after the finish, before the menus
```

A whole race held level: `wheel_0` and nothing else finishes it (about 3 minutes, 3 laps). Two
players: `enter@1` in the lobby, `menu_right` + `arm_right_56_-21` (2P, chosen by the player the
game calls the guest, the first body), then `wheel_0` and `wheel_0@1`; `step_left` if the game asks
the left player to move.

## Controls (webcam)
- **Menus**: raise a hand; the cursor follows it. Hold it over an item until the circle fills.
- **Steer**: hold both hands in front of your chest as if on a steering wheel, and turn it.
- **Turbo**: pull your hands back to your chest, then push them forward (at the start, and when the
  turbo bar is full).
- **Drift**: lean into a turn. **Tricks**: in the air, lean in any direction.
- **Items** (Battle Race): as the how-to card shows.
- **Join** (second player): jump in front of the camera in the lobby.
- **Pause**: the Kinect Guide gesture (left arm out and down at 45 degrees).

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `4D53093B`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\4D53093B\00007000\<file>" extracted\kjr\game
build\tools\x360tool\release\x360tool.exe xex-extract extracted\kjr\game\default.xex extracted\kjr\default.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\kjr\default.bin
cd games\kjr
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\kjr\game\default.xex switch_tables.toml
mkdir recomp3
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release kjr
```

XenonRecomp writes into `out_directory_path` from `config.toml` (`recomp3`), and that folder must
exist and be empty before it runs. To play the DLC in a developer build, extract each package
(`x360tool stfs-extract`) to `%LOCALAPPDATA%\AnyX360\KinectJoyRide\content\00000002\<package file name>\`.

## Pending
- **60 FPS**: the game presents every second vblank and its clock counts vblanks, so it runs at 30
  FPS. A real 60 FPS needs the game to present every vblank while its timing still advances at 60
  Hz: find where its present interval is set (the D3D swap is `826DBDB8`, which calls `VdSwap`; the
  graphics interrupt callbacks are registered at `826F4198` and `826F45D8`) and whether the
  simulation's step follows the vblank count (then presenting every vblank at 60 Hz would be the
  right speed) or is fixed per frame (then it would run twice as fast). The main loop is
  `823EB378` (thread "Sim"); it reads the time base each frame at `823EBA10` (stored at
  `this+0x17C`), and `823EC0C8` sets up its timer.
- **Frame rate under load**: at the earlier 60 FPS pacing, races and the results screen dropped to
  20-40 FPS (2P split screen to 18) while two other games ran on the test machine. At 30 FPS a race
  held 30.0 on a quieter machine; two players at 30 FPS weren't measured.
- **Not tried**: finishing Battle Race, using items, Stunt, Sprint, Smash and Trick, the other six
  tracks, unlocking cars with fans, the photo album, changing avatar ("Cambiar avatar"), "Añádeme"
  (adding an avatar), the settings, three or more people in view.
- **A person with a webcam**: the steering, pushes, drifts and tricks were only made by the
  scripted player (`wheel_*`, `boost`, `lean_*`); how the bridge's skeletons steer is untested.
- **The Chevrolet Car Pack** needs Title Update 3 (above).
- **Avatars**: the default avatar's face textures look sleepy (half-closed eyes) in the garage and
  the lobby: the eyes' frame may not be the one the game expects; the face shapes and hiding
  templates aren't applied ([AVATARS.md](AVATARS.md)); `XamAvatarLoadAnimation` (imported) fails.
- **The game's language** is the console's (Spanish on the test machine); it follows the profile
  language setting as the other games.
