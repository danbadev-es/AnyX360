// Settings panel (F2): display (window size, fullscreen, upscaling, FXAA, frame rate, render scale
// with a restart), mouse and keyboard, players' devices, the virtual Kinect (Kinect games), and
// frame pacing graphs, as pages in a sidebar, each in titled sections of labelled rows (the look is
// ui_theme's: Inter, dark, Xbox green). While it is open the game gets no keyboard and mouse input
// and the cursor is free.
//
// Everything that changes the window or the presenter is posted to the UI thread instead of running
// inside OnDraw (Xenia forbids surface changes during a paint).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>
#include <thread>

#include "anyx360_version.h"
#include "frame_timing.h"
#include "gpu_internal.h"
#include "stats_overlay.h"
#include "kinect_window.h"
#include "ui_theme.h"
#include "third_party/imgui/imgui.h"
#include "x360/input.h"
#include "x360/kernel.h"
#include "x360/nui.h"
#include "x360/settings.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/window_win.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::gpu {

namespace {

using Upscale = DisplayConfig::Upscale;
using settings::Settings;

// ---- actions (UI thread; shared with ANYX360_SETTINGS_TEST) ------------------------------------

void SetResolution(uint32_t lines) {
  uint32_t width, height;
  settings::ResolutionSize(lines, width, height);
  settings::Update([&](Settings& s) { s.display.width = width, s.display.height = height; });
  ApplyWindowSize(width, height);
}

void SetUpscaleSetting(Upscale upscale) {
  settings::Update([&](Settings& s) { s.display.upscale = upscale; });
  SetUpscale(upscale);
}

void SetFxaaSetting(bool fxaa) {
  settings::Update([&](Settings& s) { s.display.fxaa = fxaa; });
  SetFxaa(fxaa);
}

void SetVsyncSetting(bool vsync) {
  settings::Update([&](Settings& s) { s.display.vsync = vsync; });
  SetVsync(vsync);
}

void SetDisplayLockSetting(bool lock) {
  settings::Update([&](Settings& s) { s.pacing.display_lock = lock; });
  SetDisplayLock(lock);
}

void SetFps(uint32_t fps) {
  settings::Update([&](Settings& s) { s.display.target_fps = fps; });
  SetTargetFps(fps);
}

void SetRenderScale(uint32_t scale) {
  settings::Update([&](Settings& s) { s.display.render_scale = scale; });
  KLOG_I("display: render scale %ux after a restart", scale);
}

void ShowStats(bool show) {
  SetStatsOverlayVisible(show);
  settings::Update([&](Settings& s) { s.show_stats = show; });
}

// The [kinect] settings: saved, and applied by the virtual sensor (it follows the settings).
void UpdateKinect(const std::function<void(settings::KinectSettings&)>& change) {
  settings::Update([&](Settings& s) { change(s.kinect); });
}

}  // namespace

void SetKinectWindow(bool open) {
  if (open == kinect_window::IsOpen()) return;
  if (!open) return kinect_window::Close();  // it reports closing: saved then
  auto* window = static_cast<xe::ui::Win32Window*>(GameWindow());
  kinect_window::Open(settings::Get().kinect_window_placement, window ? window->hwnd() : nullptr,
                      [](bool now_open, const std::string& placement) {
                        // The window's thread: saved from the UI thread, like the panel's changes.
                        PostToUIThread([now_open, placement] {
                          settings::Update([&](Settings& s) {
                            s.kinect_window = now_open;
                            if (!placement.empty()) s.kinect_window_placement = placement;
                          });
                        });
                      });
  settings::Update([](Settings& s) { s.kinect_window = true; });
  KLOG_I("kinect window opened");
}

bool IsKinectWindowOpen() { return kinect_window::IsOpen(); }

namespace {

std::string g_restart_error;
std::string g_select_tab;  // test hook: tab to bring to the front

void RestartNow() { g_restart_error = Restart(); }

// ---- the panel --------------------------------------------------------------------------------

using namespace theme;

const char* const kDeviceLabels[] = {"Keyboard + mouse", "Controller (first free)", "Controller 1", "Controller 2",
                                     "Controller 3",     "Controller 4",            "None"};

int DeviceIndex(input::Device d) {
  if (d.kind == input::DeviceKind::kKeyboardMouse) return 0;
  if (d.kind == input::DeviceKind::kXInput) return d.xinput < 0 ? 1 : 2 + d.xinput;
  return 6;
}

input::Device DeviceAt(int index) {
  if (index == 0) return {input::DeviceKind::kKeyboardMouse, -1};
  if (index == 1) return {input::DeviceKind::kXInput, -1};
  if (index >= 2 && index <= 5) return {input::DeviceKind::kXInput, int8_t(index - 2)};
  return {};
}

// "(command line)" when this session's value comes from a flag and differs from the saved one.
void CommandLineNote(bool overridden) {
  if (!overridden) return;
  ImGui::SameLine();
  ImGui::TextDisabled("(command line)");
}

void Note(const char* text) {
  ImGui::PushFont(Small());
  ImGui::PushStyleColor(ImGuiCol_Text, color::Vec(color::kTextSecondary));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

void ColoredText(ImU32 c, const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, color::Vec(c));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

// A section of a page: a panel with a heading.
void BeginCard(const char* title) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color::Vec(color::kPanel));
  ImGui::BeginChild(title, ImVec2(0, 0),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PushFont(Heading());
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, Px(2)));
}

void EndCard() {
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::Dummy(ImVec2(0, Px(6)));
}

// Rows of a label and its control.
bool BeginRows(const char* id) {
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_None)) return false;
  ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, Px(190));
  ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
  return true;
}

void Row(const char* label, const char* help = nullptr) {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (help) ImGui::SetItemTooltip("%s", help);
  ImGui::TableNextColumn();
}

// An on/off switch.
bool Toggle(const char* id, bool* value) {
  const float h = ImGui::GetFrameHeight() * 0.8f, w = h * 1.9f;
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const float y = at.y + (ImGui::GetFrameHeight() - h) / 2;
  const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, ImGui::GetFrameHeight()));
  if (clicked) *value = !*value;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const bool hovered = ImGui::IsItemHovered();
  const ImU32 track = *value ? (hovered ? color::Rgb(0x1A8F1A) : color::kGreen)
                             : (hovered ? color::Rgb(0x3A4047) : color::Rgb(0x30363D));
  draw->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + w, y + h), track, h / 2);
  const float r = h / 2 - Px(3);
  draw->AddCircleFilled(ImVec2(*value ? at.x + w - h / 2 : at.x + h / 2, y + h / 2), r, color::kText);
  return clicked;
}

// A choice among a few values, as joined buttons.
bool Segmented(const char* id, int* value, std::initializer_list<std::pair<const char*, int>> options,
               bool enabled = true) {
  ImGui::PushID(id);
  bool changed = false;
  int i = 0;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Px(2), Px(2)));
  for (const auto& [label, v] : options) {
    if (i++) ImGui::SameLine();
    const bool active = *value == v;
    ImGui::PushStyleColor(ImGuiCol_Button, color::Vec(active ? color::kGreen : color::Rgb(0x262A30)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color::Vec(active ? color::Rgb(0x1A8F1A) : color::Rgb(0x2F343B)));
    ImGui::BeginDisabled(!enabled);
    if (ImGui::Button(label) && !active) *value = v, changed = true;
    ImGui::EndDisabled();
    ImGui::PopStyleColor(2);
  }
  ImGui::PopStyleVar();
  ImGui::PopID();
  return changed;
}

class SettingsPanel final : public xe::ui::ImGuiDialog {
 public:
  explicit SettingsPanel(xe::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  enum Page { kDisplay, kControls, kPlayers, kKinect, kPerformance };

  void OnDraw(ImGuiIO& io) override {
    Settings s = settings::Get();
    Settings saved = settings::GetSaved();
    if (!g_select_tab.empty()) {  // test hook
      const char* const names[] = {"Display", "Controls", "Players", "Kinect", "Performance"};
      for (int i = 0; i < 5; ++i)
        if (g_select_tab == names[i]) page_ = Page(i);
      g_select_tab.clear();
    }
    if (page_ == kKinect && !s.kinect.enabled) page_ = kDisplay;

    const ImVec2 size(std::min(Px(880), io.DisplaySize.x - Px(32)), std::min(Px(620), io.DisplaySize.y - Px(32)));
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    bool open = true;
    const bool visible = ImGui::Begin("AnyX360 settings###anyx360_settings", nullptr,
                                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    if (visible) {
      const float header = Px(58), footer = Px(38), sidebar = Px(196);
      DrawHeader(header, open);
      ImGui::SetCursorPos(ImVec2(0, header));
      DrawSidebar(ImVec2(sidebar, ImGui::GetWindowHeight() - header - footer), s.kinect.enabled);
      ImGui::SetCursorPos(ImVec2(sidebar, header));
      ImGui::BeginChild("##page", ImVec2(0, ImGui::GetWindowHeight() - header - footer),
                        ImGuiChildFlags_AlwaysUseWindowPadding);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Px(10), Px(8)));
      switch (page_) {
        case kDisplay: DrawDisplay(s, saved); break;
        case kControls: DrawControls(s); break;
        case kPlayers: DrawPlayers(s); break;
        case kKinect: DrawKinect(s); break;
        case kPerformance: DrawPerformance(); break;
      }
      ImGui::PopStyleVar();
      ImGui::EndChild();
      DrawFooter(footer);
    }
    ImGui::End();
    if (!open) PostToUIThread(CloseSettingsPanel);
  }

 private:
  void DrawHeader(float height, bool& open) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const float w = ImGui::GetWindowWidth();
    draw->AddLine(ImVec2(o.x, o.y + height - 1), ImVec2(o.x + w, o.y + height - 1), color::kBorder);
    ImGui::SetCursorPos(ImVec2(Px(20), (height - Title()->FontSize) / 2));
    ImGui::PushFont(Title());
    ImGui::TextUnformatted("Settings");
    ImGui::PopFont();
    // Close (F2 or Esc too).
    const float button = Px(32);
    ImGui::SetCursorPos(ImVec2(w - button - Px(14), (height - button) / 2));
    if (ImGui::InvisibleButton("##close", ImVec2(button, button))) open = false;
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    if (ImGui::IsItemHovered()) draw->AddRectFilled(a, b, color::Rgb(0x2A2F35), Px(4));
    const float d = Px(6);
    const ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2);
    draw->AddLine(ImVec2(c.x - d, c.y - d), ImVec2(c.x + d, c.y + d), color::kText, Px(1.6f));
    draw->AddLine(ImVec2(c.x - d, c.y + d), ImVec2(c.x + d, c.y - d), color::kText, Px(1.6f));
  }

  void DrawSidebar(const ImVec2& size, bool kinect) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, color::Vec(color::kPanel));
    ImGui::BeginChild("##nav", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    draw->AddLine(ImVec2(o.x + size.x - 1, o.y), ImVec2(o.x + size.x - 1, o.y + size.y), color::kBorder);
    const struct {
      const char* name;
      Page page;
    } items[] = {{"Display", kDisplay}, {"Controls", kControls}, {"Players", kPlayers}, {"Kinect", kKinect},
                 {"Performance", kPerformance}};
    ImGui::SetCursorPos(ImVec2(0, Px(10)));
    for (const auto& item : items) {
      if (item.page == kKinect && !kinect) continue;
      const bool active = page_ == item.page;
      const ImVec2 at = ImGui::GetCursorScreenPos();
      const ImVec2 row(size.x - Px(16), Px(40));
      ImGui::SetCursorPosX(Px(8));
      if (ImGui::InvisibleButton(item.name, row)) page_ = item.page;
      const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      if (active || ImGui::IsItemHovered()) draw->AddRectFilled(a, b, active ? color::kPanelHigh : color::Rgb(0x1F2328), Px(5));
      if (active) draw->AddRectFilled(ImVec2(a.x, a.y + Px(9)), ImVec2(a.x + Px(3), b.y - Px(9)), color::kGreenBright, Px(2));
      ImFont* font = active ? Heading() : Body();
      draw->AddText(font, font->FontSize, ImVec2(a.x + Px(16), a.y + (row.y - font->FontSize) / 2),
                    active ? color::kText : color::Rgb(0xB4B8BD), item.name);
      (void)at;
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Px(2));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
  }

  void DrawFooter(float height) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const float w = ImGui::GetWindowWidth(), top = ImGui::GetWindowHeight() - height;
    draw->AddRectFilled(ImVec2(o.x, o.y + top), ImVec2(o.x + w, o.y + top + height), color::kPanel,
                        ImGui::GetStyle().WindowRounding, ImDrawFlags_RoundCornersBottom);
    draw->AddLine(ImVec2(o.x, o.y + top), ImVec2(o.x + w, o.y + top), color::kBorder);
    ImFont* font = Small();
    const float y = o.y + top + (height - font->FontSize) / 2;
    draw->AddText(font, font->FontSize, ImVec2(o.x + Px(20), y), color::kTextSecondary, "AnyX360 " ANYX360_VERSION_FULL);
    const char* keys = settings::Get().kinect.enabled
                           ? "F1 performance overlay    F2 / Esc close    F3 Kinect window    F11 / Alt+Enter fullscreen"
                           : "F1 performance overlay    F2 / Esc close    F11 / Alt+Enter fullscreen";
    const float keys_w = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, keys).x;
    draw->AddText(font, font->FontSize, ImVec2(o.x + w - keys_w - Px(20), y), color::kTextSecondary, keys);
  }

  void PageTitle(const char* title, const char* description) {
    ImGui::PushFont(Title());
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    Note(description);
    ImGui::Dummy(ImVec2(0, Px(4)));
  }

  void DrawDisplay(const Settings& s, const Settings& saved) {
    const DisplayConfig& live = GetDisplayConfig();
    PageTitle("Display", "The game's window and image. Changes apply at once and are saved.");

    BeginCard("Window");
    if (BeginRows("##window")) {
      static const uint32_t kLines[] = {720, 1080, 1440, 2160};
      static const char* const kNames[] = {"1280 x 720 (720p)", "1920 x 1080 (1080p)", "2560 x 1440 (1440p)",
                                           "3840 x 2160 (4K)"};
      uint32_t lines = settings::ResolutionLines(s.display);
      int current = 0;
      for (int i = 0; i < 4; ++i)
        if (kLines[i] == lines) current = i;
      Row("Resolution", "The window's size in the screen's own pixels.");
      ImGui::SetNextItemWidth(Px(240));
      if (ImGui::Combo("##resolution", &current, kNames, 4)) {
        uint32_t chosen = kLines[current];
        PostToUIThread([chosen] { SetResolution(chosen); });
      }
      CommandLineNote(lines != settings::ResolutionLines(saved.display));
      bool fullscreen = GameWindow()->IsFullscreen();
      Row("Fullscreen");
      if (Toggle("##fullscreen", &fullscreen)) PostToUIThread([fullscreen] { SetFullscreenMode(fullscreen); });
      if (fullscreen) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("the screen's resolution");
      }
      ImGui::EndTable();
    }
    EndCard();

    BeginCard("Image");
    if (BeginRows("##image")) {
      int upscale = int(s.display.upscale);
      Row("Upscaling", "How the game's 1280x720 image is scaled to the window.");
      if (Segmented("##upscale", &upscale, {{"AMD FSR", int(Upscale::kFsr)}, {"AMD CAS", int(Upscale::kCas)},
                                            {"Bilinear", int(Upscale::kBilinear)}}))
        PostToUIThread([upscale] { SetUpscaleSetting(Upscale(upscale)); });
      CommandLineNote(s.display.upscale != saved.display.upscale);
      bool fxaa = s.display.fxaa;
      Row("FXAA", "Smooths edges after upscaling.");
      if (Toggle("##fxaa", &fxaa)) PostToUIThread([fxaa] { SetFxaaSetting(fxaa); });
      CommandLineNote(s.display.fxaa != saved.display.fxaa);
      bool vsync = s.display.vsync;
      Row("VSync", "Waits for the display's refresh to show each frame: no tearing in fullscreen.");
      if (Toggle("##vsync", &vsync)) PostToUIThread([vsync] { SetVsyncSetting(vsync); });
      Row("Frame rate");
      if (FixedFps()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%u FPS (this game runs at this rate only)", FixedFps());
      } else {
        int fps = int(s.display.target_fps);
        if (Segmented("##fps", &fps, {{"30 FPS", 30}, {"60 FPS", 60}})) PostToUIThread([fps] { SetFps(uint32_t(fps)); });
        CommandLineNote(s.display.target_fps != saved.display.target_fps);
      }
      int scale = int(s.display.render_scale);
      Row("Render scale", "2x costs the GPU about four times as much as 1x, 3x about nine times.");
      if (Segmented("##scale", &scale, {{"1x  1280 x 720", 1}, {"2x  2560 x 1440", 2}, {"3x  3840 x 2160", 3}}))
        PostToUIThread([scale] { SetRenderScale(uint32_t(scale)); });
      ImGui::EndTable();
    }
    if (uint32_t(s.display.render_scale) != live.render_scale) {
      char text[96];
      std::snprintf(text, sizeof(text), "The render scale applies after a restart (now %ux).", live.render_scale);
      ColoredText(color::kAmber, text);
      if (ImGui::Button("Restart now")) PostToUIThread(RestartNow);
      ImGui::SetItemTooltip("Saves the settings and starts the game again. Progress since the last checkpoint is lost.");
    }
    if (!g_restart_error.empty()) ColoredText(color::kRed, g_restart_error.c_str());
    EndCard();

    BeginCard("Overlay");
    if (BeginRows("##overlay")) {
      bool stats = IsStatsOverlayVisible();
      Row("Performance overlay", "Frame rate, frame times, memory: F1 in game.");
      if (Toggle("##stats", &stats)) PostToUIThread([stats] { ShowStats(stats); });
      ImGui::EndTable();
    }
    EndCard();
  }

  void DrawControls(const Settings& s) {
    PageTitle("Controls", "The mouse and keyboard play as an Xbox controller: the mouse is the right stick.");
    settings::InputSettings in = s.input;
    bool changed = false;
    BeginCard("Mouse");
    if (BeginRows("##mouse")) {
      Row("Sensitivity");
      ImGui::SetNextItemWidth(Px(300));
      changed |= ImGui::SliderFloat("##sensitivity", &in.sensitivity, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
      Row("Aiming sensitivity", "While aiming (the left trigger held).");
      ImGui::SetNextItemWidth(Px(300));
      changed |= ImGui::SliderFloat("##aim", &in.aim_sensitivity, 0.1f, 2.0f, "%.2fx");
      Row("Invert vertical look");
      changed |= Toggle("##invert", &in.invert_y);
      int smoothing = int(in.smoothing_ms);
      Row("Smoothing");
      ImGui::SetNextItemWidth(Px(300));
      if (ImGui::SliderInt("##smoothing", &smoothing, 4, 50, "%d ms")) {
        in.smoothing_ms = uint32_t(smoothing);
        changed = true;
      }
      Row("Anti dead zone", "Minimum stick deflection for any mouse movement, so slow movements still turn the camera.");
      ImGui::SetNextItemWidth(Px(300));
      changed |= ImGui::SliderFloat("##antideadzone", &in.anti_deadzone, 0.0f, 0.5f, "%.2f");
      ImGui::EndTable();
    }
    if (ImGui::Button("Reset mouse settings")) {
      settings::InputSettings defaults;
      defaults.bindings = in.bindings;
      in = defaults;
      changed = true;
    }
    if (changed) settings::Update([in](Settings& st) { st.input = in; });
    EndCard();

    BeginCard("Keys");
    if (ImGui::BeginTable("##bindings", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
      auto bindings = input::DescribeBindings();
      for (size_t i = 0; i < bindings.size(); i += 2) {
        ImGui::TableNextRow();
        for (size_t j = i; j < i + 2 && j < bindings.size(); ++j) {
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(bindings[j].first.c_str());
          ImGui::TableNextColumn();
          ImGui::TextDisabled("%s", bindings[j].second.c_str());
        }
      }
      ImGui::EndTable();
    }
    Note("Changed in settings.toml, [input.bindings]. LUP/LDOWN/LLEFT/LRIGHT: the left stick.");
    EndCard();
  }

  void DrawPlayers(const Settings& s) {
    PageTitle("Players", "Who plays with what. Player 1 is always signed in.");
    uint32_t controllers = input::ConnectedControllers();
    BeginCard("Devices");
    ImGui::Text("Controllers connected: %s%s%s%s%s", controllers ? "" : "none", (controllers & 1) ? "1 " : "",
                (controllers & 2) ? "2 " : "", (controllers & 4) ? "3 " : "", (controllers & 8) ? "4" : "");
    if (ImGui::BeginTable("##players", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
      for (uint32_t user = 0; user < 4; ++user) {
        ImGui::PushID(int(user));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Player %u", user + 1);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", s.players[user].name.c_str());
        ImGui::TableNextColumn();
        int device = DeviceIndex(input::GetSlotDevice(user));
        ImGui::SetNextItemWidth(Px(220));
        if (ImGui::Combo("##device", &device, kDeviceLabels, IM_ARRAYSIZE(kDeviceLabels))) {
          input::Device chosen = DeviceAt(device);
          PostToUIThread([user, chosen] { input::SetSlotDevice(user, chosen); });
        }
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        int controller = input::ResolvedController(user);
        if (input::GetSlotDevice(user).kind == input::DeviceKind::kNone)
          ImGui::TextDisabled("-");
        else if (!input::IsConnected(user))
          ImGui::TextColored(color::Vec(color::kAmber), "not connected");
        else if (controller >= 0)
          ImGui::Text("controller %d", controller + 1);
        else
          ImGui::Text("connected");
        ImGui::TableNextColumn();
        bool signed_in = kernel::IsUserSignedIn(user);
        if (user == 0) {
          ImGui::TextDisabled("signed in");
        } else if (signed_in) {
          if (ImGui::SmallButton("Sign out")) PostToUIThread([user] { kernel::SignOutUser(user); });
        } else if (input::IsConnected(user)) {
          if (ImGui::SmallButton("Sign in")) PostToUIThread([user] { kernel::SignInUser(user); });
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    EndCard();

    BeginCard("Signing in");
    if (BeginRows("##signin")) {
      static const char* const kSignIn[] = {"on_demand", "at_start", "manual"};
      static const char* const kSignInLabels[] = {"When the game asks", "At startup", "Only from this panel"};
      int policy = 0;
      for (int i = 0; i < 3; ++i)
        if (s.sign_in == kSignIn[i]) policy = i;
      Row("Players 2-4 sign in");
      ImGui::SetNextItemWidth(Px(240));
      if (ImGui::Combo("##policy", &policy, kSignInLabels, 3)) {
        std::string chosen = kSignIn[policy];
        settings::Update([chosen](Settings& st) { st.sign_in = chosen; });
      }
      ImGui::EndTable();
    }
    Note("Split screen: player 2 presses Start in the main menu to join (as \"not signed in\"). To join with a "
         "profile, sign in at the title screen: signing in later sends the game back to the title screen. Playing "
         "alone with a controller: set player 1 to it.");
    EndCard();
  }

  void DrawKinect(const Settings& s) {
    PageTitle("Kinect", "The virtual Kinect: your webcam (or the mouse) as the game's sensor.");
    const settings::KinectSettings& k = s.kinect;
    const nui::Status status = nui::GetStatus();
    BeginCard("Sensor");
    const bool camera = status.source == "webcam" && status.camera_running;
    ImGui::Text("Source: %s", status.source.empty() ? "not started yet" : status.source.c_str());
    if (!status.message.empty() && !camera) Note(status.message.c_str());
    if (camera) {
      ImGui::Text("Camera %ux%u at %.0f fps, latency %.0f ms", status.camera_width, status.camera_height,
                  status.camera_fps, status.latency_ms);
      ImGui::SetItemTooltip("From the bridge getting the camera's image to the game's frame (the camera's own\n"
                            "delay, before that, isn't measured).");
      char timing[200];
      std::snprintf(timing, sizeof(timing), "waiting %.0f + pose %.0f + mapping %.0f + images %.0f + to the game %.0f ms, %s",
                    status.capture_ms, status.detect_ms, status.process_ms, status.render_ms, status.transfer_ms,
                    status.dropped_frames ? "some camera frames dropped" : "no frames dropped");
      Note(timing);
    }
    ImGui::Text("People in view: %u (%u tracked)", status.bodies, status.tracked);
    if (status.restarts) {
      ImGui::SameLine();
      ImGui::TextDisabled("   pose bridge restarted %u time%s", status.restarts, status.restarts == 1 ? "" : "s");
    }
    DrawSkeletons();
    EndCard();

    BeginCard("Source");
    static const char* const kSources[] = {"webcam", "mouse", "none"};
    static const char* const kSourceLabels[] = {"Webcam", "Mouse (the right hand follows the cursor)", "None"};
    int source = 0;
    for (int i = 0; i < 3; ++i)
      if (k.source == kSources[i]) source = i;
    ImGui::SetNextItemWidth(Px(340));
    if (ImGui::Combo("##source", &source, kSourceLabels, 3)) {
      std::string chosen = kSources[source];
      UpdateKinect([chosen](settings::KinectSettings& kk) { kk.source = chosen; });
    }
    if (k.source == "mouse") Note("Left click: select (a swipe). Right click: back. Space: the Kinect guide gesture.");
    EndCard();
    if (k.source != "webcam") return;

    BeginCard("Webcam");
    if (BeginRows("##webcam")) {
      std::vector<std::string> cameras = nui::ListCameras();
      std::string current = k.camera < cameras.size() ? cameras[k.camera] : "Camera " + std::to_string(k.camera);
      Row("Camera");
      ImGui::SetNextItemWidth(Px(300));
      if (ImGui::BeginCombo("##camera", current.c_str())) {
        for (uint32_t i = 0; i < cameras.size(); ++i) {
          if (ImGui::Selectable(cameras[i].c_str(), i == k.camera))
            UpdateKinect([i](settings::KinectSettings& kk) { kk.camera = i; });
        }
        if (cameras.empty()) ImGui::TextDisabled("No webcam found");
        ImGui::EndCombo();
      }
      ImGui::SameLine();
      if (ImGui::Button("Refresh")) nui::ListCameras(true);
      bool mirror = k.mirror;
      Row("Camera mirrors its image", "Turn this on if raising your right hand moves the skeleton's left hand.");
      if (Toggle("##mirror", &mirror)) UpdateKinect([mirror](settings::KinectSettings& kk) { kk.mirror = mirror; });
      float fov = k.fov;
      Row("Field of view", "The webcam's horizontal field of view (most are 60-78 degrees). A wrong value makes\n"
                           "you look nearer or farther than you are.");
      ImGui::SetNextItemWidth(Px(300));
      if (ImGui::SliderFloat("##fov", &fov, 40.0f, 100.0f, "%.0f degrees"))
        UpdateKinect([fov](settings::KinectSettings& kk) { kk.fov = fov; });
      float smoothing = k.smoothing;
      Row("Smoothing", "More: steadier but later. Less: quicker but jittery.");
      ImGui::SetNextItemWidth(Px(300));
      if (ImGui::SliderFloat("##ksmoothing", &smoothing, 0.0f, 1.0f, "%.2f"))
        UpdateKinect([smoothing](settings::KinectSettings& kk) { kk.smoothing = smoothing; });
      int players = int(k.players);
      Row("Players");
      if (Segmented("##kplayers", &players, {{"1", 1}, {"2", 2}}))
        UpdateKinect([players](settings::KinectSettings& kk) { kk.players = uint32_t(players); });
      int model = k.model == "lite" ? 0 : 1;
      Row("Pose model", "Fast uses less CPU; Accurate tracks better.");
      if (Segmented("##model", &model, {{"Fast", 0}, {"Accurate", 1}}))
        UpdateKinect([model](settings::KinectSettings& kk) { kk.model = model ? "full" : "lite"; });
      // The webcam's tilt: a camera pitched down or rolled makes a standing player look like they're
      // leaning; the bridge measures it (from the player's body, then the floor) and corrects it.
      int tilt_mode = k.tilt_manual ? 1 : 0;
      Row("Camera tilt");
      if (Segmented("##tilt", &tilt_mode, {{"Measured", 0}, {"Set by hand", 1}}))
        UpdateKinect([tilt_mode](settings::KinectSettings& kk) { kk.tilt_manual = tilt_mode == 1; });
      if (tilt_mode == 0) {
        static const char* const kTiltSources[] = {"measuring...", "from your body", "from the floor", "set by hand"};
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (status.tilt_source == 0)
          ImGui::TextDisabled("measuring: stand still, all of you in view");
        else
          ImGui::TextDisabled("%.0f\xC2\xB0 down, %.0f\xC2\xB0 roll (%s)", status.tilt_pitch, status.tilt_roll,
                              kTiltSources[std::min<uint32_t>(status.tilt_source, 3)]);
      } else {
        float pitch = k.tilt_pitch, roll = k.tilt_roll;
        Row("Pitch (down)");
        ImGui::SetNextItemWidth(Px(300));
        if (ImGui::SliderFloat("##pitch", &pitch, -35.0f, 35.0f, "%.0f degrees"))
          UpdateKinect([pitch](settings::KinectSettings& kk) { kk.tilt_pitch = pitch; });
        Row("Roll (clockwise)");
        ImGui::SetNextItemWidth(Px(300));
        if (ImGui::SliderFloat("##roll", &roll, -35.0f, 35.0f, "%.0f degrees"))
          UpdateKinect([roll](settings::KinectSettings& kk) { kk.tilt_roll = roll; });
      }
      ImGui::EndTable();
    }
    if (status.floor_height > 0) {
      char floor[64];
      std::snprintf(floor, sizeof(floor), "Your webcam is %.2f m above the floor.", status.floor_height);
      Note(floor);
    }
    if (ImGui::Button(kinect_window::IsOpen() ? "Close the debug window" : "Open the debug window (F3)"))
      PostToUIThread([] { SetKinectWindow(!kinect_window::IsOpen()); });
    ImGui::SetItemTooltip("Everything the virtual Kinect sees and does, in a window of its own: for a second monitor.");
    ImGui::SameLine();
    if (ImGui::Button("Recentre on me")) nui::Calibrate();
    ImGui::SetItemTooltip("Stand where you'll play: the game then sees you centred, at the Kinect's distance.");
    ImGui::SameLine();
    if (ImGui::Button("Restart camera")) nui::RestartSource();
    Note("Stand 2-3 m from the webcam, in good light, with all of you in view.");
    EndCard();
  }

  // What the game sees, mirrored like a Kinect's view: the camera's color image (when the source
  // has a camera) and the skeletons over it, where the virtual color camera sees them. Each joint
  // is drawn by its state: tracked green, inferred amber. A skeleton off the person in the image
  // means a wrong field of view or tilt.
  void DrawSkeletons() {
    const ImVec2 size(Px(320), Px(240));
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 end(o.x + size.x, o.y + size.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    UpdatePreview();
    if (preview_)
      draw->AddImage(reinterpret_cast<ImTextureID>(preview_.get()), o, end);
    else
      draw->AddRectFilled(o, end, color::Rgb(0x0E1012), Px(4));
    draw->AddRect(o, end, color::kBorder, Px(4));
    const float focal = nui::kColorFocal * size.x / nui::kColorWidth;
    auto project = [&](const float* j) {
      const float z = std::max(j[2], 0.2f);
      return ImVec2(o.x + size.x / 2 + j[0] * focal / z, o.y + size.y / 2 - j[1] * focal / z);
    };
    draw->PushClipRect(o, end, true);
    for (const nui::PreviewBody& body : nui::GetBodies()) {
      const ImU32 c = body.tracked ? color::kGreenBright : color::Rgb(0x9AA0A6);
      for (const auto& bone : nui::kSkeletonBones)
        draw->AddLine(project(body.joints[bone[0]]), project(body.joints[bone[1]]), c, Px(3));
      draw->AddCircleFilled(project(body.joints[nui::kHead]), Px(7), c);
      if (!body.tracked) continue;
      for (uint32_t j = 0; j < nui::kJointCount; ++j) {
        const ImU32 state = body.joint_state[j] == nui::kJointTracked    ? color::kGreen
                            : body.joint_state[j] == nui::kJointInferred ? color::kAmber
                                                                         : color::kRed;
        draw->AddCircleFilled(project(body.joints[j]), Px(3.5f), state);
      }
    }
    draw->PopClipRect();
    ImGui::Dummy(size);
  }

  // The camera's image as a 320x240 texture, made again 10 times a second while the page shows it.
  void UpdatePreview() {
    std::shared_ptr<const std::vector<uint8_t>> color = nui::GetPreviewColor();
    xe::ui::ImmediateDrawer* drawer = SharedImmediateDrawer();
    if (!color || !drawer || color->size() < size_t(nui::kColorWidth) * nui::kColorHeight * 4) {
      preview_.reset();
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (preview_ && now - preview_time_ < std::chrono::milliseconds(100)) return;
    preview_time_ = now;
    constexpr uint32_t kW = nui::kColorWidth / 2, kH = nui::kColorHeight / 2;
    std::vector<uint8_t> rgba(size_t(kW) * kH * 4);
    for (uint32_t y = 0; y < kH; ++y) {
      for (uint32_t x = 0; x < kW; ++x) {
        const uint8_t* p = color->data() + (size_t(y * 2) * nui::kColorWidth + x * 2) * 4;  // BGRA
        uint8_t* q = &rgba[(size_t(y) * kW + x) * 4];
        q[0] = p[2], q[1] = p[1], q[2] = p[0], q[3] = 0xFF;
      }
    }
    preview_ = drawer->CreateTexture(kW, kH, xe::ui::ImmediateTextureFilter::kLinear, false, rgba.data());
  }

  void DrawPerformance() {
    PageTitle("Performance", "How evenly frames reach the screen.");
    timing::Summary t = timing::Summarize(2.0);
    BeginCard("Frame pacing");
    if (BeginRows("##pacing")) {
      Row("Frame rate");
      ImGui::Text("%.1f FPS (target %u), presented by the %s thread", t.vdswaps / 2.0, GetDisplayConfig().target_fps,
                  t.presents_ui_thread > t.presents_gpu_thread ? "UI" : "GPU");
      Row("Frame interval");
      ImGui::Text("p50 %.2f ms, p99 %.2f ms, max %.1f ms", t.swap_p50_ms, t.swap_p99_ms, t.swap_max_ms);
      double display_hz;
      bool locked;
      DisplayLockState(display_hz, locked);
      Row("Vblank");
      ImGui::Text("%.0f Hz, late p99 %.0f us; display %.2f Hz, %s", t.vblanks / 2.0, t.vblank_late_p99_us, display_hz,
                  locked ? "locked to it" : "not locked");
      bool lock = settings::Get().pacing.display_lock;
      Row("Lock to the display", "With a 60 or 120 Hz display, every frame stays on screen equally long.");
      if (Toggle("##lock", &lock)) PostToUIThread([lock] { SetDisplayLockSetting(lock); });
      ImGui::EndTable();
    }
    EndCard();
    BeginCard("Last frames (0-50 ms)");
    float values[240];
    const float width = ImGui::GetContentRegionAvail().x;
    size_t count = timing::RecentIntervals(timing::Event::kVdSwap, values, 240);
    ImGui::TextDisabled("The game's frames (VdSwap)");
    ImGui::PlotLines("##swaps", values, int(count), 0, nullptr, 0.0f, 50.0f, ImVec2(width, Px(70)));
    count = timing::RecentIntervals(timing::Event::kPresentGpuThread, values, 240);
    ImGui::TextDisabled("Presents");
    ImGui::PlotLines("##presents", values, int(count), 0, nullptr, 0.0f, 50.0f, ImVec2(width, Px(70)));
    EndCard();
  }

  Page page_ = kDisplay;
  std::unique_ptr<xe::ui::ImmediateTexture> preview_;
  std::chrono::steady_clock::time_point preview_time_;
};

SettingsPanel* g_panel = nullptr;  // lives until exit; registered with the drawer while open
std::atomic<bool> g_open = false;

// The panel is drawn with the game's frames: repainting it continuously added vsynced presents
// between them, which filled the swap chain's queue and held Dance Central 3 at 20 FPS. While the
// game presents nothing (loading), this keeps it alive at 10 frames a second; input repaints it
// too (ImGuiDrawer::OnInputWithoutContinuousRepaint).
void IdleRepaintThread() {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (g_open) PostToUIThread([] { SharedImGuiDrawer()->RequestPaintIfIdle(0.1); });
  }
}

void CenterCursor() {
  auto* window = static_cast<xe::ui::Win32Window*>(GameWindow());
  RECT client;
  if (!window || !GetClientRect(window->hwnd(), &client)) return;
  POINT center{client.right / 2, client.bottom / 2};
  ClientToScreen(window->hwnd(), &center);
  SetCursorPos(center.x, center.y);
}

void OpenSettingsPanel() {
  if (g_open || !g_panel) return;
  g_open = true;
  g_restart_error.clear();
  SharedImGuiDrawer()->AddDialog(g_panel);
  bool was_captured = input::IsCursorCaptured();
  input::SetGameInputEnabled(false);
  if (was_captured) CenterCursor();
  KLOG_I("settings panel opened");
}

// ANYX360_SETTINGS_TEST="<seconds>:<setting>=<value>,..." applies settings as the panel does
// (resolution, fullscreen, upscale, fxaa, fps, scale, stats, panel=1/0, tab=<name>, signin=<player>, signout=<player>, restart,
// kinect_source=<webcam|mouse|none>, kinect_players=<1|2>, kinect_mirror=<0|1>, kinect_restart, kinect_calibrate,
// kinect_window=<0|1>).
void RunSettingsTest(std::string spec) {
  auto start = std::chrono::steady_clock::now();
  for (size_t pos = 0; pos < spec.size();) {
    size_t end = std::min(spec.find(',', pos), spec.size());
    std::string item = spec.substr(pos, end - pos);
    pos = end + 1;
    size_t colon = item.find(':');
    if (colon == std::string::npos) continue;
    std::string action = item.substr(colon + 1);
    size_t equals = action.find('=');
    std::string key = action.substr(0, equals);
    std::string value = equals == std::string::npos ? "" : action.substr(equals + 1);
    std::this_thread::sleep_until(start + std::chrono::milliseconds(int64_t(std::atof(item.c_str()) * 1000)));
    PostToUIThread([key, value] {
      KLOG_I("settings test: %s=%s", key.c_str(), value.c_str());
      uint32_t n = uint32_t(std::atoi(value.c_str()));
      if (key == "resolution") SetResolution(n);
      else if (key == "fullscreen") SetFullscreenMode(n != 0);
      else if (key == "upscale") SetUpscaleSetting(value == "cas" ? Upscale::kCas : value == "bilinear" ? Upscale::kBilinear : Upscale::kFsr);
      else if (key == "fxaa") SetFxaaSetting(n != 0);
      else if (key == "fps" && !FixedFps()) SetFps(n >= 60 ? 60 : 30);
      else if (key == "vsync") SetVsyncSetting(n != 0);
      else if (key == "lock") SetDisplayLockSetting(n != 0);
      else if (key == "scale") SetRenderScale(std::clamp(n, 1u, 3u));
      else if (key == "stats") ShowStats(n != 0);
      else if (key == "panel") n ? OpenSettingsPanel() : CloseSettingsPanel();
      else if (key == "restart") RestartNow();
      else if (key == "tab") g_select_tab = value;
      else if (key == "signin") kernel::SignInUser(n - 1);
      else if (key == "signout") kernel::SignOutUser(n - 1);
      else if (key == "kinect_source") UpdateKinect([value](settings::KinectSettings& k) { k.source = value; });
      else if (key == "kinect_players") UpdateKinect([n](settings::KinectSettings& k) { k.players = std::clamp(n, 1u, 2u); });
      else if (key == "kinect_mirror") UpdateKinect([n](settings::KinectSettings& k) { k.mirror = n != 0; });
      else if (key == "kinect_restart") nui::RestartSource();
      else if (key == "kinect_calibrate") nui::Calibrate();
      else if (key == "kinect_window") SetKinectWindow(n != 0);
    });
  }
}

}  // namespace

void StartSettingsPanel() {
  g_panel = new SettingsPanel(SharedImGuiDrawer());  // registers itself
  SharedImGuiDrawer()->RemoveDialog(g_panel);
  std::thread(IdleRepaintThread).detach();
  // The Kinect window, if it was open when the game was last closed.
  if (settings::Get().kinect.enabled && settings::Get().kinect_window) SetKinectWindow(true);
  if (const char* spec = std::getenv("ANYX360_SETTINGS_TEST")) std::thread(RunSettingsTest, std::string(spec)).detach();
}

void ToggleSettingsPanel() {
  if (g_open)
    CloseSettingsPanel();
  else
    OpenSettingsPanel();
}

bool IsSettingsPanelOpen() { return g_open; }

void CloseSettingsPanel() {
  if (!g_open) return;
  g_open = false;
  SharedImGuiDrawer()->RemoveDialog(g_panel);
  input::SetGameInputEnabled(true);
  KLOG_I("settings panel closed");
}

}  // namespace x360::gpu
