// Doritos Crash Course host executable. Command line and paths: see x360/host.h.
//
// An Xbox Live Arcade title: the game folder is the arcade package's contents. Played with a
// controller (or the keyboard and mouse mapped to one).
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  game.arcade = true;
  return x360::host::RunHost(argc, argv, game);
}
