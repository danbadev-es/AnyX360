// Call of Duty: Black Ops II multiplayer and Zombies host executable. Command line and paths: see
// x360/host.h.
//
// Recompiled from default_mp.xex: the single-player executable starts it when the player chooses
// Multiplayer or Zombies (Zombies passes its choice in the launch data), and it starts that one
// again for the campaign. Local play only (split screen, games on one machine); Xbox Live is gone.
// Same title, user folder and saves as the single-player executable.
#include <cstdlib>
#include <cstring>

#include "game_info.h"
#include "x360/hle.h"
#include "x360/host.h"

// The title's memcpy(dst r3, src r4, size r5), returning dst. Recompiled, it copies 8 bytes per
// step through byte-swapped loads and stores; the job queue alone copies a 252-byte job with it each
// time a job is taken, and in two-player Zombies it was 3-8% of each of the busiest threads (the
// three "JQ Worker"s, "Server", "Backend" and the main thread). Bytes are bytes in either order, so
// the host's copy does the same. Overlapping ranges and ranges crossing the 0xE0000000 view (whose
// host offset differs) keep the title's own copy, which defines what those do.
X360_GUEST_ORIGINAL(82916330)
PPC_FUNC(sub_82916330) {
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, size = ctx.r5.u32;
  const auto crosses = [](uint32_t address, uint32_t n) {
    return address < 0xE0000000u && uint64_t(address) + n > 0xE0000000u;
  };
  const bool overlap = dst < uint64_t(src) + size && src < uint64_t(dst) + size;
  static const bool guest_copy = std::getenv("ANYX360_GUEST_MEMCPY") != nullptr;  // A/B tests
  if (guest_copy || !size || overlap || crosses(dst, size) || crosses(src, size) || uint64_t(dst) + size > 0x100000000ull ||
      uint64_t(src) + size > 0x100000000ull) {
    __imp__sub_82916330(ctx, base);
    return;
  }
  std::memcpy(PPC_ADDR(dst), PPC_ADDR(src), size);
}

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from the same title update (0.0.18.14): default_mp.xex is patched with its
  // default_mp.xexp at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  game.xex_name = game_info::kXexName;
  for (const auto& [xex, exe] : game_info::kExecutables) game.executables.push_back({xex, exe});
  // 60 FPS, every vblank, as in the campaign.
  game.vblanks_per_frame = 1;
  // The main thread on hardware thread 0, as on the console and in the campaign.
  game.main_thread_processor = 0;
  return x360::host::RunHost(argc, argv, game);
}
