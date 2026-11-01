// Call of Duty: Black Ops II host executable. Command line and paths: see x360/host.h.
//
// The single-player executable (the campaign), played with a controller or the keyboard and mouse.
// Its main menu starts the other executable (default_mp.xex: multiplayer and Zombies, see
// main_mp.cpp).
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from its title update (0.0.18.14): the copy's 0.0.0.14 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  game.xex_name = game_info::kXexName;
  for (const auto& [xex, exe] : game_info::kExecutables) game.executables.push_back({xex, exe});
  // Call of Duty runs at 60 FPS on the console: it presents every vblank.
  game.vblanks_per_frame = 1;
  // The main thread on hardware thread 0, as on the console (the engine places its other threads on
  // the remaining hardware threads, as Modern Warfare 3's does).
  game.main_thread_processor = 0;
  // One submission to the GPU per frame instead of one each time the command processor reaches the
  // end of the ring (about three a frame): Xenia's command processor sets the frame rate in heavy
  // scenes, and each submission costs it the wait for the D3D12 Replay thread's tail and the
  // driver's ExecuteCommandLists. The GPU, busy a quarter of the frame, starts it a little later.
  game.xenia_options = {{"d3d12_submit_on_primary_buffer_end", "false"}};
  return x360::host::RunHost(argc, argv, game);
}
