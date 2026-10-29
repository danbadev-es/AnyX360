// Forza Motorsport 4: the XAPI fibers linked into the game.
//
// The game's cooperative job scheduler (8294D940 and its neighbours: the queues behind the race start
// and the loading screens' tasks) runs each job on a fiber. SwitchToFiber (82C82C10) jumps to
// assembly (82C85380) that switches stacks, which the recompiler can't follow: it was left empty, no job
// ever ran, and the first race never loaded. The runtime runs each guest fiber on a host fiber
// (runtime/src/kernel/fibers.cpp).
#include "x360/hle.h"
#include "x360/kernel.h"

X360_GUEST_ORIGINAL(82C82C10)
X360_GUEST_ORIGINAL(82C829F0)
X360_GUEST_ORIGINAL(82C82AD0)
X360_GUEST_ORIGINAL(82C82BB0)
X360_GUEST_ORIGINAL(82C82A80)

// SwitchToFiber(fiber).
PPC_FUNC(sub_82C82C10) {
  (void)base;
  x360::kernel::SwitchToGuestFiber(ctx, ctx.r3.u32);
}

// ConvertThreadToFiber(fiber data) and CreateFiber(stack size, start address, fiber data) return a new
// context block (heap memory: the address may have been another fiber's).
PPC_FUNC(sub_82C829F0) {
  __imp__sub_82C829F0(ctx, base);
  x360::kernel::ForgetGuestFiber(ctx.r3.u32);
}
PPC_FUNC(sub_82C82AD0) {
  __imp__sub_82C82AD0(ctx, base);
  x360::kernel::ForgetGuestFiber(ctx.r3.u32);
}

// ConvertFiberToThread() frees the thread's context block.
PPC_FUNC(sub_82C82A80) {
  const uint32_t kthread = x360::kernel::Load<uint32_t>(ctx.r13.u32 + 0x100);
  x360::kernel::ForgetGuestFiber(x360::kernel::Load<uint32_t>(kthread + 0x164));
  __imp__sub_82C82A80(ctx, base);
}

// DeleteFiber(fiber).
PPC_FUNC(sub_82C82BB0) {
  x360::kernel::ForgetGuestFiber(ctx.r3.u32);
  __imp__sub_82C82BB0(ctx, base);
}
