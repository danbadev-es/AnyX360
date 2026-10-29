#include "ui/app.h"

#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <fstream>

#include "anyx360_version.h"
#include "core/anyx.h"
#include "core/catalog.h"
#include "core/launch.h"
#include "core/paths.h"
#include "core/strings.h"
#include "core/terms.h"

#define ANYX360_WIDEN2(x) L##x
#define ANYX360_WIDEN(x) ANYX360_WIDEN2(x)

namespace anyx360::launcher::ui {

using anyx360::ui::Align;
using anyx360::ui::TextStyle;

namespace {

// Metro dark: charcoal, flat panels, Xbox green for what acts.
constexpr Color kBackground = Color::Hex(0x141414);
constexpr Color kPanel = Color::Hex(0x1E1E1E);
constexpr Color kPanelHigh = Color::Hex(0x2B2B2B);
constexpr Color kPanelHover = Color::Hex(0x393939);
constexpr Color kBorder = Color::Hex(0x333333);
constexpr Color kText = Color::Hex(0xFFFFFF);
constexpr Color kTextSecondary = Color::Hex(0xB8B8B8);
constexpr Color kTextTertiary = Color::Hex(0x7E7E7E);
constexpr Color kGreen = Color::Hex(0x107C10);
constexpr Color kGreenHover = Color::Hex(0x1A9A1A);
constexpr Color kGreenText = Color::Hex(0x7FD46F);
constexpr Color kRed = Color::Hex(0xE81123);
constexpr Color kRedText = Color::Hex(0xFF8080);

constexpr float kMargin = 48;
constexpr float kDrawerBar = 56;
constexpr float kTileGap = 24;
constexpr float kSelectedScale = 1.07f;

// The logos in launcher.rc: the mark, and the mark over the "anyX360" wordmark.
constexpr int kLogoMark = 2;
constexpr int kLogoFull = 3;

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
  kIdTile = 1000,
  kIdCompatible = 2000,
  kIdButton = 3000,
  kIdChoice = 3100,
  kIdMenuItem = 3200,
  kIdWhoTile = 3300,      // + profile; the last is "Create profile"
  kIdProfileRow = 3500,   // + profile; the last is "Create profile"
  kIdKey = 3700,          // + row * 16 + key: the on-screen keyboard
};

// AnyX360's folder of %LOCALAPPDATA%: profile.txt and the profiles (next to the games' own folders).
fs::path ProfilesRoot() { return LocalAppData() / L"AnyX360"; }

// The on-screen keyboard for a gamertag: letters and digits, then Shift, Space and Delete.
constexpr const wchar_t* kKeyRows[] = {L"1234567890", L"QWERTYUIOP", L"ASDFGHJKL", L"ZXCVBNM"};
constexpr int kKeyShift = 0, kKeySpace = 1, kKeyErase = 2;  // the last row's keys

// A profile's colour for its gamerpic (none yet: its initial on this), from its XUID.
Color ProfileColor(uint64_t xuid) {
  constexpr uint32_t kColors[] = {0x107C10, 0x0063B1, 0x8E44AD, 0xC0392B, 0xD35400, 0x16A085, 0x2C3E50, 0xB7950B};
  return Color::Hex(kColors[(xuid ^ (xuid >> 17) ^ (xuid >> 33)) % std::size(kColors)]);
}

// What the games are told for a profile language ("auto": Windows').
std::wstring LanguageLabel(const std::string& tag) {
  for (const auto& language : x360::profile::Languages())
    if (tag == language.tag) return language.name;
  const uint32_t windows = x360::profile::WindowsLanguage();
  for (const auto& language : x360::profile::Languages())
    if (language.xbox == windows) return Tr(kTextLanguageAuto, {language.name});
  return Tr(kTextLanguageNoAuto);
}

// A gamertag's characters: letters and digits (ASCII) and spaces.
bool NameCharacter(wchar_t ch) { return ch < 0x80 && (std::iswalnum(ch) || ch == L' '); }

bool g_instant = false;  // screenshots: every animation at its end

double Now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

float EaseOut(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

TextStyle Style(float size, DWRITE_FONT_WEIGHT weight, Color color, Align align = Align::kStart, bool wrap = false) {
  TextStyle style;
  style.size = size;
  style.weight = weight;
  style.color = color;
  style.align = align;
  style.wrap = wrap;
  return style;
}

const TextStyle& Body() {
  static const TextStyle style = Style(15, DWRITE_FONT_WEIGHT_NORMAL, kTextSecondary, Align::kStart, true);
  return style;
}

std::wstring Version() { return ANYX360_WIDEN(ANYX360_VERSION_FULL); }

// A file embedded in the launcher (RCDATA in launcher.rc); empty if it isn't there.
std::span<const uint8_t> Embedded(int resource) {
  const HMODULE module = GetModuleHandleW(nullptr);
  const HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(resource), RT_RCDATA);
  const HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
  if (!loaded) return {};
  return {static_cast<const uint8_t*>(LockResource(loaded)), SizeofResource(module, found)};
}

// The release's box art (covers\<slug>.jpg, the front only), or the repository's (a whole scan)
// when the launcher runs from the build folder.
fs::path BoxArt(const wchar_t* slug) {
  if (!slug || !*slug) return {};
  const fs::path name = fs::path(slug) += L".jpg";
  if (Exists(Root() / L"covers" / name)) return Root() / L"covers" / name;
  return FindUpwards(Root(), fs::path(L"covers") / name);
}

// A tile's color when it has no picture: the icon's colorful pixels, kept dark for white text.
Color AverageColor(const Image& image) {
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

fs::path ChooseFile(HWND owner, const std::wstring& title,
                    const std::vector<std::pair<std::wstring, std::wstring>>& filters, bool folder) {
  fs::path chosen;
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return chosen;
  std::vector<COMDLG_FILTERSPEC> types;
  for (const auto& [name, spec] : filters) types.push_back({name.c_str(), spec.c_str()});
  if (!types.empty()) dialog->SetFileTypes(UINT(types.size()), types.data());
  dialog->SetTitle(title.c_str());
  dialog->SetOptions(folder ? FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM
                            : FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
  IShellItem* item = nullptr;
  if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item))) {
    wchar_t* path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) chosen = path;
    CoTaskMemFree(path);
    item->Release();
  }
  dialog->Release();
  return chosen;
}

// Where to save a file, `name` proposed; the dialog asks before replacing one.
fs::path ChooseSaveFile(HWND owner, const std::wstring& title, const std::wstring& name,
                        const std::vector<std::pair<std::wstring, std::wstring>>& filters, const wchar_t* extension) {
  fs::path chosen;
  IFileSaveDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return chosen;
  std::vector<COMDLG_FILTERSPEC> types;
  for (const auto& [label, spec] : filters) types.push_back({label.c_str(), spec.c_str()});
  if (!types.empty()) dialog->SetFileTypes(UINT(types.size()), types.data());
  dialog->SetTitle(title.c_str());
  dialog->SetFileName(name.c_str());
  dialog->SetDefaultExtension(extension);
  dialog->SetOptions(FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_STRICTFILETYPES | FOS_NOREADONLYRETURN |
                     FOS_PATHMUSTEXIST);
  IShellItem* item = nullptr;
  if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item))) {
    wchar_t* path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) chosen = path;
    CoTaskMemFree(path);
    item->Release();
  }
  dialog->Release();
  return chosen;
}

// A game's title as a file name (the characters Windows doesn't allow in one replaced).
std::wstring FileNameOf(std::wstring title) {
  for (wchar_t& c : title)
    if (c == L':' || c == L'<' || c == L'>' || c == L'"' || c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*')
      c = L'-';
  return title;
}

// What the file dialog lists as games (packages have no extension: "All files" shows them).
constexpr wchar_t kGameFiles[] =
    L"*.anyx;*.7z;*.zip;*.zipx;*.rar;*.001;*.iso;*.xiso;*.tar;*.gz;*.tgz;*.bz2;*.tbz2;*.xz;*.txz;*.zst;*.cab;*.wim;"
    L"*.xex";

// A promise answered once: by the player, or with the default when the launcher closes first.
template <typename T>
struct Answer {
  std::promise<T> promise;
  std::atomic<bool> given{false};
  void Give(T value) {
    if (!given.exchange(true)) promise.set_value(std::move(value));
  }
};

}  // namespace

float App::Animation::Value(double now) const {
  if (duration <= 0) return to;
  return from + (to - from) * EaseOut(float((now - start) / duration));
}

void App::Animation::Go(float target, double now, double length) {
  if (g_instant) {
    from = to = target;
    duration = 0;
    return;
  }
  if (target == to && !Running(now)) return;
  from = Value(now);
  to = target;
  start = now;
  duration = length;
}

struct App::Target {
  RectF rect;
  int id = 0;
  std::function<void()> action;
  bool focusable = true;
};

enum class OverlayKind { kAdd, kProgress, kMessage, kPassword, kMenu, kTerms, kWho, kProfiles, kGamertag };

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
  // Shift.
  uint64_t xuid = 0;
  bool shift = true;
  // The terms.
  bool require_acceptance = false, checked = false;
  float scroll = 0, content = 0, view = 0;
  Animation fade;
  int saved_focus = -1;
};

App::App(Host& host) : host_(host), images_([&host] { host.Post([&host] { host.Invalidate(); }); }) {
  compatible_open_ = ReadSetting(SettingsFile(L"launcher_compatible.txt")) == L"1";
  // A profile.txt from before 0.20.0 becomes profiles; a first start makes one to choose.
  sign_in_ = x360::profiles::LoadSignIn(ProfilesRoot(), true);
  profiles_ = x360::profiles::List(ProfilesRoot());
  drawer_.Go(compatible_open_ ? 1.0f : 0.0f, 0, 0);
  selection_.Go(1, 0, 0);
  background_fade_.Go(1, 0, 0);
}

App::~App() {
  // A job waiting for the player gets the default answer, then is cancelled.
  for (Overlay& overlay : overlays_) {
    if (overlay.replace) overlay.replace->Give(false);
    if (overlay.password) overlay.password->Give(std::nullopt);
  }
  worker_.Cancel();
  worker_.Join();
}

// ---- the library ----------------------------------------------------------------------------

void App::Load(std::wstring select) {
  title_stats_.clear();
  if (select.empty() && SelectedGame() >= 0) select = games_[size_t(SelectedGame())].info->id;
  games_ = ReadLibrary();
  tiles_.clear();
  for (size_t i = 0; i < games_.size(); ++i) {
    if (games_[i].installed) tiles_.push_back(int(i));
    // Added before icons were read: read it now; the tile is redrawn when it is ready.
    if (games_[i].needs_icon) {
      std::thread([this, info = games_[i].info, dir = games_[i].dir] {
        if (SaveIcon(*info, dir)) host_.Post([this] { if (!working_) Load(); });
      }).detach();
    }
  }
  tiles_.push_back(-1);
  selected_ = 0;
  for (size_t t = 0; t < tiles_.size(); ++t)
    if (tiles_[t] >= 0 && games_[size_t(tiles_[t])].info->id == select) selected_ = int(t);

  compatible_.clear();
  for (const LibraryGame& game : games_)
    compatible_.push_back(
        {game.info->title, game.info, BoxArt(game.info->cover), game.installed, game.info->playable, game.needs_update});
  for (const UpcomingGame& game : kUpcoming) compatible_.push_back({game.title, nullptr, BoxArt(game.cover)});
  host_.Invalidate();
}

int App::SelectedGame() const {
  return selected_ >= 0 && selected_ < int(tiles_.size()) ? tiles_[size_t(selected_)] : -1;
}

void App::Select(int tile) {
  tile = std::clamp(tile, 0, int(tiles_.size()) - 1);
  if (tile != selected_) selection_ = {0, 0, 0, 0}, selection_.Go(1, Now());
  selected_ = tile;
  host_.Invalidate();
}

void App::CheckTerms() {
  if (!Player()) ShowWhoIsPlaying(true);
  if (!TermsAccepted()) ShowTerms(true);
}

// ---- painting -------------------------------------------------------------------------------

void App::AddTarget(const RectF& r, int id, std::function<void()> action, bool focusable) {
  targets_.push_back({r, id, std::move(action), focusable});
}

ID2D1Bitmap* App::Bitmap(Canvas& c, const std::shared_ptr<const Image>& image) {
  if (!image) return nullptr;
  auto found = bitmaps_.find(image);
  if (found != bitmaps_.end()) return found->second.Get();
  if (bitmaps_.size() > 96) bitmaps_.clear();
  ComPtr<ID2D1Bitmap> bitmap = c.CreateBitmap(image->width, image->height, image->bgra);
  return (bitmaps_[image] = bitmap).Get();
}

std::shared_ptr<const Image> App::Logo(int resource, float height) {
  const uint32_t pixels = uint32_t(std::lround(height * dpi_ / 96));
  auto [found, added] = logos_.try_emplace({resource, pixels});
  if (added) {
    // Small PNGs: decoded right here, once per size (a failure isn't tried again).
    auto image = std::make_shared<Image>();
    std::string error;
    if (anyx360::ui::DecodePictureData(Embedded(resource), *image, error, 0, pixels)) found->second = image;
  }
  return found->second;
}

float App::DrawLogo(Canvas& c, const std::shared_ptr<const Image>& logo, float x, float y, float opacity) {
  if (!logo) return 0;
  const float scale = dpi_ / 96;
  const RectF r{std::round(x * scale) / scale, std::round(y * scale) / scale, logo->width / scale, logo->height / scale};
  c.DrawBitmap(Bitmap(c, logo), r, opacity);
  return r.w;
}

void App::DrawFocus(Canvas& c, const RectF& r, float radius) {
  c.StrokeRoundedRect(r.Inset(-4), radius ? radius + 4 : 0, kText, 2);
}

void App::Button(Canvas& c, const RectF& r, std::wstring_view label, int id, std::function<void()> action, bool primary,
                 bool enabled) {
  const bool hover = hover_ == id && enabled;
  Color fill = primary ? (hover ? kGreenHover : kGreen) : (hover ? kPanelHover : kPanelHigh);
  if (!enabled) fill = Color::Hex(0x262626);
  c.FillRect(r, fill);
  TextStyle text = Style(16, DWRITE_FONT_WEIGHT_SEMI_BOLD, enabled ? kText : Color::Hex(0x5E5E5E), Align::kCenter);
  text.vertical = Align::kCenter;
  c.Text(label, r.Inset(12, 0), text);
  if (focus_ == id && focus_visible_) DrawFocus(c, r);
  AddTarget(r, id, enabled ? std::move(action) : std::function<void()>(), enabled);
}

void App::Paint(Canvas& c) {
  const double now = Now();
  if (c.generation() != bitmaps_generation_) {
    bitmaps_.clear();
    bitmaps_generation_ = c.generation();
  }
  width_ = c.width();
  height_ = c.height();
  dpi_ = c.dpi();
  targets_.clear();

  DrawBackground(c);
  DrawHeader(c);
  DrawRow(c);
  DrawDetails(c);
  DrawDrawer(c);
  if (!overlays_.empty()) {
    targets_.clear();  // modal: only the top overlay can be reached
    Overlay& overlay = overlays_.back();
    // A menu opened from an overlay (a profile's) shows over it.
    if (overlay.kind == OverlayKind::kMenu && overlays_.size() > 1) {
      Overlay& under = overlays_[overlays_.size() - 2];
      if (under.kind == OverlayKind::kWho) DrawWho(c, under, false);
      else if (under.kind != OverlayKind::kTerms && under.kind != OverlayKind::kMenu) DrawOverlay(c, under, false);
    }
    if (overlay.kind == OverlayKind::kTerms) DrawTerms(c, overlay, true);
    else if (overlay.kind == OverlayKind::kMenu) DrawMenu(c, overlay, true);
    else if (overlay.kind == OverlayKind::kWho) DrawWho(c, overlay, true);
    else DrawOverlay(c, overlay, true);
  }
  if (drop_hover_) {
    c.FillRect({0, 0, width_, height_}, Color::Hex(0x000000, 0.55f));
    const RectF frame{kMargin, kMargin, width_ - 2 * kMargin, height_ - 2 * kMargin};
    c.StrokeRect(frame, kGreen, 4);
    TextStyle drop = Style(34, DWRITE_FONT_WEIGHT_LIGHT, kText, Align::kCenter);
    drop.vertical = Align::kCenter;
    c.Text(Tr(kTextDropHere), frame, drop);
  }
  (void)now;
  if (!Focused()) {
    FocusFirst();
    host_.Invalidate();
  }
  hover_ = -1;
  if (const Target* target = TargetAt(mouse_x_, mouse_y_)) hover_ = target->id;
}

bool App::Animating() const {
  const double now = Now();
  for (const Overlay& overlay : overlays_)
    if (overlay.fade.Running(now)) return true;
  const bool indeterminate = working_ && percent_ < 0;
  return row_scroll_.Running(now) || drawer_.Running(now) || drawer_scroll_.Running(now) ||
         selection_.Running(now) || background_fade_.Running(now) || indeterminate;
}

void App::DrawBackground(Canvas& c) {
  const double now = Now();
  c.Clear(kBackground);
  // The selected game's box art, blurred and dark, behind everything.
  std::shared_ptr<const Image> cover;
  if (const int game = SelectedGame(); game >= 0) {
    const LibraryGame& lib = games_[size_t(game)];
    const fs::path path = !lib.cover.empty() ? lib.cover : BoxArt(lib.info->cover);
    const float scale = dpi_ / 96;
    cover = images_.Cover(path, uint32_t(220 * scale), uint32_t(330 * scale));
  }
  std::shared_ptr<const Image> blurred;
  if (cover) {
    auto found = blurred_.find(cover);
    if (found == blurred_.end()) {
      if (blurred_.size() > 32) blurred_.clear();
      found = blurred_.emplace(cover, std::make_shared<Image>(anyx360::ui::Blurred(*cover, 48, 30))).first;
    }
    blurred = found->second;
  }
  if (blurred != background_) {
    previous_background_ = background_;
    background_ = blurred;
    background_fade_ = {};
    background_fade_.Go(1, now, 0.35);
  }
  const float fade = background_fade_.Value(now);
  const RectF all{0, 0, width_, height_};
  if (previous_background_ && fade < 1) c.DrawBitmap(Bitmap(c, previous_background_), all, (1 - fade) * 0.9f, true);
  if (background_) c.DrawBitmap(Bitmap(c, background_), all, fade * 0.9f, true);
  c.FillGradient(all, kBackground.Alpha(0.45f), kBackground.Alpha(0.92f));
}

void App::DrawHeader(Canvas& c) {
  // The mark, centered on the name's capitals, then the name and the version.
  const float mark_w = DrawLogo(c, Logo(kLogoMark, 30), kMargin, 21.5f);
  const float name_x = kMargin + (mark_w ? mark_w + 12 : 0);
  TextStyle brand = Style(26, DWRITE_FONT_WEIGHT_LIGHT, kText);
  c.Text(L"AnyX360", {name_x, 18, 200, 36}, brand);
  const float brand_w = c.MeasureText(L"AnyX360", brand).width;
  c.Text(Version(), {name_x + brand_w + 12, 30, 260, 20}, Style(12, DWRITE_FONT_WEIGHT_NORMAL, kTextTertiary));

  // Right: who made it, the other language, the terms.
  float x = width_ - kMargin;
  auto link = [&](const std::wstring& text, int id, std::function<void()> action) {
    TextStyle style = Style(13, DWRITE_FONT_WEIGHT_NORMAL, hover_ == id ? kText : kTextSecondary);
    const float w = c.MeasureText(text, style).width;
    const RectF r{x - w, 26, w, 20};
    c.Text(text, r.Inset(0, 0), style);
    if (focus_ == id && focus_visible_) DrawFocus(c, r);
    if (id) AddTarget(r, id, std::move(action));
    x -= w + 22;
  };
  link(Tr(kTextTermsLink), kIdTerms, [this] { ShowTerms(false); });
  link(Tr(kTextOtherLanguage), kIdLanguage, [this] { ToggleLanguage(); });
  // The profile playing, with its gamerpic: its profiles; and switching to another.
  const x360::profiles::Profile* player = Player();
  if (player) link(Tr(kTextSwitchProfile), kIdSwitchProfile, [this] { ShowWhoIsPlaying(false); });
  {
    TextStyle style = Style(14, DWRITE_FONT_WEIGHT_SEMI_BOLD, hover_ == kIdProfile ? kText : Color::Hex(0xE6E6E6));
    const std::wstring text = player ? Widen(player->gamertag) : Tr(kTextProfileLink);
    const float w = c.MeasureText(text, style).width;
    const float pic = player ? 30 : 0;
    const RectF r{x - w - pic, 21, w + pic, 30};
    if (player) DrawGamerpic(c, r.x + 13, r.cy(), 13, player);
    c.Text(text, {r.right() - w, 26, w + 2, 20}, style);
    if (focus_ == kIdProfile && focus_visible_) DrawFocus(c, r);
    AddTarget(r, kIdProfile, [this] { OpenProfiles(); });
    x -= r.w + 22;
  }
  TextStyle by = Style(13, DWRITE_FONT_WEIGHT_NORMAL, kTextTertiary);
  const std::wstring developed = Tr(kTextDeveloped);
  const float by_w = c.MeasureText(developed, by).width;
  c.Text(developed, {x - by_w, 26, by_w, 20}, by);

  c.Text(Tr(kTextMyGames), {kMargin - 2, 58, width_ - 2 * kMargin, 56}, Style(42, DWRITE_FONT_WEIGHT_LIGHT, kText));
}

// The tile size for the window's height: the row, the details and the drawer's bar fit.
static void TileSize(float height, float& tile_w, float& tile_h) {
  tile_h = std::clamp(height - 132 - kDrawerBar - 250, 210.0f, 330.0f);
  tile_w = std::round(tile_h * 2 / 3);
}

void App::DrawRow(Canvas& c) {
  const double now = Now();
  float tile_w, tile_h;
  TileSize(height_, tile_w, tile_h);
  const float row_y = 132 + tile_h * (kSelectedScale - 1) / 2;
  const float slot = tile_w + kTileGap;
  const float view_w = width_ - 2 * kMargin;
  // Keep the selected tile in view.
  float target = row_scroll_target_;
  const float selected_x = selected_ * slot;
  if (selected_x - target < 0) target = selected_x;
  if (selected_x + tile_w - target > view_w) target = selected_x + tile_w - view_w;
  target = std::clamp(target, 0.0f, std::max(0.0f, tiles_.size() * slot - kTileGap - view_w));
  if (target != row_scroll_target_) {
    row_scroll_target_ = target;
    row_scroll_.Go(target, now, 0.2);
  }
  const float scroll = row_scroll_.Value(now);
  const float scale = dpi_ / 96;
  const bool row_focused = focus_ >= kIdTile && focus_ < kIdCompatible;

  // Targets in the row's order; the selected tile drawn last (larger, over its neighbours).
  for (int t = 0; t < int(tiles_.size()); ++t) {
    AddTarget({kMargin + t * slot - scroll, row_y, tile_w, tile_h}, kIdTile + t, [this, t] {
      if (t == selected_) {
        if (SelectedGame() >= 0) Play();
        else OpenAdd();
      } else {
        Select(t);
      }
    });
  }
  std::vector<int> order;
  for (int t = 0; t < int(tiles_.size()); ++t)
    if (t != selected_) order.push_back(t);
  if (selected_ < int(tiles_.size())) order.push_back(selected_);
  for (int t : order) {
    const RectF slot_rect{kMargin + t * slot - scroll, row_y, tile_w, tile_h};
    const bool selected = t == selected_;
    const float grow = selected ? 1 + (kSelectedScale - 1) * selection_.Value(now) : 1;
    const RectF r{slot_rect.cx() - tile_w * grow / 2, slot_rect.cy() - tile_h * grow / 2, tile_w * grow, tile_h * grow};
    if (r.right() < -20 || r.x > width_ + 20) continue;
    const int game = tiles_[size_t(t)];
    if (game < 0) {
      // "Add game".
      c.FillRect(r, hover_ == kIdTile + t ? kPanelHover : kPanel);
      c.StrokeRect(r, kBorder, 1);
      TextStyle plus = Style(72 * grow, DWRITE_FONT_WEIGHT_LIGHT, kTextSecondary, Align::kCenter);
      c.Text(L"+", {r.x, r.y + r.h * 0.28f, r.w, 100 * grow}, plus);
      c.Text(Tr(kTextAddTile), {r.x, r.y + r.h * 0.28f + 96 * grow, r.w, 28},
             Style(16, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText, Align::kCenter));
    } else {
      const LibraryGame& lib = games_[size_t(game)];
      const fs::path path = !lib.cover.empty() ? lib.cover : BoxArt(lib.info->cover);
      std::shared_ptr<const Image> cover = images_.Cover(path, uint32_t(220 * scale), uint32_t(330 * scale));
      if (ID2D1Bitmap* bitmap = Bitmap(c, cover)) {
        c.FillRect(r, kPanel);
        c.DrawBitmap(bitmap, r, 1, true);
      } else {
        // No picture: the icon on the icon's color, the title under it.
        std::shared_ptr<const Image> icon = images_.Picture(lib.icon, uint32_t(128 * scale), uint32_t(128 * scale));
        c.FillGradient(r, icon ? AverageColor(*icon).Mix(kText, 0.08f) : Color::Hex(0x2E3B2E), kPanel);
        const float size = std::min(r.w * 0.55f, 128.0f);
        if (ID2D1Bitmap* bitmap = Bitmap(c, icon)) c.DrawBitmap(bitmap, {r.cx() - size / 2, r.y + r.h * 0.22f, size, size});
        TextStyle title = Style(17, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText, Align::kCenter, true);
        c.Text(lib.info->title, {r.x + 12, r.y + r.h * 0.22f + size + 18, r.w - 24, 52}, title);
      }
      if (lib.location_gone || lib.needs_update) {
        if (lib.location_gone) c.FillRect(r, Color::Hex(0x000000, 0.55f));
        c.FillCircle(r.right() - 22, r.y + 22, 12, kRed);
        c.Text(L"!", {r.right() - 34, r.y + 9, 24, 26}, Style(16, DWRITE_FONT_WEIGHT_BOLD, kText, Align::kCenter));
      }
      if (hover_ == kIdTile + t) c.FillRect(r, Color::Hex(0xFFFFFF, 0.07f));
    }
    if (selected) {
      const float border = row_focused ? 4 : 2;
      c.StrokeRect(r.Inset(-border - 2), row_focused ? kGreenHover : kGreen, border);
    }
  }
  // No games yet: the logo with its name in the row's empty space, beside "Add game".
  if (tiles_.size() == 1) {
    if (std::shared_ptr<const Image> logo = Logo(kLogoFull, tile_h * 0.72f)) {
      const float left = kMargin + tile_w + kTileGap, right = width_ - kMargin;
      const float w = logo->width * 96 / dpi_, h = logo->height * 96 / dpi_;
      DrawLogo(c, logo, (left + right - w) / 2, row_y + (tile_h - h) / 2);
    }
  }
}

void App::DrawDetails(Canvas& c) {
  float tile_w, tile_h;
  TileSize(height_, tile_w, tile_h);
  const float y = 132 + tile_h * kSelectedScale + 26;
  const float w = std::min(960.0f, width_ - 2 * kMargin);
  float below = 0;  // how much further down the buttons and the message go
  const int game = SelectedGame();
  if (game >= 0) {
    const LibraryGame& lib = games_[size_t(game)];
    TextStyle title = Style(34, DWRITE_FONT_WEIGHT_LIGHT, kText);
    c.Text(lib.info->title, {kMargin, y, w, 46}, title);
    if (lib.location_gone) {
      TextStyle gone = Style(15, DWRITE_FONT_WEIGHT_NORMAL, kRedText, Align::kStart, true);
      c.Text(Tr(kTextErrorLocationGone, {lib.info->title, ShortPath(lib.dir)}), {kMargin, y + 50, w, 42}, gone);
    } else if (lib.needs_update) {
      // Played with its title update, which hasn't been added: Play says how to add it.
      TextStyle missing = Style(15, DWRITE_FONT_WEIGHT_NORMAL, kRedText, Align::kStart, true);
      c.Text(Tr(kTextNeedsUpdate, {lib.info->title, lib.info->update_version}), {kMargin, y + 50, w, 42}, missing);
    } else {
      c.Text(Tr(lib.info->webcam ? kTextWebcam : kTextKeyboardPad), {kMargin, y + 50, w, 22},
             Style(15, DWRITE_FONT_WEIGHT_NORMAL, kTextSecondary));
      const bool in_place = Exists(LocationFile(*lib.info));
      std::wstring where = in_place ? Tr(kTextLocationLine, {ShortPath(lib.dir)}) : Tr(kTextExtractedLine);
      std::wstring status = Tr(lib.info->playable ? kTextStatusPlayable : kTextStatusTesting) + L"  ·  " + where;
      // The time the profile playing has played it.
      if (const x360::profiles::Profile* player = Player()) {
        const auto& stats = TitleStats(player->xuid, lib.info->title_id);
        const std::wstring gamertag = Widen(player->gamertag);
        status += L"  ·  " + (stats && !stats->last_played.empty()
                                  ? Tr(kTextPlayed, {gamertag, PlayTime(stats->seconds), ShortDate(stats->last_played)})
                                  : Tr(kTextNotPlayed, {gamertag}));
      }
      c.Text(status, {kMargin, y + 74, w, 20}, Style(13, DWRITE_FONT_WEIGHT_NORMAL, kTextTertiary));
    }
    const RectF play{kMargin, y + 108, 184, 50};
    Button(c, play, Tr(kTextPlay), kIdPlay, [this] { Play(); }, true, !lib.location_gone && !working_);
    more_button_ = {play.right() + 10, play.y, 50, 50};
    const bool hover = hover_ == kIdMore;
    c.FillRect(more_button_, hover ? kPanelHover : kPanelHigh);
    for (int i = -1; i <= 1; ++i) c.FillCircle(more_button_.cx() + i * 9.0f, more_button_.cy(), 2.4f, kText);
    if (focus_ == kIdMore && focus_visible_) DrawFocus(c, more_button_);
    AddTarget(more_button_, kIdMore, [this] { OpenMore(more_button_); });
  } else {
    const bool first = tiles_.size() == 1;
    c.Text(first ? Tr(kTextAddFirst) : Tr(kTextAddTile), {kMargin, y, w, 46}, Style(34, DWRITE_FONT_WEIGHT_LIGHT, kText));
    TextStyle hint = Body();
    // Up to three lines: the formats and the release's games, in the narrowest window.
    c.Text(Tr(kTextAddFirstHint, {JoinTitles(AvailableGames())}), {kMargin, y + 50, w, 70}, hint);
    below = 20;
    Button(c, {kMargin, y + 108 + below, 210, 50}, Tr(kTextAddTile), kIdPlay, [this] { OpenAdd(); }, true, !working_);
  }
  // What the last action ended with.
  if (!message_.empty()) {
    std::wstring text = message_.Render();
    for (const Message& note : notes_) text += L" " + note.Render();
    TextStyle style = Style(14, DWRITE_FONT_WEIGHT_NORMAL, message_.error ? kRedText : kGreenText, Align::kStart, true);
    c.Text(text, {kMargin, y + 172 + below, w, 42}, style);
  }
}

void App::DrawDrawer(Canvas& c) {
  const double now = Now();
  const float open = drawer_.Value(now);
  const float panel_h = std::clamp(height_ * 0.42f, 230.0f, 330.0f);
  const float top = height_ - kDrawerBar - (panel_h - kDrawerBar) * open - (open > 0 ? 0 : 0);
  const float y = std::min(top, height_ - kDrawerBar);
  // What the drawer covers can't be reached.
  targets_.erase(std::remove_if(targets_.begin(), targets_.end(), [y](const Target& t) { return t.rect.cy() > y; }),
                 targets_.end());
  c.FillRect({0, y, width_, height_ - y}, Color::Hex(0x161616));
  c.Line(0, y + 0.5f, width_, y + 0.5f, kBorder, 1);

  // The bar: what the drawer holds, and the chevron.
  const RectF bar{0, y, width_, kDrawerBar};
  if (hover_ == kIdCompatibleBar) c.FillRect(bar, Color::Hex(0xFFFFFF, 0.04f));
  TextStyle title = Style(20, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText);
  title.vertical = Align::kCenter;
  const std::wstring heading = Tr(kTextCompatibleTitle);
  const float heading_w = c.MeasureText(heading, title).width;
  c.Text(heading, {kMargin, y, heading_w + 4, kDrawerBar}, title);
  int playable = 0;
  for (const CompatibleItem& item : compatible_) playable += item.game != nullptr;
  TextStyle count = Style(13, DWRITE_FONT_WEIGHT_NORMAL, kTextTertiary);
  count.vertical = Align::kCenter;
  c.Text(Tr(kTextCompatibleCount, {std::to_wstring(playable), std::to_wstring(compatible_.size() - playable)}),
         {kMargin + heading_w + 16, y + 2, 360, kDrawerBar}, count);
  const float chevron_x = kMargin + heading_w + 16 + c.MeasureText(Tr(kTextCompatibleCount, {L"0", L"00"}), count).width + 26;
  const float cy = y + kDrawerBar / 2, d = 5 * (open > 0.5f ? 1 : -1);
  c.Line(chevron_x - 7, cy - d, chevron_x, cy + d, kTextSecondary, 1.6f);
  c.Line(chevron_x, cy + d, chevron_x + 7, cy - d, kTextSecondary, 1.6f);
  if (focus_ == kIdCompatibleBar && focus_visible_) DrawFocus(c, {kMargin - 8, y + 10, chevron_x - kMargin + 24, kDrawerBar - 20});
  AddTarget({0, y, std::min(width_ * 0.6f, chevron_x + 40), kDrawerBar}, kIdCompatibleBar, [this] { ToggleCompatible(); });
  DrawHints(c, y + kDrawerBar);
  if (!compatible_open_ && open <= 0.01f) return;

  // The covers: this release's games, then the coming ones.
  const float cover_h = std::max(120.0f, panel_h - kDrawerBar - 64);
  const float cover_w = std::round(cover_h * 2 / 3);
  const float slot = cover_w + 14;
  const float row_y = y + kDrawerBar + 4;
  float x = 0;
  std::vector<float> xs;
  for (size_t i = 0; i < compatible_.size(); ++i) {
    if (i > 0 && !compatible_[i].game && compatible_[i - 1].game) x += 26;  // a gap before "coming soon"
    xs.push_back(x);
    x += slot;
  }
  const float view_w = width_ - 2 * kMargin;
  if (focus_ >= kIdCompatible && focus_ < kIdButton) {
    const size_t i = size_t(focus_ - kIdCompatible);
    float target = drawer_scroll_target_;
    if (i < xs.size()) {
      if (xs[i] - target < 0) target = xs[i];
      if (xs[i] + cover_w - target > view_w) target = xs[i] + cover_w - view_w;
    }
    if (target != drawer_scroll_target_) {
      drawer_scroll_target_ = target;
      drawer_scroll_.Go(target, now, 0.2);
    }
  }
  const float scroll = drawer_scroll_.Value(now);
  const float scale = dpi_ / 96;
  c.PushClip({0, row_y - 6, width_, height_ - row_y + 6});
  for (size_t i = 0; i < compatible_.size(); ++i) {
    const CompatibleItem& item = compatible_[i];
    const RectF r{kMargin + xs[i] - scroll, row_y, cover_w, cover_h};
    const int id = kIdCompatible + int(i);
    if (compatible_open_) {
      AddTarget(r, id, [this, i] {
        const CompatibleItem& chosen = compatible_[i];
        if (!chosen.game) return;
        if (chosen.installed) {
          for (size_t t = 0; t < tiles_.size(); ++t)
            if (tiles_[t] >= 0 && games_[size_t(tiles_[t])].info == chosen.game) Select(int(t));
          ToggleCompatible();
        } else {
          OpenAdd();
        }
      });
    }
    if (r.right() < 0 || r.x > width_) continue;
    const float opacity = item.game ? 1.0f : 0.42f;
    std::shared_ptr<const Image> cover = images_.Cover(item.cover, uint32_t(cover_w * scale), uint32_t(cover_h * scale));
    if (ID2D1Bitmap* bitmap = Bitmap(c, cover)) {
      c.FillRect(r, kPanel);
      c.DrawBitmap(bitmap, r, opacity, true);
    } else {
      c.FillRect(r, kPanelHigh.Alpha(opacity));
      TextStyle name = Style(15, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText.Alpha(opacity), Align::kCenter, true);
      name.vertical = Align::kCenter;
      c.Text(item.title, r.Inset(12), name);
    }
    if (hover_ == id) c.FillRect(r, Color::Hex(0xFFFFFF, 0.07f));
    if (focus_ == id && focus_visible_) DrawFocus(c, r);
    c.Text(item.title, {r.x, r.bottom() + 6, r.w, 18}, Style(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, item.game ? kText : kTextSecondary));
    const Text status = !item.game ? kTextStatusComing : item.needs_update ? kTextStatusNeedsUpdate
                        : item.installed ? kTextStatusInLibrary : item.playable ? kTextStatusPlayable : kTextStatusTesting;
    c.Text(Tr(status), {r.x, r.bottom() + 24, r.w, 16},
           Style(12, DWRITE_FONT_WEIGHT_NORMAL, item.needs_update ? kRedText : item.installed ? kGreenText : kTextTertiary));
  }
  c.PopClip();
}

void App::DrawHints(Canvas& c, float bottom) {
  if (!pad_used_) return;
  // The controller's buttons, right to left: A select, B back, Y more, X compatible games.
  struct Hint {
    wchar_t button;
    uint32_t color;
    Text text;
  };
  const Hint hints[] = {{L'X', 0x3B8EDB, kTextHintCompatible}, {L'Y', 0xE8B923, kTextHintMore},
                        {L'B', 0xD9433B, kTextHintBack}, {L'A', 0x5DB33A, kTextHintSelect}};
  float x = width_ - kMargin;
  const float cy = bottom - kDrawerBar / 2;
  for (const Hint& hint : hints) {
    TextStyle label = Style(13, DWRITE_FONT_WEIGHT_NORMAL, kTextSecondary);
    label.vertical = Align::kCenter;
    const std::wstring text = Tr(hint.text);
    const float w = c.MeasureText(text, label).width;
    c.Text(text, {x - w, cy - 10, w + 2, 20}, label);
    x -= w + 8;
    c.FillCircle(x - 10, cy, 10, Color::Hex(hint.color));
    TextStyle letter = Style(11, DWRITE_FONT_WEIGHT_BOLD, kText, Align::kCenter);
    letter.vertical = Align::kCenter;
    c.Text(std::wstring(1, hint.button), {x - 20, cy - 10, 20, 20}, letter);
    x -= 20 + 22;
  }
}

void App::DrawOverlay(Canvas& c, Overlay& overlay, bool top) {
  const double now = Now();
  const float fade = overlay.fade.Value(now);
  c.FillRect({0, 0, width_, height_}, Color::Hex(0x000000, 0.62f * fade));
  const float column_w = std::min(760.0f, width_ - 2 * kMargin);
  const float x = (width_ - column_w) / 2;

  std::wstring text = overlay.text;
  if (overlay.kind == OverlayKind::kProgress) {
    const std::wstring title = job_game_ ? job_game_->title : L"";
    const std::wstring percent = std::to_wstring(std::max(percent_, 0));
    text = step_ == Step::kRemoving                    ? Tr(kTextRemoving, {title})
           : step_ == Step::kFinishing                 ? Tr(kTextFinishing)
           : step_ == Step::kSaving                    ? Tr(kTextSaving, {title, percent})
           : step_ == Step::kVerifying                 ? Tr(kTextVerifying, {title, percent})
           : step_ == Step::kExtracting && job_game_   ? Tr(kTextExtracting, {title, percent})
                                                       : Tr(kTextChecking);
  }
  const float text_h = text.empty() ? 0 : c.MeasureText(text, Body(), column_w).height;
  float extra = 0;
  if (overlay.kind == OverlayKind::kAdd) extra = 150 + 34;
  if (overlay.kind == OverlayKind::kProgress) extra = 40;
  if (overlay.kind == OverlayKind::kPassword) extra = 64 + 26 + (overlay.wrong ? 22 : 0);
  const float profile_row = 56;
  if (overlay.kind == OverlayKind::kProfiles) extra = (profiles_.size() + 1) * profile_row + (Player() ? 14 + 44 : 0);
  if (overlay.kind == OverlayKind::kGamertag) extra = 46 + 16 + 5 * 50 - 8;
  const float band_h = 40 + 48 + 14 + text_h + (extra ? 22 + extra : 0) + 30 + 42 + 40;
  const float band_y = std::round((height_ - band_h) / 2 + (1 - fade) * 14);
  c.FillRect({0, band_y, width_, band_h}, kPanel.Alpha(fade));
  c.Line(0, band_y + 0.5f, width_, band_y + 0.5f, kBorder.Alpha(fade));
  c.Line(0, band_y + band_h - 0.5f, width_, band_y + band_h - 0.5f, kBorder.Alpha(fade));

  float y = band_y + 40;
  c.FillRect({x - 18, y + 8, 4, 32}, (overlay.error ? kRed : kGreen).Alpha(fade));
  c.Text(overlay.kind == OverlayKind::kProgress
             ? (step_ == Step::kRemoving ? Tr(kTextRemovingTitle, {job_game_ ? job_game_->title : L""})
                : saving_ && job_game_ ? Tr(kTextSavingTitle, {job_game_->title})
                : job_game_            ? Tr(kTextAddingTitle, {job_game_->title})
                                       : Tr(kTextCheckingTitle))
             : overlay.title,
         {x, y, column_w, 48}, Style(34, DWRITE_FONT_WEIGHT_LIGHT, kText.Alpha(fade)));
  y += 48 + 14;
  if (!text.empty()) {
    TextStyle body = Body();
    body.color = (overlay.error ? kRedText : kTextSecondary).Alpha(fade);
    c.Text(text, {x, y, column_w, text_h + 4}, body);
    y += text_h;
  }
  if (extra) y += 22;

  if (overlay.kind == OverlayKind::kAdd) {
    const float card_w = (column_w - 16) / 2, card_h = 150;
    const struct {
      const wchar_t* glyph;
      Text title, hint;
      bool folder;
    } choices[] = {{L"", kTextFromFile, kTextFromFileHint, false}, {L"", kTextFromFolder, kTextFromFolderHint, true}};
    for (int i = 0; i < 2; ++i) {
      const RectF r{x + i * (card_w + 16), y, card_w, card_h};
      const int id = kIdChoice + i;
      c.FillRect(r, (hover_ == id ? kPanelHover : kPanelHigh).Alpha(fade));
      TextStyle glyph = Style(28, DWRITE_FONT_WEIGHT_NORMAL, kGreenText.Alpha(fade));
      glyph.family = L"Segoe MDL2 Assets";
      c.Text(choices[i].glyph, {r.x + 20, r.y + 18, 40, 36}, glyph);
      c.Text(Tr(choices[i].title), {r.x + 20, r.y + 62, r.w - 40, 26}, Style(19, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText.Alpha(fade)));
      TextStyle hint = Style(13, DWRITE_FONT_WEIGHT_NORMAL, kTextSecondary.Alpha(fade), Align::kStart, true);
      c.Text(Tr(choices[i].hint), {r.x + 20, r.y + 92, r.w - 40, 48}, hint);
      if (focus_ == id && focus_visible_) DrawFocus(c, r);
      if (top) AddTarget(r, id, [this, folder = choices[i].folder] { ChooseFileToAdd(folder); });
    }
    c.Text(Tr(kTextDropHint), {x, y + card_h + 14, column_w, 20}, Style(13, DWRITE_FONT_WEIGHT_NORMAL, kTextTertiary.Alpha(fade)));
    y += extra;
  } else if (overlay.kind == OverlayKind::kProgress) {
    const RectF bar{x, y + 10, column_w, 6};
    c.FillRect(bar, Color::Hex(0x3A3A3A, fade));
    if (percent_ >= 0) {
      c.FillRect({bar.x, bar.y, bar.w * std::min(percent_, 100) / 100.0f, bar.h}, kGreen.Alpha(fade));
    } else {
      // Not known yet: the bar breathes, in place (a segment starting again from the left looked
      // like the progress restarting).
      const float pulse = 0.5f + 0.5f * float(std::sin(now * 3.0));
      c.FillRect(bar, kGreen.Alpha(fade * (0.15f + 0.35f * pulse)));
    }
    y += extra;
  } else if (overlay.kind == OverlayKind::kPassword) {
    const RectF field{x, y, column_w, 46};
    const bool focused = focus_ == kIdPassword;
    c.FillRect(field, Color::Hex(0x0F0F0F, fade));
    c.StrokeRect(field, (focused ? kGreen : Color::Hex(0x444444)).Alpha(fade), focused ? 2.0f : 1.0f);
    TextStyle dots = Style(18, DWRITE_FONT_WEIGHT_NORMAL, kText.Alpha(fade));
    dots.vertical = Align::kCenter;
    const std::wstring shown(overlay.typed.size(), L'●');
    c.Text(shown, field.Inset(14, 0), dots);
    if (focused) {
      const float caret = field.x + 14 + c.MeasureText(shown, dots).width + 2;
      c.Line(caret, field.y + 11, caret, field.bottom() - 11, kText.Alpha(fade), 1.5f);
    }
    if (top) AddTarget(field, kIdPassword, [] {});
    float note_y = field.bottom() + 10;
    if (overlay.wrong) {
      c.Text(Tr(kTextErrorWrongPassword), {x, note_y, column_w, 20}, Style(14, DWRITE_FONT_WEIGHT_NORMAL, kRedText.Alpha(fade)));
      note_y += 22;
    }
    c.Text(Tr(kTextPasswordKeyboard), {x, note_y, column_w, 20}, Style(13, DWRITE_FONT_WEIGHT_NORMAL, kTextTertiary.Alpha(fade)));
    y += extra;
  } else if (overlay.kind == OverlayKind::kProfiles) {
    float row_y = y;
    TextStyle name = Style(17, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText.Alpha(fade));
    name.vertical = Align::kCenter;
    TextStyle badge = Style(13, DWRITE_FONT_WEIGHT_SEMI_BOLD, kGreenText.Alpha(fade), Align::kEnd);
    badge.vertical = Align::kCenter;
    TextStyle arrow = Style(18, DWRITE_FONT_WEIGHT_NORMAL, kTextSecondary.Alpha(fade), Align::kEnd);
    arrow.vertical = Align::kCenter;
    for (size_t i = 0; i <= profiles_.size(); ++i, row_y += profile_row) {
      const RectF r{x, row_y, column_w, profile_row - 6};
      const int id = kIdProfileRow + int(i);
      c.FillRect(r, (hover_ == id ? kPanelHover : kPanelHigh).Alpha(fade));
      if (i < profiles_.size()) {
        const x360::profiles::Profile& p = profiles_[i];
        DrawGamerpic(c, r.x + 30, r.cy(), 17, &p, fade);
        c.Text(Widen(p.gamertag), {r.x + 60, r.y, r.w - 260, r.h}, name);
        std::wstring signed_in;
        for (int slot = 0; slot < x360::profile::kPlayers; ++slot)
          if (sign_in_.slots[slot] == p.xuid)
            signed_in = slot == 0 ? Tr(kTextBadgePlaying) : Tr(kTextPlayerName, {std::to_wstring(slot + 1)});
        if (!signed_in.empty()) {
          if (signed_in != Tr(kTextBadgePlaying)) badge.color = kTextSecondary.Alpha(fade);
          c.Text(signed_in, {r.right() - 240, r.y, 200, r.h}, badge);
          badge.color = kGreenText.Alpha(fade);
        }
        c.Text(L"›", r.Inset(18, 0), arrow);
        if (top) AddTarget(r, id, [this, r, xuid = p.xuid] { OpenProfileMenu(r, xuid); });
      } else {
        TextStyle plus = Style(22, DWRITE_FONT_WEIGHT_LIGHT, kGreenText.Alpha(fade), Align::kCenter);
        plus.vertical = Align::kCenter;
        c.Text(L"+", {r.x + 13, r.y, 34, r.h}, plus);
        c.Text(Tr(kTextCreateProfile), {r.x + 60, r.y, r.w - 80, r.h}, name);
        if (top) AddTarget(r, id, [this] { OpenGamertag(0); });
      }
      if (focus_ == id && focus_visible_) DrawFocus(c, r);
    }
    // The games' language, the profile playing's: Enter, A or a click goes to the next one.
    if (const x360::profiles::Profile* player = Player()) {
      row_y += 14 - 6;
      const float label_w = 260;
      TextStyle label = Style(15, DWRITE_FONT_WEIGHT_NORMAL, kTextSecondary.Alpha(fade));
      label.vertical = Align::kCenter;
      TextStyle value = Style(17, DWRITE_FONT_WEIGHT_NORMAL, kText.Alpha(fade));
      value.vertical = Align::kCenter;
      c.Text(Tr(kTextGameLanguage) + L" (" + Widen(player->gamertag) + L")", {x, row_y, label_w, 44}, label);
      const RectF chooser{x + label_w, row_y, column_w - label_w, 44};
      c.FillRect(chooser, (hover_ == kIdProfileLanguage ? kPanelHover : Color::Hex(0x0F0F0F)).Alpha(fade));
      c.StrokeRect(chooser, (focus_ == kIdProfileLanguage ? kGreen : Color::Hex(0x444444)).Alpha(fade),
                   focus_ == kIdProfileLanguage ? 2.0f : 1.0f);
      c.Text(LanguageLabel(player->language), chooser.Inset(14, 0), value);
      c.Text(L"›", chooser.Inset(14, 0), arrow);
      if (top) {
        AddTarget(chooser, kIdProfileLanguage, [this] {
          const x360::profiles::Profile* playing = Player();
          if (!playing) return;
          std::vector<std::string> tags = {"auto"};
          for (const auto& language : x360::profile::Languages()) tags.push_back(language.tag);
          auto at = std::find(tags.begin(), tags.end(), playing->language);
          x360::profiles::Profile changed = *playing;
          changed.language = (at == tags.end() || at + 1 == tags.end()) ? tags[0] : *(at + 1);
          // The profile's, and profile.txt's for a game started without it.
          x360::profile::SignIn sign_in = x360::profile::Read(x360::profile::File(ProfilesRoot()));
          sign_in.language = changed.language;
          if (!x360::profiles::Save(ProfilesRoot(), changed) ||
              !x360::profile::Write(x360::profile::File(ProfilesRoot()), sign_in))
            return ShowMessage(Tr(kTextProblemTitle), Tr(kTextErrorProfileWrite, {ShortPath(ProfilesRoot())}), true);
          ReloadProfiles();
        });
      }
    }
    y += extra;
  } else if (overlay.kind == OverlayKind::kGamertag) {
    // The gamertag typed, with a caret, then the on-screen keyboard.
    const RectF field{x, y, column_w, 46};
    c.FillRect(field, Color::Hex(0x0F0F0F, fade));
    c.StrokeRect(field, kGreen.Alpha(fade), 2.0f);
    TextStyle value = Style(20, DWRITE_FONT_WEIGHT_NORMAL, kText.Alpha(fade));
    value.vertical = Align::kCenter;
    c.Text(overlay.typed, field.Inset(14, 0), value);
    const float caret = field.x + 14 + c.MeasureText(overlay.typed, value).width + 2;
    c.Line(caret, field.y + 11, caret, field.bottom() - 11, kText.Alpha(fade), 1.5f);
    if (top) AddTarget(field, kIdGamertagField, [] {}, false);
    const float gap = 6, key_h = 44;
    const float unit = std::floor((column_w - gap * 9) / 10);
    float key_y = field.bottom() + 16;
    TextStyle key_text = Style(17, DWRITE_FONT_WEIGHT_SEMI_BOLD, kText.Alpha(fade), Align::kCenter);
    key_text.vertical = Align::kCenter;
    auto key = [&](const RectF& r, int id, const std::wstring& label, std::function<void()> action, bool on = false) {
      c.FillRect(r, (on ? Color::Hex(0x2F4A2F) : hover_ == id ? kPanelHover : kPanelHigh).Alpha(fade));
      c.Text(label, r, key_text);
      if (focus_ == id && focus_visible_) DrawFocus(c, r);
      if (top) AddTarget(r, id, std::move(action));
    };
    for (int row = 0; row < 4; ++row, key_y += key_h + gap) {
      const std::wstring keys = kKeyRows[row];
      const float row_x = x + (column_w - (keys.size() * (unit + gap) - gap)) / 2;
      for (size_t k = 0; k < keys.size(); ++k) {
        const wchar_t ch = overlay.shift ? keys[k] : wchar_t(std::towlower(keys[k]));
        key({row_x + k * (unit + gap), key_y, unit, key_h}, kIdKey + row * 16 + int(k), std::wstring(1, ch),
            [this, ch] { TypeGamertag(ch); });
      }
    }
    const float wide = (column_w - 2 * gap) / 4;
    key({x, key_y, wide, key_h}, kIdKey + 4 * 16 + kKeyShift, Tr(kTextKeyShift), [this] {
      overlays_.back().shift = !overlays_.back().shift;
    }, overlay.shift);
    key({x + wide + gap, key_y, wide * 2, key_h}, kIdKey + 4 * 16 + kKeySpace, Tr(kTextKeySpace), [this] { TypeGamertag(L' '); });
    key({x + wide * 3 + gap * 2, key_y, wide, key_h}, kIdKey + 4 * 16 + kKeyErase, Tr(kTextKeyErase), [this] { TypeGamertag(L'\b'); });
    y += extra;
  }

  // Buttons, right-aligned; the first is the default.
  y += 30;
  float bx = x + column_w;
  for (int i = int(overlay.buttons.size()) - 1; i >= 0; --i) {
    const Overlay::Action& action = overlay.buttons[size_t(i)];
    const float w = std::max(132.0f, c.MeasureText(action.label, Style(16, DWRITE_FONT_WEIGHT_SEMI_BOLD, kText)).width + 44);
    const RectF r{bx - w, y, w, 42};
    if (top) {
      Button(c, r, action.label, kIdButton + i, action.run, i == 0 && overlay.primary, action.enabled);
    } else {
      c.FillRect(r, i == 0 && overlay.primary ? kGreen : kPanelHigh);
    }
    bx -= w + 10;
  }
}

void App::DrawMenu(Canvas& c, Overlay& overlay, bool top) {
  const float fade = overlay.fade.Value(Now());
  c.FillRect({0, 0, width_, height_}, Color::Hex(0x000000, 0.35f * fade));
  const float item_h = 46, w = 320;
  const float h = item_h * overlay.items.size() + 12;
  float x = std::clamp(overlay.anchor.x, kMargin, width_ - kMargin - w);
  float y = overlay.anchor.y - h - 10 + (1 - fade) * 8;
  if (y < 60) y = overlay.anchor.bottom() + 10;
  c.FillRect({x, y, w, h}, kPanelHigh.Alpha(fade));
  c.StrokeRect({x, y, w, h}, kBorder.Alpha(fade));
  for (size_t i = 0; i < overlay.items.size(); ++i) {
    const Overlay::Action& item = overlay.items[i];
    const RectF r{x + 6, y + 6 + i * item_h, w - 12, item_h};
    const int id = kIdMenuItem + int(i);
    if (hover_ == id && item.enabled) c.FillRect(r, kPanelHover);
    TextStyle label = Style(15, DWRITE_FONT_WEIGHT_NORMAL, (item.enabled ? kText : Color::Hex(0x5E5E5E)).Alpha(fade));
    label.vertical = Align::kCenter;
    c.Text(item.label, r.Inset(14, 0), label);
    if (focus_ == id && focus_visible_) c.StrokeRect(r, kText, 2);
    if (top) AddTarget(r, id, item.enabled ? item.run : std::function<void()>(), item.enabled);
  }
}

void App::DrawTerms(Canvas& c, Overlay& overlay, bool top) {
  const float fade = overlay.fade.Value(Now());
  c.FillRect({0, 0, width_, height_}, Color::Hex(0x000000, 0.7f * fade));
  const float column_w = std::min(820.0f, width_ - 2 * kMargin);
  const float x = (width_ - column_w) / 2;
  const RectF band{0, 36, width_, height_ - 72};
  c.FillRect(band, kPanel.Alpha(fade));
  float y = band.y + 30;
  c.FillRect({x - 18, y + 8, 4, 32}, kGreen.Alpha(fade));
  c.Text(Tr(kTextTermsHeading), {x, y, column_w, 48}, Style(34, DWRITE_FONT_WEIGHT_LIGHT, kText.Alpha(fade)));
  // The mark at the heading's other end, like a letterhead.
  if (std::shared_ptr<const Image> mark = Logo(kLogoMark, 40))
    DrawLogo(c, mark, x + column_w - mark->width * 96 / dpi_, y + 4, fade);
  y += 64;
  const float footer = overlay.require_acceptance ? 104 : 66;
  const RectF view{x, y, column_w, band.bottom() - footer - y};
  TextStyle text = Style(14, DWRITE_FONT_WEIGHT_NORMAL, Color::Hex(0xD2D2D2, fade), Align::kStart, true);
  text.ellipsis = false;
  overlay.content = c.MeasureText(overlay.text, text, column_w - 18).height;
  overlay.view = view.h;
  overlay.scroll = std::clamp(overlay.scroll, 0.0f, std::max(0.0f, overlay.content - view.h));
  c.PushClip(view);
  c.Text(overlay.text, {view.x, view.y - overlay.scroll, column_w - 18, overlay.content + 4}, text);
  c.PopClip();
  if (overlay.content > view.h) {  // the scroll bar
    const float thumb = std::max(30.0f, view.h * view.h / overlay.content);
    const float ty = view.y + (view.h - thumb) * overlay.scroll / (overlay.content - view.h);
    c.FillRect({view.right() - 4, view.y, 4, view.h}, Color::Hex(0x2E2E2E));
    c.FillRect({view.right() - 4, ty, 4, thumb}, Color::Hex(0x777777));
  }
  if (focus_ == kIdTermsText && focus_visible_) DrawFocus(c, view);
  if (top) AddTarget(view, kIdTermsText, [] {});
  y = view.bottom() + 18;
  if (overlay.require_acceptance) {
    const RectF box{x, y, 22, 22};
    c.StrokeRect(box, overlay.checked ? kGreen : Color::Hex(0x8A8A8A), 2);
    if (overlay.checked) {
      c.FillRect(box.Inset(2), kGreen);
      c.Line(box.x + 5, box.y + 11, box.x + 9, box.y + 16, kText, 2);
      c.Line(box.x + 9, box.y + 16, box.x + 17, box.y + 6, kText, 2);
    }
    TextStyle label = Style(15, DWRITE_FONT_WEIGHT_NORMAL, kText);
    label.vertical = Align::kCenter;
    const std::wstring check = Tr(kTextTermsCheck);
    const float label_w = c.MeasureText(check, label).width;
    c.Text(check, {box.right() + 12, y - 2, label_w + 4, 26}, label);
    const RectF row{box.x, box.y - 4, label_w + 40, 30};
    if (focus_ == kIdTermsCheck && focus_visible_) DrawFocus(c, row);
    if (top) AddTarget(row, kIdTermsCheck, [this] {
      Overlay& terms = overlays_.back();
      terms.checked = !terms.checked;
      terms.buttons[0].enabled = terms.checked;
      host_.Invalidate();
    });
    y += 38;
  }
  float bx = x + column_w;
  for (int i = int(overlay.buttons.size()) - 1; i >= 0; --i) {
    const Overlay::Action& action = overlay.buttons[size_t(i)];
    const float w = std::max(132.0f, c.MeasureText(action.label, Style(16, DWRITE_FONT_WEIGHT_SEMI_BOLD, kText)).width + 44);
    if (top) Button(c, {bx - w, y, w, 42}, action.label, kIdButton + i, action.run, i == 0, action.enabled);
    bx -= w + 10;
  }
}

// ---- input ----------------------------------------------------------------------------------

const App::Target* App::TargetAt(float x, float y) const {
  for (auto it = targets_.rbegin(); it != targets_.rend(); ++it)
    if (it->rect.Contains(x, y)) return &*it;
  return nullptr;
}

const App::Target* App::Focused() const {
  for (const Target& target : targets_)
    if (target.id == focus_ && target.focusable) return &target;
  return nullptr;
}

void App::FocusFirst() {
  auto exists = [&](int id) {
    return std::any_of(targets_.begin(), targets_.end(), [&](const Target& t) { return t.id == id && t.focusable; });
  };
  std::vector<int> preferred;
  if (!overlays_.empty()) {
    switch (overlays_.back().kind) {
      case OverlayKind::kPassword: preferred = {kIdPassword}; break;
      case OverlayKind::kWho: preferred = {kIdWhoTile}; break;
      case OverlayKind::kProfiles: preferred = {kIdProfileRow}; break;
      case OverlayKind::kGamertag: preferred = {kIdKey + 16}; break;
      case OverlayKind::kAdd: preferred = {kIdChoice}; break;
      case OverlayKind::kMenu:
        for (int i = 0; i < 8; ++i) preferred.push_back(kIdMenuItem + i);
        break;
      case OverlayKind::kTerms: preferred = {kIdTermsCheck, kIdButton}; break;
      default: preferred = {kIdButton}; break;
    }
  } else {
    preferred = {kIdTile + selected_, kIdTile};
  }
  for (int id : preferred) {
    if (exists(id)) {
      focus_ = id;
      return;
    }
  }
  for (const Target& target : targets_) {
    if (target.focusable) {
      focus_ = target.id;
      return;
    }
  }
}

void App::Move(Key direction) {
  const Target* from = Focused();
  if (!from) return FocusFirst();
  auto exists = [&](int id) {
    return std::any_of(targets_.begin(), targets_.end(), [&](const Target& t) { return t.id == id && t.focusable; });
  };
  const bool on_tile = focus_ >= kIdTile && focus_ < kIdCompatible;
  if (on_tile && direction == Key::kDown && exists(kIdPlay)) {
    focus_ = kIdPlay;
    return host_.Invalidate();
  }
  if ((focus_ == kIdPlay || focus_ == kIdMore) && direction == Key::kUp && exists(kIdTile + selected_)) {
    focus_ = kIdTile + selected_;
    return host_.Invalidate();
  }
  const float fx = from->rect.cx(), fy = from->rect.cy();
  const Target* best = nullptr;
  float best_score = 1e9f;
  for (const Target& t : targets_) {
    if (!t.focusable || t.id == from->id) continue;
    const float dx = t.rect.cx() - fx, dy = t.rect.cy() - fy;
    float along = 0, across = 0;
    switch (direction) {
      case Key::kLeft: along = -dx, across = dy; break;
      case Key::kRight: along = dx, across = dy; break;
      case Key::kUp: along = -dy, across = dx; break;
      case Key::kDown: along = dy, across = dx; break;
      default: return;
    }
    if (along <= 1) continue;
    const bool horizontal = direction == Key::kLeft || direction == Key::kRight;
    if (horizontal && std::abs(across) > std::max(from->rect.h, t.rect.h)) continue;  // another row
    const float score = along + 2.5f * std::abs(across);
    if (score < best_score) best_score = score, best = &t;
  }
  if (!best) return;
  focus_ = best->id;
  if (focus_ >= kIdTile && focus_ < kIdCompatible) Select(focus_ - kIdTile);
  host_.Invalidate();
}

void App::Activate(int id) {
  for (const Target& target : targets_) {
    if (target.id != id) continue;
    if (target.action) {
      std::function<void()> action = target.action;  // it may change targets_
      action();
      host_.Invalidate();
    }
    return;
  }
}

void App::Back() {
  if (!overlays_.empty()) {
    if (std::function<void()> back = overlays_.back().back) back();
    host_.Invalidate();
    return;
  }
  if (compatible_open_) ToggleCompatible();
}

void App::MouseMove(float x, float y) {
  mouse_x_ = x, mouse_y_ = y;
  const Target* target = TargetAt(x, y);
  const int hover = target && target->action ? target->id : -1;
  if (hover != hover_) {
    hover_ = hover;
    host_.Invalidate();
  }
}

void App::MouseLeave() {
  mouse_x_ = mouse_y_ = -1;
  if (hover_ != -1) hover_ = -1, host_.Invalidate();
}

void App::MouseDown(float x, float y) {
  focus_visible_ = false;
  pad_used_ = false;
  const Target* target = TargetAt(x, y);
  pressed_ = target ? target->id : -1;
  if (target && target->focusable) focus_ = target->id;
  host_.Invalidate();
}

void App::MouseUp(float x, float y) {
  const Target* target = TargetAt(x, y);
  if (target && target->id == pressed_) Activate(target->id);
  pressed_ = -1;
}

void App::Wheel(float x, float y, float notches) {
  if (!overlays_.empty()) {
    Overlay& overlay = overlays_.back();
    if (overlay.kind == OverlayKind::kTerms) overlay.scroll -= notches * 60, host_.Invalidate();
    return;
  }
  const float drawer_top = height_ - kDrawerBar - (std::clamp(height_ * 0.42f, 230.0f, 330.0f) - kDrawerBar) * drawer_.to;
  if (compatible_open_ && y > drawer_top + kDrawerBar) {
    drawer_scroll_target_ = std::max(0.0f, drawer_scroll_target_ - notches * 160);
    drawer_scroll_.Go(drawer_scroll_target_, Now(), 0.2);
  } else {
    Select(selected_ - int(notches));
  }
  host_.Invalidate();
}

void App::RightClick(float x, float y) {
  const Target* target = TargetAt(x, y);
  if (!overlays_.empty() || !target || target->id < kIdTile || target->id >= kIdCompatible) return;
  Select(target->id - kIdTile);
  focus_ = target->id;
  if (SelectedGame() >= 0) OpenMore({x, y, 0, 0});
}

void App::PadConnected() {
  if (pad_used_) return;
  pad_used_ = true;
  host_.Invalidate();
}

void App::KeyPressed(Key key, bool from_pad) {
  focus_visible_ = true;
  pad_used_ = from_pad;
  // The terms' text scrolls with up and down while it has the focus.
  if (focus_ == kIdTermsText && !overlays_.empty() && (key == Key::kUp || key == Key::kDown)) {
    overlays_.back().scroll += key == Key::kDown ? 60.0f : -60.0f;
    host_.Invalidate();
    return;
  }
  switch (key) {
    case Key::kLeft:
    case Key::kRight:
    case Key::kUp:
    case Key::kDown: Move(key); break;
    case Key::kActivate:
      if (focus_ == kIdPassword && !overlays_.empty() && !overlays_.back().buttons.empty())
        overlays_.back().buttons[0].run();
      else
        Activate(focus_);
      break;
    case Key::kBack: Back(); break;
    case Key::kMore:
      if (!overlays_.empty() && overlays_.back().kind == OverlayKind::kGamertag) overlays_.back().shift = !overlays_.back().shift;
      else if (overlays_.empty() && SelectedGame() >= 0) OpenMore(more_button_);
      break;
    case Key::kCompatible:
      if (!overlays_.empty() && overlays_.back().kind == OverlayKind::kGamertag) TypeGamertag(L' ');
      else if (overlays_.empty()) ToggleCompatible();
      break;
    case Key::kNext:
    case Key::kPrevious: {
      std::vector<int> order;
      for (const Target& t : targets_)
        if (t.focusable && std::find(order.begin(), order.end(), t.id) == order.end()) order.push_back(t.id);
      if (order.empty()) break;
      auto at = std::find(order.begin(), order.end(), focus_);
      int index = at == order.end() ? 0 : int(at - order.begin()) + (key == Key::kNext ? 1 : -1);
      focus_ = order[size_t((index + int(order.size())) % int(order.size()))];
      if (focus_ >= kIdTile && focus_ < kIdCompatible) Select(focus_ - kIdTile);
      break;
    }
    case Key::kHome:
    case Key::kEnd:
      if (overlays_.empty()) {
        Select(key == Key::kHome ? 0 : int(tiles_.size()) - 1);
        focus_ = kIdTile + selected_;
      }
      break;
    case Key::kPlay:
      if (!overlays_.empty() && overlays_.back().kind == OverlayKind::kGamertag) SubmitGamertag();
      else if (overlays_.empty()) Play();
      break;
  }
  host_.Invalidate();
}

bool App::WantsText() const {
  if (overlays_.empty()) return false;
  const OverlayKind kind = overlays_.back().kind;
  return (kind == OverlayKind::kPassword && focus_ == kIdPassword) || kind == OverlayKind::kGamertag;
}

void App::Character(wchar_t ch) {
  if (!WantsText()) return;
  Overlay& overlay = overlays_.back();
  if (overlay.kind == OverlayKind::kGamertag) {
    if (ch == L'\r') return SubmitGamertag();
    if (ch == 27) return PopOverlay();
    if (ch == L'\b' || NameCharacter(ch)) TypeGamertag(ch);
    return host_.Invalidate();
  }
  if (ch == L'\b') {
    if (!overlay.typed.empty()) overlay.typed.pop_back();
  } else if (ch == L'\r') {
    if (!overlay.buttons.empty()) overlay.buttons[0].run();
  } else if (ch == 27) {
    Back();
  } else if (ch >= 32 && overlay.typed.size() < 256) {
    overlay.typed += ch;
  }
  host_.Invalidate();
}

void App::Paste(const std::wstring& text) {
  if (!WantsText()) return;
  if (overlays_.back().kind == OverlayKind::kGamertag) {
    for (wchar_t ch : text)
      if (NameCharacter(ch)) Character(ch);
    return;
  }
  for (wchar_t ch : text)
    if (ch >= 32 && overlays_.back().typed.size() < 256) overlays_.back().typed += ch;
  host_.Invalidate();
}

void App::SetDropHover(bool hover) {
  drop_hover_ = hover && !working_;
  host_.Invalidate();
}

// ---- commands -------------------------------------------------------------------------------

void App::PushOverlay(Overlay overlay) {
  overlay.saved_focus = focus_;
  overlay.fade = {};
  overlay.fade.Go(1, Now(), 0.16);
  overlays_.push_back(std::move(overlay));
  focus_ = -1;
  host_.Invalidate();
}

void App::PopOverlay() {
  if (overlays_.empty()) return;
  focus_ = overlays_.back().saved_focus;
  overlays_.pop_back();
  host_.Invalidate();
}

void App::ShowMessage(std::wstring title, std::wstring text, bool error, const CatalogGame* logs_of) {
  Overlay overlay;
  overlay.title = std::move(title);
  overlay.text = std::move(text);
  overlay.error = error;
  overlay.buttons.push_back({Tr(kTextOk), [this] { PopOverlay(); }});
  if (logs_of) overlay.buttons.push_back({Tr(kTextOpenLogs), [this, logs_of] { OpenLogs(logs_of); }});
  overlay.back = [this] { PopOverlay(); };
  PushOverlay(std::move(overlay));
}

void App::ShowTerms(bool require_acceptance) {
  Overlay overlay;
  overlay.kind = OverlayKind::kTerms;
  overlay.text = TermsText(CurrentLanguage());
  overlay.require_acceptance = require_acceptance;
  if (require_acceptance) {
    overlay.buttons.push_back({Tr(kTextTermsAccept), [this] { AcceptTerms(); PopOverlay(); }, false});
    overlay.buttons.push_back({Tr(kTextTermsDecline), [this] { host_.Close(); }});
  } else {
    overlay.buttons.push_back({Tr(kTextTermsClose), [this] { PopOverlay(); }});
    overlay.back = [this] { PopOverlay(); };
  }
  PushOverlay(std::move(overlay));
}

void App::ToggleCompatible() {
  compatible_open_ = !compatible_open_;
  drawer_.Go(compatible_open_ ? 1.0f : 0.0f, Now(), 0.22);
  WriteSetting(SettingsFile(L"launcher_compatible.txt"), compatible_open_ ? L"1" : L"0");
  focus_ = compatible_open_ ? kIdCompatible : kIdTile + selected_;
  host_.Invalidate();
}

void App::ToggleLanguage() {
  SaveLanguage(CurrentLanguage() == Language::kSpanish ? Language::kEnglish : Language::kSpanish);
  host_.Invalidate();
}

void App::OpenAdd() {
  if (working_) return;
  Overlay overlay;
  overlay.kind = OverlayKind::kAdd;
  overlay.title = Tr(kTextAddTitle);
  overlay.primary = false;
  overlay.buttons.push_back({Tr(kTextCancel), [this] { PopOverlay(); }});
  overlay.back = [this] { PopOverlay(); };
  PushOverlay(std::move(overlay));
}

void App::ChooseFileToAdd(bool folder) {
  PopOverlay();
  const fs::path chosen = folder ? ChooseFile(host_.Window(), Tr(kTextFolderTitle), {}, true)
                                 : ChooseFile(host_.Window(), Tr(kTextDialogTitle),
                                              {{Tr(kTextFilterArchive), kGameFiles}, {Tr(kTextFilterAll), L"*.*"}}, false);
  if (!chosen.empty()) StartAdding(chosen);
}

void App::AddSource(const fs::path& source) {
  if (working_) return;
  while (!overlays_.empty() && overlays_.back().kind == OverlayKind::kAdd) PopOverlay();
  StartAdding(source);
}

void App::StartAdding(const fs::path& source) {
  StartJob([source](Job& job) { return AddGame(job, source); }, nullptr);
}

void App::SaveAnyx(std::wstring_view id, const fs::path& out) {
  if (working_) return;
  const CatalogGame* game = FindCatalogGame(id);
  fs::path target = out;
  if (game && target.empty()) {
    target = ChooseSaveFile(host_.Window(), Tr(kTextSaveAnyxTitle, {game->title}), FileNameOf(game->title) + L".anyx",
                            {{Tr(kTextFilterAnyx), L"*.anyx"}}, L"anyx");
    if (target.empty()) return;
  }
  StartJob(
      [game, target, id = std::wstring(id)](Job& job) -> Outcome {
        if (!game) return {{kTextErrorNotInLibrary, {id}, true}};
        return ExportAnyx(job, *game, target);
      },
      game);
  saving_ = true;
}

void App::StartJob(std::function<Outcome(Job&)> work, const CatalogGame* game) {
  working_ = true;
  step_ = Step::kChecking;
  percent_ = -1;
  job_game_ = game;
  saving_ = false;
  message_ = {};
  notes_.clear();
  Overlay progress;
  progress.kind = OverlayKind::kProgress;
  progress.primary = false;
  progress.buttons.push_back({Tr(kTextCancel), [this] { worker_.Cancel(); }});
  progress.back = [this] { worker_.Cancel(); };
  PushOverlay(std::move(progress));
  worker_.Start(*this, std::move(work));
}

void App::Play() {
  const int game = SelectedGame();
  if (game < 0) return OpenAdd();
  const LibraryGame& lib = games_[size_t(game)];
  if (lib.location_gone || working_ || running_game_) return;
  const Message error = StartGame(lib, [this](uint32_t code) { host_.Post([this, code] { GameExited(code); }); });
  if (!error.empty()) return ShowMessage(Tr(kTextProblemTitle), error.Render(), true);
  running_game_ = lib.info;
  message_ = {};
  host_.ShowWindow(false);
}

void App::GameExited(uint32_t code) {
  title_stats_.clear();  // the game added its time
  const CatalogGame* game = running_game_;
  running_game_ = nullptr;
  if (code == 0) return host_.Close();  // closed normally (or restarted from the game's settings)
  host_.ShowWindow(true);
  wchar_t text[16];
  swprintf(text, 16, code > 255 ? L"0x%08X" : L"%u", code);
  ShowMessage(Tr(kTextGameClosedTitle), Tr(kTextErrorExited, {game ? game->title : L"", text}), true, game);
}

void App::OpenMore(const RectF& anchor) {
  const int game = SelectedGame();
  if (game < 0 || working_) return;
  const LibraryGame& lib = games_[size_t(game)];
  const CatalogGame* info = lib.info;
  Overlay overlay;
  overlay.kind = OverlayKind::kMenu;
  overlay.anchor = anchor;
  overlay.items.push_back({Tr(kTextMenuCover), [this, info] { PopOverlay(); ChangeCover(*info); }});
  overlay.items.push_back({Tr(kTextMenuRestoreCover), [this, info] {
                             PopOverlay();
                             RestoreCover(*info);
                             Load();
                           },
                           !lib.cover.empty()});
  overlay.items.push_back({Tr(kTextMenuLogs), [this, info] { PopOverlay(); OpenLogs(info); }, Exists(LogDir(*info))});
  overlay.items.push_back({Tr(kTextMenuPrepareAgain), [this] { PopOverlay(); OpenAdd(); }});
  overlay.items.push_back({Tr(kTextMenuSaveAnyx), [this, info] { PopOverlay(); SaveAnyx(info->id, {}); },
                           lib.installed && !lib.location_gone});
  overlay.items.push_back({Tr(kTextMenuRemove), [this, info] {
                             PopOverlay();
                             Overlay confirm;
                             confirm.title = Tr(kTextRemoveTitle);
                             confirm.text = Tr(kTextConfirmRemove, {info->title});
                             confirm.buttons.push_back({Tr(kTextRemove), [this, info] {
                                                          PopOverlay();
                                                          StartJob([info](Job& job) { return RemoveGame(job, *info); }, info);
                                                        }});
                             confirm.buttons.push_back({Tr(kTextCancel), [this] { PopOverlay(); }});
                             confirm.back = [this] { PopOverlay(); };
                             PushOverlay(std::move(confirm));
                           }});
  overlay.back = [this] { PopOverlay(); };
  PushOverlay(std::move(overlay));
}

// ---- profiles (x360/profiles.h) --------------------------------------------------------------

const std::optional<x360::profiles::TitleStats>& App::TitleStats(uint64_t xuid, uint32_t title_id) {
  auto [found, added] = title_stats_.try_emplace({xuid, title_id});
  if (added) found->second = x360::profiles::ReadTitle(ProfilesRoot(), xuid, title_id);
  return found->second;
}

void App::ReloadProfiles() {
  title_stats_.clear();
  sign_in_ = x360::profile::Read(x360::profile::File(ProfilesRoot()));
  profiles_ = x360::profiles::List(ProfilesRoot());
  host_.Invalidate();
}

const x360::profiles::Profile* App::Player() const {
  for (const x360::profiles::Profile& p : profiles_)
    if (sign_in_.slots[0] && p.xuid == sign_in_.slots[0]) return &p;
  return nullptr;
}

void App::DrawGamerpic(Canvas& c, float cx, float cy, float radius, const x360::profiles::Profile* profile,
                       float opacity) {
  c.FillCircle(cx, cy, radius, (profile ? ProfileColor(profile->xuid) : kPanelHover).Alpha(opacity));
  const std::wstring initial = profile && !profile->gamertag.empty()
                                   ? std::wstring(1, wchar_t(std::towupper(wchar_t(profile->gamertag[0]))))
                                   : L"+";
  TextStyle letter = Style(radius * 1.05f, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText.Alpha(opacity), Align::kCenter);
  letter.vertical = Align::kCenter;
  c.Text(initial, {cx - radius, cy - radius - radius * 0.06f, radius * 2, radius * 2}, letter);
}

void App::ShowWhoIsPlaying(bool required) {
  ReloadProfiles();
  Overlay overlay;
  overlay.kind = OverlayKind::kWho;
  overlay.required = required;
  if (!required) {
    overlay.buttons.push_back({Tr(kTextCancel), [this] { PopOverlay(); }});
    overlay.back = [this] { PopOverlay(); };
  }
  PushOverlay(std::move(overlay));
}

void App::DrawWho(Canvas& c, Overlay& overlay, bool top) {
  const float fade = overlay.fade.Value(Now());
  c.FillRect({0, 0, width_, height_}, Color::Hex(0x000000, 0.7f * fade));
  const float column_w = std::min(900.0f, width_ - 2 * kMargin);
  const float x = (width_ - column_w) / 2;
  const RectF band{0, 36, width_, height_ - 72};
  c.FillRect(band, kPanel.Alpha(fade));
  float y = band.y + 30;
  c.FillRect({x - 18, y + 8, 4, 32}, kGreen.Alpha(fade));
  c.Text(Tr(kTextWhoTitle), {x, y, column_w, 48}, Style(34, DWRITE_FONT_WEIGHT_LIGHT, kText.Alpha(fade)));
  if (std::shared_ptr<const Image> mark = Logo(kLogoMark, 40))
    DrawLogo(c, mark, x + column_w - mark->width * 96 / dpi_, y + 4, fade);
  y += 62;
  TextStyle text = Body();
  text.color = kTextSecondary.Alpha(fade);
  c.Text(Tr(kTextWhoText), {x, y, column_w, 44}, text);
  y += 64;
  // The tiles, in rows: each profile, then "Create profile".
  const float tile_w = 150, tile_h = 178, gap = 22;
  const size_t count = profiles_.size() + 1;
  const size_t per_row = std::max<size_t>(1, size_t((column_w + gap) / (tile_w + gap)));
  // In the middle of the space left, as far as it fits.
  const size_t rows = (count + per_row - 1) / per_row;
  const float space = band.bottom() - 70 - y, used = rows * (tile_h + gap) - gap;
  if (used < space) y += std::round((space - used) / 2 * 0.8f);
  TextStyle name = Style(16, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText.Alpha(fade), Align::kCenter);
  for (size_t i = 0; i < count; ++i) {
    const size_t row = i / per_row, in_row = std::min(per_row, count - row * per_row);
    const float row_x = x + (column_w - (in_row * (tile_w + gap) - gap)) / 2;
    const RectF r{row_x + (i % per_row) * (tile_w + gap), y + row * (tile_h + gap), tile_w, tile_h};
    if (r.bottom() > band.bottom() - 70) break;  // more than fit: the rest from Profiles
    const int id = kIdWhoTile + int(i);
    c.FillRect(r, (hover_ == id ? kPanelHover : kPanelHigh).Alpha(fade));
    const x360::profiles::Profile* p = i < profiles_.size() ? &profiles_[i] : nullptr;
    DrawGamerpic(c, r.cx(), r.y + 66, 46, p, fade);
    c.Text(p ? Widen(p->gamertag) : Tr(kTextCreateProfile), {r.x + 6, r.y + 128, r.w - 12, 40}, name);
    if (p && sign_in_.slots[0] == p->xuid) c.StrokeRect(r.Inset(1), kGreen.Alpha(fade), 2);
    if (focus_ == id && focus_visible_) DrawFocus(c, r);
    if (top) {
      if (p) AddTarget(r, id, [this, xuid = p->xuid] { SignInAs(xuid); });
      else AddTarget(r, id, [this] { OpenGamertag(0); });
    }
  }
  float bx = x + column_w;
  for (int i = int(overlay.buttons.size()) - 1; i >= 0; --i) {
    const Overlay::Action& action = overlay.buttons[size_t(i)];
    const float w = std::max(132.0f, c.MeasureText(action.label, Style(16, DWRITE_FONT_WEIGHT_SEMI_BOLD, kText)).width + 44);
    if (top) Button(c, {bx - w, band.bottom() - 30 - 42, w, 42}, action.label, kIdButton + i, action.run, false, action.enabled);
    bx -= w + 10;
  }
}

void App::SignInAs(uint64_t xuid) {
  if (!x360::profiles::SetSlot(ProfilesRoot(), 0, xuid))
    return ShowMessage(Tr(kTextProblemTitle), Tr(kTextErrorProfileWrite, {ShortPath(ProfilesRoot())}), true);
  ReloadProfiles();
  // Back to the library from "Who's playing?" (and the gamertag typed for a new profile).
  while (!overlays_.empty() && (overlays_.back().kind == OverlayKind::kWho ||
                                overlays_.back().kind == OverlayKind::kGamertag ||
                                overlays_.back().kind == OverlayKind::kMenu))
    PopOverlay();
}

void App::OpenProfiles() {
  ReloadProfiles();
  Overlay overlay;
  overlay.kind = OverlayKind::kProfiles;
  overlay.title = Tr(kTextProfileTitle);
  overlay.text = Tr(kTextProfileText);
  overlay.buttons.push_back({Tr(kTextTermsClose), [this] { PopOverlay(); }});
  overlay.primary = false;
  overlay.back = [this] { PopOverlay(); };
  PushOverlay(std::move(overlay));
}

void App::OpenProfileMenu(const RectF& anchor, uint64_t xuid) {
  const auto found = std::find_if(profiles_.begin(), profiles_.end(), [xuid](const auto& p) { return p.xuid == xuid; });
  if (found == profiles_.end()) return;
  const std::wstring gamertag = Widen(found->gamertag);
  Overlay overlay;
  overlay.kind = OverlayKind::kMenu;
  overlay.anchor = {anchor.right() - 330, anchor.y, 330, anchor.h};
  overlay.items.push_back({Tr(kTextMenuPlayAs, {gamertag}), [this, xuid] { SignInAs(xuid); PopOverlay(); },
                           sign_in_.slots[0] != xuid});
  overlay.items.push_back({Tr(kTextMenuRename), [this, xuid] { PopOverlay(); OpenGamertag(xuid); }});
  overlay.items.push_back({Tr(kTextMenuDeleteProfile), [this, xuid, gamertag] {
                             PopOverlay();
                             Overlay confirm;
                             confirm.title = Tr(kTextDeleteProfileTitle, {gamertag});
                             confirm.text = Tr(kTextConfirmDeleteProfile, {gamertag, Widen(x360::profiles::XuidText(xuid))});
                             confirm.buttons.push_back({Tr(kTextDelete), [this, xuid] {
                                                          PopOverlay();
                                                          if (!x360::profiles::Remove(ProfilesRoot(), xuid))
                                                            ShowMessage(Tr(kTextProblemTitle),
                                                                        Tr(kTextErrorProfileWrite, {ShortPath(ProfilesRoot())}), true);
                                                          ReloadProfiles();
                                                          if (!Player()) ShowWhoIsPlaying(true);  // it was playing
                                                        }});
                             confirm.buttons.push_back({Tr(kTextCancel), [this] { PopOverlay(); }});
                             confirm.back = [this] { PopOverlay(); };
                             PushOverlay(std::move(confirm));
                           }});
  overlay.back = [this] { PopOverlay(); };
  PushOverlay(std::move(overlay));
}

void App::OpenGamertag(uint64_t xuid) {
  Overlay overlay;
  overlay.kind = OverlayKind::kGamertag;
  overlay.xuid = xuid;
  if (xuid) {
    for (const x360::profiles::Profile& p : profiles_)
      if (p.xuid == xuid) overlay.typed = Widen(p.gamertag), overlay.title = Tr(kTextRenameTitle, {overlay.typed});
    overlay.shift = overlay.typed.empty();
  } else {
    overlay.title = Tr(kTextNewProfileTitle);
  }
  overlay.text = Tr(kTextNameRules);
  overlay.buttons.push_back({xuid ? Tr(kTextSave) : Tr(kTextCreate), [this] { SubmitGamertag(); }});
  overlay.buttons.push_back({Tr(kTextCancel), [this] { PopOverlay(); }});
  // B deletes what was typed, then goes back.
  overlay.back = [this] {
    if (!overlays_.back().typed.empty()) TypeGamertag(L'\b');
    else PopOverlay();
  };
  PushOverlay(std::move(overlay));
}

void App::TypeGamertag(wchar_t ch) {
  if (overlays_.empty() || overlays_.back().kind != OverlayKind::kGamertag) return;
  Overlay& overlay = overlays_.back();
  std::wstring& typed = overlay.typed;
  if (ch == L'\b') {
    if (!typed.empty()) typed.pop_back();
    if (typed.empty()) overlay.shift = true;
  } else if (typed.size() < x360::profile::kNameLength && NameCharacter(ch)) {
    if (ch == L' ' && (typed.empty() || typed.back() == L' ')) return;
    if (std::iswalpha(ch)) {
      // From the on-screen keyboard, as Shift says; typed on the keyboard, as typed.
      typed += ch;
      if (overlay.shift && focus_ >= kIdKey) typed.back() = wchar_t(std::towupper(ch)), overlay.shift = false;
    } else {
      typed += ch;
    }
  }
  overlay.error = false;
  overlay.text = Tr(kTextNameRules);
  host_.Invalidate();
}

void App::SubmitGamertag() {
  if (overlays_.empty() || overlays_.back().kind != OverlayKind::kGamertag) return;
  Overlay& overlay = overlays_.back();
  const std::string gamertag = Narrow(overlay.typed);
  const x360::profiles::Result result =
      overlay.xuid ? x360::profiles::Rename(ProfilesRoot(), overlay.xuid, gamertag)
                   : x360::profiles::Create(ProfilesRoot(), gamertag, Player() ? Player()->language : sign_in_.language);
  if (!result) {
    overlay.error = true;
    overlay.text = result.error == x360::profiles::Error::kEmptyName ? Tr(kTextErrorNameEmpty)
                   : result.error == x360::profiles::Error::kNameTaken
                       ? Tr(kTextErrorNameTaken, {Widen(x360::profile::CleanName(gamertag))})
                       : Tr(kTextErrorProfileWrite, {ShortPath(ProfilesRoot())});
    return host_.Invalidate();
  }
  const bool created = !overlay.xuid;
  PopOverlay();
  ReloadProfiles();
  // A new profile from "Who's playing?" plays at once.
  if (created && !overlays_.empty() && overlays_.back().kind == OverlayKind::kWho) SignInAs(result.profile.xuid);
}

void App::ChangeCover(const CatalogGame& game) {
  const fs::path image = ChooseFile(host_.Window(), Tr(kTextCoverTitle, {game.title}),
                                    {{Tr(kTextFilterImages), L"*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.heic"},
                                     {Tr(kTextFilterAll), L"*.*"}},
                                    false);
  if (image.empty()) return;
  Image check;
  std::string error;
  if (!anyx360::ui::DecodePicture(image, check, error, 64, 64) || !SetPlayerCover(game, image))
    return ShowMessage(Tr(kTextProblemTitle), Tr(kTextErrorCover), true);
  Load();
}

void App::OpenLogs(const CatalogGame* game) {
  const fs::path logs = game && Exists(LogDir(*game)) ? LogDir(*game) : Root() / L"logs";
  ShellExecuteW(host_.Window(), L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool App::OnClose() {
  if (!working_) return true;
  if (close_after_job_) return false;
  Overlay overlay;
  overlay.title = Tr(kTextExitTitle);
  overlay.text = Tr(kTextConfirmCancelExit);
  overlay.buttons.push_back({Tr(kTextExit), [this] {
                               PopOverlay();
                               close_after_job_ = true;
                               worker_.Cancel();
                             }});
  overlay.buttons.push_back({Tr(kTextKeepGoing), [this] { PopOverlay(); }});
  overlay.back = [this] { PopOverlay(); };
  PushOverlay(std::move(overlay));
  return false;
}

// ---- the job's reports (its thread) ------------------------------------------------------------

void App::OnProgress(Step step, int percent) {
  host_.Post([this, step, percent] {
    step_ = step;
    percent_ = percent;
    host_.Invalidate();
  });
}

void App::OnGame(const CatalogGame& game) {
  host_.Post([this, &game] {
    job_game_ = &game;
    host_.Invalidate();
  });
}

bool App::AskReplace(const CatalogGame& game) {
  if (quit_result_) return false;  // unattended: nobody to ask, and nothing is replaced unasked
  auto answer = std::make_shared<Answer<bool>>();
  std::future<bool> answered = answer->promise.get_future();
  host_.Post([this, answer, &game] {
    Overlay overlay;
    overlay.title = Tr(kTextReplaceTitle);
    overlay.text = Tr(kTextConfirmReplace, {game.title});
    overlay.replace = answer;
    overlay.buttons.push_back({Tr(kTextReplace), [this, answer] { answer->Give(true); PopOverlay(); }});
    overlay.buttons.push_back({Tr(kTextKeep), [this, answer] { answer->Give(false); PopOverlay(); }});
    overlay.back = [this, answer] { answer->Give(false); PopOverlay(); };
    PushOverlay(std::move(overlay));
  });
  return answered.get();
}

std::optional<std::wstring> App::AskPassword(const fs::path& archive, bool wrong) {
  if (!command_line_password_.empty() && !command_line_password_used_) {
    command_line_password_used_ = true;
    return command_line_password_;
  }
  if (quit_result_) return std::nullopt;  // unattended: nobody to ask
  auto answer = std::make_shared<Answer<std::optional<std::wstring>>>();
  std::future<std::optional<std::wstring>> answered = answer->promise.get_future();
  host_.Post([this, answer, name = archive.filename().wstring(), wrong] {
    Overlay overlay;
    overlay.kind = OverlayKind::kPassword;
    overlay.title = Tr(kTextPasswordTitle);
    overlay.text = Tr(kTextAskPassword, {name});
    overlay.wrong = wrong;
    overlay.password = answer;
    overlay.buttons.push_back({Tr(kTextOk), [this, answer] {
                                 answer->Give(overlays_.back().typed);
                                 PopOverlay();
                               }});
    overlay.buttons.push_back({Tr(kTextCancel), [this, answer] { answer->Give(std::nullopt); PopOverlay(); }});
    overlay.back = [this, answer] { answer->Give(std::nullopt); PopOverlay(); };
    PushOverlay(std::move(overlay));
  });
  return answered.get();
}

void App::OnDone(const Outcome& outcome) {
  host_.Post([this, outcome] { Finish(outcome); });
}

void App::Finish(const Outcome& outcome) {
  worker_.Join();
  working_ = false;
  job_game_ = nullptr;
  overlays_.erase(std::remove_if(overlays_.begin(), overlays_.end(),
                                 [](const Overlay& o) { return o.kind == OverlayKind::kProgress; }),
                  overlays_.end());
  focus_ = -1;
  message_ = outcome.message;
  notes_ = outcome.notes;
  std::wstring text = message_.Render();
  for (const Message& note : notes_) text += L" " + note.Render();
  if (quit_result_) {
    std::ofstream out(*quit_result_, std::ios::binary | std::ios::trunc);
    out << (message_.error ? "error: " : "ok: ") << Narrow(text) << "\n";
    out.close();
    return host_.Close();
  }
  if (close_after_job_) return host_.Close();
  Load(outcome.select);
  if (message_.error) {
    message_ = {};
    notes_.clear();
    ShowMessage(Tr(kTextProblemTitle), text, true);
  }
}

void App::ShowState(std::wstring_view state) {
  g_instant = true;
  if (state == L"empty") {
    tiles_ = {-1};
    selected_ = 0;
  } else if (state == L"compatible") {
    compatible_open_ = true;
    drawer_.Go(1, 0, 0);
    focus_ = kIdCompatible + 1;
    focus_visible_ = true;
  } else if (state == L"add") {
    OpenAdd();
  } else if (state == L"progress") {
    StartJob([](Job&) { return Outcome{}; }, nullptr);
    worker_.Join();
    job_game_ = &kCatalog[0];
    step_ = Step::kExtracting;
    percent_ = 42;
  } else if (state == L"error") {
    ShowMessage(Tr(kTextProblemTitle), Tr(kTextErrorUnsupported, {L"58410B79", JoinTitles(AvailableGames())}), true);
  } else if (state == L"confirm") {
    Overlay overlay;
    overlay.title = Tr(kTextReplaceTitle);
    overlay.text = Tr(kTextConfirmReplace, {kCatalog[0].title});
    overlay.buttons = {{Tr(kTextReplace), [] {}}, {Tr(kTextKeep), [] {}}};
    PushOverlay(std::move(overlay));
  } else if (state == L"password") {
    Overlay overlay;
    overlay.kind = OverlayKind::kPassword;
    overlay.title = Tr(kTextPasswordTitle);
    overlay.text = Tr(kTextAskPassword, {L"FNK-DLC-TU-XBLA.rar"});
    overlay.typed = L"secret";
    overlay.wrong = true;
    overlay.buttons = {{Tr(kTextOk), [] {}}, {Tr(kTextCancel), [] {}}};
    PushOverlay(std::move(overlay));
  } else if (state == L"menu") {
    OpenMore(more_button_);
    focus_visible_ = true;
  } else if (state == L"terms") {
    ShowTerms(true);
  } else if (state == L"who" || state == L"profiles" || state == L"profile" || state == L"newprofile") {
    // Made-up profiles (nothing is written): Daniel playing, Ana on player 2's slot.
    const char* names[] = {"Daniel", "Ana", "Leo", "Marta"};
    const uint64_t xuids[] = {x360::profiles::kLegacyXuid, x360::profiles::kLegacyXuid + 1, 0xE0003A41C29F0B57ull,
                              0xE000B19D4402C6E1ull};
    profiles_.clear();
    for (int i = 0; i < 4; ++i) {
      x360::profiles::Profile p;
      p.xuid = xuids[i];
      p.gamertag = names[i];
      p.created = "2026-10-29T05:00:0" + std::to_string(i) + "Z";
      profiles_.push_back(p);
    }
    sign_in_ = {};
    if (state != L"who") sign_in_.slots[0] = xuids[0], sign_in_.slots[1] = xuids[1];
    if (state == L"who") {
      Overlay overlay;
      overlay.kind = OverlayKind::kWho;
      overlay.required = true;
      PushOverlay(std::move(overlay));
      focus_ = kIdWhoTile + 1;
    } else if (state == L"newprofile") {
      OpenGamertag(0);
      overlays_.back().typed = L"Lu";
      overlays_.back().shift = false;
      focus_ = kIdKey + 1 * 16 + 4;  // T
    } else {
      Overlay overlay;
      overlay.kind = OverlayKind::kProfiles;
      overlay.title = Tr(kTextProfileTitle);
      overlay.text = Tr(kTextProfileText);
      overlay.primary = false;
      overlay.buttons.push_back({Tr(kTextTermsClose), [] {}});
      PushOverlay(std::move(overlay));
      focus_ = kIdProfileRow + 1;
    }
    focus_visible_ = true;
  }
  host_.Invalidate();
}

}  // namespace anyx360::launcher::ui
