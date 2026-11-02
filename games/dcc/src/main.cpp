// Doritos Crash Course host executable. Command line and paths: see x360/host.h.
//
// An Xbox Live Arcade title: the game folder is the arcade package's contents. Played with a
// controller (or the keyboard and mouse mapped to one). Every character is an Xbox Avatar, made
// from the avatar asset pack that Kinect Sports' disc carries (docs/AVATARS.md).
#include "game_info.h"
#include "x360/host.h"

int main(int argc, char** argv) {
  x360::host::GameDescriptor game;
  game.title_name = game_info::kTitle;
  game.user_dir = game_info::kUserDir;
  game.dev_game_dir = game_info::kDevGameDir;
  game.title_id = game_info::kTitleId;
  // Recompiled from its title update (0.0.2.2): the copy's 0.0.0.2 is patched with it at start.
  game.update_package = game_info::kUpdatePackage;
  game.update_version = game_info::kUpdateVersion;
  game.arcade = true;
  game.avatar_system = true;
  // The avatar library's default skeleton (this version has no shaping by height and weight).
  game.avatar_skeleton = 0x82506768;
  // With no avatar pack at all it goes without avatars, as the base game did: given the system
  // with empty assets it crashed on its first level (a null read in sub_82877CC8).
  game.avatar_system_needs_pack = true;
  // The game presents every vblank: 60 FPS, as on the console. With the default 120 Hz vblank it
  // ran at up to 120 FPS on a fast display.
  game.vblanks_per_frame = 1;
  game.fixed_fps = 60;
  // Its sound came late: 8 audio frames ahead of playback (43 ms) instead of 16 (85 ms). Its mixing
  // keeps up (measured: never more than 2 frames behind, a callback under 0.5 ms).
  game.audio_queued_frames = 8;
  return x360::host::RunHost(argc, argv, game);
}
