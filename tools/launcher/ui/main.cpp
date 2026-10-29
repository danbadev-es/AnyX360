// AnyX360's launcher (AnyX360.exe): the player's games and the place to start them. The logic is in
// core/ (launcher_core); app.cpp draws the window with Direct2D; this file is the window itself:
// painting, mouse, keyboard, an Xbox controller (polled only while the launcher is in front: it
// stays alive, hidden, while a game runs), files dropped on it, a dark title bar.
//
//   --archive <file> | --folder <dir>   add a game without asking (any format, an .anyx backup too;
//                                       unattended tests)
//   --export <id> --to <file>           save the game's .anyx backup there without asking
//   --password <text>                   for a protected archive
//   --password-env <variable>           the same, read from an environment variable (it then isn't
//                                       on a command line other programs can read)
//   --quit-when-done --result <file>    after that job, write its outcome ("ok: ..." or "error: ...")
//                                       and exit (without the terms accepted: "error: ..." at once)
//   --select <id>                       the game selected at start
//   --console | --desktop               start in console mode (the dashboard) or desktop mode, this
//                                       time only (the mode chosen in the launcher is remembered)
//   --pad also takes GUIDE (the Xbox button: the dashboard's Home)
//   --render-avatar <png> [--size WxH] [--yaw degrees] [--zoom 0-1] [--avatar <entries|random:N>]
//                                       the avatar of the profile playing (or that one) alone into a
//                                       PNG, transparent around it (the viewer's picture)
//   --avatar <entries|random:N>         screenshots: the avatar edited, instead of the profile's
//   --anim idle|wave|nod [--anim-time seconds]
//                                       screenshots and --render-avatar: the avatar posed that far into
//                                       its idle and the gesture (at rest without --anim)
//   --anim-sheet <frames> [--anim-span seconds]
//                                       --render-avatar: that many frames side by side, from
//                                       --anim-time over the gesture (or the idle's 24 s, or the span)
//   --screenshot <png> [--size WxH] [--dpi N] [--lang en|es] [--state S] [--root <dir>]
//                                       draw the window offscreen into a PNG: S is library, empty,
//                                       compatible, add, progress, error, confirm, password, menu,
//                                       terms, who, profiles, newprofile, achievements,
//                                       gameachievements or the dashboard's pages (console-home,
//                                       console-games, console-achievements, console-profiles,
//                                       console-avatar, console-settings, console-add), avatar (the
//                                       editor over desktop mode) or gamerpic; --root uses another release
//                                       folder
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <ole2.h>
#include <shellapi.h>
#include <wincodec.h>
#include <xinput.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "core/paths.h"
#include "core/strings.h"
#include "core/terms.h"
#include "d2d/canvas.h"
#include "ui/app.h"
#include "ui/avatar_page.h"
#include "wic/image.h"

using namespace anyx360::launcher;
using namespace anyx360::launcher::ui;

namespace {

constexpr UINT kMsgRun = WM_APP + 1;  // run what other threads posted
constexpr UINT_PTR kPadTimer = 1;
constexpr float kWidth = 1280, kHeight = 800, kMinWidth = 1000, kMinHeight = 700;  // DIPs

class WindowHost : public Host {
 public:
  HWND hwnd = nullptr;

  void Invalidate() override {
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
  }
  void Post(std::function<void()> fn) override {
    {
      std::lock_guard lock(mutex_);
      queue_.push_back(std::move(fn));
    }
    if (hwnd) PostMessageW(hwnd, kMsgRun, 0, 0);
  }
  void RunPosted() {
    std::vector<std::function<void()>> run;
    {
      std::lock_guard lock(mutex_);
      run.swap(queue_);
    }
    for (auto& fn : run) fn();
  }
  HWND Window() override { return hwnd; }
  void ShowWindow(bool show) override {
    if (!show) {
      ::ShowWindow(hwnd, SW_HIDE);
      return;
    }
    // Painted and composed before this returns: a game leaving for the dashboard ends right after.
    InvalidateRect(hwnd, nullptr, FALSE);
    ::ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    UpdateWindow(hwnd);
    DwmFlush();
  }
  void Close() override { DestroyWindow(hwnd); }

  void SetBorderless(bool borderless, bool fullscreen) override {
    const LONG_PTR visible = GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE;
    if (borderless) {
      // The window's place as it looks (without the invisible resize borders), or its monitor. A
      // maximized window (as the launcher starts) is maximized again afterwards.
      if (!borderless_) {
        was_maximized_ = IsZoomed(hwnd) || !visible;
        if (IsZoomed(hwnd)) ::ShowWindow(hwnd, SW_RESTORE);
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &desktop_rect_, sizeof(desktop_rect_))))
          GetWindowRect(hwnd, &desktop_rect_);
      }
      RECT r = desktop_rect_;
      if (fullscreen) {
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromRect(&desktop_rect_, MONITOR_DEFAULTTONEAREST), &monitor);
        r = monitor.rcMonitor;
      }
      SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN | visible);
      DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;  // as the game's window
      DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
      SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else if (borderless_) {
      SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | visible);
      DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DEFAULT;
      DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
      // The visible rectangle again, with the frame's invisible borders around it.
      RECT frame{0, 0, 0, 0};
      AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(hwnd));
      const int border = -frame.left;
      SetWindowPos(hwnd, nullptr, desktop_rect_.left - border, desktop_rect_.top,
                   desktop_rect_.right - desktop_rect_.left + 2 * border,
                   desktop_rect_.bottom - desktop_rect_.top + border, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
      if (was_maximized_ && visible) ::ShowWindow(hwnd, SW_MAXIMIZE);
    }
    borderless_ = borderless;
    InvalidateRect(hwnd, nullptr, FALSE);  // WM_SIZE resized the canvas
  }

  bool ScreenRect(RECT& rect) override {
    // In a window with its title bar (maximized, as it starts), the whole window as it looks: the
    // borderless game covers the title bar too, so nothing of the desktop shows above it.
    if (!borderless_ && SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
      if (IsZoomed(hwnd)) {
        // Maximized, the frame overhangs the monitor's work area: within it.
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
        IntersectRect(&rect, &rect, &monitor.rcWork);
      }
      return true;
    }
    if (!GetClientRect(hwnd, &rect)) return false;
    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    OffsetRect(&rect, origin.x, origin.y);
    return true;
  }

 private:
  bool borderless_ = false;
  bool was_maximized_ = false;
  RECT desktop_rect_{};
  std::mutex mutex_;
  std::vector<std::function<void()>> queue_;
};

// Screenshots: nothing to invalidate, and what other threads post isn't needed.
class OffscreenHost : public Host {
 public:
  void Invalidate() override {}
  void Post(std::function<void()>) override {}
  HWND Window() override { return nullptr; }
  void ShowWindow(bool) override {}
  void Close() override {}
};

WindowHost g_host;
App* g_app = nullptr;
anyx360::ui::Canvas* g_canvas = nullptr;

float Dips(HWND hwnd, int pixels) { return float(pixels) * 96.0f / float(GetDpiForWindow(hwnd)); }

// ---- the avatar's frames ------------------------------------------------------------------------
// While the avatar moves, the window is painted again when its next picture is due (App::AvatarFrameIn:
// 30 a second, 60 while it gestures), from a high-resolution timer the message loop waits on; only
// while it can be seen. Everything else stays painted on events.

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
HANDLE g_frame_timer = nullptr;

void ArmFrameTimer(double seconds) {
  LARGE_INTEGER due;
  due.QuadPart = -std::max<LONGLONG>(1, LONGLONG(seconds * 1e7));  // relative, in 100 ns
  SetWaitableTimer(g_frame_timer, &due, 0, nullptr, nullptr, FALSE);
}

// Windows' animation effects (Settings > Accessibility > Visual effects): off, the avatar stands still.
bool AnimationEffects() {
  BOOL on = TRUE;
  return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0) || on;
}

// The window can't be seen: minimized, hidden (a game runs), on another virtual desktop, or under the
// window in front, which covers it all (one maximized).
bool Unseen(HWND hwnd) {
  if (IsIconic(hwnd) || !IsWindowVisible(hwnd)) return true;
  BOOL cloaked = FALSE;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return true;
  HWND front = GetForegroundWindow();
  if (!front || front == hwnd || IsIconic(front) || GetAncestor(front, GA_ROOTOWNER) == hwnd) return false;
  wchar_t name[32] = {};
  GetClassNameW(front, name, 32);
  if (!wcscmp(name, L"Progman") || !wcscmp(name, L"WorkerW") || !wcscmp(name, L"Shell_TrayWnd")) return false;  // the desktop
  RECT mine{}, theirs{};
  GetClientRect(hwnd, &mine);
  MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&mine), 2);
  if (FAILED(DwmGetWindowAttribute(front, DWMWA_EXTENDED_FRAME_BOUNDS, &theirs, sizeof(theirs)))) GetWindowRect(front, &theirs);
  return theirs.left <= mine.left && theirs.top <= mine.top && theirs.right >= mine.right && theirs.bottom >= mine.bottom;
}

// After a paint: when to paint the avatar's next picture (the canvas couldn't draw: look again in a
// while). Minimized or hidden, nothing: it is painted again when shown.
void ScheduleAvatarFrame(HWND hwnd, bool painted) {
  if (!g_app || !g_frame_timer || IsIconic(hwnd) || !IsWindowVisible(hwnd)) return;
  const double in = g_app->AvatarFrameIn();
  if (in >= 0) ArmFrameTimer(painted ? in : 0.5);
}

// The timer: paint if the avatar can be seen; else look again in half a second.
void AvatarFrameDue(HWND hwnd) {
  if (!g_app || g_app->AvatarFrameIn() < 0) return;
  if (!Unseen(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
  else if (!IsIconic(hwnd) && IsWindowVisible(hwnd)) ArmFrameTimer(0.5);
}

// The icon (launcher.rc) at the sizes for the window's DPI: the small one for the title bar, the big
// one for the taskbar and Alt+Tab. The class's are at the system's DPI, which may be another monitor's.
void SetIcons(HWND hwnd) {
  const UINT dpi = GetDpiForWindow(hwnd);
  for (const auto [which, metric] : {std::pair{ICON_SMALL, SM_CXSMICON}, std::pair{ICON_BIG, SM_CXICON}}) {
    const int size = GetSystemMetricsForDpi(metric, dpi);
    HANDLE icon = LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    if (!icon) continue;
    if (HICON previous = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_SETICON, which, reinterpret_cast<LPARAM>(icon))))
      DestroyIcon(previous);
  }
}

// ---- the Xbox controller --------------------------------------------------------------------------

struct Pad {
  bool connected = true;
  WORD buttons = 0;
  int direction = -1;      // the held direction (Key), -1 none
  ULONGLONG repeat_at = 0;  // when it repeats
};
Pad g_pads[XUSER_MAX_COUNT];
unsigned g_pad_tick = 0;

// --pad "<seconds>:<button>,...": buttons pressed on the first controller at those times after the
// window opens (A B X Y START LB RB UP DOWN LEFT RIGHT), each held 150 ms: the controller navigation
// tested unattended.
struct ScriptedPress {
  ULONGLONG at;
  WORD button;
};
std::vector<ScriptedPress> g_pad_script;
ULONGLONG g_pad_start = 0;

void ParsePadScript(const std::wstring& text) {
  static const std::pair<const wchar_t*, WORD> kButtons[] = {
      {L"A", XINPUT_GAMEPAD_A},         {L"B", XINPUT_GAMEPAD_B},
      {L"X", XINPUT_GAMEPAD_X},         {L"Y", XINPUT_GAMEPAD_Y},
      {L"START", XINPUT_GAMEPAD_START}, {L"LB", XINPUT_GAMEPAD_LEFT_SHOULDER},
      {L"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER}, {L"UP", XINPUT_GAMEPAD_DPAD_UP},
      {L"DOWN", XINPUT_GAMEPAD_DPAD_DOWN},    {L"LEFT", XINPUT_GAMEPAD_DPAD_LEFT},
      {L"RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},  {L"GUIDE", 0x0400}};
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find(L',', start);
    if (end == std::wstring::npos) end = text.size();
    const std::wstring item = text.substr(start, end - start);
    const size_t colon = item.find(L':');
    if (colon != std::wstring::npos) {
      const std::wstring name = item.substr(colon + 1);
      for (const auto& [button, bit] : kButtons)
        if (name == button) g_pad_script.push_back({ULONGLONG(_wtof(item.substr(0, colon).c_str()) * 1000), bit});
    }
    start = end + 1;
  }
}

WORD ScriptedButtons(ULONGLONG now) {
  WORD buttons = 0;
  for (const ScriptedPress& press : g_pad_script)
    if (now >= g_pad_start + press.at && now < g_pad_start + press.at + 150) buttons |= press.button;
  return buttons;
}

// XInputGetStateEx (ordinal 100) also has the Guide button (0x0400): Home on the dashboard.
using GetStateFunction = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
GetStateFunction GetState() {
  static const GetStateFunction function = [] {
    for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll"})
      if (HMODULE module = LoadLibraryW(name))
        if (auto f = reinterpret_cast<GetStateFunction>(GetProcAddress(module, reinterpret_cast<LPCSTR>(100)))) return f;
    return GetStateFunction(&XInputGetState);
  }();
  return function;
}
constexpr WORD kGuideButton = 0x0400;

void PollPads() {
  ++g_pad_tick;
  const ULONGLONG now = GetTickCount64();
  for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) {
    Pad& pad = g_pads[user];
    const bool scripted = user == 0 && !g_pad_script.empty();
    // An empty slot is slow to ask: about once a second.
    if (!pad.connected && !scripted && g_pad_tick % 60 != user) continue;
    XINPUT_STATE state{};
    const bool was_connected = pad.connected;
    pad.connected = GetState()(user, &state) == ERROR_SUCCESS || scripted;
    if (!pad.connected) continue;
    // A controller plugged in (or on at start): its button hints show before it is used.
    if (!was_connected || g_pad_tick == 1) g_app->PadConnected();
    const WORD buttons = state.Gamepad.wButtons | (scripted ? ScriptedButtons(now) : 0);
    const WORD pressed = buttons & ~pad.buttons;
    pad.buttons = buttons;
    if (pressed & XINPUT_GAMEPAD_A) g_app->KeyPressed(Key::kActivate, true);
    if (pressed & XINPUT_GAMEPAD_B) g_app->KeyPressed(Key::kBack, true);
    if (pressed & XINPUT_GAMEPAD_Y) g_app->KeyPressed(Key::kMore, true);
    if (pressed & XINPUT_GAMEPAD_X) g_app->KeyPressed(Key::kCompatible, true);
    if (pressed & XINPUT_GAMEPAD_START) g_app->KeyPressed(Key::kPlay, true);
    if (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER) g_app->KeyPressed(Key::kPrevious, true);
    if (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) g_app->KeyPressed(Key::kNext, true);
    if (pressed & kGuideButton) g_app->KeyPressed(Key::kGuide, true);
    // The right stick: the avatar viewer turns and zooms.
    {
      constexpr float kStickDead = 8000;
      auto axis = [](SHORT v) {
        const float f = float(v);
        return std::abs(f) < kStickDead ? 0.0f : (f - std::copysign(kStickDead, f)) / (32767.0f - kStickDead);
      };
      const float rx = axis(state.Gamepad.sThumbRX), ry = axis(state.Gamepad.sThumbRY);
      if (rx || ry) g_app->Stick(rx, ry);
    }
    // The d-pad or the left stick, repeating while held.
    const SHORT x = state.Gamepad.sThumbLX, y = state.Gamepad.sThumbLY;
    constexpr SHORT kDead = 10000;  // about 30 %: a light push moves the focus
    int direction = -1;
    if ((buttons & XINPUT_GAMEPAD_DPAD_LEFT) || x < -kDead) direction = int(Key::kLeft);
    else if ((buttons & XINPUT_GAMEPAD_DPAD_RIGHT) || x > kDead) direction = int(Key::kRight);
    else if ((buttons & XINPUT_GAMEPAD_DPAD_UP) || y > kDead) direction = int(Key::kUp);
    else if ((buttons & XINPUT_GAMEPAD_DPAD_DOWN) || y < -kDead) direction = int(Key::kDown);
    if (direction != pad.direction) {
      pad.direction = direction;
      if (direction >= 0) {
        g_app->KeyPressed(Key(direction), true);
        pad.repeat_at = now + 400;
      }
    } else if (direction >= 0 && now >= pad.repeat_at) {
      g_app->KeyPressed(Key(direction), true);
      pad.repeat_at = now + 110;
    }
  }
  int connected = 0;
  for (const Pad& pad : g_pads) connected += pad.connected ? 1 : 0;
  g_app->SetPads(connected);
}

// ---- files dropped on the window -----------------------------------------------------------------

class DropTarget : public IDropTarget {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
      *object = static_cast<IDropTarget*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override { return --references_; }  // static lifetime

  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    accept_ = data && SUCCEEDED(data->QueryGetData(&format));
    *effect = accept_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (accept_) g_app->SetDropHover(true);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
    *effect = accept_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragLeave() override {
    g_app->SetDropHover(false);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
    g_app->SetDropHover(false);
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (!data || FAILED(data->GetData(&format, &medium))) return S_OK;
    HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
    std::wstring path;
    if (drop && DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0) > 0) {
      path.resize(DragQueryFileW(drop, 0, nullptr, 0));
      DragQueryFileW(drop, 0, path.data(), UINT(path.size() + 1));
    }
    GlobalUnlock(medium.hGlobal);
    ReleaseStgMedium(&medium);
    *effect = DROPEFFECT_COPY;
    if (!path.empty()) g_app->AddSource(path);
    return S_OK;
  }

 private:
  ULONG references_ = 1;
  bool accept_ = false;
} g_drop;

// ---- the window ---------------------------------------------------------------------------------

std::wstring ClipboardText(HWND hwnd) {
  std::wstring text;
  if (!OpenClipboard(hwnd)) return text;
  if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
    if (const wchar_t* chars = static_cast<const wchar_t*>(GlobalLock(data))) {
      text = chars;
      GlobalUnlock(data);
    }
  }
  CloseClipboard();
  return text;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT paint;
      BeginPaint(hwnd, &paint);
      bool painted = false;
      if (g_canvas && g_app && g_canvas->Begin()) {
        g_app->Paint(*g_canvas);
        g_canvas->End();
        painted = true;
      }
      EndPaint(hwnd, &paint);
      if (g_app && g_app->Animating()) InvalidateRect(hwnd, nullptr, FALSE);
      else ScheduleAvatarFrame(hwnd, painted);
      return 0;
    }
    case WM_SETTINGCHANGE:
      if (g_app) g_app->SetReducedMotion(!AnimationEffects());
      break;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
      if (g_canvas) g_canvas->Resize();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_DPICHANGED: {
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      if (g_canvas) g_canvas->Resize();
      SetIcons(hwnd);
      return 0;
    }
    case WM_GETMINMAXINFO: {
      const UINT dpi = GetDpiForWindow(hwnd);
      RECT frame{0, 0, LONG(kMinWidth * dpi / 96), LONG(kMinHeight * dpi / 96)};
      AdjustWindowRectExForDpi(&frame, DWORD(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE, 0, dpi);
      reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {frame.right - frame.left, frame.bottom - frame.top};
      return 0;
    }
    case WM_MOUSEMOVE: {
      TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&track);
      if (g_app) g_app->MouseMove(Dips(hwnd, GET_X_LPARAM(lparam)), Dips(hwnd, GET_Y_LPARAM(lparam)));
      return 0;
    }
    case WM_MOUSELEAVE:
      if (g_app) g_app->MouseLeave();
      return 0;
    case WM_LBUTTONDOWN:
      SetCapture(hwnd);
      if (g_app) g_app->MouseDown(Dips(hwnd, GET_X_LPARAM(lparam)), Dips(hwnd, GET_Y_LPARAM(lparam)));
      return 0;
    case WM_LBUTTONUP:
      ReleaseCapture();
      if (g_app) g_app->MouseUp(Dips(hwnd, GET_X_LPARAM(lparam)), Dips(hwnd, GET_Y_LPARAM(lparam)));
      return 0;
    case WM_RBUTTONUP:
      if (g_app) g_app->RightClick(Dips(hwnd, GET_X_LPARAM(lparam)), Dips(hwnd, GET_Y_LPARAM(lparam)));
      return 0;
    case WM_MOUSEWHEEL: {
      POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd, &at);
      if (g_app) g_app->Wheel(Dips(hwnd, at.x), Dips(hwnd, at.y), float(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA);
      return 0;
    }
    case WM_MOUSEHWHEEL: {  // a tilting wheel, or a trackpad's sideways swipe
      POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd, &at);
      if (g_app) g_app->Wheel(Dips(hwnd, at.x), Dips(hwnd, at.y), float(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA, true);
      return TRUE;
    }
    case WM_SETCURSOR:
      if (LOWORD(lparam) == HTCLIENT) {
        SetCursor(LoadCursorW(nullptr, g_app && g_app->PointerOnTarget() ? IDC_HAND : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_KEYDOWN: {
      if (!g_app) break;
      const bool shift = GetKeyState(VK_SHIFT) & 0x8000, control = GetKeyState(VK_CONTROL) & 0x8000;
      if (g_app->WantsText()) {
        if (wparam == 'V' && control) {
          g_app->Paste(ClipboardText(hwnd));
          return 0;
        }
        // Typed into the field: WM_CHAR has them.
        if (wparam == VK_RETURN || wparam == VK_ESCAPE || wparam == VK_BACK || wparam == VK_SPACE) return 0;
      }
      switch (wparam) {
        case VK_LEFT: g_app->KeyPressed(Key::kLeft, false); return 0;
        case VK_RIGHT: g_app->KeyPressed(Key::kRight, false); return 0;
        case VK_UP: g_app->KeyPressed(Key::kUp, false); return 0;
        case VK_DOWN: g_app->KeyPressed(Key::kDown, false); return 0;
        case VK_RETURN:
        case VK_SPACE: g_app->KeyPressed(Key::kActivate, false); return 0;
        case VK_ESCAPE:
        case VK_BACK: g_app->KeyPressed(Key::kBack, false); return 0;
        case VK_TAB: g_app->KeyPressed(shift ? Key::kPrevious : Key::kNext, false); return 0;
        case VK_HOME: g_app->KeyPressed(Key::kHome, false); return 0;
        case VK_END: g_app->KeyPressed(Key::kEnd, false); return 0;
        case VK_APPS: g_app->KeyPressed(Key::kMore, false); return 0;
      }
      break;
    }
    case WM_CHAR:
      if (g_app && g_app->WantsText()) g_app->Character(wchar_t(wparam));
      return 0;
    case WM_ACTIVATE:
      // The controller is read only while the launcher is in front.
      if (LOWORD(wparam) == WA_INACTIVE) {
        if (g_pad_script.empty()) KillTimer(hwnd, kPadTimer);
      } else {
        SetTimer(hwnd, kPadTimer, 16, nullptr);
        if (g_app && g_app->AvatarFrameIn() >= 0) InvalidateRect(hwnd, nullptr, FALSE);  // in front again: it moves
      }
      break;
    case WM_TIMER:
      // Only while the launcher is in front, so a controller used in another program doesn't move it
      // (a scripted test reads it anyway: started from a background program, it can't come in front).
      if (wparam == kPadTimer && g_app && IsWindowVisible(hwnd) &&
          (GetForegroundWindow() == hwnd || !g_pad_script.empty())) {
        PollPads();
        if (g_pad_tick % 60 == 0) g_app->Tick();
      }
      return 0;
    case kMsgRun:
      g_host.RunPosted();
      return 0;
    case WM_CLOSE:
      if (!g_app || g_app->OnClose()) DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

// The window drawn into a PNG, without showing it.
int Screenshot(const fs::path& out, int width, int height, float dpi, const std::wstring& state, const std::wstring& select,
               const std::wstring& avatar, const AvatarFrames& frames) {
  using anyx360::ui::ComPtr;
  OffscreenHost host;
  App app(host);
  app.images().SetSynchronous(true);
  app.Load(select);
  app.SetScreenshotAvatar(Narrow(avatar));
  if (frames.posed) app.SetScreenshotMotion(frames.gesture, frames.time);
  ComPtr<IWICImagingFactory> wic;
  ComPtr<IWICBitmap> bitmap;
  ComPtr<ID2D1RenderTarget> target;
  const UINT w = UINT(std::lround(width * dpi / 96)), h = UINT(std::lround(height * dpi / 96));
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
      FAILED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
      FAILED(anyx360::ui::D2DFactory()->CreateWicBitmapRenderTarget(
          bitmap.Get(),
          D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                       D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi),
          &target)))
    return 1;
  anyx360::ui::Canvas canvas(target);
  // Laid out once, the state set up, then drawn (twice: the focus settles on the first).
  for (int pass = 0; pass < 3; ++pass) {
    if (pass == 1) app.ShowState(state);
    canvas.Begin();
    app.Paint(canvas);
    canvas.End();
  }
  anyx360::ui::Image image;
  image.width = w;
  image.height = h;
  image.bgra.resize(size_t(w) * h * 4);
  if (FAILED(bitmap->CopyPixels(nullptr, w * 4, UINT(image.bgra.size()), image.bgra.data()))) return 1;
  std::string error;
  return anyx360::ui::SavePng(image, out, error) ? 0 : 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  OleInitialize(nullptr);  // COM for the file dialogs and WIC, and drag and drop
  LoadLanguage();

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::wstring select, source, password, screenshot, state = L"library", language, export_id;
  std::wstring avatar, render_avatar;
  float yaw = 0, zoom = 0;
  AvatarFrames frames;
  fs::path result, export_to;
  bool quit_when_done = false;
  int mode = -1;  // --desktop 0, --console 1; -1: the one saved
  int shot_w = int(kWidth), shot_h = int(kHeight);
  float shot_dpi = 96;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    const bool value = i + 1 < argc;
    if (arg == L"--quit-when-done") quit_when_done = true;
    if (arg == L"--console") mode = 1;
    if (arg == L"--desktop") mode = 0;
    if (!value) continue;
    if (arg == L"--select") select = argv[++i];
    else if (arg == L"--archive" || arg == L"--folder") source = argv[++i];
    else if (arg == L"--password") password = argv[++i];
    else if (arg == L"--password-env") {
      wchar_t value_text[1024];
      const DWORD length = GetEnvironmentVariableW(argv[++i], value_text, 1024);
      if (length && length < 1024) password.assign(value_text, length);
    }
    else if (arg == L"--export") export_id = argv[++i];
    else if (arg == L"--to") export_to = argv[++i];
    else if (arg == L"--result") result = argv[++i];
    else if (arg == L"--screenshot") screenshot = argv[++i];
    else if (arg == L"--state") state = argv[++i];
    else if (arg == L"--lang") language = argv[++i];
    else if (arg == L"--dpi") shot_dpi = float(_wtof(argv[++i]));
    else if (arg == L"--root") SetRootForTesting(argv[++i]);
    else if (arg == L"--size") swscanf_s(argv[++i], L"%dx%d", &shot_w, &shot_h);
    else if (arg == L"--pad") ParsePadScript(argv[++i]);
    else if (arg == L"--avatar") avatar = argv[++i];
    else if (arg == L"--render-avatar") render_avatar = argv[++i];
    else if (arg == L"--yaw") yaw = float(_wtof(argv[++i])) * 3.14159265f / 180;
    else if (arg == L"--zoom") zoom = float(_wtof(argv[++i]));
    else if (arg == L"--anim") {
      const std::wstring gesture = argv[++i];
      frames.posed = true;
      frames.gesture = gesture == L"wave"  ? avatar::Gesture::kWave
                       : gesture == L"nod" ? avatar::Gesture::kNod
                                           : avatar::Gesture::kNone;
    }
    else if (arg == L"--anim-time") frames.posed = true, frames.time = _wtof(argv[++i]);
    else if (arg == L"--anim-span") frames.span = _wtof(argv[++i]);
    else if (arg == L"--anim-sheet") frames.posed = true, frames.count = _wtoi(argv[++i]);
  }
  LocalFree(argv);
  if (language == L"es" || language == L"en") SetLanguage(language == L"es" ? Language::kSpanish : Language::kEnglish);
  if (!render_avatar.empty()) {
    const int code = RenderAvatarFile(render_avatar, shot_w, shot_h, yaw, zoom, Narrow(avatar), frames);
    OleUninitialize();
    return code;
  }
  if (!screenshot.empty()) {
    const int code = Screenshot(screenshot, shot_w, shot_h, shot_dpi > 0 ? shot_dpi : 96, state, select, avatar, frames);
    OleUninitialize();
    return code;
  }

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR | LR_SHARED));
  wc.hbrBackground = CreateSolidBrush(RGB(0x14, 0x14, 0x14));
  wc.lpszClassName = L"AnyX360Launcher";
  RegisterClassExW(&wc);
  const DWORD style = WS_OVERLAPPEDWINDOW;
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"AnyX360", style, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr,
                              nullptr, instance, nullptr);
  if (!hwnd) return 1;
  g_host.hwnd = hwnd;
  SetIcons(hwnd);
  // Dark title bar (and, on Windows 11, the background's color).
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
  COLORREF caption = RGB(0x14, 0x14, 0x14);
  DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));

  // The client area at the window's DPI, centered on its monitor and inside its work area.
  const UINT dpi = GetDpiForWindow(hwnd);
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor);
  RECT frame{0, 0, LONG(kWidth * dpi / 96), LONG(kHeight * dpi / 96)};
  AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
  const int width = std::min<int>(frame.right - frame.left, monitor.rcWork.right - monitor.rcWork.left);
  const int height = std::min<int>(frame.bottom - frame.top, monitor.rcWork.bottom - monitor.rcWork.top);
  SetWindowPos(hwnd, nullptr, (monitor.rcWork.left + monitor.rcWork.right - width) / 2,
               (monitor.rcWork.top + monitor.rcWork.bottom - height) / 2, width, height, SWP_NOZORDER);

  {
    anyx360::ui::Canvas canvas(hwnd);
    App app(g_host);
    g_canvas = &canvas;
    g_app = &app;
    g_frame_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!g_frame_timer) g_frame_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);  // before Windows 10 1803
    app.SetReducedMotion(!AnimationEffects());
    app.Load(select);
    // Console mode (the dashboard), as last chosen, or as the command line says (not remembered).
    bool console = false, fullscreen = false;
    App::SavedMode(console, fullscreen);
    if (mode >= 0) console = mode == 1;
    if (console) app.SetConsoleMode(true, false);
    app.CheckTerms();
    if (!password.empty()) app.SetPassword(password);
    if (quit_when_done && !result.empty()) app.QuitWhenDone(result);
    // Maximized, with its title bar and the taskbar in view (console mode's full screen, if chosen in
    // Settings, is borderless over the monitor instead).
    (void)show;
    if (!(console && fullscreen)) ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    else ShowWindow(hwnd, SW_SHOW);
    // In front, and the controller read from the start: activation alone may not come (a launcher
    // started from another program's window), and the pad would do nothing until a click.
    SetForegroundWindow(hwnd);
    SetTimer(hwnd, kPadTimer, 16, nullptr);
    g_pad_start = GetTickCount64();
    RegisterDragDrop(hwnd, &g_drop);
    const bool unattended = quit_when_done && !result.empty() && (!source.empty() || !export_id.empty());
    if (unattended && !TermsAccepted()) {
      // Nobody to accept them: say so instead of waiting.
      std::ofstream(result, std::ios::binary | std::ios::trunc) << "error: the terms of use aren't accepted\n";
      DestroyWindow(hwnd);
    } else if (!source.empty() && TermsAccepted()) {
      app.AddSource(source);
    } else if (!export_id.empty() && TermsAccepted()) {
      app.SaveAnyx(export_id, export_to);
    }

    // Messages, and the avatar's frames when they are due.
    MSG msg;
    for (bool running = true; running;) {
      const DWORD woke = MsgWaitForMultipleObjectsEx(g_frame_timer ? 1 : 0, g_frame_timer ? &g_frame_timer : nullptr, INFINITE,
                                                     QS_ALLINPUT, MWMO_INPUTAVAILABLE);
      if (g_frame_timer && woke == WAIT_OBJECT_0) AvatarFrameDue(hwnd);
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          running = false;
          break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
    g_app = nullptr;
    g_canvas = nullptr;
    if (g_frame_timer) CloseHandle(g_frame_timer);
    g_frame_timer = nullptr;
  }
  OleUninitialize();
  return 0;
}
