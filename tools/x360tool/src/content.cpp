#include "content.h"

#include <cstring>
#include <fstream>

#include "x360/endian.h"

namespace x360tool {

namespace {

// Offsets in the header (the metadata follows the 0x344-byte signed header).
constexpr size_t kHeaderSize = 0x340;
constexpr size_t kContentType = 0x344;
constexpr size_t kTitleId = 0x360;
constexpr size_t kVolumeDescriptor = 0x379;
constexpr size_t kDataFileCount = 0x39D;
constexpr size_t kVolumeType = 0x3A9;
constexpr size_t kDisplayName = 0x411;  // 128 UTF-16BE characters per language
constexpr size_t kThumbnailSize = 0x1712;
constexpr size_t kThumbnail = 0x171A;
constexpr size_t kMaxThumbnail = 0x4000;
constexpr size_t kMinimumRead = kThumbnail + kMaxThumbnail;

std::string Utf16BeToUtf8(const uint8_t* p, size_t max_chars) {
  std::string out;
  for (size_t i = 0; i < max_chars; ++i) {
    uint32_t c = uint32_t(p[i * 2]) << 8 | p[i * 2 + 1];
    if (!c) break;
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < max_chars) {  // a surrogate pair
      uint32_t low = uint32_t(p[i * 2 + 2]) << 8 | p[i * 2 + 3];
      c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
      ++i;
    }
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xF0 | (c >> 18));
      out += char(0x80 | ((c >> 12) & 0x3F));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

}  // namespace

std::optional<ContentPackage> read_content_header(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::vector<uint8_t> h(kMinimumRead);
  in.read(reinterpret_cast<char*>(h.data()), std::streamsize(h.size()));
  if (size_t(in.gcount()) < kThumbnail) return std::nullopt;
  h.resize(size_t(in.gcount()));
  ContentPackage package;
  package.magic.assign(reinterpret_cast<const char*>(h.data()), 4);
  if (package.magic != "CON " && package.magic != "LIVE" && package.magic != "PIRS") return std::nullopt;
  package.header_size = x360::load_be<uint32_t>(h.data() + kHeaderSize);
  package.content_type = x360::load_be<uint32_t>(h.data() + kContentType);
  package.title_id = x360::load_be<uint32_t>(h.data() + kTitleId);
  std::memcpy(package.volume_descriptor.data(), h.data() + kVolumeDescriptor, package.volume_descriptor.size());
  package.data_file_count = x360::load_be<uint32_t>(h.data() + kDataFileCount);
  package.volume_type = uint8_t(x360::load_be<uint32_t>(h.data() + kVolumeType));  // a 32-bit enum
  package.display_name = Utf16BeToUtf8(h.data() + kDisplayName, 128);
  uint32_t thumbnail_size = x360::load_be<uint32_t>(h.data() + kThumbnailSize);
  if (thumbnail_size && thumbnail_size <= kMaxThumbnail && kThumbnail + thumbnail_size <= h.size())
    package.thumbnail.assign(h.begin() + kThumbnail, h.begin() + kThumbnail + thumbnail_size);
  return package;
}

const char* content_type_name(uint32_t type) {
  switch (type) {
    case content_type::kSavedGame: return "saved game";
    case content_type::kMarketplace: return "downloadable content";
    case content_type::kGamesOnDemand: return "Games on Demand";
    case content_type::kGameDemo: return "game demo";
    case content_type::kInstaller: return "title update";
    default: return "content";
  }
}

}  // namespace x360tool
