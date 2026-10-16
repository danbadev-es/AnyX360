// Game window hook (UI thread): captures the cursor for mouse look (clipped to the window and
// hidden) while the window is active and a player uses the keyboard and mouse, releases it when the
// window loses focus, and handles the system keys Xenia's window doesn't see (Alt+Enter) or that
// would stall the UI thread (Alt or F10 alone enter the window menu's modal loop).
#include <atomic>
#include <functional>

#include "input_internal.h"
#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::input::detail {

namespace {

constexpr UINT_PTR kClipTimer = 0xA360;

HWND g_hwnd = nullptr;
WNDPROC g_original = nullptr;
std::function<void()> g_toggle_fullscreen;
std::atomic<bool> g_captured{false};
RECT g_clip{};

bool CursorInClient() {
  POINT p;
  RECT client;
  if (!GetCursorPos(&p) || !ScreenToClient(g_hwnd, &p) || !GetClientRect(g_hwnd, &client)) return false;
  return PtInRect(&client, p);
}

bool ClientRectOnScreen(RECT& r) {
  if (!GetClientRect(g_hwnd, &r)) return false;
  MapWindowPoints(g_hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
  return r.right > r.left && r.bottom > r.top;
}

void Clip() {
  if (ClientRectOnScreen(g_clip)) ClipCursor(&g_clip);
}

void Capture() {
  if (g_captured || !KeyboardMouseInUse() || !GameInputEnabled() || GetForegroundWindow() != g_hwnd) return;
  g_captured = true;
  Clip();
  SetCursor(nullptr);
  SetTimer(g_hwnd, kClipTimer, 500, nullptr);
  KLOG_I("input: cursor captured");
}

LRESULT CALLBACK HookProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_ACTIVATE:
      if (LOWORD(wparam) == WA_INACTIVE)
        ReleaseCursor();
      else if (CursorInClient())
        Capture();
      break;
    case WM_KILLFOCUS:
      ReleaseCursor();
      break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
      if (!g_captured && KeyboardMouseInUse() && GameInputEnabled()) {
        Capture();
        if (g_captured) LatchHeldKeys();  // the click that captures the cursor doesn't fire
      }
      break;
    case WM_SETCURSOR:
      if (g_captured && LOWORD(lparam) == HTCLIENT) {
        SetCursor(nullptr);
        return TRUE;
      }
      break;
    case WM_TIMER:
      if (wparam == kClipTimer) {
        // Other programs (and UAC prompts) can reset the clip rectangle.
        RECT current;
        if (g_captured && GetClipCursor(&current) && !EqualRect(&current, &g_clip)) Clip();
        return 0;
      }
      break;
    case WM_SYSKEYDOWN:
      if (wparam == VK_RETURN && (lparam & (1 << 29))) {  // Alt+Enter
        if (!(lparam & (1 << 30)) && g_toggle_fullscreen) g_toggle_fullscreen();
        return 0;
      }
      break;
    case WM_SYSCOMMAND:
      if ((wparam & 0xFFF0) == SC_KEYMENU) return 0;  // no window menu
      break;
    default:
      break;
  }
  LRESULT result = CallWindowProcW(g_original, hwnd, message, wparam, lparam);
  switch (message) {
    case WM_SIZE:
    case WM_MOVE:
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
    case WM_WINDOWPOSCHANGED:
      if (g_captured) Clip();
      break;
    default:
      break;
  }
  return result;
}

}  // namespace

void InstallWindowHook(void* hwnd, std::function<void()> toggle_fullscreen) {
  g_hwnd = static_cast<HWND>(hwnd);
  g_toggle_fullscreen = std::move(toggle_fullscreen);
  g_original = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookProc)));
  if (GetForegroundWindow() == g_hwnd && CursorInClient()) Capture();
}

bool Captured() { return g_captured.load(std::memory_order_relaxed); }

bool GameWindowActive() { return g_hwnd && GetForegroundWindow() == g_hwnd; }

void ReleaseCursor() {
  if (!g_captured.exchange(false)) return;
  ClipCursor(nullptr);
  KillTimer(g_hwnd, kClipTimer);
  SetCursor(LoadCursorW(nullptr, IDC_ARROW));
  KLOG_I("input: cursor released");
}

void TryCaptureCursor() {
  if (CursorInClient()) Capture();
}

}  // namespace x360::input::detail
