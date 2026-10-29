// Resident Evil 6 host executable. Command line and paths: see x360/host.h.
//
// The campaigns, played with a controller or the keyboard and mouse.
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from its title update (0.0.6.3): the copy's 0.0.0.3 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  game.xex_name = game_info::kXexName;
  // Resident Evil 6 runs at 30 FPS on the console: it presents every other vblank.
  game.vblanks_per_frame = 2;
  // A console region from the player's country: told "every region", the game takes the console
  // for a Japanese one and shows its Japanese title, Biohazard 6.
  game.game_region_from_country = true;
  // Entering a new area (the parking level of the prologue, for one) needs dozens of new pipelines
  // at once; with Xenia's asynchronous pipeline creation their draws were skipped until they were
  // ready, so meshes, lighting and effects went missing and came back for a few seconds. The draws
  // of a frame may now wait up to 100 ms in all for their pipelines (they are usually ready within
  // a few milliseconds).
  game.xenia_options = {{"async_shader_compilation_wait_ms", "100"}};
  return x360::host::RunHost(argc, argv, game);
}
