// The games this release was built with (games_catalog.h, generated from games/*/game.json) and the
// ones coming soon (kUpcoming, from tools/launcher/upcoming.json).
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "games_catalog.h"

namespace anyx360::launcher {

// The catalog games this release can run (their executable is there).
std::vector<const CatalogGame*> AvailableGames();
const CatalogGame* FindAvailableGame(uint32_t title_id);
const CatalogGame* FindCatalogGame(std::wstring_view id);
// Added to the library: extracted here or remembered where the player's copy is.
bool IsInstalled(const CatalogGame& game);

}  // namespace anyx360::launcher
