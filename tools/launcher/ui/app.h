// The launcher's window contents, drawn with Direct2D (Metro dark, like the Xbox 360's last
// dashboard): the player's games as box art in a row, the selected one's details and Play, a
// collapsible "compatible games" drawer (this release's games and the coming ones), and overlays
// instead of dialogs (add a game, progress, messages, confirmations, a password, the ⋯ menu, the
// terms, "Who's playing?" and the profiles, x360/profiles.h). Mouse, keyboard and an Xbox
// controller move one focus.
//
// Everything is laid out while it is drawn: Paint() records what the pointer and the focus can
// reach, and input acts on that.
#pragma once

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/jobs.h"
#include "core/library.h"
#include "x360/achievements.h"
#include "x360/profiles.h"
#include "d2d/canvas.h"
#include "ui/images.h"

namespace anyx360::launcher::ui {

using anyx360::ui::Canvas;
using anyx360::ui::Color;
using anyx360::ui::ComPtr;
using anyx360::ui::RectF;

// What the window does for the app (screenshots have no window).
class Host {
 public:
  virtual ~Host() = default;
  virtual void Invalidate() = 0;
  // Runs `fn` on the UI thread (called from any thread).
  virtual void Post(std::function<void()> fn) = 0;
  virtual HWND Window() = 0;  // for the system's file dialogs; null in screenshots
  // Shown (in front, painted and on the screen when this returns) or hidden.
  virtual void ShowWindow(bool show) = 0;
  virtual void Close() = 0;
  // Console mode: borderless at the window's place, or over its monitor; desktop mode: the usual
  // window again.
  virtual void SetBorderless(bool borderless, bool fullscreen) { (void)borderless, (void)fullscreen; }
  // The client area on the screen, in pixels (false in screenshots).
  virtual bool ScreenRect(RECT& rect) {
    (void)rect;
    return false;
  }
};

enum class Key { kLeft, kRight, kUp, kDown, kActivate, kBack, kMore, kCompatible, kNext, kPrevious, kHome, kEnd, kPlay, kGuide };

// The dashboard's pages (console mode).
enum class Page { kHome, kGames, kAchievements, kProfiles, kSettings, kAdd };

class App : public JobObserver {
 public:
  explicit App(Host& host);
  ~App() override;

  // Reads the library; the selection stays on the same game (or `select`).
  void Load(std::wstring select = {});
  // Shows the terms if they haven't been accepted (the launcher closes if they are declined), and
  // "Who's playing?" (under them) if no profile is remembered.
  void CheckTerms();

  void Paint(Canvas& canvas);
  // Something moves: paint again on the next frame.
  bool Animating() const;

  void MouseMove(float x, float y);
  void MouseLeave();
  void MouseDown(float x, float y);
  void MouseUp(float x, float y);
  void Wheel(float x, float y, float notches);
  // Right click: the ⋯ menu of the game under the pointer.
  void RightClick(float x, float y);
  // The pointer is on something that acts (the hand cursor).
  bool PointerOnTarget() const { return hover_ >= 0; }
  void KeyPressed(Key key, bool from_pad);
  // A controller is connected: its button hints show.
  void PadConnected();
  void Character(wchar_t c);
  void Paste(const std::wstring& text);
  bool WantsText() const;
  // The window is asked to close: false keeps it open (a job is running; the player is asked).
  bool OnClose();

  // A file or folder dropped on the window or named on the command line.
  void AddSource(const fs::path& source);
  // Saves the game's .anyx backup to `out` (named on the command line; empty: the player chooses).
  void SaveAnyx(std::wstring_view id, const fs::path& out);
  void SetDropHover(bool hover);
  void SetPassword(std::wstring password) { command_line_password_ = std::move(password); }
  // Unattended: after the first job, its outcome is written to `result` and the launcher closes.
  void QuitWhenDone(fs::path result) { quit_result_ = std::move(result); }
  // Console mode (the dashboard, borderless, controller first; docs/DASHBOARD.md) or desktop mode
  // (the window with the row of games). Remembered (launcher_mode.txt) unless `remember` is false.
  void SetConsoleMode(bool console, bool remember = true);
  bool ConsoleMode() const { return console_; }
  bool Fullscreen() const { return fullscreen_; }
  // The mode saved, read at start.
  static void SavedMode(bool& console, bool& fullscreen);

  ImageCache& images() { return images_; }
  // Screenshots: library, empty, compatible, add, progress, error, confirm, password, menu, terms,
  // who, profiles (or profile), newprofile, achievements (made up), gameachievements (the selected
  // game's, from the files).
  void ShowState(std::wstring_view state);

  // JobObserver (the job's thread).
  void OnProgress(Step step, int percent) override;
  void OnGame(const CatalogGame& game) override;
  bool AskReplace(const CatalogGame& game) override;
  std::optional<std::wstring> AskPassword(const fs::path& archive, bool wrong) override;
  void OnDone(const Outcome& outcome) override;

 private:
  struct Target;
  struct Overlay;
  struct Animation {
    float from = 0, to = 0;
    double start = 0, duration = 0.15;
    float Value(double now) const;
    bool Running(double now) const { return now < start + duration; }
    void Go(float target, double now, double length = 0.15);
  };
  struct CompatibleItem {
    std::wstring title;
    const CatalogGame* game = nullptr;  // null: coming soon
    fs::path cover;
    bool installed = false;
    bool playable = false;
    bool needs_update = false;  // in the library without the title update it is played with
  };

  // Drawing (and laying out the targets).
  void DrawBackground(Canvas& c);
  void DrawHeader(Canvas& c);
  void DrawRow(Canvas& c);
  void DrawDetails(Canvas& c);
  void DrawDrawer(Canvas& c);
  void DrawHints(Canvas& c, float bottom);
  void DrawOverlay(Canvas& c, Overlay& overlay, bool top);
  void DrawTerms(Canvas& c, Overlay& overlay, bool top);
  void DrawMenu(Canvas& c, Overlay& overlay, bool top);
  void Button(Canvas& c, const RectF& r, std::wstring_view label, int id, std::function<void()> action,
              bool primary = false, bool enabled = true);
  void DrawFocus(Canvas& c, const RectF& r, float radius = 0);
  ID2D1Bitmap* Bitmap(Canvas& c, const std::shared_ptr<const Image>& image);
  // A logo embedded in the launcher (launcher.rc) decoded `height` DIPs tall at the window's DPI,
  // to the pixel: drawn 1:1, it stays sharp. Null if it can't be decoded.
  std::shared_ptr<const Image> Logo(int resource, float height);
  // Draws `logo` with its top left at (x, y) moved onto whole pixels; its width in DIPs.
  float DrawLogo(Canvas& c, const std::shared_ptr<const Image>& logo, float x, float y, float opacity = 1);
  void AddTarget(const RectF& r, int id, std::function<void()> action, bool focusable = true);

  // Input.
  const Target* TargetAt(float x, float y) const;
  const Target* Focused() const;
  void Move(Key direction);
  void Activate(int id);
  void Back();
  void FocusFirst();

  // Commands.
  int SelectedGame() const;  // index into games_, -1: the add tile
  void Select(int tile);
  void Play();
  void OpenAdd();
  void ChooseFileToAdd(bool folder);
  void StartAdding(const fs::path& source);
  void StartJob(std::function<Outcome(Job&)> work, const CatalogGame* game);
  void OpenMore(const RectF& anchor);
  void ChangeCover(const CatalogGame& game);
  void OpenLogs(const CatalogGame* game);
  void ShowMessage(std::wstring title, std::wstring text, bool error, const CatalogGame* logs_of = nullptr);
  void ShowTerms(bool require_acceptance);
  void ToggleCompatible();
  void ToggleLanguage();
  // Profiles (x360/profiles.h): "Who's playing?" (`required`: at start, nobody signed in yet),
  // their list (create, rename, delete, the games' language), a gamertag typed (`xuid` 0: a new
  // profile, signed in once made).
  void ReloadProfiles();
  const x360::profiles::Profile* Player() const;  // signed in on slot 1; null: nobody
  void ShowWhoIsPlaying(bool required);
  void OpenProfiles();
  void OpenProfileMenu(const RectF& anchor, uint64_t xuid);
  void OpenGamertag(uint64_t xuid);
  void SubmitGamertag();
  void TypeGamertag(wchar_t ch);
  void SignInAs(uint64_t xuid);
  void DrawWho(Canvas& c, Overlay& overlay, bool top);
  // Achievements (x360/achievements.h): a game's list, read once off the UI thread (null until it
  // is; then empty for a game without any), and a profile's unlocks and gamerscore, read again when
  // a game exits or the profiles change.
  const x360::achievements::TitleList* Achievements(const LibraryGame& game);
  const x360::achievements::TitleUnlocks& Unlocks(uint64_t xuid, uint32_t title_id);
  uint32_t Gamerscore(uint64_t xuid);
  void ForgetUnlocks();
  void OpenAchievements(const LibraryGame& game);
  void DrawAchievements(Canvas& c, Overlay& overlay, bool top);
  void DrawGamerpic(Canvas& c, float cx, float cy, float radius, const x360::profiles::Profile* profile, float opacity = 1);
  void PushOverlay(Overlay overlay);
  void PopOverlay();
  void Finish(const Outcome& outcome);
  // The running game (core/launch.h GameEvents): its first frame shown, about to end, ended.
  void GameReady();
  void GameLeaving(std::function<void()> answer);
  void GameExited(uint32_t code);

  // Console mode (the dashboard).
  void DrawConsole(Canvas& c);
  void DrawTabs(Canvas& c);
  void DrawConsoleHints(Canvas& c);
  void DrawHomePage(Canvas& c, const RectF& area);
  void DrawGamesPage(Canvas& c, const RectF& area);
  void DrawAchievementsPage(Canvas& c, const RectF& area);
  void DrawProfilesPage(Canvas& c, const RectF& area);
  void DrawSettingsPage(Canvas& c, const RectF& area);
  void DrawAddPage(Canvas& c, const RectF& area);
  void DrawStarting(Canvas& c);
  // A game's cover (or its icon and title) in `r`, as a target.
  void DrawCover(Canvas& c, const RectF& r, int game, int id, std::function<void()> action, bool show_title);
  void ShowPage(Page page);
  void PlayGame(int game);  // index into games_
  // The installed games the profile playing played last, newest first.
  std::vector<int> RecentlyPlayed();
  void ToggleFullscreen();

  Host& host_;
  ImageCache images_;
  std::vector<LibraryGame> games_;   // the catalog games this release has
  std::vector<int> tiles_;           // the row: indexes into games_ of installed games, then -1 (add)
  int selected_ = 0;                 // index into tiles_
  std::vector<CompatibleItem> compatible_;
  bool compatible_open_ = false;
  // Who is signed in (profile.txt) and every profile, read again after each change.
  x360::profile::SignIn sign_in_;
  std::vector<x360::profiles::Profile> profiles_;
  // Time played (titles\<TitleID>.json) by profile and title, read once: forgotten when the
  // library or the profiles are read again and when a game exits.
  std::map<std::pair<uint64_t, uint32_t>, std::optional<x360::profiles::TitleStats>> title_stats_;
  const std::optional<x360::profiles::TitleStats>& TitleStats(uint64_t xuid, uint32_t title_id);
  std::map<uint32_t, std::optional<x360::achievements::TitleList>> achievement_lists_;  // by title; none: reading
  std::map<std::pair<uint64_t, uint32_t>, x360::achievements::TitleUnlocks> unlocks_;
  std::map<uint64_t, uint32_t> gamerscores_;

  // The layout of the last Paint.
  std::vector<Target> targets_;
  float width_ = 0, height_ = 0, dpi_ = 96;
  RectF more_button_{};
  int focus_ = -1, hover_ = -1, pressed_ = -1;
  bool focus_visible_ = false;  // keyboard and controller show where the focus is; the mouse doesn't
  float mouse_x_ = -1, mouse_y_ = -1;
  bool pad_used_ = false;

  // Movement.
  Animation row_scroll_, drawer_, drawer_scroll_, selection_, background_fade_;
  float row_scroll_target_ = 0, drawer_scroll_target_ = 0;
  std::shared_ptr<const Image> background_, previous_background_;
  bool drop_hover_ = false;

  std::vector<Overlay> overlays_;

  // Jobs and the running game.
  Worker worker_;
  bool working_ = false;
  Step step_ = Step::kChecking;
  int percent_ = -1;
  const CatalogGame* job_game_ = nullptr;
  bool saving_ = false;  // the job writes an .anyx backup
  bool close_after_job_ = false;
  const CatalogGame* running_game_ = nullptr;
  bool game_starting_ = false;  // started, its window not shown yet
  double leaving_at_ = 0;       // when the game said it was leaving (0: it hasn't)
  // Console mode.
  bool console_ = false, fullscreen_ = false;
  Page page_ = Page::kHome;
  Animation page_fade_;
  Message message_;
  std::vector<Message> notes_;
  std::wstring command_line_password_;
  bool command_line_password_used_ = false;
  std::optional<fs::path> quit_result_;

  // Decoded pictures as Direct2D bitmaps, for the canvas generation they were made for.
  std::map<std::shared_ptr<const Image>, ComPtr<ID2D1Bitmap>> bitmaps_;
  uint64_t bitmaps_generation_ = 0;
  std::map<std::shared_ptr<const Image>, std::shared_ptr<const Image>> blurred_;
  // The logos, by resource and height in pixels.
  std::map<std::pair<int, uint32_t>, std::shared_ptr<const Image>> logos_;
};

}  // namespace anyx360::launcher::ui
