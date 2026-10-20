// Host entry point shared by every game executable (see x360/host.h).
#include "x360/host.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include "anyx360_version.h"
#include "x360/kernel.h"
#include "xex.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::host {

namespace fs = std::filesystem;

namespace {

fs::path FindUpwards(const fs::path& start, const fs::path& relative) {
  std::error_code ec;
  for (fs::path dir = start; !dir.empty(); dir = dir.parent_path()) {
    if (fs::exists(dir / relative, ec)) return dir / relative;
    if (dir == dir.parent_path()) break;
  }
  return {};
}

// The recompiled code only runs its own executable: refuse another title before any window opens.
bool CheckTitleId(const fs::path& xex_path, const GameDescriptor& game) {
  if (!game.title_id) return true;
  uint32_t title_id = 0;
  try {
    x360tool::Xex xex(x360tool::read_file(xex_path));
    if (auto info = xex.execution_info()) title_id = info->title_id;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "cannot read %s: %s\n", xex_path.string().c_str(), e.what());
    return false;
  }
  if (title_id == game.title_id) return true;
  std::fprintf(stderr, "%s is title %08X; this executable is built for %08X (%s).\n", xex_path.string().c_str(),
               title_id, game.title_id, game.title_name);
  return false;
}

}  // namespace

int RunHost(int argc, char** argv, const GameDescriptor& game) {
  using gpu::DisplayConfig;
  using settings::Settings;
  kernel::TitleInfo info;
  fs::path exe_dir = fs::absolute(argv[0]).parent_path();

  // Display flags override the saved settings for this session only.
  std::vector<std::function<void(Settings&)>> overrides;
  unsigned long restarted_from = 0;
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
    else if (!std::strcmp(arg, "--restarted-from")) restarted_from = std::strtoul(value, nullptr, 10);
    else if (!std::strcmp(arg, "--resolution")) {
      uint32_t lines = uint32_t(std::atoi(value));
      overrides.push_back(
          [lines](Settings& s) { settings::ResolutionSize(lines, s.display.width, s.display.height); });
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
    if (info.game_root.empty() && *game.dev_game_dir) info.game_root = FindUpwards(exe_dir, game.dev_game_dir);
  }
  if (info.key_path.empty()) info.key_path = FindUpwards(exe_dir, "keys/xex2_retail.key");
  if (info.user_root.empty()) {
    const char* local = std::getenv("LOCALAPPDATA");
    info.user_root = fs::path(local ? local : ".") / "AnyX360" / game.user_dir;
  }
  info.xex_path = info.game_root / "default.xex";
  info.title_name = game.title_name;
  info.arcade = game.arcade;
  info.on_function_table = game.on_function_table;

  if (!fs::exists(info.xex_path)) {
    std::fprintf(stderr, "default.xex not found. Pass --game <folder containing default.xex>.\n");
    return 1;
  }
  if (!CheckTitleId(info.xex_path, game)) return 1;

  if (restarted_from) {
    if (HANDLE previous = OpenProcess(SYNCHRONIZE, FALSE, DWORD(restarted_from))) {
      WaitForSingleObject(previous, 10000);
      CloseHandle(previous);
    }
    std::printf("restarted from process %lu\n", restarted_from);
  }
  Settings defaults;
  if (game.defaults) game.defaults(defaults);
  settings::SetDefaults(defaults);
  settings::Load(info.user_root / "settings.toml");
  for (const auto& change : overrides) settings::Override(change);
  if (game.fixed_fps) settings::Override([&](Settings& s) { s.display.target_fps = game.fixed_fps; });
  info.display = settings::Get().display;
  gpu::SetVblanksPerFrame(game.vblanks_per_frame);
  gpu::SetFixedFps(game.fixed_fps);

  const auto& d = info.display;
  std::printf("AnyX360 " ANYX360_VERSION_FULL " - %s\n  game: %s\n  user: %s\n", game.title_name,
              info.game_root.string().c_str(), info.user_root.string().c_str());
  std::printf("  display: %ux%u%s, rendering %ux%u (%ux), %u FPS\n", d.width, d.height,
              d.fullscreen ? " fullscreen" : "", 1280 * d.render_scale, 720 * d.render_scale, d.render_scale,
              d.target_fps);
  return kernel::Run(info);
}

}  // namespace x360::host
