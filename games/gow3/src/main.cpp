// Gears of War 3 host executable.
//
//   GearsOfWar3.exe [--game <folder with default.xex>] [--key <xex2_retail.key>] [--user <folder>]
//                   [--resolution 720|1080|1440|2160] [--scale 1-3] [--fps 30|60] [--fullscreen]
//                   [--upscale fsr|cas|bilinear] [--fxaa]
//
// Defaults: the game folder extracted by the docs (extracted\gow3\Gears Of War 3\AP7kB), the key in
// keys\xex2_retail.key, and %LOCALAPPDATA%\AnyX360\GearsOfWar3 for saves, cache and settings.
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

#include "x360/kernel.h"

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

void SetResolution(x360::gpu::DisplayConfig& display, int lines) {
  switch (lines) {
    case 720: display.width = 1280, display.height = 720; break;
    case 1440: display.width = 2560, display.height = 1440; break;
    case 2160: display.width = 3840, display.height = 2160; break;
    default: display.width = 1920, display.height = 1080; break;
  }
}

}  // namespace

int main(int argc, char** argv) {
  x360::kernel::TitleInfo info;
  fs::path exe_dir = fs::absolute(argv[0]).parent_path();

  uint32_t scale_override = 0;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const char* value = i + 1 < argc ? argv[i + 1] : nullptr;
    if (!std::strcmp(arg, "--fullscreen")) {
      info.display.fullscreen = true;
      continue;
    }
    if (!std::strcmp(arg, "--fxaa")) {
      info.display.fxaa = true;
      continue;
    }
    if (!value) continue;
    if (!std::strcmp(arg, "--game")) info.game_root = value;
    else if (!std::strcmp(arg, "--key")) info.key_path = value;
    else if (!std::strcmp(arg, "--user")) info.user_root = value;
    else if (!std::strcmp(arg, "--resolution")) SetResolution(info.display, std::atoi(value));
    else if (!std::strcmp(arg, "--scale")) scale_override = uint32_t(std::atoi(value));
    else if (!std::strcmp(arg, "--fps")) info.display.target_fps = std::atoi(value) >= 60 ? 60 : 30;
    else if (!std::strcmp(arg, "--upscale"))
      info.display.upscale = !std::strcmp(value, "bilinear") ? x360::gpu::DisplayConfig::Upscale::kBilinear
                             : !std::strcmp(value, "cas")    ? x360::gpu::DisplayConfig::Upscale::kCas
                                                             : x360::gpu::DisplayConfig::Upscale::kFsr;
    else continue;
    ++i;
  }
  if (scale_override) info.display.render_scale = std::clamp(scale_override, 1u, 3u);

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
  info.on_function_table = gow3::InstallUe3Trace;

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
