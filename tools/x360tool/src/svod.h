// SVOD: the volume of a Games on Demand package. The disc's game partition (XDVDFS) is stored in
// data files next to the package header (<header>.data\Data0000, Data0001...) as 0x800-byte
// blocks, with hash tables between them: a level-0 table before every 0x198 blocks and a level-1
// table before every 0xA1C4 level-0 tables; each data file holds 0x14388 blocks (0xA290000 bytes).
// Same mapping as Xenia (vfs/devices/xcontent_devices/svod_container_device.cc).
#pragma once

#include <memory>

#include "content.h"
#include "xdvdfs.h"

namespace x360tool {

// The game partition of the Games on Demand package whose header is `header` (throws if it
// isn't one, or its data files are missing).
std::unique_ptr<XdvdfsImage> open_games_on_demand(const fs::path& header, ContentPackage* package = nullptr);

}  // namespace x360tool
