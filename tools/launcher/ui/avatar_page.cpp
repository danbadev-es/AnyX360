// The avatar editor (docs/AVATARS.md): the dashboard's Avatar page and desktop mode's overlay, the
// viewer beside it, saving into the profile, and gamerpics (a picture of the avatar or a game's).
//
// Controller first: up and down move between the rows, left and right change the row with the
// focus (the avatar changes at once), the right stick turns the avatar and zooms to its face, Y
// makes a random one, Start saves. The mouse clicks a row's arrows and drags the avatar; the wheel
// zooms.
#include "ui/avatar_page.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include "avatar_look.h"
#include "core/catalog.h"
#include "core/library.h"
#include "core/strings.h"
#include "ui/app_internal.h"
#include "util.h"
#include "wic/image.h"
#include "x360/achievements.h"
#include "x360/profiles.h"

namespace anyx360::launcher::ui {

using x360tool::AvatarCategory;
using x360tool::AvatarColor;

namespace {

constexpr int kMaxRows = 48;  // ids: kIdAvatarRow + row, kIdAvatarRow + 50 + row (its left arrow)
constexpr wchar_t kGamerpicFile[] = L"gamerpic.png";

fs::path PackFile() { return ProfilesRoot() / L"system" / x360tool::kAvatarPackToc; }

// The XLanguage of the launcher's language, for the parts' names.
uint32_t PackLanguage() { return CurrentLanguage() == Language::kSpanish ? 5 : 1; }

std::shared_ptr<x360tool::AvatarCatalog> ReadPack() {
  auto pack = std::make_shared<x360tool::AvatarCatalog>();
  try {
    std::error_code ec;
    if (fs::exists(PackFile(), ec)) {
      std::string error;
      pack->load(x360tool::read_file(PackFile()), error);
    }
  } catch (const std::exception&) {
  }
  return pack;
}

Color Argb(uint32_t argb) { return Color::Hex(argb & 0xFFFFFF); }

uint32_t ColorArgb(const Color& c) {
  auto byte = [](float v) { return uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
  return 0xFF000000 | byte(c.r) << 16 | byte(c.g) << 8 | byte(c.b);
}

enum class RowKind { kStep, kColor, kSlider, kAction };

struct Row {
  RowKind kind = RowKind::kStep;
  std::wstring label, value;
  bool sub = false;     // the colours of the row above
  uint32_t swatch = 0;  // kColor: ARGB (0: none)
  float slider = 0;     // kSlider: -1 to 1
  std::function<void(int)> step;
  std::function<void()> activate;
};

Text CategoryText(AvatarCategory category) {
  switch (category) {
    case AvatarCategory::kHair: return kTextAvatarHair;
    case AvatarCategory::kEyes: return kTextAvatarEyes;
    case AvatarCategory::kEyebrows: return kTextAvatarEyebrows;
    case AvatarCategory::kMouth: return kTextAvatarMouth;
    case AvatarCategory::kFacialHair: return kTextAvatarFacialHair;
    case AvatarCategory::kNose: return kTextAvatarNose;
    case AvatarCategory::kChin: return kTextAvatarChin;
    case AvatarCategory::kEars: return kTextAvatarEars;
    case AvatarCategory::kShirt: return kTextAvatarTop;
    case AvatarCategory::kTrousers: return kTextAvatarBottom;
    case AvatarCategory::kShoes: return kTextAvatarShoes;
    case AvatarCategory::kHat: return kTextAvatarHat;
    case AvatarCategory::kGlasses: return kTextAvatarGlasses;
    default: return kTextAvatarAccessories;
  }
}

}  // namespace

// ---- the pack and the profile edited ------------------------------------------------------------

const x360tool::AvatarCatalog* App::AvatarPack() {
  // Read again if there was none and a game has brought one since.
  std::error_code ec;
  if (avatar_pack_ && !avatar_pack_->loaded() && !avatar_pack_reading_ && fs::exists(PackFile(), ec)) avatar_pack_.reset();
  if (avatar_pack_) return avatar_pack_.get();
  if (images_.Synchronous()) {  // screenshots
    avatar_pack_ = ReadPack();
    return avatar_pack_.get();
  }
  if (!avatar_pack_reading_) {
    avatar_pack_reading_ = true;
    std::thread([this] {
      std::shared_ptr<const x360tool::AvatarCatalog> pack = ReadPack();
      host_.Post([this, pack] {
        avatar_pack_ = pack;
        avatar_pack_reading_ = false;
        if (avatar_ && !avatar_->editor) BeginAvatarEdit(avatar_->xuid);
        host_.Invalidate();
      });
    }).detach();
  }
  return nullptr;
}

bool App::BeginAvatarEdit(uint64_t xuid) {
  const auto profile = std::find_if(profiles_.begin(), profiles_.end(), [xuid](const auto& p) { return p.xuid == xuid; });
  if (profile == profiles_.end()) {
    avatar_.reset();
    return false;
  }
  if (!avatar_ || avatar_->xuid != xuid) {
    avatar_ = std::make_unique<AvatarEdit>();
    avatar_->xuid = xuid;
  }
  const x360tool::AvatarCatalog* pack = AvatarPack();
  if (!pack || !pack->loaded()) return true;  // reading it, or there's none: the page says so
  if (avatar_->editor && avatar_->pack.get() == pack) return true;
  avatar_->pack = avatar_pack_;
  x360tool::AvatarDescription d;
  const std::vector<uint8_t> bytes = x360tool::base64_decode(profile->avatar);
  if (!screenshot_avatar_.empty()) {
    d = x360tool::make_avatar_description(*pack, x360tool::AvatarLook::kMale);
    x360tool::customize_avatar(*pack, d, screenshot_avatar_);
  } else if (!x360tool::read_avatar_description(*pack, bytes, d)) {
    // None yet: the default a game gives player 1.
    d = x360tool::make_avatar_description(*pack, x360tool::AvatarLook::kMale);
  }
  avatar_->editor.emplace(*pack, d);
  avatar_->saved = avatar_->editor->bytes();
  avatar_->options.clear();
  avatar_->error.clear();
  ++avatar_->revision;
  return true;
}

bool App::AvatarShown() const {
  if (!overlays_.empty()) return overlays_.back().kind == OverlayKind::kAvatar;
  return console_ && page_ == Page::kAvatar;
}

bool App::AvatarChanged() const { return avatar_ && avatar_->editor && avatar_->editor->bytes() != avatar_->saved; }

void App::OpenAvatarEditor(uint64_t xuid) {
  if (!BeginAvatarEdit(xuid)) return;
  Overlay overlay;
  overlay.kind = OverlayKind::kAvatar;
  overlay.xuid = xuid;
  overlay.back = [this] {
    LeaveAvatar([this] {
      if (!overlays_.empty() && overlays_.back().kind == OverlayKind::kAvatar) PopOverlay();
    });
  };
  PushOverlay(std::move(overlay));
}

void App::LeaveAvatar(std::function<void()> then) {
  if (!AvatarChanged()) {
    avatar_.reset();
    return then();
  }
  Overlay confirm;
  confirm.title = Tr(kTextAvatarLeaveTitle);
  confirm.text = Tr(kTextAvatarLeaveText);
  confirm.buttons.push_back({Tr(kTextSave), [this, then] {
                               PopOverlay();
                               if (!SaveAvatar()) return;
                               avatar_.reset();
                               then();
                             }});
  confirm.buttons.push_back({Tr(kTextDiscard), [this, then] {
                               PopOverlay();
                               avatar_.reset();
                               then();
                             }});
  confirm.buttons.push_back({Tr(kTextCancel), [this] { PopOverlay(); }});
  confirm.back = [this] { PopOverlay(); };
  PushOverlay(std::move(confirm));
}

bool App::SaveAvatar() {
  if (!avatar_ || !avatar_->editor) return false;
  std::optional<x360::profiles::Profile> profile = x360::profiles::Load(ProfilesRoot(), avatar_->xuid);
  const std::vector<uint8_t> bytes = avatar_->editor->bytes();
  bool saved = false;
  if (profile) {
    profile->avatar = x360tool::base64_encode(bytes);
    saved = x360::profiles::Save(ProfilesRoot(), *profile);
  }
  if (!saved) {
    ShowMessage(Tr(kTextProblemTitle), Tr(kTextErrorAvatarWrite, {ShortPath(ProfilesRoot())}), true);
    return false;
  }
  avatar_->saved = bytes;
  // A profile without a gamerpic gets a picture of its avatar.
  if (profile->gamerpic.empty()) {
    Image picture;
    if (RenderAvatarPicture(avatar_->xuid, bytes, 256, picture)) SetGamerpic(avatar_->xuid, &picture, {});
  }
  ReloadProfiles();
  message_ = {kTextAvatarSaved, {Widen(profile->gamertag)}, false};
  return true;
}

// ---- input ---------------------------------------------------------------------------------------

bool App::AvatarKey(Key key) {
  if (!AvatarShown() || !avatar_ || !avatar_->editor) return false;
  const int row = focus_ - kIdAvatarRow;
  if ((key == Key::kLeft || key == Key::kRight) && row >= 0 && row < int(avatar_->steps.size()) && avatar_->steps[size_t(row)]) {
    avatar_->steps[size_t(row)](key == Key::kLeft ? -1 : 1);
    host_.Invalidate();
    return true;
  }
  // Up and down go through the rows in order, those scrolled out of view too (the list scrolls to
  // the focus); down from the last one, to Save; up from the first, as anywhere else (the tabs).
  const int rows = int(avatar_->steps.size());
  if (key == Key::kDown && row >= 0 && row < rows) {
    focus_ = row + 1 < rows ? kIdAvatarRow + row + 1 : AvatarChanged() ? kIdAvatarButton : kIdAvatarButton + 1;
    host_.Invalidate();
    return true;
  }
  if (key == Key::kUp && row > 0 && row < rows) {
    focus_ = kIdAvatarRow + row - 1;
    host_.Invalidate();
    return true;
  }
  if (key == Key::kMore) {  // Y: a random avatar
    avatar_->editor->randomize(uint32_t(GetTickCount64()));
    ++avatar_->revision;
    host_.Invalidate();
    return true;
  }
  if (key == Key::kPlay) {  // Start: save
    if (AvatarChanged()) SaveAvatar();
    host_.Invalidate();
    return true;
  }
  return false;
}

void App::Stick(float x, float y) {
  if (!AvatarShown() || !avatar_) return;
  avatar_->yaw += x * 0.07f;
  avatar_->zoom = std::clamp(avatar_->zoom + y * 0.035f, 0.0f, 1.0f);
  avatar_->turning_until = Now() + 0.1;
  host_.Invalidate();
}

// ---- drawing -------------------------------------------------------------------------------------

void App::DrawAvatarPage(Canvas& c, const RectF& area) {
  const x360::profiles::Profile* player = Player();
  if (!player) {
    c.Text(Tr(kTextAvatarNoProfile), {area.x, area.y, std::min(area.w, 900.0f), 60}, Style(22, DWRITE_FONT_WEIGHT_LIGHT, kText, Align::kStart, true));
    Button(c, {area.x, area.y + 80, 240, 46}, Tr(kTextSwitchProfile), kIdConsoleAction, [this] { ShowWhoIsPlaying(false); });
    return;
  }
  if (!avatar_ || avatar_->xuid != player->xuid) BeginAvatarEdit(player->xuid);
  DrawAvatarEditor(c, area, overlays_.empty(), 1);
}

void App::DrawAvatarOverlay(Canvas& c, Overlay& overlay, bool top) {
  const float fade = overlay.fade.Value(Now());
  c.FillRect({0, 0, width_, height_}, Color::Hex(0x000000, 0.7f * fade));
  const RectF band{0, 28, width_, height_ - 56};
  c.FillRect(band, kPanel.Alpha(fade));
  const x360::profiles::Profile* profile = nullptr;
  for (const auto& p : profiles_)
    if (p.xuid == overlay.xuid) profile = &p;
  const float x = kMargin;
  c.FillRect({x - 18, band.y + 30, 4, 32}, kGreen.Alpha(fade));
  c.Text(Tr(kTextAvatarTitle, {profile ? Widen(profile->gamertag) : L""}), {x, band.y + 22, width_ - 2 * x, 48},
         Style(34, DWRITE_FONT_WEIGHT_LIGHT, kText.Alpha(fade)));
  DrawAvatarEditor(c, {x, band.y + 92, width_ - 2 * x, band.h - 92 - 26}, top, fade);
}

void App::DrawAvatarEditor(Canvas& c, const RectF& area, bool top, float fade) {
  if (!avatar_) return;
  AvatarEdit& edit = *avatar_;
  const x360tool::AvatarCatalog* pack = AvatarPack();
  if (pack && pack->loaded() && !edit.editor) BeginAvatarEdit(edit.xuid);
  if (!edit.editor) {
    const bool reading = !pack;
    c.Text(Tr(reading ? kTextAvatarLoading : kTextAvatarNoPack), {area.x, area.y, std::min(area.w, 900.0f), 80},
           Style(reading ? 22 : 18, DWRITE_FONT_WEIGHT_LIGHT, kText.Alpha(fade), Align::kStart, true));
    if (top && !reading)
      Button(c, {area.x, area.y + 100, 200, 44}, Tr(kTextTermsClose), kIdAvatarButton + 1, [this] {
        if (!overlays_.empty() && overlays_.back().back) overlays_.back().back();
        else ShowPage(Page::kHome);
      });
    return;
  }
  x360tool::AvatarEditor& editor = *edit.editor;

  // The viewer, at the left: as tall as the area (its hint under it), 4:5 at most (the bind pose's
  // arms are wide).
  const float view_h = area.h - 40, view_w = std::min(std::round(view_h * 0.8f), std::round(area.w * 0.45f));
  const RectF view{area.x, area.y, view_w, view_h};
  edit.view = view;
  c.FillGradient(view, Color::Hex(0x2A3A2A, 0.9f * fade), Color::Hex(0x121812, 0.9f * fade));
  const float scale = dpi_ / 96;
  const uint32_t pw = uint32_t(std::lround(view.w * scale)), ph = uint32_t(std::lround(view.h * scale));
  if (edit.built_revision != edit.revision) {
    edit.error.clear();
    if (!edit.renderer.SetAvatar(*edit.pack, editor.description(), edit.error) && edit.error.empty()) edit.error = "?";
    edit.built_revision = edit.revision;
    edit.picture_revision = ~0u;
  }
  if (edit.error.empty() && pw && ph &&
      (edit.picture_revision != edit.revision || edit.picture_w != pw || edit.picture_h != ph || edit.picture_yaw != edit.yaw ||
       edit.picture_zoom != edit.zoom)) {
    if (edit.renderer.Render(pw, ph, edit.yaw, edit.zoom, edit.picture)) {
      edit.picture_revision = edit.revision, edit.picture_w = pw, edit.picture_h = ph;
      edit.picture_yaw = edit.yaw, edit.picture_zoom = edit.zoom;
      edit.bitmap_stale = true;
    }
  }
  if (edit.bitmap_stale || edit.bitmap_generation != c.generation()) {
    edit.bitmap = edit.picture.width ? c.CreateBitmap(edit.picture.width, edit.picture.height, edit.picture.bgra) : nullptr;
    edit.bitmap_generation = c.generation();
    edit.bitmap_stale = false;
  }
  if (!edit.error.empty()) {
    c.Text(Tr(kTextAvatarError, {Widen(edit.error)}), view.Inset(20), Style(15, DWRITE_FONT_WEIGHT_NORMAL, kRedText.Alpha(fade), Align::kStart, true));
  } else if (edit.bitmap) {
    c.DrawBitmap(edit.bitmap.Get(), view, fade);
  }
  c.Text(Tr(kTextAvatarViewHint), {view.x, view.bottom() + 6, view.w, 34},
         Style(12, DWRITE_FONT_WEIGHT_NORMAL, kTextTertiary.Alpha(fade), Align::kStart, true));

  // The rows.
  std::vector<Row> rows;
  const uint32_t language = PackLanguage();
  auto bump = [this] {
    ++avatar_->revision;
    host_.Invalidate();
  };
  auto options = [&](AvatarCategory category) -> const std::vector<x360tool::AvatarOption>& {
    auto key = std::make_pair(int(category), editor.gender());
    auto found = edit.options.find(key);
    if (found == edit.options.end()) found = edit.options.emplace(key, editor.options(category)).first;
    return found->second;
  };
  auto category_rows = [&](AvatarCategory category, bool colours) {
    const auto& list = options(category);
    if (list.empty() || (list.size() == 1 && list[0].index == x360tool::kAvatarNone)) return;
    const uint32_t current = editor.current(category);
    int at = -1;
    for (size_t i = 0; i < list.size(); ++i)
      if (list[i].index == current) at = int(i);
    Row row;
    row.label = Tr(CategoryText(category));
    if (current == x360tool::kAvatarNone)
      row.value = Tr(category == AvatarCategory::kNose || category == AvatarCategory::kChin || category == AvatarCategory::kEars
                         ? kTextAvatarOwnShape
                         : kTextAvatarNone);
    else
      row.value = Widen(edit.pack->display_name(current, language));
    if (at >= 0) row.value += L"   " + std::to_wstring(at + 1) + L"/" + std::to_wstring(list.size());
    row.step = [this, category, at](int dir) {
      x360tool::AvatarEditor& e = *avatar_->editor;
      const auto& choices = avatar_->options[{int(category), e.gender()}];
      const int n = int(choices.size());
      if (!n) return;
      int next = at < 0 ? (dir > 0 ? 0 : n - 1) : at;
      for (int tries = 0; tries < n; ++tries) {
        if (at >= 0 || tries > 0) next = (next + dir + n) % n;
        if (e.choose(category, choices[size_t(next)].index)) break;
      }
      ++avatar_->revision;
      host_.Invalidate();
    };
    row.activate = [step = row.step] { step(1); };
    rows.push_back(std::move(row));
    const uint32_t ways = colours ? editor.colorways(category) : 0;
    if (ways > 1) {
      Row sub;
      sub.kind = RowKind::kColor;
      sub.sub = true;
      sub.label = Tr(kTextAvatarColors);
      const int way = editor.colorway(category);
      sub.value = way < 0 ? Tr(kTextAvatarOwnColors) : Tr(kTextAvatarColorway, {std::to_wstring(way + 1), std::to_wstring(ways)});
      if (const x360tool::AvatarPart* part = edit.pack->find_part(current)) sub.swatch = part->colorway_color(way < 0 ? 0 : uint32_t(way), 0);
      sub.step = [this, category, way, ways, bump](int dir) {
        const int next = way < 0 ? (dir > 0 ? 0 : int(ways) - 1) : (way + dir + int(ways)) % int(ways);
        avatar_->editor->choose_colorway(category, uint32_t(next));
        bump();
      };
      sub.activate = [step = sub.step] { step(1); };
      rows.push_back(std::move(sub));
    }
  };
  auto color_row = [&](Text label, AvatarColor which, std::span<const uint32_t> palette) {
    Row row;
    row.kind = RowKind::kColor;
    row.label = Tr(label);
    const uint32_t current = editor.color(which) | 0xFF000000;
    int at = -1;
    for (size_t i = 0; i < palette.size(); ++i)
      if (palette[i] == current) at = int(i);
    row.swatch = current;
    row.value = at >= 0 ? Tr(kTextAvatarColorway, {std::to_wstring(at + 1), std::to_wstring(palette.size())}) : L"";
    row.step = [this, which, palette, at, bump](int dir) {
      const int n = int(palette.size());
      const int next = at < 0 ? (dir > 0 ? 0 : n - 1) : (at + dir + n) % n;
      avatar_->editor->set_color(which, palette[size_t(next)]);
      bump();
    };
    row.activate = [step = row.step] { step(1); };
    rows.push_back(std::move(row));
  };
  auto slider_row = [&](Text label, float value, bool height) {
    Row row;
    row.kind = RowKind::kSlider;
    row.label = Tr(label);
    row.slider = value;
    row.step = [this, height, bump](int dir) {
      x360tool::AvatarEditor& e = *avatar_->editor;
      if (height) e.set_height(e.height() + 0.1f * dir);
      else e.set_weight(e.weight() + 0.1f * dir);
      bump();
    };
    rows.push_back(std::move(row));
  };
  {
    Row gender;
    gender.label = Tr(kTextAvatarGender);
    gender.value = Tr(editor.gender() == 2 ? kTextAvatarFemale : kTextAvatarMale);
    gender.step = [this, bump](int) {
      avatar_->editor->set_gender(avatar_->editor->gender() == 2 ? 1 : 2);
      bump();
    };
    gender.activate = [step = gender.step] { step(1); };
    rows.push_back(std::move(gender));
  }
  color_row(kTextAvatarSkin, x360tool::kAvatarSkin, x360tool::avatar_skin_palette());
  slider_row(kTextAvatarHeight, editor.height(), true);
  slider_row(kTextAvatarWeight, editor.weight(), false);
  category_rows(AvatarCategory::kHair, false);
  color_row(kTextAvatarHairColor, x360tool::kAvatarHairColor, x360tool::avatar_hair_palette());
  category_rows(AvatarCategory::kEyes, false);
  color_row(kTextAvatarEyeColor, x360tool::kAvatarEyes, x360tool::avatar_eye_palette());
  category_rows(AvatarCategory::kEyebrows, false);
  category_rows(AvatarCategory::kMouth, false);
  color_row(kTextAvatarLipColor, x360tool::kAvatarLips, x360tool::avatar_lip_palette());
  category_rows(AvatarCategory::kFacialHair, false);
  category_rows(AvatarCategory::kNose, false);
  category_rows(AvatarCategory::kChin, false);
  category_rows(AvatarCategory::kEars, false);
  for (AvatarCategory category : {AvatarCategory::kShirt, AvatarCategory::kTrousers, AvatarCategory::kShoes, AvatarCategory::kHat,
                                  AvatarCategory::kGlasses, AvatarCategory::kAccessories})
    category_rows(category, true);
  {
    Row random;
    random.kind = RowKind::kAction;
    random.label = Tr(kTextAvatarRandom);
    random.activate = [this, bump] {
      avatar_->editor->randomize(uint32_t(GetTickCount64()));
      bump();
    };
    rows.push_back(std::move(random));
    Row gamerpic;
    gamerpic.kind = RowKind::kAction;
    gamerpic.label = Tr(kTextAvatarGamerpic);
    gamerpic.activate = [this] { OpenGamerpicPicker(avatar_->xuid); };
    rows.push_back(std::move(gamerpic));
  }
  if (rows.size() > size_t(kMaxRows)) rows.resize(kMaxRows);
  edit.steps.clear();
  for (const Row& row : rows) edit.steps.push_back(row.step);

  const float list_x = view.right() + 32, list_w = std::min(area.right() - list_x, 720.0f);
  const float row_h = 42, gap = 4, buttons_h = 58;
  const int visible = std::max(1, int((area.h - buttons_h + gap) / (row_h + gap)));
  const int focused = focus_ - kIdAvatarRow;
  if (focused >= 0 && focused < int(rows.size())) {
    if (focused < edit.first_row) edit.first_row = focused;
    if (focused >= edit.first_row + visible) edit.first_row = focused - visible + 1;
  }
  edit.first_row = std::clamp(edit.first_row, 0, std::max(0, int(rows.size()) - visible));
  TextStyle label = Style(16, DWRITE_FONT_WEIGHT_SEMI_LIGHT, kText.Alpha(fade));
  label.vertical = Align::kCenter;
  TextStyle sub_label = Style(14, DWRITE_FONT_WEIGHT_NORMAL, kTextSecondary.Alpha(fade));
  sub_label.vertical = Align::kCenter;
  TextStyle value = Style(15, DWRITE_FONT_WEIGHT_NORMAL, kText.Alpha(fade), Align::kCenter);
  value.vertical = Align::kCenter;
  TextStyle arrow = Style(20, DWRITE_FONT_WEIGHT_NORMAL, kTextSecondary.Alpha(fade), Align::kCenter);
  arrow.vertical = Align::kCenter;
  for (int i = edit.first_row; i < int(rows.size()) && i < edit.first_row + visible; ++i) {
    const Row& row = rows[size_t(i)];
    const RectF r{list_x, area.y + (i - edit.first_row) * (row_h + gap), list_w, row_h};
    const int id = kIdAvatarRow + i;
    const bool focused_row = focus_ == id;
    c.FillRect(r, (hover_ == id ? kPanelHover : focused_row ? Color::Hex(0x333333) : kPanelHigh).Alpha(fade));
    if (row.kind == RowKind::kAction) {
      TextStyle action = Style(16, DWRITE_FONT_WEIGHT_SEMI_BOLD, kGreenText.Alpha(fade));
      action.vertical = Align::kCenter;
      c.Text(row.label, r.Inset(16, 0), action);
    } else {
      c.Text(row.label, {r.x + (row.sub ? 34 : 16), r.y, r.w * 0.38f, r.h}, row.sub ? sub_label : label);
      const RectF box{r.x + r.w * 0.4f, r.y, r.w * 0.6f - 8, r.h};
      const RectF left{box.x, box.y, 34, box.h}, right{box.right() - 34, box.y, 34, box.h};
      c.Text(L"‹", left, arrow);
      c.Text(L"›", right, arrow);
      RectF inner{left.right(), box.y, box.w - 68, box.h};
      if (row.kind == RowKind::kSlider) {
        const RectF track{inner.x + 12, r.cy() - 2, inner.w - 24, 4};
        c.FillRect(track, Color::Hex(0x4A4A4A, fade));
        const float at = track.x + track.w * (row.slider + 1) / 2;
        c.FillRect({track.cx() - 1, track.y - 4, 2, 12}, Color::Hex(0x6A6A6A, fade));
        c.FillCircle(at, r.cy(), 8, (focused_row ? kGreenHover : kText).Alpha(fade));
      } else {
        if (row.kind == RowKind::kColor && row.swatch) {
          const float text_w = row.value.empty() ? 0 : c.MeasureText(row.value, value).width + 12;
          const float cx = inner.cx() - text_w / 2;
          c.FillCircle(cx, r.cy(), 11, Argb(row.swatch).Alpha(fade));
          c.StrokeCircle(cx, r.cy(), 11, Color::Hex(0xFFFFFF, 0.35f * fade), 1);
          inner = {cx + 17, inner.y, text_w, inner.h};
          TextStyle start = value;
          start.align = Align::kStart;
          c.Text(row.value, inner, start);
        } else {
          c.Text(row.value, inner, value);
        }
      }
      if (top) {
        // The arrows: the previous and next choice (the row itself is the next).
        AddTarget(r, id, row.activate ? row.activate : [step = row.step] { step(1); });
        AddTarget(left, kIdAvatarRow + 50 + i, [step = row.step] { step(-1); }, false);
      }
    }
    if (row.kind == RowKind::kAction && top) AddTarget(r, id, row.activate);
    if (focused_row && focus_visible_) DrawFocus(c, r);
  }
  // Scrolled: a hint of more above or below.
  if (edit.first_row > 0)
    c.Text(L"▲", {list_x + list_w - 30, area.y - 22, 30, 18}, Style(11, DWRITE_FONT_WEIGHT_NORMAL, kTextTertiary.Alpha(fade), Align::kCenter));
  if (edit.first_row + visible < int(rows.size()))
    c.Text(L"▼", {list_x + list_w - 30, area.y + visible * (row_h + gap) - 2, 30, 18},
           Style(11, DWRITE_FONT_WEIGHT_NORMAL, kTextTertiary.Alpha(fade), Align::kCenter));

  // Save and Cancel (Close without changes).
  if (top) {
    const float by = area.bottom() - 46;
    Button(c, {list_x, by, 180, 46}, Tr(kTextSave), kIdAvatarButton, [this] { SaveAvatar(); }, true, AvatarChanged());
    const bool overlay = !overlays_.empty() && overlays_.back().kind == OverlayKind::kAvatar;
    Button(c, {list_x + 190, by, 180, 46}, Tr(AvatarChanged() ? kTextCancel : kTextTermsClose), kIdAvatarButton + 1,
           [this, overlay] {
             if (AvatarChanged()) {
               // Back to what was saved.
               x360tool::AvatarDescription d;
               if (x360tool::read_avatar_description(*avatar_->pack, avatar_->saved, d)) avatar_->editor.emplace(*avatar_->pack, d);
               ++avatar_->revision;
               return;
             }
             if (overlay) overlays_.back().back();
             else ShowPage(Page::kHome);
           });
  }
}

// ---- gamerpics ------------------------------------------------------------------------------------

bool App::RenderAvatarPicture(uint64_t xuid, const std::vector<uint8_t>& description, uint32_t size, Image& out) {
  const x360tool::AvatarCatalog* pack = AvatarPack();
  if (!pack || !pack->loaded()) return false;
  x360tool::AvatarDescription d;
  std::vector<uint8_t> bytes = description;
  if (bytes.empty())
    if (std::optional<x360::profiles::Profile> profile = x360::profiles::Load(ProfilesRoot(), xuid))
      bytes = x360tool::base64_decode(profile->avatar);
  if (!x360tool::read_avatar_description(*pack, bytes, d)) d = x360tool::make_avatar_description(*pack, x360tool::AvatarLook::kMale);
  AvatarRenderer renderer;
  std::string error;
  return renderer.SetAvatar(*pack, d, error) && renderer.Render(size, size, 0.35f, 0.85f, out, ColorArgb(ProfileColor(xuid)));
}

bool App::SetGamerpic(uint64_t xuid, const Image* picture, const fs::path& file) {
  std::optional<x360::profiles::Profile> profile = x360::profiles::Load(ProfilesRoot(), xuid);
  if (!profile) return false;
  const fs::path folder = x360::profiles::ProfileFolder(ProfilesRoot(), xuid), out = folder / kGamerpicFile;
  std::error_code ec;
  bool ok = true;
  if (picture || !file.empty()) {
    Image decoded;
    std::string error;
    if (!picture) {
      ok = anyx360::ui::DecodePicture(file, decoded, error, 256, 256);
      picture = &decoded;
    }
    fs::create_directories(folder, ec);
    // Through a temporary file, so a game never reads half a picture.
    fs::path partial = out;
    partial += L".partial";
    ok = ok && anyx360::ui::SavePng(*picture, partial, error);
    if (ok) {
      fs::rename(partial, out, ec);
      ok = !ec;
    }
    profile->gamerpic = Narrow(kGamerpicFile);
  } else {
    fs::remove(out, ec);  // the initial again
    profile->gamerpic.clear();
  }
  ok = ok && x360::profiles::Save(ProfilesRoot(), *profile);
  if (!ok) ShowMessage(Tr(kTextProblemTitle), Tr(kTextErrorAvatarWrite, {ShortPath(folder)}), true);
  else message_ = {kTextGamerpicSaved, {Widen(profile->gamertag)}, false};
  ReloadProfiles();
  return ok;
}

void App::OpenGamerpicPicker(uint64_t xuid) {
  const auto profile = std::find_if(profiles_.begin(), profiles_.end(), [xuid](const auto& p) { return p.xuid == xuid; });
  if (profile == profiles_.end()) return;
  Overlay overlay;
  overlay.kind = OverlayKind::kGamerpic;
  overlay.xuid = xuid;
  overlay.title = Tr(kTextGamerpicTitle, {Widen(profile->gamertag)});
  overlay.text = Tr(kTextGamerpicText);
  // The avatar as it is now (being edited, or saved).
  auto picture = std::make_shared<Image>();
  const std::vector<uint8_t> description =
      avatar_ && avatar_->xuid == xuid && avatar_->editor ? avatar_->editor->bytes() : std::vector<uint8_t>();
  if (RenderAvatarPicture(xuid, description, 256, *picture)) overlay.picture = picture;
  // Each game's icon and its achievements' pictures.
  std::error_code ec;
  for (const LibraryGame& game : games_) {
    if (!game.installed) continue;
    if (!game.icon.empty() && fs::exists(game.icon, ec)) overlay.pictures.push_back(game.icon);
    std::vector<fs::path> pictures;
    for (const auto& entry : fs::directory_iterator(x360::achievements::TitleFolder(ProfilesRoot(), game.info->title_id), ec))
      if (entry.path().extension() == L".png") pictures.push_back(entry.path());
    std::sort(pictures.begin(), pictures.end());
    overlay.pictures.insert(overlay.pictures.end(), pictures.begin(), pictures.end());
  }
  overlay.buttons.push_back({Tr(kTextCancel), [this] { PopOverlay(); }});
  overlay.primary = false;
  overlay.back = [this] { PopOverlay(); };
  PushOverlay(std::move(overlay));
}

void App::DrawGamerpicPicker(Canvas& c, Overlay& overlay, bool top) {
  const float fade = overlay.fade.Value(Now());
  c.FillRect({0, 0, width_, height_}, Color::Hex(0x000000, 0.7f * fade));
  const float column_w = std::min(960.0f, width_ - 2 * kMargin);
  const float x = (width_ - column_w) / 2;
  const RectF band{0, 36, width_, height_ - 72};
  c.FillRect(band, kPanel.Alpha(fade));
  float y = band.y + 30;
  c.FillRect({x - 18, y + 8, 4, 32}, kGreen.Alpha(fade));
  c.Text(overlay.title, {x, y, column_w, 48}, Style(34, DWRITE_FONT_WEIGHT_LIGHT, kText.Alpha(fade)));
  y += 60;
  TextStyle text = Body();
  text.color = kTextSecondary.Alpha(fade);
  c.Text(overlay.text, {x, y, column_w, 44}, text);
  y += 56;
  // The avatar's, the initial, then the games'.
  const size_t extra = overlay.picture ? 2 : 1;
  const size_t count = extra + overlay.pictures.size();
  const float tile = 96, gap = 14;
  const int per_row = std::max(1, int((column_w + gap) / (tile + gap)));
  const int rows_shown = std::max(1, int((band.bottom() - 80 - y + gap) / (tile + gap)));
  int first_row = 0;
  if (focus_ >= kIdGamerpic && focus_ < kIdGamerpic + int(count)) first_row = std::max(0, (focus_ - kIdGamerpic) / per_row - rows_shown + 1);
  const float scale = dpi_ / 96;
  const x360::profiles::Profile* profile = nullptr;
  for (const auto& p : profiles_)
    if (p.xuid == overlay.xuid) profile = &p;
  for (size_t i = size_t(first_row) * per_row; i < count; ++i) {
    const int row = int(i) / per_row - first_row;
    if (row >= rows_shown) break;
    const RectF r{x + (i % per_row) * (tile + gap), y + row * (tile + gap), tile, tile};
    const int id = kIdGamerpic + int(i);
    c.FillRect(r, (hover_ == id ? kPanelHover : kPanelHigh).Alpha(fade));
    std::function<void()> choose;
    const uint64_t xuid = overlay.xuid;
    if (overlay.picture && i == 0) {
      ID2D1Bitmap* bitmap = Bitmap(c, overlay.picture);
      if (bitmap) c.DrawBitmap(bitmap, r.Inset(4), fade);
      choose = [this, xuid, picture = overlay.picture] {
        PopOverlay();
        SetGamerpic(xuid, picture.get(), {});
      };
    } else if (i == extra - 1) {
      // The initial, as before.
      x360::profiles::Profile plain = profile ? *profile : x360::profiles::Profile{};
      plain.gamerpic.clear();
      DrawGamerpic(c, r.cx(), r.cy(), tile / 2 - 8, profile ? &plain : nullptr, fade);
      choose = [this, xuid] {
        PopOverlay();
        SetGamerpic(xuid, nullptr, {});
      };
    } else {
      const fs::path& file = overlay.pictures[i - extra];
      if (ID2D1Bitmap* bitmap = Bitmap(c, images_.Picture(file, uint32_t(tile * scale), uint32_t(tile * scale))))
        c.DrawBitmap(bitmap, r.Inset(4), fade);
      choose = [this, xuid, file] {
        PopOverlay();
        SetGamerpic(xuid, nullptr, file);
      };
    }
    if (focus_ == id && focus_visible_) DrawFocus(c, r);
    if (top) AddTarget(r, id, std::move(choose));
  }
  float bx = x + column_w;
  for (int i = int(overlay.buttons.size()) - 1; i >= 0; --i) {
    const Overlay::Action& action = overlay.buttons[size_t(i)];
    const float w = std::max(132.0f, c.MeasureText(action.label, Style(16, DWRITE_FONT_WEIGHT_SEMI_BOLD, kText)).width + 44);
    if (top) Button(c, {bx - w, band.bottom() - 30 - 42, w, 42}, action.label, kIdButton + i, action.run, false, action.enabled);
    bx -= w + 10;
  }
}

// ---- the command line ----------------------------------------------------------------------------

int RenderAvatarFile(const fs::path& out, int width, int height, float yaw, float zoom, const std::string& spec) {
  std::shared_ptr<x360tool::AvatarCatalog> pack = ReadPack();
  if (!pack->loaded()) return 2;
  x360tool::AvatarDescription d = x360tool::make_avatar_description(*pack, x360tool::AvatarLook::kMale);
  if (!spec.empty()) {
    x360tool::customize_avatar(*pack, d, spec);
  } else {
    const x360::profile::SignIn sign_in = x360::profile::Read(x360::profile::File(ProfilesRoot()));
    if (std::optional<x360::profiles::Profile> profile = x360::profiles::Load(ProfilesRoot(), sign_in.slots[0]))
      x360tool::read_avatar_description(*pack, x360tool::base64_decode(profile->avatar), d);
  }
  AvatarRenderer renderer;
  Image image;
  std::string error;
  if (!renderer.SetAvatar(*pack, d, error) || !renderer.Render(uint32_t(width), uint32_t(height), yaw, zoom, image)) return 3;
  return anyx360::ui::SavePng(image, out, error) ? 0 : 1;
}

}  // namespace anyx360::launcher::ui
