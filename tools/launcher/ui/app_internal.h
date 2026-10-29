// What app.cpp and avatar_page.cpp share: the look (colours, text styles), the targets' ids and
// the overlays (internal to the launcher's window code).
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iterator>

#include "core/paths.h"
#include "ui/app.h"

namespace anyx360::launcher::ui {

using anyx360::ui::Align;
using anyx360::ui::TextStyle;

// Metro dark: charcoal, flat panels, Xbox green for what acts.
inline constexpr Color kBackground = Color::Hex(0x141414);
inline constexpr Color kPanel = Color::Hex(0x1E1E1E);
inline constexpr Color kPanelHigh = Color::Hex(0x2B2B2B);
inline constexpr Color kPanelHover = Color::Hex(0x393939);
inline constexpr Color kBorder = Color::Hex(0x333333);
inline constexpr Color kText = Color::Hex(0xFFFFFF);
inline constexpr Color kTextSecondary = Color::Hex(0xB8B8B8);
inline constexpr Color kTextTertiary = Color::Hex(0x7E7E7E);
inline constexpr Color kGreen = Color::Hex(0x107C10);
inline constexpr Color kGreenHover = Color::Hex(0x1A9A1A);
inline constexpr Color kGreenText = Color::Hex(0x7FD46F);
inline constexpr Color kRed = Color::Hex(0xE81123);
inline constexpr Color kRedText = Color::Hex(0xFF8080);

inline constexpr float kMargin = 48;
inline constexpr float kDrawerBar = 56;
inline constexpr float kTileGap = 24;
inline constexpr float kSelectedScale = 1.07f;

// Target ids: what the pointer and the focus reach. Stable across frames.
enum : int {
  kIdPlay = 1,
  kIdMore,
  kIdCompatibleBar,
  kIdLanguage,
  kIdTerms,
  kIdPassword,
  kIdTermsCheck,
  kIdTermsText,
  kIdProfile,
  kIdProfileLanguage,
  kIdSwitchProfile,
  kIdGamertagField,
  kIdAchievements,
  kIdAchievementList,
  kIdTile = 1000,
  kIdCompatible = 2000,
  kIdButton = 3000,
  kIdChoice = 3100,
  kIdMenuItem = 3200,
  kIdWhoTile = 3300,      // + profile; the last is "Create profile"
  kIdProfileRow = 3500,   // + profile; the last is "Create profile"
  kIdKey = 3700,          // + row * 16 + key: the on-screen keyboard
  kIdConsoleMode = 3900,  // the header's link to console mode
  // Console mode (the dashboard).
  kIdTab = 4000,             // + page
  kIdConsoleGame = 4100,     // + game (index into games_): the Games page
  kIdConsoleRecent = 4200,   // + game: Home's covers
  kIdConsoleAch = 4300,      // + game: the Achievements page's rows
  kIdConsoleProfile = 4400,  // + profile; the last is "Create profile"
  kIdConsoleAction = 4500,   // + n: a page's buttons
  kIdConsoleEnd = 4600,
  // The avatar editor.
  kIdAvatarRow = 5000,     // + row
  kIdAvatarButton = 5100,  // + n: Save, Cancel
  kIdGamerpic = 5200,      // + picture in the picker
};
inline constexpr int kPages = 7;

// The logos in launcher.rc: the mark, and the mark over the "anyX360" wordmark.
inline constexpr int kLogoMark = 2;
inline constexpr int kLogoFull = 3;

// The dashboard's Home (ui/home_page.cpp): its targets besides the games (kIdConsoleRecent + game).
inline constexpr int kIdHomeAdd = kIdConsoleAction + 60;    // "Add a game", when there are none
inline constexpr int kIdHomePanel = kIdConsoleAction + 70;  // + entry: My games, Settings, Add games
inline constexpr int kIdHomeAvatar = kIdConsoleAction + 80;
inline constexpr int kIdHomePill = kIdConsoleAction + 85;  // who's signed in: "Who's playing?"
inline constexpr int kIdHomeCard = kIdConsoleAction + 90;  // the gamercard (as on the other pages)

// The release's box art (covers\<slug>.jpg, the front only), or the repository's (a whole scan)
// when the launcher runs from the build folder.
inline fs::path BoxArt(const wchar_t* slug) {
  if (!slug || !*slug) return {};
  const fs::path name = fs::path(slug) += L".jpg";
  if (Exists(Root() / L"covers" / name)) return Root() / L"covers" / name;
  return FindUpwards(Root(), fs::path(L"covers") / name);
}

// The game's background art (backgrounds\<slug>.jpg or .png, backgrounds/README.md): the release's,
// or the repository's when the launcher runs from the build folder. Empty if it has none.
inline fs::path BackgroundArt(const wchar_t* slug) {
  if (!slug || !*slug) return {};
  for (const wchar_t* extension : {L".jpg", L".png"}) {
    const fs::path name = fs::path(slug) += extension;
    if (Exists(Root() / L"backgrounds" / name)) return Root() / L"backgrounds" / name;
    if (fs::path found = FindUpwards(Root(), fs::path(L"backgrounds") / name); !found.empty()) return found;
  }
  return {};
}

// A tile's color when it has no picture: the icon's colorful pixels, kept dark for white text.
inline Color AverageColor(const anyx360::ui::Image& image) {
  double sum[3] = {}, weight = 0;
  for (size_t i = 0; i + 3 < image.bgra.size(); i += 4) {
    if (image.bgra[i + 3] < 128) continue;
    const double b = image.bgra[i] / 255.0, g = image.bgra[i + 1] / 255.0, r = image.bgra[i + 2] / 255.0;
    const double high = std::max({r, g, b}), low = std::min({r, g, b});
    const double saturation = high > 0 ? (high - low) / high : 0;
    const double w = saturation * saturation * high + 0.01;
    sum[0] += r * w, sum[1] += g * w, sum[2] += b * w, weight += w;
  }
  if (weight <= 0) return Color::Hex(0x2B2B2B);
  const double r = sum[0] / weight, g = sum[1] / weight, b = sum[2] / weight;
  const double scale = 0.5 / std::max({r, g, b, 0.01});
  return {float(std::min(1.0, r * scale)), float(std::min(1.0, g * scale)), float(std::min(1.0, b * scale)), 1};
}

// AnyX360's folder of %LOCALAPPDATA%: profile.txt and the profiles (next to the games' own folders).
inline fs::path ProfilesRoot() { return LocalAppData() / L"AnyX360"; }

// A profile's colour for its gamerpic (none yet: its initial on this), from its XUID.
inline Color ProfileColor(uint64_t xuid) {
  constexpr uint32_t kColors[] = {0x107C10, 0x0063B1, 0x8E44AD, 0xC0392B, 0xD35400, 0x16A085, 0x2C3E50, 0xB7950B};
  return Color::Hex(kColors[(xuid ^ (xuid >> 17) ^ (xuid >> 33)) % std::size(kColors)]);
}

inline bool g_instant = false;  // screenshots: every animation at its end

inline double Now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline float EaseOut(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

inline TextStyle Style(float size, DWRITE_FONT_WEIGHT weight, Color color, Align align = Align::kStart,
                       bool wrap = false) {
  TextStyle style;
  style.size = size;
  style.weight = weight;
  style.color = color;
  style.align = align;
  style.wrap = wrap;
  return style;
}

inline const TextStyle& Body() {
  static const TextStyle style = Style(15, DWRITE_FONT_WEIGHT_NORMAL, kTextSecondary, Align::kStart, true);
  return style;
}

// A promise answered once: by the player, or with the default when the launcher closes first.
template <typename T>
struct Answer {
  std::promise<T> promise;
  std::atomic<bool> given{false};
  void Give(T value) {
    if (!given.exchange(true)) promise.set_value(std::move(value));
  }
};

struct App::Target {
  RectF rect;
  int id = 0;
  std::function<void()> action;
  bool focusable = true;
};

enum class OverlayKind {
  kAdd,
  kProgress,
  kMessage,
  kPassword,
  kMenu,
  kTerms,
  kWho,
  kProfiles,
  kGamertag,
  kAchievements,
  kAvatar,    // the avatar editor (desktop mode)
  kGamerpic,  // the gamerpic picker
};

struct App::Overlay {
  struct Action {
    std::wstring label;
    std::function<void()> run;
    bool enabled = true;
  };
  OverlayKind kind = OverlayKind::kMessage;
  std::wstring title, text;
  bool error = false;
  std::vector<Action> buttons;  // the first is the default
  bool primary = true;          // the first button in green (not for a lone Cancel)
  std::function<void()> back;   // Esc, B; none: nothing happens
  // A password.
  std::wstring typed;
  bool wrong = false;
  std::shared_ptr<Answer<std::optional<std::wstring>>> password;
  std::shared_ptr<Answer<bool>> replace;
  // The ⋯ menu.
  RectF anchor;
  std::vector<Action> items;
  // "Who's playing?" at start: it can't be left without choosing.
  bool required = false;
  // A gamertag typed (in `typed`): the profile renamed (0: a new one), the on-screen keyboard's
  // Shift. Achievements, the avatar editor, the gamerpic picker: the profile whose they are (0:
  // nobody playing); achievements: the title's.
  uint64_t xuid = 0;
  uint32_t title_id = 0;
  bool shift = true;
  // The terms.
  bool require_acceptance = false, checked = false;
  float scroll = 0, content = 0, view = 0;
  Animation fade;
  int saved_focus = -1;
  // The gamerpic picker's pictures: files, after the avatar's (`picture`, if it has one) and the
  // initial.
  std::vector<fs::path> pictures;
  std::shared_ptr<const anyx360::ui::Image> picture;
};

}  // namespace anyx360::launcher::ui
