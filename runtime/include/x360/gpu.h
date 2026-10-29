// GPU: Xenia's Direct3D 12 Xenos emulation (thirdparty/xenia), driven by AnyX360's kernel.
// The title's statically linked Direct3D writes PM4 packets to the ring buffer and pokes the GPU
// registers at 0x7FC80000 (MMIO, handled by xe::Memory); Xenia's command processor renders them
// and presents into the game window.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace x360::gpu {

struct DisplayConfig {
  enum class Upscale { kBilinear, kCas, kFsr };

  uint32_t width = 1920;   // window client size
  uint32_t height = 1080;
  // Xenia's draw resolution scale: the title renders at 1280x720 x scale (integer only). 2x costs the
  // GPU about 4x as much as 1x.
  uint32_t render_scale = 1;
  Upscale upscale = Upscale::kFsr;  // scaling of the rendered image to the window (AMD FidelityFX)
  bool fxaa = false;                // FXAA on the final image
  bool vsync = true;                // present on the display's vertical blank (no tearing in fullscreen)
  bool fullscreen = false;
  // Target frame rate. Titles present every N vblanks (SetVblanksPerFrame), so the vblank interrupt
  // runs at target_fps x N.
  uint32_t target_fps = 60;
};

// How many vblanks the title waits between presents at its normal frame rate (Gears of War 3: 2,
// i.e. 30 FPS at 60 Hz). A property of the title, not a user setting. Call before Initialize.
void SetVblanksPerFrame(uint32_t vblanks);
// A title that runs at one frame rate only (a rhythm game's timing): the settings panel shows it
// without a choice. 0 (the default): 30 or 60 FPS.
void SetFixedFps(uint32_t fps);
uint32_t FixedFps();

// Creates the game window, the D3D12 graphics system and the vblank/interrupt thread. Must run on
// the UI (main) thread after the guest memory exists. Returns false if D3D12 is unavailable.
bool Initialize(const char* window_title, const DisplayConfig& config);
const DisplayConfig& GetDisplayConfig();  // UI thread
// Changes the target frame rate (the vblank rate) while running. UI thread.
void SetTargetFps(uint32_t fps);
void SetWindowTitle(const std::string& title);  // UI thread

// Loads (and keeps updating) the title's persistent shader and pipeline cache under cache_root,
// compiling cached pipelines in the background as Xenia does when it launches a title.
void InitializeShaderStorage(const std::filesystem::path& cache_root, uint32_t title_id);

// Runs the UI message loop until the window closes or the title exits; returns the exit code.
int RunMessageLoop();
// Asks the UI loop to exit (any thread).
void RequestQuit(int exit_code);
// Saves the settings and replaces this process with `exe` started with `arguments` and
// --restarted-from this process (it waits for this one to exit before using the settings, cache and
// saves): the settings panel's restart, and a title starting another of its executables. The new
// process gets this one's standard handles, so a log the launcher redirected keeps going; test hooks
// that drive this session don't carry over. Exits on success; returns the error otherwise. Any thread.
std::string ReplaceProcess(const std::filesystem::path& exe, const std::vector<std::wstring>& arguments);

// The sign-in picker over the game (signin_picker.cpp), for a title's sign-in UI: the profiles not
// signed in, "Create profile" with an on-screen keyboard, and Cancel, driven by every controller
// and the keyboard. A profile chosen signs in on the slot whose controller chose it
// (kernel::SignInProfile); after `count` players, or Cancel, it closes (kernel::CloseSigninUi).
// False when there is no game window to show it in. Any thread.
bool OpenSigninPicker(uint32_t count);

// The dashboard (x360/dashboard.h): the launcher started the game as part of its window. Set from
// the command line before Initialize.
struct LaunchWindow {
  uint32_t dashboard_pid = 0;  // --dashboard: the launcher's events; the window is hidden until its first frame
  bool has_rect = false;       // --window: borderless at this rectangle (screen pixels)
  int x = 0, y = 0, w = 0, h = 0;
  bool fullscreen = false;     // --fullscreen came with it (carried to the next process)
};
void SetLaunchWindow(const LaunchWindow& window);
const LaunchWindow& GetLaunchWindow();
// The game is about to end on its own (its window closed, Xbox Home, another of its executables
// starting): the dashboard is shown first (it waits up to a second and a half for it). Any thread.
void PrepareToLeave();
// The Guide's Xbox Home: saves the settings and the time played, then ends the process with
// dashboard::kExitToDashboard. Not the UI thread.
[[noreturn]] void ExitToDashboard();

// A system notification's toast (achievement_toast.cpp, the same queue): a controller connected,
// a profile signed in. `glyph` is drawn in the circle (a player's number, a gamertag's initial). False
// when they are off (settings.toml system_notifications) or there is no game window. Any thread.
struct Notification {
  std::string title, text;
  char glyph = 0;
};
bool ShowNotification(const Notification& notification);

// An achievement unlocked, for its toast (achievement_toast.cpp): bottom centre, a few seconds, with
// a chime, one after another. `gamertag` is shown when given (several players signed in). False when
// toasts are off (settings.toml achievement_toasts) or there is no game window. Any thread.
struct AchievementToast {
  std::string name;          // UTF-8, the game's language
  uint32_t gamerscore = 0;
  std::string gamertag;
  std::vector<uint8_t> png;  // its picture; empty: none
};
bool ShowAchievementToast(const AchievementToast& toast);

// The system's list of a player's achievements in this game over it (achievements_ui.cpp), for a
// title's XamShowAchievementsUI: unlocked first, scrolled with the d-pad, closed with B (then
// kernel::CloseAchievementsUi). False when there is no game window. Any thread.
struct AchievementsView {
  std::string title;     // the game's name
  std::string gamertag;  // whose
  struct Item {
    std::string name, description;  // the game's language; the description as unlocked or how to
    uint32_t gamerscore = 0;
    bool unlocked = false, secret = false;  // secret: locked and hidden (its name isn't shown)
    std::string date;                       // ISO 8601 UTC; empty: locked (or this session)
    std::vector<uint8_t> png;
  };
  std::vector<Item> items;
};
bool OpenAchievementsUi(AchievementsView view);

// Records a guest frame (VdSwap) for the performance overlay.
void NotifySwap(uint32_t frontbuffer_width, uint32_t frontbuffer_height);

// Vd* exports.
void InitializeRingBuffer(uint32_t physical, uint32_t size_log2);
void EnableReadPointerWriteBack(uint32_t physical, uint32_t block_size_log2);
void SetInterruptCallback(uint32_t callback, uint32_t user_data);

}  // namespace x360::gpu
