// Running a game: its output goes to logs\<id>\last_run.log (the previous run's is kept).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "core/library.h"
#include "core/strings.h"
#include "x360/dashboard.h"

namespace anyx360::launcher {

// Shaders and pipelines found while testing (bin\shader_cache\<id>): the player's first run doesn't
// compile them during the game. A player who already has a cache keeps it, with what it lacks added.
void SeedShaderCache(const CatalogGame& game);

// Adds to Xenia's cache file `target` (a .xsh of shaders or a .xpso of pipelines) the entries of
// `source` it lacks; copies `source` if there is no `target`. A target in another format version, or
// either file unreadable, is left as it is. Returns how many entries were added.
size_t MergeShaderCacheFile(const fs::path& source, const fs::path& target);

// How the game's window goes with the launcher's (x360/dashboard.h).
struct LaunchOptions {
  // The launcher's process: the game shows its window with its first frame and says so (`ready`),
  // and says before it ends on its own (`leaving`). 0: neither.
  uint32_t dashboard_pid = 0;
  // Borderless at this rectangle (screen pixels), or over its whole monitor with `fullscreen`.
  std::optional<x360::dashboard::WindowRect> window;
  bool fullscreen = false;
};

// The game's command line: the executable, the game and key, then the options' arguments.
std::wstring GameCommandLine(const fs::path& exe, const fs::path& game_dir, const fs::path& key,
                             const LaunchOptions& options);

// What a running game tells the launcher, each on another thread.
struct GameEvents {
  std::function<void()> ready;  // its first frame is shown (with a dashboard)
  // It is about to end (or to replace itself): the dashboard shows, then calls `answer` (any thread).
  std::function<void(std::function<void()> answer)> leaving;
  std::function<void(uint32_t code)> exited;  // every process of the game has ended
};

// Starts the game. Returns an error message, or none on success (a game without the title update
// it needs doesn't start).
Message StartGame(const LibraryGame& game, const LaunchOptions& options, GameEvents events);
inline Message StartGame(const LibraryGame& game, std::function<void(uint32_t)> exited) {
  return StartGame(game, {}, {nullptr, nullptr, std::move(exited)});
}

// Starts `command` (in `dir`, with these standard handles; null: none) in a job object and follows
// it: the processes it starts from the same folder as its own executable (a game replacing itself
// with another of its executables, Modern Warfare 3's multiplayer) are followed too. `exited` is
// called once, on another thread, when the last of them has ended, with that one's exit code.
// Others it starts (a helper in another folder) don't keep it waiting. Returns the Windows error
// (0: started).
uint32_t StartFollowed(std::wstring command, const fs::path& dir, void* std_in, void* std_out,
                       std::function<void(uint32_t code)> exited);

}  // namespace anyx360::launcher
