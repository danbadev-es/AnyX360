// Dance Central 2 host executable. Command line and paths: see x360/host.h.
//
// Harmonix's Milo engine, one game before Dance Central 3 (games/dc3). The title presents on every
// vblank, so the vblank runs at the console's 60 Hz. The Kinect is the virtual sensor (x360/nui.h,
// nui_bindings.cpp); the [kinect] settings choose where the players' skeletons come from.
#include "game_info.h"
#include "x360/host.h"

namespace dc2 {
void InstallNui();
}

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from the title update (0.0.1.5): the disc's 0.0.0.5 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  // Each song's key is derived through the security engine's key slots from a table the console
  // holds in obscured form (the same four keys as Dance Central 3's): their real values come from
  // keys\title_keys.txt.
  game.title_keys = true;
  game.vblanks_per_frame = 1;
  game.fixed_fps = 60;  // as Dance Central 3: its timing is untested at 30
  game.defaults = [](x360::settings::Settings& s) {
    s.kinect.enabled = true;
    // Played with the body: keyboard and mouse don't drive a pad, so the cursor stays free.
    s.players[0].device = "pad";
    s.players[1].device = "none";
  };
  dc2::InstallNui();
  return x360::host::RunHost(argc, argv, game);
}
