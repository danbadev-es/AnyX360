// Running a game: its output goes to logs\<id>\last_run.log (the previous run's is kept).
#pragma once

#include <cstdint>
#include <functional>

#include "core/library.h"
#include "core/strings.h"

namespace anyx360::launcher {

// Shaders and pipelines found while testing: the player's first run stutters less.
void SeedShaderCache(const CatalogGame& game);

// Starts the game; `exited` is called on another thread with its exit code when it closes.
// Returns an error message, or none on success.
Message StartGame(const LibraryGame& game, std::function<void(uint32_t)> exited);

}  // namespace anyx360::launcher
