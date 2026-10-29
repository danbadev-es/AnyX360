// Call of Duty: Modern Warfare 3 multiplayer host executable. Command line and paths: see x360/host.h.
//
// The multiplayer executable, recompiled from default_mp.xex: the single-player one starts it when
// the player chooses Multiplayer, and it starts that one again for the campaign. Local play only
// (split screen, private matches on one machine); Xbox Live is gone. Same title, user folder and
// saves as the single-player executable.
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from the same title update (0.0.23.8): default_mp.xex is patched with its
  // default_mp.xexp at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  game.xex_name = game_info::kXexName;
  for (const auto& [xex, exe] : game_info::kExecutables) game.executables.push_back({xex, exe});
  // 60 FPS, every vblank, as in the campaign.
  game.vblanks_per_frame = 1;
  // The main thread on hardware thread 0, as on the console (see main.cpp: per-processor buffers).
  game.main_thread_processor = 0;
  return x360::host::RunHost(argc, argv, game);
}
