// Forza Motorsport 4 host executable. Command line and paths: see x360/host.h.
//
// The single-player game, driven with a controller (or the keyboard and mouse mapped to one). The
// second disc's content (the car packs and Autovista) is installed as content packages.
#include "game_info.h"
#include "ppc_config.h"
#include <ppc_context.h>
#include "x360/host.h"

// The game's DLLs, recompiled into this executable (games/fm4/CMakeLists.txt).
extern PPCFuncMapping xmedia_PPCFuncMappings[];
extern PPCFuncMapping speech_PPCFuncMappings[];

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // XMediaFacade plays the game's videos (WMV): the intro, the loading animations. SpeechFacade is
  // Kinect's voice commands. The game loads both at start and stops with a disc error without them.
  game.modules = {{"XMediaFacade_default.xex", xmedia_PPCFuncMappings},
                  {"SpeechFacade_default.xex", speech_PPCFuncMappings}};
  return x360::host::RunHost(argc, argv, game);
}
