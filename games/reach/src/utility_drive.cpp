// Halo: Reach: the XDK's utility drive linked into the game, as in Halo 3 (games/halo3/src/
// utility_drive.cpp).
//
// At start the game mounts the hard drive's cache partitions (sub_82237A98): XMountUtilityDriveEx
// (827E2B38), XFlushUtilityDrive (827E2358), then it writes a test file to cache0:. Only if all of
// that works does it set its "using utility drive" flag (83151375), which is what it takes for a
// hard drive. Its own XMountUtilityDriveEx formats and mounts the partitions with a file system
// driver of its own, which the runtime doesn't run; here they are host folders, already cache0: and
// cache1:, so both calls just answer success.
#include "x360/hle.h"
#include "x360/kernel.h"

// XMountUtilityDriveEx(flags, bytes per cluster, file cache size): a Win32 error code.
PPC_FUNC(sub_827E2B38) {
  (void)base;
  ctx.r3.u64 = x360::kernel::MountUtilityDrive();
}

// XFlushUtilityDrive(): a Win32 error code.
PPC_FUNC(sub_827E2358) {
  (void)base;
  ctx.r3.u64 = x360::kernel::MountUtilityDrive();
}
