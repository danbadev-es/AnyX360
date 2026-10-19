// AnyX360's launcher (AnyX360.exe): a gallery of the games the player has added to the release, from
// their own copies, and the place to start them. The logic (the library, adding and removing games,
// the text, the terms, starting a game) is in core/ (launcher_core); this file is the window.
//
// `--archive <file>` and `--folder <dir>` add a game without asking (unattended tests), `--select
// <id>` selects one.
//
// The text is in English and Spanish: Windows' display language picks one, the footer switches
// (remembered). The terms of use must be accepted before the first use, and again whenever they
// change.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <future>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "anyx360_version.h"
#include "core/catalog.h"
#include "core/jobs.h"
#include "core/launch.h"
#include "core/library.h"
#include "core/paths.h"
#include "core/strings.h"
#include "core/terms.h"
#include "util.h"

namespace gp = Gdiplus;
using namespace anyx360::launcher;

#define ANYX360_WIDEN2(x) L##x
#define ANYX360_WIDEN(x) ANYX360_WIDEN2(x)

namespace {

const wchar_t* const kVersion = ANYX360_WIDEN(ANYX360_VERSION_FULL);

constexpr UINT kMsgProgress = WM_APP + 1;    // wParam: Step, lParam: percent (-1 when unknown)
constexpr UINT kMsgDone = WM_APP + 2;        // lParam: Outcome* of the job
constexpr UINT kMsgGameExited = WM_APP + 3;  // wParam: exit code
constexpr UINT kMsgAskReplace = WM_APP + 4;  // wParam: std::promise<bool>* answered, lParam: const CatalogGame*
constexpr UINT kMsgJobGame = WM_APP + 5;     // lParam: const CatalogGame* the job turned out to be
constexpr UINT kMsgIconReady = WM_APP + 6;   // a game's icon was read

enum Control { kTitle = 100, kSubtitle, kGallery, kSupported, kMessage, kStatus, kProgress, kPlay, kAdd, kLogs, kFooter };
enum TermsControl { kTermsHeading = 200, kTermsText, kTermsCheck, kTermsAccept, kTermsDecline };
enum MenuCommand { kMenuArchive = 1, kMenuFolder, kMenuPlay, kMenuCover, kMenuRestoreCover, kMenuLogs, kMenuPrepareAgain,
                   kMenuRemove };
enum class State { kIdle, kWorking, kRunning };

// ---- state ------------------------------------------------------------------------------------

// A library game with its pictures decoded.
struct Game : LibraryGame {
  std::unique_ptr<gp::Bitmap> icon_image, cover_image;
  gp::Color color{255, 62, 70, 92};
};

HWND g_window = nullptr;
HWND g_gallery = nullptr;
HFONT g_title_font = nullptr, g_text_font = nullptr, g_small_font = nullptr;
std::vector<Game> g_games;     // the catalog games this release has, in catalog order
std::vector<int> g_tiles;      // the gallery: indexes of installed games, then -1 for "Add game"
int g_selected = 0;            // index into g_tiles
int g_hover = -1;
int g_scroll = 0;
State g_state = State::kIdle;
Message g_message;  // shown above the status
Step g_step = Step::kChecking;
int g_percent = -1;
const CatalogGame* g_job_game = nullptr;  // the game being added or removed, once known
Worker g_worker;
bool g_close_after_cancel = false;
const CatalogGame* g_running = nullptr;

// The job's reports, passed to the window's thread.
class WindowObserver : public JobObserver {
 public:
  void OnProgress(Step step, int percent) override { PostMessageW(g_window, kMsgProgress, WPARAM(step), percent); }
  void OnGame(const CatalogGame& game) override { PostMessageW(g_window, kMsgJobGame, 0, LPARAM(&game)); }
  bool AskReplace(const CatalogGame& game) override {
    std::promise<bool> answer;
    std::future<bool> answered = answer.get_future();
    PostMessageW(g_window, kMsgAskReplace, WPARAM(&answer), LPARAM(&game));
    return answered.get();
  }
  void OnDone(const Outcome& outcome) override {
    PostMessageW(g_window, kMsgDone, 0, LPARAM(new Outcome(outcome)));
  }
} g_observer;

// ---- images -----------------------------------------------------------------------------------

// An image file decoded into memory (GDI+ keeps a file it loads open, and a cover can be replaced).
std::unique_ptr<gp::Bitmap> DecodeImage(const fs::path& path) {
  std::vector<uint8_t> bytes;
  try {
    bytes = x360tool::read_file(path);
  } catch (const std::exception&) {
    return nullptr;
  }
  IStream* stream = SHCreateMemStream(bytes.data(), UINT(bytes.size()));
  if (!stream) return nullptr;
  std::unique_ptr<gp::Bitmap> decoded(gp::Bitmap::FromStream(stream));
  std::unique_ptr<gp::Bitmap> copy;
  if (decoded && decoded->GetLastStatus() == gp::Ok && decoded->GetWidth() && decoded->GetHeight()) {
    copy = std::make_unique<gp::Bitmap>(INT(decoded->GetWidth()), INT(decoded->GetHeight()), PixelFormat32bppPARGB);
    gp::Graphics graphics(copy.get());
    graphics.DrawImage(decoded.get(), 0, 0, INT(decoded->GetWidth()), INT(decoded->GetHeight()));
  }
  decoded.reset();
  stream->Release();
  return copy;
}

// The color a tile is painted with: the average of the icon's colorful pixels, kept dark enough for
// white text.
gp::Color TileColor(gp::Bitmap* icon) {
  gp::Color fallback(255, 62, 70, 92);
  if (!icon) return fallback;
  gp::Rect rect(0, 0, INT(icon->GetWidth()), INT(icon->GetHeight()));
  gp::BitmapData data;
  if (icon->LockBits(&rect, gp::ImageLockModeRead, PixelFormat32bppARGB, &data) != gp::Ok) return fallback;
  double sum[3] = {}, weight = 0;
  for (UINT y = 0; y < data.Height; ++y) {
    const uint32_t* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(data.Scan0) + y * data.Stride);
    for (UINT x = 0; x < data.Width; ++x) {
      uint32_t argb = row[x];
      if ((argb >> 24) < 128) continue;
      double r = (argb >> 16 & 0xFF) / 255.0, g = (argb >> 8 & 0xFF) / 255.0, b = (argb & 0xFF) / 255.0;
      double high = std::max({r, g, b}), low = std::min({r, g, b});
      double saturation = high > 0 ? (high - low) / high : 0;
      double w = saturation * saturation * high + 0.01;
      sum[0] += r * w, sum[1] += g * w, sum[2] += b * w, weight += w;
    }
  }
  icon->UnlockBits(&data);
  if (weight <= 0) return fallback;
  double r = sum[0] / weight, g = sum[1] / weight, b = sum[2] / weight;
  // Scale the brightest channel to 0.55 (dark enough for white text, still colorful).
  double high = std::max({r, g, b, 0.01});
  double scale = 0.55 / high;
  auto channel = [&](double v) { return BYTE(std::clamp(v * scale, 0.0, 1.0) * 255.0 + 0.5); };
  return gp::Color(255, channel(r), channel(g), channel(b));
}

// ---- the library ------------------------------------------------------------------------------

int SelectedGame() {
  return g_selected >= 0 && g_selected < int(g_tiles.size()) ? g_tiles[size_t(g_selected)] : -1;
}

// Reads what the player has added; keeps the selection on the same game (or `select`).
void LoadGames(std::wstring select = {}) {
  if (select.empty() && SelectedGame() >= 0) select = g_games[size_t(SelectedGame())].info->id;
  g_games.clear();
  for (LibraryGame& read : ReadLibrary()) {
    Game game;
    static_cast<LibraryGame&>(game) = std::move(read);
    if (game.installed) {
      if (!game.icon.empty()) game.icon_image = DecodeImage(game.icon);
      if (!game.cover.empty()) game.cover_image = DecodeImage(game.cover);
      game.color = TileColor(game.icon_image.get());
      // A copy added before icons were read: read it now, the tile is redrawn when it is ready.
      if (game.needs_icon) {
        std::thread([info = game.info, dir = game.dir] {
          if (SaveIcon(*info, dir)) PostMessageW(g_window, kMsgIconReady, 0, 0);
        }).detach();
      }
    }
    g_games.push_back(std::move(game));
  }
  g_tiles.clear();
  for (size_t i = 0; i < g_games.size(); ++i)
    if (g_games[i].installed) g_tiles.push_back(int(i));
  g_tiles.push_back(-1);
  g_selected = 0;
  for (size_t t = 0; t < g_tiles.size(); ++t)
    if (g_tiles[t] >= 0 && g_games[size_t(g_tiles[t])].info->id == select) g_selected = int(t);
}

// The terms for the read-only edit control, which needs \r\n.
std::wstring TermsEditText() {
  std::wstring crlf;
  for (wchar_t c : TermsText(CurrentLanguage())) {
    if (c == L'\n') crlf += L'\r';
    crlf += c;
  }
  return crlf;
}

// ---- gallery ----------------------------------------------------------------------------------

int Scale(int value) { return MulDiv(value, int(GetDpiForWindow(g_window)), 96); }
float ScaleF(float value) { return value * float(GetDpiForWindow(g_window)) / 96.0f; }

struct GalleryMetrics {
  int tile_w, tile_h, gap, pad, columns, content_h;
};

GalleryMetrics Metrics() {
  RECT client{};
  GetClientRect(g_gallery, &client);
  GalleryMetrics m{Scale(184), Scale(232), Scale(22), Scale(12), 1, 0};
  m.columns = std::max(1, (int(client.right) - 2 * m.pad + m.gap) / (m.tile_w + m.gap));
  int rows = (int(g_tiles.size()) + m.columns - 1) / m.columns;
  m.content_h = 2 * m.pad + rows * m.tile_h + std::max(0, rows - 1) * m.gap;
  return m;
}

RECT TileRect(int tile, const GalleryMetrics& m) {
  int column = tile % m.columns, row = tile / m.columns;
  int x = m.pad + column * (m.tile_w + m.gap), y = m.pad + row * (m.tile_h + m.gap) - g_scroll;
  return {x, y, x + m.tile_w, y + m.tile_h};
}

int TileAt(int x, int y) {
  GalleryMetrics m = Metrics();
  for (int t = 0; t < int(g_tiles.size()); ++t) {
    RECT r = TileRect(t, m);
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return t;
  }
  return -1;
}

void ClampScroll() {
  RECT client{};
  GetClientRect(g_gallery, &client);
  g_scroll = std::clamp(g_scroll, 0, std::max(0, Metrics().content_h - int(client.bottom)));
}

void EnsureVisible(int tile) {
  RECT client{};
  GetClientRect(g_gallery, &client);
  GalleryMetrics m = Metrics();
  RECT r = TileRect(tile, m);
  if (r.top < m.pad) g_scroll += r.top - m.pad;
  if (r.bottom > client.bottom - m.pad) g_scroll += r.bottom - (client.bottom - m.pad);
  ClampScroll();
}

void RoundedRect(gp::GraphicsPath& path, const gp::RectF& r, float radius) {
  float d = std::min({radius * 2, r.Width, r.Height});
  path.AddArc(r.X, r.Y, d, d, 180, 90);
  path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
  path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
  path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
  path.CloseFigure();
}

gp::Color Shade(const gp::Color& c, float factor) {
  auto channel = [factor](BYTE v) { return BYTE(std::clamp(float(v) * factor, 0.0f, 255.0f)); };
  return gp::Color(c.GetA(), channel(c.GetR()), channel(c.GetG()), channel(c.GetB()));
}

// Draws `image` to fill `dest`, cropping what doesn't fit (like a poster).
void DrawCover(gp::Graphics& g, gp::Bitmap* image, const gp::RectF& dest) {
  float w = float(image->GetWidth()), h = float(image->GetHeight());
  float scale = std::max(dest.Width / w, dest.Height / h);
  float src_w = dest.Width / scale, src_h = dest.Height / scale;
  g.DrawImage(image, dest, (w - src_w) / 2, (h - src_h) / 2, src_w, src_h, gp::UnitPixel);
}

void DrawGameTile(gp::Graphics& g, const gp::RectF& r, const Game& game, bool hover) {
  gp::GraphicsPath tile;
  RoundedRect(tile, r, ScaleF(12));
  gp::LinearGradientBrush background(gp::PointF(r.X, r.Y), gp::PointF(r.X, r.GetBottom()), Shade(game.color, 1.25f),
                                     Shade(game.color, 0.6f));
  g.FillPath(&background, &tile);
  g.SetClip(&tile);
  float caption_h = ScaleF(54);
  gp::RectF picture(r.X, r.Y, r.Width, r.Height - caption_h);
  g.SetInterpolationMode(gp::InterpolationModeHighQualityBicubic);
  if (game.cover_image) {
    DrawCover(g, game.cover_image.get(), picture);
  } else if (game.icon_image) {
    float size = ScaleF(128);
    gp::RectF icon(picture.X + (picture.Width - size) / 2, picture.Y + (picture.Height - size) / 2 + ScaleF(4), size,
                   size);
    gp::GraphicsPath shadow;
    RoundedRect(shadow, gp::RectF(icon.X + ScaleF(3), icon.Y + ScaleF(5), size, size), ScaleF(10));
    gp::SolidBrush shadow_brush(gp::Color(70, 0, 0, 0));
    g.FillPath(&shadow_brush, &shadow);
    gp::GraphicsPath icon_path;
    RoundedRect(icon_path, icon, ScaleF(10));
    g.SetClip(&icon_path, gp::CombineModeIntersect);
    g.DrawImage(game.icon_image.get(), icon);
    g.SetClip(&tile);
  }
  gp::RectF caption(r.X, r.GetBottom() - caption_h, r.Width, caption_h);
  gp::SolidBrush caption_brush(gp::Color(game.cover_image ? 170 : 70, 0, 0, 0));
  g.FillRectangle(&caption_brush, caption);
  gp::Font font(L"Segoe UI Semibold", ScaleF(15), gp::FontStyleRegular, gp::UnitPixel);
  gp::StringFormat format;
  format.SetAlignment(gp::StringAlignmentCenter);
  format.SetLineAlignment(gp::StringAlignmentCenter);
  format.SetTrimming(gp::StringTrimmingEllipsisWord);
  gp::SolidBrush white(gp::Color(255, 255, 255, 255));
  gp::RectF text(caption.X + ScaleF(10), caption.Y, caption.Width - ScaleF(20), caption.Height);
  g.DrawString(game.info->title, -1, &font, text, &format, &white);
  if (game.location_gone) {
    gp::SolidBrush faded(gp::Color(150, 255, 255, 255));
    g.FillRectangle(&faded, r);
  }
  if (hover) {
    gp::SolidBrush light(gp::Color(28, 255, 255, 255));
    g.FillRectangle(&light, r);
  }
  g.ResetClip();
}

void DrawAddTile(gp::Graphics& g, const gp::RectF& r, bool hover) {
  gp::GraphicsPath tile;
  RoundedRect(tile, r, ScaleF(12));
  gp::SolidBrush background(hover ? gp::Color(255, 236, 240, 247) : gp::Color(255, 245, 246, 248));
  g.FillPath(&background, &tile);
  gp::Pen border(gp::Color(255, 160, 166, 178), ScaleF(2));
  border.SetDashStyle(gp::DashStyleDash);
  g.DrawPath(&border, &tile);
  gp::Pen plus(gp::Color(255, 110, 118, 132), ScaleF(5));
  plus.SetStartCap(gp::LineCapRound);
  plus.SetEndCap(gp::LineCapRound);
  float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2 - ScaleF(18), arm = ScaleF(22);
  g.DrawLine(&plus, cx - arm, cy, cx + arm, cy);
  g.DrawLine(&plus, cx, cy - arm, cx, cy + arm);
  gp::Font font(L"Segoe UI Semibold", ScaleF(15), gp::FontStyleRegular, gp::UnitPixel);
  gp::StringFormat format;
  format.SetAlignment(gp::StringAlignmentCenter);
  gp::SolidBrush text_brush(gp::Color(255, 70, 76, 88));
  std::wstring label = Tr(kTextAddTile);
  g.DrawString(label.c_str(), -1, &font, gp::RectF(r.X, cy + arm + ScaleF(14), r.Width, ScaleF(30)), &format,
               &text_brush);
}

void PaintGallery(HWND hwnd) {
  PAINTSTRUCT paint;
  HDC dc = BeginPaint(hwnd, &paint);
  RECT client{};
  GetClientRect(hwnd, &client);
  int w = std::max<int>(1, client.right), h = std::max<int>(1, client.bottom);
  gp::Bitmap buffer(w, h, PixelFormat32bppPARGB);
  {
    gp::Graphics g(&buffer);
    g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(gp::TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(gp::PixelOffsetModeHighQuality);
    g.Clear(gp::Color(255, 255, 255, 255));
    GalleryMetrics m = Metrics();
    bool focused = GetFocus() == hwnd;
    for (int t = 0; t < int(g_tiles.size()); ++t) {
      RECT tr = TileRect(t, m);
      if (tr.bottom < 0 || tr.top > h) continue;
      gp::RectF r(float(tr.left), float(tr.top), float(tr.right - tr.left), float(tr.bottom - tr.top));
      if (g_tiles[size_t(t)] < 0)
        DrawAddTile(g, r, t == g_hover);
      else
        DrawGameTile(g, r, g_games[size_t(g_tiles[size_t(t)])], t == g_hover);
      if (t == g_selected) {
        float inset = ScaleF(5);
        gp::GraphicsPath outline;
        RoundedRect(outline, gp::RectF(r.X - inset, r.Y - inset, r.Width + 2 * inset, r.Height + 2 * inset),
                    ScaleF(16));
        gp::Pen pen(focused ? gp::Color(255, 0, 103, 192) : gp::Color(255, 120, 150, 190), ScaleF(3));
        g.DrawPath(&pen, &outline);
      }
    }
  }
  gp::Graphics screen(dc);
  screen.DrawImage(&buffer, 0, 0);
  EndPaint(hwnd, &paint);
}

// ---- window -----------------------------------------------------------------------------------

void Layout();
bool ShowTerms(bool require_acceptance);
void OnPlay();
void OnAdd(HWND anchor, POINT at);
void ShowTileMenu(int tile, POINT at);

void SelectTile(int tile) {
  if (tile < 0 || tile >= int(g_tiles.size())) return;
  g_selected = tile;
  EnsureVisible(tile);
  InvalidateRect(g_gallery, nullptr, FALSE);
  void UpdateUi();
  UpdateUi();
}

const Game* SelectedGamePtr() {
  int index = SelectedGame();
  return index >= 0 ? &g_games[size_t(index)] : nullptr;
}

void UpdateUi() {
  const Game* game = SelectedGamePtr();
  bool playable = game && !game->location_gone && g_state == State::kIdle;
  std::wstring status;
  bool status_error = false;
  if (g_state == State::kWorking) {
    std::wstring title = g_job_game ? g_job_game->title : L"";
    status = g_step == Step::kRemoving                      ? Tr(kTextRemoving, {title})
             : g_step == Step::kFinishing                   ? Tr(kTextFinishing)
             : g_step == Step::kExtracting && g_job_game     ? Tr(kTextExtracting, {title, std::to_wstring(std::max(g_percent, 0))})
                                                            : Tr(kTextChecking);
  } else if (!game) {
    status = Tr(kTextAddHint, {JoinTitles(AvailableGames())});
  } else if (game->location_gone) {
    status = Tr(kTextErrorLocationGone, {game->info->title, ShortPath(game->dir)});
    status_error = true;
  } else {
    status = Tr(game->info->webcam ? kTextReadyWebcam : kTextReadyPad, {game->info->title});
  }
  std::vector<const CatalogGame*> others;
  for (const Game& g : g_games)
    if (!g.installed) others.push_back(g.info);
  SetDlgItemTextW(g_window, kSupported, others.empty() ? L"" : Tr(kTextAlsoSupported, {JoinTitles(others)}).c_str());
  SetDlgItemTextW(g_window, kMessage, g_message.Render().c_str());
  SetDlgItemTextW(g_window, kStatus, status.c_str());
  SetWindowLongPtrW(GetDlgItem(g_window, kStatus), GWLP_USERDATA, status_error);
  ShowWindow(GetDlgItem(g_window, kMessage), g_message.empty() ? SW_HIDE : SW_SHOW);
  HWND progress = GetDlgItem(g_window, kProgress);
  ShowWindow(progress, g_state == State::kWorking ? SW_SHOW : SW_HIDE);
  if (g_percent < 0) {
    SetWindowLongPtrW(progress, GWL_STYLE, GetWindowLongPtrW(progress, GWL_STYLE) | PBS_MARQUEE);
    SendMessageW(progress, PBM_SETMARQUEE, TRUE, 30);
  } else {
    SendMessageW(progress, PBM_SETMARQUEE, FALSE, 0);
    SetWindowLongPtrW(progress, GWL_STYLE, GetWindowLongPtrW(progress, GWL_STYLE) & ~LONG_PTR(PBS_MARQUEE));
    SendMessageW(progress, PBM_SETPOS, g_percent, 0);
  }
  EnableWindow(GetDlgItem(g_window, kPlay), playable);
  SetDlgItemTextW(g_window, kAdd, Tr(g_state == State::kWorking ? kTextCancel : kTextAddGame).c_str());
  EnableWindow(GetDlgItem(g_window, kAdd), g_state != State::kRunning);
  fs::path logs = game ? LogDir(*game->info) : Root() / L"logs";
  EnableWindow(GetDlgItem(g_window, kLogs), Exists(logs));
  SendMessageW(g_window, DM_SETDEFID, playable ? kPlay : kAdd, 0);
  InvalidateRect(GetDlgItem(g_window, kStatus), nullptr, TRUE);
  InvalidateRect(g_gallery, nullptr, FALSE);
}

// The texts that don't depend on the state, after a language change.
void ApplyLanguage() {
  SetDlgItemTextW(g_window, kSubtitle, Tr(kTextSubtitle, {kVersion}).c_str());
  SetDlgItemTextW(g_window, kPlay, Tr(kTextPlay).c_str());
  SetDlgItemTextW(g_window, kLogs, Tr(kTextLogs).c_str());
  SetDlgItemTextW(g_window, kFooter, Tr(kTextFooter).c_str());
  UpdateUi();
}

fs::path ChooseFile(const std::wstring& title, const std::vector<std::pair<std::wstring, std::wstring>>& filters,
                    bool folder) {
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
  if (SUCCEEDED(dialog->Show(g_window)) && SUCCEEDED(dialog->GetResult(&item))) {
    wchar_t* path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) chosen = path;
    CoTaskMemFree(path);
    item->Release();
  }
  dialog->Release();
  return chosen;
}

void StartJob(std::function<Outcome(Job&)> work) {
  g_state = State::kWorking;
  g_step = Step::kChecking;
  g_percent = -1;
  g_message = {};
  g_job_game = nullptr;
  g_worker.Start(g_observer, std::move(work));
  UpdateUi();
}

// `from_folder`: the player's folder, used in place; otherwise an archive (.7z, .rar or .zip) extracted here.
void StartAdding(const fs::path& source, bool from_folder) {
  StartJob([source, from_folder](Job& job) { return from_folder ? AddFolder(job, source) : AddArchive(job, source); });
}

void CancelJob() { g_worker.Cancel(); }

// "Add game": where the copy comes from, in a menu at `at` (screen coordinates).
void OnAdd(HWND anchor, POINT at) {
  if (g_state == State::kWorking) return CancelJob();
  if (g_state != State::kIdle) return;
  std::wstring from_archive = Tr(kTextMenuArchive), from_folder = Tr(kTextMenuFolder);
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kMenuArchive, from_archive.c_str());
  AppendMenuW(menu, MF_STRING, kMenuFolder, from_folder.c_str());
  int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, at.x, at.y, 0, anchor,
                              nullptr);
  DestroyMenu(menu);
  if (choice == kMenuArchive) {
    if (SevenZip().empty()) {
      MessageBoxW(g_window, Tr(kTextMissing7Zip).c_str(), L"AnyX360", MB_ICONERROR);
      return;
    }
    fs::path archive = ChooseFile(Tr(kTextDialogTitle),
                                  {{Tr(kTextFilterArchive), L"*.7z;*.rar;*.zip"}, {Tr(kTextFilterAll), L"*.*"}}, false);
    if (!archive.empty()) StartAdding(archive, false);
  } else if (choice == kMenuFolder) {
    fs::path folder = ChooseFile(Tr(kTextFolderTitle), {}, true);
    if (!folder.empty()) StartAdding(folder, true);
  }
}

void OnAddButton() {
  RECT button;
  GetWindowRect(GetDlgItem(g_window, kAdd), &button);
  OnAdd(g_window, {button.left, button.bottom});
}

void OnPlay() {
  const Game* game = SelectedGamePtr();
  if (!game || game->location_gone || g_state != State::kIdle) return;
  Message error = StartGame(*game, [](uint32_t code) { PostMessageW(g_window, kMsgGameExited, code, 0); });
  if (!error.empty()) {
    g_message = error;
    UpdateUi();
    return;
  }
  g_state = State::kRunning;
  g_running = game->info;
  g_message = {};
  ShowWindow(g_window, SW_HIDE);
}

void ChangeCover(const Game& game) {
  fs::path image = ChooseFile(Tr(kTextCoverTitle, {game.info->title}),
                              {{Tr(kTextFilterImages), L"*.png;*.jpg;*.jpeg"}, {Tr(kTextFilterAll), L"*.*"}}, false);
  if (image.empty()) return;
  if (!DecodeImage(image) || !SetPlayerCover(*game.info, image)) {
    g_message = {kTextErrorCover, {}, true};
    UpdateUi();
    return;
  }
  g_message = {};
  LoadGames();
  UpdateUi();
}

void ShowTileMenu(int tile, POINT at) {
  if (tile < 0 || tile >= int(g_tiles.size())) return;
  SelectTile(tile);
  if (g_tiles[size_t(tile)] < 0) return OnAdd(g_gallery, at);
  if (g_state != State::kIdle) return;
  const Game& game = g_games[size_t(g_tiles[size_t(tile)])];
  std::vector<std::pair<int, std::wstring>> items = {
      {kMenuPlay, Tr(kTextMenuPlay)},
      {0, L""},
      {kMenuCover, Tr(kTextMenuCover)},
      {kMenuRestoreCover, Tr(kTextMenuRestoreCover)},
      {kMenuLogs, Tr(kTextMenuLogs)},
      {0, L""},
      {kMenuPrepareAgain, Tr(kTextMenuPrepareAgain)},
      {kMenuRemove, Tr(kTextMenuRemove)},
  };
  HMENU menu = CreatePopupMenu();
  for (const auto& [id, text] : items) {
    UINT flags = id ? MF_STRING : MF_SEPARATOR;
    if ((id == kMenuPlay && game.location_gone) || (id == kMenuRestoreCover && game.cover.empty()) ||
        (id == kMenuLogs && !Exists(LogDir(*game.info))))
      flags |= MF_GRAYED;
    AppendMenuW(menu, flags, UINT_PTR(id), text.c_str());
  }
  if (!game.location_gone) SetMenuDefaultItem(menu, kMenuPlay, FALSE);
  int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, at.x, at.y, 0,
                              g_gallery, nullptr);
  DestroyMenu(menu);
  const CatalogGame* info = game.info;
  switch (choice) {
    case kMenuPlay: OnPlay(); break;
    case kMenuCover: ChangeCover(game); break;
    case kMenuRestoreCover:
      RestoreCover(*info);
      LoadGames();
      UpdateUi();
      break;
    case kMenuLogs: ShellExecuteW(g_window, L"open", LogDir(*game.info).c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case kMenuPrepareAgain: OnAdd(g_gallery, at); break;
    case kMenuRemove:
      if (MessageBoxW(g_window, Tr(kTextConfirmRemove, {game.info->title}).c_str(), L"AnyX360",
                      MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
        StartJob([info](Job& job) { return RemoveGame(job, *info); });
      }
      break;
  }
}

LRESULT CALLBACK GalleryProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT: PaintGallery(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
      ClampScroll();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_LBUTTONDOWN: {
      SetFocus(hwnd);
      int tile = TileAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (tile >= 0) SelectTile(tile);
      return 0;
    }
    case WM_LBUTTONDBLCLK: {
      int tile = TileAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (tile < 0) return 0;
      SelectTile(tile);
      if (g_tiles[size_t(tile)] < 0) {
        POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ClientToScreen(hwnd, &at);
        OnAdd(hwnd, at);
      } else {
        OnPlay();
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      int tile = TileAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (tile != g_hover) {
        g_hover = tile;
        InvalidateRect(hwnd, nullptr, FALSE);
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
      }
      SetCursor(LoadCursorW(nullptr, tile >= 0 ? IDC_HAND : IDC_ARROW));
      return 0;
    }
    case WM_MOUSELEAVE:
      g_hover = -1;
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_MOUSEWHEEL:
      g_scroll -= GET_WHEEL_DELTA_WPARAM(wparam) * Scale(120) / WHEEL_DELTA;
      ClampScroll();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_CONTEXTMENU: {
      POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      int tile = g_selected;
      if (at.x == -1 && at.y == -1) {  // the keyboard's menu key: under the selected tile
        RECT r = TileRect(tile, Metrics());
        at = {r.left + Scale(16), r.top + Scale(16)};
        ClientToScreen(hwnd, &at);
      } else {
        POINT client = at;
        ScreenToClient(hwnd, &client);
        tile = TileAt(client.x, client.y);
      }
      ShowTileMenu(tile, at);
      return 0;
    }
    case WM_KEYDOWN: {
      int columns = Metrics().columns;
      int tile = g_selected;
      switch (wparam) {
        case VK_LEFT: tile -= 1; break;
        case VK_RIGHT: tile += 1; break;
        case VK_UP: tile -= columns; break;
        case VK_DOWN: tile += columns; break;
        case VK_HOME: tile = 0; break;
        case VK_END: tile = int(g_tiles.size()) - 1; break;
        default: return DefWindowProcW(hwnd, message, wparam, lparam);
      }
      SelectTile(std::clamp(tile, 0, int(g_tiles.size()) - 1));
      return 0;
    }
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: InvalidateRect(hwnd, nullptr, FALSE); return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

void CreateFonts(UINT dpi) {
  for (HFONT font : {g_title_font, g_text_font, g_small_font})
    if (font) DeleteObject(font);
  auto make = [dpi](int points, int weight) {
    return CreateFontW(-MulDiv(points, int(dpi), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  };
  g_title_font = make(20, FW_SEMIBOLD);
  g_text_font = make(10, FW_NORMAL);
  g_small_font = make(9, FW_NORMAL);
  for (int id : {kTitle, kSubtitle, kSupported, kStatus, kMessage, kPlay, kAdd, kLogs, kFooter}) {
    HFONT font = id == kTitle ? g_title_font : id == kSubtitle || id == kFooter || id == kSupported ? g_small_font
                                                                                                      : g_text_font;
    SendDlgItemMessageW(g_window, id, WM_SETFONT, WPARAM(font), TRUE);
  }
}

constexpr int kClientWidth = 860, kClientHeight = 640, kMinWidth = 560, kMinHeight = 520;

void Layout() {
  RECT client{};
  GetClientRect(g_window, &client);
  int dpi = int(GetDpiForWindow(g_window));
  int w = MulDiv(client.right, 96, dpi), h = MulDiv(client.bottom, 96, dpi);  // in 96-DPI units
  int inner = w - 48;
  struct Item {
    int id, x, y, w, h;
  };
  const Item items[] = {
      {kTitle, 24, 14, inner, 40},
      {kSubtitle, 26, 54, inner, 20},
      {kGallery, 12, 82, w - 24, h - 82 - 212},
      {kSupported, 24, h - 204, inner, 20},
      {kMessage, 24, h - 182, inner, 40},
      {kStatus, 24, h - 138, inner, 40},
      {kProgress, 24, h - 94, inner, 14},
      {kPlay, 24, h - 72, 150, 36},
      {kAdd, 186, h - 72, 190, 36},
      {kLogs, w - 24 - 128, h - 72, 128, 36},
      {kFooter, 24, h - 28, inner, 20},
  };
  for (const Item& item : items)
    SetWindowPos(GetDlgItem(g_window, item.id), nullptr, Scale(item.x), Scale(item.y), Scale(std::max(item.w, 1)),
                 Scale(std::max(item.h, 1)), SWP_NOZORDER | SWP_NOACTIVATE);
  ClampScroll();
}

void CreateControls() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  auto add = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, g_window, HMENU(INT_PTR(id)),
                           instance, nullptr);
  };
  add(L"STATIC", L"AnyX360", SS_LEFT | SS_NOPREFIX, kTitle);
  add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kSubtitle);
  g_gallery = add(L"AnyX360Gallery", L"", WS_TABSTOP, kGallery);
  add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kSupported);
  add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kMessage);
  add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kStatus);
  add(PROGRESS_CLASSW, L"", 0, kProgress);
  add(L"BUTTON", L"", BS_DEFPUSHBUTTON | WS_TABSTOP, kPlay);
  add(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, kAdd);
  add(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, kLogs);
  add(WC_LINK, L"", WS_TABSTOP, kFooter);
  SendDlgItemMessageW(g_window, kProgress, PBM_SETRANGE32, 0, 100);
}

// ---- terms window ------------------------------------------------------------------------------

struct TermsWindow {
  HWND hwnd = nullptr;
  bool accepted = false;
  bool done = false;
} g_terms;

LRESULT CALLBACK TermsProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kTermsCheck:
          EnableWindow(GetDlgItem(hwnd, kTermsAccept), IsDlgButtonChecked(hwnd, kTermsCheck) == BST_CHECKED);
          return 0;
        case kTermsAccept:
          g_terms.accepted = true;
          DestroyWindow(hwnd);
          return 0;
        case kTermsDecline:
        case IDCANCEL:
          DestroyWindow(hwnd);
          return 0;
      }
      break;
    case WM_CTLCOLORSTATIC: {  // also the read-only text box
      HDC dc = HDC(wparam);
      SetBkColor(dc, RGB(255, 255, 255));
      SetTextColor(dc, RGB(25, 25, 25));
      return LRESULT(GetStockObject(WHITE_BRUSH));
    }
    case WM_DESTROY:
      g_terms.done = true;
      return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

// Shows the terms of use in the current language. With `require_acceptance` the player must tick
// "I accept" and press Accept (remembered); otherwise it's just for reading. Returns whether they
// were accepted.
bool ShowTerms(bool require_acceptance) {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  static bool registered = [&] {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = TermsProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = HBRUSH(GetStockObject(WHITE_BRUSH));
    wc.lpszClassName = L"AnyX360Terms";
    return RegisterClassExW(&wc) != 0;
  }();
  (void)registered;
  g_terms = {};
  const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
  RECT frame{0, 0, Scale(640), Scale(530)};
  AdjustWindowRectExForDpi(&frame, style, FALSE, 0, GetDpiForWindow(g_window));
  int width = frame.right - frame.left, height = frame.bottom - frame.top;
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTOPRIMARY), &monitor);
  std::wstring title = Tr(kTextTermsTitle);
  g_terms.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"AnyX360Terms", title.c_str(), style,
                                 (monitor.rcWork.left + monitor.rcWork.right - width) / 2,
                                 (monitor.rcWork.top + monitor.rcWork.bottom - height) / 2, width, height,
                                 IsWindowVisible(g_window) ? g_window : nullptr, nullptr, instance, nullptr);
  auto add = [&](const wchar_t* cls, const std::wstring& text, DWORD control_style, int id, int x, int y, int w,
                 int h, HFONT font) {
    HWND control = CreateWindowExW(0, cls, text.c_str(), WS_CHILD | WS_VISIBLE | control_style, Scale(x), Scale(y),
                                   Scale(w), Scale(h), g_terms.hwnd, HMENU(INT_PTR(id)), instance, nullptr);
    SendMessageW(control, WM_SETFONT, WPARAM(font), TRUE);
    return control;
  };
  add(L"STATIC", Tr(kTextTermsHeading), SS_LEFT | SS_NOPREFIX, kTermsHeading, 24, 16, 592, 32, g_title_font);
  add(L"EDIT", TermsEditText(), WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_TABSTOP,
      kTermsText, 24, 58, 592, 370, g_text_font);
  HWND focus;
  if (require_acceptance) {
    focus = add(L"BUTTON", Tr(kTextTermsCheck), BS_AUTOCHECKBOX | WS_TABSTOP, kTermsCheck, 24, 440, 592, 24,
                g_text_font);
    HWND accept = add(L"BUTTON", Tr(kTextTermsAccept), BS_DEFPUSHBUTTON | WS_TABSTOP, kTermsAccept, 380, 480, 112,
                      34, g_text_font);
    EnableWindow(accept, FALSE);
    add(L"BUTTON", Tr(kTextTermsDecline), BS_PUSHBUTTON | WS_TABSTOP, kTermsDecline, 504, 480, 112, 34, g_text_font);
  } else {
    focus = add(L"BUTTON", Tr(kTextTermsClose), BS_DEFPUSHBUTTON | WS_TABSTOP, kTermsDecline, 504, 480, 112, 34,
                g_text_font);
  }
  bool owner_enabled = IsWindowVisible(g_window);
  if (owner_enabled) EnableWindow(g_window, FALSE);
  ShowWindow(g_terms.hwnd, SW_SHOW);
  SetForegroundWindow(g_terms.hwnd);
  SetFocus(focus);
  MSG msg;
  while (!g_terms.done) {
    BOOL got = GetMessageW(&msg, nullptr, 0, 0);
    if (got <= 0) {
      if (got == 0) PostQuitMessage(int(msg.wParam));  // leave WM_QUIT for the main loop
      break;
    }
    if (IsDialogMessageW(g_terms.hwnd, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (owner_enabled) {
    EnableWindow(g_window, TRUE);
    SetForegroundWindow(g_window);
  }
  if (require_acceptance && g_terms.accepted) AcceptTerms();
  return g_terms.accepted;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_NOTIFY: {
      const NMLINK* link = reinterpret_cast<const NMLINK*>(lparam);
      if (link->hdr.idFrom != kFooter || (link->hdr.code != NM_CLICK && link->hdr.code != NM_RETURN)) break;
      if (link->item.iLink == 0) {
        ShowTerms(false);
      } else {  // the other language
        SaveLanguage(CurrentLanguage() == Language::kSpanish ? Language::kEnglish : Language::kSpanish);
        ApplyLanguage();
      }
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kPlay: OnPlay(); return 0;
        case kAdd: OnAddButton(); return 0;
        case kLogs: {
          const Game* game = SelectedGamePtr();
          fs::path logs = game && Exists(LogDir(*game->info)) ? LogDir(*game->info) : Root() / L"logs";
          ShellExecuteW(hwnd, L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          return 0;
        }
      }
      break;
    case kMsgProgress:
      g_step = Step(wparam);
      g_percent = int(lparam);
      UpdateUi();
      return 0;
    case kMsgJobGame:
      g_job_game = reinterpret_cast<const CatalogGame*>(lparam);
      UpdateUi();
      return 0;
    case kMsgAskReplace: {
      const CatalogGame* game = reinterpret_cast<const CatalogGame*>(lparam);
      reinterpret_cast<std::promise<bool>*>(wparam)->set_value(
          MessageBoxW(hwnd, Tr(kTextConfirmReplace, {game->title}).c_str(), L"AnyX360", MB_YESNO | MB_ICONQUESTION) ==
          IDYES);
      return 0;
    }
    case kMsgDone: {
      g_worker.Join();
      std::unique_ptr<Outcome> outcome(reinterpret_cast<Outcome*>(lparam));
      g_state = State::kIdle;
      g_message = outcome->message;
      g_job_game = nullptr;
      if (g_close_after_cancel) {
        DestroyWindow(hwnd);
        return 0;
      }
      LoadGames(outcome->select);
      EnsureVisible(g_selected);
      UpdateUi();
      return 0;
    }
    case kMsgIconReady:
      if (g_state == State::kIdle) {
        LoadGames();
        UpdateUi();
      }
      return 0;
    case kMsgGameExited: {
      if (wparam == 0) {  // closed normally (or restarted from the game's settings panel)
        DestroyWindow(hwnd);
        return 0;
      }
      g_state = State::kIdle;
      wchar_t code[16];
      swprintf(code, 16, DWORD(wparam) > 255 ? L"0x%08X" : L"%u", DWORD(wparam));
      g_message = {kTextErrorExited, {g_running ? g_running->title : L"", code}, true};
      g_running = nullptr;
      UpdateUi();
      ShowWindow(hwnd, SW_SHOW);
      SetForegroundWindow(hwnd);
      return 0;
    }
    case WM_CTLCOLORSTATIC: {
      HDC dc = HDC(wparam);
      HWND control = HWND(lparam);
      int id = GetDlgCtrlID(control);
      SetBkColor(dc, RGB(255, 255, 255));
      SetTextColor(dc, id == kSubtitle || id == kFooter || id == kSupported ? RGB(110, 110, 110)
                       : id == kMessage && g_message.error                 ? RGB(170, 30, 30)
                       : id == kMessage                                    ? RGB(20, 110, 40)
                       : id == kStatus && GetWindowLongPtrW(control, GWLP_USERDATA) ? RGB(170, 30, 30)
                                                                                     : RGB(25, 25, 25));
      return LRESULT(GetStockObject(WHITE_BRUSH));
    }
    case WM_SIZE:
      Layout();
      return 0;
    case WM_GETMINMAXINFO: {
      if (!g_window) break;
      MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lparam);
      RECT frame{0, 0, Scale(kMinWidth), Scale(kMinHeight)};
      AdjustWindowRectExForDpi(&frame, DWORD(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE, 0, GetDpiForWindow(hwnd));
      info->ptMinTrackSize = {frame.right - frame.left, frame.bottom - frame.top};
      return 0;
    }
    case WM_DPICHANGED: {
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      CreateFonts(HIWORD(wparam));
      Layout();
      return 0;
    }
    case WM_CLOSE:
      if (g_state == State::kWorking) {
        if (MessageBoxW(hwnd, Tr(kTextConfirmCancelExit).c_str(), L"AnyX360", MB_YESNO | MB_ICONQUESTION) != IDYES)
          return 0;
        g_close_after_cancel = true;
        CancelJob();  // the window closes when the worker has cleaned up
        return 0;
      }
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_LINK_CLASS};
  InitCommonControlsEx(&controls);
  gp::GdiplusStartupInput gdiplus_input;
  ULONG_PTR gdiplus = 0;
  gp::GdiplusStartup(&gdiplus, &gdiplus_input, nullptr);
  LoadLanguage();

  WNDCLASSEXW gallery{sizeof(gallery)};
  gallery.style = CS_DBLCLKS;
  gallery.lpfnWndProc = GalleryProc;
  gallery.hInstance = instance;
  gallery.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  gallery.lpszClassName = L"AnyX360Gallery";
  RegisterClassExW(&gallery);

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = HBRUSH(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = L"AnyX360Launcher";
  RegisterClassExW(&wc);
  const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
  g_window = CreateWindowExW(0, wc.lpszClassName, L"AnyX360", style, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr,
                             nullptr, instance, nullptr);
  if (!g_window) return 1;

  // Size the client area for the window's DPI and center it on its monitor.
  UINT dpi = GetDpiForWindow(g_window);
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTOPRIMARY), &monitor);
  RECT frame{0, 0, Scale(kClientWidth), Scale(kClientHeight)};
  AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
  int width = std::min<int>(frame.right - frame.left, monitor.rcWork.right - monitor.rcWork.left);
  int height = std::min<int>(frame.bottom - frame.top, monitor.rcWork.bottom - monitor.rcWork.top);
  CreateControls();
  CreateFonts(dpi);
  SetWindowPos(g_window, nullptr, (monitor.rcWork.left + monitor.rcWork.right - width) / 2,
               (monitor.rcWork.top + monitor.rcWork.bottom - height) / 2, width, height, SWP_NOZORDER);
  std::wstring select;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i + 1 < argc; ++i)
    if (std::wstring(argv[i]) == L"--select") select = argv[i + 1];
  LoadGames(select);
  Layout();
  ApplyLanguage();
  if (!TermsAccepted() && !ShowTerms(true)) {
    DestroyWindow(g_window);
    gp::GdiplusShutdown(gdiplus);
    CoUninitialize();
    return 0;
  }
  ShowWindow(g_window, show);
  SetFocus(g_gallery);
  for (int i = 1; i + 1 < argc && g_state == State::kIdle; ++i) {  // one job: the first one given
    std::wstring arg = argv[i];
    if (arg == L"--archive" && !SevenZip().empty()) StartAdding(argv[i + 1], false);
    if (arg == L"--folder") StartAdding(argv[i + 1], true);
  }
  LocalFree(argv);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (IsDialogMessageW(g_window, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  g_worker.Join();
  g_games.clear();  // the bitmaps go before GDI+ does
  gp::GdiplusShutdown(gdiplus);
  CoUninitialize();
  return 0;
}
