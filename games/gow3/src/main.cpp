// Gears of War 3 host executable.
//
//   GearsOfWar3.exe [--game <folder with default.xex>] [--key <xex2_retail.key>] [--user <folder>]
//
// Defaults: the game folder extracted by the docs (extracted\gow3\Gears Of War 3\AP7kB), the key in
// keys\xex2_retail.key, and %LOCALAPPDATA%\AnyX360\GearsOfWar3 for saves, cache and settings.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "x360/kernel.h"

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

}  // namespace

int main(int argc, char** argv) {
  x360::kernel::TitleInfo info;
  fs::path exe_dir = fs::absolute(argv[0]).parent_path();

  for (int i = 1; i + 1 < argc; ++i) {
    if (!std::strcmp(argv[i], "--game")) info.game_root = argv[++i];
    else if (!std::strcmp(argv[i], "--key")) info.key_path = argv[++i];
    else if (!std::strcmp(argv[i], "--user")) info.user_root = argv[++i];
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

  if (!fs::exists(info.xex_path)) {
    std::fprintf(stderr, "default.xex not found. Pass --game <folder containing default.xex>.\n");
    return 1;
  }
  std::printf("AnyX360 - Gears of War 3\n  game: %s\n  user: %s\n", info.game_root.string().c_str(),
              info.user_root.string().c_str());
  return x360::kernel::Run(info);
}
