#include "x360/settings.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "toml++/toml.h"
#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::settings {

namespace {

using Upscale = gpu::DisplayConfig::Upscale;

std::mutex g_mutex;
std::condition_variable g_save_cv;
std::filesystem::path g_path;
Settings g_defaults, g_session, g_saved;
bool g_dirty = false;
std::chrono::steady_clock::time_point g_save_at;
std::vector<std::function<void(const Settings&)>> g_listeners;

const char* UpscaleName(Upscale u) {
  return u == Upscale::kFsr ? "fsr" : u == Upscale::kCas ? "cas" : "bilinear";
}

Upscale ParseUpscale(std::string_view name, Upscale fallback) {
  if (name == "fsr") return Upscale::kFsr;
  if (name == "cas") return Upscale::kCas;
  if (name == "bilinear") return Upscale::kBilinear;
  return fallback;
}

template <typename T>
void Read(const toml::node_view<const toml::node>& node, T& value) {
  if (auto v = node.value<T>()) value = *v;
}

// Files from earlier versions: values still at that version's defaults take the current ones (the
// player didn't choose them); the others are kept.
void Migrate(Settings& s, const Settings& defaults) {
  if (s.version < 2) {
    if (s.input.sensitivity == 1.0f) s.input.sensitivity = defaults.input.sensitivity;
    if (s.input.aim_sensitivity == 0.6f) s.input.aim_sensitivity = defaults.input.aim_sensitivity;
  }
  // The tilt was measured by default; with a laptop's camera (level, the legs out of view) the
  // estimates were off by up to 20 degrees and turned the player and the picture.
  if (s.version < 3 && !s.kinect.tilt_manual) s.kinect.tilt_manual = defaults.kinect.tilt_manual;
  s.version = defaults.version;
}

void Parse(const toml::table& t, Settings& s) {
  s.version = 1;  // a file without a version is from the first one
  Read(t["version"], s.version);
  auto display = t["display"];
  uint32_t lines = ResolutionLines(s.display);
  Read(display["resolution"], lines);
  ResolutionSize(lines, s.display.width, s.display.height);
  Read(display["fullscreen"], s.display.fullscreen);
  Read(display["render_scale"], s.display.render_scale);
  s.display.render_scale = std::clamp(s.display.render_scale, 1u, 3u);
  if (auto v = display["upscale"].value<std::string>()) s.display.upscale = ParseUpscale(*v, s.display.upscale);
  Read(display["fxaa"], s.display.fxaa);
  Read(display["vsync"], s.display.vsync);
  Read(display["fps"], s.display.target_fps);
  s.display.target_fps = s.display.target_fps >= 60 ? 60 : 30;
  Read(display["show_stats"], s.show_stats);
  Read(display["achievement_toasts"], s.achievement_toasts);
  Read(display["achievement_sound"], s.achievement_sound);
  Read(display["guide_sound"], s.guide_sound);
  Read(display["system_notifications"], s.system_notifications);
  Read(display["kinect_window"], s.kinect_window);
  Read(display["kinect_window_placement"], s.kinect_window_placement);

  auto input = t["input"];
  Read(input["sensitivity"], s.input.sensitivity);
  Read(input["aim_sensitivity"], s.input.aim_sensitivity);
  Read(input["invert_y"], s.input.invert_y);
  Read(input["smoothing_ms"], s.input.smoothing_ms);
  Read(input["anti_deadzone"], s.input.anti_deadzone);
  Read(input["response"], s.input.response);
  s.input.sensitivity = std::clamp(s.input.sensitivity, 0.05f, 20.0f);
  s.input.aim_sensitivity = std::clamp(s.input.aim_sensitivity, 0.05f, 4.0f);
  s.input.smoothing_ms = std::clamp(s.input.smoothing_ms, 4u, 100u);
  s.input.anti_deadzone = std::clamp(s.input.anti_deadzone, 0.0f, 0.9f);
  s.input.response = std::clamp(s.input.response, 0.3f, 1.0f);
  if (const toml::table* bindings = input["bindings"].as_table())
    for (auto&& [control, keys] : *bindings)
      if (auto v = keys.value<std::string>()) s.input.bindings[std::string(control.str())] = *v;

  auto players = t["players"];
  Read(players["sign_in"], s.sign_in);
  for (int i = 0; i < 4; ++i) {
    auto p = players["p" + std::to_string(i + 1)];
    Read(p["device"], s.players[i].device);
    Read(p["name"], s.players[i].name);
    if (s.players[i].name.size() > 15) s.players[i].name.resize(15);  // gamertag length
  }

  auto pacing = t["pacing"];
  Read(pacing["vblank_spin_us"], s.pacing.vblank_spin_us);
  Read(pacing["display_lock"], s.pacing.display_lock);
  Read(pacing["thread_priorities"], s.pacing.thread_priorities);
  Read(pacing["mmcss"], s.pacing.mmcss);

  auto kinect = t["kinect"];
  Read(kinect["source"], s.kinect.source);
  Read(kinect["camera"], s.kinect.camera);
  Read(kinect["mirror"], s.kinect.mirror);
  Read(kinect["fov"], s.kinect.fov);
  Read(kinect["smoothing"], s.kinect.smoothing);
  Read(kinect["players"], s.kinect.players);
  Read(kinect["camera_height"], s.kinect.camera_height);
  Read(kinect["model"], s.kinect.model);
  Read(kinect["tilt_manual"], s.kinect.tilt_manual);
  Read(kinect["tilt_pitch"], s.kinect.tilt_pitch);
  Read(kinect["tilt_roll"], s.kinect.tilt_roll);
  Read(kinect["centre_player"], s.kinect.centre_player);
  s.kinect.tilt_pitch = std::clamp(s.kinect.tilt_pitch, -35.0f, 35.0f);
  s.kinect.tilt_roll = std::clamp(s.kinect.tilt_roll, -35.0f, 35.0f);
  s.kinect.fov = std::clamp(s.kinect.fov, 30.0f, 120.0f);
  s.kinect.smoothing = std::clamp(s.kinect.smoothing, 0.0f, 1.0f);
  s.kinect.players = std::clamp(s.kinect.players, 1u, 2u);
  s.kinect.camera_height = std::clamp(s.kinect.camera_height, 0.2f, 2.0f);
  if (const toml::table* xenia = t["xenia"].as_table()) {
    for (const auto& [key, node] : *xenia) {
      std::ostringstream text;
      node.visit([&](const auto& value) { text << value; });
      s.xenia[std::string(key.str())] = text.str();
    }
  }
}

std::string Float(float v) {
  char text[32];
  std::snprintf(text, sizeof(text), "%g", double(v));
  std::string out = text;
  if (out.find_first_of(".en") == std::string::npos) out += ".0";
  return out;
}

std::string Quote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

std::string Serialize(const Settings& s) {
  std::ostringstream o;
  o << "# AnyX360 settings. Command-line flags override these for one session.\n"
    << "version = " << s.version << "\n\n"
    << "[display]\n"
    << "resolution = " << ResolutionLines(s.display) << "  # window size: 720, 1080, 1440, 2160\n"
    << "fullscreen = " << (s.display.fullscreen ? "true" : "false") << "\n"
    << "render_scale = " << s.display.render_scale << "  # 1-3: the game renders at 1280x720 times this\n"
    << "upscale = \"" << UpscaleName(s.display.upscale) << "\"  # fsr, cas, bilinear\n"
    << "fxaa = " << (s.display.fxaa ? "true" : "false") << "\n"
    << "vsync = " << (s.display.vsync ? "true" : "false") << "\n"
    << "fps = " << s.display.target_fps << "  # 30 or 60\n"
    << "show_stats = " << (s.show_stats ? "true" : "false") << "\n"
    << "achievement_toasts = " << (s.achievement_toasts ? "true" : "false") << "  # a toast when an achievement unlocks\n"
    << "achievement_sound = " << (s.achievement_sound ? "true" : "false") << "  # and its chime\n"
    << "guide_sound = " << (s.guide_sound ? "true" : "false") << "  # a sound when the Guide opens\n"
    << "system_notifications = " << (s.system_notifications ? "true" : "false") << "  # toasts: controllers, sign-ins\n"
    << "kinect_window = " << (s.kinect_window ? "true" : "false") << "  # the Kinect debug window, F3\n"
    << (s.kinect_window_placement.empty() ? "" : "kinect_window_placement = " + Quote(s.kinect_window_placement) + "\n")
    << "\n"
    << "[input]\n"
    << "sensitivity = " << Float(s.input.sensitivity) << "\n"
    << "aim_sensitivity = " << Float(s.input.aim_sensitivity) << "\n"
    << "invert_y = " << (s.input.invert_y ? "true" : "false") << "\n"
    << "smoothing_ms = " << s.input.smoothing_ms << "\n"
    << "anti_deadzone = " << Float(s.input.anti_deadzone) << "\n"
    << "response = " << Float(s.input.response) << "  # 0.3-1: below 1, slow movements turn faster\n\n"
    << "# Overrides of the default bindings, e.g. A = \"SPACE LSHIFT\". Controls: A B X Y LB RB LT RT\n"
    << "# LS RS BACK START UP DOWN LEFT RIGHT LUP LDOWN LLEFT LRIGHT. Keys: letters, digits, F1-F12,\n"
    << "# SPACE RETURN ESCAPE TAB BACK LSHIFT RSHIFT LCTRL RCTRL LALT UP DOWN LEFT RIGHT, mouse\n"
    << "# MOUSE1-MOUSE5 WHEELUP WHEELDOWN.\n"
    << "[input.bindings]\n";
  for (const auto& [control, keys] : s.input.bindings) o << control << " = " << Quote(keys) << "\n";
  o << "\n[players]\n"
    << "sign_in = " << Quote(s.sign_in) << "  # on_demand (a profile is chosen), automatic, at_start, manual\n";
  for (int i = 0; i < 4; ++i)
    o << "p" << i + 1 << " = { device = " << Quote(s.players[i].device) << ", name = " << Quote(s.players[i].name)
      << " }  # keyboard_mouse, pad, pad1-pad4, none\n";
  o << "\n[pacing]\n"
    << "vblank_spin_us = " << s.pacing.vblank_spin_us << "\n"
    << "display_lock = " << (s.pacing.display_lock ? "true" : "false") << "\n"
    << "thread_priorities = " << (s.pacing.thread_priorities ? "true" : "false") << "\n"
    << "mmcss = " << (s.pacing.mmcss ? "true" : "false") << "\n";
  if (s.kinect.enabled) {
    o << "\n[kinect]\n"
      << "source = " << Quote(s.kinect.source) << "  # webcam, mouse, synthetic, none\n"
      << "camera = " << s.kinect.camera << "  # webcam index\n"
      << "mirror = " << (s.kinect.mirror ? "true" : "false") << "  # the webcam already mirrors its image\n"
      << "fov = " << Float(s.kinect.fov) << "  # the webcam's horizontal field of view, degrees\n"
      << "smoothing = " << Float(s.kinect.smoothing) << "  # 0 raw - 1 heavy\n"
      << "players = " << s.kinect.players << "  # 1 or 2\n"
      << "camera_height = " << Float(s.kinect.camera_height) << "  # metres above the floor\n"
      << "model = " << Quote(s.kinect.model) << "  # lite, full, heavy\n"
      << "tilt_manual = " << (s.kinect.tilt_manual ? "true" : "false")
      << "  # true: tilt_pitch (0 for a laptop's camera facing you); false: the bridge measures the pitch\n"
      << "tilt_pitch = " << Float(s.kinect.tilt_pitch) << "  # degrees, > 0 looking down\n"
      << "tilt_roll = " << Float(s.kinect.tilt_roll) << "  # degrees, > 0 turned clockwise (seen from behind)\n"
      << "centre_player = " << (s.kinect.centre_player ? "true" : "false")
      << "  # false: the game sees you where you are; true: centred 2.3 m from the virtual Kinect\n";
  }
  if (!s.xenia.empty()) {
    o << "\n# Options of Xenia's GPU code, by name (advanced: for trying a fix on one GPU).\n[xenia]\n";
    for (const auto& [name, value] : s.xenia) o << name << " = " << value << "\n";
  }
  return o.str();
}

// Writes the saved settings: a temporary file, then an atomic replace.
void Write(const Settings& s) {
  if (g_path.empty()) return;
  std::error_code ec;
  std::filesystem::create_directories(g_path.parent_path(), ec);
  std::filesystem::path tmp = g_path;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      KLOG_W("settings: cannot write %s", tmp.string().c_str());
      return;
    }
    f << Serialize(s);
  }
  if (!MoveFileExW(tmp.c_str(), g_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    KLOG_W("settings: cannot replace %s (%lu)", g_path.string().c_str(), GetLastError());
}

void SaverThread() {
  std::unique_lock lock(g_mutex);
  for (;;) {
    g_save_cv.wait(lock, [] { return g_dirty; });
    if (g_save_cv.wait_until(lock, g_save_at, [] { return !g_dirty; })) continue;  // saved by SaveNow
    if (std::chrono::steady_clock::now() < g_save_at) continue;  // postponed by a newer change
    g_dirty = false;
    Settings copy = g_saved;
    lock.unlock();
    Write(copy);
    lock.lock();
  }
}

}  // namespace

void ResolutionSize(uint32_t lines, uint32_t& width, uint32_t& height) {
  switch (lines) {
    case 720: width = 1280, height = 720; break;
    case 1440: width = 2560, height = 1440; break;
    case 2160: width = 3840, height = 2160; break;
    default: width = 1920, height = 1080; break;
  }
}

uint32_t ResolutionLines(const gpu::DisplayConfig& display) {
  return display.height <= 720 ? 720 : display.height <= 1080 ? 1080 : display.height <= 1440 ? 1440 : 2160;
}

void SetDefaults(const Settings& defaults) {
  std::lock_guard lock(g_mutex);
  g_defaults = defaults;
}

Settings Defaults() {
  std::lock_guard lock(g_mutex);
  return g_defaults;
}

void Load(const std::filesystem::path& path) {
  Settings s = [] {
    std::lock_guard lock(g_mutex);
    return g_defaults;
  }();
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    try {
      toml::table table = toml::parse_file(path.string());
      Parse(table, s);
      Migrate(s, Defaults());
      KLOG_I("settings: %s", path.string().c_str());
    } catch (const toml::parse_error& e) {
      std::filesystem::path bad = path;
      bad += ".bad";
      KLOG_W("settings: %s is invalid (%s, line %u); using defaults, the file was renamed to %s",
             path.string().c_str(), std::string(e.description()).c_str(), unsigned(e.source().begin.line),
             bad.filename().string().c_str());
      std::filesystem::rename(path, bad, ec);
      std::lock_guard lock(g_mutex);
      s = g_defaults;
    }
  }
  {
    std::lock_guard lock(g_mutex);
    g_path = path;
    g_session = g_saved = s;
  }
  static std::once_flag saver;
  std::call_once(saver, [] { std::thread(SaverThread).detach(); });
}

Settings Get() {
  std::lock_guard lock(g_mutex);
  return g_session;
}

Settings GetSaved() {
  std::lock_guard lock(g_mutex);
  return g_saved;
}

void Override(const std::function<void(Settings&)>& change) {
  std::lock_guard lock(g_mutex);
  change(g_session);
}

void Update(const std::function<void(Settings&)>& change) {
  Settings session;
  std::vector<std::function<void(const Settings&)>> listeners;
  {
    std::lock_guard lock(g_mutex);
    change(g_session);
    change(g_saved);
    g_dirty = true;
    g_save_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    session = g_session;
    listeners = g_listeners;
  }
  g_save_cv.notify_all();
  for (auto& listener : listeners) listener(session);
}

void SaveNow() {
  Settings copy;
  {
    std::lock_guard lock(g_mutex);
    if (!g_dirty) return;
    g_dirty = false;
    copy = g_saved;
  }
  g_save_cv.notify_all();
  Write(copy);
}

void Subscribe(std::function<void(const Settings&)> listener) {
  std::lock_guard lock(g_mutex);
  g_listeners.push_back(std::move(listener));
}

}  // namespace x360::settings
