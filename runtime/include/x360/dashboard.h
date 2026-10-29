// The dashboard (the launcher's console mode) and a game, as one window to the player
// (docs/DASHBOARD.md). Shared by the runtime and the launcher.
//
// The launcher starts a game with
//   --dashboard <launcher process ID>  the events below exist; the game window stays hidden (cloaked)
//                                      until the first frame is shown, then `ready` is set
//   --window <x,y,w,h>                 the window borderless at that rectangle (screen pixels): the
//                                      dashboard's own
//   --fullscreen                       or borderless over the whole monitor of that rectangle
// The game sets `leaving` before it ends on its own (Xbox Home in the Guide, its window closed, a
// switch to another of its executables) and waits briefly for `shown`: the launcher shows the
// dashboard first, so nothing else flashes between the two. It exits with kExitToDashboard after
// Xbox Home.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace x360::dashboard {

// The exit code of a game left with the Guide's Xbox Home: the dashboard comes back at once.
constexpr uint32_t kExitToDashboard = 0x00DB0001;

struct WindowRect {
  int x = 0, y = 0, w = 0, h = 0;
  bool operator==(const WindowRect&) const = default;
};

// "x,y,w,h" (integers, x and y may be negative; w and h at least 64). None if it isn't one.
inline std::optional<WindowRect> ParseWindowArg(std::string_view text) {
  WindowRect r;
  int* fields[] = {&r.x, &r.y, &r.w, &r.h};
  size_t at = 0;
  for (int i = 0; i < 4; ++i) {
    const size_t end = i < 3 ? text.find(',', at) : text.size();
    if (end == std::string_view::npos || end == at) return std::nullopt;
    std::string_view field = text.substr(at, end - at);
    bool negative = false;
    if (field.front() == '-') negative = true, field.remove_prefix(1);
    if (field.empty() || field.size() > 6) return std::nullopt;
    int value = 0;
    for (char c : field) {
      if (c < '0' || c > '9') return std::nullopt;
      value = value * 10 + (c - '0');
    }
    *fields[i] = negative ? -value : value;
    at = end + 1;
  }
  if (r.w < 64 || r.h < 64) return std::nullopt;
  return r;
}

inline std::string FormatWindowArg(const WindowRect& r) {
  return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," + std::to_string(r.h);
}

// The events' names for a launcher process: "ready", "leaving" and "shown" (auto-reset).
inline std::wstring EventName(uint32_t launcher_pid, const wchar_t* which) {
  return L"Local\\AnyX360.Dashboard." + std::to_wstring(launcher_pid) + L"." + which;
}

}  // namespace x360::dashboard
