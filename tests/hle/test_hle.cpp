// Tests the HLE bridge the same way recompiled code uses it: by calling __imp__<Export>(ctx, base)
// with arguments in PPC registers and guest pointers into a real 4 GiB guest address space.
#include <cstdio>
#include <cstring>

#include "x360/hle.h"
#include "x360/memory.h"

// Declared exactly as XenonRecomp's generated ppc_recomp_shared.h does.
PPC_EXTERN_FUNC(__imp__RtlInitAnsiString);
PPC_EXTERN_FUNC(__imp__RtlInitUnicodeString);
PPC_EXTERN_FUNC(__imp__RtlCompareMemory);
PPC_EXTERN_FUNC(__imp__RtlCompareMemoryUlong);
PPC_EXTERN_FUNC(__imp__RtlFillMemoryUlong);
PPC_EXTERN_FUNC(__imp__RtlUpperChar);
PPC_EXTERN_FUNC(__imp__KeQueryPerformanceFrequency);
PPC_EXTERN_FUNC(__imp__KeGetCurrentProcessType);
PPC_EXTERN_FUNC(__imp__XeCryptRandom);  // not implemented: must resolve to the weak stub
PPC_EXTERN_FUNC(__imp__XamGetCurrentTitleId);

// The kernel's loader fills the function table from this generated array; tests have no game.
PPCFuncMapping PPCFuncMappings[] = {{0, nullptr}};

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (cond) {                                                        \
      std::printf("PASS %s\n", #cond);                                 \
    } else {                                                           \
      std::printf("FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__);    \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

int main() {
  using namespace x360;
  GuestMemory mem;
  CHECK(mem.valid());
  constexpr guest_addr kHeap = 0x40000000;
  CHECK(mem.commit(kHeap, 0x10000));
  uint8_t* base = mem.base();
  hle::set_guest_base(base);

  alignas(64) PPCContext ctx{};

  // RtlInitAnsiString(ANSI_STRING* dst = kHeap, const char* src = kHeap + 0x100)
  std::strcpy(mem.translate<char>(kHeap + 0x100), "game:\\default.xex");
  ctx.r3.u64 = kHeap;
  ctx.r4.u64 = kHeap + 0x100;
  __imp__RtlInitAnsiString(ctx, base);
  CHECK(mem.read<uint16_t>(kHeap + 0) == 17);
  CHECK(mem.read<uint16_t>(kHeap + 2) == 18);
  CHECK(mem.read<uint32_t>(kHeap + 4) == kHeap + 0x100);

  // NULL source
  ctx.r3.u64 = kHeap;
  ctx.r4.u64 = 0;
  __imp__RtlInitAnsiString(ctx, base);
  CHECK(mem.read<uint16_t>(kHeap + 0) == 0 && mem.read<uint32_t>(kHeap + 4) == 0);

  // RtlInitUnicodeString with a big-endian UTF-16 "abc"
  for (int i = 0; i < 4; ++i) mem.write<uint16_t>(kHeap + 0x200 + i * 2, i < 3 ? uint16_t('a' + i) : 0);
  ctx.r3.u64 = kHeap + 0x10;
  ctx.r4.u64 = kHeap + 0x200;
  __imp__RtlInitUnicodeString(ctx, base);
  CHECK(mem.read<uint16_t>(kHeap + 0x10) == 6);
  CHECK(mem.read<uint16_t>(kHeap + 0x12) == 8);

  // RtlCompareMemory: first 5 bytes equal
  std::memcpy(mem.translate(kHeap + 0x300), "HELLOxyz", 8);
  std::memcpy(mem.translate(kHeap + 0x400), "HELLOabc", 8);
  ctx.r3.u64 = kHeap + 0x300;
  ctx.r4.u64 = kHeap + 0x400;
  ctx.r5.u64 = 8;
  __imp__RtlCompareMemory(ctx, base);
  CHECK(ctx.r3.u32 == 5);

  // RtlFillMemoryUlong then RtlCompareMemoryUlong (pattern is a guest-order dword)
  ctx.r3.u64 = kHeap + 0x500;
  ctx.r4.u64 = 16;
  ctx.r5.u64 = 0xDEADBEEF;
  __imp__RtlFillMemoryUlong(ctx, base);
  CHECK(mem.read<uint32_t>(kHeap + 0x50C) == 0xDEADBEEF);
  CHECK(mem.translate(kHeap + 0x500)[0] == 0xDE);  // stored big-endian
  ctx.r3.u64 = kHeap + 0x500;
  ctx.r4.u64 = 20;
  ctx.r5.u64 = 0xDEADBEEF;
  __imp__RtlCompareMemoryUlong(ctx, base);
  CHECK(ctx.r3.u32 == 16);

  // Narrow integer argument: only the low byte is meaningful
  ctx.r3.u64 = 0xFFFFFF00u | 'q';
  __imp__RtlUpperChar(ctx, base);
  CHECK(ctx.r3.u64 == 'Q');

  __imp__KeQueryPerformanceFrequency(ctx, base);
  CHECK(ctx.r3.u64 == 50'000'000);
  __imp__KeGetCurrentProcessType(ctx, base);
  CHECK(ctx.r3.u64 == 1);

  // Weak stubs: link, log once, return 0
  ctx.r3.u64 = 0x1234;
  __imp__XeCryptRandom(ctx, base);
  CHECK(ctx.r3.u64 == 0);
  ctx.r3.u64 = 0x1234;
  __imp__XamGetCurrentTitleId(ctx, base);
  CHECK(ctx.r3.u64 == 0);

  std::printf("\n%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
