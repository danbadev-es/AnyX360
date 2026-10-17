// Dance Central 3 host executable. Command line and paths: see x360/host.h.
//
// The title runs its vblank at the console's 60 Hz until its presentation interval is measured.
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  game.vblanks_per_frame = 1;
  return x360::host::RunHost(argc, argv, game);
}
