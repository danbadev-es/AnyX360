// Guitar Hero III: Legends of Rock host executable. Command line and paths: see x360/host.h.
//
// Neversoft's rhythm game: a controller (or the keyboard) per player: career, quick play, practice
// and, for two, face-offs, battles and co-op.
#include "game_info.h"
#include "x360/host.h"
#include "x360/settings.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from its title update (0.0.3.2): the copy's 0.0.0.2 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  game.xex_name = game_info::kXexName;
  // The game presents every vblank: 60 FPS, as on the console.
  game.vblanks_per_frame = 1;
  // A controller plays without a guitar: the game's gamepad layout has the frets on LT, LB, RB, RT
  // and A (green to orange) and the strum on the D-pad. On the keyboard the frets go on 1-5 and the
  // strum on the up and down arrows (F1-F3 are AnyX360's own keys).
  game.defaults = [](x360::settings::Settings& s) {
    auto& b = s.input.bindings;
    b["LT"] = "1";
    b["LB"] = "2";
    b["RB"] = "3";
    b["RT"] = "4";
    b["A"] = "5 SPACE RETURN";
    b["UP"] = "UP";
    b["DOWN"] = "DOWN RSHIFT";
    b["LEFT"] = "LEFT";
    b["RIGHT"] = "RIGHT";
  };
  return x360::host::RunHost(argc, argv, game);
}
