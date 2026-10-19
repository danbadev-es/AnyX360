// XDBF: the title's dashboard data (SPA), a resource of the XEX named after the title ID. It holds
// the title's strings, achievements and images; the launcher shows the title image (the 64x64 PNG
// icon the Xbox 360 dashboard shows for the game).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "xex.h"

namespace x360tool {

constexpr uint16_t kXdbfImageSection = 2;
constexpr uint64_t kXdbfTitleImage = 0x8000;

// The data of entry {section, id} in an XDBF blob, or nullopt if it has none (or isn't XDBF).
std::optional<std::span<const uint8_t>> xdbf_entry(std::span<const uint8_t> xdbf, uint16_t section, uint64_t id);

// The RESOURCE_INFO resource `name` of the XEX, as bytes of its extracted image (Xex::extract_image).
std::optional<std::span<const uint8_t>> xex_resource(const Xex& xex, std::span<const uint8_t> image,
                                                     const char* name);

// The title image (PNG) of the XEX's own title: its XDBF resource is named after the title ID.
std::optional<std::vector<uint8_t>> xex_title_image(const Xex& xex, std::span<const uint8_t> image);

}  // namespace x360tool
