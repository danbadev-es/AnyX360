// AnyX360's launcher (AnyX360.exe): the player's games and the place to start them. The logic is in
// core/ (launcher_core); app.cpp draws the window with Direct2D; this file is the window itself:
// painting, mouse, keyboard, an Xbox controller (polled only while the launcher is in front: it
// stays alive, hidden, while a game runs), files dropped on it, a dark title bar.
//
//   --archive <file> | --folder <dir>   add a game without asking (any format; unattended tests)
//   --password <text>                   for a protected archive
//   --quit-when-done --result <file>    after that job, write its outcome ("ok: ..." or "error: ...")
//                                       and exit
//   --select <id>                       the game selected at start
//   --screenshot <png> [--size WxH] [--dpi N] [--lang en|es] [--state S] [--root <dir>]
//                                       draw the window offscreen into a PNG: S is library, empty,
//                                       compatible, add, progress, error, confirm, password, menu
//                                       or terms; --root uses another release folder
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <ole2.h>
#include <shellapi.h>
#include <wincodec.h>
#include <xinput.h>

#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#include "core/paths.h"
#include "core/strings.h"
#include "core/terms.h"
#include "d2d/canvas.h"
#include "ui/app.h"
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
    ::ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE);
    if (show) SetForegroundWindow(hwnd);
  }
  void Close() override { DestroyWindow(hwnd); }

 private:
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

// ---- the Xbox controller --------------------------------------------------------------------------

struct Pad {
  bool connected = true;
  WORD buttons = 0;
  int direction = -1;      // the held direction (Key), -1 none
  ULONGLONG repeat_at = 0;  // when it repeats
};
Pad g_pads[XUSER_MAX_COUNT];
unsigned g_pad_tick = 0;

void PollPads() {
  ++g_pad_tick;
  const ULONGLONG now = GetTickCount64();
  for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) {
    Pad& pad = g_pads[user];
    // An empty slot is slow to ask: about once a second.
    if (!pad.connected && g_pad_tick % 60 != user) continue;
    XINPUT_STATE state{};
    pad.connected = XInputGetState(user, &state) == ERROR_SUCCESS;
    if (!pad.connected) continue;
    const WORD buttons = state.Gamepad.wButtons;
    const WORD pressed = buttons & ~pad.buttons;
    pad.buttons = buttons;
    if (pressed & XINPUT_GAMEPAD_A) g_app->KeyPressed(Key::kActivate, true);
    if (pressed & XINPUT_GAMEPAD_B) g_app->KeyPressed(Key::kBack, true);
    if (pressed & XINPUT_GAMEPAD_Y) g_app->KeyPressed(Key::kMore, true);
    if (pressed & XINPUT_GAMEPAD_X) g_app->KeyPressed(Key::kCompatible, true);
    if (pressed & XINPUT_GAMEPAD_START) g_app->KeyPressed(Key::kPlay, true);
    if (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER) g_app->KeyPressed(Key::kPrevious, true);
    if (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) g_app->KeyPressed(Key::kNext, true);
    // The d-pad or the left stick, repeating while held.
    const SHORT x = state.Gamepad.sThumbLX, y = state.Gamepad.sThumbLY;
    constexpr SHORT kDead = 16000;
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
      if (g_canvas && g_app && g_canvas->Begin()) {
        g_app->Paint(*g_canvas);
        g_canvas->End();
      }
      EndPaint(hwnd, &paint);
      if (g_app && g_app->Animating()) InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
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
      if (LOWORD(wparam) == WA_INACTIVE) KillTimer(hwnd, kPadTimer);
      else SetTimer(hwnd, kPadTimer, 16, nullptr);
      break;
    case WM_TIMER:
      if (wparam == kPadTimer && g_app && IsWindowVisible(hwnd) && GetForegroundWindow() == hwnd) PollPads();
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
int Screenshot(const fs::path& out, int width, int height, float dpi, const std::wstring& state, const std::wstring& select) {
  using anyx360::ui::ComPtr;
  OffscreenHost host;
  App app(host);
  app.images().SetSynchronous(true);
  app.Load(select);
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
  std::wstring select, source, password, screenshot, state = L"library", language;
  fs::path result;
  bool quit_when_done = false;
  int shot_w = int(kWidth), shot_h = int(kHeight);
  float shot_dpi = 96;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    const bool value = i + 1 < argc;
    if (arg == L"--quit-when-done") quit_when_done = true;
    if (!value) continue;
    if (arg == L"--select") select = argv[++i];
    else if (arg == L"--archive" || arg == L"--folder") source = argv[++i];
    else if (arg == L"--password") password = argv[++i];
    else if (arg == L"--result") result = argv[++i];
    else if (arg == L"--screenshot") screenshot = argv[++i];
    else if (arg == L"--state") state = argv[++i];
    else if (arg == L"--lang") language = argv[++i];
    else if (arg == L"--dpi") shot_dpi = float(_wtof(argv[++i]));
    else if (arg == L"--root") SetRootForTesting(argv[++i]);
    else if (arg == L"--size") swscanf_s(argv[++i], L"%dx%d", &shot_w, &shot_h);
  }
  LocalFree(argv);
  if (language == L"es" || language == L"en") SetLanguage(language == L"es" ? Language::kSpanish : Language::kEnglish);
  if (!screenshot.empty()) {
    const int code = Screenshot(screenshot, shot_w, shot_h, shot_dpi > 0 ? shot_dpi : 96, state, select);
    OleUninitialize();
    return code;
  }

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  wc.hbrBackground = CreateSolidBrush(RGB(0x14, 0x14, 0x14));
  wc.lpszClassName = L"AnyX360Launcher";
  RegisterClassExW(&wc);
  const DWORD style = WS_OVERLAPPEDWINDOW;
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"AnyX360", style, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr,
                              nullptr, instance, nullptr);
  if (!hwnd) return 1;
  g_host.hwnd = hwnd;
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
    app.Load(select);
    app.CheckTerms();
    if (!password.empty()) app.SetPassword(password);
    if (quit_when_done && !result.empty()) app.QuitWhenDone(result);
    ShowWindow(hwnd, show);
    RegisterDragDrop(hwnd, &g_drop);
    if (!source.empty() && TermsAccepted()) app.AddSource(source);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    g_app = nullptr;
    g_canvas = nullptr;
  }
  OleUninitialize();
  return 0;
}
