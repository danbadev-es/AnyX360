// Player slots and the keyboard and mouse virtual controller.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "input_internal.h"
#include "x360/kernel.h"
#include "x360/settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

namespace x360::input {

using namespace detail;

namespace {

struct Config {
  settings::InputSettings input;
  std::vector<Binding> bindings;
};

std::mutex g_mutex;  // slots, config, callbacks
Device g_slots[4] = {{DeviceKind::kKeyboardMouse, -1}, {DeviceKind::kXInput, -1}, {}, {}};
std::shared_ptr<const Config> g_config;
std::vector<std::function<void()>> g_device_callbacks;
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_latched[kKeyCount];
const bool g_trace = std::getenv("ANYX360_INPUT_TRACE") != nullptr;

// The controller a slot uses: its own, or for "first free controller" slots, the lowest connected
// controller not assigned to another slot or taken by an earlier such slot. g_mutex held.
int ResolveLocked(uint32_t user, uint32_t connected) {
  const Device& device = g_slots[user];
  if (device.kind != DeviceKind::kXInput) return -1;
  if (device.xinput >= 0) return (connected & (1u << device.xinput)) ? device.xinput : -1;
  uint32_t taken = 0;
  for (const Device& d : g_slots)
    if (d.kind == DeviceKind::kXInput && d.xinput >= 0) taken |= 1u << d.xinput;
  for (uint32_t u = 0; u < 4; ++u) {
    if (g_slots[u].kind != DeviceKind::kXInput || g_slots[u].xinput >= 0) continue;
    int pick = -1;
    for (int i = 0; i < 4 && pick < 0; ++i)
      if ((connected & (1u << i)) && !(taken & (1u << i))) pick = i;
    if (u == user) return pick;
    if (pick >= 0) taken |= 1u << pick;
  }
  return -1;
}

// ---- scripted input ---------------------------------------------------------------------------
// ANYX360_INPUT="<seconds>:<button>[/<hold>][@<user>],..." presses each button for 120 ms (or <hold>
// seconds) at the given time after the first input poll, on player <user> (default 0). Buttons:
// START BACK A B X Y UP DOWN LEFT RIGHT LB RB LT RT, left stick LUP LDOWN LLEFT LRIGHT.

struct Press {
  double at = 0;
  std::string button;
  double hold = 0.12;
  uint32_t user = 0;
};

const std::vector<Press>& Script() {
  static const std::vector<Press> script = [] {
    std::vector<Press> out;
    const char* env = std::getenv("ANYX360_INPUT");
    if (!env) return out;
    std::string spec = env;
    for (size_t pos = 0; pos < spec.size();) {
      size_t end = std::min(spec.find(',', pos), spec.size());
      std::string item = spec.substr(pos, end - pos);
      pos = end + 1;
      size_t colon = item.find(':');
      if (colon == std::string::npos) continue;
      Press press;
      press.at = std::atof(item.c_str());
      press.button = item.substr(colon + 1);
      if (size_t at = press.button.find('@'); at != std::string::npos) {
        press.user = uint32_t(std::atoi(press.button.c_str() + at + 1)) & 3;
        press.button.resize(at);
      }
      if (size_t slash = press.button.find('/'); slash != std::string::npos) {
        press.hold = std::atof(press.button.c_str() + slash + 1);
        press.button.resize(slash);
      }
      out.push_back(press);
    }
    return out;
  }();
  return script;
}

bool ScriptUsesUser(uint32_t user) {
  for (const Press& p : Script())
    if (p.user == user) return true;
  return false;
}

void ApplyScript(uint32_t user, Pad& pad) {
  static const auto start = std::chrono::steady_clock::now();
  double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  static const std::pair<const char*, uint16_t> names[] = {
      {"START", XINPUT_GAMEPAD_START},      {"BACK", XINPUT_GAMEPAD_BACK},
      {"A", XINPUT_GAMEPAD_A},              {"B", XINPUT_GAMEPAD_B},
      {"X", XINPUT_GAMEPAD_X},              {"Y", XINPUT_GAMEPAD_Y},
      {"UP", XINPUT_GAMEPAD_DPAD_UP},       {"DOWN", XINPUT_GAMEPAD_DPAD_DOWN},
      {"LEFT", XINPUT_GAMEPAD_DPAD_LEFT},   {"RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
      {"LB", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER}};
  uint16_t buttons = 0;
  for (const Press& p : Script()) {
    if (p.user != user || now < p.at || now >= p.at + p.hold) continue;
    if (p.button == "LT") pad.left_trigger = 255;
    if (p.button == "RT") pad.right_trigger = 255;
    if (p.button == "LUP") pad.ly = 32767;
    if (p.button == "LDOWN") pad.ly = -32768;
    if (p.button == "LLEFT") pad.lx = -32768;
    if (p.button == "LRIGHT") pad.lx = 32767;
    for (auto& [name, bit] : names)
      if (p.button == name) buttons |= bit;
  }
  static std::atomic<uint16_t> last[4];
  if (buttons != last[user].exchange(buttons) && buttons) KLOG_I("input: scripted buttons %04X (player %u)", buttons, user + 1);
  pad.buttons |= buttons;
}

// ---- keyboard and mouse -----------------------------------------------------------------------

bool RawKeyDown(uint16_t key, int64_t now) {
  if (key == kWheelUp || key == kWheelDown) {
    int64_t t = LastWheel(key == kWheelUp);
    return t && now - t < QpcFrequency() * 80 / 1000;  // a wheel notch is an 80 ms press
  }
  return (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool KeyDown(uint16_t key, int64_t now) {
  bool down = RawKeyDown(key, now);
  if (g_latched[key].load(std::memory_order_relaxed)) {
    if (down) return false;
    g_latched[key].store(false, std::memory_order_relaxed);
  }
  return down;
}

bool OurWindowInForeground() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
}

int16_t Axis(double v) { return int16_t(std::clamp<long>(std::lround(v * 32767.0), -32767, 32767)); }

Pad KeyboardMousePad(const Config& c) {
  Pad pad;
  if (!g_enabled.load(std::memory_order_relaxed) || !OurWindowInForeground()) return pad;
  int64_t now = QpcNow();
  int up = 0, down = 0, left = 0, right = 0;
  for (const Binding& b : c.bindings) {
    bool pressed = false;
    for (uint16_t key : b.keys) pressed |= KeyDown(key, now);  // every key, so latches clear
    if (!pressed) continue;
    switch (b.target) {
      case Target::kButton: pad.buttons |= b.button; break;
      case Target::kLeftTrigger: pad.left_trigger = 255; break;
      case Target::kRightTrigger: pad.right_trigger = 255; break;
      case Target::kStickUp: up = 1; break;
      case Target::kStickDown: down = 1; break;
      case Target::kStickLeft: left = 1; break;
      case Target::kStickRight: right = 1; break;
    }
  }
  int x = right - left, y = up - down;
  double diagonal = (x && y) ? std::sqrt(0.5) : 1.0;
  pad.lx = Axis(x * diagonal);
  pad.ly = Axis(y * diagonal);

  // Mouse look: the mouse's speed over a short window is the right stick's deflection, so the
  // camera turns while the mouse moves whatever the rate the title polls at.
  if (Captured()) {
    double vx, vy;
    MouseVelocity(QpcFrequency() * c.input.smoothing_ms / 1000, vx, vy);
    double rx, ry;
    detail::MouseLook(vx, vy, pad.left_trigger, c.input, rx, ry);
    if (rx || ry) {
      pad.rx = Axis(rx);
      pad.ry = Axis(ry);
    }
  }
  return pad;
}

void Trace(uint32_t user, const Pad& pad, bool connected) {
  static std::mutex mutex;
  static Pad last[4];
  static bool last_connected[4];
  auto moved = [](int16_t a, int16_t b) { return std::abs(int(a) - int(b)) > 4096 || ((a == 0) != (b == 0)); };
  std::lock_guard lock(mutex);
  const Pad& l = last[user];
  if (connected == last_connected[user] && pad.buttons == l.buttons && pad.left_trigger == l.left_trigger &&
      pad.right_trigger == l.right_trigger && !moved(pad.lx, l.lx) && !moved(pad.ly, l.ly) && !moved(pad.rx, l.rx) &&
      !moved(pad.ry, l.ry))
    return;
  last[user] = pad;
  last_connected[user] = connected;
  KLOG_I("input: P%u %s buttons %04X LT %u RT %u L %d,%d R %d,%d", user + 1, connected ? "" : "(disconnected)",
         pad.buttons, pad.left_trigger, pad.right_trigger, pad.lx, pad.ly, pad.rx, pad.ry);
}

void ApplySettings(const settings::Settings& s) {
  auto config = std::make_shared<Config>();
  config->input = s.input;
  config->bindings = BuildBindings(s.input.bindings);
  bool keyboard_mouse;
  {
    std::lock_guard lock(g_mutex);
    for (int i = 0; i < 4; ++i) g_slots[i] = ParseDevice(s.players[i].device);
    g_config = std::move(config);
    keyboard_mouse = std::any_of(std::begin(g_slots), std::end(g_slots),
                                 [](const Device& d) { return d.kind == DeviceKind::kKeyboardMouse; });
  }
  if (!keyboard_mouse) ReleaseCursor();
}

}  // namespace

namespace detail {

void MouseLook(double vx, double vy, bool aiming, const settings::InputSettings& in, double& rx, double& ry) {
  rx = ry = 0;
  // At sensitivity 1, 4000 counts a second (about 12 cm/s at 800 DPI) is the whole stick.
  const double k = in.sensitivity * (aiming ? in.aim_sensitivity : 1.0) / 4000.0;
  const double sx = vx * k, sy = -vy * k * (in.invert_y ? -1.0 : 1.0);
  const double m = std::hypot(sx, sy);
  if (m <= 1e-6) return;
  // The response curve lifts slow movements; then a radial anti dead zone: any motion deflects the
  // stick past the title's own dead zone.
  const double curved = std::pow(std::min(m, 1.0), double(in.response));
  const double dz = in.anti_deadzone;
  const double deflection = std::min(1.0, dz + (1.0 - dz) * curved);
  rx = sx * deflection / m;
  ry = sy * deflection / m;
}

bool KeyboardMouseInUse() {
  std::lock_guard lock(g_mutex);
  return std::any_of(std::begin(g_slots), std::end(g_slots),
                     [](const Device& d) { return d.kind == DeviceKind::kKeyboardMouse; });
}

bool GameInputEnabled() { return g_enabled.load(); }

void LatchHeldKeys() {
  std::shared_ptr<const Config> config;
  {
    std::lock_guard lock(g_mutex);
    config = g_config;
  }
  if (!config) return;
  int64_t now = QpcNow();
  for (const Binding& b : config->bindings)
    for (uint16_t key : b.keys)
      if (RawKeyDown(key, now)) g_latched[key] = true;
}

void NotifyDevicesChanged() {
  std::vector<std::function<void()>> callbacks;
  {
    std::lock_guard lock(g_mutex);
    callbacks = g_device_callbacks;
  }
  for (auto& callback : callbacks) callback();
}

}  // namespace detail

Device ParseDevice(const std::string& name) {
  if (name == "keyboard_mouse" || name == "keyboard") return {DeviceKind::kKeyboardMouse, -1};
  if (name == "pad") return {DeviceKind::kXInput, -1};
  if (name.size() == 4 && name.rfind("pad", 0) == 0 && name[3] >= '1' && name[3] <= '4')
    return {DeviceKind::kXInput, int8_t(name[3] - '1')};
  return {};
}

std::string DeviceName(Device device) {
  switch (device.kind) {
    case DeviceKind::kKeyboardMouse: return "keyboard_mouse";
    case DeviceKind::kXInput: return device.xinput < 0 ? "pad" : "pad" + std::to_string(device.xinput + 1);
    default: return "none";
  }
}

void Start(void* hwnd, std::function<void()> toggle_fullscreen) {
  ApplySettings(settings::Get());
  settings::Subscribe(ApplySettings);
  StartDeviceThread();
  InstallWindowHook(hwnd, std::move(toggle_fullscreen));
  for (uint32_t i = 0; i < 4; ++i) {
    Device d = GetSlotDevice(i);
    if (d.kind != DeviceKind::kNone)
      KLOG_I("input: player %u: %s%s", i + 1, DeviceName(d).c_str(), IsConnected(i) ? "" : " (not connected)");
  }
}

void SetSlotDevice(uint32_t user, Device device) {
  if (user >= 4) return;
  std::string names[4];
  {
    std::lock_guard lock(g_mutex);
    Device previous = g_slots[user];
    // A device drives one player: the player that had it gets this player's previous device.
    if (device.kind == DeviceKind::kKeyboardMouse || (device.kind == DeviceKind::kXInput && device.xinput >= 0))
      for (Device& other : g_slots)
        if (&other != &g_slots[user] && other == device) other = previous;
    g_slots[user] = device;
    for (int i = 0; i < 4; ++i) names[i] = DeviceName(g_slots[i]);
  }
  KLOG_I("input: player %u -> %s", user + 1, names[user].c_str());
  settings::Update([&](settings::Settings& s) {
    for (int i = 0; i < 4; ++i) s.players[i].device = names[i];
  });
}

Device GetSlotDevice(uint32_t user) {
  std::lock_guard lock(g_mutex);
  return user < 4 ? g_slots[user] : Device{};
}

int ResolvedController(uint32_t user) {
  if (user >= 4) return -1;
  std::lock_guard lock(g_mutex);
  return ResolveLocked(user, ControllerMask());
}

bool IsConnected(uint32_t user) {
  if (user >= 4) return false;
  if (ScriptUsesUser(user)) return true;
  Device d = GetSlotDevice(user);
  return d.kind == DeviceKind::kKeyboardMouse || (d.kind == DeviceKind::kXInput && ResolvedController(user) >= 0);
}

uint32_t ConnectedControllers() { return ControllerMask(); }

bool GetPad(uint32_t user, Pad& pad) {
  pad = {};
  if (user >= 4) return false;
  Device device;
  std::shared_ptr<const Config> config;
  int controller = -1;
  {
    std::lock_guard lock(g_mutex);
    device = g_slots[user];
    config = g_config;
    controller = ResolveLocked(user, ControllerMask());
  }
  bool connected = false;
  if (device.kind == DeviceKind::kKeyboardMouse) {
    connected = true;
    if (config) pad = KeyboardMousePad(*config);
  } else if (device.kind == DeviceKind::kXInput) {
    connected = ControllerState(controller, pad);
  }
  if (ScriptUsesUser(user)) {
    connected = true;
    ApplyScript(user, pad);
  }
  if (g_trace) Trace(user, pad, connected);
  return connected;
}

void SetVibration(uint32_t user, uint16_t left, uint16_t right) {
  int controller = ResolvedController(user);
  if (controller < 0) return;
  XINPUT_VIBRATION v{left, right};
  XInputSetState(DWORD(controller), &v);
}

void SetGameInputEnabled(bool enabled) {
  if (g_enabled.exchange(enabled) == enabled) return;
  if (enabled) {
    LatchHeldKeys();  // e.g. Esc or the click that closed the panel
    TryCaptureCursor();
  } else {
    ReleaseCursor();
  }
}

bool IsCursorCaptured() { return Captured(); }

std::vector<std::pair<std::string, std::string>> DescribeBindings() {
  return detail::DescribeBindings(settings::Get().input.bindings);
}

void OnDevicesChanged(std::function<void()> callback) {
  std::lock_guard lock(g_mutex);
  g_device_callbacks.push_back(std::move(callback));
}

}  // namespace x360::input
