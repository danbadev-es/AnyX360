# Kinect Sports: the disc's addresses in the title update

The game is recompiled from its title update (0.0.4.2), built with XDK 2.0.20353; the disc (0.0.0.2)
was 2.0.11775. Every guest address in `src/nui_bindings.cpp` and `src/main.cpp`, how it was found
in the patched image, and how it was checked. "Shape" is the instructions with immediates,
displacements and branch targets masked; "FNK" is Fruit Ninja Kinect's base executable (NUI
2.0.20500); "callers" means the branch at the same place in the game's calling functions (matched
by shape) lands there, for every caller that matched.

| Disc | Update | What | Found by |
|---|---|---|---|
| `829CF908` | `82A1CDF8` | Initialize | shape = FNK `821C2918`; the game's call (`82F4E460` -> `82FA1B48`) |
| `829D2BE0` | `82A1ED68` | SkeletonTrackingEnable | shape = FNK `821B3720`; game calls `82F4D830`/`82F4D88C` |
| `829D23A0` | `82A1E488` | SkeletonTrackingDisable | its own shape (one match); game calls |
| `829D2D28` | `82A1EEB8` | SkeletonGetNextFrame | shape = FNK `821B3870`; game call `82F4D8C0` -> `82FA0FB0` |
| `829D24B8` | `82A1E600` | SkeletonSetTrackedSkeletons | shape = FNK `821B30F8`; caller |
| `829D2750` | `82A1E8C8` | TransformSmooth | its own shape; game call `82F4E5DC` -> `82FA1CC4` |
| `829CFE50` | `82A1E100` | depth to color (traced only) | callers (2 of 2); the same prologue and checks |
| `829D1AD8` | `82A1E268` | ImageStreamOpen | shape = FNK `821C3CC0`; caller |
| `829D21D8` | `82A1DA78` | the texture header code (a function of its own now) | shape = FNK `821C3598` |
| `829D22A4` | `82A1DC50` | return address after `bl XGSetTextureHeaderEx` | the `bl` at `82A1DC4C` |
| `829D22B0` | `82A1DC64` | return address after `bl XGOffsetResourceAddress` | the `bl` at `82A1DC60` |
| `829CFFE0` | `82A1D600` | ImageStreamGetNextFrame | shape = FNK `821C3120`; caller |
| `829D02F0` | `82A1D928` | ImageStreamReleaseFrame | its own shape, = FNK `821C3448` |
| `829C5988` | `82A13DE0` | camera elevation | callers (7 of 7) |
| `829C62A8` | `82A14780` | elevation adjustment | its own shape, = FNK `821D15D0` |
| `829C90F0` | `82A13A78` | camera setting (set) | callers (9 of 9); the same leaf |
| `829C8658` | `82A131E0` | camera setting (get) | callers (5 of 5); the same leaf |
| `829C8668` | `82A131F0` | camera setting (get) | callers (6 of 6) |
| `829C9100` | `82A13A88` | camera property | callers (14 of 14); the same leaf |
| `829C9110` | `82A13A98` | camera region | callers (2 of 2); the same leaf |
| `829C7D00` | `82A12A50` | `829C7D00` | caller; the same code |
| `829DA6D8` | `82A28D48` | identity request (with callback) | the game's call `82F4E298` -> `82FA1980` |
| `829DA858` | `82A28F08` | IdentityIdentify | the game's call `82F4E654` -> `82FA1DC4` (the same calls around it) |
| `829DAB70` | `82A29280` | identity `829DAB70` | caller |
| `829C79C8` | `82A15ED8` | the troubleshooter | the same leaf (`li r5,0; li r4,0; li r3,255; b XamShowNuiTroubleshooterUI`), the only one |
| `82A93C88` | `82AE8748` | XGSetTextureHeaderEx | shape = FNK `823D4220`; callers |
| `82A93F70` | `82AE8A30` | XGOffsetResourceAddress | shape = FNK `823D43D8`; callers |
| `82A84A00` | `82AD7AE8` | the avatar skeleton builder (XAVATAR 2.0.20353) | caller |

The library's format tables are at `820598B0` (per image type) and `82059940` (per stream), word
for word Fruit Ninja Kinect's `82001C18` and `82001CA8` in its update.
