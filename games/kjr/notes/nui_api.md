# Kinect Joy Ride: the NUI API at its call sites

The Kinect library linked into the executable is NUI 2.0.11775 (static library tail `0x4006`, one
QFE before Kinect Adventures' and Kinect Sports' `0x4007`). It spans about `82CE47E8`-`82CFA000`,
right after the avatar library (`82CC0000`-`82CE47E0`); the skeleton tracking (pure floating point)
follows it. The game's own Kinect layer is at `827E0000`-`827F0000`.

Every function the game calls was matched to Kinect Sports' (`ks`) or Kinect Adventures' (`ka`)
function by its instructions with immediates, displacements and branch targets masked (the
scratch scripts are described in [docs/KINECTJOYRIDE.md](../../../docs/KINECTJOYRIDE.md)); word by
word, only the addresses of the library's globals differ. Names and signatures are in
[../../ka/notes/nui_api.md](../../ka/notes/nui_api.md). Bindings:
[../src/nui_bindings.cpp](../src/nui_bindings.cpp).

[nui_entry_points.md](nui_entry_points.md) is `tools/scripts/nui_recon.py` over the range. It lists
calls (`bl`) only: the game enables and disables skeleton tracking with tail calls (`b` at
`827E7308`, `827E7310`), found by scanning every branch into the range.

## Core

| Address | Name | Matched | Called from | Binding |
|---|---|---|---|---|
| `82CEB078` | NuiInitialize | ks `829CF908` | `827E1508` | `nui::Initialize` |
| `82CE9220` | NuiShutdown | ka `82D23E30` | `827E13F8` | `nui::Shutdown` |
| `82CEBEF0` | NuiSkeletonTrackingEnable | ks `829D2BE0` | `b` at `827E7308`; `(event 0, flags 2)` | `nui::SkeletonTrackingEnable` |
| `82CEB678` | NuiSkeletonTrackingDisable | ks `829D23A0` | `b` at `827E7310` | `nui::SkeletonTrackingDisable` |
| `82CEC038` | NuiSkeletonGetNextFrame | ks `829D2D28` | `827E8DC8` | identity answers, then `nui::SkeletonGetNextFrame` |
| `82CEB790` | NuiSkeletonSetTrackedSkeletons | ks `829D24B8` | `827EC630` | `nui::SkeletonSetTrackedSkeletons` |
| `82CEBA28` | NuiTransformSmooth | ks `829D2750` | `827E8378` | `nui::TransformSmooth` |
| `82CECF10` | depth pixel to color pixel | ks `829CFE50` | `827EC7A0`, `827EC9E0` | original code (pure) |
| `82CEEB98` | NuiImageStreamOpen | ks `829D1AD8` | `827EB878`; `(1, 2)` | `nui::ImageStreamOpen` |
| `82CED0A0` | NuiImageStreamGetNextFrame | ks `829CFFE0` | `827EB908` | `nui::ImageStreamGetNextFrame` |
| `82CED3B0` | NuiImageStreamReleaseFrame | ks `829D02F0` | `827EBA08` | `nui::ImageStreamReleaseFrame` |

The image formats are Kinect Sports' (the same `ImageStreamOpen`); the texture header is made with
the game's XGSetTextureHeaderEx `82704990` and XGOffsetResourceAddress `82704D40`, called from
`82CEF360`/`82CEF36C` in the library.

## Color camera settings (camera 0)

| Address | Use | Binding |
|---|---|---|
| `82CEF788` | begins a batch of changes (fails unless the library was initialized) | 0 |
| `82CF0480` | ends it: applies the settings when the count of open batches returns to 0 | 0 |
| `82CF0AD0` | set an integer setting `(camera, setting, value)` (ks `829C90F0` → `82CF0628`) | kept |
| `82CF0AE0` | set a float setting `(camera, setting, f1)` (ks `829C9100` → `82CF08E8`) | 0 |
| `82CF0AC0` | `(three words*)` → `82CF0528(0, ...)` (ks `829C8A38`) | 0 |
| `82CF00B0` | get a setting `(0, setting, value*)` for 0, 5, 6, 7, 8 (ks `829C8668`) | read back |

## Identity and system UI

| Address | Use | Binding |
|---|---|---|
| `82CEB460` | enrollment info `(index, info*)` (ka `82D2D948`) | nobody enrolled |
| `82CEB358` | identify a player `(tracking ID, flags, callback 827E4908, context)` (ks `829DA858`) | accepted, answered with the next skeleton frame: done, S_OK, -2 |
| `82CEB1D8` | a request with a callback (`827E4AE8`) (ks `829DA6D8`) | fail |
| `82CEB670` | `li r3,0; b 82CEB540` (ks `829DAB70`) | 0 |
| `82CEB1D0` | `li r4,0; b 82CF2098` (ka `82D2F118`): works on the library's tables, empty | original code |
| `82CE47E8` | the Kinect troubleshooter: `XamShowNuiTroubleshooterUI(255, 0, 0)` | 0 |

`82CEBC58` (stores a callback in the library's globals; called from another library at
`82D17B00`-`82D17DB8`) stays as it is. The library's camera tilt code (`82CF7958`-`82CF9998`, which
reaches `XamNuiCameraElevation*` and `XamNuiCameraTiltGetStatus`) is only reached from the
library's own threads, which the virtual sensor doesn't start.
