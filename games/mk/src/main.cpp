// Mortal Kombat Komplete Edition host executable. Command line and paths: see x360/host.h.
//
// NetherRealm's Unreal Engine 3 fighting game: up to four players on one console (versus, the tag
// modes), each with a controller or the keyboard and mouse mapped to one.
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Mortal Kombat runs at 60 FPS on the console: it presents every vblank. Its fights count frames,
  // so it only runs at 60 (at 30 the game would run at half speed).
  game.vblanks_per_frame = 1;
  game.fixed_fps = 60;
  return x360::host::RunHost(argc, argv, game);
}
