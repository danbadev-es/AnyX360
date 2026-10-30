# Dance Central 3: the disc's addresses in the title update

The game is recompiled from its title update (0.0.0.5 to 0.0.1.5). Every guest address given by
hand in `src/` and `config.toml`, how it was found in the patched image and how it was checked.
"Shape" is the instructions with immediates, displacements and branch targets masked. The other
notes ([nui_api.md](nui_api.md), [nui_entry_points.md](nui_entry_points.md), [sections.txt](sections.txt),
[imports.txt](imports.txt)) give the disc's addresses.

The update moved most of the code: Milo's start-up by `+0xA38`, the NUI library by `+0x15B0` (its
audio part by `+0x15B8`, its speech part by `+0x1650` to `+0x1B60`), the title's D3D library (XG)
by `+0x1500`. The image keeps its size (16.12 MiB); `.rdata` grew by 0xAD8 bytes and `.text` by
0x2090.

## How they were found
- **`.pdata` functions**: the function's whole shape, searched over the patched image's `.pdata`
  functions. Each had exactly one match; the address keeps its offset into the function.
- **Leaf functions** (no `.pdata` entry): the shape from the address to its first `blr` (or 64
  instructions), searched over the whole `.text`. Each had exactly one match, except
  SetUnhandledExceptionFilter (two: the one App::Run calls, at the expected delta, was taken).
- **Data**: from the instructions that load them, in the matched functions.

| Disc | Update | What | Check |
|---|---|---|---|
| `82311A68` | `823124A0` | App::Run (`milo_hooks.cpp`); return addresses `82311A8C`/`82311A98` -> `823124C4`/`823124D0` | `.pdata` shape, 13/17 identical; read by hand |
| `82311A40` | `82312478` | Milo's exception filter | `.pdata` shape, 6/9 identical |
| `82311338` | `82311D70` | the main loop (comment) | `.pdata` shape, the filter's call |
| `827FBC80` | `827FD230` | SetUnhandledExceptionFilter | the call in App::Run |
| `82D5C784`, `82D5C794` | `82D5C844`, `82D5C854` | `kApp`, `kPreviousFilter` | App::Run's and the filter's loads and stores |
| `82AC7908`, `82AC7B28` | `82AC8E08`, `82AC9028` | XGSetTextureHeaderEx, XGOffsetResourceAddress | `.pdata` shape, 27/29 and 52/53 identical |
| `820F0080`, `820F0110` | `820F0600`, `820F0690` | the NUI library's stream type and format tables | the loads in `8280E110` and `8280E8D8`; the same values, the string pointers moved by `+0x580` |
| `8280CB60` (`8280CD0C`, `8280CD1C`) | `8280E110` (`8280E2BC`, `8280E2CC`) | the library's own texture header code (return addresses `TextureHeader` gives) | `.pdata` shape, 159/165 identical |
| `828151F8`, `82812D98` | `828167A8`, `82814348` | Initialize, Shutdown | `.pdata` shape |
| `828067F0`, `82806018`, `82806990`, `82806190` | `82807DA0`, `828075C8`, `82807F40`, `82807740` | skeleton tracking enable, disable, next frame, tracked skeletons | `.pdata` shape |
| `82807768`, `82807DB0`, `8280A350` | `82808D18`, `82809360`, `8280B900` | traced pure functions | `.pdata` shape; leaf windows of 21 and 61 instructions |
| `8280D328`, `8280C6E8`, `8280CA10` | `8280E8D8`, `8280DC98`, `8280DFC0` | image stream open, next frame, release | `.pdata` shape |
| `828080F8`, `828081E0`, `82808B40` | `828096A8`, `82809790`, `8280A0F0` | camera elevation get, set, adjustment | `.pdata` shape |
| `8280BF40`, `8280B258`, `8280BF98`, `8280AA68` | `8280D4F0`, `8280C808`, `8280D548`, `8280C018` | camera settings set, get, region set, get | `.pdata` shape; leaf windows of 21 and 28 instructions |
| `82807B98`, `828078B0`, `82807A70` | `82809148`, `82808E60`, `82809020` | identity enrollment, requests | `.pdata` shape |
| `82815660`, `82815750`, `828157F0`, `82815870` | `82816C10`, `82816D00`, `82816DA0`, `82816E20` | identity | `.pdata` shape |
| `82852000`, `82851900`, `82851978`, `82851418` | `828535B8`, `82852EB8`, `82852F30`, `828529D0` | audio create, callback, 82851978, destroy | `.pdata` shape; create: a leaf window of 42 instructions |
| `828650B8`-`828650F0` (4) | `82866708`-`82866740` | speech | leaf windows (`+0x1650`) |
| `82866988`-`82866A58` (7) | `82868290`-`82868360` | speech | `82866A10`: `.pdata` shape; the others leaf windows (`+0x1908`) |
| `82867B80`-`82868B98` (6) | `828696E0`-`8286A6F8` | speech (start: `82868B98` -> `8286A6F8`) | leaf windows (`+0x1B60`) |

The register helpers are find_helpers.py's on the patched image (each pattern occurs once).
setjmp/longjmp (`82C64770`/`82C64340`) are configured now: find_helpers.py's candidates, read by
hand (the disc build went without them; on the disc they are `82C62800`/`82C623D0`).

## The song keys
The retail executable (the Games on Demand copy's, and the update's) decrypts its songs' keys with
`XeKeysSetKey` and `XeKeysAesCbc` (`827FFA30`/`827FFAC8` on the disc, `82800FE0`/`82801078` in the
update): key slot `0xE0` + n, one 16-byte block, no feed. The re-packed disc dump the port was first
made from had both imports replaced with `XeCryptAesKey`/`XeCryptAesCbc` and those two wrappers and
their caller (`82623180`) patched to pass an AES state on the stack; apart from that, its code was
the same as the Games on Demand copy's base executable (both recompiled with the same configuration:
the output differs only in those three functions and the two imports).
