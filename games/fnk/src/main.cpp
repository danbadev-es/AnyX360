// Fruit Ninja Kinect host executable. Command line and paths: see x360/host.h.
//
// An Xbox Live Arcade title: the game folder is the arcade package's contents. The Kinect is the
// virtual sensor (x360/nui.h, nui_bindings.cpp); the [kinect] settings choose where the players'
// skeletons come from.
#include "game_info.h"
#include "x360/host.h"

namespace fnk {
void InstallNui();
}

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from its title update (0.0.1.3): the copy's 0.0.0.3 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  game.vblanks_per_frame = 1;
  game.arcade = true;
  game.defaults = [](x360::settings::Settings& s) {
    s.kinect.enabled = true;
    // Played with the body: keyboard and mouse don't drive a pad, so the cursor stays free.
    s.players[0].device = "pad";
    s.players[1].device = "none";
  };
  fnk::InstallNui();
  return x360::host::RunHost(argc, argv, game);
}
