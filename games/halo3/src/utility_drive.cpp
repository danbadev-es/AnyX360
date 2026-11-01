// Halo 3: the XDK's utility drive linked into the game.
//
// At start the game mounts the hard drive's cache partitions (sub_821A8448): XMountUtilityDriveEx
// (825982F8), XFlushUtilityDrive (82597AD0), then it writes cache0:\test.dat. Only if all of that
// works does it set its "using utility drive" flag (828C3F05), which is what it takes for a hard
// drive: without it Theater and campaign co-op over System Link said "one or more players don't have
// an Xbox 360 hard drive". This XDK's XMountUtilityDriveEx formats the partitions through the raw
// disk (partition 0's header, an XTAF volume each) and mounts them with a file system driver of its
// own (IoCreateDevice and the title's own I/O routines), which the runtime doesn't run. Here the
// partitions are host folders, already cache0: and cache1:, so both calls just answer success.
#include "x360/hle.h"
#include "x360/kernel.h"

// XMountUtilityDriveEx(flags, bytes per cluster, file cache size): a Win32 error code.
PPC_FUNC(sub_825982F8) {
  (void)base;
  ctx.r3.u64 = x360::kernel::MountUtilityDrive();
}

// XFlushUtilityDrive(): a Win32 error code. Called after the game writes its saves to the cache.
PPC_FUNC(sub_82597AD0) {
  (void)base;
  ctx.r3.u64 = x360::kernel::MountUtilityDrive();
}
