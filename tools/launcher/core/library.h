// The player's library: the catalog games this release has, with what the player has done with
// them (added, from where, their icon and cover). Pictures are paths; the UI decodes them.
#pragma once

#include <vector>

#include "core/paths.h"

struct CatalogGame;

namespace anyx360::launcher {

struct LibraryGame {
  const CatalogGame* info = nullptr;
  fs::path exe;
  bool installed = false;
  fs::path dir;                // the folder holding default.xex
  bool location_gone = false;  // added from a folder that is no longer there
  // Played with its title update (game.json "update"), which isn't installed: it doesn't start.
  bool needs_update = false;
  fs::path icon;               // games\<id>\icon.png, once read from default.xex
  fs::path cover;              // the player's cover, if they chose one
  // Added before icons were read: SaveIcon(*info, dir) reads it (slow, off the UI thread).
  bool needs_icon = false;
};

// Every catalog game whose executable this release has, in catalog order.
std::vector<LibraryGame> ReadLibrary();

// Reads the dashboard icon from the copy's default.xex into games\<id>\icon.png.
bool SaveIcon(const CatalogGame& game, const fs::path& game_dir);

// The player's cover (a .png or .jpg, copied to games\<id>\cover.*) replaces the game's.
bool SetPlayerCover(const CatalogGame& game, const fs::path& image);
void RestoreCover(const CatalogGame& game);

}  // namespace anyx360::launcher
