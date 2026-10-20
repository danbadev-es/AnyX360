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
  // The game presents every vblank: one vblank per frame is 60 FPS.
  game.vblanks_per_frame = 1;
  // The game creates its avatar manager only if the avatar system starts, and uses it regardless.
  game.avatar_system = true;
  game.defaults = [](x360::settings::Settings& s) {
    s.kinect.enabled = true;
    // Played with the body: keyboard and mouse don't drive a pad, so the cursor stays free.
    s.players[0].device = "pad";
    s.players[1].device = "none";
  };
  ks::InstallNui();
  return x360::host::RunHost(argc, argv, game);
}
