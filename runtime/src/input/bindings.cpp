// Keyboard and mouse bindings of the virtual controller (Gears of War style defaults).
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#include "input_internal.h"
#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

namespace x360::input::detail {

namespace {

struct Control {
  const char* name;
  Target target;
  uint16_t button;
  const char* keys;  // default bindings
};

// Movement on WASD, camera on the mouse, aim (LT) and fire (RT) on the mouse buttons; A (cover,
// roadie run, confirm) on Space/Shift/Enter, B (melee, back) on V/Backspace, RB (reload) on R,
// X (use, pick up, revive) on E, LB (Tac-Com) on Q, weapon slots (D-pad) on the arrows, 1-4 and the
// wheel.
const Control kControls[] = {
    {"LUP", Target::kStickUp, 0, "W"},
    {"LDOWN", Target::kStickDown, 0, "S"},
    {"LLEFT", Target::kStickLeft, 0, "A"},
    {"LRIGHT", Target::kStickRight, 0, "D"},
    {"A", Target::kButton, XINPUT_GAMEPAD_A, "SPACE LSHIFT RETURN"},
    {"B", Target::kButton, XINPUT_GAMEPAD_B, "V BACK"},
    {"X", Target::kButton, XINPUT_GAMEPAD_X, "E"},
    {"Y", Target::kButton, XINPUT_GAMEPAD_Y, "F"},
    {"LB", Target::kButton, XINPUT_GAMEPAD_LEFT_SHOULDER, "Q"},
    {"RB", Target::kButton, XINPUT_GAMEPAD_RIGHT_SHOULDER, "R"},
    {"LT", Target::kLeftTrigger, 0, "MOUSE2"},
    {"RT", Target::kRightTrigger, 0, "MOUSE1"},
    {"LS", Target::kButton, XINPUT_GAMEPAD_LEFT_THUMB, "C"},
    {"RS", Target::kButton, XINPUT_GAMEPAD_RIGHT_THUMB, "MOUSE3"},
    {"BACK", Target::kButton, XINPUT_GAMEPAD_BACK, "TAB"},
    {"START", Target::kButton, XINPUT_GAMEPAD_START, "ESCAPE"},
    {"UP", Target::kButton, XINPUT_GAMEPAD_DPAD_UP, "UP 4"},
    {"DOWN", Target::kButton, XINPUT_GAMEPAD_DPAD_DOWN, "DOWN 3"},
    {"LEFT", Target::kButton, XINPUT_GAMEPAD_DPAD_LEFT, "LEFT 1 WHEELDOWN"},
    {"RIGHT", Target::kButton, XINPUT_GAMEPAD_DPAD_RIGHT, "RIGHT 2 WHEELUP"},
};

const std::pair<const char*, uint16_t> kNamedKeys[] = {
    {"SPACE", VK_SPACE},     {"RETURN", VK_RETURN},   {"ENTER", VK_RETURN},     {"ESCAPE", VK_ESCAPE},
    {"ESC", VK_ESCAPE},      {"TAB", VK_TAB},         {"BACK", VK_BACK},        {"BACKSPACE", VK_BACK},
    {"LSHIFT", VK_LSHIFT},   {"RSHIFT", VK_RSHIFT},   {"LCTRL", VK_LCONTROL},   {"RCTRL", VK_RCONTROL},
    {"LALT", VK_LMENU},      {"RALT", VK_RMENU},      {"UP", VK_UP},            {"DOWN", VK_DOWN},
    {"LEFT", VK_LEFT},       {"RIGHT", VK_RIGHT},     {"CAPSLOCK", VK_CAPITAL}, {"INSERT", VK_INSERT},
    {"DELETE", VK_DELETE},   {"HOME", VK_HOME},       {"END", VK_END},          {"PAGEUP", VK_PRIOR},
    {"PAGEDOWN", VK_NEXT},   {"MOUSE1", VK_LBUTTON},  {"MOUSE2", VK_RBUTTON},   {"MOUSE3", VK_MBUTTON},
    {"MOUSE4", VK_XBUTTON1}, {"MOUSE5", VK_XBUTTON2}, {"WHEELUP", kWheelUp},    {"WHEELDOWN", kWheelDown},
    {"NUMPAD0", VK_NUMPAD0}, {"NUMPAD1", VK_NUMPAD1}, {"NUMPAD2", VK_NUMPAD2},  {"NUMPAD3", VK_NUMPAD3},
    {"NUMPAD4", VK_NUMPAD4}, {"NUMPAD5", VK_NUMPAD5}, {"NUMPAD6", VK_NUMPAD6},  {"NUMPAD7", VK_NUMPAD7},
    {"NUMPAD8", VK_NUMPAD8}, {"NUMPAD9", VK_NUMPAD9}, {"NUMPADADD", VK_ADD},    {"NUMPADSUB", VK_SUBTRACT},
    {"NUMPADMUL", VK_MULTIPLY}, {"NUMPADDIV", VK_DIVIDE}, {"NUMPADDEC", VK_DECIMAL},
    // Named as on a US keyboard; the panel shows what the layout in use prints (KeyLabel).
    {"SEMICOLON", VK_OEM_1}, {"PLUS", VK_OEM_PLUS},   {"COMMA", VK_OEM_COMMA},  {"MINUS", VK_OEM_MINUS},
    {"PERIOD", VK_OEM_PERIOD}, {"SLASH", VK_OEM_2},   {"GRAVE", VK_OEM_3},      {"LBRACKET", VK_OEM_4},
    {"BACKSLASH", VK_OEM_5}, {"RBRACKET", VK_OEM_6},  {"QUOTE", VK_OEM_7},      {"OEM8", VK_OEM_8},
    {"OEM102", VK_OEM_102},
};

// The window's own keys (F1 overlay, F2 panel, F3 Kinect window, F11 fullscreen, Home Guide).
const char* const kReservedKeys[] = {"F1", "F2", "F3", "F11", "HOME"};

}  // namespace

std::vector<std::string> Split(const std::string& text) {
  std::istringstream in(text);
  std::vector<std::string> out;
  for (std::string word; in >> word;) out.push_back(word);
  return out;
}

uint16_t KeyCode(std::string_view name) {
  std::string upper(name);
  for (char& c : upper) c = char(std::toupper(uint8_t(c)));
  if (upper.size() == 1 && (std::isalnum(uint8_t(upper[0])))) return uint16_t(upper[0]);  // VK_A.., VK_0..
  if (upper.size() >= 2 && upper.size() <= 3 && upper[0] == 'F') {
    int n = std::atoi(upper.c_str() + 1);
    if (n >= 1 && n <= 12) return uint16_t(VK_F1 + n - 1);
  }
  for (const auto& [key_name, code] : kNamedKeys)
    if (upper == key_name) return code;
  return 0;
}

std::string KeyName(uint16_t code) {
  if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')) return std::string(1, char(code));
  if (code >= VK_F1 && code <= VK_F12) return "F" + std::to_string(code - VK_F1 + 1);
  for (const auto& [key_name, key_code] : kNamedKeys)
    if (code == key_code) return key_name;
  return {};
}

std::vector<uint16_t> AllKeyCodes() {
  std::vector<uint16_t> out;
  for (uint16_t c = 'A'; c <= 'Z'; ++c) out.push_back(c);
  for (uint16_t c = '0'; c <= '9'; ++c) out.push_back(c);
  for (uint16_t c = VK_F1; c <= VK_F12; ++c) out.push_back(c);
  for (const auto& [key_name, code] : kNamedKeys)
    if (std::find(out.begin(), out.end(), code) == out.end()) out.push_back(code);
  return out;
}

bool IsReservedCode(uint16_t code) {
  for (const char* reserved : kReservedKeys)
    if (code && code == KeyCode(reserved)) return true;
  return false;
}

std::vector<Binding> BuildBindings(const std::map<std::string, std::string>& overrides) {
  std::vector<Binding> out;
  for (const Control& c : kControls) {
    Binding b{c.name, c.target, c.button, {}};
    auto it = overrides.find(c.name);
    for (const std::string& name : Split(it != overrides.end() ? it->second : c.keys)) {
      if (uint16_t code = KeyCode(name))
        b.keys.push_back(code);
      else
        KLOG_W("input: unknown key \"%s\" bound to %s", name.c_str(), c.name);
    }
    out.push_back(std::move(b));
  }
  for (const auto& [control, keys] : overrides) {
    bool known = false;
    for (const Control& c : kControls) known |= control == c.name;
    if (!known) KLOG_W("input: unknown control \"%s\" in the bindings", control.c_str());
  }
  return out;
}

std::vector<std::pair<std::string, std::string>> DescribeBindings(const std::map<std::string, std::string>& overrides) {
  std::vector<std::pair<std::string, std::string>> out;
  for (const Control& c : kControls) {
    auto it = overrides.find(c.name);
    out.push_back({c.name, it != overrides.end() ? it->second : c.keys});
  }
  return out;
}

}  // namespace x360::input::detail

namespace x360::input {

std::vector<std::pair<std::string, std::string>> DescribeBindings(const std::map<std::string, std::string>& overrides) {
  return detail::DescribeBindings(overrides);
}

bool IsReservedKey(std::string_view key) { return detail::IsReservedCode(detail::KeyCode(key)); }

std::map<std::string, std::string> BindKey(const std::map<std::string, std::string>& overrides, std::string_view control,
                                           std::string_view key, bool add, std::vector<std::string>* moved_from) {
  std::map<std::string, std::string> out = overrides;
  const uint16_t code = detail::KeyCode(key);
  if (!code) return out;
  for (const auto& [name, keys] : detail::DescribeBindings(overrides)) {
    std::string kept;
    bool had = false;
    for (const std::string& word : detail::Split(keys)) {
      if (detail::KeyCode(word) == code) {
        had = true;
        continue;
      }
      kept += (kept.empty() ? "" : " ") + word;
    }
    if (name == control) {
      // Last of its keys, or the only one.
      out[name] = add && !kept.empty() ? kept + " " + detail::KeyName(code) : detail::KeyName(code);
    } else if (had) {
      out[name] = kept;
      if (moved_from) moved_from->push_back(name);
    }
  }
  return out;
}

}  // namespace x360::input
