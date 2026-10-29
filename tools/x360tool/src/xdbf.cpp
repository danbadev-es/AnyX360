#include "xdbf.h"

#include <cstdio>
#include <cstring>

#include "x360/endian.h"

namespace x360tool {

namespace {

// Header: 'XDBF', be32 version, be32 entry table size, be32 entry count, be32 free table size,
// be32 free count. Entries: {be16 section, be64 id, be32 offset (from the data), be32 size}; the
// free table ({be32 offset, be32 size}) follows, then the data. `fn(section, id, data)` returns true
// to stop.
template <typename Fn>
void for_each_entry(std::span<const uint8_t> xdbf, Fn&& fn) {
  if (xdbf.size() < 24 || x360::load_be<uint32_t>(xdbf.data()) != 0x58444246) return;
  const uint64_t entry_table = x360::load_be<uint32_t>(xdbf.data() + 8);
  const uint32_t entry_count = x360::load_be<uint32_t>(xdbf.data() + 12);
  const uint64_t free_table = x360::load_be<uint32_t>(xdbf.data() + 16);
  const uint64_t data = 24 + entry_table * 18 + free_table * 8;
  if (entry_count > entry_table || data > xdbf.size()) return;
  for (uint32_t i = 0; i < entry_count; ++i) {
    const uint8_t* entry = xdbf.data() + 24 + i * 18;
    const uint64_t offset = data + x360::load_be<uint32_t>(entry + 10);
    const uint32_t size = x360::load_be<uint32_t>(entry + 14);
    if (offset + size > xdbf.size()) continue;
    if (fn(x360::load_be<uint16_t>(entry), x360::load_be<uint64_t>(entry + 2), xdbf.subspan(size_t(offset), size)))
      return;
  }
}

constexpr uint32_t kTagXach = 0x58414348;  // 'XACH'
constexpr uint32_t kTagXstc = 0x58535443;  // 'XSTC'
constexpr uint32_t kTagXstr = 0x58535452;  // 'XSTR'

}  // namespace

std::optional<std::span<const uint8_t>> xdbf_entry(std::span<const uint8_t> xdbf, uint16_t section, uint64_t id) {
  std::optional<std::span<const uint8_t>> found;
  for_each_entry(xdbf, [&](uint16_t s, uint64_t i, std::span<const uint8_t> data) {
    if (s != section || i != id) return false;
    found = data;
    return true;
  });
  return found;
}

std::vector<std::pair<uint64_t, std::span<const uint8_t>>> xdbf_entries(std::span<const uint8_t> xdbf,
                                                                       uint16_t section) {
  std::vector<std::pair<uint64_t, std::span<const uint8_t>>> entries;
  for_each_entry(xdbf, [&](uint16_t s, uint64_t id, std::span<const uint8_t> data) {
    if (s == section) entries.push_back({id, data});
    return false;
  });
  return entries;
}

std::optional<std::string> xdbf_string(std::span<const uint8_t> xdbf, uint32_t language, uint16_t id) {
  // 'XSTR', be32 version, be32 size, be16 count, then {be16 id, be16 length, UTF-8}.
  auto table = xdbf_entry(xdbf, kXdbfStringSection, language);
  if (!table || table->size() < 14 || x360::load_be<uint32_t>(table->data()) != kTagXstr) return std::nullopt;
  size_t at = 14;
  const uint16_t count = x360::load_be<uint16_t>(table->data() + 12);
  for (uint16_t i = 0; i < count && at + 4 <= table->size(); ++i) {
    const uint16_t string_id = x360::load_be<uint16_t>(table->data() + at);
    const uint16_t length = x360::load_be<uint16_t>(table->data() + at + 2);
    if (at + 4 + length > table->size()) break;
    if (string_id == id) return std::string(reinterpret_cast<const char*>(table->data() + at + 4), length);
    at += 4 + length;
  }
  return std::nullopt;
}

uint32_t xdbf_default_language(std::span<const uint8_t> xdbf) {
  // 'XSTC', be32 version, be32 size, be32 language.
  auto xstc = xdbf_entry(xdbf, kXdbfMetadataSection, kTagXstc);
  if (!xstc || xstc->size() < 16 || x360::load_be<uint32_t>(xstc->data()) != kTagXstc) return 1;
  const uint32_t language = x360::load_be<uint32_t>(xstc->data() + 12);
  return language ? language : 1;
}

std::vector<XdbfAchievement> xdbf_achievements(std::span<const uint8_t> xdbf) {
  std::vector<XdbfAchievement> list;
  auto xach = xdbf_entry(xdbf, kXdbfMetadataSection, kTagXach);
  if (!xach || xach->size() < 14 || x360::load_be<uint32_t>(xach->data()) != kTagXach) return list;
  const uint16_t count = x360::load_be<uint16_t>(xach->data() + 12);
  constexpr size_t kEntry = 0x24;
  for (size_t i = 0; i < count && 14 + (i + 1) * kEntry <= xach->size(); ++i) {
    const uint8_t* e = xach->data() + 14 + i * kEntry;
    XdbfAchievement a;
    a.id = x360::load_be<uint16_t>(e + 0);
    a.name_id = x360::load_be<uint16_t>(e + 2);
    a.unlocked_id = x360::load_be<uint16_t>(e + 4);
    a.locked_id = x360::load_be<uint16_t>(e + 6);
    a.image_id = x360::load_be<uint32_t>(e + 8);
    a.gamerscore = x360::load_be<uint16_t>(e + 12);
    a.flags = x360::load_be<uint32_t>(e + 16);
    list.push_back(a);
  }
  return list;
}

x360::achievements::TitleList xdbf_title_list(std::span<const uint8_t> xdbf, uint32_t title_id,
                                              std::map<uint32_t, std::vector<uint8_t>>* images) {
  x360::achievements::TitleList list;
  list.title_id = title_id;
  // FNV-1a of the whole SPA: another SPA (a title update's) writes the list again.
  uint64_t hash = 0xCBF29CE484222325ull;
  for (uint8_t b : xdbf) hash = (hash ^ b) * 0x100000001B3ull;
  char source[24];
  std::snprintf(source, sizeof(source), "%016llX", (unsigned long long)hash);
  list.source = source;
  list.title = xdbf_string(xdbf, 1, kXdbfTitleName).value_or("");
  if (list.title.empty()) list.title = xdbf_string(xdbf, xdbf_default_language(xdbf), kXdbfTitleName).value_or("");
  std::vector<uint32_t> languages;
  for (const auto& [id, data] : xdbf_entries(xdbf, kXdbfStringSection))
    if (!x360::achievements::LanguageTag(uint32_t(id)).empty()) languages.push_back(uint32_t(id));
  for (const XdbfAchievement& entry : xdbf_achievements(xdbf)) {
    x360::achievements::Achievement a;
    a.id = entry.id;
    a.gamerscore = entry.gamerscore;
    a.image_id = entry.image_id;
    a.flags = entry.flags;
    for (uint32_t language : languages) {
      x360::achievements::Strings s;
      s.name = xdbf_string(xdbf, language, entry.name_id).value_or("");
      s.locked = xdbf_string(xdbf, language, entry.locked_id).value_or("");
      s.unlocked = xdbf_string(xdbf, language, entry.unlocked_id).value_or("");
      if (!s.name.empty()) a.strings[x360::achievements::LanguageTag(language)] = std::move(s);
    }
    if (images && !images->count(entry.image_id)) {
      if (auto png = xdbf_entry(xdbf, kXdbfImageSection, entry.image_id))
        (*images)[entry.image_id] = std::vector<uint8_t>(png->begin(), png->end());
    }
    list.achievements.push_back(std::move(a));
  }
  return list;
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

std::optional<std::span<const uint8_t>> xex_title_xdbf(const Xex& xex, std::span<const uint8_t> image) {
  auto info = xex.execution_info();
  if (!info) return std::nullopt;
  char name[9];
  std::snprintf(name, sizeof(name), "%08X", info->title_id);
  return xex_resource(xex, image, name);
}

std::optional<std::vector<uint8_t>> xex_title_image(const Xex& xex, std::span<const uint8_t> image) {
  auto xdbf = xex_title_xdbf(xex, image);
  if (!xdbf) return std::nullopt;
  auto png = xdbf_entry(*xdbf, kXdbfImageSection, kXdbfTitleImage);
  if (!png) return std::nullopt;
  return std::vector<uint8_t>(png->begin(), png->end());
}

}  // namespace x360tool
