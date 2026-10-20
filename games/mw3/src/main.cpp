// Call of Duty: Modern Warfare 3 host executable. Command line and paths: see x360/host.h.
//
// The single-player executable (campaign and Special Ops), played with a controller or the keyboard
// and mouse. The multiplayer executable (default_mp.xex) is a different program and is not
// recompiled.
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Call of Duty runs at 60 FPS on the console: it presents every vblank.
  game.vblanks_per_frame = 1;
  return x360::host::RunHost(argc, argv, game);
}
