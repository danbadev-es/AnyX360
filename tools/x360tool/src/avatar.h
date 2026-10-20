// Xbox Avatar asset data as the console keeps it: the avatar asset pack (AvatarAssetPack.toc, which
// discs of avatar games ship for consoles without it) and the STRB files it holds.
//
// AvatarAssetPack.toc (big-endian): a 0x28-byte header (+0x1C: entry count), the entries (0xF8 bytes
// each: +0x00 two words describing the asset, +0xBC 13 offsets of its name in each language (UTF-16
// big-endian strings stored after the entries), +0xF0 offset and size of its STRB data, 0 when it has
// none), then the strings, then the STRB files back to back.
//
// STRB: "STRB", a byte "alignment stored", a byte "little endian", a format GUID, the block ID size,
// the block span size, two unused bytes, the alignment (if stored); then blocks from offset 0x1A or
// 0x1E rounded up to the alignment, each {ID, data length, field size} (padded to the alignment)
// followed by its data. Block IDs: 1 animation, 2 texture, 3 model, 4 shape overrides, 5 skeleton,
// 6 and 8 metadata, 7 custom color table. The pack's model data is compressed (see
// avatar_unpack).
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace x360tool {

enum class StrbBlockId : uint32_t {
  kAnimation = 1,
  kTexture = 2,
  kModel = 3,
  kShapeOverrides = 4,
  kSkeleton = 5,
  kMetadata = 6,
  kCustomColorTable = 7,
  kMetadataVersioned = 8,
};

struct StrbBlock {
  uint32_t id = 0;
  uint32_t length = 0;      // bytes of data
  uint32_t field_size = 0;  // the length is a multiple of it
  size_t offset = 0;        // of the data, in the file
};

struct Strb {
  bool little_endian = false;
  uint8_t format_guid[16] = {};
  uint32_t alignment = 1;
  std::vector<StrbBlock> blocks;
};

// Parses the header and block list; false with `error` if the file isn't a valid STRB.
bool parse_strb(std::span<const uint8_t> data, Strb& out, std::string& error);

// Asset metadata (block 8, version 2, or block 6): gender (1 male, 2 female, 3 both), binary asset
// type (1 component, 2 texture, 3 shape override, 4 animation, 5 shape override post), details,
// category (e.g. 0xC9 tee), skeleton version (1 NXE, 2 Natal, 3 both). Little-endian fields.
struct AvatarAssetMetadata {
  uint32_t version = 0, gender = 0, type = 0, details = 0, category = 0, skeleton_version = 0;
};
bool parse_avatar_metadata(std::span<const uint8_t> block, uint32_t block_id, AvatarAssetMetadata& out);

// Compressed data (the pack's model blocks): blocks of up to 32 KiB of output, each preceded by a
// little-endian {compressed size, output offset, output size} and each an LZX stream of its own; the
// same container the avatar library embeds its own data in. Tries the LZX window sizes from 32 KiB
// up (the pack uses 32 KiB); false if none decodes.
bool avatar_unpack(std::span<const uint8_t> data, std::vector<uint8_t>& out, uint32_t& window_size,
                   std::string& error);
// Whether data looks like that container (the chain of block headers covers it exactly).
bool is_avatar_packed(std::span<const uint8_t> data);

struct AvatarPackEntry {
  uint32_t index = 0;
  uint32_t word0 = 0, word1 = 0;  // +0x00, +0x04: what the asset is (not decoded yet)
  std::u16string name;            // English
  uint32_t offset = 0, size = 0;  // its STRB file in the pack
};

class AvatarPack {
 public:
  explicit AvatarPack(std::vector<uint8_t> toc);
  const std::vector<AvatarPackEntry>& entries() const { return entries_; }
  std::span<const uint8_t> data(const AvatarPackEntry& e) const;
  std::span<const uint8_t> raw_entry(uint32_t index) const;  // the 0xF8 bytes

 private:
  std::vector<uint8_t> toc_;
  std::vector<AvatarPackEntry> entries_;
};

// Where discs of avatar games keep the pack: a package (PIRS) holding AvatarAssetPack.toc.
inline constexpr const char* kAvatarPackPackage = "AvatarAssetPack";
inline constexpr const char* kAvatarPackToc = "AvatarAssetPack.toc";

// Copies the table out of a game's AvatarAssetPack package to `toc` (written whole or not at all);
// false with `error` if the package has none or can't be read.
bool extract_avatar_pack(const std::filesystem::path& package, const std::filesystem::path& toc, std::string& error);

}  // namespace x360tool
