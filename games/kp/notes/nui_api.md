# Kinect Party: the NUI API at its call sites

The Kinect library linked into the executable (NUI 2.0.21119, the February 2012 XDK, with NUISP,
NUIHNDL and NUIAUD), named from the evidence in [nui_entry_points.md](nui_entry_points.md)
(`tools/scripts/nui_recon.py` over `826A0000`-`826C0000`) and the callers' code. Most entry points
match Fruit Ninja Kinect's library (2.0.20500) exactly with addresses and offsets masked
([../../fnk/notes/nui_api.md](../../fnk/notes/nui_api.md)). Bindings:
[../src/nui_bindings.cpp](../src/nui_bindings.cpp).

The API sits around `826A3000`-`826B4000`; the library's internals (identity, head orientation,
fitness, the colour/depth registration on the GPU) follow it up to about `826D0000`. Double Fine's
Kinect layer ("Natal") is around `82A43000`-`82A51000`: `82A501C8` initialises the library and
creates the Kinect manager (global `82DAEEA8`) only if that succeeds; `82A4CF18` maps a skeleton
point to the screen through the colour image (191 call sites, the activities' effects).

## Core

| Address | Name | Signature as called | Evidence | Binding |
|---|---|---|---|---|
| `826ABB48` | NuiInitialize | `(0x480B, hardware thread 5)` | `82A501C8`; creates the library's threads, registers its tracing, sets the tilt callback | `kp::Initialize`: `nui::Initialize`, then "NUI binding changed" |
| `826ADA00` | NuiSkeletonTrackingEnable | `(event 0, flags 0 or 2)` | `82A50238` at start-up, then each activity | `nui::SkeletonTrackingEnable` |
| `826AD380` | NuiSkeletonTrackingDisable | `()` | the activities; clears the tracking flag and sets its events | `nui::SkeletonTrackingDisable` |
| `826ADBA0` | NuiSkeletonGetNextFrame | `(timeout 10, NUI_SKELETON_FRAME*)` | `82A4D288`; FNK's `821B3870` | `nui::SkeletonGetNextFrame` |
| `826AD4F8` | NuiSkeletonSetTrackedSkeletons | `(DWORD ids[2])` | every activity; FNK's `821B30F8` | `nui::SkeletonSetTrackedSkeletons` |
| `826AE978` | transform from gravity | `(matrix*, vector)` | pure; FNK's `821B4648` | original code |
| `826AD160` | NuiImageStreamOpen | `(type, resolution, flags 0, frame limit 1, event 0, HANDLE*)` | `(1, 2)` and `(4, 1)` at `82A47078`, `(0, 1)` and `(7, 0)` at `82A4CB80`; FNK's `821C3CC0` | `nui::ImageStreamOpen` |
| `826AC520` | NuiImageStreamGetNextFrame | `(stream, 0 or 33 ms, const NUI_IMAGE_FRAME**)` | FNK's `821C3120` | `nui::ImageStreamGetNextFrame` |
| `826AC848` | NuiImageStreamReleaseFrame | `(stream, frame)` | FNK's `821C3448` | `nui::ImageStreamReleaseFrame` |
| `826ACFF8` | NuiImageGetColorPixelCoordinatesFromDepthPixel | `(color resolution 2, view area 0, x, y, packed depth, LONG*, LONG*)` | `82A4CF18`; calls `826AC408`, which reads the camera's registration tables (`82D63D80+0x2AF0`) and fails when the library didn't start the camera | `nui::ColorPixelFromDepthPixel` (generic) |

## Image formats (8208C3A8 per type, 8208C438 per stream)

| Type | Name | Resolution | Stream | Size | D3DFORMAT |
|---|---|---|---|---|---|
| 0 | DEPTH_AND_PLAYER_INDEX | 1 | 0 | 320x240 | `0x1A220058` |
| 1 | COLOR | 2 | 2 | 640x480 | `0x28287EB2` |
| 2 | COLOR_YUV | 2 | 5 | 640x480 | `0x1A20000C` |
| 3 | DEPTH | 1 | 0 | 320x240 | `0x1A220058` |
| 4 | DEPTH_AND_PLAYER_INDEX_IN_COLOR_SPACE | 1 | 1 | 320x240 | `0x1A220058` |
| 5 | DEPTH_IN_COLOR_SPACE | 1 | 1 | 320x240 | `0x1A220058` |
| 6 | COLOR_IN_DEPTH_SPACE | 2 | 3 | 640x480 | `0x28287EB2` |
| 7 | DEPTH_AND_PLAYER_INDEX_80x60 | 0 | 4 | 80x60 | `0x1A220058` |
| 8 | DEPTH_80x60 | 0 | 4 | 80x60 | `0x1A220058` |

The game cuts its players out of the colour picture with type 4: the depth and player index as the
colour camera sees them. The virtual sensor makes it by resampling its depth image along the colour
camera's rays (the virtual cameras share their optical centre). The game then does its own
processing on it (`DepthFixer`, blob shrink and grow, flicker reduction, the composite mask).

Each frame's texture header is made with the title's XG library as the library does (`826ACB5C`):
XGSetTextureHeaderEx `82620DF0`, XGOffsetResourceAddress `82621150`.

## The motor and the camera

| Address | Use | Binding |
|---|---|---|
| `826AF6F0` | elevation `(LONG* angle, status*)` (XamNuiCameraElevationGetAngle) | `nui::CameraElevationGetAngle` |
| `826AF7D8` | set the elevation `(angle)` | `nui::CameraElevationSetAngle` |
| `826B0138` | automatic tilt (flags 8 from `82A4B340`); FNK's `821D15D0` | the overlapped completed at once, 997 |
| `826B34D0` | camera property `(camera 0, property, value)`: activities switch automatic exposure and white balance | kept |
| `826B3470` | view area `{digital zoom, pan x, pan y}` from a Lua binding (`82A4B930`) | accepted and ignored: no zoom |

## Identity

| Address | Use | Binding |
|---|---|---|
| `826A3FB0` | enrollment slot info `(slot, {user, flags}*)`; FNK's `821B2AF0` | slot 0 is player 1 |
| `826A3CC8`, `826A3E88` | identity requests with callbacks; FNK's `821B2808`, `821B29C8` | original code |
| `826AEDE8`, `826AF008` | head orientation / fitness (state at `82D72788`) | original code: not started, they fail and the game goes on |
