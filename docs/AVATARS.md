# Xbox Avatars: working notes

Avatar games (Doritos Crash Course, Kinect Sports, Kinect Joy Ride, ...) draw their players as Xbox
Avatars. On the console the system (xam) supplies an avatar's description, its parts (models,
textures) and animations; the avatar library linked into each game (XAVATAR) turns them into
something to draw. AnyX360 has no system software, so it supplies them itself
(`runtime/src/hle/avatar_system.cpp`) from the console's avatar asset pack, which discs of avatar
games carry. These notes collect what is known.

## What a title asks the system for

| Call | What it returns |
|---|---|
| `XamAvatarInitialize(4, coordinate system, ...)` | Starts the system. The coordinate system is the **second** argument (1 right-handed; the library mirrors z otherwise). Kinect Sports can't run without it (`GameDescriptor::avatar_system`). |
| `XamAvatarGetManifestLocalUser`, `XamAvatarGetMetadataRandom`, `XamAvatarGetMetadataSignedOutProfile` | An avatar's description (`XAVATAR_METADATA`, 1000 bytes, below). |
| `XamAvatarGetAssetsResultSize(components)` | The sizes of a CPU result buffer and a GPU resource buffer for those components. |
| `XamAvatarGetAssets(metadata, components, flags, result, gpu, overlapped)` | Fills both buffers (layout below). The library's `XAvatarGetAssets` (Kinect Sports `82A821B0`) passes the title's buffers straight through. |
| `XamAvatarLoadAnimation` | One of the system's animations (not implemented yet). |
| `XamAvatarSetCustomAsset` | A title's own part (Kinect Sports gives six). |

## The data: the avatar asset pack

Discs of avatar games carry `AvatarAssetPack`, a PIRS package (title `FFFE07DF`) holding
`AvatarAssetPack.toc` (13 MB on the Kinect Sports disc). The launcher extracts it to the shared
`%LOCALAPPDATA%\AnyX360\system` folder when it adds a game that has one, and a game that has one
copies it there on its first run, so avatar games without a pack (Doritos Crash Course) find it.
`x360tool avatar-pack` lists it and unpacks every asset; `x360tool avatar-model` decodes a part
(meshes, material parameters, textures) and writes it as OBJ.

- **Table** (big-endian): a 0x28-byte header (+0x1C entry count: 1282), entries of 0xF8 bytes
  (+0x00 the asset's category bits, +0x04 a second word, +0x98 a word (an ordering?), +0x9C the
  asset ID of a linked asset, +0xBC 13 offsets of its name per language, +0xF0 offset and size of
  its STRB file, 0 for 203 entries without data), the names (UTF-16 big-endian), then the STRB
  files. The entry's index is the asset's number in its ID. 40 entries also keep colours at
  +0x08. **Links** (+0x9C): 266 components link to their hiding or body modification template
  (e.g. "Pinstripe Pants" to "Pinstripe Pants Hiding Template", "Male Power Tee" to the generic
  "Male Short Sleeved Top Template", "Cork High Heels" to "High Heel Body Modification Template");
  90 hairstyles link to their version for under a hat ("Peak" to "Peak (Hat)"); eye shadows to
  another eye shadow. `x360tool avatar-shape <toc> all` checks every link.
- **Category bits** (first word): components 0-12 (head 1, body 2, hair 4, shirt 8, trousers 0x10,
  shoes 0x20, hat 0x40, gloves 0x80, glasses 0x100, wristwear 0x200, earrings 0x400, ring 0x800,
  carryable 0x1000; a part may cover several, e.g. a wrap shirt 0x208); face textures (eyes 0x2000,
  eyebrows 0x4000, mouth 0x8000, facial hair 0x10000, face paint 0x20000, eye shadow 0x40000);
  0x400000 animations; shape overrides (nose 0x80000, chin 0x100000, ears 0x200000, 0x1000000
  hiding templates). For hairstyles the second byte of the second word groups them (1 short styles,
  2 long ones, 0/3 either), although their metadata says "both genders".
- **STRB** (as in Velocity's avatar asset viewer): "STRB", alignment and endianness bytes, a format
  GUID (`59bde08b-4909-4ee4-b991-ada87b7c0b6b`); blocks from offset 32, each `{ID, length, field
  size}`. IDs: 1 animation, 2 texture, 3 model, 4 shape overrides, 5 skeleton, 6/8 metadata (gender
  1/2/3, type 1 component, 2 texture, 3 shape override, 4 animation), 7 colour table (a little-endian
  count, then per colourway three `{BGRA, word}`).
- **Compression**: model and texture blocks are a chain of `{u32 LE packed size, output offset,
  output size}` + LZX data (32 KiB window), each block a stream of its own. Animations and shape
  overrides are stored as they are.

## Models

Twelve u32 (CPU size = h9 + 512 x meshes, GPU size, texture/vertex/index region sizes, mesh and
texture counts, vertex and index region offsets, mesh and texture record offsets, tiling scratch),
then bit-packed meshes (a 33-byte header, 24-byte parameters, a vertex stream on a lattice, an index
stream), textures (33-byte header, data; "blank" textures are filled by the system) and weld pairs.
See `tools/x360tool/src/avatar_model.h`. The library's own "loading" avatar (Kinect Sports
`820BCB20`) is a model in the same format.

**Parameters** (per mesh, 24 bytes): kind 1 a texture (index, sampler, address flags); kind 3 a
pixel shader constant; kind 2 a vertex shader constant. Kinect Sports maps ids to constants at
`8259D2F0`: 27 -> c201, 22-24 -> c210-c212, 26/25 -> c213/c214, 13-21 -> c215-c223. Its shaders:
- body and clothes: colour = lerp(texture 0, c210 x mask.r + c211 x mask.g + c212 x mask.b,
  mask.a) with mask = texture 1, then texture 2 laid over by its alpha. The body's and the hair's
  masks are all red: 22 is the skin or hair colour; clothes take their colour table's.
- head: the skin (13) under six layers, each the texture's red channel times its colour (green is
  white, blue the skin): face paint (20/21), facial hair (19), mouth (15), eyes (16), eye shadow
  (18), eyebrows (17). Head textures: 0 face paint 128x128, 1 facial hair, 2 eyebrows (5 frames),
  3 eyes (14 frames), 4 mouth (14 frames), 5 eye shadow; the title picks the frame.
- 27 is a rim light colour with its exponent in w; black in every asset, left as it is.

**Textures** (`x360tool avatar-model <toc> <entry>` prints each one's layout): DXT1 or DXT5,
8-in-16 endian; linear ones (most) have rows 32 blocks apart (Xenia's pitch for a texture's base
level), tiled ones (format bit 0x100) use the GPU's 2D tiling. The title makes a D3D texture header
per texture and frame from the 44-byte texture records (Kinect Sports' base executable 0.0.0.2,
`82524770`:
`XGSetTextureHeaderEx(width, height, 1 level, format)`, then each frame at address + frame x slice
size); the per-mesh code (`8259E8C0`) picks the frame for ids 7-10 and 12 (eyebrows, eyes, mouth,
each side) from the avatar's expression, and sets wrap or clamp from the texture parameter's flags.
So the **width and height in the record are what the GPU samples**: the head's blank face slots say
64x64 (eyebrows, eyes, mouth), but the pack's face textures are 128x64, a whole eye or mouth per
frame with the same slice size. The system writes the texture's own header in the slot; with the
slot's, only the left half of each eye, eyebrow and mouth showed (half-closed, shifted eyes).

## Descriptions (`XAVATAR_METADATA`)

As Kinect Sports passes them for its guests (big-endian):

| Offset | Contents |
|---|---|
| +4, +8 | height and weight, -1 to 1 (the library reads them for the skeleton) |
| +12, +28, +44 | chin, nose, ears: asset IDs |
| +60 | six face textures, 32 bytes each (an asset ID, 16 bytes: 1.0 or 0): mouth, eyes, eyebrows, facial hair, then eye shadow and face paint |
| +252 | nine ARGB colours: skin, hair, lips, eyes, eyebrows, eye shadow, facial hair, two face paint (= head constants 13-21) |
| +288 | fifteen parts, 32 bytes each: an asset ID, the component bits (16 bits), 0, 12 bytes (zero in the title's; this system keeps a part's three colours there) |
| +768 | four more part records (shoes, trousers, shirt, hair) |
| +896 | 16 bytes that look like a hash |

An **asset ID** is 16 bytes: the category bits, the asset number (the pack entry), its gender
(1 male, 2 female, 3 both) and `C1C8F109A19CB2E0` (every stock asset).

## The result of XamAvatarGetAssets

Absolute pointers: `+0` the skeleton (16-byte aligned: count, joints*, then 72 joints of 96 bytes:
parent, first child, sibling, model-space and local translation/rotation, scale; filled by the
title's own skeleton builder, Kinect Sports `82A84A00(skeleton, body type, height, weight)` (Kinect Joy
Ride's same library build: `82CC8C30`);
Doritos Crash Course's older library (2.0.11626) only has the default skeleton, `825F7EE0(skeleton)`), `+8`
part count, `+12` 32-byte asset records (ID, 3 at +16, a colour at +20), `+16` 52-byte part
descriptors (header words 0-6, CPU block, GPU block, vertex and index regions, mesh and texture
records). Kinect Sports wants the head first and the body second, and stops drawing an avatar at
the first part without indices (`8259E750`), so parts with nothing to draw ("Bald") stay out.

How Kinect Sports draws them (`8259D418`): passes by material (0 body/clothing, 1 hair, 2 glasses,
3 shades, 4 head), each walking all parts' meshes of that material (`8259E2A0`), constants and
textures per mesh (`8259E8C0`). It never writes depth, cull or stencil state.

## Shape overrides: hiding templates and face shapes

The body is a whole body, and clothes are drawn over it: without help, the body's skin pokes
through tight clothes (light patches on trousers) and z-fights with them. On the console the system
removes the body's triangles under each worn component and reshapes the body where it needs to
(high heels raise the feet); it shapes the head with the chin, nose and ears the same way. AnyX360
does the same when it builds the parts (`ApplyShapes`):

- the **body** takes the template each worn component links to (the table's +0x9C, above), if it
  was made for that body (male or female);
- the **head** takes the description's chin, nose and ears (+12, +28, +44);
- under a **hat**, a hairstyle is replaced by its linked version for hats;
- the default avatars keep the head's own shape (the pack's first ears are "Fantasy Ears").

The format (block 4, little-endian; `x360tool/src/avatar_model.h`): two sections, each a 32-byte
header {count, the target's index (vertex) region size, the target's asset ID, 0, a bit width}:

1. `count` triangle numbers of that width (sorted), counted through the target's index region:
   triangles to hide (made degenerate);
2. a lattice vertex stream as in models (the 402-bit header without the count: scale, minimum, the
   three lattice widths, four fields as base + width), each vertex starting with its byte offset in
   the target's vertex region (that width), then its position and the four fields (normal, blend
   weights, blend indices, colour): vertices to replace; their UVs stay.

The region sizes say which model a template fits (MaleBody 13584/45568 bytes, FemaleBody
14724/49280, Head 5076/33792). `x360tool avatar-shape <toc> <entry>` applies one to its target and
`all` checks the 266 in the pack (230 templates, 36 face shapes): all apply.

**Testing**: `ANYX360_AVATAR` dresses the players' avatars (not the random ones titles ask for):
a comma-separated list of pack entries (`x360tool avatar-pack`'s `index.tsv`), each taking its
slots' place (a body sets the gender; a face texture or face shape its place in the face), or
`random:<seed>`. `ANYX360_AVATAR_SHAPES=0` builds the parts without shape overrides, to compare.

## Open

- **Animations** (`XamAvatarLoadAnimation`): 41 in the pack.
- Unmapped avatar buffers: in Kinect Sports' menus Xenia refuses a few draws of avatar parts (index
  buffers the size of a head's or a body's) because their memory is no longer mapped ("Invalid
  upload range", about three frames each, as in 0.16.1); probably avatars drawn once more after
  the title freed them. Not visible in the screenshots taken; not investigated further.
- The 16 bytes after each face texture, the second part list and the trailing bytes of a
  description.
