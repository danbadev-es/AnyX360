// XAPI fibers. CreateFiber, ConvertThreadToFiber, SwitchToFiber and DeleteFiber are XAPI code linked
// into the title, not kernel exports. SwitchToFiber is assembly: it saves the non-volatile registers,
// CR, LR and r1 into the current fiber's context block, loads the target's and ends with
// KeSetCurrentStackPointers, which sets r1 and returns into the target. Recompiled code can't switch
// stacks that way (its frames are on the host stack), so each guest fiber runs on a host fiber with a
// guest context (PPCContext) of its own: a switch suspends the caller where it is, registers and host
// frames included, and resumes the target where it left off or, the first time, enters it as the
// assembly would: r1 and r14..r31 from its context block, then a call to its saved LR (CreateFiber's
// start thunk, which calls the fiber routine).
//
// Context block (XDK 2.0.20764): +0x00 fiber data, +0x04 stack allocation base, +0x08 stack base
// (high), +0x0C stack limit (low), +0x1C LR, +0x30 r1, +0x98 r14..r31 (8 bytes each). The thread's
// current fiber is KTHREAD+0x164. A title hooks its SwitchToFiber with SwitchToGuestFiber and calls
// ForgetGuestFiber for each context block CreateFiber or ConvertThreadToFiber returns and DeleteFiber
// frees (blocks are heap memory, and an address can come back for another fiber).
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::kernel {

namespace {

constexpr uint32_t kKThreadFiber = 0x164;
constexpr uint32_t kFiberStackBase = 0x08, kFiberStackLimit = 0x0C, kFiberLr = 0x1C, kFiberR1 = 0x30,
                   kFiberR14 = 0x98;
constexpr SIZE_T kHostFiberStack = 32 * 1024 * 1024;  // as a guest thread's host stack

struct GuestFiber {
  uint32_t block = 0;
  void* host = nullptr;               // the host fiber running it, once it has run
  PPCContext* context = nullptr;      // its guest context (on its host stack) once it has run
  const PPCContext* first = nullptr;  // the switching context, copied when it starts
  bool owned = false;    // its host fiber was created here (not a thread converted to a fiber)
  bool running = false;  // running on some thread now
};

std::mutex g_mutex;
std::unordered_map<uint32_t, std::unique_ptr<GuestFiber>> g_fibers;

GuestFiber& Entry(uint32_t block) {
  auto& entry = g_fibers[block];
  if (!entry) {
    entry = std::make_unique<GuestFiber>();
    entry->block = block;
  }
  return *entry;
}

// The guest side of a switch, as the assembly leaves it: the thread's current fiber and its stack.
void EnterGuestFiber(Thread* t, uint32_t kthread, uint32_t block) {
  Store<uint32_t>(kthread + kKThreadFiber, block);
  const uint32_t stack_base = Load<uint32_t>(block + kFiberStackBase);
  const uint32_t stack_limit = Load<uint32_t>(block + kFiberStackLimit);
  Store<uint32_t>(kthread + 0x5C, stack_base);
  Store<uint32_t>(kthread + 0x60, stack_limit);
  if (t && t->kthread == kthread) {
    t->stack_base = stack_base;
    t->stack_limit = stack_limit;
    if (t->pcr) {
      Store<uint32_t>(t->pcr + 0x070, stack_base);
      Store<uint32_t>(t->pcr + 0x074, stack_limit);
    }
  }
}

void WINAPI HostFiberMain(void* param) {
  auto* fiber = static_cast<GuestFiber*>(param);
  alignas(64) PPCContext ctx = *fiber->first;
  const uint32_t block = fiber->block;
  ctx.r1.u64 = Load<uint64_t>(block + kFiberR1);
  PPCRegister* nonvolatile[] = {&ctx.r14, &ctx.r15, &ctx.r16, &ctx.r17, &ctx.r18, &ctx.r19,
                                &ctx.r20, &ctx.r21, &ctx.r22, &ctx.r23, &ctx.r24, &ctx.r25,
                                &ctx.r26, &ctx.r27, &ctx.r28, &ctx.r29, &ctx.r30, &ctx.r31};
  for (int i = 0; i < 18; ++i) nonvolatile[i]->u64 = Load<uint64_t>(block + kFiberR14 + 8 * i);
  const uint32_t entry = Load<uint32_t>(block + kFiberLr);
  ctx.lr = entry;
  {
    std::lock_guard lock(g_mutex);
    fiber->context = &ctx;
    fiber->first = nullptr;
  }
  if (Thread* t = CurrentThread()) t->context = &ctx;
  PPCFunc* function = PPC_LOOKUP_FUNC(g_base, entry);
  if (!function) Fatal("guest fiber %08X: no recompiled function at %08X", block, entry);
  static std::atomic<int> started{0};
  if (started.fetch_add(1) < 8) KLOG_I("guest fiber %08X starting at %08X (r1 %08X)", block, entry, ctx.r1.u32);
  function(ctx, g_base);
  // CreateFiber's start thunk ends the thread (KeBugCheck) when the routine returns.
  Fatal("guest fiber %08X returned from %08X", block, entry);
}

}  // namespace

void SwitchToGuestFiber(PPCContext& ctx, uint32_t target) {
  Thread* t = CurrentThread();
  const uint32_t kthread = Load<uint32_t>(ctx.r13.u32 + 0x100);
  const uint32_t current = Load<uint32_t>(kthread + kKThreadFiber);
  if (!target || target == current) return;
  if (!::IsThreadAFiber() && !::ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH))
    Fatal("SwitchToFiber: ConvertThreadToFiberEx failed (%lu)", ::GetLastError());
  void* host_target;
  {
    std::lock_guard lock(g_mutex);
    // The running fiber (a converted thread's block is first seen here).
    if (current) {
      GuestFiber& from = Entry(current);
      if (!from.host) from.host = ::GetCurrentFiber();
      from.context = &ctx;
      from.running = false;
    }
    GuestFiber& to = Entry(target);
    if (to.running) Fatal("SwitchToFiber: fiber %08X is already running", target);
    if (!to.host) {
      to.first = &ctx;
      to.owned = true;
      to.host = ::CreateFiberEx(64 * 1024, kHostFiberStack, FIBER_FLAG_FLOAT_SWITCH, HostFiberMain, &to);
      if (!to.host) Fatal("SwitchToFiber: CreateFiberEx failed (%lu)", ::GetLastError());
    } else if (t && to.context) {
      t->context = to.context;
    }
    to.running = true;
    host_target = to.host;
  }
  EnterGuestFiber(t, kthread, target);
  ::SwitchToFiber(host_target);
  // Resumed: whoever switched back has set the guest side for this fiber.
  if (Thread* back = CurrentThread()) back->context = &ctx;
}

void ForgetGuestFiber(uint32_t block) {
  if (!block) return;
  std::lock_guard lock(g_mutex);
  auto it = g_fibers.find(block);
  if (it == g_fibers.end()) return;
  // A host fiber created here that is suspended (a deleted fiber's) goes with it. A running one (a
  // fiber deleting itself ends its thread) and a converted thread's stay.
  const GuestFiber& fiber = *it->second;
  if (fiber.host && fiber.owned && !fiber.running) ::DeleteFiber(fiber.host);
  g_fibers.erase(it);
}

}  // namespace x360::kernel
