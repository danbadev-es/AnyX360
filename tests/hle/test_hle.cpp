// Tests the HLE bridge the same way recompiled code uses it: by calling __imp__<Export>(ctx, base)
// with arguments in PPC registers and guest pointers into a real 4 GiB guest address space.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include "x360/hle.h"
#include "x360/kernel.h"
#include "x360/memory.h"
#include "x360/profile.h"
#include "x360/profiles.h"
#include "x360/settings.h"
#include "../../runtime/src/input/input_internal.h"
#include "avatar_model.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

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
PPC_EXTERN_FUNC(__imp__XeCryptShaInit);
PPC_EXTERN_FUNC(__imp__XeCryptShaUpdate);
PPC_EXTERN_FUNC(__imp__XeCryptShaFinal);
PPC_EXTERN_FUNC(__imp__XeCryptSha256Init);
PPC_EXTERN_FUNC(__imp__XeCryptSha256Update);
PPC_EXTERN_FUNC(__imp__XeCryptSha256Final);
PPC_EXTERN_FUNC(__imp__XeCryptSha384Init);
PPC_EXTERN_FUNC(__imp__XeCryptSha384Update);
PPC_EXTERN_FUNC(__imp__XeCryptSha384Final);
PPC_EXTERN_FUNC(__imp__XeCryptSha512Init);
PPC_EXTERN_FUNC(__imp__XeCryptSha512Update);
PPC_EXTERN_FUNC(__imp__XeCryptSha512Final);
PPC_EXTERN_FUNC(__imp__sprintf);
PPC_EXTERN_FUNC(__imp__vswprintf);
PPC_EXTERN_FUNC(__imp__XamLoaderSetLaunchData);
PPC_EXTERN_FUNC(__imp__XamLoaderGetLaunchDataSize);
PPC_EXTERN_FUNC(__imp__XamLoaderGetLaunchData);
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

  // Incremental SHA-1/SHA-2 (FIPS 180 examples), the message fed in two parts: the context is in guest
  // memory between the calls. The 56-byte message needs a second block for its padding.
  {
    using ShaFunc = void (*)(PPCContext&, uint8_t*);
    struct Case {
      ShaFunc init, update, final;
      const char* message;
      const char* digest;
    };
    const char* two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const Case cases[] = {
        {__imp__XeCryptShaInit, __imp__XeCryptShaUpdate, __imp__XeCryptShaFinal, "abc",
         "a9993e364706816aba3e25717850c26c9cd0d89d"},
        {__imp__XeCryptShaInit, __imp__XeCryptShaUpdate, __imp__XeCryptShaFinal, two_blocks,
         "84983e441c3bd26ebaae4aa1f95129e5e54670f1"},
        {__imp__XeCryptSha256Init, __imp__XeCryptSha256Update, __imp__XeCryptSha256Final, "abc",
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {__imp__XeCryptSha256Init, __imp__XeCryptSha256Update, __imp__XeCryptSha256Final, two_blocks,
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {__imp__XeCryptSha384Init, __imp__XeCryptSha384Update, __imp__XeCryptSha384Final, "abc",
         "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"},
        {__imp__XeCryptSha512Init, __imp__XeCryptSha512Update, __imp__XeCryptSha512Final, "abc",
         "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
         "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
    };
    const guest_addr context = kHeap + 0x1800, message = kHeap + 0x1900, out = kHeap + 0x1A00;
    for (const Case& c : cases) {
      const uint32_t length = uint32_t(std::strlen(c.message)), first = length / 2;
      std::memcpy(mem.translate<char>(message), c.message, length);
      ctx.r3.u64 = context;
      c.init(ctx, base);
      ctx.r3.u64 = context, ctx.r4.u64 = message, ctx.r5.u64 = first;
      c.update(ctx, base);
      ctx.r3.u64 = context, ctx.r4.u64 = message + first, ctx.r5.u64 = length - first;
      c.update(ctx, base);
      ctx.r3.u64 = context, ctx.r4.u64 = out, ctx.r5.u64 = 64;
      c.final(ctx, base);
      char hex[129] = {};
      const uint32_t size = uint32_t(std::strlen(c.digest)) / 2;
      for (uint32_t i = 0; i < size; ++i) std::snprintf(hex + i * 2, 3, "%02x", mem.translate<uint8_t>(out)[i]);
      CHECK(std::strcmp(hex, c.digest) == 0);
    }
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

  // Launch data: a title reads back what it set; a short buffer gets what fits, and that is a
  // success (Xenia's behaviour).
  {
    const guest_addr data = kHeap + 0xB00, size = kHeap + 0xB80, out = kHeap + 0xC00;
    std::memcpy(mem.translate(data), "launch-data", 12);
    ctx.r3.u64 = data, ctx.r4.u64 = 12;
    __imp__XamLoaderSetLaunchData(ctx, base);
    CHECK(ctx.r3.u32 == 0);
    ctx.r3.u64 = size;
    __imp__XamLoaderGetLaunchDataSize(ctx, base);
    CHECK(ctx.r3.u32 == 0 && mem.read<uint32_t>(size) == 12);
    std::memset(mem.translate(out), 0, 16);
    ctx.r3.u64 = out, ctx.r4.u64 = 6;
    __imp__XamLoaderGetLaunchData(ctx, base);
    CHECK(ctx.r3.u32 == 0 && std::memcmp(mem.translate(out), "launch\0", 7) == 0);
  }

  // A named host thread's call stack (the stall dump prints the GPU command processor's): the thread
  // is stopped only while it is unwound, and runs on afterwards.
  {
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> ticks{0};
    std::thread sleeper([&] {
      SetThreadDescription(GetCurrentThread(), L"stack test");
      while (!stop) {
        ++ticks;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    kernel::PrintHostThreadStack(L"stack test");
    uint64_t before = ticks;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(ticks > before);
    stop = true;
    sleeper.join();
  }

  // Who is signed in (x360/profile.h): written by the launcher and the sign-in picker, read by
  // every game. And the global profiles (x360/profiles.h).
  {
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "anyx360_test_profile";
    fs::remove_all(folder);
    profile::SignIn written;
    written.slots[0] = 0xE000000012345678ull;
    written.slots[2] = 0xE0003A41C29F0B57ull;
    written.language = "es";
    CHECK(profile::Write(profile::File(folder), written));
    const profile::SignIn read = profile::Read(profile::File(folder));
    CHECK(read.slots[0] == 0xE000000012345678ull && read.slots[1] == 0 && read.slots[2] == 0xE0003A41C29F0B57ull);
    CHECK(read.language == "es" && !read.legacy);
    CHECK(profile::CleanName("  Daniel  B!rr@to ") == "Daniel Brrto");  // a gamertag's letters, digits and single spaces
    CHECK(profile::CleanName("ABCDEFGHIJKLMNOPQRS") == "ABCDEFGHIJKLMNO");
    CHECK(profile::Read(folder / "missing.txt").language == "auto");
    CHECK(profile::Resolve("es").language == 5 && profile::Resolve("en").language == 1);
    const uint32_t windows = profile::WindowsLanguage();
    CHECK(profile::Resolve("auto").language == (windows ? windows : 1));
    const uint32_t country = profile::Resolve("es").country;
    CHECK(country >= 1 && country <= 110);
    fs::remove_all(folder);

    // A profile.txt from before 0.20.0: its names become profiles with the XUIDs they played with.
    fs::create_directories(folder);
    std::ofstream(profile::File(folder), std::ios::binary) << "player1 = Daniel\nplayer2 =\nplayer3 = Ana\nlanguage = es\n";
    const profile::SignIn migrated = profiles::LoadSignIn(folder, false);
    CHECK(migrated.slots[0] == profiles::kLegacyXuid && migrated.slots[1] == 0 &&
          migrated.slots[2] == profiles::kLegacyXuid + 2 && migrated.slots[3] == 0);
    CHECK(fs::exists(folder / "profile.txt.before-0.20.0"));
    const auto daniel = profiles::Load(folder, profiles::kLegacyXuid);
    const auto ana = profiles::Load(folder, profiles::kLegacyXuid + 2);
    CHECK(daniel && daniel->gamertag == "Daniel" && daniel->language == "es" && !daniel->created.empty());
    CHECK(ana && ana->gamertag == "Ana");
    CHECK(!profiles::Load(folder, profiles::kLegacyXuid + 1) && profiles::List(folder).size() == 2);
    CHECK(profile::Read(profile::File(folder)).slots[2] == profiles::kLegacyXuid + 2);  // rewritten
    CHECK(!profile::Read(profile::File(folder)).legacy);
    // Once only: a profile deleted later isn't made again.
    CHECK(profiles::Remove(folder, profiles::kLegacyXuid + 2));
    CHECK(profiles::LoadSignIn(folder, true).slots[2] == 0 && profiles::List(folder).size() == 1);
    CHECK(fs::exists(profiles::ProfileFolder(folder, profiles::kLegacyXuid + 2) / "profile.deleted.json"));

    // Saves: the first offline XUID keeps the game's root folder, the others players\<XUID>.
    const fs::path user = folder / "GearsOfWar3";
    CHECK(profiles::UserFolder(user, profiles::kLegacyXuid) == user);
    CHECK(profiles::UserFolder(user, profiles::kLegacyXuid + 1) == user / "players" / "E000000012345679");
    CHECK(profiles::UserFolder(user, 0xE0003A41C29F0B57ull) == user / "players" / "E0003A41C29F0B57");

    // Time played, per profile and title.
    CHECK(profiles::AddPlayTime(folder, profiles::kLegacyXuid, 0x4D5308AB, "Gears of War 3", 61));
    CHECK(profiles::AddPlayTime(folder, profiles::kLegacyXuid, 0x4D5308AB, "", 60));
    const auto played = profiles::ReadTitle(folder, profiles::kLegacyXuid, 0x4D5308AB);
    CHECK(played && played->seconds == 121 && played->title == "Gears of War 3" && played->last_played.size() == 20);
    CHECK(!profiles::ReadTitle(folder, profiles::kLegacyXuid, 0x415608CB));
    fs::remove_all(folder);

    // A first start without profile.txt: a profile for the launcher's "Who's playing?", nobody signed in.
    CHECK(profiles::LoadSignIn(folder, false).slots[0] == 0 && !fs::exists(profiles::ProfilesFolder(folder)));
    CHECK(profiles::LoadSignIn(folder, true).slots[0] == 0);
    const auto first = profiles::List(folder);
    CHECK(first.size() == 1 && first[0].xuid == profiles::kLegacyXuid && first[0].gamertag == "Player");
    fs::remove_all(folder);
    // Player 2 played without a name (a game has saves in players\<their XUID>): "Player 2".
    fs::create_directories(folder / "BlackOps2" / "players" / "E000000012345679" / "profile");
    CHECK(profiles::LoadSignIn(folder, true).slots[1] == 0);
    const auto with_saves = profiles::List(folder);
    CHECK(with_saves.size() == 2 && with_saves[1].xuid == profiles::kLegacyXuid + 1 &&
          with_saves[1].gamertag == "Player 2");
    fs::remove_all(folder);
  }

  // Mouse look: the mouse's speed as the right stick.
  {
    settings::InputSettings in;  // the defaults
    double rx, ry;
    input::detail::MouseLook(0, 0, false, in, rx, ry);
    CHECK(rx == 0 && ry == 0);
    // A slow movement (400 counts/s, about 1.3 cm/s at 800 DPI) deflects the stick well past the
    // anti dead zone; with the old linear mapping (sensitivity 1) it was 0.325.
    input::detail::MouseLook(400, 0, false, in, rx, ry);
    CHECK(rx > 0.45 && rx < 0.55 && ry == 0);
    input::detail::MouseLook(100000, 0, false, in, rx, ry);
    CHECK(std::abs(rx - 1.0) < 1e-9);  // never past the whole stick
    double aim_x, aim_y;
    input::detail::MouseLook(400, 0, true, in, aim_x, aim_y);
    CHECK(aim_x == rx || std::abs(aim_x - 0.5) < 0.05);  // aiming: the games slow it themselves
    input::detail::MouseLook(0, 400, false, in, rx, ry);
    CHECK(ry < 0);  // the mouse down looks down
    in.invert_y = true;
    input::detail::MouseLook(0, 400, false, in, rx, ry);
    CHECK(ry > 0);
  }

  // Settings files from version 1 still at its mouse defaults take the new ones; chosen values stay.
  {
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "anyx360_test_settings";
    fs::remove_all(folder);
    fs::create_directories(folder);
    std::ofstream(folder / "settings.toml") << "version = 1\n[input]\nsensitivity = 1.0\naim_sensitivity = 0.45\n";
    settings::SetDefaults(settings::Settings{});
    settings::Load(folder / "settings.toml");
    const settings::Settings loaded = settings::Get();
    CHECK(loaded.input.sensitivity == settings::InputSettings{}.sensitivity);  // was the old default
    CHECK(loaded.input.aim_sensitivity == 0.45f);                              // chosen: kept
    CHECK(loaded.version == settings::Settings{}.version);
    // Version 2 measured the webcam's tilt by default: it's set by hand (0) now. Version 3 keeps the
    // player's choice.
    std::ofstream(folder / "settings.toml") << "version = 2\n[kinect]\ntilt_manual = false\ncentre_player = true\n";
    settings::Load(folder / "settings.toml");
    CHECK(settings::Get().kinect.tilt_manual && settings::Get().kinect.centre_player);
    std::ofstream(folder / "settings.toml") << "version = 3\n[kinect]\ntilt_manual = false\n";
    settings::Load(folder / "settings.toml");
    CHECK(!settings::Get().kinect.tilt_manual && !settings::Get().kinect.centre_player);
    fs::remove_all(folder);
  }

  // Avatar shape overrides (hiding templates, face shapes): a synthetic one hides triangles 0 and 2
  // of a three-triangle part and moves its second vertex, keeping its UVs.
  {
    std::vector<uint8_t> block;
    uint64_t bit = 0;
    auto put = [&](uint64_t value, uint32_t n) {
      for (uint32_t i = 0; i < n; ++i, ++bit) {
        if ((bit >> 3) >= block.size()) block.push_back(0);
        if ((value >> i) & 1) block[bit >> 3] |= uint8_t(1u << (bit & 7));
      }
    };
    auto section = [&](uint32_t count, uint32_t region, uint32_t bits) {
      put(count, 32), put(region, 32);
      put(2, 32), put(7, 16), put(1, 16), put(0x09F1C8C1, 32), put(0xE0B29CA1, 32);  // target: entry 7
      put(0, 32), put(bits, 32);
    };
    section(2, 18, 4);
    put(0, 4), put(2, 4);
    section(1, 96, 8);
    float scale = 0.5f, zero = 0.0f;
    uint32_t u;
    std::memcpy(&u, &scale, 4);
    put(u, 32);
    std::memcpy(&u, &zero, 4);
    put(u, 32), put(u, 32), put(u, 32);
    put(4, 6), put(4, 6), put(4, 6);
    put(0x123, 32), put(0, 32);
    for (int k = 0; k < 3; ++k) put(0, 32), put(0, 32);
    put(32, 8), put(1, 4), put(0, 4), put(2, 4);  // vertex at byte 32: lattice (1, 0, 2), no field bits
    x360tool::AvatarShapeOverride shape;
    std::string error;
    CHECK(x360tool::parse_avatar_shape_override(block, shape, error));
    CHECK(shape.target == 7 && shape.hidden_triangles.size() == 2 && shape.hidden_triangles[1] == 2);
    CHECK(shape.vertices.size() == 1 && shape.vertices[0].offset == 32 && shape.vertices[0].fields[0] == 0x123);
    x360tool::AvatarModel model;
    model.header[3] = 96, model.header[4] = 18, model.header[7] = 0x100, model.header[8] = 0x200;
    x360tool::AvatarMesh mesh;
    mesh.stride = 32, mesh.uv_sets = 1, mesh.vertex_count = 3, mesh.triangles = 3;
    mesh.vertex_offset = 0x100, mesh.index_offset = 0x200;
    mesh.vertices.assign(96, 0xAA);
    mesh.indices = {0, 1, 2, 0, 2, 1, 1, 2, 0};
    model.meshes.push_back(mesh);
    CHECK(shape.fits(model));
    CHECK(x360tool::apply_avatar_shape_override(model, shape) == 3);
    const auto& m = model.meshes[0];
    CHECK(m.indices[0] == m.indices[1] && m.indices[1] == m.indices[2]);
    CHECK(m.indices[3] == 0 && m.indices[4] == 2 && m.indices[5] == 1);
    CHECK(m.indices[6] == m.indices[7] && m.indices[7] == m.indices[8]);
    const uint8_t* v = &m.vertices[32];
    auto be_float = [](const uint8_t* p) {
      const uint32_t bits = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
      float f;
      std::memcpy(&f, &bits, 4);
      return f;
    };
    CHECK(be_float(v) == 1.0f && be_float(v + 4) == 0.0f && std::fabs(be_float(v + 8) - 1.7320508f) < 1e-5f);
    CHECK(v[12] == 0 && v[13] == 0 && v[14] == 0x01 && v[15] == 0x23);
    CHECK(v[28] == 0xAA && m.vertices[0] == 0xAA);  // UVs and other vertices untouched
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
