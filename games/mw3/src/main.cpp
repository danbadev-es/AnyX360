// Call of Duty: Modern Warfare 3 host executable. Command line and paths: see x360/host.h.
//
// The single-player executable (campaign and Special Ops), played with a controller or the keyboard
// and mouse. Choosing Multiplayer starts the multiplayer executable (main_mp.cpp, recompiled from
// default_mp.xex), as the console does.
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  game.xex_name = game_info::kXexName;
  for (const auto& [xex, exe] : game_info::kExecutables) game.executables.push_back({xex, exe});
  // Call of Duty runs at 60 FPS on the console: it presents every vblank.
  game.vblanks_per_frame = 1;
  // The main thread on hardware thread 0, as on the console: the game puts its first worker thread on
  // 1 and keeps per-processor buffers for the models it builds, which the two shared otherwise (the
  // first level crashed while loading).
  game.main_thread_processor = 0;
  return x360::host::RunHost(argc, argv, game);
}
