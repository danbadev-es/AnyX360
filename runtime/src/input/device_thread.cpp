// The Input thread: raw mouse input (motion and wheel) through a message-only window, and a cache
// of the Xbox controllers' state so the game's threads never call XInput (which is slow, above all
// for empty slots).
#include <atomic>
#include <mutex>
#include <thread>

#include "input_internal.h"
#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

namespace x360::input::detail {

namespace {

// Cumulative mouse motion (while the cursor is captured) with timestamps.
struct MouseSample {
  int64_t time = 0;
  int64_t x = 0, y = 0;
};
constexpr size_t kMouseSamples = 1024;
std::mutex g_mouse_mutex;
MouseSample g_mouse[kMouseSamples];
uint64_t g_mouse_count = 0;
std::atomic<int64_t> g_wheel_up{0}, g_wheel_down{0};

std::mutex g_pad_mutex;
Pad g_pads[4];
std::atomic<uint32_t> g_connected{0};

void AddMouseMotion(int64_t time, int32_t dx, int32_t dy) {
  std::lock_guard lock(g_mouse_mutex);
  MouseSample last = g_mouse_count ? g_mouse[(g_mouse_count - 1) % kMouseSamples] : MouseSample{};
  g_mouse[g_mouse_count % kMouseSamples] = {time, last.x + dx, last.y + dy};
  ++g_mouse_count;
}

void HandleRawInput(HRAWINPUT handle) {
  alignas(8) uint8_t buffer[sizeof(RAWINPUT) + 64];
  UINT size = sizeof(buffer);
  if (GetRawInputData(handle, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
  const RAWINPUT& raw = *reinterpret_cast<const RAWINPUT*>(buffer);
  if (raw.header.dwType != RIM_TYPEMOUSE || !Captured()) return;
  const RAWMOUSE& mouse = raw.data.mouse;
  int64_t now = QpcNow();
  // Absolute devices (remote desktop, pens, some tablets) can't drive a stick.
  if (!(mouse.usFlags & MOUSE_MOVE_ABSOLUTE) && (mouse.lLastX || mouse.lLastY))
    AddMouseMotion(now, mouse.lLastX, mouse.lLastY);
  if (mouse.usButtonFlags & RI_MOUSE_WHEEL) {
    SHORT delta = SHORT(mouse.usButtonData);
    (delta > 0 ? g_wheel_up : g_wheel_down) = now;
  }
}

void PollControllers(int64_t now, int64_t* next_probe) {
  uint32_t connected = g_connected.load();
  uint32_t new_connected = connected;
  for (DWORD i = 0; i < 4; ++i) {
    bool was = connected & (1u << i);
    if (!was && now < next_probe[i]) continue;  // empty slots: once a second
    XINPUT_STATE state{};
    bool is = XInputGetState(i, &state) == ERROR_SUCCESS;
    if (is) {
      const XINPUT_GAMEPAD& g = state.Gamepad;
      Pad pad{g.wButtons, g.bLeftTrigger, g.bRightTrigger, g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY};
      std::lock_guard lock(g_pad_mutex);
      g_pads[i] = pad;
      new_connected |= 1u << i;
    } else {
      next_probe[i] = now + QpcFrequency();
      new_connected &= ~(1u << i);
      if (was) {
        std::lock_guard lock(g_pad_mutex);
        g_pads[i] = {};
      }
    }
  }
  if (new_connected != connected) {
    g_connected = new_connected;
    for (int i = 0; i < 4; ++i)
      if ((new_connected ^ connected) & (1u << i))
        KLOG_I("input: controller %d %s", i + 1, (new_connected & (1u << i)) ? "connected" : "disconnected");
    NotifyDevicesChanged();
  }
}

void DeviceThreadBody() {
  SetThreadDescription(GetCurrentThread(), L"Input");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"AnyX360RawInput";
  RegisterClassExW(&wc);
  HWND sink = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
  // Generic desktop mouse, delivered even while another window has the focus (only counted while
  // the cursor is captured). Legacy mouse messages still reach the game window.
  RAWINPUTDEVICE device{0x01, 0x02, RIDEV_INPUTSINK, sink};
  if (!sink || !RegisterRawInputDevices(&device, 1, sizeof(device)))
    KLOG_W("input: raw mouse input unavailable (%lu); the mouse won't move the camera", GetLastError());

  int64_t next_probe[4] = {};
  const int64_t poll_period = QpcFrequency() / 250;  // 4 ms
  int64_t next_poll = 0;
  for (;;) {
    MsgWaitForMultipleObjectsEx(0, nullptr, 4, QS_RAWINPUT | QS_POSTMESSAGE | QS_SENDMESSAGE,
                                MWMO_INPUTAVAILABLE);
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_INPUT) HandleRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
      DispatchMessageW(&msg);  // DefWindowProc cleans up WM_INPUT
    }
    int64_t now = QpcNow();
    if (now >= next_poll) {
      PollControllers(now, next_probe);
      next_poll = now + poll_period;
    }
  }
}

}  // namespace

int64_t QpcNow() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

int64_t QpcFrequency() {
  static const int64_t frequency = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  return frequency;
}

void StartDeviceThread() {
  // Report the controllers connected at startup before the title polls them.
  int64_t next_probe[4] = {};
  PollControllers(QpcNow(), next_probe);
  std::thread(DeviceThreadBody).detach();
}

bool ControllerState(int index, Pad& pad) {
  if (index < 0 || index >= 4 || !(g_connected.load() & (1u << index))) return false;
  std::lock_guard lock(g_pad_mutex);
  pad = g_pads[index];
  return true;
}

uint32_t ControllerMask() { return g_connected.load(); }

void MouseVelocity(int64_t window, double& vx, double& vy) {
  vx = vy = 0;
  int64_t now = QpcNow();
  std::lock_guard lock(g_mouse_mutex);
  if (!g_mouse_count) return;
  const MouseSample& newest = g_mouse[(g_mouse_count - 1) % kMouseSamples];
  if (newest.time <= now - window) return;  // no motion within the window
  // Cumulative motion at the start of the window: the newest sample at or before it.
  MouseSample base{};
  uint64_t available = std::min<uint64_t>(g_mouse_count, kMouseSamples);
  bool found = false;
  for (uint64_t i = 1; i <= available; ++i) {
    const MouseSample& s = g_mouse[(g_mouse_count - i) % kMouseSamples];
    if (s.time <= now - window) {
      base = s;
      found = true;
      break;
    }
  }
  if (!found && g_mouse_count > kMouseSamples) base = g_mouse[g_mouse_count % kMouseSamples];  // oldest kept
  double seconds = double(window) / double(QpcFrequency());
  vx = double(newest.x - base.x) / seconds;
  vy = double(newest.y - base.y) / seconds;
}

int64_t LastWheel(bool up) { return (up ? g_wheel_up : g_wheel_down).load(); }

}  // namespace x360::input::detail
