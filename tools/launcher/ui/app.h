// The launcher's window contents, drawn with Direct2D (Metro dark, like the Xbox 360's last
// dashboard): the player's games as box art in a row, the selected one's details and Play, a
// collapsible "compatible games" drawer (this release's games and the coming ones), and overlays
// instead of dialogs (add a game, progress, messages, confirmations, a password, the ⋯ menu, the
// terms). Mouse, keyboard and an Xbox controller move one focus.
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
  virtual void ShowWindow(bool show) = 0;
  virtual void Close() = 0;
};

enum class Key { kLeft, kRight, kUp, kDown, kActivate, kBack, kMore, kCompatible, kNext, kPrevious, kHome, kEnd, kPlay };

class App : public JobObserver {
 public:
  explicit App(Host& host);
  ~App() override;

  // Reads the library; the selection stays on the same game (or `select`).
  void Load(std::wstring select = {});
  // Shows the terms if they haven't been accepted (the launcher closes if they are declined).
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
  void Character(wchar_t c);
  void Paste(const std::wstring& text);
  bool WantsText() const;
  // The window is asked to close: false keeps it open (a job is running; the player is asked).
  bool OnClose();

  // A file or folder dropped on the window or named on the command line.
  void AddSource(const fs::path& source);
  void SetDropHover(bool hover);
  void SetPassword(std::wstring password) { command_line_password_ = std::move(password); }
  // Unattended: after the first job, its outcome is written to `result` and the launcher closes.
  void QuitWhenDone(fs::path result) { quit_result_ = std::move(result); }

  ImageCache& images() { return images_; }
  // Screenshots: library, empty, compatible, add, progress, error, confirm, password, menu, terms.
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
  void PushOverlay(Overlay overlay);
  void PopOverlay();
  void Finish(const Outcome& outcome);
  void GameExited(uint32_t code);

  Host& host_;
  ImageCache images_;
  std::vector<LibraryGame> games_;   // the catalog games this release has
  std::vector<int> tiles_;           // the row: indexes into games_ of installed games, then -1 (add)
  int selected_ = 0;                 // index into tiles_
  std::vector<CompatibleItem> compatible_;
  bool compatible_open_ = false;

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
  bool close_after_job_ = false;
  const CatalogGame* running_game_ = nullptr;
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
