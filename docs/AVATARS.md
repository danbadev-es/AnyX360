# Xbox Avatars: working notes

Avatar games (Doritos Crash Course, Kinect Sports, Kinect Joy Ride, ...) draw their players as Xbox
Avatars. On the console the system (xam) supplies an avatar's description, its parts (models,
textures) and animations; the avatar library linked into each game (XAVATAR) turns them into
something to draw. AnyX360 has no system software, so it has to supply them itself. These notes
collect what is known; the implementation follows them.

## What a title asks the system for

| Call | What it returns |
|---|---|
| `XamAvatarInitialize` | Starts the system. Kinect Sports can't run without it (`GameDescriptor::avatar_system`). |
| `XamAvatarGetManifestLocalUser`, `XamAvatarGetMetadataRandom`, `XamAvatarGetMetadataSignedOutProfile` | An avatar's description (`XAVATAR_METADATA`, 1000 bytes in XNA's `AvatarDescription`): body type, height, weight, colours and the parts it wears. |
| `XamAvatarGetAssetsResultSize(components)` | The sizes of a CPU result buffer and a GPU resource buffer for those components. |
| `XamAvatarGetAssets(metadata, components, flags, result, gpu, overlapped)` | Fills both buffers. The library's `XAvatarGetAssets` (Kinect Sports `82A821B0`) passes the title's buffers straight through, so their layout is the system's. |
| `XamAvatarLoadAnimation` | One of the system's animations. |
| `XamAvatarSetCustomAsset` | A title's own part (Kinect Sports gives six). |

Today all of them fail or return nothing: titles that tolerate it run without avatars (Kinect
Adventures); Doritos Crash Course has an invisible player; Kinect Sports reads the skeleton from the
empty result and crashes after the player is picked.

## The data: the avatar asset pack

Discs of avatar games carry the console's asset pack: `AvatarAssetPack`, a PIRS package (title
`FFFE07DF`) holding `AvatarAssetPack.toc` (13 MB on the Kinect Sports disc). `x360tool avatar-pack`
lists it and unpacks every asset.

- **Table** (big-endian): a 0x28-byte header (+0x1C entry count: 1282), entries of 0xF8 bytes
  (+0x00/+0x04 two words describing the asset, +0xBC 13 offsets of its name per language, +0xF0
  offset and size of its STRB file, 0 for 203 entries without data), the names (UTF-16 big-endian:
  "MaleBody", "FemaleBody", "Head", "Animation Generic Stand 0", ...), then the STRB files.
- **STRB** (as in Velocity's avatar asset viewer): "STRB", a byte "alignment stored", a byte "little
  endian" (0: big-endian block headers), a format GUID (`59bde08b-4909-4ee4-b991-ada87b7c0b6b`), block
  ID size 1, span size 4, alignment 4; blocks from offset 32, each `{ID, length, field size}` in 12
  bytes. IDs: 1 animation, 2 texture, 3 model, 4 shape overrides, 5 skeleton, 6/8 metadata, 7 colour
  table. Metadata (version 2, little-endian): gender (1 male, 2 female, 3 both), type (1 component,
  2 texture, 3 shape override, 4 animation), details, category (e.g. 0xC9 tee), skeleton version
  (1 NXE, 2 Natal, 3 both).
- **Contents**: 585 components (a model block, most with a colour table), 187 textures, 266 shape
  overrides, 41 animations. No skeleton block.
- **Compression**: model and texture blocks are a chain of `{u32 LE packed size, output offset,
  output size}` + LZX data (32 KiB window), each block a stream of its own (the library resets its
  decoder after every block). Animations and shape overrides are stored as they are.

## The avatar library

- It links in each title (XAVATAR 2.0.11626 in DCC, 2.0.11775 in Kinect Sports) and embeds its own
  "loading" avatar (Kinect Sports `820BCB20`, 64 KiB packed, 85 KiB unpacked): **a model in the
  same format as the pack's** (header: CPU size `0x290`, GPU size, three GPU section sizes, two
  counts, ...). `XAvatarInitialize` unpacks it through the kernel's LDI and loads it
  (`82A84D98` -> `82A8ACB0`) into a 0x290-byte CPU part and a GPU part (descriptor at `832234B0`:
  sizes, then the CPU and GPU buffers at +28/+32 and pointers into them at +36..+48).
- **Skeletons**: two 71-joint parent tables (big-endian `int16`, root -1) in both titles' read-only
  data, one per skeleton version (Kinect Sports `82029C08` and `8202F648`), each followed by other
  per-joint tables.

## What Kinect Sports reads from the result
- `+0`: pointer to the skeleton: `+0` joint count (71), `+4` joints, 96 bytes each (it reads floats
  at +80/+84/+88) (`82524E38`).
- `+8`: a count and `+16` an array of textures: fields at +12, +16 and GPU offsets at +36/+40, given
  to `XGSetTextureHeader`/`XGOffsetResourceAddress` (`82524770`).
