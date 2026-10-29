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
  float sensitivity = 1.5f;      // mouse to right stick
  float aim_sensitivity = 1.0f;  // multiplier while aiming (left trigger held); the games slow aiming already
  bool invert_y = false;
  uint32_t smoothing_ms = 16;    // mouse speed is measured over this window
  float anti_deadzone = 0.25f;   // added to any mouse movement so the game's stick dead zone doesn't eat it
  // The stick's deflection grows as the mouse's speed to this power: below 1 slow movements deflect
  // the stick more, against the titles' own stick curves, which turn small deflections very slowly.
  float response = 0.6f;
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
  // Run the title's vblank in step with the display's refresh when the rates are compatible
  // (e.g. 120 Hz guest vblank on a 60 or 120 Hz screen), so every frame stays on screen equally long.
  bool display_lock = true;
  bool thread_priorities = true;  // guest priorities mapped to Windows thread priorities
  bool mmcss = false;             // MMCSS "Games" class for the vblank and audio threads
};

// Kinect titles: the virtual sensor (x360/nui.h). Only games that use it write this section.
struct KinectSettings {
  bool enabled = false;  // set by the game's defaults, not saved
  // Where the players' skeletons come from: "webcam" (the pose bridge), "mouse" (the right hand
  // follows the mouse), "synthetic" (scripted test poses, ANYX360_NUI_SCRIPT) or "none".
  std::string source = "webcam";
  uint32_t camera = 0;         // webcam index
  bool mirror = false;         // the webcam already mirrors its image
  float fov = 60.0f;           // the webcam's horizontal field of view in degrees
  float smoothing = 0.5f;      // 0 raw, 1 heavy
  uint32_t players = 2;        // bodies tracked at once (1-2)
  float camera_height = 0.8f;  // the virtual sensor's height above the floor, in metres
  std::string model = "full";  // pose model: lite, full, heavy
  // The webcam's tilt: the pitch measured by the pose bridge, or set here; the roll always set here
  // (degrees; pitch > 0 looking down, roll > 0 turned clockwise as seen from behind it).
  bool tilt_manual = true;  // 3: by hand at 0 (a laptop's camera turned to face the player)
  float tilt_pitch = 0.0f;
  float tilt_roll = 0.0f;
  // Where the game sees the player: where they are (false: the virtual sensor stands where the webcam
  // is, so its picture is the webcam's own), or moved to a Kinect's spot, centred 2.3 m away (true).
  bool centre_player = false;
  bool operator==(const KinectSettings&) const = default;
};

struct Settings {
  // The file's format. 2: the mouse's response curve and its new defaults (a version 1 file still
  // at the old defaults takes the new ones). 3: the webcam's tilt set by hand at 0 by default (a
  // version 2 file measuring it takes the new default).
  int version = 3;
  gpu::DisplayConfig display;
  bool show_stats = false;
  // The Kinect debug window (F3): open, and where it was ("left,top,right,bottom,maximized" in
  // screen pixels; empty: not placed yet).
  bool kinect_window = false;
  std::string kinect_window_placement;
  InputSettings input;
  KinectSettings kinect;
  PlayerSettings players[4] = {{"keyboard_mouse", "Player"}, {"pad", "Player 2"}, {"none", "Player 3"},
                               {"none", "Player 4"}};
  // When local players sign in: "on_demand" (when the title asks, each choosing a profile in the
  // sign-in picker; at once in scripted runs), "automatic" (when the title asks, at once, as their
  // slot's profile), "at_start" (every player with a device, at boot) or "manual" (from the panel
  // only).
  std::string sign_in = "on_demand";
  PacingSettings pacing;
  // [xenia]: options of Xenia's GPU code by name, each value as TOML text (half_pixel_offset = false):
  // advanced, for trying a fix on one GPU without rebuilding. Applied when the game starts.
  std::map<std::string, std::string> xenia;
};

// The game's defaults (values missing from the file). Call before Load; without it the defaults are
// Settings{}.
void SetDefaults(const Settings& defaults);
// The game's defaults (F2's resets).
Settings Defaults();
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
