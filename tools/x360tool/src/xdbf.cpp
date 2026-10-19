#include "xdbf.h"

#include <cstdio>
#include <cstring>

#include "x360/endian.h"

namespace x360tool {

std::optional<std::span<const uint8_t>> xdbf_entry(std::span<const uint8_t> xdbf, uint16_t section, uint64_t id) {
  // Header: 'XDBF', be32 version, be32 entry table size, be32 entry count, be32 free table size,
  // be32 free count. Entries: {be16 section, be64 id, be32 offset (from the data), be32 size}; the
  // free table ({be32 offset, be32 size}) follows, then the data.
  if (xdbf.size() < 24 || x360::load_be<uint32_t>(xdbf.data()) != 0x58444246) return std::nullopt;
  const uint64_t entry_table = x360::load_be<uint32_t>(xdbf.data() + 8);
  const uint32_t entry_count = x360::load_be<uint32_t>(xdbf.data() + 12);
  const uint64_t free_table = x360::load_be<uint32_t>(xdbf.data() + 16);
  const uint64_t data = 24 + entry_table * 18 + free_table * 8;
  if (entry_count > entry_table || data > xdbf.size()) return std::nullopt;
  for (uint32_t i = 0; i < entry_count; ++i) {
    const uint8_t* entry = xdbf.data() + 24 + i * 18;
    if (x360::load_be<uint16_t>(entry) != section || x360::load_be<uint64_t>(entry + 2) != id) continue;
    const uint64_t offset = data + x360::load_be<uint32_t>(entry + 10);
    const uint32_t size = x360::load_be<uint32_t>(entry + 14);
    if (offset + size > xdbf.size()) return std::nullopt;
    return xdbf.subspan(size_t(offset), size);
  }
  return std::nullopt;
}

std::optional<std::span<const uint8_t>> xex_resource(const Xex& xex, std::span<const uint8_t> image,
                                                     const char* name) {
  // RESOURCE_INFO: be32 size, then {char name[8], be32 address, be32 size} per resource.
  auto info = xex.opt_offset(xex_key::kResourceInfo);
  const std::vector<uint8_t>& file = xex.data();
  if (!info || *info + 4 > file.size()) return std::nullopt;
  const uint32_t size = x360::load_be<uint32_t>(file.data() + *info);
  if (size < 4 || *info + uint64_t(size) > file.size()) return std::nullopt;
  const uint32_t base = xex.base_address();
  for (uint32_t at = *info + 4; at + 16 <= *info + size; at += 16) {
    const char* entry = reinterpret_cast<const char*>(file.data() + at);
    if (strnlen(entry, 8) != strlen(name) || std::strncmp(entry, name, 8) != 0) continue;
    const uint32_t address = x360::load_be<uint32_t>(file.data() + at + 8);
    const uint32_t length = x360::load_be<uint32_t>(file.data() + at + 12);
    if (address < base || uint64_t(address - base) + length > image.size()) return std::nullopt;
    return image.subspan(address - base, length);
  }
  return std::nullopt;
}

std::optional<std::vector<uint8_t>> xex_title_image(const Xex& xex, std::span<const uint8_t> image) {
  auto info = xex.execution_info();
  if (!info) return std::nullopt;
  char name[9];
  std::snprintf(name, sizeof(name), "%08X", info->title_id);
  auto xdbf = xex_resource(xex, image, name);
  if (!xdbf) return std::nullopt;
  auto png = xdbf_entry(*xdbf, kXdbfImageSection, kXdbfTitleImage);
  if (!png) return std::nullopt;
  return std::vector<uint8_t>(png->begin(), png->end());
}

}  // namespace x360tool
