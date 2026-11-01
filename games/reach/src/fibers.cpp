// Halo: Reach: the XAPI fibers linked into the game, as in Halo 3 (games/halo3/src/fibers.cpp).
//
// SwitchToFiber (82960418) is a branch to assembly (829604B0) that switches stacks, which the
// recompiler can't follow. Halo 3's map loader switches to a fiber and polls until it is done; the
// runtime runs each guest fiber on a host fiber (runtime/src/kernel/fibers.cpp). The other XAPI
// fiber functions: ConvertThreadToFiber 829601F8, ConvertFiberToThread 82960288, CreateFiber
// 829602D8, DeleteFiber 829603B8, with the same context block as Halo 3's.
#include "x360/hle.h"
#include "x360/kernel.h"

X360_GUEST_ORIGINAL(829601F8)
X360_GUEST_ORIGINAL(82960288)
X360_GUEST_ORIGINAL(829602D8)
X360_GUEST_ORIGINAL(829603B8)

// SwitchToFiber(fiber).
PPC_FUNC(sub_82960418) {
  (void)base;
  x360::kernel::SwitchToGuestFiber(ctx, ctx.r3.u32);
}

// ConvertThreadToFiber(fiber data) and CreateFiber(stack size, start address, fiber data) return a new
// context block (heap memory: the address may have been another fiber's).
PPC_FUNC(sub_829601F8) {
  __imp__sub_829601F8(ctx, base);
  x360::kernel::ForgetGuestFiber(ctx.r3.u32);
}
PPC_FUNC(sub_829602D8) {
  __imp__sub_829602D8(ctx, base);
  x360::kernel::ForgetGuestFiber(ctx.r3.u32);
}

// ConvertFiberToThread() frees the thread's context block.
PPC_FUNC(sub_82960288) {
  const uint32_t kthread = x360::kernel::Load<uint32_t>(ctx.r13.u32 + 0x100);
  x360::kernel::ForgetGuestFiber(x360::kernel::Load<uint32_t>(kthread + 0x164));
  __imp__sub_82960288(ctx, base);
}

// DeleteFiber(fiber).
PPC_FUNC(sub_829603B8) {
  x360::kernel::ForgetGuestFiber(ctx.r3.u32);
  __imp__sub_829603B8(ctx, base);
}
