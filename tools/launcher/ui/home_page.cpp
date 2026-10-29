// The dashboard's Home (console mode; docs/DASHBOARD.md): the selected game's art behind everything,
// the logo and the gamercard on top, then a row on a mirror floor: the profile's avatar standing at
// the left, the game before the selected one half hidden behind it, the selected game's box art
// large in a green glow with its title under it, the next games smaller and dimmer; at the bottom a
// glass panel to the other pages (My games, Settings, Add games) and the controller's hints.
//
// Controller first: left and right go through the games, newest played first (left of the first,
// the avatar: A edits it), down to the panel, up to the gamercard. The mouse clicks a game to select
// it (again to play it), the wheel or a trackpad's swipe moves the row.
#include <algorithm>
#include <cmath>

#include "avatar_look.h"
#include "core/catalog.h"
#include "core/library.h"
#include "core/strings.h"
#include "ui/app_internal.h"
#include "ui/avatar_page.h"
#include "util.h"
#include "x360/achievements.h"
#include "x360/profiles.h"

namespace anyx360::launcher::ui {

namespace {

constexpr float kCoverAspect = 0.70f;  // an Xbox 360 case's front
constexpr Color kNeon = Color::Hex(0x4CFF2E);
constexpr Color kNeonCore = Color::Hex(0xC8FFB4);
constexpr Color kBlack = Color::Hex(0x000000);
constexpr Color kWhite = Color::Hex(0xFFFFFF);
constexpr float kAvatarYaw = 0.32f;  // turned a little towards the games

// Where everything goes for the window's size (DIPs); `u` scales the design from small windows to
// 4K.
struct HomeLayout {
  float u = 1, m = 44;
  float top_cy = 52;  // the top bar's centre line
  RectF panel;        // the glass panel
  RectF selected;     // the selected game's box art
  float floor_y = 0;  // where the other covers stand (the mirror's horizon)
  float hs = 0, ws = 0, gap = 0;  // the other covers' size and the space between them
  float title_y = 0;
  float avatar_cx = 0, avatar_feet = 0, avatar_h = 0;
};

HomeLayout Layout(float width, float height) {
  HomeLayout l;
  const float u = l.u = std::clamp(std::min(width / 1280, height / 760), 0.72f, 3.0f);
  l.m = 44 * u;
  l.top_cy = 52 * u;
  const float panel_h = 80 * u, panel_w = std::min(std::clamp(width * 0.44f, 420 * u, 900 * u), width - 2 * l.m);
  l.panel = {std::round((width - panel_w) / 2), height - 20 * u - panel_h, panel_w, panel_h};
  const float bottom = l.panel.y - 12 * u - 18 * u - 30 * u - 8 * u;
  const float big_h = std::max(120.0f, std::min(height * 0.47f, bottom - (l.top_cy + 56 * u)));
  l.hs = big_h * 0.70f;
  l.ws = l.hs * kCoverAspect;
  l.gap = 14 * u;
  l.floor_y = bottom - big_h * 0.06f;
  l.avatar_h = big_h * 1.18f;
  const float avatar_w = l.avatar_h * 0.42f;
  l.avatar_cx = l.m + avatar_w / 2;
  l.avatar_feet = bottom + 8 * u;
  l.selected = {std::round(l.m + avatar_w + l.ws * 0.40f), bottom - big_h, std::round(big_h * kCoverAspect), big_h};
  l.title_y = bottom + 8 * u;
  return l;
}

// A cover's place and look `d` places from the selected one (d < 0: before it).
struct Slot {
  RectF r;
  float opacity = 1, dim = 0, blur = 0;
};

Slot SlotAt(const HomeLayout& l, int d) {
  if (d == 0) return {l.selected, 1, 0, 0};
  if (d > 0)
    return {{l.selected.right() + l.gap + (d - 1) * (l.ws + l.gap), l.floor_y - l.hs, l.ws, l.hs},
            1,
            std::min(0.16f + 0.1f * (d - 1), 0.5f),
            0};
  // Before it: smaller, translucent and blurred, behind the avatar; further ones gone.
  const float w = l.ws * 0.86f, h = l.hs * 0.86f;
  return {{l.selected.x - l.gap * 0.6f - w + (d + 1) * (w + l.gap), l.floor_y - h, w, h}, d == -1 ? 0.62f : 0.0f, 0.3f, 1};
}

Slot SlotBetween(const HomeLayout& l, float d) {
  const int a = int(std::floor(d));
  const float t = d - float(a);
  const Slot s = SlotAt(l, a), e = SlotAt(l, a + 1);
  auto mix = [t](float x, float y) { return x + (y - x) * t; };
  return {{mix(s.r.x, e.r.x), mix(s.r.y, e.r.y), mix(s.r.w, e.r.w), mix(s.r.h, e.r.h)},
          mix(s.opacity, e.opacity),
          mix(s.dim, e.dim),
          mix(s.blur, e.blur)};
}

}  // namespace

std::vector<int> App::HomeOrder() { return RecentlyPlayed(); }

void App::OpenHomeAvatar() {
  if (!Player()) return ShowWhoIsPlaying(false);
  ShowPage(Page::kAvatar);
}

void App::HomeFocusGame(int game) {
  home_game_ = game;
  home_on_avatar_ = false;
  focus_ = game >= 0 ? kIdConsoleRecent + game : kIdHomeAdd;
  host_.Invalidate();
}

void App::HomeFocusAvatar() {
  home_on_avatar_ = true;
  focus_ = kIdHomeAvatar;
  host_.Invalidate();
}

void App::HomeFocusCarousel() {
  if (home_on_avatar_) return HomeFocusAvatar();
  HomeFocusGame(home_game_);
}

bool App::HomeKey(Key key) {
  if (!console_ || page_ != Page::kHome || !overlays_.empty()) return false;
  const std::vector<int> order = HomeOrder();
  const bool on_avatar = focus_ == kIdHomeAvatar;
  const bool on_game = (focus_ >= kIdConsoleRecent && focus_ < kIdConsoleRecent + 100) || focus_ == kIdHomeAdd;
  const bool on_panel = focus_ >= kIdHomePanel && focus_ < kIdHomePanel + 3;
  const bool on_top = focus_ == kIdHomePill || focus_ == kIdHomeCard;
  const int pos = int(std::find(order.begin(), order.end(), focus_ - kIdConsoleRecent) - order.begin());
  switch (key) {
    case Key::kLeft:
      if (on_avatar) return true;
      if (on_game) {
        if (focus_ != kIdHomeAdd && pos > 0 && pos < int(order.size())) HomeFocusGame(order[size_t(pos - 1)]);
        else HomeFocusAvatar();
        return true;
      }
      return false;
    case Key::kRight:
      if (on_avatar) {
        HomeFocusGame(order.empty() ? -1 : order[0]);
        return true;
      }
      if (on_game) {
        if (focus_ != kIdHomeAdd && pos + 1 < int(order.size())) HomeFocusGame(order[size_t(pos + 1)]);
        return true;
      }
      return false;
    case Key::kDown:
      if (on_game || on_avatar) {
        // The panel's entry nearest below.
        const Target* from = Focused();
        const float x = from ? from->rect.cx() : width_ / 2;
        int best = kIdHomePanel;
        float best_d = 1e9f;
        for (const Target& t : targets_)
          if (t.id >= kIdHomePanel && t.id < kIdHomePanel + 3 && std::abs(t.rect.cx() - x) < best_d)
            best = t.id, best_d = std::abs(t.rect.cx() - x);
        focus_ = best;
        host_.Invalidate();
        return true;
      }
      if (on_top) {
        HomeFocusCarousel();
        return true;
      }
      return on_panel;
    case Key::kUp:
      if (on_panel) {
        HomeFocusCarousel();
        return true;
      }
      if (on_game || on_avatar) {
        focus_ = kIdHomeCard;
        host_.Invalidate();
        return true;
      }
      return on_top;
    case Key::kBack:  // from the panel or the top bar, back to the row
      if (on_panel || on_top) {
        HomeFocusCarousel();
        return true;
      }
      return false;
    default: return false;
  }
}

void App::HomeWheel(float notches) {
  const std::vector<int> order = HomeOrder();
  if (order.empty()) return;
  wheel_rest_ += notches;
  const int steps = int(wheel_rest_);
  wheel_rest_ -= float(steps);
  if (!steps) return;
  int pos = int(std::find(order.begin(), order.end(), home_game_) - order.begin());
  if (pos >= int(order.size())) pos = 0;
  HomeFocusGame(order[size_t(std::clamp(pos - steps, 0, int(order.size()) - 1))]);
}

void App::Tick() {
  SYSTEMTIME local;
  GetLocalTime(&local);
  if (console_ && int(local.wHour) * 60 + local.wMinute != painted_minute_) host_.Invalidate();
}

void App::SetPads(int count) {
  if (count == pads_) return;
  pads_ = count;
  host_.Invalidate();
}

// ---- drawing -------------------------------------------------------------------------------------

std::shared_ptr<const Image> App::HomeBackdrop(const LibraryGame& lib) {
  const float scale = dpi_ / 96;
  const float window_w = std::max(1.0f, width_ * scale), window_h = std::max(1.0f, height_ * scale);
  // The game's own art, decoded at 1080p at most, else its cover.
  std::shared_ptr<const Image> source;
  bool art = false;
  if (const fs::path file = BackgroundArt(lib.info->cover); !file.empty()) {
    source = images_.Picture(file, 1920, 1080);
    art = source != nullptr;
  }
  if (!source) source = images_.Cover(!lib.cover.empty() ? lib.cover : BoxArt(lib.info->cover), 400, 600);
  if (!source) return nullptr;
  // A slight blur for art (a third of the window, smoothed when stretched); the cover, enlarged that
  // much, blurred more.
  const uint32_t bw = art ? std::clamp(uint32_t(std::lround(window_w / 3 / 32)) * 32, 384u, 768u) : 192u;
  const uint32_t bh = std::max(8u, uint32_t(std::lround(bw * window_h / window_w / 4)) * 4);
  const auto key = std::make_tuple(source, bw, bh);
  auto found = home_blurred_.find(key);
  if (found == home_blurred_.end()) {
    if (home_blurred_.size() > 16) home_blurred_.clear();
    found = home_blurred_.emplace(key, std::make_shared<Image>(anyx360::ui::Blurred(*source, bw, bh, art ? 1 : 3))).first;
  }
  return found->second;
}

void App::DrawHomeBackground(Canvas& c) {
  const double now = Now();
  c.Clear(kBackground);
  std::shared_ptr<const Image> backdrop;
  if (const int game = SelectedGame(); game >= 0) backdrop = HomeBackdrop(games_[size_t(game)]);
  // While the next one is being decoded, the last one stays.
  if (!backdrop && SelectedGame() >= 0) backdrop = background_;
  if (backdrop != background_) {
    previous_background_ = background_;
    background_ = backdrop;
    background_fade_ = {};
    background_fade_.Go(1, now, 0.45);
  }
  const float fade = background_fade_.Value(now);
  const RectF all{0, 0, width_, height_};
  if (previous_background_ && fade < 1) c.DrawBitmap(Bitmap(c, previous_background_), all, 1, true);
  if (background_) c.DrawBitmap(Bitmap(c, background_), all, previous_background_ ? fade : 1, true);
  // The interface in front: a little darker overall, more under the top bar and towards the floor.
  c.FillRect(all, kBlack.Alpha(0.16f));
  c.FillGradient({0, 0, width_, height_ * 0.24f}, kBlack.Alpha(0.6f), kBlack.Alpha(0));
  c.FillGradient({0, height_ * 0.42f, width_, height_ * 0.58f}, kBlack.Alpha(0), kBlack.Alpha(0.55f));
}

void App::DrawHomeCover(Canvas& c, const RectF& r, int game, float opacity, float dim, float blur, float radius,
                        bool hover) {
  const float scale = dpi_ / 96;
  const LibraryGame& lib = games_[size_t(game)];
  // One size for every place (the selected one's, in steps of 96 pixels), so moving or resizing the
  // window doesn't decode it again each time.
  const float big_h = home_cover_h_ > 0 ? home_cover_h_ : r.h;
  const uint32_t pixels_h = (uint32_t(big_h * scale) + 95) / 96 * 96;
  std::shared_ptr<const Image> cover = images_.Cover(!lib.cover.empty() ? lib.cover : BoxArt(lib.info->cover),
                                                     uint32_t(pixels_h * kCoverAspect), pixels_h);
  c.PushClip(r, radius);
  if (ID2D1Bitmap* bitmap = Bitmap(c, cover)) {
    c.FillRect(r, kPanel.Alpha(opacity));
    if (blur < 1) c.DrawBitmap(bitmap, r, opacity * (1 - blur), true);
    if (blur > 0) {
      auto found = home_blurred_covers_.find(cover);
      if (found == home_blurred_covers_.end()) {
        if (home_blurred_covers_.size() > 24) home_blurred_covers_.clear();
        found = home_blurred_covers_
                    .emplace(cover, std::make_shared<Image>(anyx360::ui::Blurred(*cover, std::max(8u, cover->width / 7),
                                                                                  std::max(8u, cover->height / 7), 2)))
                    .first;
      }
      c.DrawBitmap(Bitmap(c, found->second), r, opacity * blur, true);
    }
  } else {
    // No picture: the icon on its colour, the title under it.
    std::shared_ptr<const Image> icon = images_.Picture(lib.icon, uint32_t(128 * scale), uint32_t(128 * scale));
    c.FillGradient(r, (icon ? AverageColor(*icon).Mix(kText, 0.08f) : Color::Hex(0x2E3B2E)).Alpha(opacity),
                   kPanel.Alpha(opacity));
    const float size = std::min(r.w * 0.5f, 128.0f);
    if (ID2D1Bitmap* bitmap = Bitmap(c, icon)) c.DrawBitmap(bitmap, {r.cx() - size / 2, r.y + r.h * 0.2f, size, size}, opacity);
    c.Text(lib.info->title, {r.x + 10, r.y + r.h * 0.2f + size + 12, r.w - 20, r.h * 0.3f},
           Style(std::max(12.0f, r.h * 0.045f), DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText.Alpha(opacity), Align::kCenter, true));
  }
  // A sheen at the top, the hover, the dimming of the ones further away.
  c.FillGradient({r.x, r.y, r.w, r.h * 0.3f}, kWhite.Alpha(0.10f * opacity), kWhite.Alpha(0));
  if (hover) c.FillRect(r, kWhite.Alpha(0.08f));
  if (dim > 0) c.FillRect(r, kBlack.Alpha(dim * opacity));
  c.PopClip();
  c.StrokeRoundedRect(r, radius, kWhite.Alpha(0.14f * opacity), 1);
  if (lib.location_gone || lib.needs_update) {
    const float dot = std::max(9.0f, r.w * 0.05f);
    c.FillCircle(r.right() - dot * 1.8f, r.y + dot * 1.8f, dot, kRed.Alpha(opacity));
    c.Text(L"!", {r.right() - dot * 2.8f, r.y + dot * 0.75f, dot * 2, dot * 2.1f},
           Style(dot * 1.3f, DWRITE_FONT_WEIGHT_BOLD, kText.Alpha(opacity), Align::kCenter));
  }
}

// The game's box art upside down under it, fading into the floor.
void App::DrawHomeReflection(Canvas& c, const RectF& r, int game, float opacity, float dim, float blur, float radius) {
  if (opacity <= 0.01f) return;
  const float length = r.h * 0.45f;
  c.PushFade({r.x - 2, r.bottom(), r.w + 4, length}, 0.26f * opacity, 0);
  c.PushMirror(r.bottom());
  DrawHomeCover(c, r, game, 1, dim, blur, radius, false);
  c.PopMirror();
  c.PopClip();
}

// A green neon glow behind `r` (drawn before what it frames): rounded layers adding up to a soft halo.
static void NeonGlow(Canvas& c, const RectF& r, float radius, float u, float strength) {
  for (int i = 14; i >= 1; --i) {
    const float e = i * 1.8f * u;
    c.FillRoundedRect(r.Inset(-e), radius + e, kNeon.Alpha(strength * 0.045f));
  }
}

// A green neon frame around `r`: a bright line and a white core (over the glow).
static void Neon(Canvas& c, const RectF& r, float radius, float u, float strength) {
  c.StrokeRoundedRect(r.Inset(-5 * u), radius + 5 * u, kNeon.Alpha(0.35f * strength), 3 * u);
  c.StrokeRoundedRect(r.Inset(-2.5f * u), radius + 2.5f * u, kNeon.Alpha(strength), 3.2f * u);
  c.StrokeRoundedRect(r.Inset(-1.6f * u), radius + 1.6f * u, kNeonCore.Alpha(0.85f * strength), 1.1f * u);
}

void App::DrawHomeAvatar(Canvas& c, float cx, float feet, float h, float u, bool focused) {
  const x360::profiles::Profile* player = Player();
  RectF body{cx - h * 0.22f, feet - h, h * 0.44f, h};
  bool drawn = false;
  // Lit from behind while it has the focus; a ring on the floor at its feet.
  const float glow = focused ? 1.0f : 0.0f;
  if (glow > 0) {
    c.FillRadial(cx, feet - h * 0.45f, h * 0.45f, h * 0.62f, kNeon.Alpha(0.24f), kNeon.Alpha(0));
    c.FillRadial(cx, feet, h * 0.3f, h * 0.06f, kNeon.Alpha(0.45f), kNeon.Alpha(0));
  }
  const x360tool::AvatarCatalog* pack = player ? AvatarPack() : nullptr;
  if (player && pack && pack->loaded()) {
    // The profile's avatar (else the default a game gives player 1), built again when it changes.
    const std::string key = screenshot_avatar_.empty() ? player->avatar : "spec:" + screenshot_avatar_;
    if (!home_avatar_) home_avatar_ = std::make_unique<HomeAvatar>();
    HomeAvatar& a = *home_avatar_;
    if (a.xuid != player->xuid || a.avatar != key || a.pack != pack) {
      x360tool::AvatarDescription d;
      if (!screenshot_avatar_.empty()) {
        d = x360tool::make_avatar_description(*pack, x360tool::AvatarLook::kMale);
        x360tool::customize_avatar(*pack, d, screenshot_avatar_);
      } else if (!x360tool::read_avatar_description(*pack, x360tool::base64_decode(player->avatar), d)) {
        d = x360tool::make_avatar_description(*pack, x360tool::AvatarLook::kMale);
      }
      a.error.clear();
      a.built = a.renderer.SetAvatar(*pack, d, a.error);
      a.xuid = player->xuid, a.avatar = key, a.pack = pack;
      a.picture = {};
      a.width = a.height = 0;
    }
    // Drawn once for the size it shows at (to the pixel: the avatar's own height in the picture is
    // measured, and the picture sized again once if it isn't the height asked).
    const float scale = dpi_ / 96;
    const uint32_t ph = uint32_t(std::lround(h * scale / std::max(0.5f, a.fill))), pw = uint32_t(std::lround(ph * 0.62f));
    if (a.built && ph >= 16 && (std::abs(int(ph) - int(a.height)) > 2 || !a.picture.width) && a.attempts < 4) {
      ++a.attempts;
      if (a.renderer.Render(pw, ph, kAvatarYaw, 0, a.picture)) {
        a.width = pw, a.height = ph;
        a.left = pw, a.right = 0, a.top = ph, a.bottom = 0;
        for (uint32_t y = 0; y < ph; ++y)
          for (uint32_t x = 0; x < pw; ++x)
            if (a.picture.bgra[(size_t(y) * pw + x) * 4 + 3] > 24) {
              a.left = std::min(a.left, x), a.right = std::max(a.right, x + 1);
              a.top = std::min(a.top, y), a.bottom = std::max(a.bottom, y + 1);
            }
        if (a.bottom > a.top) a.fill = float(a.bottom - a.top) / float(ph);
        a.stale = true;
      }
    } else if (std::abs(int(ph) - int(a.height)) <= 2) {
      a.attempts = 0;
    }
    if (a.picture.width && (a.stale || a.generation != c.generation())) {
      a.bitmap = c.CreateBitmap(a.picture.width, a.picture.height, a.picture.bgra);
      a.generation = c.generation();
      a.stale = false;
    }
    if (a.bitmap && a.bottom > a.top) {
      const float f = h / float(a.bottom - a.top);  // DIPs per pixel
      const RectF r{cx - (a.left + a.right) / 2.0f * f, feet - a.bottom * f, a.picture.width * f, a.picture.height * f};
      body = {cx - (a.right - a.left) / 2.0f * f, feet - h, (a.right - a.left) * f, h};
      // Its reflection, then the avatar.
      c.PushFade({r.x, feet, r.w, h * 0.32f}, 0.24f, 0);
      c.PushMirror(feet);
      c.DrawBitmap(a.bitmap.Get(), r);
      c.PopMirror();
      c.PopClip();
      c.DrawBitmap(a.bitmap.Get(), r);
      drawn = true;
    }
  }
  if (!drawn) {
    // Nobody playing, no avatar pack yet (or it can't be drawn): the gamerpic, large.
    const float radius = h * 0.17f;
    DrawGamerpic(c, cx, feet - h * 0.55f, radius, player);
    body = {cx - radius - 8 * u, feet - h * 0.55f - radius - 8 * u, 2 * radius + 16 * u, 2 * radius + 16 * u};
    if (!player) {
      c.Text(Tr(kTextHomeSignIn), {cx - h * 0.3f, feet - h * 0.55f + radius + 12 * u, h * 0.6f, 40 * u},
             Style(15 * u, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText, Align::kCenter, true));
    }
  }
  if (focused) {
    c.StrokeEllipse(cx, feet, body.w * 0.62f + 10 * u, 9 * u, kNeon.Alpha(0.9f), 2.2f * u);
    c.StrokeEllipse(cx, feet, body.w * 0.62f + 14 * u, 12 * u, kNeon.Alpha(0.25f), 4 * u);
    // What A does, under it.
    const std::wstring label = player ? Tr(kTextHomeEditAvatar) : Tr(kTextHomeWhoHint);
    TextStyle name = Style(20 * u, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText, Align::kCenter);
    c.Text(player ? Widen(player->gamertag) : L"", {cx - 160 * u, feet + 14 * u, 320 * u, 28 * u}, name);
    c.Text(label, {cx - 160 * u, feet + 14 * u + (player ? 28 * u : 0), 320 * u, 20 * u},
           Style(13.5f * u, DWRITE_FONT_WEIGHT_SEMI_BOLD, kGreenText, Align::kCenter));
  }
  if (hover_ == kIdHomeAvatar && !focused) c.FillRadial(cx, feet - h * 0.45f, h * 0.36f, h * 0.55f, kWhite.Alpha(0.08f), kWhite.Alpha(0));
  AddTarget(body, kIdHomeAvatar, [this] {
    if (home_on_avatar_) OpenHomeAvatar();
    else HomeFocusAvatar();
  });
}

void App::DrawHomeTopBar(Canvas& c, float u, float m, float cy) {
  // Left: the logo, the name and the pill showing who's signed in (A: "Who's playing?").
  const float mark_w = DrawLogo(c, Logo(kLogoMark, 36 * u), m, cy - 18 * u);
  TextStyle brand = Style(30 * u, DWRITE_FONT_WEIGHT_LIGHT, kText);
  brand.vertical = Align::kCenter;
  const float name_x = m + (mark_w ? mark_w + 12 * u : 0);
  const float brand_w = c.MeasureText(L"AnyX360", brand).width;
  c.Text(L"AnyX360", {name_x, cy - 24 * u, brand_w + 4, 46 * u}, brand);
  const x360::profiles::Profile* player = Player();
  const RectF pill{name_x + brand_w + 18 * u, cy - 15 * u, 56 * u, 30 * u};
  c.FillRoundedRect(pill.Offset(0, 2 * u), pill.h / 2, kBlack.Alpha(0.25f));
  c.FillRoundedRect(pill, pill.h / 2, kWhite.Alpha(hover_ == kIdHomePill ? 1.0f : 0.93f));
  c.FillGradient({pill.x + pill.h / 2, pill.y + 2 * u, pill.w - pill.h, pill.h * 0.45f}, kWhite.Alpha(0.9f), kWhite.Alpha(0));
  c.FillCircle(pill.x + pill.h / 2 + 2 * u, pill.cy(), 5 * u, player ? Color::Hex(0x2BB24C) : Color::Hex(0x9A9A9A));
  c.FillRoundedRect({pill.x + pill.h / 2 + 12 * u, pill.cy() - 2 * u, pill.w - pill.h - 8 * u, 4 * u}, 2 * u, Color::Hex(0xC9CDD2));
  if (focus_ == kIdHomePill && focus_visible_) Neon(c, pill, pill.h / 2, u * 0.6f, 0.9f);
  AddTarget(pill.Inset(-4 * u), kIdHomePill, [this] { ShowWhoIsPlaying(false); });

  // Right: the time, the controller, the gamercard.
  SYSTEMTIME local;
  GetLocalTime(&local);
  painted_minute_ = int(local.wHour) * 60 + local.wMinute;
  wchar_t clock[8];
  swprintf(clock, 8, L"%02u:%02u", local.wHour, local.wMinute);
  TextStyle clock_style = Style(21 * u, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText, Align::kEnd);
  clock_style.vertical = Align::kCenter;
  const float clock_w = c.MeasureText(clock, clock_style).width;
  c.Text(clock, {width_ - m - clock_w - 2, cy - 16 * u, clock_w + 2, 32 * u}, clock_style);
  TextStyle pad = Style(22 * u, DWRITE_FONT_WEIGHT_NORMAL, kText.Alpha(pads_ > 0 ? 0.95f : 0.35f), Align::kCenter);
  pad.family = L"Segoe MDL2 Assets";
  pad.vertical = Align::kCenter;
  const float pad_x = width_ - m - clock_w - 26 * u - 28 * u;
  c.Text(L"\xE7FC", {pad_x, cy - 16 * u, 28 * u, 32 * u}, pad);
  if (pads_ > 1) {
    c.Text(std::to_wstring(pads_), {pad_x + 22 * u, cy - 2 * u, 14 * u, 16 * u}, Style(10 * u, DWRITE_FONT_WEIGHT_BOLD, kText));
  }
  // The gamercard: gamerpic, gamertag, gamerscore; opens the Profiles page.
  const std::wstring name = player ? Widen(player->gamertag) : Tr(kTextNobodyPlaying);
  const std::wstring score = player ? std::to_wstring(Gamerscore(player->xuid)) : L"";
  TextStyle name_style = Style(17 * u, DWRITE_FONT_WEIGHT_SEMI_BOLD, kText);
  TextStyle score_style = Style(14 * u, DWRITE_FONT_WEIGHT_NORMAL, Color::Hex(0xDADADA));
  const float text_w = std::max(c.MeasureText(name, name_style).width, c.MeasureText(score, score_style).width + 20 * u);
  const float pic_r = 22 * u;
  const RectF card{pad_x - 26 * u - text_w - 2 * pic_r - 14 * u, cy - pic_r - 6 * u, text_w + 2 * pic_r + 14 * u + 12 * u,
                   2 * pic_r + 12 * u};
  if (hover_ == kIdHomeCard) c.FillRoundedRect(card, card.h / 2, kWhite.Alpha(0.08f));
  DrawGamerpic(c, card.x + 6 * u + pic_r, cy, pic_r, player);
  c.StrokeCircle(card.x + 6 * u + pic_r, cy, pic_r, kWhite.Alpha(0.35f), 1.2f * u);
  const float tx = card.x + 6 * u + 2 * pic_r + 12 * u;
  c.Text(name, {tx, cy - (player ? 22 * u : 12 * u), text_w + 4, 24 * u}, name_style);
  if (player) {
    // The gamerscore's "G" in a ring, then the score.
    const float gx = tx + 7 * u, gy = cy + 11 * u;
    c.StrokeCircle(gx, gy, 7 * u, Color::Hex(0xDADADA), 1.4f * u);
    TextStyle g = Style(9.5f * u, DWRITE_FONT_WEIGHT_BOLD, Color::Hex(0xDADADA), Align::kCenter);
    g.vertical = Align::kCenter;
    c.Text(L"G", {gx - 7 * u, gy - 7.5f * u, 14 * u, 14 * u}, g);
    TextStyle s = score_style;
    s.vertical = Align::kCenter;
    c.Text(score, {tx + 20 * u, gy - 10 * u, text_w, 20 * u}, s);
  }
  if (focus_ == kIdHomeCard && focus_visible_) Neon(c, card, card.h / 2, u * 0.6f, 0.9f);
  AddTarget(card, kIdHomeCard, [this] { ShowPage(Page::kProfiles); });
}

void App::DrawHomePanel(Canvas& c, const RectF& panel, float u) {
  const float radius = 14 * u;
  // A soft shadow, then frosted glass: the backdrop again (hiding the reflections behind it), dark
  // and translucent, a sheen and a fine edge.
  c.FillRoundedRect(panel.Offset(0, 6 * u).Inset(-3 * u), radius + 3 * u, kBlack.Alpha(0.28f));
  c.PushClip(panel, radius);
  if (background_) c.DrawBitmap(Bitmap(c, background_), {0, 0, width_, height_}, 1, true);
  c.FillRect(panel, Color::Hex(0x0C0F14, 0.62f));
  c.FillGradient({panel.x, panel.y, panel.w, panel.h * 0.55f}, kWhite.Alpha(0.09f), kWhite.Alpha(0.02f));
  c.PopClip();
  c.StrokeRoundedRect(panel, radius, kWhite.Alpha(0.16f), 1);
  const struct {
    const wchar_t* glyph;
    Text label;
    Page page;
  } entries[] = {{L"\xE7FC", kTextHomeMyGames, Page::kGames},
                 {L"\xE713", kTextHomeSettings, Page::kSettings},
                 {L"\xE896", kTextHomeAddGames, Page::kAdd}};
  const float w = panel.w / 3;
  for (int i = 0; i < 3; ++i) {
    const int id = kIdHomePanel + i;
    const RectF r{panel.x + i * w, panel.y, w, panel.h};
    const RectF hit = r.Inset(8 * u, 7 * u);
    const bool focused = focus_ == id && focus_visible_;
    if (focused) c.FillRoundedRect(hit, 10 * u, kWhite.Alpha(0.13f));
    else if (hover_ == id) c.FillRoundedRect(hit, 10 * u, kWhite.Alpha(0.07f));
    TextStyle glyph = Style(27 * u, DWRITE_FONT_WEIGHT_NORMAL, kText, Align::kCenter);
    glyph.family = L"Segoe MDL2 Assets";
    glyph.vertical = Align::kCenter;
    c.Text(entries[i].glyph, {r.x, r.y + 8 * u, r.w, 36 * u}, glyph);
    TextStyle label = Style(15 * u, DWRITE_FONT_WEIGHT_NORMAL, kText.Alpha(focused ? 1.0f : 0.88f), Align::kCenter);
    c.Text(Tr(entries[i].label), {r.x + 4, r.y + 46 * u, r.w - 8, 22 * u}, label);
    if (focused) Neon(c, hit, 10 * u, u * 0.55f, 0.85f);
    AddTarget(hit, id, [this, page = entries[i].page] { ShowPage(page); });
  }
}

void App::DrawHomeHints(Canvas& c, const RectF& panel, float u, float m, bool on_game) {
  struct Hint {
    const wchar_t* button;
    uint32_t color;
    std::wstring text;
  };
  const float cy = panel.cy() + 8 * u;
  auto draw = [&](const std::vector<Hint>& hints, float x, float limit, bool right) {
    // From the left edge rightwards, or from the right edge leftwards; those that don't fit are left out.
    for (const Hint& hint : hints) {
      TextStyle letter = Style(11 * u, DWRITE_FONT_WEIGHT_BOLD, Color::Hex(hint.color), Align::kCenter);
      letter.vertical = Align::kCenter;
      const bool round = std::wstring_view(hint.button).size() == 1;
      const float key_w = round ? 22 * u : c.MeasureText(hint.button, letter).width + 14 * u;
      TextStyle label = Style(14 * u, DWRITE_FONT_WEIGHT_NORMAL, Color::Hex(0xD6D6D6));
      label.vertical = Align::kCenter;
      const float text_w = c.MeasureText(hint.text, label).width + 2;
      const float w = key_w + 8 * u + text_w;
      const float left = right ? x - w : x;
      if (right ? left < limit : left + w > limit) break;
      const RectF key{left, cy - 11 * u, key_w, 22 * u};
      c.FillRoundedRect(key, 11 * u, kBlack.Alpha(0.35f));
      c.StrokeRoundedRect(key, 11 * u, Color::Hex(hint.color), 1.4f * u);
      c.Text(hint.button, key, letter);
      c.Text(hint.text, {key.right() + 8 * u, cy - 11 * u, text_w, 22 * u}, label);
      x = right ? left - 22 * u : left + w + 22 * u;
    }
  };
  std::vector<Hint> left = {{L"A", 0x5DC23A, Tr(home_on_avatar_ && focus_ == kIdHomeAvatar ? kTextHomeEditAvatar : kTextHomeSelect)}};
  if (on_game) left.push_back({L"Y", 0xE8B923, Tr(kTextHomeMore)});
  draw(left, m, panel.x - 16 * u, false);
  // At the right: what the last action ended with, else LB RB.
  if (!message_.empty()) {
    TextStyle style = Style(13.5f * u, DWRITE_FONT_WEIGHT_NORMAL, message_.error ? kRedText : kGreenText, Align::kEnd, true);
    style.vertical = Align::kCenter;
    c.Text(message_.Render(), {panel.right() + 16 * u, panel.y, width_ - m - panel.right() - 16 * u, panel.h}, style);
  } else {
    draw({{L"LB RB", 0xBFC4CA, Tr(kTextHomePages)}}, width_ - m, panel.right() + 16 * u, true);
  }
}

void App::DrawHome(Canvas& c) {
  const double now = Now();
  const HomeLayout l = Layout(width_, height_);
  const float u = l.u;
  home_cover_h_ = l.selected.h;

  // The games, newest played first; the selection stays on its game when they change order.
  const std::vector<int> order = HomeOrder();
  if (std::find(order.begin(), order.end(), home_game_) == order.end()) home_game_ = order.empty() ? -1 : order[0];
  for (size_t t = 0; t < tiles_.size(); ++t)
    if (tiles_[t] == home_game_ && int(t) != selected_) Select(int(t));
  const int pos = home_game_ >= 0 ? int(std::find(order.begin(), order.end(), home_game_) - order.begin()) : 0;
  if (float(pos) != home_scroll_target_) {
    // The first frame starts where the selection is; later ones glide.
    home_scroll_.Go(float(pos), now, home_scroll_target_ < 0 ? 0 : 0.3);
    home_scroll_target_ = float(pos);
  }
  const float scroll = home_scroll_.Value(now);

  DrawHomeBackground(c);
  // The floor: darker, a mirror for what stands on it.
  const float haze = 40 * u;
  c.FillGradient({0, l.floor_y - haze, width_, haze}, kBlack.Alpha(0), kBlack.Alpha(0.3f));
  c.FillGradient({0, l.floor_y, width_, height_ - l.floor_y}, kBlack.Alpha(0.38f), kBlack.Alpha(0.82f));
  c.FillGradient({0, l.floor_y, width_, 2 * u}, kWhite.Alpha(0.06f), kWhite.Alpha(0));

  struct Item {
    int game;
    float d;
    Slot slot;
  };
  std::vector<Item> items;
  for (size_t i = 0; i < order.size(); ++i) {
    const float d = float(i) - scroll;
    if (d < -2.5f) continue;
    Slot s = SlotBetween(l, d);
    if (s.opacity <= 0.01f || s.r.x > width_ + 20 || s.r.right() < -20) continue;
    items.push_back({order[i], d, s});
  }
  const float radius = 7 * u;
  const bool carousel_focused = (focus_ >= kIdConsoleRecent && focus_ < kIdConsoleRecent + 100) || focus_ == kIdHomeAdd;
  const bool avatar_focused = focus_ == kIdHomeAvatar;

  // Reflections, then the covers before the selected one, the avatar, the ones after it, and the
  // selected one in front.
  for (const Item& it : items) DrawHomeReflection(c, it.slot.r, it.game, it.slot.opacity, it.slot.dim, it.slot.blur, radius);
  auto cover = [&](const Item& it) {
    const int id = kIdConsoleRecent + it.game;
    const bool selected = it.game == home_game_;
    DrawHomeCover(c, it.slot.r, it.game, it.slot.opacity, it.slot.dim, it.slot.blur, radius, hover_ == id);
    AddTarget(it.slot.r, id, [this, game = it.game] {
      if (game == home_game_ && !home_on_avatar_) PlayGame(game);
      else HomeFocusGame(game);
    }, selected || it.slot.opacity > 0.5f);
  };
  for (const Item& it : items)
    if (it.d < -0.5f) cover(it);
  DrawHomeAvatar(c, l.avatar_cx, l.avatar_feet, l.avatar_h, u, avatar_focused);
  for (auto it = items.rbegin(); it != items.rend(); ++it)
    if (it->d > 0.5f) cover(*it);
  for (const Item& it : items) {
    if (std::abs(it.d) > 0.5f) continue;
    // The selected game: its glow on the floor, its box art, the neon frame.
    const float strength = home_on_avatar_ ? 0.0f : carousel_focused ? 1.0f : 0.55f;
    if (strength > 0) {
      c.FillRadial(it.slot.r.cx(), it.slot.r.bottom() + 6 * u, it.slot.r.w * 0.75f, 18 * u, kNeon.Alpha(0.3f * strength), kNeon.Alpha(0));
      NeonGlow(c, it.slot.r, radius, u, strength * (1 - std::abs(it.d)));
    }
    cover(it);
    if (strength > 0) Neon(c, it.slot.r, radius, u, strength * (1 - std::abs(it.d)));
    else c.StrokeRoundedRect(it.slot.r.Inset(-1.5f * u), radius + 1.5f * u, kWhite.Alpha(0.35f), 1.5f * u);
  }
  if (order.empty()) {
    // No games yet: "Add a game" where the selected one would be.
    const RectF r = l.selected;
    c.FillRoundedRect(r, radius, (hover_ == kIdHomeAdd ? kPanelHover : kPanel).Alpha(0.85f));
    c.Text(L"+", {r.x, r.y + r.h * 0.26f, r.w, r.h * 0.3f}, Style(r.h * 0.2f, DWRITE_FONT_WEIGHT_LIGHT, kTextSecondary, Align::kCenter));
    c.Text(Tr(kTextPageAdd), {r.x, r.y + r.h * 0.58f, r.w, 30 * u}, Style(17 * u, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText, Align::kCenter));
    if (!home_on_avatar_) {
      NeonGlow(c, r, radius, u, carousel_focused ? 1.0f : 0.55f);
      Neon(c, r, radius, u, carousel_focused ? 1.0f : 0.55f);
    }
    AddTarget(r, kIdHomeAdd, [this] {
      if (!home_on_avatar_ && focus_ == kIdHomeAdd) ShowPage(Page::kAdd);
      else HomeFocusGame(-1);
    });
    c.Text(Tr(kTextNoGamesYet), {r.x - 60 * u, l.title_y, r.w + 120 * u, 26 * u},
           Style(15 * u, DWRITE_FONT_WEIGHT_NORMAL, kTextSecondary, Align::kCenter));
  } else if (home_game_ >= 0) {
    // Its title under it, and what the profile playing has done in it.
    const LibraryGame& lib = games_[size_t(home_game_)];
    const float fade = selection_.Value(now);
    const RectF r{l.selected.cx() - 220 * u, l.title_y, 440 * u, 30 * u};
    TextStyle title = Style(24 * u, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText.Alpha(fade), Align::kCenter);
    c.Text(lib.info->title, r, title);
    std::wstring line;
    bool red = false;
    if (lib.location_gone || lib.needs_update) {
      line = lib.location_gone ? Tr(kTextErrorLocationGone, {lib.info->title, ShortPath(lib.dir)})
                               : Tr(kTextNeedsUpdate, {lib.info->title, lib.info->update_version});
      red = true;
    } else if (const x360::profiles::Profile* player = Player()) {
      const auto& stats = TitleStats(player->xuid, lib.info->title_id);
      line = stats && stats->seconds ? Tr(kTextHomePlayed, {PlayTime(stats->seconds)}) : Tr(kTextHomeNewGame);
      if (const x360::achievements::TitleList* list = Achievements(lib); list && !list->achievements.empty())
        line += L"  ·  " + Tr(kTextGamerscoreOf, {std::to_wstring(Unlocks(player->xuid, lib.info->title_id).Gamerscore()),
                                                  std::to_wstring(list->TotalGamerscore())});
    }
    c.Text(line, {r.x - 60 * u, r.bottom() + 1 * u, r.w + 120 * u, 20 * u},
           Style(13.5f * u, DWRITE_FONT_WEIGHT_NORMAL, (red ? kRedText : Color::Hex(0xCFCFCF)).Alpha(fade), Align::kCenter));
  }

  DrawHomePanel(c, l.panel, u);
  DrawHomeTopBar(c, u, l.m, l.top_cy);
  DrawHomeHints(c, l.panel, u, l.m, carousel_focused && home_game_ >= 0);
}

}  // namespace anyx360::launcher::ui
