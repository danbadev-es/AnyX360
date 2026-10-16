// Shared between the input module's files.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "x360/input.h"

namespace x360::input::detail {

// Key codes are Windows virtual keys plus two pseudo keys for the mouse wheel.
constexpr uint16_t kWheelUp = 0x100, kWheelDown = 0x101, kKeyCount = 0x102;

enum class Target : uint8_t { kButton, kLeftTrigger, kRightTrigger, kStickUp, kStickDown, kStickLeft, kStickRight };

struct Binding {
  std::string control;  // "A", "LT", "LUP", ...
  Target target = Target::kButton;
  uint16_t button = 0;  // XINPUT_GAMEPAD_* for kButton
  std::vector<uint16_t> keys;
};

// The default bindings with `overrides` (control -> space-separated key names) applied.
std::vector<Binding> BuildBindings(const std::map<std::string, std::string>& overrides);
uint16_t KeyCode(std::string_view name);  // 0 if unknown
// The default bindings as text, for the panel: {control, keys}.
std::vector<std::pair<std::string, std::string>> DescribeBindings(const std::map<std::string, std::string>& overrides);

int64_t QpcNow();
int64_t QpcFrequency();

// Input thread (device_thread.cpp).
void StartDeviceThread();
bool ControllerState(int index, Pad& pad);  // cached state; false if disconnected
uint32_t ControllerMask();
// Mouse speed in counts per second over the last `window` QPC ticks (movement while captured).
void MouseVelocity(int64_t window, double& vx, double& vy);
int64_t LastWheel(bool up);  // QPC time of the last wheel notch (0: never)
void NotifyDevicesChanged();

// Window hook (window_hook.cpp).
void InstallWindowHook(void* hwnd, std::function<void()> toggle_fullscreen);
bool Captured();
bool GameWindowActive();
void ReleaseCursor();
void TryCaptureCursor();
bool KeyboardMouseInUse();  // some slot uses the keyboard and mouse (input.cpp)
bool GameInputEnabled();    // input.cpp
void LatchHeldKeys();       // input.cpp: keys held now are ignored until released

}  // namespace x360::input::detail
