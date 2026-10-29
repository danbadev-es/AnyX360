// Call of Duty: Black Ops III Zombies host executable. Command line and paths: see x360/host.h.
//
// Recompiled from default_zm.xex: the multiplayer executable starts it when the player chooses
// Zombies, and it starts that one again for multiplayer. Local play only (split screen, games on one
// machine); Xbox Live is gone. Same title, user folder and saves as the multiplayer executable.
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from the same title update (0.0.8.5): default_zm.xex is patched with its
  // default_zm.xexp at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  game.xex_name = game_info::kXexName;
  for (const auto& [xex, exe] : game_info::kExecutables) game.executables.push_back({xex, exe});
  // 60 FPS, every vblank, as in multiplayer.
  game.vblanks_per_frame = 1;
  // The main thread on hardware thread 0, as on the console and in multiplayer.
  game.main_thread_processor = 0;
  return x360::host::RunHost(argc, argv, game);
}
