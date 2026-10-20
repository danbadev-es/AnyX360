// Glue between AnyX360's kernel and Xenia's GPU (D3D12) and UI.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <deque>

#include "x360/gpu.h"
#include "frame_timing.h"
#include "gpu_internal.h"
#include "host_vblank.h"
#include "stats_overlay.h"
#include "ui_theme.h"
#include "x360/input.h"
#include "x360/kernel.h"
#include "x360/settings.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#include "xenia/gpu/graphics_host.h"
#include "xenia/memory.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_listener.h"
#include "xenia/ui/window_win.h"
#include "xenia/ui/windowed_app_context_win.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

DECLARE_int32(log_level);
DECLARE_int32(draw_resolution_scale_x);
DECLARE_int32(draw_resolution_scale_y);

namespace x360::gpu {

using namespace x360::kernel;

namespace {

// Saves the latest guest frame as a 24-bit BMP (debugging aid, ANYX360_CAPTURE_DIR).
void CaptureFrame(xe::ui::Presenter* presenter, const std::string& dir, int index) {
  xe::ui::RawImage image;
  if (!presenter || !presenter->CaptureGuestOutput(image)) return;
  uint32_t row = (image.width * 3 + 3) & ~3u;
  uint32_t data_size = row * image.height;
  uint8_t header[54] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) { std::memcpy(header + at, &v, 4); };
  put32(2, 54 + data_size);
  put32(10, 54);
  put32(14, 40);
  put32(18, image.width);
  put32(22, image.height);
  header[26] = 1;
  header[28] = 24;
  put32(34, data_size);
  char name[64];
  std::snprintf(name, sizeof(name), "/frame_%04d.bmp", index);
  FILE* f = std::fopen((dir + name).c_str(), "wb");
  if (!f) return;
  std::fwrite(header, 1, sizeof(header), f);
  std::vector<uint8_t> line(row, 0);
  for (uint32_t y = image.height; y-- > 0;) {
    const uint8_t* src = image.data.data() + y * image.stride;
    for (uint32_t x = 0; x < image.width; ++x) {
      line[x * 3 + 0] = src[x * 4 + 2];
      line[x * 3 + 1] = src[x * 4 + 1];
      line[x * 3 + 2] = src[x * 4 + 0];
    }
    std::fwrite(line.data(), 1, row, f);
  }
  std::fclose(f);
}

// Debugging aid: logs every place in guest memory holding `text` as ASCII or big-endian UTF-16
// (UE3 FStrings), with the surrounding characters.
void ScanGuestMemory(const char* text) {
  std::string ascii = text;
  std::string utf16;
  for (char c : ascii) utf16 += '\0', utf16 += c;
  if (ascii.rfind("0x", 0) == 0) {  // "0x12345678": a big-endian 32-bit value
    uint32_t value = uint32_t(std::strtoul(text + 2, nullptr, 16));
    ascii.assign({char(value >> 24), char(value >> 16), char(value >> 8), char(value)});
    utf16 = ascii;
  }
  uint8_t* base = XeMemory()->virtual_membase();
  uint8_t* at = base + 0x40000000ull;
  uint8_t* end = base + 0x100000000ull;
  int hits = 0;
  while (at < end && hits < 64) {
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(at, &info, sizeof(info))) break;
    uint8_t* region_end = static_cast<uint8_t*>(info.BaseAddress) + info.RegionSize;
    bool readable = info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                    (info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                                     PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY));
    if (readable) {
      std::string_view region(reinterpret_cast<const char*>(at), size_t(region_end - at));
      for (const std::string& needle : {ascii, utf16}) {
        bool wide = &needle == &utf16;
        for (size_t pos = region.find(needle); pos != std::string_view::npos && hits < 64;
             pos = region.find(needle, pos + 1)) {
          std::string context;
          size_t from = pos >= 64 ? pos - 64 : 0;
          for (size_t i = from; i < std::min(region.size(), pos + 160); i += wide ? 2 : 1) {
            char c = region[wide ? i + 1 : i];
            if (wide && region[i] != 0) c = '?';
            context += (c >= 32 && c < 127) ? c : '.';
          }
          KLOG_I("memscan %s %08X: %s", wide ? "w" : "a", uint32_t(at + pos - base), context.c_str());
          if (std::getenv("ANYX360_MEMSCAN_HEX") && pos >= 32) {
            std::string hex;
            char byte[4];
            for (size_t i = pos - 32; i < pos; ++i) {
              std::snprintf(byte, sizeof(byte), "%02X ", uint8_t(region[i]));
              hex += byte;
            }
            KLOG_I("memscan   before: %s", hex.c_str());
          }
          ++hits;
        }
      }
    }
    at = region_end;
  }
}

struct InterruptRequest {
  uint32_t callback;
  uint32_t data;
  uint32_t source;
};

std::unique_ptr<xe::ui::Win32WindowedAppContext> g_app;
std::unique_ptr<xe::ui::Window> g_window;
std::unique_ptr<xe::gpu::d3d12::D3D12GraphicsSystem> g_graphics;
std::shared_ptr<Thread> g_interrupt_thread;
std::mutex g_interrupt_mutex;
HANDLE g_interrupt_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset: queued work
std::deque<InterruptRequest> g_interrupts;
std::atomic<uint32_t> g_vblank_hz{120};
std::atomic<uint32_t> g_vblanks_per_frame{2};
std::atomic<uint64_t> g_vblanks_skipped{0};
std::atomic<bool> g_display_lock{true};
std::atomic<bool> g_display_locked{false};
// Where new frames are presented within the display's refresh, as an error from the middle in
// refreshes ([-0.5, 0.5)), smoothed; and when it was last updated (QPC).
std::atomic<double> g_present_phase_error{0};
std::atomic<int64_t> g_present_phase_time{0};
std::atomic<int> g_exit_code{0};
std::atomic<uint32_t> g_writeback{0};
DisplayConfig g_display;
// Window size to apply when leaving fullscreen (the panel's resolution while fullscreen).
uint32_t g_pending_width = 0, g_pending_height = 0;

}  // namespace

std::atomic<uint64_t> g_swap_count{0};
std::atomic<uint64_t> g_interrupt_count{0};
std::atomic<uint64_t> g_gpu_swaps{0};       // swaps executed by the command processor
std::atomic<uint64_t> g_presented_swap{0};  // g_gpu_swaps at the last present

namespace {

// Runs the title's interrupt callback the way the kernel's DPC does: on a guest thread at
// DISPATCH_LEVEL with the PRCB's DPC-active flag set.
void RunInterrupt(const InterruptRequest& r) {
  ++g_interrupt_count;
  Thread* t = CurrentThread();
  Store<uint8_t>(t->pcr + 0x18, 2);
  Store<uint32_t>(t->pcr + 0x150, 1);
  CallGuest(r.callback, {r.source, r.data});
  Store<uint32_t>(t->pcr + 0x150, 0);
  Store<uint8_t>(t->pcr + 0x18, 0);
}

class Host final : public xe::gpu::GraphicsHost {
 public:
  void DispatchInterrupt(uint32_t callback, uint32_t user_data, uint32_t source, uint32_t cpu) override {
    (void)cpu;
    InterruptRequest r{callback, user_data, source};
    if (g_interrupt_thread && CurrentThread() == g_interrupt_thread.get()) {
      RunInterrupt(r);
      return;
    }
    {
      std::lock_guard lock(g_interrupt_mutex);
      g_interrupts.push_back(r);
    }
    ::SetEvent(g_interrupt_event);
  }
  uint32_t GetTitleId() override { return Title().title_id; }
  void OnSwap() override {
    ++g_gpu_swaps;
    timing::Record(timing::Event::kGpuSwap);
  }
};

Host g_host;

int64_t QpcNow() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

// New frames presented by the GPU thread: how far from the middle of the display's refresh they
// land (the display lock steers the vblank so they land there, as far as possible from the
// refresh where a late or early frame would be shown one refresh off).
void TrackPresentPhase() {
  int64_t vblank;
  double period;
  if (!host_vblank::Estimate(vblank, period)) return;
  int64_t now = QpcNow();
  double phase = double(now - vblank) / period;
  phase -= std::floor(phase);
  double error = phase - 0.5;
  double smoothed = g_present_phase_error.load(std::memory_order_relaxed);
  // Restart the average after a jump (near the wrap-around, values on both sides would cancel).
  smoothed = std::abs(error - smoothed) > 0.25 ? error : smoothed + (error - smoothed) * 0.1;
  g_present_phase_error.store(smoothed, std::memory_order_relaxed);
  g_present_phase_time.store(now, std::memory_order_relaxed);
}

// The vblank interrupt (g_vblank_hz) plus queued command-buffer interrupts, delivered on a guest
// thread. Each vblank is due one period after the previous one (no drift, no truncated period): a
// high-resolution waitable timer sleeps until shortly before it and the rest is spun
// (ANYX360_VBLANK_SPIN_US, default 300). A late tick skips whole periods but keeps the phase.
//
// Display lock: when the display's refresh rate is the vblank rate times or divided by 1-4, the
// period follows the measured refresh period exactly and the phase is steered (at most 20 us per
// tick) so new frames are presented mid-refresh. Free-running, a guest vblank a few ppm off the
// display drifts through the refresh, and while presents straddle a refresh boundary frames are
// shown for one refresh too many or too few (periodic judder).
void InterruptThreadBody() {
  SetLatencySensitiveThread(THREAD_PRIORITY_HIGHEST);
  HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!timer) timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);  // before Windows 10 1803
  LARGE_INTEGER frequency_value;
  QueryPerformanceFrequency(&frequency_value);
  const int64_t frequency = frequency_value.QuadPart;
  static const char* spin_env = std::getenv("ANYX360_VBLANK_SPIN_US");
  const int64_t spin_us = spin_env ? std::atoi(spin_env) : settings::Get().pacing.vblank_spin_us;
  const int64_t spin = std::clamp<int64_t>(spin_us, 0, 2000) * frequency / 1000000;
  const double max_slew = 20e-6 * double(frequency);
  uint32_t hz = 0;
  double next = 0;  // QPC time of the next vblank
  bool locked = false;
  for (;;) {
    if (uint32_t wanted = std::max(1u, g_vblank_hz.load(std::memory_order_relaxed)); wanted != hz) {
      hz = wanted;
      next = double(QpcNow()) + double(frequency) / hz;
    }
    std::deque<InterruptRequest> requests;
    {
      std::lock_guard lock(g_interrupt_mutex);
      requests.swap(g_interrupts);
    }
    if (!requests.empty()) {
      for (const InterruptRequest& r : requests) RunInterrupt(r);
      continue;
    }
    int64_t target = int64_t(next);
    int64_t remaining = target - QpcNow();
    if (remaining > spin) {
      LARGE_INTEGER relative;
      relative.QuadPart = -std::max<int64_t>(1, (remaining - spin) * 10000000 / frequency);  // 100 ns units
      SetWaitableTimer(timer, &relative, 0, nullptr, nullptr, FALSE);
      HANDLE handles[] = {g_interrupt_event, timer};
      WaitForMultipleObjects(2, handles, FALSE, INFINITE);
      continue;
    }
    int64_t now;
    while ((now = QpcNow()) < target) YieldProcessor();
    timing::Record(timing::Event::kVblank, int32_t((now - target) * 1000000 / frequency));

    double period = double(frequency) / hz;
    int64_t display_vblank;
    double display_period;
    bool lock = g_display_lock.load(std::memory_order_relaxed) && host_vblank::Estimate(display_vblank, display_period);
    if (lock) {
      double ticks_per_refresh = hz * display_period / double(frequency);
      double k = std::round(ticks_per_refresh >= 1 ? ticks_per_refresh : 1 / ticks_per_refresh);
      double off = std::abs((ticks_per_refresh >= 1 ? ticks_per_refresh : 1 / ticks_per_refresh) - k);
      lock = k >= 1 && k <= 4 && off < 0.01 * k;
      if (lock) period = ticks_per_refresh >= 1 ? display_period / k : display_period * k;
    }
    if (lock != locked) {
      locked = lock;
      g_display_locked = locked;
      KLOG_I("vblank: %s", locked ? "locked to the display's refresh" : "free-running");
    }
    if (locked && now - g_present_phase_time.load(std::memory_order_relaxed) < frequency / 2) {
      double error = g_present_phase_error.load(std::memory_order_relaxed);
      period -= std::clamp(error * display_period * 0.005, -max_slew, max_slew);
    }
    next += period;
    for (; next <= double(now); next += period) ++g_vblanks_skipped;
    g_graphics->MarkVblank();  // calls DispatchInterrupt(source 0) back on this thread
  }
}

class WindowListener final : public xe::ui::WindowListener {
 public:
  // Another monitor's scale: the panels' fonts and sizes follow.
  void OnDpiChanged(xe::ui::UISetupEvent& e) override {
    (void)e;
    theme::OnDpiChanged();
  }
  void OnClosing(xe::ui::UIEvent& e) override {
    (void)e;
    KLOG_I("window closed: exiting");
    settings::SaveNow();
    std::fflush(nullptr);
    std::_Exit(0);
  }
};
WindowListener g_window_listener;

// F1 toggles the performance overlay, F2 the settings panel (Esc closes it), F11 fullscreen
// (Alt+Enter: see the input module's window hook).
class WindowKeys final : public xe::ui::WindowInputListener {
 public:
  void OnKeyDown(xe::ui::KeyEvent& e) override {
    if (e.prev_state()) return;  // auto-repeat
    switch (e.virtual_key()) {
      case xe::ui::VirtualKey::kF1: {
        bool show = !IsStatsOverlayVisible();
        SetStatsOverlayVisible(show);
        settings::Update([show](settings::Settings& s) { s.show_stats = show; });
        break;
      }
      case xe::ui::VirtualKey::kF2: ToggleSettingsPanel(); break;
      case xe::ui::VirtualKey::kEscape:
        if (!IsSettingsPanelOpen()) return;
        CloseSettingsPanel();
        break;
      case xe::ui::VirtualKey::kF11: SetFullscreenMode(!g_window->IsFullscreen()); break;
      default: return;
    }
    e.set_handled(true);
  }
};
WindowKeys g_window_keys;

xe::ui::Presenter::GuestOutputPaintConfig MakePaintConfig(const DisplayConfig& config) {
  using Effect = xe::ui::Presenter::GuestOutputPaintConfig::Effect;
  xe::ui::Presenter::GuestOutputPaintConfig paint;
  paint.SetAllowOverscanCutoff(true);
  paint.SetEffect(config.upscale == DisplayConfig::Upscale::kFsr   ? Effect::kFsr
                  : config.upscale == DisplayConfig::Upscale::kCas ? Effect::kCas
                                                                   : Effect::kBilinear);
  return paint;
}

HWND GameHwnd() { return g_window ? static_cast<xe::ui::Win32Window*>(g_window.get())->hwnd() : nullptr; }

// Quotes a command-line argument the way CommandLineToArgvW parses it.
std::wstring QuoteArgument(const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
  std::wstring out = L"\"";
  for (size_t i = 0;; ++i) {
    size_t backslashes = 0;
    while (i < arg.size() && arg[i] == L'\\') ++backslashes, ++i;
    if (i == arg.size()) {
      out.append(backslashes * 2, L'\\');
      break;
    }
    out.append(arg[i] == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
    out += arg[i];
  }
  return out + L"\"";
}

}  // namespace

const DisplayConfig& GetDisplayConfig() { return g_display; }

void PostToUIThread(std::function<void()> fn) { g_app->CallInUIThread(std::move(fn)); }

xe::ui::Window* GameWindow() { return g_window.get(); }

void ApplyWindowSize(uint32_t width, uint32_t height) {
  HWND hwnd = GameHwnd();
  if (!hwnd || !width || !height) return;
  g_display.width = width;
  g_display.height = height;
  if (g_window->IsFullscreen()) {
    g_pending_width = width, g_pending_height = height;
    return;
  }
  g_pending_width = g_pending_height = 0;
  if (IsZoomed(hwnd)) ShowWindow(hwnd, SW_RESTORE);
  // The size is in physical pixels: the frame comes from the window's DPI.
  UINT dpi = GetDpiForWindow(hwnd);
  DWORD style = DWORD(GetWindowLongPtrW(hwnd, GWL_STYLE)), ex_style = DWORD(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
  RECT frame{0, 0, 0, 0};
  AdjustWindowRectExForDpi(&frame, style, FALSE, ex_style, dpi);
  int frame_w = frame.right - frame.left, frame_h = frame.bottom - frame.top;
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
  const RECT& work = monitor.rcWork;
  int max_w = work.right - work.left - frame_w, max_h = work.bottom - work.top - frame_h;
  double fit = std::min({1.0, double(max_w) / width, double(max_h) / height});  // keep the aspect ratio
  int w = int(width * fit) + frame_w, h = int(height * fit) + frame_h;
  RECT current;
  GetWindowRect(hwnd, &current);
  int x = std::clamp<int>(current.left, work.left, std::max<int>(work.left, work.right - w));
  int y = std::clamp<int>(current.top, work.top, std::max<int>(work.top, work.bottom - h));
  SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  RECT client;
  GetClientRect(hwnd, &client);
  KLOG_I("display: window %ux%u requested, client %ldx%ld (DPI %u)%s", width, height, client.right, client.bottom,
         dpi, fit < 1.0 ? ", reduced to fit the screen" : "");
}

void SetFullscreenMode(bool fullscreen) {
  if (!g_window) return;
  if (g_window->IsFullscreen() != fullscreen) g_window->SetFullscreen(fullscreen);
  g_display.fullscreen = fullscreen;
  if (!fullscreen && g_pending_width) ApplyWindowSize(g_pending_width, g_pending_height);
  settings::Update([fullscreen](settings::Settings& s) { s.display.fullscreen = fullscreen; });
  KLOG_I("display: %s", fullscreen ? "fullscreen" : "windowed");
}

void SetUpscale(DisplayConfig::Upscale upscale) {
  g_display.upscale = upscale;
  g_graphics->presenter()->SetGuestOutputPaintConfigFromUIThread(MakePaintConfig(g_display));
  KLOG_I("display: upscaling %s", upscale == DisplayConfig::Upscale::kFsr   ? "FSR"
                                  : upscale == DisplayConfig::Upscale::kCas ? "CAS"
                                                                            : "bilinear");
}

void SetVsync(bool vsync) {
  g_display.vsync = vsync;
  g_graphics->presenter()->SetPresentSyncInterval(vsync ? 1 : 0);
  KLOG_I("display: vsync %s", vsync ? "on" : "off");
}

void SetDisplayLock(bool lock) {
  g_display_lock = lock;
  KLOG_I("display: vblank lock %s", lock ? "on" : "off");
}

void DisplayLockState(double& display_hz, bool& locked) {
  int64_t vblank;
  double period;
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  display_hz = host_vblank::Estimate(vblank, period) ? double(frequency.QuadPart) / period : 0;
  locked = g_display_locked.load();
}

void SetFxaa(bool fxaa) {
  g_display.fxaa = fxaa;
  g_graphics->command_processor()->SetDesiredSwapPostEffect(fxaa ? xe::gpu::CommandProcessor::SwapPostEffect::kFxaa
                                                                 : xe::gpu::CommandProcessor::SwapPostEffect::kNone);
  KLOG_I("display: FXAA %s", fxaa ? "on" : "off");
}

std::string Restart() {
  settings::SaveNow();
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  // Same arguments without the display flags (the saved settings apply) and the previous restart.
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::wstring command = QuoteArgument(exe);
  for (int i = 1; i < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--resolution" || arg == L"--scale" || arg == L"--fps" || arg == L"--upscale" ||
        arg == L"--restarted-from") {
      ++i;
      continue;
    }
    if (arg == L"--fxaa" || arg == L"--fullscreen") continue;
    command += L" " + QuoteArgument(arg);
  }
  LocalFree(argv);
  command += L" --restarted-from " + std::to_wstring(GetCurrentProcessId());
  STARTUPINFOW startup{sizeof(startup)};
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (output && output != INVALID_HANDLE_VALUE) {
    startup.dwFlags = STARTF_USESTDHANDLES;  // keep redirected logs going
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  }
  // Test hooks that drive this session don't carry over.
  SetEnvironmentVariableW(L"ANYX360_SETTINGS_TEST", nullptr);
  SetEnvironmentVariableW(L"ANYX360_FPS_SWITCH", nullptr);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
    DWORD error = GetLastError();
    KLOG_E("restart failed (%lu)", error);
    return "could not start the game again (error " + std::to_string(error) + ")";
  }
  KLOG_I("restarting: new process %lu", process.dwProcessId);
  std::fflush(nullptr);
  std::_Exit(0);
}

void SetWindowTitle(const std::string& title) {
  if (g_window) g_window->SetTitle(title);
}

void SetTargetFps(uint32_t fps) {
  g_display.target_fps = fps;
  g_vblank_hz = std::max(1u, fps) * g_vblanks_per_frame;
  KLOG_I("display: %u FPS (vblank %u Hz)", fps, g_vblank_hz.load());
}

void SetVblanksPerFrame(uint32_t vblanks) { g_vblanks_per_frame = std::max(1u, vblanks); }

namespace {
uint32_t g_fixed_fps = 0;
}
void SetFixedFps(uint32_t fps) { g_fixed_fps = fps; }
uint32_t FixedFps() { return g_fixed_fps; }

bool Initialize(const char* window_title, const DisplayConfig& config) {
  g_display = config;
  g_vblank_hz = std::max(1u, config.target_fps) * g_vblanks_per_frame;
  KLOG_I("display: %u FPS (vblank %u Hz)", config.target_fps, g_vblank_hz.load());
  cvars::draw_resolution_scale_x = int32_t(std::max(1u, config.render_scale));
  cvars::draw_resolution_scale_y = int32_t(std::max(1u, config.render_scale));
  // ANYX360_XENIA_LOG_LEVEL: 0 error, 1 warning, 2 info (default), 3 debug (GPU details).
  if (const char* level = std::getenv("ANYX360_XENIA_LOG_LEVEL")) cvars::log_level = std::atoi(level);
  KLOG_I("GPU init: logging");
  xe::InitializeLogging("AnyX360");
  KLOG_I("GPU init: UI context");

  // The UI thread presents with the overlay or the panel open, and handles input and resizes.
  if (settings::Get().pacing.thread_priorities) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  g_app = std::make_unique<xe::ui::Win32WindowedAppContext>(GetModuleHandleW(nullptr), SW_SHOWNORMAL);
  if (!g_app->Initialize()) {
    KLOG_E("could not initialize the UI");
    return false;
  }
  KLOG_I("GPU init: window");
  // Window::Create scales its size by the system DPI (the game is per-monitor DPI aware): the size
  // asked for is physical pixels, so it is given unscaled; ApplyWindowSize below makes it exact.
  const UINT system_dpi = std::max<UINT>(GetDpiForSystem(), 1);
  g_window = xe::ui::Window::Create(*g_app, window_title, MulDiv(int(config.width), USER_DEFAULT_SCREEN_DPI, int(system_dpi)),
                                    MulDiv(int(config.height), USER_DEFAULT_SCREEN_DPI, int(system_dpi)));
  if (!g_window) {
    KLOG_E("could not create the game window");
    return false;
  }
  g_window->AddListener(&g_window_listener);
  g_window->AddInputListener(&g_window_keys, 0);
  if (config.fullscreen) g_window->SetFullscreen(true);
  if (!g_window->Open()) {
    KLOG_E("could not open the game window");
    return false;
  }
  // Window::Create takes a size in DPI-scaled units: set the client area in physical pixels.
  if (config.fullscreen)
    g_pending_width = config.width, g_pending_height = config.height;
  else
    ApplyWindowSize(config.width, config.height);
  input::Start(GameHwnd(), [] { SetFullscreenMode(!g_window->IsFullscreen()); });
  host_vblank::Start(GameHwnd());

  if (!xe::gpu::d3d12::D3D12GraphicsSystem::IsAvailable()) {
    KLOG_E("Direct3D 12 is not available on this system");
    return false;
  }
  // Occlusion query mode (fake, fast, fast-alt, strict); see Xenia's occlusion_query cvar.
  if (const char* zpd = std::getenv("ANYX360_ZPD")) xe::gpu::SetZPDMode(zpd);
  KLOG_I("GPU init: D3D12 graphics system");
  g_graphics = std::make_unique<xe::gpu::d3d12::D3D12GraphicsSystem>();
  if (g_graphics->Setup(XeMemory(), &g_host, g_app.get(), true) != 0) {
    KLOG_E("could not initialize the Direct3D 12 GPU back end");
    return false;
  }
  KLOG_I("GPU init: presenter");
  g_window->SetPresenter(g_graphics->presenter());
  g_graphics->presenter()->SetGuestOutputPaintConfigFromUIThread(MakePaintConfig(config));
  g_graphics->presenter()->SetPresentSyncInterval(config.vsync ? 1 : 0);
  g_display_lock = settings::Get().pacing.display_lock && !std::getenv("ANYX360_NO_DISPLAY_LOCK");
  if (config.fxaa)
    g_graphics->command_processor()->SetDesiredSwapPostEffect(xe::gpu::CommandProcessor::SwapPostEffect::kFxaa);
  g_graphics->presenter()->SetPresentObserver([](bool ui_thread, bool presented) {
    if (!presented) return;
    uint64_t swaps = g_gpu_swaps.load();
    bool new_frame = g_presented_swap.exchange(swaps) != swaps;
    timing::Record(ui_thread ? timing::Event::kPresentUiThread : timing::Event::kPresentGpuThread, new_frame);
    if (new_frame && !ui_thread) TrackPresentPhase();  // UI-thread presents wait for the display anyway
  });
  // ANYX360_NO_OVERLAY=1: no ImGui at all, so the guest output thread always presents (A/B tests).
  if (!std::getenv("ANYX360_NO_OVERLAY")) {
    StartStatsOverlay(g_window.get(), g_graphics.get(), XeMemory());
    if (std::getenv("ANYX360_OVERLAY") || settings::Get().show_stats) SetStatsOverlayVisible(true);
    StartSettingsPanel();
  }

  g_interrupt_thread = CreateHostDrivenThread("GPU interrupts", 2, InterruptThreadBody);
  std::thread([] {
    SetThreadDescription(GetCurrentThread(), L"GPU stats");
    uint64_t last_swaps = 0;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      uint64_t swaps = g_swap_count.load();
      uint32_t wptr = g_graphics->register_file()->values[0x01C5];
      uint32_t wb = g_writeback.load();
      uint32_t rptr = wb ? load_be<uint32_t>(XeMemory()->TranslatePhysical(wb)) : 0;
      KLOG_I("GPU: %llu swaps (+%llu), %llu interrupts, wptr %u, rptr %u", (unsigned long long)swaps,
             (unsigned long long)(swaps - last_swaps), (unsigned long long)g_interrupt_count.load(), wptr, rptr);
      timing::Summary t = timing::Summarize(5.0);
      KLOG_I("frames: %.1f swaps/s (gpu %.1f), presents/s gpu thread %.1f ui thread %.1f (%.1f repaints), "
             "swap interval mean %.2f sd %.2f p50 %.2f p99 %.2f max %.1f ms, present p99 %.2f max %.1f ms, "
             "vblank %.1f/s late p99 %.0f max %.0f us",
             t.vdswaps / 5.0, t.gpu_swaps / 5.0, t.presents_gpu_thread / 5.0, t.presents_ui_thread / 5.0,
             t.ui_repaints / 5.0, t.swap_mean_ms, t.swap_stddev_ms, t.swap_p50_ms, t.swap_p99_ms, t.swap_max_ms,
             t.present_p99_ms, t.present_max_ms, t.vblanks / 5.0, t.vblank_late_p99_us, t.vblank_late_max_us);
      if (uint64_t skipped = g_vblanks_skipped.exchange(0)) KLOG_W("vblank: %llu ticks skipped (late)",
                                                                   (unsigned long long)skipped);
      static uint64_t last_wakes = 0;
      uint64_t wakes, missed;
      DispatcherCounters(wakes, missed);
      KLOG_I("dispatcher: %.0f wakes/s, %llu missed wakeups; vblank %s, present phase error %+.2f",
             (wakes - last_wakes) / 5.0, (unsigned long long)missed,
             g_display_locked ? "locked to the display" : "free-running", g_present_phase_error.load());
      last_wakes = wakes;
      // ANYX360_FRAMETIMES=<file.csv>: every frame timing event (see tools/scripts/frametimes.py).
      static const char* frametimes = std::getenv("ANYX360_FRAMETIMES");
      if (frametimes) timing::AppendCsv(frametimes);
      static int stalled = 0;
      stalled = (swaps == last_swaps) ? stalled + 1 : 0;
      if (stalled == 2) DumpThreads();
      last_swaps = swaps;
      static const char* capture_dir = std::getenv("ANYX360_CAPTURE_DIR");
      static int capture_index = 0;
      if (capture_dir && !std::getenv("ANYX360_CAPTURE_MS"))
        CaptureFrame(g_graphics->presenter(), capture_dir, capture_index++);
      static const char* dump_at = std::getenv("ANYX360_DUMP_AT");  // seconds, comma separated
      static int tick = 0;
      ++tick;
      if (dump_at) {
        for (const char* p = dump_at; *p;) {
          int seconds = std::atoi(p);
          if (seconds / 5 == tick) {
            DumpThreads();
            if (const char* text = std::getenv("ANYX360_MEMSCAN")) ScanGuestMemory(text);
            if (const char* dump = std::getenv("ANYX360_MEMDUMP")) {  // "addr,length" in hex
              uint32_t addr = uint32_t(std::strtoul(dump, nullptr, 16));
              const char* comma = std::strchr(dump, ',');
              uint32_t length = comma ? uint32_t(std::strtoul(comma + 1, nullptr, 16)) : 0x100;
              for (uint32_t row = 0; row < length; row += 32) {
                std::string line;
                char word[12];
                for (uint32_t i = 0; i < 32; i += 4) {
                  std::snprintf(word, sizeof(word), "%08X ", Load<uint32_t>(addr + row + i));
                  line += word;
                }
                KLOG_I("memdump %08X: %s", addr + row, line.c_str());
              }
            }
          }
          while (*p && *p != ',') ++p;
          if (*p == ',') ++p;
        }
      }
    }
  }).detach();
  // ANYX360_CAPTURE_MS: capture frames at this interval instead of every 5 s (with ANYX360_CAPTURE_DIR).
  if (const char* ms = std::getenv("ANYX360_CAPTURE_MS"); ms && std::getenv("ANYX360_CAPTURE_DIR")) {
    int interval = std::max(50, std::atoi(ms));
    std::thread([interval] {
      std::string dir = std::getenv("ANYX360_CAPTURE_DIR");
      for (int index = 0;; ++index) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        CaptureFrame(g_graphics->presenter(), dir, index);
      }
    }).detach();
  }
  // Test hook: ANYX360_FPS_SWITCH="<seconds>:<fps>,..." changes the target frame rate at those times.
  if (const char* spec = std::getenv("ANYX360_FPS_SWITCH")) {
    std::thread([schedule = std::string(spec)] {
      auto start = std::chrono::steady_clock::now();
      for (size_t pos = 0; pos < schedule.size();) {
        size_t end = std::min(schedule.find(',', pos), schedule.size());
        std::string item = schedule.substr(pos, end - pos);
        pos = end + 1;
        size_t colon = item.find(':');
        if (colon == std::string::npos) continue;
        std::this_thread::sleep_until(start + std::chrono::milliseconds(int64_t(std::atof(item.c_str()) * 1000)));
        uint32_t fps = uint32_t(std::atoi(item.c_str() + colon + 1));
        g_app->CallInUIThread([fps] { SetTargetFps(fps); });
      }
    }).detach();
  }
  KLOG_I("GPU: %s", g_graphics->name().c_str());
  return true;
}

void InitializeShaderStorage(const std::filesystem::path& cache_root, uint32_t title_id) {
  if (!g_graphics) return;
  KLOG_I("GPU: shader storage in %s", cache_root.string().c_str());
  g_graphics->InitializeShaderStorage(cache_root, title_id, false);
}

int RunMessageLoop() {
  int result = g_app->RunMainMessageLoop();
  return result ? result : g_exit_code.load();
}

void RequestQuit(int exit_code) {
  g_exit_code = exit_code;
  g_app->CallInUIThread([] { g_app->QuitFromUIThread(); });
}

void InitializeRingBuffer(uint32_t physical, uint32_t size_log2) {
  g_graphics->InitializeRingBuffer(physical, size_log2);
}

void EnableReadPointerWriteBack(uint32_t physical, uint32_t block_size_log2) {
  g_writeback = physical;
  g_graphics->EnableReadPointerWriteBack(physical, block_size_log2);
}

void SetInterruptCallback(uint32_t callback, uint32_t user_data) {
  KLOG_I("GPU: interrupt callback %08X (%08X)", callback, user_data);
  g_graphics->SetInterruptCallback(callback, user_data);
}

}  // namespace x360::gpu

// Xenia's config.cc (persisted settings) is not part of the subset AnyX360 builds.
namespace config {
void SaveConfig() {}
}  // namespace config
