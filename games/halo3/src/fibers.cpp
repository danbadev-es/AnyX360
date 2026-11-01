// Halo 3: the XAPI fibers linked into the game.
//
// SwitchToFiber (825B5FE0) is a branch to assembly (825B8320) that switches stacks, which the
// recompiler can't follow: it left the function empty. The game's loader (822C51F0, reading the
// maps behind the loading screen) switches to a fiber and polls until it is done, so without them
// the first loading screen never ended. The runtime runs each guest fiber on a host fiber
// (runtime/src/kernel/fibers.cpp), as for Forza Motorsport 4 and Mortal Kombat; this older XDK's
// context block has the same layout (LR at +0x1C, r1 at +0x30, r14 at +0x98, the current fiber at
// KTHREAD+0x164).
#include "x360/hle.h"
#include "x360/kernel.h"

X360_GUEST_ORIGINAL(825B5DC0)
X360_GUEST_ORIGINAL(825B5E50)
X360_GUEST_ORIGINAL(825B5EA0)
X360_GUEST_ORIGINAL(825B5F80)

// SwitchToFiber(fiber).
PPC_FUNC(sub_825B5FE0) {
  (void)base;
  x360::kernel::SwitchToGuestFiber(ctx, ctx.r3.u32);
}

// ConvertThreadToFiber(fiber data) and CreateFiber(stack size, start address, fiber data) return a new
// context block (heap memory: the address may have been another fiber's).
PPC_FUNC(sub_825B5DC0) {
  __imp__sub_825B5DC0(ctx, base);
  x360::kernel::ForgetGuestFiber(ctx.r3.u32);
}
PPC_FUNC(sub_825B5EA0) {
  __imp__sub_825B5EA0(ctx, base);
  x360::kernel::ForgetGuestFiber(ctx.r3.u32);
}

// ConvertFiberToThread() frees the thread's context block.
PPC_FUNC(sub_825B5E50) {
  const uint32_t kthread = x360::kernel::Load<uint32_t>(ctx.r13.u32 + 0x100);
  x360::kernel::ForgetGuestFiber(x360::kernel::Load<uint32_t>(kthread + 0x164));
  __imp__sub_825B5E50(ctx, base);
}

// DeleteFiber(fiber).
PPC_FUNC(sub_825B5F80) {
  x360::kernel::ForgetGuestFiber(ctx.r3.u32);
  __imp__sub_825B5F80(ctx, base);
}
