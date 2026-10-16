// Gears of War 3 host executable.
//
//   GearsOfWar3.exe [--game <folder with default.xex>] [--key <xex2_retail.key>] [--user <folder>]
//                   [--resolution 720|1080|1440|2160] [--scale 1-3] [--fps 30|60] [--fullscreen]
//                   [--upscale fsr|cas|bilinear] [--fxaa]
//
// Defaults: the game folder extracted by the docs (extracted\gow3\Gears Of War 3\AP7kB), the key in
// keys\xex2_retail.key, and %LOCALAPPDATA%\AnyX360\GearsOfWar3 for saves, cache and settings.
// Display options are saved in <user>\settings.toml; the flags below override them for one session.
//
// --resolution sets the window size (default 1080p). The title renders at 1280x720 times --scale
// (default 1; Xenia only scales by integers, so 1080p cannot be rendered natively: 2x renders 1440p
// and costs the GPU about four times as much) and the image is scaled to the window with --upscale
// (default AMD FSR). --fps 60 (default) runs the vblank at 120 Hz, as the game presents every second
// vblank (30 FPS on the console). F11 or Alt+Enter toggle fullscreen, F1 the performance overlay.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <vector>

#include "x360/kernel.h"
#include "x360/settings.h"

namespace fs = std::filesystem;

namespace gow3 {
void InstallUe3Trace();
}

namespace {

fs::path FindUpwards(const fs::path& start, const fs::path& relative) {
  std::error_code ec;
  for (fs::path dir = start; !dir.empty(); dir = dir.parent_path()) {
    if (fs::exists(dir / relative, ec)) return dir / relative;
    if (dir == dir.parent_path()) break;
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  using x360::gpu::DisplayConfig;
  using x360::settings::Settings;
  x360::kernel::TitleInfo info;
  fs::path exe_dir = fs::absolute(argv[0]).parent_path();

  // Display flags override the saved settings for this session only.
  std::vector<std::function<void(Settings&)>> overrides;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const char* value = i + 1 < argc ? argv[i + 1] : nullptr;
    if (!std::strcmp(arg, "--fullscreen")) {
      overrides.push_back([](Settings& s) { s.display.fullscreen = true; });
      continue;
    }
    if (!std::strcmp(arg, "--fxaa")) {
      overrides.push_back([](Settings& s) { s.display.fxaa = true; });
      continue;
    }
    if (!value) continue;
    if (!std::strcmp(arg, "--game")) info.game_root = value;
    else if (!std::strcmp(arg, "--key")) info.key_path = value;
    else if (!std::strcmp(arg, "--user")) info.user_root = value;
    else if (!std::strcmp(arg, "--resolution")) {
      uint32_t lines = uint32_t(std::atoi(value));
      overrides.push_back(
          [lines](Settings& s) { x360::settings::ResolutionSize(lines, s.display.width, s.display.height); });
    } else if (!std::strcmp(arg, "--scale")) {
      uint32_t scale = std::clamp(uint32_t(std::atoi(value)), 1u, 3u);
      overrides.push_back([scale](Settings& s) { s.display.render_scale = scale; });
    } else if (!std::strcmp(arg, "--fps")) {
      uint32_t fps = std::atoi(value) >= 60 ? 60 : 30;
      overrides.push_back([fps](Settings& s) { s.display.target_fps = fps; });
    } else if (!std::strcmp(arg, "--upscale")) {
      DisplayConfig::Upscale upscale = !std::strcmp(value, "bilinear") ? DisplayConfig::Upscale::kBilinear
                                       : !std::strcmp(value, "cas")    ? DisplayConfig::Upscale::kCas
                                                                       : DisplayConfig::Upscale::kFsr;
      overrides.push_back([upscale](Settings& s) { s.display.upscale = upscale; });
    } else
      continue;
    ++i;
  }

  if (info.game_root.empty()) {
    info.game_root = FindUpwards(exe_dir, "game");
    if (info.game_root.empty()) info.game_root = FindUpwards(exe_dir, "extracted/gow3/Gears Of War 3/AP7kB");
  }
  if (info.key_path.empty()) info.key_path = FindUpwards(exe_dir, "keys/xex2_retail.key");
  if (info.user_root.empty()) {
    const char* local = std::getenv("LOCALAPPDATA");
    info.user_root = fs::path(local ? local : ".") / "AnyX360" / "GearsOfWar3";
  }
  info.xex_path = info.game_root / "default.xex";
  info.title_name = "Gears of War 3";
  info.on_function_table = gow3::InstallUe3Trace;

  x360::settings::Load(info.user_root / "settings.toml");
  for (const auto& change : overrides) x360::settings::Override(change);
  info.display = x360::settings::Get().display;

  if (!fs::exists(info.xex_path)) {
    std::fprintf(stderr, "default.xex not found. Pass --game <folder containing default.xex>.\n");
    return 1;
  }
  const auto& d = info.display;
  std::printf("AnyX360 - Gears of War 3\n  game: %s\n  user: %s\n", info.game_root.string().c_str(),
              info.user_root.string().c_str());
  std::printf("  display: %ux%u%s, rendering %ux%u (%ux), %u FPS\n", d.width, d.height,
              d.fullscreen ? " fullscreen" : "", 1280 * d.render_scale, 720 * d.render_scale, d.render_scale,
              d.target_fps);
  return x360::kernel::Run(info);
}
