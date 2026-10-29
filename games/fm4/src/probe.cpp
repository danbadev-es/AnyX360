// TEMPORARY probe (not for commit): the first race's load after the welcome video.
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "x360/hle.h"

namespace {
const auto g_t0 = std::chrono::steady_clock::now();
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count(); }
void P(const char* fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  std::fprintf(stderr, "PROBE %7.2f %s\n", Now(), buf);
}
uint32_t L(uint8_t* base, uint32_t a) { return __builtin_bswap32(*(uint32_t*)(base + a)); }
uint32_t g_last_status = 1;
}  // namespace

X360_GUEST_ORIGINAL(82985E78)
X360_GUEST_ORIGINAL(8298FE60)

PPC_FUNC(sub_8298FE60) {
  P("movie %08X started (slot 9)", ctx.r3.u32);
  __imp__sub_8298FE60(ctx, base);
}

// The video-flag listener's event handler (event 13: 0 started, 1 ended; data+12 the path).
X360_GUEST_ORIGINAL(824ABBE8)
PPC_FUNC(sub_824ABBE8) {
  const uint32_t ev = ctx.r4.u32, sub = ctx.r5.u32, data = ctx.r6.u32;
  if (ev == 13) {
    const uint32_t s = data + 12;
    const uint32_t cap = L(base, s + 20), len = L(base, s + 16);
    const uint32_t text = cap >= 16 ? L(base, s) : s;
    char buf[80] = {};
    for (uint32_t i = 0; i < len && i < 79; ++i) buf[i] = char(base[text + i]);
    P("listener %08X event 13/%u '%s'", ctx.r3.u32, sub, buf);
  }
  __imp__sub_824ABBE8(ctx, base);
}

// The event dispatcher's post (vtable 8200227C slot 4): event, subtype.
X360_GUEST_ORIGINAL(82947780)
PPC_FUNC(sub_82947780) {
  static std::atomic<int> counts[64][16];
  const uint32_t ev = ctx.r4.u32, sub = ctx.r5.u32;
  const int c = (ev < 64 && sub < 16) ? ++counts[ev][sub] : 1;
  static const double from = std::getenv("PROBE_EV_FROM") ? std::atof(std::getenv("PROBE_EV_FROM")) : 1e9;
  static const double to = std::getenv("PROBE_EV_TO") ? std::atof(std::getenv("PROBE_EV_TO")) : 0;
  const double now = Now();
  if (c <= 3 || (c & (c - 1)) == 0 || (now >= from && now <= to && ev != 2))
    P("post event %u/%u #%d r6 %08X r7 %08X r8 %08X", ev, sub, c, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32);
  __imp__sub_82947780(ctx, base);
}

// The facade's next frame: 0x166600E8 not ready yet, 0x16660026 the end.
PPC_FUNC(sub_82985E78) {
  const uint32_t movie = ctx.r4.u32;
  __imp__sub_82985E78(ctx, base);
  if (ctx.r3.u32 != g_last_status) {
    P("next frame (movie handle %08X) -> %08X", movie, ctx.r3.u32);
    g_last_status = ctx.r3.u32;
  }
}

// The global manager at [0x82DC3888] (CNatalCameraManager's owner?): its busy state.
namespace {
void DescribeKmgr(uint8_t* base, char* out, size_t n) {
  const uint32_t k = L(base, 0x82DC3888);
  if (!k) {
    std::snprintf(out, n, "kmgr null");
    return;
  }
  std::snprintf(out, n, "kmgr %08X b1373 %u b1374 %u q544 %08X..%08X q576 %08X..%08X c1376 %d", k, base[k + 1373],
                base[k + 1374], L(base, k + 544), L(base, k + 548), L(base, k + 576), L(base, k + 580),
                int(L(base, k + 1376)));
}
}  // namespace

// CGame::LoadingUpdate(float, float): its flags at +349..+380.
X360_GUEST_ORIGINAL(826E35F0)
PPC_FUNC(sub_826E35F0) {
  static int n = 0;
  static uint8_t last[32];
  static char last_k[200];
  const uint32_t self = ctx.r3.u32;
  __imp__sub_826E35F0(ctx, base);
  if (!self) return;
  uint8_t cur[32];
  std::memcpy(cur, base + self + 349, 32);
  char k[200];
  DescribeKmgr(base, k, sizeof(k));
  ++n;
  if (n == 1 || std::memcmp(cur, last, 32) || std::strcmp(k, last_k) || (n % 1200) == 0) {
    char flags[200];
    int at = 0;
    for (int i = 0; i < 32; ++i) at += std::snprintf(flags + at, sizeof(flags) - at, "%s%u", i % 8 ? "" : " ", cur[i]);
    P("CGame %08X LoadingUpdate #%d flags349..380%s; %s", self, n, flags, k);
    std::memcpy(last, cur, 32);
    std::strcpy(last_k, k);
  }
}

// The race-start job (thread 21) and the Natal camera manager's init in it.
X360_GUEST_ORIGINAL(826D5358)
PPC_FUNC(sub_826D5358) {
  P("job 826D5358 begin r3 %08X r4 %08X r5 %08X r6 %08X", ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
  __imp__sub_826D5358(ctx, base);
  char k[200];
  DescribeKmgr(base, k, sizeof(k));
  P("job 826D5358 end -> %08X; %s", ctx.r3.u32, k);
}
X360_GUEST_ORIGINAL(82A689F8)
PPC_FUNC(sub_82A689F8) {
  const uint32_t self = ctx.r3.u32;
  P("natal init %08X begin r4 %08X r5 %u r6 %u r7 %u r8 %u r9 %u", self, ctx.r4.u32, ctx.r5.u32 & 0xFF,
    ctx.r6.u32 & 0xFF, ctx.r7.u32 & 0xFF, ctx.r8.u32 & 0xFF, ctx.r9.u32 & 0xFF);
  __imp__sub_82A689F8(ctx, base);
  P("natal init %08X end: available(+4408) %u event(+17012) %08X", self, base[self + 4408], L(base, self + 17012));
}
X360_GUEST_ORIGINAL(827C2470)
PPC_FUNC(sub_827C2470) {
  P("NuiInitialize(%08X, %08X)", ctx.r3.u32, ctx.r4.u32);
  if (std::getenv("PROBE_NUI_FAIL")) {
    ctx.r3.u64 = 0x8007048F;  // E_NUI_DEVICE_NOT_CONNECTED
    P("NuiInitialize -> %08X (forced)", ctx.r3.u32);
    return;
  }
  __imp__sub_827C2470(ctx, base);
  P("NuiInitialize -> %08X", ctx.r3.u32);
}

// CGame::Init's IsReady functor: obj->+152's states at +36/+40.
X360_GUEST_ORIGINAL(826DD3B8)
PPC_FUNC(sub_826DD3B8) {
  static uint32_t last = ~0u;
  const uint32_t o = L(base, ctx.r3.u32), x = o ? L(base, o + 152) : 0;
  __imp__sub_826DD3B8(ctx, base);
  const uint32_t key = (ctx.r3.u32 & 1) | (x ? (L(base, x + 36) << 8) | (L(base, x + 40) << 16) : 0);
  if (key != last) {
    P("CGame::Init IsReady -> %u (obj %08X +152 %08X: +36 %d +40 %d)", ctx.r3.u32 & 1, o, x, x ? int(L(base, x + 36)) : -1,
      x ? int(L(base, x + 40)) : -1);
    last = key;
  }
}

// The audio init functor's body (InitAudioFunctor in CGame::LoadingUpdate).
X360_GUEST_ORIGINAL(826E0968)
PPC_FUNC(sub_826E0968) {
  P("InitAudio %08X begin", ctx.r3.u32);
  __imp__sub_826E0968(ctx, base);
  P("InitAudio end");
}

// The resource load state machine (state at +508, sub-state +504; +512 type, +516 mode).
X360_GUEST_ORIGINAL(828513A0)
PPC_FUNC(sub_828513A0) {
  static std::atomic<int> lines{0};
  const uint32_t r = ctx.r3.u32;
  const uint32_t before = L(base, r + 508), sub_before = L(base, r + 504);
  __imp__sub_828513A0(ctx, base);
  const uint32_t after = L(base, r + 508), sub_after = L(base, r + 504);
  if ((before != after || sub_before != sub_after) && lines++ < 4000)
    P("res %08X state %u.%u -> %u.%u type %u mode %u +4 %08X", r, before, sub_before, after, sub_after, L(base, r + 512),
      L(base, r + 516), L(base, r + 4));
}

// The resource manager's update ([0x82DC3820] vtable slot 7): state +88 (3 = ready), pending +36, +60.
X360_GUEST_ORIGINAL(8239F2C8)
PPC_FUNC(sub_8239F2C8) {
  static int n = 0;
  static uint32_t last = ~0u;
  const uint32_t m = ctx.r3.u32;
  __imp__sub_8239F2C8(ctx, base);
  const uint32_t state = L(base, m + 88), pending = L(base, m + 36), busy = L(base, m + 60);
  const uint32_t key = state | (pending << 8) | (busy << 24);
  if (key != last || (++n % 1200) == 0) {
    uint32_t first = 0, obj = 0;
    const uint32_t head = L(base, m + 32);
    if (pending && head) {
      const uint32_t node = L(base, head);
      first = node;
      // node+8: a handle whose +28... is resolved by 828F93E0; log the raw words instead.
      obj = L(base, node + 8);
    }
    P("resmgr %08X update: state %u pending %u +60 %u first node %08X value %08X %08X %08X", m, state, pending, busy,
      first, obj, first ? L(base, first + 12) : 0, first ? L(base, first + 16) : 0);
    last = key;
  }
}
X360_GUEST_ORIGINAL(82426560)
PPC_FUNC(sub_82426560) {
  const uint32_t m = ctx.r3.u32;
  P("resmgr %08X request state %u flag %u (was %u, pending %u)", m, ctx.r4.u32, ctx.r5.u32 & 0xFF, L(base, m + 88),
    L(base, m + 36));
  __imp__sub_82426560(ctx, base);
  P("resmgr %08X request done: state %u pending %u", m, L(base, m + 88), L(base, m + 36));
}

// The race start: the job 826D5B38 (posted by the game loop), the game manager's 826D5660, CGame::Init.
#define PROBE_ENTER_EXIT(addr, label)                                                               \
  X360_GUEST_ORIGINAL(addr)                                                                         \
  PPC_FUNC(sub_##addr) {                                                                            \
    P("%s begin r3 %08X r4 %08X r5 %08X", label, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);               \
    __imp__sub_##addr(ctx, base);                                                                   \
    P("%s end -> %08X", label, ctx.r3.u32);                                                         \
  }
PROBE_ENTER_EXIT(826D5B38, "race start job 826D5B38")
PROBE_ENTER_EXIT(826D5660, "gamemgr start 826D5660")
PROBE_ENTER_EXIT(82499F60, "gamemgr check 82499F60")
PROBE_ENTER_EXIT(82434528, "game init 82434528")
PROBE_ENTER_EXIT(826E0E78, "CGame::Init 826E0E78")
X360_GUEST_ORIGINAL(82AABF00)
PPC_FUNC(sub_82AABF00) {
  static uint32_t last[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};
  const uint32_t which = ctx.r4.u32, lr = uint32_t(ctx.lr);
  __imp__sub_82AABF00(ctx, base);
  if (which < 8 && last[which] != ctx.r3.u32) {
    P("82AABF00(%08X, %u) -> %d (lr %08X)", ctx.r3.u32, which, int(ctx.r3.u32), lr);
    last[which] = ctx.r3.u32;
  }
}

// The fiber scheduler's jobs: post (8294D940: queue, 0, functor) and run (82949A10: functor; +16 the
// implementation, whose vtable slot 1 is the call).
X360_GUEST_ORIGINAL(8294D940)
PPC_FUNC(sub_8294D940) {
  const uint32_t impl = ctx.r5.u32 ? L(base, ctx.r5.u32 + 16) : 0;
  const uint32_t vt = impl ? L(base, impl) : 0;
  P("job post queue %08X impl %08X vtable %08X call %08X arg %08X", ctx.r3.u32, impl, vt, vt ? L(base, vt + 4) : 0,
    impl ? L(base, impl + 4) : 0);
  __imp__sub_8294D940(ctx, base);
}
X360_GUEST_ORIGINAL(82949A10)
PPC_FUNC(sub_82949A10) {
  const uint32_t impl = L(base, ctx.r3.u32 + 16);
  const uint32_t vt = impl ? L(base, impl) : 0;
  const uint32_t call = vt ? L(base, vt + 4) : 0;
  const uint32_t kthread = L(base, ctx.r13.u32 + 0x100);
  P("job run begin impl %08X vtable %08X call %08X arg %08X fiber %08X", impl, vt, call, impl ? L(base, impl + 4) : 0,
    L(base, kthread + 0x164));
  __imp__sub_82949A10(ctx, base);
  P("job run end call %08X", call);
}
