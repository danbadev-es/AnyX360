// Doritos Crash Course host executable. Command line and paths: see x360/host.h.
//
// An Xbox Live Arcade title: the game folder is the arcade package's contents. Played with a
// controller (or the keyboard and mouse mapped to one). Every character is an Xbox Avatar, made
// from the avatar asset pack that Kinect Sports' disc carries (docs/AVATARS.md).
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  game.arcade = true;
  game.avatar_system = true;
  // The avatar library's default skeleton (this version has no shaping by height and weight).
  game.avatar_skeleton = 0x825F7EE0;
  return x360::host::RunHost(argc, argv, game);
}
