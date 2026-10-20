// Avatar asset pack and STRB files (see avatar.h).
#include "avatar.h"

#include <cstring>

#include "lzx_frames.h"
#include "stfs.h"
#include "util.h"
#include "x360/endian.h"

namespace x360tool {

namespace {

uint32_t load_le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

uint32_t load_sized(const uint8_t* p, uint32_t size, bool little_endian) {
  uint32_t value = 0;
  for (uint32_t i = 0; i < size; ++i) value |= uint32_t(p[i]) << (8 * (little_endian ? i : size - 1 - i));
  return value;
}

uint32_t round_up(uint32_t value, uint32_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }

constexpr size_t kEntrySize = 0xF8;
constexpr size_t kEntriesOffset = 0x28;

}  // namespace

bool parse_strb(std::span<const uint8_t> data, Strb& out, std::string& error) {
  out = {};
  if (data.size() < 0x1A || std::memcmp(data.data(), "STRB", 4) != 0) {
    error = "not an STRB file";
    return false;
  }
  const bool alignment_stored = data[4] != 0;
  out.little_endian = data[5] != 0;
  std::memcpy(out.format_guid, data.data() + 6, 16);
  const uint32_t id_size = data[0x16], span_size = data[0x17];
  out.alignment = alignment_stored ? data[0x1A] : 1;
  if (id_size == 0 || id_size > 4 || span_size == 0 || span_size > 4 || out.alignment == 0 ||
      (out.alignment & (out.alignment - 1)) != 0) {
    error = "unsupported STRB header";
    return false;
  }
  const uint32_t header = round_up(id_size + 2 * span_size, out.alignment);
  uint32_t at = round_up(alignment_stored ? 0x1E : 0x1A, out.alignment);
  while (at + header <= data.size()) {
    StrbBlock block;
    block.id = load_sized(&data[at], id_size, out.little_endian);
    block.length = load_sized(&data[at + id_size], span_size, out.little_endian);
    block.field_size = load_sized(&data[at + id_size + span_size], span_size, out.little_endian);
    block.offset = at + header;
    if (block.field_size == 0 || block.length % block.field_size != 0 || block.offset + block.length > data.size()) {
      error = "bad block at " + std::to_string(at);
      return false;
    }
    out.blocks.push_back(block);
    at = round_up(uint32_t(block.offset + block.length), out.alignment);
  }
  return true;
}

bool parse_avatar_metadata(std::span<const uint8_t> block, uint32_t block_id, AvatarAssetMetadata& out) {
  out = {};
  size_t at = 0;
  if (block_id == uint32_t(StrbBlockId::kMetadataVersioned)) {
    if (block.empty()) return false;
    out.version = block[0];
    at = 1;
  } else if (block_id == uint32_t(StrbBlockId::kMetadata)) {
    out.version = 1;
  } else {
    return false;
  }
  if (block.size() < at + 13) return false;
  out.gender = block[at];
  out.type = load_le32(&block[at + 1]);
  out.details = load_le32(&block[at + 5]);
  out.category = load_le32(&block[at + 9]);
  if (out.version >= 2 && block.size() >= at + 14) out.skeleton_version = block[at + 13];
  return true;
}

bool is_avatar_packed(std::span<const uint8_t> data) {
  size_t at = 0;
  uint32_t total = 0;
  while (at + 12 <= data.size()) {
    const uint32_t packed = load_le32(&data[at]), offset = load_le32(&data[at + 4]), size = load_le32(&data[at + 8]);
    if (offset != total || size == 0 || size > 0x8000 || packed == 0 || at + 12 + packed > data.size()) return false;
    total += size;
    at += 12 + packed;
  }
  return at == data.size() && total != 0;
}

bool avatar_unpack(std::span<const uint8_t> data, std::vector<uint8_t>& out, uint32_t& window_size,
                   std::string& error) {
  if (!is_avatar_packed(data)) {
    error = "not an avatar data container";
    return false;
  }
  uint32_t total = 0;
  for (size_t at = 0; at < data.size(); at += 12 + load_le32(&data[at])) total += load_le32(&data[at + 8]);
  // Each block is a stream of its own (the avatar library resets its decoder after every block).
  for (window_size = 0x8000; window_size <= 0x200000; window_size <<= 1) {
    LzxFrameDecoder decoder(window_size);
    out.assign(total, 0);
    bool ok = decoder.ok();
    for (size_t at = 0; ok && at < data.size();) {
      const uint32_t packed = load_le32(&data[at]), offset = load_le32(&data[at + 4]), size = load_le32(&data[at + 8]);
      ok = decoder.reset() &&
           decoder.decode(data.subspan(at + 12, packed), std::span<uint8_t>(out).subspan(offset, size), error);
      at += 12 + packed;
    }
    if (ok) return true;
  }
  return false;
}

AvatarPack::AvatarPack(std::vector<uint8_t> toc) : toc_(std::move(toc)) {
  if (toc_.size() < kEntriesOffset) throw Error("AvatarAssetPack.toc: too short");
  const uint32_t count = x360::load_be<uint32_t>(&toc_[0x1C]);
  if (kEntriesOffset + size_t(count) * kEntrySize > toc_.size()) throw Error("AvatarAssetPack.toc: bad entry count");
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* e = &toc_[kEntriesOffset + i * kEntrySize];
    AvatarPackEntry entry;
    entry.index = i;
    entry.word0 = x360::load_be<uint32_t>(e);
    entry.word1 = x360::load_be<uint32_t>(e + 4);
    entry.offset = x360::load_be<uint32_t>(e + 0xF0);
    entry.size = x360::load_be<uint32_t>(e + 0xF4);
    if (size_t(entry.offset) + entry.size > toc_.size()) throw Error("AvatarAssetPack.toc: entry " + std::to_string(i) + " out of range");
    for (size_t at = x360::load_be<uint32_t>(e + 0xBC); at + 1 < toc_.size(); at += 2) {
      const char16_t c = char16_t(x360::load_be<uint16_t>(&toc_[at]));
      if (c == 0) break;
      entry.name.push_back(c);
    }
    entries_.push_back(std::move(entry));
  }
}

std::span<const uint8_t> AvatarPack::data(const AvatarPackEntry& e) const {
  return std::span<const uint8_t>(toc_).subspan(e.offset, e.size);
}

std::span<const uint8_t> AvatarPack::raw_entry(uint32_t index) const {
  return std::span<const uint8_t>(toc_).subspan(kEntriesOffset + index * kEntrySize, kEntrySize);
}

bool extract_avatar_pack(const std::filesystem::path& package, const std::filesystem::path& toc, std::string& error) {
  try {
    StfsPackage stfs(package);
    for (const auto& entry : stfs.entries()) {
      if (entry.is_dir || entry.path != kAvatarPackToc) continue;
      // Through a temporary file, so a game starting meanwhile never reads half a table.
      std::error_code ec;
      std::filesystem::create_directories(toc.parent_path(), ec);
      std::filesystem::path partial = toc;
      partial += ".partial";
      if (!stfs.extract(entry, partial)) {
        error = "extraction stopped";
        return false;
      }
      std::filesystem::rename(partial, toc, ec);
      if (ec) {
        std::filesystem::remove(partial, ec);
        error = "can't write " + toc.string();
        return false;
      }
      return true;
    }
    error = "no " + std::string(kAvatarPackToc) + " in the package";
  } catch (const std::exception& e) {
    error = e.what();
  }
  return false;
}

}  // namespace x360tool
