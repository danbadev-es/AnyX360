// Call of Duty: Black Ops II host executable. Command line and paths: see x360/host.h.
//
// The single-player executable (the campaign), played with a controller or the keyboard and mouse.
// Its main menu starts the other executable (default_mp.xex: multiplayer and Zombies, see
// main_mp.cpp).
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
  // The main thread on hardware thread 0, as on the console (the engine places its other threads on
  // the remaining hardware threads, as Modern Warfare 3's does).
  game.main_thread_processor = 0;
  return x360::host::RunHost(argc, argv, game);
}
