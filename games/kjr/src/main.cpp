// Kinect Joy Ride host executable. Command line and paths: see x360/host.h.
//
// BigPark's kart racer played with the Kinect, which is the virtual sensor (x360/nui.h); the
// [kinect] settings choose where the players' skeletons come from. The drivers are Xbox Avatars,
// made from the avatar asset pack on the disc (docs/AVATARS.md).
#include "game_info.h"
#include "x360/host.h"

namespace kjr {
void InstallNui();
}

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // The game presents every second vblank (30 FPS on the console): two vblanks per frame is 60 FPS.
  game.vblanks_per_frame = 2;
  // The drivers are avatars: the avatar system starts, and avatars are made from the asset pack
  // with the avatar library's skeleton builder (the same XAVATAR 2.0.11775 as Kinect Sports').
  game.avatar_system = true;
  game.avatar_skeleton = 0x82CC8C30;
  game.defaults = [](x360::settings::Settings& s) {
    s.kinect.enabled = true;
    // Played with the body: keyboard and mouse don't drive a pad, so the cursor stays free.
    s.players[0].device = "pad";
    s.players[1].device = "none";
  };
  kjr::InstallNui();
  return x360::host::RunHost(argc, argv, game);
}
