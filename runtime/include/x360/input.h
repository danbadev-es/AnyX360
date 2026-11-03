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
#include <map>
#include <string>
#include <string_view>
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

// XINPUT_GAMEPAD in host byte order. The Guide (Xbox) button is kGuideButton in `buttons` (read with
// XInputGetStateEx): system UIs see it (GetUiPad), the game never does (GetPad).
constexpr uint16_t kGuideButton = 0x0400;
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

// A player's controller state, without the Guide button. Returns false if the player has no
// connected device. Neutral (but connected) while a system UI is open (SetSystemUiOpen). Any thread.
bool GetPad(uint32_t user, Pad& pad);
// A system UI over the game (the sign-in picker) takes the controllers: the game reads neutral pads
// while it is open, the UI reads them with GetUiPad (the scripted input too). Any thread.
void SetSystemUiOpen(bool open);
bool GetUiPad(uint32_t user, Pad& pad);
void SetVibration(uint32_t user, uint16_t left, uint16_t right);

// Off while the settings panel is open: the keyboard and mouse player's pad is neutral and the
// cursor is free. When input is re-enabled, keys still held are ignored until released. UI thread.
void SetGameInputEnabled(bool enabled);
bool IsCursorCaptured();

// The mouse over the game window, for a virtual Kinect hand: the cursor in the client area (0-1,
// top left 0,0), the buttons and the Space key. False while the window isn't in the foreground or
// game input is off (settings panel). Any thread.
struct MouseHand {
  float x = 0.5f, y = 0.5f;
  bool left = false, right = false, space = false;
};
bool GetMouseHand(MouseHand& hand);

// The keyboard and mouse bindings in effect: {control, keys}.
std::vector<std::pair<std::string, std::string>> DescribeBindings();
// The same with those overrides of the default bindings (settings.toml's [input.bindings]).
std::vector<std::pair<std::string, std::string>> DescribeBindings(const std::map<std::string, std::string>& overrides);

// Rebinding, for the settings panel. The key or mouse button held now, or a wheel notch, by the name
// bindings use ("E", "MOUSE4", "WHEELUP"); empty if none or the window isn't in the foreground.
std::string PressedKey();
// What to show for a key's name: readable mouse buttons and special keys, what the keyboard layout in
// use prints for a punctuation key (GRAVE is the N with a tilde on a Spanish one), else the name.
std::string KeyLabel(std::string_view key);
// F1, F2, F3, F11 and Home belong to the window: no control can have them.
bool IsReservedKey(std::string_view key);
// The overrides with `key` on `control`: its only key, or (`add`) one more. A key has one control: it
// leaves any other that had it, which `moved_from` lists.
std::map<std::string, std::string> BindKey(const std::map<std::string, std::string>& overrides, std::string_view control,
                                           std::string_view key, bool add, std::vector<std::string>* moved_from = nullptr);

// Called (on the Input thread) when a controller connects or disconnects.
void OnDevicesChanged(std::function<void()> callback);

}  // namespace x360::input
