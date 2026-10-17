# Dance Central 3: the NUI API at its call sites

The Kinect library linked into the executable, named from the evidence in
[nui_entry_points.md](nui_entry_points.md) (Harmonix's profiling scopes around the calls,
arguments, use of the results, imports reached) and the callers' code. Signatures are written from
that evidence and the public Kinect for Windows SDK 1.x documentation, which grew out of this API.
Bindings: [games/dc3/src/nui_bindings.cpp](../src/nui_bindings.cpp).

## Core

| Address | Name | Signature as called | Evidence | Binding |
|---|---|---|---|---|
| `828151F8` | NuiInitialize | `(flags 0x4049, hardware thread -1)` | scope "NuiInitialize" in 823C53A0 | `nui::Initialize` |
| `82812D98` | NuiShutdown | `()` | called from the shutdown 823C5A18 | `nui::Shutdown` |
| `828067F0` | NuiSkeletonTrackingEnable | `(event, flags)`: flags 2 = the title chooses whom to track | called before the streams; 82310C80 pairs it with 82806018 | `nui::SkeletonTrackingEnable` |
| `82806018` | NuiSkeletonTrackingDisable | `()` | as above | `nui::SkeletonTrackingDisable` |
| `82806990` | NuiSkeletonGetNextFrame | `(0 ms, NUI_SKELETON_FRAME*)` | copies 0xAB0 bytes to the caller; 823C2790 compares the 64-bit timestamps | `nui::SkeletonGetNextFrame` |
| `82806190` | NuiSkeletonSetTrackedSkeletons | `(DWORD ids[2])` | 823C35C8 passes the two IDs it picked (-1: none) | `nui::SkeletonSetTrackedSkeletons` |
| `8280D328` | NuiImageStreamOpen | `(type, resolution, flags, frame limit, event, HANDLE*)` | scopes "NuiImageStreamOpen:color/depth": `(2, 2, 0, 2, 0, &h)` and `(4, 1, 0, 2, 0, &h)` | `nui::ImageStreamOpen` |
| `8280C6E8` | NuiImageStreamGetNextFrame | `(stream, 0 ms, const NUI_IMAGE_FRAME**)` | 823C32E0; on failure compares with `0x8007048F` | `nui::ImageStreamGetNextFrame` |
| `8280CA10` | NuiImageStreamReleaseFrame | `(stream, frame)` | 823C32E0, the slot before | `nui::ImageStreamReleaseFrame` |
| `828080F8` | NuiCameraElevationGetAngle | `(LONG* angle, ...)` | reaches XamNuiCameraElevationGetAngle | `nui::CameraElevationGetAngle` |
| `828081E0` | NuiCameraElevationSetAngle | `(LONG angle)`: ±27 | reaches XamNuiCameraElevationSetAngle | `nui::CameraElevationSetAngle` |
| `82807768` | transform from gravity | vector in v1 | pure (no imports); called with the frame's gravity vector | original code |

## Structures (big-endian, confirmed in the callers)

- `NUI_SKELETON_FRAME`, 0xAB0 bytes: `+0x00` timestamp in ms (the game scales the delta by 0.001),
  `+0x08` frame number, `+0x0C` flags, `+0x10` floor clip plane, `+0x20` normal to gravity, `+0x30`
  six `NUI_SKELETON_DATA`.
- `NUI_SKELETON_DATA`, 0x1C0 bytes (the stride 448 is all over the library): `+0x00` tracking state
  (2 tracked, 1 position only), `+0x04` tracking ID, `+0x08` enrollment index, `+0x0C` user index,
  `+0x10` position, `+0x20` 20 joints as 16-byte vectors, `+0x160` 20 joint states, `+0x1B0` quality
  flags (823C6FB8 reads all of them).
- `NUI_IMAGE_FRAME` (inside the library's 120-byte frame record, at +8): `+0x0C` image type, `+0x10`
  resolution, `+0x14` `D3DTexture*` (823C3288 returns it; 823C34A0 locks it read-only with the D3D
  library's LockRect at `829DFC50`, 823C3B20 also binds it for rendering).
- Image formats, from the library's tables (`820F0080`: per type, `820F0110`: per format):
  type 2 (color YUV) 640×480, 2 bytes/pixel, D3DFORMAT `0x1A20000C`; type 4 (depth) 320×240, 2
  bytes/pixel, D3DFORMAT `0x1A220058`. The library builds the texture headers with the D3D library's
  `82AC7908` (XGSetTextureHeaderEx: width, height, 1 level, usage 4, format, 0, flags 1, base 0, mip
  -1, pitch 0, header, NULL, NULL) and `82AC7B28` (XGOffsetResourceAddress: header, pixels).

## Camera settings (`camera` is always 0)

| Address | Use | Binding |
|---|---|---|
| `8280BF40` | set setting 0 or 2 to a value | kept and read back |
| `8280B258` | get it (`(0, setting, value*)`) | |
| `8280BF98` | set a region of 4 floats | kept and read back |
| `8280AA68` | get it | |
| `82808B40` | elevation adjustment with floats and two outputs | returns 0 |

## Identity (face recognition)

| Address | Use | Binding |
|---|---|---|
| `82807B98` | enrollment info for indices 0-7: `{+0 value, +4 enrolled}` | nobody enrolled |
| `828078B0`, `82807A70` | per-player requests with a callback (`823C6C28`) | fail |
| `82815660`, `82815750`, `828157F0`, `82815870` | identity state | fail / succeed / 0 |

## Audio and speech

| Address | Use | Binding |
|---|---|---|
| `82852000` | create the echo-cancelling audio (`5, table, 1, out, 0`); a failure skips it | fail |
| `82851900` | register its callback | 0 |
| `82851978`, `82851418` | shutdown | 0 |
| `82868B98` | start speech recognition (`params, 3`); non-zero turns voice off (823C9B80) | fail |
| `82865000`-`82869000` | the other speech entry points (thin wrappers) | 0 or fail |
