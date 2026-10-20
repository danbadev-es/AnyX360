#include "ui_theme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "x360/fonts.h"
#include "x360/kernel.h"
#include "xenia/ui/imgui_drawer.h"

namespace x360::gpu::theme {

namespace {

xe::ui::ImGuiDrawer* g_drawer = nullptr;
float g_scale = 1.0f;
ImFont* g_body = nullptr;
ImFont* g_small = nullptr;
ImFont* g_heading = nullptr;
ImFont* g_title = nullptr;

// Latin, punctuation, the euro and trade mark signs, arrows, ✓ and ● (what the subset has).
const ImWchar kGlyphs[] = {0x0020, 0x017F, 0x2010, 0x2027, 0x2030, 0x203A, 0x20AC, 0x20AC, 0x2122, 0x2122,
                           0x2190, 0x2193, 0x25CF, 0x25CF, 0x2713, 0x2713, 0};

ImFont* AddInter(ImGuiIO& io, std::span<const uint8_t> ttf, float size) {
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;  // the data is the runtime's
  config.OversampleH = 2;
  config.OversampleV = 1;
  config.PixelSnapH = true;
  return io.Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(ttf.data()), int(ttf.size()), std::round(size * g_scale),
                                        &config, kGlyphs);
}

// Fonts[0] is ImGui's default, Fonts[1] the drawer's title font (notifications).
void BuildFonts() {
  ImGuiIO& io = g_drawer->GetIO();
  io.Fonts->Clear();
  g_body = AddInter(io, fonts::InterRegular(), 15);
  g_title = AddInter(io, fonts::InterSemiBold(), 20);
  g_heading = AddInter(io, fonts::InterSemiBold(), 15);
  g_small = AddInter(io, fonts::InterRegular(), 13);
  io.FontDefault = g_body;
}

void ApplyStyle() {
  ImGuiStyle style;  // from the defaults each time: ScaleAllSizes multiplies
  style.WindowPadding = ImVec2(16, 16);
  style.FramePadding = ImVec2(10, 6);
  style.CellPadding = ImVec2(8, 5);
  style.ItemSpacing = ImVec2(10, 8);
  style.ItemInnerSpacing = ImVec2(8, 6);
  style.IndentSpacing = 18;
  style.ScrollbarSize = 12;
  style.GrabMinSize = 12;
  style.WindowRounding = 8;
  style.ChildRounding = 6;
  style.FrameRounding = 4;
  style.PopupRounding = 6;
  style.ScrollbarRounding = 6;
  style.GrabRounding = 3;
  style.TabRounding = 4;
  style.WindowBorderSize = 1;
  style.ChildBorderSize = 1;
  style.PopupBorderSize = 1;
  style.FrameBorderSize = 0;
  style.SeparatorTextBorderSize = 1;
  style.SeparatorTextPadding = ImVec2(0, 4);
  style.WindowTitleAlign = ImVec2(0, 0.5f);
  style.ScaleAllSizes(g_scale);

  using namespace color;
  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = Vec(kText);
  c[ImGuiCol_TextDisabled] = Vec(kTextSecondary);
  c[ImGuiCol_WindowBg] = Vec(Rgb(0x121417, 248));
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = Vec(kPanel);
  c[ImGuiCol_Border] = Vec(kBorder);
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = Vec(kPanelHigh);
  c[ImGuiCol_FrameBgHovered] = Vec(Rgb(0x2A2F35));
  c[ImGuiCol_FrameBgActive] = Vec(Rgb(0x30363D));
  c[ImGuiCol_TitleBg] = Vec(kBackground);
  c[ImGuiCol_TitleBgActive] = Vec(kBackground);
  c[ImGuiCol_TitleBgCollapsed] = Vec(kBackground);
  c[ImGuiCol_MenuBarBg] = Vec(kPanel);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = Vec(Rgb(0x2E3338));
  c[ImGuiCol_ScrollbarGrabHovered] = Vec(Rgb(0x3A4047));
  c[ImGuiCol_ScrollbarGrabActive] = Vec(Rgb(0x454C54));
  c[ImGuiCol_CheckMark] = Vec(kGreenBright);
  c[ImGuiCol_SliderGrab] = Vec(kGreenBright);
  c[ImGuiCol_SliderGrabActive] = Vec(Rgb(0x52C75A));
  c[ImGuiCol_Button] = Vec(Rgb(0x262A30));
  c[ImGuiCol_ButtonHovered] = Vec(Rgb(0x2F343B));
  c[ImGuiCol_ButtonActive] = Vec(kGreen);
  c[ImGuiCol_Header] = Vec(Rgb(0x23282E));
  c[ImGuiCol_HeaderHovered] = Vec(Rgb(0x2A2F35));
  c[ImGuiCol_HeaderActive] = Vec(Rgb(0x30363D));
  c[ImGuiCol_Separator] = Vec(kBorder);
  c[ImGuiCol_SeparatorHovered] = Vec(kGreenBright);
  c[ImGuiCol_SeparatorActive] = Vec(kGreenBright);
  c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ResizeGripHovered] = Vec(kGreenBright);
  c[ImGuiCol_ResizeGripActive] = Vec(kGreenBright);
  c[ImGuiCol_Tab] = Vec(kPanelHigh);
  c[ImGuiCol_TabHovered] = Vec(Rgb(0x2A2F35));
  c[ImGuiCol_TabSelected] = Vec(Rgb(0x30363D));
  c[ImGuiCol_TableHeaderBg] = Vec(kPanelHigh);
  c[ImGuiCol_TableBorderStrong] = Vec(kBorder);
  c[ImGuiCol_TableBorderLight] = Vec(Rgb(0x23272C));
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = Vec(Rgb(0xFFFFFF, 6));
  c[ImGuiCol_PlotLines] = Vec(kGreenBright);
  c[ImGuiCol_PlotLinesHovered] = Vec(kText);
  c[ImGuiCol_PlotHistogram] = Vec(kGreenBright);
  c[ImGuiCol_PlotHistogramHovered] = Vec(kText);
  c[ImGuiCol_TextSelectedBg] = Vec(Rgb(0x3CB043, 90));
  c[ImGuiCol_NavCursor] = Vec(kGreenBright);
  c[ImGuiCol_ModalWindowDimBg] = Vec(Rgb(0x000000, 110));
  g_drawer->GetIO();  // the drawer's context is the current one
  ImGui::GetStyle() = style;
}

}  // namespace

ImVec4 color::Vec(ImU32 c) {
  return ImVec4(float(c & 0xFF) / 255.0f, float((c >> 8) & 0xFF) / 255.0f, float((c >> 16) & 0xFF) / 255.0f,
                float((c >> 24) & 0xFF) / 255.0f);
}

// ANYX360_UI_SCALE=<factor> (tests): the panels at another scale than the window's DPI.
float ScaleFor(xe::ui::ImGuiDrawer* drawer) {
  if (const char* forced = std::getenv("ANYX360_UI_SCALE")) return std::clamp(float(std::atof(forced)), 0.5f, 4.0f);
  return std::max(drawer->GetDpiScale(), 1.0f);
}

void Setup(xe::ui::ImGuiDrawer* drawer) {
  g_drawer = drawer;
  drawer->SetPhysicalPixels(true);
  g_scale = ScaleFor(drawer);
  BuildFonts();
  ApplyStyle();
  KLOG_I("display: panels at %.0f %% scale", g_scale * 100);
}

void OnDpiChanged() {
  if (!g_drawer) return;
  const float scale = ScaleFor(g_drawer);
  if (scale == g_scale) return;
  g_scale = scale;
  BuildFonts();
  ApplyStyle();
  g_drawer->RebuildFontTexture();
  KLOG_I("display: panels at %.0f %% scale", g_scale * 100);
}

float Px(float dips) { return std::round(dips * g_scale); }
float Scale() { return g_scale; }

ImFont* Body() { return g_body; }
ImFont* Small() { return g_small; }
ImFont* Heading() { return g_heading; }
ImFont* Title() { return g_title; }

}  // namespace x360::gpu::theme
