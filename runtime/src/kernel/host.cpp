// Host entry point shared by every game executable (see x360/host.h).
#include "x360/host.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include "anyx360_version.h"
#include "x360/dashboard.h"
#include "x360/gpu.h"
#include "x360/kernel.h"
#include "x360/profile.h"
#include "x360/profiles.h"
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

// "a.b.c.d" as an execution info version; 0 if it isn't one.
uint32_t ParseVersion(const char* text) {
  unsigned major = 0, minor = 0, build = 0, qfe = 0;
  if (std::sscanf(text, "%u.%u.%u.%u", &major, &minor, &build, &qfe) != 4) return 0;
  return major << 28 | (minor & 0xF) << 24 | (build & 0xFFFF) << 8 | (qfe & 0xFF);
}

// A game recompiled from its title update: the update installed for the player (the launcher puts
// it in <user>\content\000B0000\<package>), the game folder's XEX patched with it (cached), and the
// folder to mount as update:\. False, with the reason printed, when it can't start.
bool ApplyTitleUpdate(const GameDescriptor& game, kernel::TitleInfo& info) {
  const fs::path folder = info.user_root / "content" / "000B0000" / game.update_package;
  fs::path patch = folder / fs::path(game.xex_name).filename();
  patch += "p";  // default.xex -> default.xexp, as Xenia finds it
  if (!fs::exists(patch)) {
    std::fprintf(stderr,
                 "%s is played with its title update (version %s), which isn't installed: %s was not found.\n"
                 "Add the update in the AnyX360 launcher (Add game: the archive the game came in, or the update's "
                 "TU_ file or package), or put the update's files in %s.\n",
                 game.title_name, game.update_version, patch.string().c_str(), folder.string().c_str());
    return false;
  }
  const kernel::PreparedUpdate prepared =
      kernel::PrepareTitleUpdate(info.xex_path, patch, ParseVersion(game.update_version), info.user_root / "cache" / "patched");
  if (prepared.xex.empty()) {
    std::fprintf(stderr, "%s: the title update in %s can't be applied to %s: %s.\n", game.title_name,
                 folder.string().c_str(), info.xex_path.string().c_str(), prepared.error.c_str());
    return false;
  }
  std::printf("  update: %s (version %s, %s)\n", folder.string().c_str(), game.update_version,
              prepared.cached ? "cached" : "patched now");
  info.image_path = prepared.xex;
  info.update_root = folder;
  return true;
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
  gpu::LaunchWindow launch;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const char* value = i + 1 < argc ? argv[i + 1] : nullptr;
    if (!std::strcmp(arg, "--fullscreen")) {
      overrides.push_back([](Settings& s) { s.display.fullscreen = true; });
      launch.fullscreen = true;
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
    else if (!std::strcmp(arg, "--dashboard")) launch.dashboard_pid = uint32_t(std::strtoul(value, nullptr, 10));
    else if (!std::strcmp(arg, "--window")) {
      if (const auto rect = dashboard::ParseWindowArg(value)) {
        launch.has_rect = true;
        launch.x = rect->x, launch.y = rect->y, launch.w = rect->w, launch.h = rect->h;
      } else {
        std::fprintf(stderr, "--window %s: expected x,y,width,height\n", value);
      }
    }
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
  info.xex_path = info.game_root / game.xex_name;
  info.title_name = game.title_name;
  info.arcade = game.arcade;
  info.avatar_system = game.avatar_system;
  info.avatar_skeleton = game.avatar_skeleton;
  info.main_thread_processor = game.main_thread_processor;
  info.game_region_from_country = game.game_region_from_country;
  info.on_function_table = game.on_function_table;
  for (const TitleExecutable& executable : game.executables) {
    std::string xex = executable.xex;
    for (char& c : xex) c = char(std::tolower(uint8_t(c)));
    info.executables.emplace_back(xex, exe_dir / (std::string(executable.exe) + ".exe"));
  }
  for (const TitleModule& module : game.modules) {
    std::string xex = module.xex;
    for (char& c : xex) c = char(std::tolower(uint8_t(c)));
    info.modules.emplace_back(xex, module.mappings);
  }
  // Who is signed in, next to the games' user folders (the launcher writes it), and their profiles.
  // A profile.txt from before 0.20.0 becomes profiles first.
  info.profiles_root = info.user_root.parent_path();
  const profile::SignIn sign_in = profiles::LoadSignIn(info.profiles_root, false);
  const profiles::SignedIn signed_in = profiles::Resolve(info.profiles_root, sign_in);
  for (int i = 0; i < profile::kPlayers; ++i) {
    if (!signed_in.slots[i]) continue;
    info.players[i].xuid = signed_in.slots[i]->xuid;
    info.players[i].gamertag = signed_in.slots[i]->gamertag;
  }
  // Player 1 without a profile signed in plays as the one that keeps the root folder, if it exists.
  if (!info.players[0].xuid) {
    if (std::optional<profiles::Profile> first = profiles::Load(info.profiles_root, profiles::kLegacyXuid)) {
      bool elsewhere = false;
      for (const auto& player : info.players) elsewhere |= player.xuid == first->xuid;
      if (!elsewhere) info.players[0] = {first->xuid, first->gamertag};
    }
  }
  // The language: player 1's profile's, else profile.txt's.
  std::string language = sign_in.language;
  if (signed_in.slots[0] && signed_in.slots[0]->language != "auto") language = signed_in.slots[0]->language;
  const profile::Locale locale = profile::Resolve(language);
  info.language = locale.language;
  info.country = locale.country;

  if (!fs::exists(info.xex_path)) {
    std::fprintf(stderr, "%s not found. Pass --game <folder containing %s>.\n", game.xex_name, game.xex_name);
    return 1;
  }
  if (!CheckTitleId(info.xex_path, game)) return 1;
  if (*game.update_package && !ApplyTitleUpdate(game, info)) return 1;

  info.restarted = restarted_from != 0;
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
  // In the dashboard's window: windowed in its rectangle unless --fullscreen came too (the saved
  // setting doesn't apply).
  if (launch.has_rect && !launch.fullscreen) settings::Override([](Settings& s) { s.display.fullscreen = false; });
  gpu::SetLaunchWindow(launch);
  if (game.fixed_fps) settings::Override([&](Settings& s) { s.display.target_fps = game.fixed_fps; });
  info.display = settings::Get().display;
  gpu::SetVblanksPerFrame(game.vblanks_per_frame);
  gpu::SetFixedFps(game.fixed_fps);
  {
    std::vector<std::pair<std::string, std::string>> xenia_options;
    for (const auto& [name, value] : game.xenia_options) xenia_options.emplace_back(name, value);
    gpu::SetTitleXeniaOptions(xenia_options);
  }

  const auto& d = info.display;
  std::printf("AnyX360 " ANYX360_VERSION_FULL " - %s\n  game: %s\n  user: %s\n", game.title_name,
              info.game_root.string().c_str(), info.user_root.string().c_str());
  std::printf("  language %u, country %u (profile: %s)\n", info.language, info.country, language.c_str());
  for (int i = 0; i < profile::kPlayers; ++i)
    if (info.players[i].xuid)
      std::printf("  player %d: %s (%s)\n", i + 1, info.players[i].gamertag.c_str(),
                  profiles::XuidText(info.players[i].xuid).c_str());
  std::printf("  display: %ux%u%s, rendering %ux%u (%ux), %u FPS\n", d.width, d.height,
              d.fullscreen ? " fullscreen" : "", 1280 * d.render_scale, 720 * d.render_scale, d.render_scale,
              d.target_fps);
  return kernel::Run(info);
}

}  // namespace x360::host
