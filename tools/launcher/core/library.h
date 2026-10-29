// The player's library: the catalog games this release has, with what the player has done with
// them (added, from where, their icon and cover). Pictures are paths; the UI decodes them.
#pragma once

#include <optional>
#include <vector>

#include "core/paths.h"
#include "x360/achievements.h"

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

// The game's achievements (x360/achievements.h) in `folder` (AnyX360's folder of %LOCALAPPDATA%): the
// list the game wrote when it last started (from its title update, if it has one), else the one in
// the copy's default.xex, then written there with its pictures. Reading the executable takes a few
// seconds: off the UI thread. None: the game isn't there to read.
std::optional<x360::achievements::TitleList> ReadAchievements(const LibraryGame& game, const fs::path& folder);

// The player's cover (a .png or .jpg, copied to games\<id>\cover.*) replaces the game's.
bool SetPlayerCover(const CatalogGame& game, const fs::path& image);
void RestoreCover(const CatalogGame& game);

}  // namespace anyx360::launcher
