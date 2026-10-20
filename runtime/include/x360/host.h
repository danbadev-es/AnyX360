// Host entry point shared by every game executable: command line, paths, settings, then the title.
//
//   <Game>.exe [--game <folder with default.xex>] [--key <xex2_retail.key>] [--user <folder>]
//              [--resolution 720|1080|1440|2160] [--scale 1-3] [--fps 30|60] [--fullscreen]
//              [--upscale fsr|cas|bilinear] [--fxaa] [--restarted-from <pid>]
//
// Defaults: a "game" folder next to (or above) the executable, else the game's development folder in
// the repository (GameDescriptor::dev_game_dir); the key in keys\xex2_retail.key; and
// %LOCALAPPDATA%\AnyX360\<user_dir> for saves, cache and settings. Display flags override the values
// in <user>\settings.toml for one session. The settings panel's "Restart now" relaunches with
// --restarted-from: the new process waits for the old one to exit before touching the settings, cache
// and saves.
#pragma once

#include <cstdint>
#include <functional>

#include "x360/settings.h"

namespace x360::host {

struct GameDescriptor {
  const char* title_name = "";    // "Gears of War 3": banner, window title fallback, error messages
  const char* user_dir = "";      // folder under %LOCALAPPDATA%\AnyX360
  const char* dev_game_dir = "";  // game folder relative to the repository root (development builds)
  // Title ID of the default.xex this executable was recompiled from; another XEX is refused before
  // anything starts (the recompiled code only matches its own executable). 0 skips the check.
  uint32_t title_id = 0;
  // The title presents every N vblanks at its normal frame rate (see gpu::SetVblanksPerFrame).
  uint32_t vblanks_per_frame = 2;
  // The only frame rate the title runs at (0: 30 or 60, the player's choice).
  uint32_t fixed_fps = 0;
  // An Xbox Live Arcade title, run from its package's contents: XTitleGetDeploymentType reports a
  // download and its content volume is on the hard drive.
  bool arcade = false;
  // The avatar system starts (XamAvatarInitialize succeeds) although there are no avatars to give: for
  // titles that can't run without it. Others are told it's unavailable and go without avatars.
  bool avatar_system = false;
  // The avatar library's skeleton builder in this title (skeleton*, body type, height f1, weight f2;
  // Kinect Sports 82A84A00): with it the system returns avatars made from the console's avatar
  // asset pack (docs/AVATARS.md). 0: assets requests return nothing.
  uint32_t avatar_skeleton = 0;
  // The hardware thread (0-5) the title's main thread starts on; threads it creates inherit it
  // unless they are given one. The console starts it on 0. The games before Modern Warfare 3 were
  // brought up and tested with 1, so that stays the default; Modern Warfare 3 needs 0 (it places its
  // first worker thread on 1 and keeps per-processor data).
  uint8_t main_thread_processor = 1;
  // Game-specific default settings, applied before settings.toml is read.
  std::function<void(settings::Settings&)> defaults;
  // Called once the function table is filled, before the title runs (game-specific debug hooks).
  std::function<void()> on_function_table;
};

// Parses the command line, loads the settings and runs the title. Returns the process exit code.
int RunHost(int argc, char** argv, const GameDescriptor& game);

}  // namespace x360::host
