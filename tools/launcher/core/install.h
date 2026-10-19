// Putting a checked copy in its place: games\<id>\game, the title's downloadable content, and
// Games on Demand packages converted into files (the game can't run from the package).
#pragma once

#include <cstdint>
#include <vector>

#include "content.h"
#include "core/jobs.h"

struct CatalogGame;

namespace anyx360::launcher {

void RemoveAll(const fs::path& path);
// Removes a folder quickly: renamed out of the way first, so a half-deleted copy never looks ready.
void RemoveFolder(const fs::path& path);

// Free space on the release's drive for `needed` bytes.
bool EnoughSpace(uint64_t needed, Message& error);

// Moves a checked copy (the folder holding default.xex) to games\<id>\game, replacing an earlier
// copy, extracted or used in place, but keeping the player's cover.
bool InstallCopy(const CatalogGame& game, const fs::path& found, Message& error);

// Writes the disc's files of a Games on Demand package to `out`, reporting progress in [from, to].
bool ExtractGamesOnDemand(Job& job, const fs::path& header, const fs::path& out, int from, int to, Message& error);

// Extracts downloadable content packages where the runtime looks for them:
// %LOCALAPPDATA%\AnyX360\<user dir>\content\00000002\<package name>\. Returns how many.
int InstallDownloadableContent(const CatalogGame& game, const std::vector<fs::path>& packages);

// Converts the Games on Demand copy at `header` (in `source`, an extracted archive or the player's
// folder) into games\<id>\game, with its downloadable content. Progress goes from `from` to 99.
Outcome AddGamesOnDemand(Job& job, const fs::path& source, const fs::path& header,
                         const x360tool::ContentPackage& package, int from);

}  // namespace anyx360::launcher
