// Fonts embedded in the runtime: Inter (Regular and SemiBold, Latin with arrows, ✓ and ●;
// thirdparty/fonts/inter, SIL Open Font License 1.1), for the in-game panels and the Kinect window.
#pragma once

#include <cstdint>
#include <span>

namespace x360::fonts {

std::span<const uint8_t> InterRegular();
std::span<const uint8_t> InterSemiBold();

}  // namespace x360::fonts
