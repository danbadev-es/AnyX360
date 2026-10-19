// What a copy is: the folder holding default.xex, its title, whether it is complete, and Games on
// Demand packages with their downloadable content.
#pragma once

#include <cstdint>
#include <vector>

#include "content.h"
#include "core/paths.h"
#include "core/strings.h"

struct CatalogGame;

namespace anyx360::launcher {

// The folder holding default.xex: `folder` itself or one up to 4 levels below it (archives and copies
// often have the disc inside a folder or two).
fs::path FindGameRoot(const fs::path& folder);
// The title ID in default.xex's header.
bool ReadTitleId(const fs::path& xex, uint32_t& title_id);
// Which of this release's games the copy in `root` is, or an error.
const CatalogGame* IdentifyCopy(const fs::path& root, Message& error);
// A copy must be complete: the game's required files are there.
bool CheckCopy(const fs::path& root, const CatalogGame& game, Message& error);

// A Games on Demand copy is the console's HDD content: <TitleID>\00007000\<package>, with the disc's
// files in <package>.data\Data0000... (x360tool's svod.cpp reads them), and maybe the title's
// downloadable content in <TitleID>\00000002\.

// The Games on Demand package in `folder` (up to 4 levels down), or empty.
fs::path FindGamesOnDemand(const fs::path& folder, x360tool::ContentPackage* package = nullptr);
// The title's downloadable content packages in `folder` (<TitleID>\00000002\*).
std::vector<fs::path> FindDownloadableContent(const fs::path& folder, uint32_t title_id);

}  // namespace anyx360::launcher
