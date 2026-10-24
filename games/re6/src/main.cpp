// Resident Evil 6 host executable. Command line and paths: see x360/host.h.
//
// The campaigns, played with a controller or the keyboard and mouse.
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  game.xex_name = game_info::kXexName;
  // Resident Evil 6 runs at 30 FPS on the console: it presents every other vblank.
  game.vblanks_per_frame = 2;
  // A console region from the player's country: told "every region", the game takes the console
  // for a Japanese one and shows its Japanese title, Biohazard 6.
  game.game_region_from_country = true;
  return x360::host::RunHost(argc, argv, game);
}
