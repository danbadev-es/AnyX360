// The in-game panels' look (the F1 overlay, the F2 settings): Inter, a dark theme with Xbox green
// for what is active, and sizes for the window's DPI. The ImGui drawer works in physical pixels, so
// text is rasterized at the screen's own resolution; sizes written in 96-DPI units go through Px().
#pragma once

#include "third_party/imgui/imgui.h"

namespace xe::ui {
class ImGuiDrawer;
}

namespace x360::gpu::theme {

// Fonts and style for the drawer, before its font texture is made (UI thread).
void Setup(xe::ui::ImGuiDrawer* drawer);
// The window moved to a monitor with another scale: fonts and sizes again (UI thread, not while
// drawing).
void OnDpiChanged();

// A size in 96-DPI units, in the drawer's pixels.
float Px(float dips);
float Scale();

ImFont* Body();     // Inter 15
ImFont* Small();    // Inter 13
ImFont* Heading();  // Inter SemiBold 15
ImFont* Title();    // Inter SemiBold 20

namespace color {
constexpr ImU32 Rgb(unsigned rgb, unsigned alpha = 255) {
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}
constexpr ImU32 kBackground = Rgb(0x121417);
constexpr ImU32 kPanel = Rgb(0x1A1D21);
constexpr ImU32 kPanelHigh = Rgb(0x22262B);
constexpr ImU32 kBorder = Rgb(0x2A2E33);
constexpr ImU32 kText = Rgb(0xE6E8EB);
constexpr ImU32 kTextSecondary = Rgb(0x8B9096);
constexpr ImU32 kGreen = Rgb(0x107C10);
constexpr ImU32 kGreenBright = Rgb(0x3CB043);
constexpr ImU32 kAmber = Rgb(0xF0B429);
constexpr ImU32 kRed = Rgb(0xF2545B);
ImVec4 Vec(ImU32 color);
}  // namespace color

}  // namespace x360::gpu::theme
