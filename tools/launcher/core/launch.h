// Running a game: its output goes to logs\<id>\last_run.log (the previous run's is kept).
#pragma once

#include <cstdint>
#include <functional>

#include "core/library.h"
#include "core/strings.h"

namespace anyx360::launcher {

// Shaders and pipelines found while testing (bin\shader_cache\<id>): the player's first run doesn't
// compile them during the game. A player who already has a cache keeps it, with what it lacks added.
void SeedShaderCache(const CatalogGame& game);

// Adds to Xenia's cache file `target` (a .xsh of shaders or a .xpso of pipelines) the entries of
// `source` it lacks; copies `source` if there is no `target`. A target in another format version, or
// either file unreadable, is left as it is. Returns how many entries were added.
size_t MergeShaderCacheFile(const fs::path& source, const fs::path& target);

// Starts the game; `exited` is called on another thread with its exit code when it closes.
// Returns an error message, or none on success (a game without the title update it needs doesn't
// start).
Message StartGame(const LibraryGame& game, std::function<void(uint32_t)> exited);

}  // namespace anyx360::launcher
