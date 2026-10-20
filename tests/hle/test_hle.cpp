// Tests the HLE bridge the same way recompiled code uses it: by calling __imp__<Export>(ctx, base)
// with arguments in PPC registers and guest pointers into a real 4 GiB guest address space.
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "x360/hle.h"
#include "x360/kernel.h"
#include "x360/memory.h"
#include "x360/profile.h"

// Declared exactly as XenonRecomp's generated ppc_recomp_shared.h does.
PPC_EXTERN_FUNC(__imp__RtlInitAnsiString);
PPC_EXTERN_FUNC(__imp__RtlInitUnicodeString);
PPC_EXTERN_FUNC(__imp__RtlCompareMemory);
PPC_EXTERN_FUNC(__imp__RtlCompareMemoryUlong);
PPC_EXTERN_FUNC(__imp__RtlFillMemoryUlong);
PPC_EXTERN_FUNC(__imp__RtlUpperChar);
PPC_EXTERN_FUNC(__imp__KeQueryPerformanceFrequency);
PPC_EXTERN_FUNC(__imp__KeGetCurrentProcessType);
PPC_EXTERN_FUNC(__imp__XeCryptAesKey);
PPC_EXTERN_FUNC(__imp__XeCryptAesCbc);
PPC_EXTERN_FUNC(__imp__XeCryptAesEcb);
PPC_EXTERN_FUNC(__imp__sprintf);
PPC_EXTERN_FUNC(__imp__vswprintf);
PPC_EXTERN_FUNC(__imp__KeBlowFuses);  // not implemented: must resolve to the weak stub
PPC_EXTERN_FUNC(__imp__XamShowRecentMessageUI);

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
  kernel::g_base = base;  // exports reach guest memory through the kernel's pointer helpers

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

  // AES-128 (NIST SP 800-38A F.2.1/F.2.2, two CBC blocks): encrypt, then decrypt in place.
  {
    const uint8_t key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    const uint8_t plain[32] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
                               0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51};
    const uint8_t cipher[32] = {0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46, 0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
                                0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee, 0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2};
    const guest_addr state = kHeap + 0x1000, key_at = kHeap + 0x1200, data = kHeap + 0x1300, out = kHeap + 0x1400,
                     feed = kHeap + 0x1500;
    std::memcpy(mem.translate<uint8_t>(key_at), key, 16);
    std::memcpy(mem.translate<uint8_t>(data), plain, 32);
    for (int i = 0; i < 16; ++i) mem.translate<uint8_t>(feed)[i] = uint8_t(i);
    ctx.r3.u64 = state;
    ctx.r4.u64 = key_at;
    __imp__XeCryptAesKey(ctx, base);
    ctx.r3.u64 = state, ctx.r4.u64 = data, ctx.r5.u64 = 32, ctx.r6.u64 = out, ctx.r7.u64 = feed, ctx.r8.u64 = 1;
    __imp__XeCryptAesCbc(ctx, base);
    CHECK(std::memcmp(mem.translate<uint8_t>(out), cipher, 32) == 0);
    CHECK(std::memcmp(mem.translate<uint8_t>(feed), cipher + 16, 16) == 0);  // chains on the last block
    for (int i = 0; i < 16; ++i) mem.translate<uint8_t>(feed)[i] = uint8_t(i);
    ctx.r3.u64 = state, ctx.r4.u64 = out, ctx.r5.u64 = 32, ctx.r6.u64 = out, ctx.r7.u64 = feed, ctx.r8.u64 = 0;
    __imp__XeCryptAesCbc(ctx, base);
    CHECK(std::memcmp(mem.translate<uint8_t>(out), plain, 32) == 0);
    // ECB of the first block is CBC with a zero IV.
    std::memcpy(mem.translate<uint8_t>(data), plain, 16);
    for (int i = 0; i < 16; ++i) mem.translate<uint8_t>(data)[i] ^= uint8_t(i);
    ctx.r3.u64 = state, ctx.r4.u64 = data, ctx.r5.u64 = out, ctx.r6.u64 = 1;
    __imp__XeCryptAesEcb(ctx, base);
    CHECK(std::memcmp(mem.translate<uint8_t>(out), cipher, 16) == 0);
  }

  // sprintf (variadic: arguments in r5..): in the narrow form %s is narrow and %S wide.
  {
    const guest_addr format = kHeap + 0x600, narrow = kHeap + 0x680, wide = kHeap + 0x6C0, out = kHeap + 0x700;
    std::strcpy(mem.translate<char>(format), "%d|%04X|%s|%S|%c");
    std::strcpy(mem.translate<char>(narrow), "mw3");
    const char* ok = "Ok";
    for (int i = 0; i < 3; ++i) mem.write<uint16_t>(wide + i * 2, uint16_t(ok[i]));
    ctx.r3.u64 = out, ctx.r4.u64 = format, ctx.r5.s64 = -5, ctx.r6.u64 = 0xBEEF, ctx.r7.u64 = narrow;
    ctx.r8.u64 = wide, ctx.r9.u64 = 'z';
    __imp__sprintf(ctx, base);
    CHECK(std::strcmp(mem.translate<char>(out), "-5|BEEF|mw3|Ok|z") == 0);
    CHECK(ctx.r3.u32 == 16);

    // vswprintf(buffer, format, va_list): UTF-16 format and result; %s is wide, %hs narrow. A va_list
    // holds 8-byte slots.
    const guest_addr wformat = kHeap + 0x800, list = kHeap + 0x900, wout = kHeap + 0xA00;
    const char* spec = "%s-%hs-%d";
    for (size_t i = 0; i <= std::strlen(spec); ++i) mem.write<uint16_t>(wformat + uint32_t(i) * 2, uint16_t(spec[i]));
    mem.write<uint64_t>(list + 0, wide);
    mem.write<uint64_t>(list + 8, narrow);
    mem.write<uint64_t>(list + 16, 42);
    ctx.r3.u64 = wout, ctx.r4.u64 = wformat, ctx.r5.u64 = list;
    __imp__vswprintf(ctx, base);
    const char* expected = "Ok-mw3-42";
    bool same = true;
    for (size_t i = 0; i <= std::strlen(expected); ++i)
      same &= mem.read<uint16_t>(wout + uint32_t(i) * 2) == uint16_t(expected[i]);
    CHECK(same);
    CHECK(ctx.r3.u32 == 9);
  }

  // The player profile (x360/profile.h): written by the launcher, read by every game.
  {
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "anyx360_test_profile";
    fs::remove_all(folder);
    profile::Profile written;
    written.names[0] = "  Daniel  B!rr@to ";
    written.names[1] = "Ana";
    written.language = "es";
    CHECK(profile::Write(profile::File(folder), written));
    const profile::Profile read = profile::Read(profile::File(folder));
    CHECK(read.names[0] == "Daniel Brrto");  // a gamertag's letters, digits and single spaces
    CHECK(read.names[1] == "Ana" && read.names[2].empty() && read.names[3].empty());
    CHECK(read.language == "es");
    CHECK(profile::CleanName("ABCDEFGHIJKLMNOPQRS") == "ABCDEFGHIJKLMNO");
    CHECK(profile::Read(folder / "missing.txt").language == "auto");
    CHECK(profile::Resolve("es").language == 5 && profile::Resolve("en").language == 1);
    const uint32_t windows = profile::WindowsLanguage();
    CHECK(profile::Resolve("auto").language == (windows ? windows : 1));
    const uint32_t country = profile::Resolve("es").country;
    CHECK(country >= 1 && country <= 110);
    fs::remove_all(folder);
  }

  // Weak stubs: link, log once, return 0
  ctx.r3.u64 = 0x1234;
  __imp__KeBlowFuses(ctx, base);
  CHECK(ctx.r3.u64 == 0);
  ctx.r3.u64 = 0x1234;
  __imp__XamShowRecentMessageUI(ctx, base);
  CHECK(ctx.r3.u64 == 0);

  std::printf("\n%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
