# Dance Central 2: the NUI API at its call sites

The Kinect library linked into the executable (NUIPRE, NUISP and NUIAUD 2.0.20764), with its entry
points found by [nui_recon.py](../../../tools/scripts/nui_recon.py) over `0x826DB000`-`0x82760000`
([nui_entry_points.md](nui_entry_points.md); the first ~70 entries of that range are the CRT and
XAPI, which come before the library). Dance Central 3 links a later build of the same library
(2.0.21173) and calls it from the same Harmonix layer, so each entry point was named after Dance
Central 3's ([games/dc3/notes/nui_api.md](../../dc3/notes/nui_api.md)): the same size, the same
kernel functions reached, the same arguments and the caller in the same place. The speech
library's thin wrappers have the same code, in the same order. Bindings:
[games/dc2/src/nui_bindings.cpp](../src/nui_bindings.cpp).

| Range | What |
|---|---|
| `0x825A8000`-`0x825B4000` | Harmonix's Kinect layer; its init `0x825AB9E8` (Dance Central 3: `823C53A0`), shutdown `0x825AB7D8` |
| `0x826E3C08`-`0x826F2400` | NUI library |
| `0x82727F40`-`0x8272BC18` | the speech library's wrappers the game calls |

## Core

| Address | Dance Central 3 | Name | As called | Binding |
|---|---|---|---|---|
| `826F2150` | `828151F8` | NuiInitialize | `(flags, hardware thread 5)` from the init | `nui::Initialize` |
| `826EFCA0` | `82812D98` | NuiShutdown | `()` from the shutdown | `nui::Shutdown` |
| `826E43E0` | `828067F0` | NuiSkeletonTrackingEnable | `(event, flags)` from the init and `822A0088` | `nui::SkeletonTrackingEnable` |
| `826E3C08` | `82806018` | NuiSkeletonTrackingDisable | `()` from `822A0088` | `nui::SkeletonTrackingDisable` |
| `826E4580` | `82806990` | NuiSkeletonGetNextFrame | `(0 ms, NUI_SKELETON_FRAME*)` | `nui::SkeletonGetNextFrame` |
| `826E3D80` | `82806190` | NuiSkeletonSetTrackedSkeletons | `(DWORD ids[2])` | `nui::SkeletonSetTrackedSkeletons` |
| `826E8E38` | `8280D328` | NuiImageStreamOpen | `(2, 2, 0, 2, 0, &h)` color and the depth stream | `nui::ImageStreamOpen` |
| `826E82A8` | `8280C6E8` | NuiImageStreamGetNextFrame | `(stream, 0 ms, const NUI_IMAGE_FRAME**)` | `nui::ImageStreamGetNextFrame` |
| `826E85D0` | `8280CA10` | NuiImageStreamReleaseFrame | `(stream, frame)` (identical code) | `nui::ImageStreamReleaseFrame` |
| `826E5358` | `82807768` | transform from gravity | pure (identical code) | original code |
| `826E59A0` | `82807DB0` | (pure) | | original code |
| `826E7C50` | `8280A350` | gesture troubleshooter | string "show_gesture_troubleshooter" next to the call | original code |

The game doesn't move the camera itself: no call reaches `XamNuiCameraElevation*` from outside the
library (Dance Central 3 calls `828080F8`/`828081E0`).

## Image formats
The library's tables have Dance Central 3's layout: per stream type at `820C5220` (16 bytes),
per format at `820C52B0` (28 bytes): color YUV 640x480 `0x1A20000C`, depth 320x240
`0x1A220058`. The texture headers are made with the title's `8296F688` (XGSetTextureHeaderEx) and
`8296F840` (XGOffsetResourceAddress), whose code is Dance Central 3's `82AC7908`/`82AC7B28`; the
library's own use is in `826E8720`.

## Camera settings (`camera` is always 0)

| Address | Dance Central 3 | Use | Binding |
|---|---|---|---|
| `826EA950` | `8280BF40` | set a setting | kept and read back |
| `826E9EC8` | `8280B258` | get it (`(0, setting, value*)`) | |
| `826EA960` | `8280BF98` | set a region of 4 floats | kept and read back |
| `826E96D8` | `8280AA68` | get it | |

## Identity (face recognition)

| Address | Dance Central 3 | Use | Binding |
|---|---|---|---|
| `826E5788` | `82807B98` | enrollment info `{+0 value, +4 enrolled}` | nobody enrolled |
| `826E54A0`, `826E5660` | `828078B0`, `82807A70` | per-player requests with a callback (`825AEA40`) | fail |

Dance Central 2 doesn't call the identity state functions Dance Central 3 does (`82815660`...), nor
the echo-cancelling audio (`82852000`...): its Kinect init creates no NUI audio.

## Speech

| Address | Dance Central 3 | Binding |
|---|---|---|
| `8272BC18` | `82868B98` start recognition (`params, 3`) | fail: the game turns voice off |
| `82727F40`, `82727F50`, `82727F60` | `828650B8`, `828650E0`, `828650F0` | 0, 0, fail |
| `82729828`-`827298F8` (7) | `82866988`-`82866A58` | 0 (`82729848` fails, as `828669A8`) |
| `8272ABF8`, `8272AC08`, `8272AC30`, `8272AC40`, `8272BB40` | `82867B80`... `82868AC0` | 0 |
