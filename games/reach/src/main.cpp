// Halo: Reach host executable. Command line and paths: see x360/host.h.
//
// Bungie's engine (2010), Halo 3's successor: the campaign and Firefight for up to four players in
// split screen, and local multiplayer custom games on its maps and the downloadable map packs.
#include "game_info.h"
#include "ppc_config.h"
#include <ppc_context.h>
#include "x360/host.h"

// The game's DLLs, recompiled into this executable (games/reach/CMakeLists.txt).
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
  // Recompiled from its title update (0.0.1.7): the copy's 0.0.0.7 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  // The Waves audio processing the game loads at start, as Halo 3 does: WaveShell hosts the
  // plug-ins, which import from WavesLibDLL.
  game.modules = {{"WaveShell-Xbox.dll", waveshell_PPCFuncMappings},
                  {"WavesLibDLL.dll", waveslib_PPCFuncMappings},
                  {"waves/L360.dll", l360_PPCFuncMappings},
                  {"waves/Q10.dll", q10_PPCFuncMappings}};
  // WavesLibDLL's function-table slots fall in the 0x90000000 heap, which must not be Xenia's view
  // of the title's image.
  game.separate_xex_4k_heap = true;
  return x360::host::RunHost(argc, argv, game);
}
