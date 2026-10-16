// Gears of War 3 host executable. Command line and paths: see x360/host.h.
//
// The title renders at 1280x720 times --scale (default 1; Xenia only scales by integers, so 1080p
// cannot be rendered natively: 2x renders 1440p and costs the GPU about four times as much) and the
// image is scaled to the window with --upscale (default AMD FSR). --fps 60 (default) runs the vblank
// at 120 Hz, as the game presents every second vblank (30 FPS on the console). F11 or Alt+Enter toggle
// fullscreen, F1 the performance overlay, F2 the settings panel.
#include "x360/host.h"

namespace gow3 {
void InstallUe3Trace();
}

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = "Gears of War 3";
  game.user_dir = "GearsOfWar3";
  game.dev_game_dir = "extracted/gow3/Gears Of War 3/AP7kB";
  game.title_id = 0x4D5308AB;
  game.vblanks_per_frame = 2;
  game.on_function_table = gow3::InstallUe3Trace;
  return x360::host::RunHost(argc, argv, game);
}
