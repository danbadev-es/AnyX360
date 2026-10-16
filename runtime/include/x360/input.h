// Local players' controllers: each of the four player slots is driven by the keyboard and mouse,
// an Xbox controller (XInput) or nothing. The keyboard and mouse become a virtual Xbox 360 pad
// (mouse movement is the right stick); the bindings and mouse settings come from settings.toml.
//
// Threads: an "Input" thread receives raw mouse input and polls the controllers; the game's threads
// read cached state (GetPad never calls XInput and never waits for the UI thread); the window hook
// (cursor capture, Alt+Enter) runs on the UI thread.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace x360::input {

enum class DeviceKind : uint8_t { kNone, kKeyboardMouse, kXInput };

struct Device {
  DeviceKind kind = DeviceKind::kNone;
  int8_t xinput = -1;  // controller 0-3, or -1: the first connected controller no other player uses
  bool operator==(const Device&) const = default;
};
// "keyboard_mouse", "pad", "pad1".."pad4", "none" (settings.toml names).
Device ParseDevice(const std::string& name);
std::string DeviceName(Device device);

// XINPUT_GAMEPAD in host byte order.
struct Pad {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0, right_trigger = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
  bool operator==(const Pad&) const = default;
};

// Hooks the game window and starts the Input thread. UI thread, once the window is open.
// `toggle_fullscreen` runs on Alt+Enter.
void Start(void* hwnd, std::function<void()> toggle_fullscreen);

// The device of a player slot (0-3); from the settings, changed by the panel.
void SetSlotDevice(uint32_t user, Device device);
Device GetSlotDevice(uint32_t user);
// The controller a slot currently uses (-1: none or keyboard).
int ResolvedController(uint32_t user);
// Whether the slot's device is present (the keyboard always is).
bool IsConnected(uint32_t user);
// Connected controllers, bit i = controller i.
uint32_t ConnectedControllers();

// A player's controller state. Returns false if the player has no connected device. Any thread.
bool GetPad(uint32_t user, Pad& pad);
void SetVibration(uint32_t user, uint16_t left, uint16_t right);

// Off while the settings panel is open: the keyboard and mouse player's pad is neutral and the
// cursor is free. When input is re-enabled, keys still held are ignored until released. UI thread.
void SetGameInputEnabled(bool enabled);
bool IsCursorCaptured();

// The keyboard and mouse bindings in effect: {control, keys}.
std::vector<std::pair<std::string, std::string>> DescribeBindings();

// Called (on the Input thread) when a controller connects or disconnects.
void OnDevicesChanged(std::function<void()> callback);

}  // namespace x360::input
