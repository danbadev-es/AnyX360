// TEMPORARY probe (not for commit): the first race's loading screen (IELoading).
#include <cstdio>
#include <cstring>
#include "x360/hle.h"

X360_GUEST_ORIGINAL(825482D0)
X360_GUEST_ORIGINAL(826460F8)
X360_GUEST_ORIGINAL(823CD428)

static uint32_t L32(uint8_t* base, uint32_t a) { return __builtin_bswap32(*(uint32_t*)(base + a)); }

PPC_FUNC(sub_825482D0) {
  static int n = 0;
  const uint32_t self = ctx.r3.u32;
  if ((n++ % 120) == 0)
    std::fprintf(stderr, "PROBE IELoading::Update %08X task %08X timer@544 %08X %08X\n", self, L32(base, self + 352),
                 L32(base, self + 544), L32(base, self + 548));
  __imp__sub_825482D0(ctx, base);
}

PPC_FUNC(sub_826460F8) {
  static int n = 0;
  const uint32_t self = ctx.r3.u32;
  const uint64_t lr = ctx.lr;
  __imp__sub_826460F8(ctx, base);
  if ((n++ % 120) == 0) {
    const uint32_t name = L32(base, 0x82DDF1D8);
    char text[64] = {};
    if (name) std::memcpy(text, base + name, 48);
    for (char& c : text) if (c && (c < 32 || c > 126)) c = '.';
    std::fprintf(stderr, "PROBE done? %08X -> %u (lr %08llX) name %08X '%s' arg %08X ui %08X\n", self, ctx.r3.u32,
                 (unsigned long long)lr, name, text, L32(base, self + 132), L32(base, self + 120));
  }
}

PPC_FUNC(sub_823CD428) {
  std::fprintf(stderr, "PROBE CContentInstaller start %08X\n", ctx.r3.u32);
  __imp__sub_823CD428(ctx, base);
}
