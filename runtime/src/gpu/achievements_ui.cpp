// The system's achievements list over the game (x360/gpu.h OpenAchievementsUi), shown when a title
// asks for it (XamShowAchievementsUI: Minecraft's main menu): the player's
// achievements of this game, unlocked first, with their pictures, gamerscore and dates. The game is
// paused behind it and reads neutral controllers, as with the sign-in picker; the d-pad or the stick
// scrolls, B (or A, Start, Esc) closes it (kernel::CloseAchievementsUi). It grows with the window.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gpu_internal.h"
#include "stats_overlay.h"
#include "ui_theme.h"
#include "third_party/imgui/imgui.h"
#include "third_party/stb/stb_image.h"  // its implementation is compiled into Xenia's ImGui drawer
#include "x360/gpu.h"
#include "x360/input.h"
#include "x360/kernel.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

namespace x360::gpu {

namespace {

using namespace theme;
using Clock = std::chrono::steady_clock;

struct Strings {
  const char* title;
  const char* summary;  // %s gamertag, %u unlocked, %u achievements, %u earned, %u total
  const char* none;
  const char* secret;
  const char* secret_hint;
  const char* unlocked_on;  // %s date
  const char* hint_scroll;
  const char* hint_close;
  const char* months[12];
};
constexpr Strings kEnglish = {"Achievements", "%s: %u of %u unlocked \xC2\xB7 %u of %u G", "This game has no achievements.",
                              "Secret achievement", "Keep playing to unlock it.", "Unlocked on %s", "Scroll", "Close",
                              {"January", "February", "March", "April", "May", "June", "July", "August", "September",
                               "October", "November", "December"}};
constexpr Strings kSpanish = {"Logros", "%s: %u de %u desbloqueados \xC2\xB7 %u de %u G", "Este juego no tiene logros.",
                              "Logro secreto", "Sigue jugando para desbloquearlo.", "Desbloqueado el %s", "Desplazar",
                              "Cerrar",
                              {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre",
                               "octubre", "noviembre", "diciembre"}};
const Strings& Tr() { return kernel::TitlePaths().language == 5 ? kSpanish : kEnglish; }

// "29 October 2026" / "29 de octubre de 2026" from an ISO 8601 UTC time, in the computer's time zone.
std::string LongDate(const std::string& iso) {
  SYSTEMTIME utc{}, local{};
  if (sscanf_s(iso.c_str(), "%4hu-%2hu-%2huT%2hu:%2hu:%2hu", &utc.wYear, &utc.wMonth, &utc.wDay, &utc.wHour,
               &utc.wMinute, &utc.wSecond) != 6 || utc.wMonth < 1 || utc.wMonth > 12)
    return {};
  if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) local = utc;
  char text[64];
  if (&Tr() == &kSpanish)
    std::snprintf(text, sizeof(text), "%u de %s de %u", local.wDay, Tr().months[local.wMonth - 1], local.wYear);
  else
    std::snprintf(text, sizeof(text), "%u %s %u", local.wDay, Tr().months[local.wMonth - 1], local.wYear);
  return text;
}

enum class Press { kUp, kDown, kClose };
std::mutex g_events_mutex;
std::deque<Press> g_events;
std::atomic<bool> g_open{false};

// Every controller (GetUiPad: the scripted input too): up and down repeat while held; B, A or Start
// close. Presses are queued for the next paint, which is asked for at once.
void PadThread() {
  SetThreadDescription(GetCurrentThread(), L"Achievements list");
  uint16_t held[4] = {};
  int direction[4] = {-1, -1, -1, -1};
  Clock::time_point repeat_at[4]{};
  bool was_open = false;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(was_open ? 8 : 50));
    const bool open = g_open.load();
    const Clock::time_point now = Clock::now();
    bool any = false;
    for (uint32_t slot = 0; slot < 4; ++slot) {
      input::Pad pad;
      input::GetUiPad(slot, pad);
      constexpr int16_t kDead = 12000;
      int d = -1;
      if ((pad.buttons & XINPUT_GAMEPAD_DPAD_UP) || pad.ly > kDead) d = int(Press::kUp);
      else if ((pad.buttons & XINPUT_GAMEPAD_DPAD_DOWN) || pad.ly < -kDead) d = int(Press::kDown);
      std::vector<Press> presses;
      if (open && was_open) {  // what is held on opening doesn't count
        if ((pad.buttons & ~held[slot]) & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START))
          presses.push_back(Press::kClose);
        if (d >= 0 && d != direction[slot]) {
          presses.push_back(Press(d));
          repeat_at[slot] = now + std::chrono::milliseconds(400);
        } else if (d >= 0 && now >= repeat_at[slot]) {
          presses.push_back(Press(d));
          repeat_at[slot] = now + std::chrono::milliseconds(110);
        }
      }
      held[slot] = pad.buttons;
      direction[slot] = d;
      if (presses.empty()) continue;
      any = true;
      std::lock_guard lock(g_events_mutex);
      g_events.insert(g_events.end(), presses.begin(), presses.end());
    }
    if (open && !was_open) {
      std::lock_guard lock(g_events_mutex);
      g_events.clear();
    }
    was_open = open;
    static uint32_t tick = 0;
    if (open && (any || ++tick % 6 == 0)) {
      PostToUIThread([any] {
        if (auto* drawer = SharedImGuiDrawer()) drawer->RequestPaintIfIdle(any ? 0.0 : 0.05);
      });
    }
  }
}

class AchievementsDialog : public xe::ui::ImGuiDialog {
 public:
  explicit AchievementsDialog(xe::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

  // UI thread.
  void Open(AchievementsView view) {
    view_ = std::move(view);
    pictures_.clear();
    pictures_.resize(view_.items.size());
    decoded_.clear();
    decoded_.resize(view_.items.size());
    for (size_t i = 0; i < view_.items.size(); ++i) {
      const std::vector<uint8_t>& png = view_.items[i].png;
      int w = 0, h = 0, channels = 0;
      if (png.empty()) continue;
      if (stbi_uc* pixels = stbi_load_from_memory(png.data(), int(png.size()), &w, &h, &channels, 4)) {
        decoded_[i] = {uint32_t(w), uint32_t(h), std::vector<uint8_t>(pixels, pixels + size_t(w) * h * 4)};
        stbi_image_free(pixels);
      }
    }
    first_ = 0;
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::deque<Press> events;
    {
      std::lock_guard lock(g_events_mutex);
      events.swap(g_events);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) events.push_back(Press::kUp);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) events.push_back(Press::kDown);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter)) events.push_back(Press::kClose);
    const float wheel = io.MouseWheel;
    if (wheel > 0) events.push_back(Press::kUp);
    if (wheel < 0) events.push_back(Press::kDown);
    for (Press p : events) {
      if (p == Press::kClose) return Finish();
      first_ += p == Press::kDown ? 1 : -1;
    }
    Draw(io);
  }

 private:
  struct Decoded {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
  };

  void Finish() {
    g_open = false;
    input::SetGameInputEnabled(true);
    imgui_drawer()->RemoveDialog(this);
    for (auto& p : pictures_) p.reset();
    KLOG_I("achievements: the list closed");
    kernel::CloseAchievementsUi();
  }

  xe::ui::ImmediateTexture* Picture(size_t i) {
    if (!pictures_[i] && !decoded_[i].rgba.empty()) {
      if (xe::ui::ImmediateDrawer* immediate = SharedImmediateDrawer())
        pictures_[i] = immediate->CreateTexture(decoded_[i].width, decoded_[i].height,
                                                xe::ui::ImmediateTextureFilter::kLinear, false, decoded_[i].rgba.data());
    }
    return pictures_[i].get();
  }

  void Draw(ImGuiIO& io) {
    const ImVec2 display = io.DisplaySize;
    const float scale = std::clamp(display.y / (720.0f * Scale()), 1.0f, 3.0f);
    auto S = [&](float dips) { return Px(dips) * scale; };
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display, color::Rgb(0x000000, 150));
    const float width = std::min(S(680), display.x - S(32));
    const float row_h = S(64);
    const int visible = std::max(2, int((display.y * 0.62f) / row_h));
    const int count = int(view_.items.size());
    first_ = std::clamp(first_, 0, std::max(0, count - visible));

    ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, color::Vec(color::kPanel));
    ImGui::PushStyleColor(ImGuiCol_Border, color::Vec(color::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(20)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##anyx360_achievements", nullptr, flags)) {
      ImGui::SetWindowFontScale(scale);
      ImDrawList* draw = ImGui::GetWindowDrawList();
      const ImVec2 corner = ImGui::GetCursorScreenPos();
      draw->AddRectFilled(ImVec2(corner.x - S(24), corner.y - S(20)), ImVec2(corner.x - S(20), corner.y + S(30)),
                          color::kGreen);
      ImGui::PushFont(Title());
      ImGui::TextUnformatted(Tr().title);
      ImGui::PopFont();
      ImGui::TextUnformatted(view_.title.c_str());
      uint32_t unlocked = 0, earned = 0, total = 0;
      for (const auto& item : view_.items) {
        total += item.gamerscore;
        if (item.unlocked) ++unlocked, earned += item.gamerscore;
      }
      char summary[256];
      if (view_.items.empty())
        std::snprintf(summary, sizeof(summary), "%s", Tr().none);
      else
        std::snprintf(summary, sizeof(summary), Tr().summary, view_.gamertag.c_str(), unlocked,
                      unsigned(view_.items.size()), earned, total);
      ImGui::TextColored(color::Vec(color::kTextSecondary), "%s", summary);
      // The share of the gamerscore earned.
      const float full = ImGui::GetContentRegionAvail().x;
      ImVec2 at = ImGui::GetCursorScreenPos();
      draw->AddRectFilled(at, ImVec2(at.x + full, at.y + S(4)), color::Rgb(0x30353B));
      if (total) draw->AddRectFilled(at, ImVec2(at.x + full * float(earned) / float(total), at.y + S(4)), color::kGreen);
      ImGui::Dummy(ImVec2(full, S(10)));

      ImFont* heading = Heading();
      ImFont* small_font = Small();
      const float heading_size = heading->FontSize * scale, small_size = small_font->FontSize * scale;
      const float list_top = ImGui::GetCursorScreenPos().y;
      for (int i = first_; i < std::min(count, first_ + visible); ++i) {
        const AchievementsView::Item& item = view_.items[size_t(i)];
        at = ImGui::GetCursorScreenPos();
        const ImVec2 end(at.x + full - S(10), at.y + row_h - S(6));
        draw->AddRectFilled(at, end, item.unlocked ? color::kPanelHigh : color::Rgb(0x1E2125), S(4));
        const float icon = row_h - S(18);
        const ImVec2 p0(at.x + S(8), at.y + S(6)), p1(p0.x + icon, p0.y + icon);
        xe::ui::ImmediateTexture* picture = item.secret ? nullptr : Picture(size_t(i));
        if (picture)
          draw->AddImage(reinterpret_cast<ImTextureID>(picture), p0, p1, ImVec2(0, 0), ImVec2(1, 1),
                         item.unlocked ? IM_COL32_WHITE : color::Rgb(0xFFFFFF, 70));
        else
          draw->AddRectFilled(p0, p1, color::Rgb(0x2A2E33), S(3));
        if (!item.unlocked) {  // a padlock: a shackle over a body
          const ImVec2 c((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
          const float u = icon / 10;
          draw->AddRect(ImVec2(c.x - 1.4f * u, c.y - 2.6f * u), ImVec2(c.x + 1.4f * u, c.y + 0.6f * u),
                        color::Rgb(0xD0D4D9), u * 1.4f, 0, u * 0.5f);
          draw->AddRectFilled(ImVec2(c.x - 2.2f * u, c.y - 0.4f * u), ImVec2(c.x + 2.2f * u, c.y + 2.6f * u),
                              color::Rgb(0xD0D4D9), u * 0.4f);
        }
        const float tx = p1.x + S(12);
        const std::string name = item.secret ? Tr().secret : item.name;
        const std::string description = item.secret ? Tr().secret_hint : item.description;
        draw->AddText(heading, heading_size, ImVec2(tx, at.y + S(8)), item.unlocked ? color::kText : color::kTextSecondary,
                      name.c_str());
        const float right = end.x - S(12);
        char score[32];
        std::snprintf(score, sizeof(score), "%u G", item.gamerscore);
        const float score_w = heading->CalcTextSizeA(heading_size, FLT_MAX, 0, score).x;
        draw->AddText(heading, heading_size, ImVec2(right - score_w, at.y + S(8)),
                      item.unlocked ? color::kGreenBright : color::kTextSecondary, score);
        float date_w = 0;
        if (item.unlocked && !item.date.empty()) {
          char when[96];
          std::snprintf(when, sizeof(when), Tr().unlocked_on, LongDate(item.date).c_str());
          date_w = small_font->CalcTextSizeA(small_size, FLT_MAX, 0, when).x;
          draw->AddText(small_font, small_size, ImVec2(right - date_w, at.y + S(32)), color::kTextSecondary, when);
        }
        draw->PushClipRect(ImVec2(tx, at.y), ImVec2(right - date_w - S(12), end.y), true);
        draw->AddText(small_font, small_size, ImVec2(tx, at.y + S(32)), color::kTextSecondary, description.c_str());
        draw->PopClipRect();
        ImGui::Dummy(ImVec2(full, row_h));
      }
      // Where the list is: a bar at the right when it doesn't fit.
      if (count > visible) {
        const float top = list_top;
        const float h = ImGui::GetCursorScreenPos().y - list_top - S(12);
        const float x = corner.x + full - S(4);
        draw->AddRectFilled(ImVec2(x, top), ImVec2(x + S(4), top + h), color::Rgb(0x2A2E33), S(2));
        const float thumb = std::max(S(20), h * visible / count);
        const float y = top + (h - thumb) * first_ / std::max(1, count - visible);
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + S(4), y + thumb), color::Rgb(0x8B9096), S(2));
      }
      // The buttons' hints.
      ImGui::Dummy(ImVec2(0, S(4)));
      at = ImGui::GetCursorScreenPos();
      const float r = S(9), cy = at.y + r;
      float x = at.x;
      ImGui::PushFont(Small());
      auto hint = [&](const char* button, ImU32 fill, const char* text) {
        draw->AddCircleFilled(ImVec2(x + r, cy), r, fill, 24);
        const ImVec2 size = ImGui::CalcTextSize(button);
        draw->AddText(ImVec2(x + r - size.x / 2, cy - size.y / 2), color::kText, button);
        x += 2 * r + S(6);
        draw->AddText(ImVec2(x, cy - ImGui::GetTextLineHeight() / 2), color::kTextSecondary, text);
        x += ImGui::CalcTextSize(text).x + S(18);
      };
      if (count > visible) hint("\xE2\x86\x91",color::Rgb(0x5A5F66), Tr().hint_scroll);
      hint("B", color::Rgb(0xD9433B), Tr().hint_close);
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0, 2 * r));
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
  }

  AchievementsView view_;
  std::vector<Decoded> decoded_;
  std::vector<std::unique_ptr<xe::ui::ImmediateTexture>> pictures_;
  int first_ = 0;  // the first row shown
};

AchievementsDialog* g_dialog = nullptr;  // lives until exit; registered with the drawer while open

}  // namespace

bool OpenAchievementsUi(AchievementsView view) {
  if (!SharedImGuiDrawer() || !GameWindow()) return false;
  static std::once_flag started;
  std::call_once(started, [] { std::thread(PadThread).detach(); });
  PostToUIThread([view = std::move(view)]() mutable {
    xe::ui::ImGuiDrawer* drawer = SharedImGuiDrawer();
    if (!g_dialog) {
      g_dialog = new AchievementsDialog(drawer);  // registers itself
      drawer->RemoveDialog(g_dialog);
    }
    KLOG_I("achievements: the list opens (%zu)", view.items.size());
    g_dialog->Open(std::move(view));
    input::SetGameInputEnabled(false);  // the keyboard is the list's; the cursor is free
    g_open = true;
    drawer->AddDialog(g_dialog);
    drawer->RequestPaintIfIdle(0);
  });
  return true;
}

}  // namespace x360::gpu
