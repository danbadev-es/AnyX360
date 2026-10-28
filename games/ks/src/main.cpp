// Kinect Sports host executable. Command line and paths: see x360/host.h.
//
// Rare's sports collection played with the Kinect, which is the virtual sensor (x360/nui.h); the
// [kinect] settings choose where the players' skeletons come from.
#include "game_info.h"
#include "x360/host.h"

namespace ks {
void InstallNui();
}

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from its title update (0.0.4.2): the copy's 0.0.0.2 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  // The game presents every vblank: one vblank per frame is 60 FPS.
  game.vblanks_per_frame = 1;
  // The game creates its avatar manager only if the avatar system starts, and uses it regardless.
  game.avatar_system = true;
  // The avatar library's skeleton builder: avatars are made from the disc's avatar asset pack.
  game.avatar_skeleton = 0x82AD7AE8;
  game.defaults = [](x360::settings::Settings& s) {
    s.kinect.enabled = true;
    // Played with the body: keyboard and mouse don't drive a pad, so the cursor stays free.
    s.players[0].device = "pad";
    s.players[1].device = "none";
  };
  ks::InstallNui();
  return x360::host::RunHost(argc, argv, game);
}
