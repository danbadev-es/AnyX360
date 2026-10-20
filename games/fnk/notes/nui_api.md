# Fruit Ninja Kinect: the NUI API at its call sites

The Kinect library linked into the executable (NUI 2.0.20500, the February 2011 XDK), named from the
evidence in [nui_entry_points.md](nui_entry_points.md) (arguments, use of the results, imports
reached) and the callers' code, compared with Kinect Adventures' and Dance Central 3's libraries
([../../ka/notes/nui_api.md](../../ka/notes/nui_api.md), [../../dc3/notes/nui_api.md](../../dc3/notes/nui_api.md)).
Bindings: [../src/nui_bindings.cpp](../src/nui_bindings.cpp).

The library sits low in the image, after the XAPI: the API around `821B2700`-`821D3000`, the
skeleton tracking itself (pure floating point) after it. Halfbrick's engine wraps it in a small
Kinect layer around `8266C000`-`8268F000` with three threads of its own ("SkeletonTrackingUpdate",
"DepthUpdate", "ColorUpdate", started in `8266D848`); the menus' user handling is around
`82685000`-`82698000` and `826CB000`-`826D3000`.

## Core

| Address | Name | Signature as called | Evidence | Binding |
|---|---|---|---|---|
| `821C2918` | NuiInitialize | `(0xC09, hardware thread -1)`; `0xC0B` when the color stream is wanted | `8266D848`; creates the library's threads, registers its tracing, sets the tilt callback | `fnk::Initialize`: `nui::Initialize`, then the system's "NUI binding changed" notification |
| `821C04E8` | NuiShutdown | `()` | `8266CD60` | `nui::Shutdown` |
| `821B3720` | NuiSkeletonTrackingEnable | `(event, flags 2)`: the title chooses whom to track | `8266D8C4` | `nui::SkeletonTrackingEnable` |
| `821B3870` | NuiSkeletonGetNextFrame | `(0 ms, NUI_SKELETON_FRAME*)` | `8266CA80` waits on the event, then fetches into one of two 0xAB0-byte frames | `nui::SkeletonGetNextFrame` |
| `821B30F8` | NuiSkeletonSetTrackedSkeletons | `(DWORD ids[2])` | `8266C960` picks the two tracking IDs (the closer first) | `nui::SkeletonSetTrackedSkeletons` |
| `821B4648` | transform from gravity | `(matrix*, vector in v1)` | pure; `8266C2F0` builds it from the frame's normal to gravity (`+0x20`, `(0, 1, 0)` when zero) and turns the joints with it | original code |
| `821C3CC0` | NuiImageStreamOpen | `(type, resolution, flags 0, frame limit 1, event, HANDLE*)` | `(1, 2, …)` color, only when the game asked for it; `(0, 1, …)` depth and player index | `nui::ImageStreamOpen` |
| `821C3120` | NuiImageStreamGetNextFrame | `(stream, 0 ms, const NUI_IMAGE_FRAME**)` | `8266D1A0` (depth) and `8266D348` (color), each after waiting on its event | `nui::ImageStreamGetNextFrame` |
| `821C3448` | NuiImageStreamReleaseFrame | `(stream, frame)` | the same threads | `nui::ImageStreamReleaseFrame` |

## Structures

The same as Dance Central 3's and Kinect Adventures': `NUI_SKELETON_FRAME` (0xAB0 bytes) with six
0x1C0-byte `NUI_SKELETON_DATA` at `+0x30`; `NUI_IMAGE_FRAME` at `+8` of the library's 120-byte
frame record, with `+0x0C` type, `+0x10` resolution, `+0x14` `D3DTexture*`.

- `8266CF18` copies a frame into the game's own texture: it locks the frame's texture with the D3D
  library's LockRect (`8220FFD0`, read-only) and copies the rows. The shadow drawn behind the fruit
  (`effects\shadowman\`) is made from the depth image's player indices.
- `826CD9C0` keeps the frame's floor clip plane (`+0x10`) and normal to gravity (`+0x20`) at
  `8286B6B4` and `8286B6A4` for the calibration, and hands each player's skeleton to `826CD068`.
- `8266D4F0` pairs signed-in users with bodies **by the skeletons' user index (`+0x0C`) alone**:
  for each skeleton whose user index is below 4, the user's entry gets its tracking ID (and its
  enrollment index, `+0x08`, unless -1). The XDK fills `+0x0C` with `XUSER_INDEX_NONE` (254) for
  bodies no profile is bound to; the virtual sensor does the same and binds the fully tracked bodies
  in order to the signed-in players.

## Image formats (82001C10, 82001CA0)

`82001C10` has 16 bytes per `NUI_IMAGE_TYPE` (stream, initialisation flags it needs, resolution,
name); `82001CA0` has 28 bytes per stream (frame bytes, width, height, bytes per pixel, two
D3DFORMATs).

| Type | Resolution | Size | Bytes/pixel | Frame D3DFORMAT |
|---|---|---|---|---|
| 0 depth and player index | 1 | 320x240 | 2 | `0x1A220058` |
| 1 color | 2 | 640x480 | 4 | `0x28287EB2` |
| 2 color YUV | 2 | 640x480 | 2 | `0x1A20000C` |
| 3 depth | 1 | 320x240 | 2 | `0x1A220058` |
| 7 depth and player index | 0 | 80x60 | 2 | `0x1A220058` |
| 8 depth | 0 | 80x60 | 2 | `0x1A220058` |

Texture headers: XGSetTextureHeaderEx `823D4220` and XGOffsetResourceAddress `823D43D8`, called from
`821C376C` and `821C3780` with the same arguments as in Kinect Adventures.

## The motor and the calibration

| Address | Use | Binding |
|---|---|---|
| `821D15D0` | automatic tilt: `(flags, …, f1-f3, players*, XOVERLAPPED*)`; flags `0x50` from the calibration screen, `8` to go back to the default angle. Reaches XamNuiCameraElevationGet/SetAngle. Returns 997 (pending), 170 (busy), 56 or 1237; `8268DCA8` polls the overlapped and then calls the calibration's completion | the overlapped completed at once, 997 |
| `821D0C88` | stop the motor | 0 |

The calibration (`826D19F0`, states NONE / WAITING_FOR_SKELETON / WAITING_FOR_DATA / MOVING /
WAITING_FOR_PLAYER / COMPLETE) checks the framing in `826D16F0`: from the floor plane's pitch and
the camera's height, the top of the view 1.8 m away must be 0.5 m (±0.2) above the player's head. If
not, it tilts and checks again, three times, then starts over. There is no motor and the view is
the webcam's, so `826D16F0` is answered "framed" (`fnk::PlayerFramed`).

## Identity

| Address | Use | Binding |
|---|---|---|
| `821B2AF0` | enrollment slot info: `(slot 0-7, {user, flags}*)`; 254 = no user. `82685370` asks for all eight when the system says the NUI binding changed (`82685C68`, notification `0x0006001D`) | slot 0 is player 1, who is always signed in |
| `821B2808`, `821B29C8`, `821B2D08`, `821B2E38` | identity requests (Kinect ID) from the game's user handling (`826CB548`-`826CD068`) | original code: the library was never initialised and answers "not ready", which the game accepts |
