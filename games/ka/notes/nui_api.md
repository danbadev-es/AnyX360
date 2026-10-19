# Kinect Adventures: the NUI API at its call sites

The Kinect library linked into the executable (NUI 2.0.11775, a launch-era build), named from the
evidence in [nui_entry_points.md](nui_entry_points.md) (arguments, use of the results, imports
reached) and the callers' code, compared with Dance Central 3's later library
([../../dc3/notes/nui_api.md](../../dc3/notes/nui_api.md)). Unlike Harmonix, the game doesn't wrap
the calls in named profiling scopes. Bindings: [../src/nui_bindings.cpp](../src/nui_bindings.cpp).

The library spans about `82D10000`-`82D80000`: the API around `82D23E00`-`82D2E000`, and the
skeleton tracking itself (pure floating point) after it: in this XDK the title runs the tracking,
from depth frames it reads from the camera through `PsCamDeviceRequest`. The game's own Kinect layer
is around `82A50000`-`82A70000` and `82B60000`-`82B80000`.

## Core

| Address | Name | Signature as called | Evidence | Binding |
|---|---|---|---|---|
| `82D25C88` | NuiInitialize | `(0x849, hardware thread 5)` | creates the library's threads, registers its tracing (82A561A4) | `nui::Initialize` |
| `82D23E30` | NuiShutdown | `()` | unregisters, closes handles (82A53AF0) | `nui::Shutdown` |
| `82D26620` | NuiSkeletonTrackingEnable | `(event, flags 3)` | 82A54288; the same caller disables it | `nui::SkeletonTrackingEnable` |
| `82D25DE0` | NuiSkeletonTrackingDisable | `()` | 0xB0 bytes, as Dance Central 3's | `nui::SkeletonTrackingDisable` |
| `82D26768` | NuiSkeletonGetNextFrame | `(timeout up to 33 ms, NUI_SKELETON_FRAME*)` | 82A5531C, frame at `this+0x17D00`, number at +8 | `nui::SkeletonGetNextFrame` |
| `82D25EF8` | NuiSkeletonSetTrackedSkeletons | `(DWORD ids[2])` | 82B6D838, 82B6D8B0 | `nui::SkeletonSetTrackedSkeletons` |
| `82D26190` | NuiTransformSmooth | `(NUI_SKELETON_FRAME*, NULL)` | 82A558C0 on the frame; pure | original code |
| `82D28E28` | depth pixel to color pixel | `(color resolution 2, view area, x, y, depth, *x, *y)` | 82B761D0; pure | original code |
| `82D2AAB0` | NuiImageStreamOpen | `(type, resolution, flags 0, frame limit 1, event 0, HANDLE*)` | `(1, 2)`, `(0, 1)`, `(7, 0)` at 82A53CFC, 82A53FA8, 82A53FF8 | `nui::ImageStreamOpen` |
| `82D28FB8` | NuiImageStreamGetNextFrame | `(stream, 0 ms, const NUI_IMAGE_FRAME**)` | 82A54308 | `nui::ImageStreamGetNextFrame` |
| `82D292C8` | NuiImageStreamReleaseFrame | `(stream, frame)` | 0x14C bytes, as Dance Central 3's | `nui::ImageStreamReleaseFrame` |
| `82D2B678` | NuiCameraElevationGetAngle | `(LONG* angle, status*)` | reaches XamNuiCameraElevationGetAngle | `nui::CameraElevationGetAngle` |
| `82D2BF98` | elevation adjustment | `(32, ...)` | reaches XamNuiCameraElevationGet/SetAngle; 0x1E4 bytes, as Dance Central 3's | returns 0 |

## Structures

The same as Dance Central 3's: `NUI_SKELETON_FRAME` with the six `NUI_SKELETON_DATA` at +0x30,
0x1C0 bytes apart (the game's code uses the stride 448 72 times); `NUI_IMAGE_FRAME` with `+0x0C`
type, `+0x10` resolution, `+0x14` `D3DTexture*` (82A54320 checks type 1 and resolution 2, then locks
the texture and copies 0x12C000 bytes).

## Image formats (82D2AAB0)

| Type | Resolution | Size | Bytes/pixel | D3DFORMAT | Frame bytes |
|---|---|---|---|---|---|
| 0 depth and player index | 1 | 320x240 | 2 | `0x1A220058` | |
| 1 color | 2 | 640x480 | 4 | `0x28287EB2` | `0x12C000` |
| 2 color YUV | 2 | 640x480 | 2 | `0x1A20000C` | `0x96000` |
| 4 depth | 1 | 320x240 | 2 | `0x1A220058` | |
| 7 small player map | 0 | 80x60 | 2 | depth's | |

Texture headers: XGSetTextureHeaderEx `82D713A0`, XGOffsetResourceAddress `82D71690` (called from
82D2B278, 82D2B284).

## Camera settings (`camera` is always 0; small wrappers add it)

| Address | Use | Binding |
|---|---|---|
| `82D28A08` | set setting 0 or 2 (→ `82D28450(0, 0, setting, value)`) | kept |
| `82D27F70`, `82D27F80` | get a setting (`(0, setting, value*)`; 0, 2, 5) | read back |
| `82D28A28` | set a region of 4 floats | kept |
| `82D27688` | get it (`(0, float[4]*)`, stack r1+96 as in Dance Central 3) | read back |
| `82D28A18` | `(0, property)`: 0, 5, 6, 7, 8 | returns 0 |
| `82D27608` | `(buffer*)`, result tested | fails |

## Identity and system UI

| Address | Use | Binding |
|---|---|---|
| `82D2D948` | enrollment info: `(index, info*)` | nobody enrolled |
| `82D2D6C0`, `82D2D840` | requests with a callback (`82B67F18`, `82B67E38`) | fail |
| `82D2D6B8`, `82D2DB58` | identity state | fail / 0 |
| `82D2DB60` | the Kinect troubleshooter (reaches XamShowNuiTroubleshooterUI) | 0 |

`82D270F8` and `82D40388` sit in the range but create threads (the XAPI thread pool): not NUI.
