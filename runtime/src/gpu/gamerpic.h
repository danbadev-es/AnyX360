// Profiles' gamerpics in the system's UIs drawn over the game (the Guide, the sign-in picker): the
// picture the launcher saved in the profile's folder (profile.json "gamerpic", a PNG: a picture of
// its avatar or of one of the player's games; docs/PROFILES.md).
#pragma once

#include <cstdint>

#include "third_party/imgui/imgui.h"

namespace x360::gpu {

// Draws the gamerpic of the profile `xuid` round at `center`; false if it has none (the caller
// draws its initial). The drawer's thread; the picture is read again when the file changes.
bool DrawGamerpic(ImDrawList* draw, ImVec2 center, float radius, uint64_t xuid);

}  // namespace x360::gpu
