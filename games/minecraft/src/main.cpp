// Minecraft: Xbox 360 Edition host executable. Command line and paths: see x360/host.h.
//
// An Xbox Live Arcade title, played with a controller (or the keyboard and mouse mapped to one), up
// to four players in split screen. Recompiled from its last title update (0.0.80.1, "TU80"), which
// carries most of the game: the arcade package's 0.0.0.1 is patched with it at start.
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  game.arcade = true;
  // The picture of each saved world (its thumbnail in the Load tab) is made on the GPU and read by
  // the CPU: a resolve of the frame, then a memory export. Xenia doesn't copy either back by default,
  // so the PNG came out black. Both read back: the thumbnails show the world, at a cost of about a
  // frame a second at 60 FPS (59 against 60, p99 24-28 ms against 21-22 in a world). settings.toml's
  // [xenia] readback_resolve = "none" and readback_memexport = false go back.
  game.xenia_options = {{"readback_resolve", "\"full\""}, {"readback_memexport", "true"}};
  return x360::host::RunHost(argc, argv, game);
}
