// Mortal Kombat Komplete Edition: the XAPI fibers linked into the game.
//
// SwitchToFiber (82BDC380) is a branch to assembly (82BDCC80) that switches stacks, which the
// recompiler can't follow: it left the function empty. The game switches fibers in 82AFAB28 (a list
// of latent tasks the main loop runs, 829D06C8): the thread converts itself to a fiber, switches to
// the task's fiber and, back from it, converts itself back. The runtime runs each guest fiber on a
// host fiber (runtime/src/kernel/fibers.cpp), as for Forza Motorsport 4.
#include "x360/hle.h"
#include "x360/kernel.h"

X360_GUEST_ORIGINAL(82BDC240)
X360_GUEST_ORIGINAL(82BDC2D0)
X360_GUEST_ORIGINAL(82BDC320)

// SwitchToFiber(fiber).
PPC_FUNC(sub_82BDC380) {
  (void)base;
  x360::kernel::SwitchToGuestFiber(ctx, ctx.r3.u32);
}

// ConvertThreadToFiber(fiber data) returns a new context block (heap memory: the address may have
// been another fiber's).
PPC_FUNC(sub_82BDC240) {
  __imp__sub_82BDC240(ctx, base);
  x360::kernel::ForgetGuestFiber(ctx.r3.u32);
}

// ConvertFiberToThread() frees the thread's context block.
PPC_FUNC(sub_82BDC2D0) {
  const uint32_t kthread = x360::kernel::Load<uint32_t>(ctx.r13.u32 + 0x100);
  x360::kernel::ForgetGuestFiber(x360::kernel::Load<uint32_t>(kthread + 0x164));
  __imp__sub_82BDC2D0(ctx, base);
}

// DeleteFiber(fiber).
PPC_FUNC(sub_82BDC320) {
  x360::kernel::ForgetGuestFiber(ctx.r3.u32);
  __imp__sub_82BDC320(ctx, base);
}
