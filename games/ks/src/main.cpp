// Kinect Sports host executable. Command line and paths: see x360/host.h.
//
// Rare's sports collection played with the Kinect, which is the virtual sensor (x360/nui.h); the
// [kinect] settings choose where the players' skeletons come from.
#include "game_info.h"
#include "x360/hle.h"
#include "x360/host.h"

namespace ks {
void InstallNui();
}

// A small function that turns a number into a name (Beach Volleyball's intro asks for 23 when it's
// skipped) by a jump through a table of bytes, whose cases load the address of a string. The
// analyser misread the jump (switch_tables.toml has it with 3 cases on the wrong register) and
// took the other cases for functions of their own, so the recompiled code returned the number
// itself and the caller compared a string at address 23: a crash. This does what the code does: the
// byte picks a case (`lis r11,hi; addi r11,r11,lo; b tail`) and the string is its hi and lo.
X360_GUEST_ORIGINAL(8236B200)
PPC_FUNC(sub_8236B200) {
  constexpr uint32_t kTable = 0x82197A48, kCases = 0x8236B22C;
  const uint32_t code = kCases + uint32_t(PPC_LOAD_U8(kTable + ctx.r3.u32)) * 4;
  const uint32_t hi = PPC_LOAD_U32(code) & 0xFFFF;
  const int16_t lo = int16_t(PPC_LOAD_U32(code + 4) & 0xFFFF);
  ctx.r3.u64 = uint32_t((hi << 16) + int32_t(lo));
}

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from its title update (0.0.4.2): the copy's 0.0.0.2 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  // The game presents every vblank: one vblank per frame is 60 FPS.
  game.vblanks_per_frame = 1;
  // The game creates its avatar manager only if the avatar system starts, and uses it regardless.
  game.avatar_system = true;
  // The avatar library's skeleton builder: avatars are made from the disc's avatar asset pack.
  game.avatar_skeleton = 0x82AD7AE8;
  game.defaults = [](x360::settings::Settings& s) {
    s.kinect.enabled = true;
    // Played with the body: keyboard and mouse don't drive a pad, so the cursor stays free.
    s.players[0].device = "pad";
    s.players[1].device = "none";
  };
  ks::InstallNui();
  return x360::host::RunHost(argc, argv, game);
}
