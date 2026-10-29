// XDBF: the title's dashboard data (SPA), a resource of the XEX named after the title ID. It holds
// the title's strings, achievements and images; the launcher shows the title image (the 64x64 PNG
// icon the Xbox 360 dashboard shows for the game).
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "x360/achievements.h"
#include "xex.h"

namespace x360tool {

constexpr uint16_t kXdbfMetadataSection = 1;  // 'XACH', 'XSTC', 'XTHD'... by their tag
constexpr uint16_t kXdbfImageSection = 2;     // PNGs by image ID
constexpr uint16_t kXdbfStringSection = 3;    // an 'XSTR' table per language (XDBF numbering)
constexpr uint64_t kXdbfTitleImage = 0x8000;
constexpr uint16_t kXdbfTitleName = 0x8000;   // the title's name in each string table

// The data of entry {section, id} in an XDBF blob, or nullopt if it has none (or isn't XDBF).
std::optional<std::span<const uint8_t>> xdbf_entry(std::span<const uint8_t> xdbf, uint16_t section, uint64_t id);

// Every entry of a section: {id, data}.
std::vector<std::pair<uint64_t, std::span<const uint8_t>>> xdbf_entries(std::span<const uint8_t> xdbf, uint16_t section);

// String `id` of the XDBF language's table (1 English, 5 Spanish...: x360::achievements::LanguageTag).
std::optional<std::string> xdbf_string(std::span<const uint8_t> xdbf, uint32_t language, uint16_t id);
// The title's default language ('XSTC'), 1 (English) without one.
uint32_t xdbf_default_language(std::span<const uint8_t> xdbf);

// An achievement as XACH holds it (0x24 bytes: be16 id, name, unlocked and locked description
// string IDs, be32 image ID, be16 gamerscore, 2 bytes, be32 flags, 16 bytes).
struct XdbfAchievement {
  uint16_t id = 0;
  uint16_t name_id = 0, unlocked_id = 0, locked_id = 0;
  uint32_t image_id = 0;
  uint16_t gamerscore = 0;
  uint32_t flags = 0;
};
// The 'XACH' table ('XACH', be32 version, be32 size, be16 count, then the entries); empty without one.
std::vector<XdbfAchievement> xdbf_achievements(std::span<const uint8_t> xdbf);

// The title's achievement list with its strings in every language its SPA has, and their pictures
// (PNG bytes by image ID) into `images` if given. `source` is a hash of the SPA.
x360::achievements::TitleList xdbf_title_list(std::span<const uint8_t> xdbf, uint32_t title_id,
                                              std::map<uint32_t, std::vector<uint8_t>>* images = nullptr);

// The RESOURCE_INFO resource `name` of the XEX, as bytes of its extracted image (Xex::extract_image).
std::optional<std::span<const uint8_t>> xex_resource(const Xex& xex, std::span<const uint8_t> image,
                                                     const char* name);

// The title's XDBF (SPA) in the XEX: its resource named after the title ID.
std::optional<std::span<const uint8_t>> xex_title_xdbf(const Xex& xex, std::span<const uint8_t> image);

// The title image (PNG) of the XEX's own title: its XDBF resource is named after the title ID.
std::optional<std::vector<uint8_t>> xex_title_image(const Xex& xex, std::span<const uint8_t> image);

}  // namespace x360tool
