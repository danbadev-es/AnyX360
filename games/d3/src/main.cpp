// Diablo III: Reaper of Souls - Ultimate Evil Edition host executable. Command line and paths: see
// x360/host.h.
//
// Blizzard's action role-playing game: one to four players on the same screen, each with a
// controller.
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from its title update (0.0.2.14): the copy's 0.0.0.14 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  game.xex_name = game_info::kXexName;
  // The game presents on every vblank (60 FPS on the console); at the default 120 Hz vblank it
  // presented 70-90 frames a second.
  game.vblanks_per_frame = 1;
  // A new hero's name comes from the on-screen keyboard; cancelled, no hero can be created.
  game.keyboard_ui_accepts = true;
  return x360::host::RunHost(argc, argv, game);
}
