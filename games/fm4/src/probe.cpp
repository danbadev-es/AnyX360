// TEMPORARY probe (not for commit): CWMVMovie's update and the end of the welcome video.
#include <atomic>
#include <cstdio>
#include <cstring>
#include "x360/hle.h"

X360_GUEST_ORIGINAL(8298FB40)
X360_GUEST_ORIGINAL(82985E78)
X360_GUEST_ORIGINAL(823A9740)
X360_GUEST_ORIGINAL(824B2A30)
X360_GUEST_ORIGINAL(829860E8)
X360_GUEST_ORIGINAL(8298FE60)

namespace {
thread_local uint32_t g_movie = 0;
thread_local int g_slot[3];
std::atomic<int> g_updates{0};
uint32_t g_last_status = 1;
int g_last_slots = -1;
}  // namespace

// The update (CWMVMovie vtable slot 7).
PPC_FUNC(sub_8298FB40) {
  g_movie = ctx.r3.u32;
  g_slot[0] = g_slot[1] = g_slot[2] = -1;
  __imp__sub_8298FB40(ctx, base);
  const int slots = (g_slot[0] + 1) * 100 + (g_slot[1] + 1) * 10 + (g_slot[2] + 1);
  const int n = ++g_updates;
  if (slots != g_last_slots || (n % 600) == 0) {
    std::fprintf(stderr, "PROBE movie %08X update #%d: slot5 %d slot10 %d slot14 %d\n", g_movie, n, g_slot[0], g_slot[1],
                 g_slot[2]);
    g_last_slots = slots;
  }
  g_movie = 0;
}

PPC_FUNC(sub_8298FE60) {
  std::fprintf(stderr, "PROBE movie %08X started (slot 9)\n", ctx.r3.u32);
  __imp__sub_8298FE60(ctx, base);
}

#define SLOT(addr, i)                                         \
  PPC_FUNC(sub_##addr) {                                      \
    const uint32_t self = ctx.r3.u32;                         \
    __imp__sub_##addr(ctx, base);                             \
    if (self == g_movie) g_slot[i] = int(ctx.r3.u32 & 0xFF);  \
  }
SLOT(823A9740, 0)
SLOT(824B2A30, 1)
SLOT(829860E8, 2)

// The video-flag listener's event handler (event 13: 0 started, 1 ended; data+12 the path).
X360_GUEST_ORIGINAL(824ABBE8)
PPC_FUNC(sub_824ABBE8) {
  const uint32_t ev = ctx.r4.u32, sub = ctx.r5.u32, data = ctx.r6.u32;
  if (ev == 13) {
    auto L = [&](uint32_t a) { return __builtin_bswap32(*(uint32_t*)(base + a)); };
    const uint32_t s = data + 12;
    const uint32_t cap = L(s + 20), len = L(s + 16);
    const uint32_t text = cap >= 16 ? L(s) : s;
    char buf[80] = {};
    for (uint32_t i = 0; i < len && i < 79; ++i) buf[i] = char(base[text + i]);
    std::fprintf(stderr, "PROBE listener %08X event 13/%u '%s' (len %u)\n", ctx.r3.u32, sub, buf, len);
  }
  const uint32_t self = ctx.r3.u32;
  __imp__sub_824ABBE8(ctx, base);
  if (ev == 13) {
    auto L = [&](uint32_t a) { return __builtin_bswap32(*(uint32_t*)(base + a)); };
    for (uint32_t e = L(self + 8); e && e < L(self + 12); e += 32) {
      const uint32_t cap = L(e + 20), len = L(e + 16);
      const uint32_t text = cap >= 16 ? L(e) : e;
      char buf[80] = {};
      for (uint32_t i = 0; i < len && i < 79; ++i) buf[i] = char(base[text + i]);
      std::fprintf(stderr, "PROBE   entry %08X '%s' flag %u\n", e, buf, base[e + 28]);
    }
  }
}

// The query: container (r3), key string (r4).
X360_GUEST_ORIGINAL(824AB458)
PPC_FUNC(sub_824AB458) {
  static int n = 0;
  const uint32_t self = ctx.r3.u32;
  __imp__sub_824AB458(ctx, base);
  if ((n++ % 600) == 0) {
    auto L = [&](uint32_t a) { return __builtin_bswap32(*(uint32_t*)(base + a)); };
    std::fprintf(stderr, "PROBE query container %08X -> %u; entries %08X..%08X\n", self, ctx.r3.u32, L(self + 20), L(self + 24));
    for (uint32_t e = L(self + 20); e && e < L(self + 24); e += 32) {
      const uint32_t cap = L(e + 20), len = L(e + 16);
      const uint32_t text = cap >= 16 ? L(e) : e;
      char buf[80] = {};
      for (uint32_t i = 0; i < len && i < 79; ++i) buf[i] = char(base[text + i]);
      std::fprintf(stderr, "PROBE   q-entry %08X '%s' flag %u\n", e, buf, base[e + 28]);
    }
  }
}

// ForzaUI::IELoading's update: the pending video at +352, the timer at +544.
X360_GUEST_ORIGINAL(825482D0)
PPC_FUNC(sub_825482D0) {
  static int n = 0;
  static uint32_t last_task = 1;
  const uint32_t self = ctx.r3.u32;
  auto L = [&](uint32_t a) { return __builtin_bswap32(*(uint32_t*)(base + a)); };
  const uint32_t task = L(self + 352);
  if (task != last_task || (n % 1200) == 0)
    std::fprintf(stderr, "PROBE IELoading %08X update #%d task %08X +540 %08X +544 %08X %08X %08X\n", self, n, task,
                 L(self + 540), L(self + 544), L(self + 548), L(self + 552));
  last_task = task;
  ++n;
  __imp__sub_825482D0(ctx, base);
}

// The event dispatcher's post (vtable 8200227C slot 4): event, subtype.
X360_GUEST_ORIGINAL(82947780)
PPC_FUNC(sub_82947780) {
  static std::atomic<int> counts[64][16];
  const uint32_t ev = ctx.r4.u32, sub = ctx.r5.u32;
  const int c = (ev < 64 && sub < 16) ? ++counts[ev][sub] : 1;
  if (c <= 3 || (c & (c - 1)) == 0)
    std::fprintf(stderr, "PROBE post event %u/%u #%d r6 %08X r7 %08X r8 %08X\n", ev, sub, c, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32);
  __imp__sub_82947780(ctx, base);
}

// The main loop (822DADD8): its object's flags, logged when they change.
X360_GUEST_ORIGINAL(822DADD8)
PPC_FUNC(sub_822DADD8) {
  static uint64_t last = ~0ull;
  static uint32_t last_ptr = 1;
  const uint32_t self = ctx.r3.u32;
  const uint16_t offsets[] = {2080, 2812, 2813, 2814, 2815, 2816, 2817, 2844, 3050, 3973, 3975, 4144};
  uint64_t flags = 0;
  for (int i = 0; i < 12; ++i) flags |= uint64_t(base[self + offsets[i]] != 0) << i;
  const uint32_t ptr = __builtin_bswap32(*(uint32_t*)(base + self + 2804));
  if (flags != last || ptr != last_ptr) {
    std::fprintf(stderr, "PROBE main %08X flags", self);
    for (int i = 0; i < 12; ++i) std::fprintf(stderr, " %u:%u", offsets[i], base[self + offsets[i]]);
    std::fprintf(stderr, " +2804 %08X\n", ptr);
    last = flags;
    last_ptr = ptr;
  }
  __imp__sub_822DADD8(ctx, base);
}

// The facade's next frame: 0x166600E8 not ready yet, 0x16660026 the end.
PPC_FUNC(sub_82985E78) {
  const uint32_t movie = ctx.r4.u32;
  __imp__sub_82985E78(ctx, base);
  if (ctx.r3.u32 != g_last_status) {
    std::fprintf(stderr, "PROBE next frame (movie handle %08X) -> %08X\n", movie, ctx.r3.u32);
    g_last_status = ctx.r3.u32;
  }
}
