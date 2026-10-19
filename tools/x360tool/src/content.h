// Xbox 360 content packages: the header ("CON ", "LIVE" or "PIRS") of Games on Demand titles,
// downloadable content, title updates, demos and saves. It names the content's type and title,
// and describes where its files are: an STFS volume in the same file (stfs.h), or an SVOD volume
// in the data files next to it (svod.h, Games on Demand).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "util.h"

namespace x360tool {

namespace content_type {
constexpr uint32_t kSavedGame = 0x00000001;
constexpr uint32_t kMarketplace = 0x00000002;  // downloadable content
constexpr uint32_t kGamesOnDemand = 0x00007000;
constexpr uint32_t kGameDemo = 0x00080000;
constexpr uint32_t kInstaller = 0x000B0000;  // title updates
}  // namespace content_type

struct ContentPackage {
  std::string magic;  // "CON ", "LIVE" or "PIRS"
  uint32_t header_size = 0;
  uint32_t content_type = 0;
  uint32_t title_id = 0;
  uint8_t volume_type = 0;  // 0: STFS, 1: SVOD
  std::array<uint8_t, 0x24> volume_descriptor{};
  uint32_t data_file_count = 0;
  std::string display_name;  // UTF-8, the first language's
  std::vector<uint8_t> thumbnail;  // PNG
};

constexpr uint8_t kVolumeStfs = 0;
constexpr uint8_t kVolumeSvod = 1;

// The package header of `path`, or nullopt if it isn't a content package.
std::optional<ContentPackage> read_content_header(const fs::path& path);

// "Games on Demand", "Downloadable content"... for messages.
const char* content_type_name(uint32_t type);

}  // namespace x360tool
