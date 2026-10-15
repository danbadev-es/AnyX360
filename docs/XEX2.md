# XEX2 format notes

These are the notes `tools/x360tool/src/xex.cpp` is built from. All multi-byte fields are
**big-endian**. Sources: Xenia (`src/xenia/kernel/util/xex2_info.h`, `cpu/xex_module.cc`) and the Free60
wiki. Items marked *(unconfirmed)* still need checking against real files.

## File header (offset 0)

| Off | Size | Field |
|---|---|---|
| 0x00 | 4 | magic `XEX2` |
| 0x04 | 4 | module flags (1 = title, 2 = exports to title, 8 = DLL, …) |
| 0x08 | 4 | offset of the PE data (the payload) |
| 0x0C | 4 | reserved |
| 0x10 | 4 | offset of the security info |
| 0x14 | 4 | optional header count |
| 0x18 | 8·n | optional headers: `{u32 key, u32 value}` |

The low byte of an optional header key gives the meaning of `value`:
- `0x00` or `0x01`: `value` is the data itself
- `0xFF`: `value` is an offset; the first dword of the record is its size
- anything else: `value` is an offset to a record of `(key & 0xFF) * 4` bytes

## Security info (at the security offset)

| Off | Size | Field |
|---|---|---|
| 0x000 | 4 | header size |
| 0x004 | 4 | image size (uncompressed PE) |
| 0x008 | 256 | RSA signature |
| 0x108 | 4 | length |
| 0x10C | 4 | image flags (`0x10000000` = 4 KiB pages; otherwise 64 KiB) |
| 0x110 | 4 | load address |
| 0x114 | 20 | section digest |
| 0x128 | 4 | import table count |
| 0x12C | 20 | import table digest |
| 0x140 | 16 | XGD2 media ID |
| 0x150 | 16 | **AES key** (encrypted session key) |
| 0x160 | 4 | export table address |
| 0x164 | 20 | header digest |
| 0x178 | 4 | game regions |
| 0x17C | 4 | allowed media types |
| 0x180 | 4 | page descriptor count |
| 0x184 | 24·n | page descriptors: `u32 (page_count << 4 \| info)` + SHA-1 |

## Encryption
`session_key = AES-128-ECB-decrypt(console_key, security.aes_key)`. The payload (from the PE data
offset to the end of the file) is then one continuous AES-128-CBC stream with a zero IV under the
session key. `console_key` is the retail key or all zeros (devkit). AnyX360 never ships the retail key;
see [keys/README.md](../keys/README.md).

## Compression (`FILE_FORMAT_INFO`, key `0x000003FF`)
`{u32 info_size, u16 encryption_type, u16 compression_type}` followed by:

- **0 None**: the decrypted payload is the image.
- **1 Basic**: `{u32 data_size, u32 zero_size}[]`. Copy `data_size` bytes, then write `zero_size` zeros.
- **2 Normal (LZX)**: `{u32 window_size, u32 first_block_size, u8 first_block_sha1[20]}`. The payload is a
  chain of blocks. Each block starts with `{u32 next_block_size, u8 next_block_sha1[20]}`, followed by
  chunks `{u16 length, data}` and ends with a zero-length chunk. The concatenated chunk data is a
  single LZX stream (window = `window_size`, no reset interval), decoded with libmspack's `lzxd`.
- **3 Delta**: title-update patches (`.xexp`). These apply on top of the base executable and are not
  usable on their own. XenonRecomp can apply them.

## Imports (`IMPORT_LIBRARIES`, key `0x000103FF`)
```
u32 size
u32 string_table_size
u32 string_count
char strings[string_table_size]   // NUL-terminated names, padded to 4 bytes
library[]                          // until `size`
  u32 size; u8 next_digest[20]; u32 id; u32 version; u32 version_min;
  u16 name_index; u16 record_count; u32 record_address[record_count]
```
Each record address points into the image at a big-endian dword: `type << 24 | lib_index << 16 |
ordinal`. Type 0 is a variable import; type 1 is a function thunk. A function normally has both records.

## Versions
`u32`: `major:4 | minor:4 | build:16 | qfe:8` (most to least significant).

## Static libraries (`STATIC_LIBRARIES`, key `0x000200FF`)
`u32 size`, then 16-byte entries: `char name[8]; u16 major, minor, build; u16 qfe/approval` *(packing
of the last field unconfirmed)*. This list tells us the XDK build the game was linked against (D3D,
XAPI, XAudio2, XMP…), which matters for identifying statically linked library code.

## Execution info (key `0x00040006`, 24 bytes)
`u32 media_id; u32 version; u32 base_version; u32 title_id; u8 platform, executable_type,
disc_number, disc_count; u32 savegame_id`.
