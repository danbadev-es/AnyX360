// Persistent settings (<user root>\settings.toml): display, mouse and keyboard, players, pacing.
//
// Precedence: built-in defaults, then settings.toml, then command-line flags. Command-line values
// apply to the current session only (Override) and are written to the file only if the user changes
// that setting in the panel (Update applies the change to both the session and the saved values).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

#include "x360/gpu.h"

namespace x360::settings {

struct InputSettings {
  float sensitivity = 1.0f;     // mouse to right stick
  float aim_sensitivity = 0.6f;  // multiplier while aiming (left trigger held)
  bool invert_y = false;
  uint32_t smoothing_ms = 16;   // mouse speed is measured over this window
  float anti_deadzone = 0.25f;  // added to any mouse movement so the game's stick dead zone doesn't eat it
  // Overrides of the default bindings: pad control -> space-separated key names (see input.h).
  std::map<std::string, std::string> bindings;
};

struct PlayerSettings {
  // "keyboard_mouse", "pad" (first controller not used by another player), "pad1".."pad4", "none".
  std::string device;
  std::string name;
};

struct PacingSettings {
  uint32_t vblank_spin_us = 300;  // the vblank thread spins this long before each tick
  bool thread_priorities = true;  // guest priorities mapped to Windows thread priorities
  bool mmcss = false;             // MMCSS "Games" class for the vblank and audio threads
};

struct Settings {
  int version = 1;
  gpu::DisplayConfig display;
  bool show_stats = false;
  InputSettings input;
  PlayerSettings players[4] = {{"keyboard_mouse", "Player"}, {"pad", "Player 2"}, {"none", "Player 3"},
                               {"none", "Player 4"}};
  // When local players sign in: "on_demand" (when the title asks), "at_start" (every player with a
  // device, at boot) or "manual" (from the panel only).
  std::string sign_in = "on_demand";
  PacingSettings pacing;
};

// Reads the file (missing: defaults; unreadable: renamed to settings.toml.bad, defaults). Call once,
// before anything else uses the settings.
void Load(const std::filesystem::path& path);

// The session's settings (saved values plus command-line overrides).
Settings Get();
// The values in the file.
Settings GetSaved();

// Changes the session's settings without saving them (command-line flags).
void Override(const std::function<void(Settings&)>& change);
// Changes a setting for this session and in the file (saved after 500 ms, or by SaveNow), then
// notifies the listeners. Any thread.
void Update(const std::function<void(Settings&)>& change);
void SaveNow();

// Called after every Update with the new session settings, on the updating thread.
void Subscribe(std::function<void(const Settings&)> listener);

// Window size for a vertical resolution (720, 1080, 1440, 2160) and back.
void ResolutionSize(uint32_t lines, uint32_t& width, uint32_t& height);
uint32_t ResolutionLines(const gpu::DisplayConfig& display);

}  // namespace x360::settings
