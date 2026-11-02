// Halo 3 host executable. Command line and paths: see x360/host.h.
//
// Bungie's engine (2007): the campaign for two players in split screen (four over System Link),
// and Forge and local multiplayer custom games for up to four, on its maps and the map packs.
#include "game_info.h"
#include "ppc_config.h"
#include <ppc_context.h>
#include "x360/host.h"

// The game's DLLs, recompiled into this executable (games/halo3/CMakeLists.txt).
extern PPCFuncMapping waveshell_PPCFuncMappings[];
extern PPCFuncMapping waveslib_PPCFuncMappings[];
extern PPCFuncMapping l360_PPCFuncMappings[];
extern PPCFuncMapping q10_PPCFuncMappings[];

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from its title update (0.0.3.5): the copy's 0.0.0.5 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  // The Waves audio processing the game loads at start (it reports a disc read error without it):
  // WaveShell hosts the plug-ins, which import from WavesLibDLL.
  game.modules = {{"WaveShell-Xbox.dll", waveshell_PPCFuncMappings},
                  {"WavesLibDLL.dll", waveslib_PPCFuncMappings},
                  {"waves/L360.dll", l360_PPCFuncMappings},
                  {"waves/Q10.dll", q10_PPCFuncMappings}};
  // WavesLibDLL's function-table slots fall in the 0x90000000 heap, which must not be Xenia's view
  // of the title's image.
  game.separate_xex_4k_heap = true;
  // Made for 30 FPS, as Halo: Reach: it presents every other vblank of the console's 60 Hz, and its
  // menus and cinematics advance a step per frame. At 60 FPS (a 120 Hz vblank) Arrival ran twice as
  // fast. Gameplay keeps time on its own clock.
  game.vblanks_per_frame = 2;
  game.fixed_fps = 30;
  // Its films: the I/O thread writes the one being recorded to cache1:\autosave\<name>.temp while
  // the main thread deletes the folder's stale temporary films, this one included; on the console
  // that delete fails (the recorder doesn't share delete), and here Theater's Recent films stayed
  // empty.
  game.file_share_access = true;
  return x360::host::RunHost(argc, argv, game);
}
