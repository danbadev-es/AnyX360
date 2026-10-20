// Call of Duty: Black Ops II multiplayer and Zombies host executable. Command line and paths: see
// x360/host.h.
//
// Recompiled from default_mp.xex: the single-player executable starts it when the player chooses
// Multiplayer or Zombies (Zombies passes its choice in the launch data), and it starts that one
// again for the campaign. Local play only (split screen, games on one machine); Xbox Live is gone.
// Same title, user folder and saves as the single-player executable.
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
  // 60 FPS, every vblank, as in the campaign.
  game.vblanks_per_frame = 1;
  // The main thread on hardware thread 0, as on the console and in the campaign.
  game.main_thread_processor = 0;
  return x360::host::RunHost(argc, argv, game);
}
