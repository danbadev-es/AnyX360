// The sign-in picker (x360/gpu.h OpenSigninPicker): the console's sign-in screen over the game when
// a title asks for its sign-in UI. It lists the profiles that aren't signed in (x360/profiles.h),
// then "Guest" (kernel::SignInGuest: "Daniel (1)", nothing of theirs kept), "Create profile" (a
// gamertag typed on an on-screen keyboard, or the physical one) and Cancel.
// Every controller drives it, each from its own slot: the profile a controller chooses signs in on
// that controller's slot (or, chosen by player 1 or the keyboard, on the first free slot with a
// controller). The game is paused behind it (xam.cpp's XamShowSigninUI) and reads neutral pads.
//
// The controllers are read on a thread of their own (presses are queued for the next paint: a short
// press between two paints still counts); the keyboard through ImGui. The look is ui_theme's.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "gamerpic.h"
#include "gpu_internal.h"
#include "stats_overlay.h"
#include "ui_theme.h"
#include "third_party/imgui/imgui.h"
#include "x360/gpu.h"
#include "x360/input.h"
#include "x360/kernel.h"
#include "x360/profiles.h"
#include "x360/settings.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

namespace x360::gpu {


namespace {

using namespace theme;
using Clock = std::chrono::steady_clock;

// ---- text (the game's language: English or Spanish) ---------------------------------------------

struct Strings {
  const char* title;
  const char* choose;
  const char* choose_more;  // %u players
  const char* signed_in_as;  // %u player, %s gamertag
  const char* create;
  const char* guest;
  const char* guest_detail;  // %s: the guest's gamertag
  const char* cancel;
  const char* done;
  const char* none;
  const char* none_no_guest;  // the title disallows guests
  const char* new_title;
  const char* new_hint;
  const char* space;
  const char* shift;
  const char* erase;
  const char* error_empty;
  const char* error_taken;
  const char* error_write;
  const char* error_no_slot;
  const char* hint_select;
  const char* hint_back;
  const char* hint_space;
  const char* hint_shift;
  const char* hint_done;
};

constexpr Strings kEnglish = {
    "Sign in",
    "Choose your profile with the controller you'll play with.",
    "Choose your profiles with the controllers you'll play with (%u more players).",
    "Player %u signed in as %s.",
    "Create profile",
    "Guest",
    "as %s, nothing is kept",
    "Cancel",
    "Done",
    "Every profile is signed in. Create one for this player, or play as a guest.",
    "Every profile is signed in. Create one for this player.",
    "New profile",
    "Your gamertag: letters, digits and spaces, up to 15.",
    "Space",
    "Shift",
    "Delete",
    "Type a gamertag first.",
    "Another profile has that gamertag.",
    "The profile could not be saved.",
    "Connect a controller for the player who signs in.",
    "Select",
    "Back",
    "Space",
    "Shift",
    "Done",
};

constexpr Strings kSpanish = {
    "Iniciar sesión",
    "Elige tu perfil con el mando con el que vas a jugar.",
    "Elegid vuestros perfiles con los mandos con los que vais a jugar (%u jugadores más).",
    "El jugador %u ha iniciado sesión como %s.",
    "Crear perfil",
    "Invitado",
    "como %s, no se guarda nada",
    "Cancelar",
    "Hecho",
    "Todos los perfiles tienen la sesión iniciada. Crea uno para este jugador o juega como invitado.",
    "Todos los perfiles tienen la sesión iniciada. Crea uno para este jugador.",
    "Perfil nuevo",
    "Tu gamertag: letras, números y espacios, hasta 15.",
    "Espacio",
    "Mayús",
    "Borrar",
    "Escribe antes un gamertag.",
    "Otro perfil tiene ese gamertag.",
    "No se ha podido guardar el perfil.",
    "Conecta un mando para el jugador que inicia sesión.",
    "Elegir",
    "Atrás",
    "Espacio",
    "Mayús",
    "Hecho",
};

const Strings& Tr() { return kernel::TitlePaths().language == 5 ? kSpanish : kEnglish; }

// ---- the controllers --------------------------------------------------------------------------

enum class Press { kUp, kDown, kLeft, kRight, kA, kB, kX, kY, kStart };
struct Event {
  int slot;  // the controller's player slot; -1: the keyboard
  Press press;
};

std::mutex g_events_mutex;
std::deque<Event> g_events;
std::atomic<bool> g_open{false};

// Reads every slot's pad (GetUiPad: the scripted input too) and queues presses; directions repeat
// while held, like the launcher's.
void PadThread() {
  SetThreadDescription(GetCurrentThread(), L"Sign-in picker");
  struct State {
    uint16_t buttons = 0;
    int direction = -1;
    Clock::time_point repeat_at{};
  } state[4];
  bool was_open = false;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(was_open ? 8 : 50));
    const bool open = g_open.load();
    const Clock::time_point now = Clock::now();
    for (uint32_t slot = 0; slot < 4; ++slot) {
      input::Pad pad;
      input::GetUiPad(slot, pad);
      constexpr int16_t kDead = 12000;
      int direction = -1;
      if ((pad.buttons & XINPUT_GAMEPAD_DPAD_UP) || pad.ly > kDead) direction = int(Press::kUp);
      else if ((pad.buttons & XINPUT_GAMEPAD_DPAD_DOWN) || pad.ly < -kDead) direction = int(Press::kDown);
      else if ((pad.buttons & XINPUT_GAMEPAD_DPAD_LEFT) || pad.lx < -kDead) direction = int(Press::kLeft);
      else if ((pad.buttons & XINPUT_GAMEPAD_DPAD_RIGHT) || pad.lx > kDead) direction = int(Press::kRight);
      State& s = state[slot];
      std::vector<Press> presses;
      if (open && was_open) {  // on opening, what is held already doesn't count
        const uint16_t pressed = pad.buttons & ~s.buttons;
        const std::pair<uint16_t, Press> buttons[] = {{XINPUT_GAMEPAD_A, Press::kA}, {XINPUT_GAMEPAD_B, Press::kB},
                                                      {XINPUT_GAMEPAD_X, Press::kX}, {XINPUT_GAMEPAD_Y, Press::kY},
                                                      {XINPUT_GAMEPAD_START, Press::kStart}};
        for (const auto& [bit, press] : buttons)
          if (pressed & bit) presses.push_back(press);
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
      std::lock_guard lock(g_events_mutex);
      for (Press press : presses) g_events.push_back({int(slot), press});
    }
    if (open && !was_open) {
      std::lock_guard lock(g_events_mutex);
      g_events.clear();
    }
    was_open = open;
    // A press is drawn at once, and the picker 20 times a second while the game presents nothing.
    static uint32_t tick = 0;
    bool pending;
    {
      std::lock_guard lock(g_events_mutex);
      pending = !g_events.empty();
    }
    if (open && (pending || ++tick % 6 == 0)) {
      PostToUIThread([pending] {
        if (auto* drawer = SharedImGuiDrawer()) drawer->RequestPaintIfIdle(pending ? 0.0 : 0.05);
      });
    }
  }
}

// ---- the picker -------------------------------------------------------------------------------

// The on-screen keyboard: rows of keys, each `units` wide (a key is 1).
struct Key {
  enum Kind { kChar, kShift, kSpace, kErase, kDone } kind = kChar;
  char ch = 0;
  float units = 1;
};

std::vector<std::vector<Key>> KeyboardRows() {
  std::vector<std::vector<Key>> rows;
  for (const char* row : {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"}) {
    rows.emplace_back();
    for (const char* c = row; *c; ++c) rows.back().push_back({Key::kChar, *c});
  }
  rows.push_back({{Key::kShift, 0, 2}, {Key::kSpace, 0, 4}, {Key::kErase, 0, 2}, {Key::kDone, 0, 2}});
  return rows;
}

class SigninPicker : public xe::ui::ImGuiDialog {
 public:
  explicit SigninPicker(xe::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer), rows_(KeyboardRows()) {}

  // UI thread.
  void Open(uint32_t count, bool allow_guest) {
    remaining_ = count;
    allow_guest_ = allow_guest;
    page_ = Page::kList;
    selected_ = 0;
    message_.clear();
    note_.clear();
    Refresh();
    KLOG_I("sign-in picker: open for %u player(s), %zu profile(s) to choose from%s", count, profiles_.size(),
           allow_guest ? ", or a guest" : "");
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::deque<Event> events;
    {
      std::lock_guard lock(g_events_mutex);
      events.swap(g_events);
    }
    // The keyboard: arrows, Enter, Esc, and on the keyboard page the letters typed.
    auto key = [&](ImGuiKey k, Press p) {
      if (ImGui::IsKeyPressed(k, true)) events.push_back({-1, p});
    };
    key(ImGuiKey_UpArrow, Press::kUp), key(ImGuiKey_DownArrow, Press::kDown);
    key(ImGuiKey_LeftArrow, Press::kLeft), key(ImGuiKey_RightArrow, Press::kRight);
    key(ImGuiKey_Enter, page_ == Page::kKeyboard ? Press::kStart : Press::kA);
    key(ImGuiKey_KeypadEnter, page_ == Page::kKeyboard ? Press::kStart : Press::kA);
    key(ImGuiKey_Escape, Press::kB);
    if (page_ == Page::kKeyboard) {
      if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) Erase();
      for (ImWchar c : io.InputQueueCharacters)
        if (c < 0x80 && (std::isalnum(int(c)) || c == ' ')) Type(char(c));
    }
    for (const Event& e : events) {
      if (!g_open) break;
      Handle(e);
    }
    if (!g_open) return;
    Draw(io);
  }

 private:
  enum class Page { kList, kKeyboard };

  struct Item {
    enum Kind { kProfile, kGuest, kCreate, kCancel } kind;
    profiles::Profile profile;
  };

  void Refresh() {
    items_.clear();
    std::vector<uint64_t> signed_in;
    for (uint32_t i = 0; i < 4; ++i)
      if (uint64_t xuid = kernel::UserXuid(i)) signed_in.push_back(xuid);
    for (profiles::Profile& p : profiles::List(kernel::TitlePaths().profiles_root))
      if (std::find(signed_in.begin(), signed_in.end(), p.xuid) == signed_in.end())
        items_.push_back({Item::kProfile, std::move(p)});
    profiles_.clear();
    for (const Item& item : items_) profiles_.push_back(item.profile.gamertag);
    // As on the console, a guest plays from a signed-in profile (player 1's: always signed in).
    if (allow_guest_) items_.push_back({Item::kGuest});
    items_.push_back({Item::kCreate});
    items_.push_back({Item::kCancel});
    selected_ = std::clamp(selected_, 0, int(items_.size()) - 1);
  }

  // The slot a choice signs in: the chooser's own when it isn't signed in, else the first free slot
  // with a controller (player 1 or the keyboard choosing for the player who joins). -1: none.
  static int SlotFor(int chooser) {
    if (chooser >= 1 && chooser < 4 && !kernel::IsUserSignedIn(uint32_t(chooser))) return chooser;
    for (uint32_t slot = 1; slot < 4; ++slot)
      if (!kernel::IsUserSignedIn(slot) && input::IsConnected(slot)) return int(slot);
    return -1;
  }

  void Handle(const Event& e) {
    if (page_ == Page::kKeyboard) return HandleKeyboard(e);
    switch (e.press) {
      case Press::kUp: selected_ = std::max(0, selected_ - 1); break;
      case Press::kDown: selected_ = std::min(int(items_.size()) - 1, selected_ + 1); break;
      case Press::kA:
      case Press::kStart: Choose(selected_, e.slot); break;
      case Press::kB: Finish(); break;
      default: break;
    }
  }

  void Choose(int index, int chooser) {
    if (index < 0 || index >= int(items_.size())) return;
    const Item& item = items_[size_t(index)];
    if (item.kind == Item::kCancel) return Finish();
    const int slot = SlotFor(chooser);
    if (slot < 0) {
      message_ = Tr().error_no_slot;
      return;
    }
    message_.clear();
    if (item.kind == Item::kGuest) return SignInGuest(slot);
    if (item.kind == Item::kCreate) {
      page_ = Page::kKeyboard;
      typing_slot_ = slot;
      text_.clear();
      shift_ = true;
      key_row_ = 1, key_col_ = 0;
      return;
    }
    SignIn(slot, item.profile);
  }

  void SignIn(int slot, const profiles::Profile& profile) {
    if (!kernel::SignInProfile(uint32_t(slot), profile.xuid, profile.gamertag)) return;
    char text[160];
    std::snprintf(text, sizeof(text), Tr().signed_in_as, unsigned(slot + 1), profile.gamertag.c_str());
    note_ = text;
    if (remaining_ > 0) --remaining_;
    if (remaining_ == 0 || SlotFor(-1) < 0) return Finish();
    page_ = Page::kList;
    Refresh();
  }

  void SignInGuest(int slot) {
    if (!kernel::SignInGuest(uint32_t(slot))) return;
    char text[160];
    std::snprintf(text, sizeof(text), Tr().signed_in_as, unsigned(slot + 1), kernel::UserName(uint32_t(slot)).c_str());
    note_ = text;
    if (remaining_ > 0) --remaining_;
    if (remaining_ == 0 || SlotFor(-1) < 0) return Finish();
    Refresh();
  }

  // The gamertag a guest signed in now would have ("Daniel (1)").
  static std::string NextGuestName() {
    int sponsor = -1;
    bool taken[profiles::kMaxGuests + 1] = {};
    for (uint32_t i = 0; i < 4; ++i) {
      if (!kernel::IsUserSignedIn(i)) continue;
      if (const int n = profiles::GuestNumber(kernel::UserXuid(i))) taken[n] = true;
      else if (sponsor < 0) sponsor = int(i);
    }
    int number = 1;
    while (number < profiles::kMaxGuests && taken[number]) ++number;
    return profiles::GuestGamertag(sponsor >= 0 ? kernel::UserName(uint32_t(sponsor)) : "", number);
  }

  void HandleKeyboard(const Event& e) {
    switch (e.press) {
      case Press::kUp:
      case Press::kDown: {
        const int row = std::clamp(key_row_ + (e.press == Press::kUp ? -1 : 1), 0, int(rows_.size()) - 1);
        key_col_ = NearestKey(row, KeyCenter(key_row_, key_col_));
        key_row_ = row;
        break;
      }
      case Press::kLeft: key_col_ = (key_col_ + int(rows_[size_t(key_row_)].size()) - 1) % int(rows_[size_t(key_row_)].size()); break;
      case Press::kRight: key_col_ = (key_col_ + 1) % int(rows_[size_t(key_row_)].size()); break;
      case Press::kA: PressKey(rows_[size_t(key_row_)][size_t(key_col_)]); break;
      case Press::kB:
        if (text_.empty()) {
          page_ = Page::kList;
          message_.clear();
        } else {
          Erase();
        }
        break;
      case Press::kX: Type(' '); break;
      case Press::kY: shift_ = !shift_; break;
      case Press::kStart: Create(); break;
    }
  }

  float KeyCenter(int row, int col) const {
    float x = 0;
    const auto& keys = rows_[size_t(row)];
    for (int i = 0; i < col; ++i) x += keys[size_t(i)].units;
    return x + keys[size_t(col)].units / 2;
  }

  int NearestKey(int row, float center) const {
    int best = 0;
    float best_distance = 1e9f;
    for (int i = 0; i < int(rows_[size_t(row)].size()); ++i) {
      const float distance = std::abs(KeyCenter(row, i) - center);
      if (distance < best_distance) best_distance = distance, best = i;
    }
    return best;
  }

  void PressKey(const Key& key) {
    switch (key.kind) {
      case Key::kChar: Type(key.ch); break;
      case Key::kShift: shift_ = !shift_; break;
      case Key::kSpace: Type(' '); break;
      case Key::kErase: Erase(); break;
      case Key::kDone: Create(); break;
    }
  }

  void Type(char c) {
    if (text_.size() >= profile::kNameLength) return;
    if (c == ' ' && (text_.empty() || text_.back() == ' ')) return;
    if (std::isalpha(uint8_t(c))) c = shift_ ? char(std::toupper(uint8_t(c))) : char(std::tolower(uint8_t(c)));
    text_ += c;
    message_.clear();
    if (std::isalpha(uint8_t(c))) shift_ = false;  // a capital first, then small letters
  }

  void Erase() {
    if (!text_.empty()) text_.pop_back();
    if (text_.empty()) shift_ = true;
  }

  void Create() {
    const profiles::Result created =
        profiles::Create(kernel::TitlePaths().profiles_root, text_, "auto");
    switch (created.error) {
      case profiles::Error::kNone: break;
      case profiles::Error::kEmptyName: message_ = Tr().error_empty; return;
      case profiles::Error::kNameTaken: message_ = Tr().error_taken; return;
      default: message_ = Tr().error_write; return;
    }
    KLOG_I("sign-in picker: created %s (%016llX)", created.profile.gamertag.c_str(),
           (unsigned long long)created.profile.xuid);
    int slot = typing_slot_;
    if (kernel::IsUserSignedIn(uint32_t(slot))) slot = SlotFor(-1);
    page_ = Page::kList;
    if (slot < 0) {
      Refresh();
      return;
    }
    SignIn(slot, created.profile);
  }

  void Finish() {
    g_open = false;
    PostToUIThread([this] {
      if (auto* drawer = SharedImGuiDrawer()) drawer->RemoveDialog(this);
      if (!IsSettingsPanelOpen()) input::SetGameInputEnabled(true);
      kernel::CloseSigninUi();
    });
  }

  // ---- drawing --------------------------------------------------------------------------------

  // Sizes in 96-DPI units, as large again as the window is taller than 720 lines at its DPI: on a
  // 1080p window the picker is half again the F2 panel's size, readable from the couch.
  float S(float dips) const { return theme::Px(dips) * scale_; }

  void Draw(ImGuiIO& io) {
    const ImVec2 display = io.DisplaySize;
    scale_ = std::clamp(display.y / (720.0f * theme::Scale()), 1.0f, 3.0f);
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display, color::Rgb(0x000000, 150));
    const float width = std::min(S(560), display.x - S(32));
    ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, color::Vec(color::kPanel));
    ImGui::PushStyleColor(ImGuiCol_Border, color::Vec(color::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(20)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##anyx360_signin", nullptr, flags)) {
      ImGui::SetWindowFontScale(scale_);  // the theme's fonts, scaled with the window
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(8)));
      ImDrawList* draw = ImGui::GetWindowDrawList();
      const ImVec2 corner = ImGui::GetCursorScreenPos();
      draw->AddRectFilled(ImVec2(corner.x - S(24), corner.y - S(20)), ImVec2(corner.x - S(20), corner.y + S(30)),
                          color::kGreen);
      ImGui::PushFont(Title());
      ImGui::TextUnformatted(page_ == Page::kKeyboard ? Tr().new_title : Tr().title);
      ImGui::PopFont();
      if (page_ == Page::kList)
        DrawList();
      else
        DrawKeyboard();
      if (!message_.empty()) ImGui::TextColored(color::Vec(color::kAmber), "%s", message_.c_str());
      DrawHints();
      ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
  }

  void DrawList() {
    if (!note_.empty()) ImGui::TextColored(color::Vec(color::kGreenBright), "%s", note_.c_str());
    if (remaining_ > 1)
      ImGui::TextWrapped(Tr().choose_more, remaining_);
    else
      ImGui::TextWrapped("%s", Tr().choose);
    if (profiles_.empty()) ImGui::TextDisabled("%s", allow_guest_ ? Tr().none : Tr().none_no_guest);
    ImGui::Dummy(ImVec2(0, S(4)));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float row_h = S(52), width = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < int(items_.size()); ++i) {
      const Item& item = items_[size_t(i)];
      ImGui::PushID(i);
      const ImVec2 at = ImGui::GetCursorScreenPos();
      const bool clicked = ImGui::InvisibleButton("##item", ImVec2(width, row_h));
      if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x + ImGui::GetIO().MouseDelta.y != 0) selected_ = i;
      const bool selected = i == selected_;
      const ImVec2 end(at.x + width, at.y + row_h);
      draw->AddRectFilled(at, end, selected ? color::kGreen : color::kPanelHigh, S(4));
      if (selected) draw->AddRect(at, end, color::Rgb(0xFFFFFF), S(4), 0, S(2));
      // A gamerpic of their initial, a plus, or nothing for Cancel.
      const float r = S(16);
      const ImVec2 pic(at.x + S(14) + r, at.y + row_h / 2);
      const char* label = item.kind == Item::kProfile  ? item.profile.gamertag.c_str()
                          : item.kind == Item::kGuest  ? Tr().guest
                          : item.kind == Item::kCreate ? Tr().create
                                                       : Tr().cancel;
      float text_x = at.x + S(16);
      if (item.kind != Item::kCancel) {
        // The profile's gamerpic, else its initial.
        if (item.kind != Item::kProfile || !DrawGamerpic(draw, pic, r, item.profile.xuid)) {
          draw->AddCircleFilled(pic, r, selected ? color::Rgb(0x0B5A0B) : color::Rgb(0x30353B), 32);
          const char initial[2] = {item.kind == Item::kCreate ? '+' : char(std::toupper(uint8_t(label[0]))), 0};
          ImGui::PushFont(Heading());
          const ImVec2 size = ImGui::CalcTextSize(initial);
          draw->AddText(ImVec2(pic.x - size.x / 2, pic.y - size.y / 2), color::kText, initial);
          ImGui::PopFont();
        }
        text_x = pic.x + r + S(14);
      }
      ImGui::PushFont(Heading());
      const float text_h = ImGui::GetTextLineHeight();
      draw->AddText(ImVec2(text_x, at.y + (row_h - text_h) / 2), color::kText, label);
      const float label_w = ImGui::CalcTextSize(label).x;
      ImGui::PopFont();
      if (item.kind == Item::kGuest) {  // who they play as, after the label
        char detail[96];
        std::snprintf(detail, sizeof(detail), Tr().guest_detail, NextGuestName().c_str());
        ImGui::PushFont(Small());
        const float small_h = ImGui::GetTextLineHeight();
        draw->AddText(ImVec2(text_x + label_w + S(12), at.y + (row_h - small_h) / 2),
                      selected ? color::kText : color::kTextSecondary, detail);
        ImGui::PopFont();
      }
      ImGui::PopID();
      if (clicked) {
        selected_ = i;
        Choose(i, -1);
        if (!g_open || page_ != Page::kList) return;
      }
    }
  }

  void DrawKeyboard() {
    ImGui::TextWrapped("%s", Tr().new_hint);
    ImGui::Dummy(ImVec2(0, S(2)));
    // The gamertag typed, with a caret.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float field_h = S(44);
    draw->AddRectFilled(at, ImVec2(at.x + width, at.y + field_h), color::Rgb(0x0E1012), S(4));
    draw->AddRect(at, ImVec2(at.x + width, at.y + field_h), color::kGreen, S(4), 0, S(2));
    ImGui::PushFont(Title());
    const float text_h = ImGui::GetTextLineHeight();
    const ImVec2 text_at(at.x + S(14), at.y + (field_h - text_h) / 2);
    draw->AddText(text_at, color::kText, text_.c_str());
    const float caret = text_at.x + ImGui::CalcTextSize(text_.c_str()).x + S(2);
    ImGui::PopFont();
    if (std::fmod(ImGui::GetTime(), 1.0) < 0.6)
      draw->AddLine(ImVec2(caret, at.y + S(10)), ImVec2(caret, at.y + field_h - S(10)), color::kText, S(1.5f));
    ImGui::Dummy(ImVec2(width, field_h + S(8)));

    const float unit = std::floor((width - S(6) * 9) / 10);
    const float gap = S(6), key_h = S(40);
    for (int row = 0; row < int(rows_.size()); ++row) {
      float x = ImGui::GetCursorScreenPos().x;
      const float y = ImGui::GetCursorScreenPos().y;
      for (int col = 0; col < int(rows_[size_t(row)].size()); ++col) {
        const Key& key = rows_[size_t(row)][size_t(col)];
        const float w = unit * key.units + gap * (key.units - 1);
        const ImVec2 a(x, y), b(x + w, y + key_h);
        const bool selected = row == key_row_ && col == key_col_;
        const bool active = key.kind == Key::kShift && shift_;
        draw->AddRectFilled(a, b, selected ? color::kGreen : active ? color::Rgb(0x2F4A2F) : color::kPanelHigh, S(4));
        if (selected) draw->AddRect(a, b, color::Rgb(0xFFFFFF), S(4), 0, S(2));
        char label[2] = {key.ch, 0};
        if (key.kind == Key::kChar && std::isalpha(uint8_t(key.ch)) && !shift_) label[0] = char(std::tolower(uint8_t(key.ch)));
        const char* text = key.kind == Key::kChar    ? label
                           : key.kind == Key::kShift ? Tr().shift
                           : key.kind == Key::kSpace ? Tr().space
                           : key.kind == Key::kErase ? Tr().erase
                                                     : Tr().done;
        ImGui::PushFont(Heading());
        const ImVec2 size = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2(a.x + (w - size.x) / 2, a.y + (key_h - size.y) / 2), color::kText, text);
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(row * 100 + col);
        if (ImGui::InvisibleButton("##key", ImVec2(w, key_h))) {
          key_row_ = row, key_col_ = col;
          PressKey(key);
        }
        ImGui::PopID();
        x += w + gap;
      }
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x, y + key_h + gap));
    }
    ImGui::Dummy(ImVec2(0, S(2)));
  }

  void DrawHints() {
    struct Hint {
      char button;
      ImU32 color;
      const char* text;
    };
    std::vector<Hint> hints = {{'A', color::Rgb(0x5DB33A), Tr().hint_select}, {'B', color::Rgb(0xD9433B), Tr().hint_back}};
    if (page_ == Page::kKeyboard) {
      hints.push_back({'X', color::Rgb(0x3B8EDB), Tr().hint_space});
      hints.push_back({'Y', color::Rgb(0xE8B923), Tr().hint_shift});
    }
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
    for (const Hint& h : hints) {
      const char button[2] = {h.button, 0};
      hint(button, h.color, h.text);
    }
    if (page_ == Page::kKeyboard) {
      // Start: a wider pill.
      const char* label = "START";
      const ImVec2 size = ImGui::CalcTextSize(label);
      draw->AddRectFilled(ImVec2(x, cy - r), ImVec2(x + size.x + S(12), cy + r), color::Rgb(0x5A5F66), r);
      draw->AddText(ImVec2(x + S(6), cy - size.y / 2), color::kText, label);
      x += size.x + S(18);
      draw->AddText(ImVec2(x, cy - ImGui::GetTextLineHeight() / 2), color::kTextSecondary, Tr().hint_done);
    }
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 2 * r));
  }

  std::vector<std::vector<Key>> rows_;
  std::vector<Item> items_;
  std::vector<std::string> profiles_;
  uint32_t remaining_ = 1;
  bool allow_guest_ = true;
  Page page_ = Page::kList;
  int selected_ = 0;
  std::string message_, note_;
  // The keyboard page.
  std::string text_;
  bool shift_ = true;
  int key_row_ = 1, key_col_ = 0;
  int typing_slot_ = -1;
  float scale_ = 1;
};

SigninPicker* g_picker = nullptr;  // lives until exit; registered with the drawer while open

}  // namespace

bool OpenSigninPicker(uint32_t count, bool allow_guest) {
  if (!SharedImGuiDrawer() || !GameWindow()) return false;
  static std::once_flag started;
  std::call_once(started, [] { std::thread(PadThread).detach(); });
  PostToUIThread([count, allow_guest] {
    xe::ui::ImGuiDrawer* drawer = SharedImGuiDrawer();
    if (!g_picker) {
      g_picker = new SigninPicker(drawer);  // registers itself
      drawer->RemoveDialog(g_picker);
    }
    g_picker->Open(count, allow_guest);
    input::SetGameInputEnabled(false);  // the keyboard is the picker's; the cursor is free
    g_open = true;
    drawer->AddDialog(g_picker);
    drawer->RequestPaintIfIdle(0);
  });
  return true;
}

}  // namespace x360::gpu
