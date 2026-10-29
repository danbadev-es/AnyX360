// Call of Duty: Black Ops III host executable. Command line and paths: see x360/host.h.
//
// The executable the console starts, default.xex: multiplayer (the Xbox 360 version has no
// campaign). Its menu starts the other executable for Zombies (default_zm.xex, see main_zm.cpp).
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from its title update (0.0.8.5): the copy's 0.0.0.5 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  game.xex_name = game_info::kXexName;
  for (const auto& [xex, exe] : game_info::kExecutables) game.executables.push_back({xex, exe});
  // Call of Duty presents every vblank (60 FPS), as Black Ops II does.
  game.vblanks_per_frame = 1;
  // The main thread on hardware thread 0, as on the console (the engine places its other threads on
  // the remaining hardware threads and keeps per-processor data, as Black Ops II's does).
  game.main_thread_processor = 0;
  return x360::host::RunHost(argc, argv, game);
}
