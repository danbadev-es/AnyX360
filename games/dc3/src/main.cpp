// Dance Central 3 host executable. Command line and paths: see x360/host.h.
//
// The title presents on every vblank, so the vblank runs at the console's 60 Hz. The Kinect is the
// virtual sensor (x360/nui.h, nui_bindings.cpp); the [kinect] settings choose where the players'
// skeletons come from.
#include "game_info.h"
#include "x360/host.h"

namespace dc3 {
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
  // holds in obscured form: their real values come from keys\title_keys.txt.
  game.title_keys = true;
  // Xenia's host render target path (rtv, its default) blows the menus' glass panels out to white
  // (the bloom over them); the rasterizer-ordered view path packs the render targets exactly and
  // draws them as on the console. It costs more GPU time (6.4 ms a frame in a song on an RTX 5060
  // Laptop, against 2.5): settings.toml's [xenia] render_target_path_d3d12 = "rtv" goes back.
  game.xenia_options = {{"render_target_path_d3d12", "\"rov\""}};
  game.vblanks_per_frame = 1;
  game.fixed_fps = 60;  // its timing is untested at 30
  game.defaults = [](x360::settings::Settings& s) {
    s.kinect.enabled = true;
    // Played with the body: keyboard and mouse don't drive a pad, so the cursor stays free.
    s.players[0].device = "pad";
    s.players[1].device = "none";
  };
  dc3::InstallNui();
  return x360::host::RunHost(argc, argv, game);
}
