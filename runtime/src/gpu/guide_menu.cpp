// The Guide (x360/gpu.h StartGuide): the console's menu on the Xbox button, over the game. The Guide
// button (XInputGetStateEx), Back and Start held together for a second (controllers whose Guide
// button Windows keeps), or Home on the keyboard open it, and close it again. It holds:
//   Resume       back to the game;
//   Achievements this game's, for the profile on the controller that opened it (achievements_ui);
//   Profiles     who is signed in on each slot: sign a player in (the sign-in picker) or out;
//   Settings     the F2 panel's pages;
//   Xbox Home    after a warning that progress not saved is lost, back to the dashboard
//                (ExitToDashboard).
// The game is paused behind it as on the console (kernel::SetSystemUiOpen(kGuide): XN_SYS_UIVISIBLE,
// XamIsUIActive, XN_SYS_NUIPAUSE in a Kinect title) and reads neutral controllers. Every controller
// drives it, and the keyboard (arrows, Enter, Esc). The look is the sign-in picker's.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gpu_internal.h"
#include "stats_overlay.h"
#include "ui_theme.h"
#include "third_party/imgui/imgui.h"
#include "x360/gpu.h"
#include "x360/input.h"
#include "x360/kernel.h"
#include "x360/settings.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>

namespace x360::gpu {

namespace {

using namespace theme;
using Clock = std::chrono::steady_clock;

// ---- text (the game's language: English or Spanish) ---------------------------------------------

struct Strings {
  const char* resume;
  const char* achievements;
  const char* profiles;
  const char* settings;
  const char* home;
  const char* player;          // %u
  const char* not_signed_in;
  const char* sign_in;
  const char* sign_out;
  const char* first_player;    // player 1 can't sign out here
  const char* back;
  const char* home_title;
  const char* home_text;
  const char* home_confirm;
  const char* cancel;
  const char* no_achievements;  // nobody signed in on that controller
  const char* settings_note;
  const char* hint_select;
  const char* hint_back;
  const char* paused;
};

constexpr Strings kEnglish = {
    "Resume",
    "Achievements",
    "Profiles",
    "Settings",
    "Xbox Home",
    "Player %u",
    "Not signed in",
    "Sign in",
    "Sign out",
    "Player 1 stays signed in",
    "Back",
    "Go to Xbox Home?",
    "The game closes. Progress you haven't saved is lost.",
    "Go to Xbox Home",
    "Cancel",
    "Sign in on this controller to see your achievements.",
    "The settings open over the game: close them with Esc to come back here.",
    "Select",
    "Back",
    "Paused",
};

constexpr Strings kSpanish = {
    "Reanudar",
    "Logros",
    "Perfiles",
    "Ajustes",
    "Inicio de Xbox",
    "Jugador %u",
    "Sin sesión iniciada",
    "Iniciar sesión",
    "Cerrar sesión",
    "El jugador 1 sigue con la sesión iniciada",
    "Atrás",
    "¿Ir al Inicio de Xbox?",
    "El juego se cierra. Se pierde el progreso que no hayas guardado.",
    "Ir al Inicio de Xbox",
    "Cancelar",
    "Inicia sesión con este mando para ver tus logros.",
    "Los ajustes se abren sobre el juego: ciérralos con Esc para volver aquí.",
    "Elegir",
    "Atrás",
    "En pausa",
};

const Strings& Tr() { return kernel::TitlePaths().language == 5 ? kSpanish : kEnglish; }

// The opening sound: one soft, short note (16-bit mono WAV in memory, made once).
const std::vector<uint8_t>& OpenSound() {
  static const std::vector<uint8_t> wav = [] {
    constexpr uint32_t kRate = 44100;
    const uint32_t samples = uint32_t(kRate * 0.22);
    std::vector<int16_t> pcm(samples);
    constexpr double kPi = 3.14159265358979;
    for (uint32_t i = 0; i < samples; ++i) {
      const double t = double(i) / kRate;
      const double envelope = std::min(1.0, t / 0.004) * std::exp(-t * 16.0);
      const double v = envelope * (std::sin(2 * kPi * 659.25 * t) + 0.3 * std::sin(2 * kPi * 1318.5 * t));
      pcm[i] = int16_t(std::clamp(v * 0.28, -1.0, 1.0) * 32767);
    }
    std::vector<uint8_t> out(44 + samples * 2);
    auto put32 = [&](size_t at, uint32_t x) { std::memcpy(&out[at], &x, 4); };
    auto put16 = [&](size_t at, uint16_t x) { std::memcpy(&out[at], &x, 2); };
    std::memcpy(&out[0], "RIFF", 4), put32(4, uint32_t(out.size() - 8)), std::memcpy(&out[8], "WAVEfmt ", 8);
    put32(16, 16), put16(20, 1), put16(22, 1), put32(24, kRate), put32(28, kRate * 2), put16(32, 2), put16(34, 16);
    std::memcpy(&out[36], "data", 4), put32(40, samples * 2);
    std::memcpy(&out[44], pcm.data(), samples * 2);
    return out;
  }();
  return wav;
}

// ---- the controllers --------------------------------------------------------------------------

enum class Press { kUp, kDown, kA, kB, kStart };
struct Event {
  int slot;
  Press press;
};

std::mutex g_events_mutex;
std::deque<Event> g_events;
std::atomic<bool> g_open{false};

void ToggleGuide(int slot);  // UI thread

// Every slot's pad (GetUiPad: the scripted input too), all the time: the Guide button, or Back and
// Start held a second, toggles the Guide; while it is open the d-pad, the stick, A, B and Start
// drive it (queued for the next paint; directions repeat while held). Each controller's
// connection, for the notifications.
void PadThread() {
  SetThreadDescription(GetCurrentThread(), L"Guide");
  struct State {
    uint16_t buttons = 0;
    int direction = -1;
    Clock::time_point repeat_at{}, combo_since{};
    bool combo_fired = false;
  } state[4];
  bool was_open = false;
  for (;;) {
    const bool open = g_open.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(open ? 8 : 16));
    const Clock::time_point now = Clock::now();
    bool queued = false;
    for (uint32_t slot = 0; slot < 4; ++slot) {
      input::Pad pad;
      input::GetUiPad(slot, pad);
      State& s = state[slot];
      const uint16_t pressed = pad.buttons & ~s.buttons;
      // The Guide button, or Back+Start held for a second (once per hold).
      constexpr uint16_t kCombo = XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START;
      bool guide = pressed & input::kGuideButton;
      if ((pad.buttons & kCombo) == kCombo) {
        if ((s.buttons & kCombo) != kCombo) s.combo_since = now, s.combo_fired = false;
        if (!s.combo_fired && now - s.combo_since >= std::chrono::milliseconds(1000)) guide = true, s.combo_fired = true;
      }
      if (guide) {
        KLOG_I("guide: the Guide button (player %u)", slot + 1);
        PostToUIThread([slot] { ToggleGuide(int(slot)); });
      }
      constexpr int16_t kDead = 12000;
      int direction = -1;
      if ((pad.buttons & XINPUT_GAMEPAD_DPAD_UP) || pad.ly > kDead) direction = int(Press::kUp);
      else if ((pad.buttons & XINPUT_GAMEPAD_DPAD_DOWN) || pad.ly < -kDead) direction = int(Press::kDown);
      std::vector<Press> presses;
      if (open && was_open) {  // on opening, what is held already doesn't count
        if (pressed & XINPUT_GAMEPAD_A) presses.push_back(Press::kA);
        if (pressed & XINPUT_GAMEPAD_B) presses.push_back(Press::kB);
        // Start alone (not the Back+Start hold) is A.
        if ((pressed & XINPUT_GAMEPAD_START) && !(pad.buttons & XINPUT_GAMEPAD_BACK)) presses.push_back(Press::kStart);
        if (direction != s.direction && direction >= 0) {
          presses.push_back(Press(direction));
          s.repeat_at = now + std::chrono::milliseconds(400);
        } else if (direction >= 0 && direction == s.direction && now >= s.repeat_at) {
          presses.push_back(Press(direction));
          s.repeat_at = now + std::chrono::milliseconds(120);
        }
      }
      s.buttons = pad.buttons;
      s.direction = direction;
      if (presses.empty()) continue;
      queued = true;
      std::lock_guard lock(g_events_mutex);
      for (Press press : presses) g_events.push_back({int(slot), press});
    }
    if (open && !was_open) {
      std::lock_guard lock(g_events_mutex);
      g_events.clear();
    }
    was_open = open;
    static uint32_t tick = 0;
    if (open && (queued || ++tick % 6 == 0)) {
      PostToUIThread([queued] {
        if (auto* drawer = SharedImGuiDrawer()) drawer->RequestPaintIfIdle(queued ? 0.0 : 0.05);
      });
    }
  }
}

// ---- notifications ----------------------------------------------------------------------------

// A controller connected or disconnected: a toast (Input thread).
void OnControllersChanged() {
  static std::atomic<uint32_t> known{~0u};
  const uint32_t now = input::ConnectedControllers();
  const uint32_t before = known.exchange(now);
  if (before == ~0u) return;  // the first report: the controllers at start
  const bool spanish = kernel::TitlePaths().language == 5;
  for (uint32_t i = 0; i < 4; ++i) {
    if (!((before ^ now) & (1u << i))) continue;
    const bool connected = now & (1u << i);
    char text[96];
    std::snprintf(text, sizeof(text), spanish ? (connected ? "Mando %u conectado" : "Mando %u desconectado")
                                              : (connected ? "Controller %u connected" : "Controller %u disconnected"),
                  i + 1);
    ShowNotification({spanish ? "Mandos" : "Controllers", text, char('1' + i)});
  }
}

// ---- the menu ---------------------------------------------------------------------------------

class GuideMenu : public xe::ui::ImGuiDialog {
 public:
  explicit GuideMenu(xe::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

  // UI thread.
  void Open(int slot) {
    slot_ = std::clamp(slot, 0, 3);
    page_ = Page::kMain;
    selected_ = 0;
    message_.clear();
    child_ = false;
  }

  // Something opened from it (the achievements list, the picker, the settings panel) is over it:
  // it isn't drawn and takes no input meanwhile.
  static bool ChildOpen() {
    return kernel::IsSystemUiOpen(kernel::SystemUi::kAchievements) ||
           kernel::IsSystemUiOpen(kernel::SystemUi::kSigninPicker) || IsSettingsPanelOpen();
  }

  void Close();

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::deque<Event> events;
    {
      std::lock_guard lock(g_events_mutex);
      events.swap(g_events);
    }
    const Clock::time_point now = Clock::now();
    if (ChildOpen()) {
      child_ = true;
      return;  // its presses are its own
    }
    if (child_) {
      // Back from it: the press that closed it isn't this menu's, and the keyboard is the menu's again
      // (the settings panel gives it back to the game when it closes).
      child_ = false;
      ignore_until_ = now + std::chrono::milliseconds(200);
      input::SetGameInputEnabled(false);
    }
    if (now < ignore_until_) events.clear();
    auto key = [&](ImGuiKey k, Press p) {
      if (ImGui::IsKeyPressed(k, true)) events.push_back({-1, p});
    };
    key(ImGuiKey_UpArrow, Press::kUp), key(ImGuiKey_DownArrow, Press::kDown);
    key(ImGuiKey_Enter, Press::kA), key(ImGuiKey_KeypadEnter, Press::kA);
    key(ImGuiKey_Escape, Press::kB), key(ImGuiKey_Backspace, Press::kB);
    for (const Event& e : events) {
      if (!g_open) return;
      Handle(e);
    }
    if (!g_open || ChildOpen()) return;
    Draw(io);
  }

 private:
  enum class Page { kMain, kProfiles, kHome };

  struct Item {
    std::string label, detail;
    bool enabled = true;
    std::function<void()> run;
  };

  std::vector<Item> Items() {
    std::vector<Item> items;
    switch (page_) {
      case Page::kMain:
        items.push_back({Tr().resume, "", true, [this] { Close(); }});
        items.push_back({Tr().achievements, "", true, [this] {
                           if (!kernel::IsUserSignedIn(uint32_t(slot_)) || !kernel::ShowAchievements(uint32_t(slot_)))
                             message_ = Tr().no_achievements;
                         }});
        items.push_back({Tr().profiles, "", true, [this] { Go(Page::kProfiles); }});
        items.push_back({Tr().settings, Tr().settings_note, true, [] { ToggleSettingsPanel(); }});
        items.push_back({Tr().home, "", true, [this] { Go(Page::kHome); }});
        break;
      case Page::kProfiles:
        for (uint32_t i = 0; i < 4; ++i) {
          char label[64];
          std::snprintf(label, sizeof(label), Tr().player, i + 1);
          const bool in = kernel::IsUserSignedIn(i);
          Item item{label, in ? kernel::UserName(i) : Tr().not_signed_in, i > 0};
          if (i == 0) item.detail += std::string("  \xC2\xB7  ") + Tr().first_player;
          if (i > 0 && in) {
            item.detail += std::string("  \xC2\xB7  ") + Tr().sign_out;
            item.run = [i] { kernel::SignOutUser(i); };  // with its toast
          } else if (i > 0) {
            item.detail += std::string("  \xC2\xB7  ") + Tr().sign_in;
            item.run = [] { kernel::ShowSigninPicker(1); };
          }
          items.push_back(std::move(item));
        }
        items.push_back({Tr().back, "", true, [this] { Go(Page::kMain); }});
        break;
      case Page::kHome:
        items.push_back({Tr().home_confirm, "", true, [] {
                           KLOG_I("guide: Xbox Home");
                           // Off the UI thread: it waits for the dashboard before the process ends.
                           std::thread([] { ExitToDashboard(); }).detach();
                         }});
        items.push_back({Tr().cancel, "", true, [this] { Go(Page::kMain, 4); }});
        break;
    }
    return items;
  }

  void Go(Page page, int selected = 0) {
    page_ = page;
    selected_ = selected;
    message_.clear();
  }

  void Handle(const Event& e) {
    std::vector<Item> items = Items();
    switch (e.press) {
      case Press::kUp: selected_ = std::max(0, selected_ - 1); break;
      case Press::kDown: selected_ = std::min(int(items.size()) - 1, selected_ + 1); break;
      case Press::kA:
      case Press::kStart:
        if (e.slot >= 0 && page_ == Page::kMain) slot_ = e.slot;  // the achievements of whoever chooses
        Choose(items, selected_);
        break;
      case Press::kB:
        if (page_ == Page::kMain) Close();
        else Go(Page::kMain, page_ == Page::kProfiles ? 2 : 4);
        break;
    }
  }

  void Choose(std::vector<Item>& items, int index) {
    if (index < 0 || index >= int(items.size())) return;
    message_.clear();
    Item& item = items[size_t(index)];
    if (item.enabled && item.run) item.run();
  }

  // ---- drawing --------------------------------------------------------------------------------

  float S(float dips) const { return theme::Px(dips) * scale_; }

  void Draw(ImGuiIO& io) {
    const ImVec2 display = io.DisplaySize;
    scale_ = std::clamp(display.y / (720.0f * theme::Scale()), 1.0f, 3.0f);
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display, color::Rgb(0x000000, 150));
    const float width = std::min(S(460), display.x - S(32));
    ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, color::Vec(color::kPanel));
    ImGui::PushStyleColor(ImGuiCol_Border, color::Vec(color::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(20)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##anyx360_guide", nullptr, flags)) {
      ImGui::SetWindowFontScale(scale_);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(8)));
      DrawHeader();
      if (page_ == Page::kHome) {
        ImGui::PushFont(Title());
        ImGui::TextUnformatted(Tr().home_title);
        ImGui::PopFont();
        ImGui::TextWrapped("%s", Tr().home_text);
        ImGui::Dummy(ImVec2(0, S(4)));
      }
      DrawItems();
      if (!message_.empty()) ImGui::TextColored(color::Vec(color::kAmber), "%s", message_.c_str());
      DrawHints();
      ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
  }

  // The player's gamerpic (their initial), gamertag and the game, as the console's Guide shows them.
  void DrawHeader() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float r = S(22);
    const ImVec2 pic(at.x + r, at.y + r);
    const std::string name = kernel::IsUserSignedIn(uint32_t(slot_)) ? kernel::UserName(uint32_t(slot_)) : "";
    draw->AddCircleFilled(pic, r, color::kGreen, 40);
    const char initial[2] = {name.empty() ? '?' : char(std::toupper(uint8_t(name[0]))), 0};
    ImGui::PushFont(Title());
    const ImVec2 size = ImGui::CalcTextSize(initial);
    draw->AddText(ImVec2(pic.x - size.x / 2, pic.y - size.y / 2), color::kText, initial);
    const float text_x = at.x + 2 * r + S(14);
    char player[48];
    std::snprintf(player, sizeof(player), Tr().player, unsigned(slot_ + 1));
    draw->AddText(ImVec2(text_x, at.y), color::kText, name.empty() ? player : name.c_str());
    const float line = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    ImGui::PushFont(Small());
    std::string under = kernel::TitlePaths().title_name;
    under += std::string("  \xC2\xB7  ") + Tr().paused;
    draw->AddText(ImVec2(text_x, at.y + line + S(4)), color::kTextSecondary, under.c_str());
    // The time, top right, as the console's Guide has it.
    SYSTEMTIME local;
    GetLocalTime(&local);
    char clock[16];
    std::snprintf(clock, sizeof(clock), "%02u:%02u", local.wHour, local.wMinute);
    const float clock_w = ImGui::CalcTextSize(clock).x;
    draw->AddText(ImVec2(at.x + ImGui::GetContentRegionAvail().x - clock_w, at.y + S(2)), color::kTextSecondary, clock);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 2 * r + S(8)));
    draw->AddLine(ImVec2(at.x, at.y + 2 * r + S(6)), ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y + 2 * r + S(6)),
                  color::kBorder, S(1));
  }

  void DrawItems() {
    std::vector<Item> items = Items();
    selected_ = std::clamp(selected_, 0, int(items.size()) - 1);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < int(items.size()); ++i) {
      const Item& item = items[size_t(i)];
      const bool two_lines = !item.detail.empty() && (page_ == Page::kProfiles || i == selected_);
      const float row_h = S(two_lines ? 56 : 44);
      ImGui::PushID(i);
      const ImVec2 at = ImGui::GetCursorScreenPos();
      const bool clicked = ImGui::InvisibleButton("##item", ImVec2(width, row_h));
      if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x + ImGui::GetIO().MouseDelta.y != 0) selected_ = i;
      const bool selected = i == selected_;
      const ImVec2 end(at.x + width, at.y + row_h);
      draw->AddRectFilled(at, end, selected ? color::kGreen : color::kPanelHigh, S(4));
      if (selected) draw->AddRect(at, end, color::Rgb(0xFFFFFF), S(4), 0, S(2));
      ImGui::PushFont(Heading());
      const float text_h = ImGui::GetTextLineHeight();
      const ImU32 text_color = item.enabled ? color::kText : color::kTextSecondary;
      if (two_lines) {
        draw->AddText(ImVec2(at.x + S(16), at.y + S(8)), text_color, item.label.c_str());
        ImGui::PopFont();
        ImGui::PushFont(Small());
        draw->AddText(ImVec2(at.x + S(16), at.y + S(10) + text_h), selected ? color::Rgb(0xDDEEDD) : color::kTextSecondary,
                      item.detail.c_str());
      } else {
        draw->AddText(ImVec2(at.x + S(16), at.y + (row_h - text_h) / 2), text_color, item.label.c_str());
      }
      ImGui::PopFont();
      ImGui::PopID();
      if (clicked) {
        selected_ = i;
        Choose(items, i);
        return;
      }
    }
  }

  void DrawHints() {
    ImGui::Dummy(ImVec2(0, S(6)));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGui::PushFont(Small());
    const float r = S(9);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    float x = start.x;
    const float cy = start.y + r;
    auto hint = [&](const char* button, ImU32 fill, const char* text) {
      draw->AddCircleFilled(ImVec2(x + r, cy), r, fill, 24);
      const ImVec2 size = ImGui::CalcTextSize(button);
      draw->AddText(ImVec2(x + r - size.x / 2, cy - size.y / 2), color::kText, button);
      x += 2 * r + S(6);
      draw->AddText(ImVec2(x, cy - ImGui::GetTextLineHeight() / 2), color::kTextSecondary, text);
      x += ImGui::CalcTextSize(text).x + S(18);
    };
    hint("A", color::Rgb(0x5DB33A), Tr().hint_select);
    hint("B", color::Rgb(0xD9433B), Tr().hint_back);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 2 * r));
  }

  int slot_ = 0;  // the player whose Guide it is
  Page page_ = Page::kMain;
  int selected_ = 0;
  std::string message_;
  bool child_ = false;
  Clock::time_point ignore_until_{};
  float scale_ = 1;
};

GuideMenu* g_menu = nullptr;  // lives until exit; registered with the drawer while open

void GuideMenu::Close() {
  if (!g_open.exchange(false)) return;
  imgui_drawer()->RemoveDialog(this);
  if (!IsSettingsPanelOpen()) input::SetGameInputEnabled(true);
  KLOG_I("guide: closed");
  kernel::SetSystemUiOpen(kernel::SystemUi::kGuide, false);
}

void ToggleGuide(int slot) {
  xe::ui::ImGuiDrawer* drawer = SharedImGuiDrawer();
  if (!drawer) return;
  if (g_open) {
    if (!GuideMenu::ChildOpen()) g_menu->Close();
    return;
  }
  // A system UI the title opened (its sign-in or achievements), or the F2 panel, is in front: the
  // Guide waits for it.
  if (GuideMenu::ChildOpen()) return;
  if (!g_menu) {
    g_menu = new GuideMenu(drawer);  // registers itself
    drawer->RemoveDialog(g_menu);
  }
  g_menu->Open(slot);
  KLOG_I("guide: opened by player %d", slot + 1);
  kernel::SetSystemUiOpen(kernel::SystemUi::kGuide, true);
  input::SetGameInputEnabled(false);  // the keyboard is the Guide's; the cursor is free
  g_open = true;
  drawer->AddDialog(g_menu);
  drawer->RequestPaintIfIdle(0);
  if (settings::Get().guide_sound)
    PlaySoundW(reinterpret_cast<LPCWSTR>(OpenSound().data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

}  // namespace

void StartGuide() {
  std::thread(PadThread).detach();
  input::OnDevicesChanged(OnControllersChanged);
  OnControllersChanged();  // what is connected now
}

void GuideKey() {
  // Home: the keyboard's player's Guide (player 1 when no slot has the keyboard).
  int slot = 0;
  for (uint32_t i = 0; i < 4; ++i)
    if (input::GetSlotDevice(i).kind == input::DeviceKind::kKeyboardMouse) {
      slot = int(i);
      break;
    }
  ToggleGuide(slot);
}

bool IsGuideOpen() { return g_open; }

}  // namespace x360::gpu
