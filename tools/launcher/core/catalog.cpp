#include "core/catalog.h"

#include "core/paths.h"

namespace anyx360::launcher {

std::vector<const CatalogGame*> AvailableGames() {
  std::vector<const CatalogGame*> games;
  for (const CatalogGame& game : kCatalog)
    if (!FindGameExe(game).empty()) games.push_back(&game);
  return games;
}

const CatalogGame* FindAvailableGame(uint32_t title_id) {
  for (const CatalogGame* game : AvailableGames())
    if (game->title_id == title_id) return game;
  return nullptr;
}

const CatalogGame* FindCatalogGame(std::wstring_view id) {
  for (const CatalogGame& game : kCatalog)
    if (id == game.id) return &game;
  return nullptr;
}

bool IsInstalled(const CatalogGame& game) {
  return Exists(ExtractedDir(game) / L"default.xex") || Exists(LocationFile(game));
}

}  // namespace anyx360::launcher
