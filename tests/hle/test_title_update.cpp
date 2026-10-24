// Title updates at start (runtime/src/kernel/title_update.cpp): a synthetic executable and a delta
// patch built as a title update's default.xexp is (XenonRecomp's patcher, the one the runtime uses,
// applies it): the patched executable is cached, used again, replaced when the update changes, and
// refused for another version.
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "x360/kernel.h"
#include "xex.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

namespace fs = std::filesystem;

// The kernel's loader fills the function table from this generated array; tests have no game.
PPCFuncMapping PPCFuncMappings[] = {{0, nullptr}};

static int failures = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    if (cond) {                                                     \
      std::printf("PASS %s\n", #cond);                              \
    } else {                                                        \
      std::printf("FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++failures;                                                   \
    }                                                               \
  } while (0)

namespace {

using Bytes = std::vector<uint8_t>;
using Block = std::array<uint8_t, 16>;

void Put32(Bytes& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (24 - 8 * i));
}
void Put16(Bytes& b, size_t at, uint16_t v) {
  b[at] = uint8_t(v >> 8);
  b[at + 1] = uint8_t(v);
}

// AES-128 of one block (ECB), and SHA-1, with Windows' CNG.
Block AesEncrypt(const Block& key, const Block& plain) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_KEY_HANDLE handle = nullptr;
  Block out{};
  ULONG written = 0;
  BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
  BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
  BCryptGenerateSymmetricKey(alg, &handle, nullptr, 0, (PUCHAR)key.data(), 16, 0);
  Block in = plain;
  BCryptEncrypt(handle, in.data(), 16, nullptr, nullptr, 0, out.data(), 16, &written, 0);
  BCryptDestroyKey(handle);
  BCryptCloseAlgorithmProvider(alg, 0);
  return out;
}

std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t size) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  std::array<uint8_t, 20> digest{};
  BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
  BCryptHash(alg, nullptr, 0, (PUCHAR)data, ULONG(size), digest.data(), 20);
  BCryptCloseAlgorithmProvider(alg, 0);
  return digest;
}

constexpr Block kRetailKey = {0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
                              0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91};
constexpr uint32_t kHeaders = 0x1000, kSecurity = 0x200, kImageSize = 0x2000;
constexpr uint32_t kFrom = 0x00000003, kTo = 0x00000103;  // 0.0.0.3 -> 0.0.1.3
// The session key, as decrypted with the retail key.
constexpr Block kSessionKey = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

// An unencrypted, uncompressed XEX of `version`: execution info at 0x320, the version it will be
// patched to at 0x400 (the patch copies it in), the image ("MZ", then a pattern) after 0x1000.
Bytes Base(uint32_t version) {
  Bytes x(kHeaders + kImageSize, 0);
  Put32(x, 0, 0x58455832);
  Put32(x, 0x04, 1);
  Put32(x, 0x08, kHeaders);
  Put32(x, 0x10, kSecurity);
  Put32(x, 0x14, 3);
  Put32(x, 0x18, 0x000003FF), Put32(x, 0x1C, 0x300);  // file format: none, none
  Put32(x, 0x20, 0x00040006), Put32(x, 0x24, 0x320);  // execution info
  Put32(x, 0x28, 0x00010201), Put32(x, 0x2C, 0x82000000);
  Put32(x, kSecurity + 0x004, kImageSize);
  Put32(x, kSecurity + 0x110, 0x82000000);
  const Block key = AesEncrypt(kRetailKey, kSessionKey);
  std::memcpy(x.data() + kSecurity + 0x150, key.data(), 16);
  Put32(x, 0x300, 8);
  Put32(x, 0x320, 0x12345678);  // media ID
  Put32(x, 0x324, version), Put32(x, 0x328, version);
  Put32(x, 0x32C, 0x4D5308ED);
  Put32(x, 0x400, kTo);
  x[kHeaders] = 'M', x[kHeaders + 1] = 'Z';
  for (uint32_t i = 2; i < kImageSize; ++i) x[kHeaders + i] = uint8_t(i * 7);
  return x;
}

// A delta patch from `from` to `to`: the headers copied with the version moved in (two copy
// operations), and one data block that copies 16 bytes of the image (0x200 -> 0x100 + `shift`).
Bytes Patch(uint32_t from, uint32_t to, uint32_t shift = 0) {
  Bytes p(kHeaders, 0);
  Put32(p, 0, 0x58455832);
  Put32(p, 0x04, 0x50);  // a delta patch
  Put32(p, 0x08, kHeaders);
  Put32(p, 0x10, kSecurity);
  Put32(p, 0x14, 2);
  Put32(p, 0x18, 0x000003FF), Put32(p, 0x1C, 0x300);
  Put32(p, 0x20, 0x000005FF), Put32(p, 0x24, 0x400);
  // File format: no encryption, delta; window 0x8000, then the first data block {size, SHA-1}.
  Put32(p, 0x300, 0x24);
  Put16(p, 0x306, 3);
  Put32(p, 0x308, 0x8000);
  // The descriptor: size, versions, digest, image key source, the target headers' size, the
  // headers' and the image's source and target ranges, then the header patches.
  const uint32_t d = 0x400;
  Put32(p, d + 0x00, 0x80);
  Put32(p, d + 0x04, to);
  Put32(p, d + 0x08, from);
  const Block source = AesEncrypt(kSessionKey, kSessionKey);  // decrypts to the base's key
  std::memcpy(p.data() + d + 0x20, source.data(), 16);
  Put32(p, d + 0x30, kHeaders);
  Put32(p, d + 0x34, 0), Put32(p, d + 0x38, kHeaders), Put32(p, d + 0x3C, 0);
  Put32(p, d + 0x40, 0), Put32(p, d + 0x44, kImageSize), Put32(p, d + 0x48, 0);
  // {old, new, length, 1 = copy}, 12 bytes each; all zero ends them.
  Put32(p, d + 0x4C, 0x400), Put32(p, d + 0x50, 0x324), Put16(p, d + 0x54, 4), Put16(p, d + 0x56, 1);
  Put32(p, d + 0x58, 0x400), Put32(p, d + 0x5C, 0x328), Put16(p, d + 0x60, 4), Put16(p, d + 0x62, 1);
  // The data block: {next block size 0, its SHA-1}, then {0x200 -> 0x100 + shift, 16 bytes, copy}.
  Bytes block(24 + 12 + 12, 0);
  Put32(block, 24, 0x200), Put32(block, 28, 0x100 + shift), Put16(block, 32, 16), Put16(block, 34, 1);
  const auto hash = Sha1(block.data(), block.size());
  Put32(p, 0x30C, uint32_t(block.size()));
  std::memcpy(p.data() + 0x310, hash.data(), 20);
  p.insert(p.end(), block.begin(), block.end());
  return p;
}

}  // namespace

int main() {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  const fs::path dir = fs::path(temp) / (L"anyx360_title_update_" + std::to_wstring(GetCurrentProcessId()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  const fs::path base = dir / "default.xex", patch = dir / "default.xexp", cache = dir / "cache" / "patched";
  x360tool::write_file(base, Base(kFrom));
  x360tool::write_file(patch, Patch(kFrom, kTo));

  // Patched once, then found in the cache.
  const auto first = x360::kernel::PrepareTitleUpdate(base, patch, kTo, cache);
  CHECK(first.error.empty() && !first.xex.empty() && !first.cached);
  CHECK(first.xex.parent_path() == cache && first.xex.filename().string().rfind("default-", 0) == 0 &&
        first.xex.extension() == ".xex");
  const x360tool::Xex patched(x360tool::read_file(first.xex));
  CHECK(patched.execution_info() && patched.execution_info()->version.raw == kTo &&
        patched.execution_info()->base_version.raw == kTo && patched.execution_info()->title_id == 0x4D5308ED);
  CHECK(patched.file_format().encryption == x360tool::XexEncryption::None);
  const auto image = patched.extract_image(std::nullopt);
  const Bytes original = Base(kFrom);
  CHECK(image.size() == kImageSize && image[0] == 'M' &&
        std::memcmp(image.data() + 0x100, original.data() + kHeaders + 0x200, 16) == 0 &&
        std::memcmp(image.data() + 0x110, original.data() + kHeaders + 0x110, 16) == 0);
  const auto again = x360::kernel::PrepareTitleUpdate(base, patch, kTo, cache);
  CHECK(again.cached && again.xex == first.xex);
  CHECK(x360::kernel::PrepareTitleUpdate(base, patch, 0, cache).cached);  // any version

  // Another update: patched again, the old copy removed.
  x360tool::write_file(patch, Patch(kFrom, kTo, 0x40));
  const auto changed = x360::kernel::PrepareTitleUpdate(base, patch, kTo, cache);
  CHECK(changed.error.empty() && !changed.cached && changed.xex != first.xex);
  CHECK(fs::exists(changed.xex) && !fs::exists(first.xex));
  const auto changed_image = x360tool::Xex(x360tool::read_file(changed.xex)).extract_image(std::nullopt);
  CHECK(std::memcmp(changed_image.data() + 0x140, original.data() + kHeaders + 0x200, 16) == 0);

  // Refused: an update to another version than the game's, one for another executable version, not
  // a patch at all, a damaged block.
  auto wrong = x360::kernel::PrepareTitleUpdate(base, patch, 0x00000203, cache);
  CHECK(wrong.xex.empty() && wrong.error.find("0.0.1.3") != std::string::npos && wrong.error.find("0.0.2.3") != std::string::npos);
  x360tool::write_file(patch, Patch(0x00000002, kTo));
  wrong = x360::kernel::PrepareTitleUpdate(base, patch, kTo, cache);
  CHECK(wrong.xex.empty() && wrong.error.find("0.0.0.2") != std::string::npos);
  x360tool::write_file(patch, Base(kFrom));
  CHECK(x360::kernel::PrepareTitleUpdate(base, patch, kTo, cache).xex.empty());
  Bytes damaged = Patch(kFrom, kTo, 0x80);
  damaged.back() ^= 1;  // the block no longer matches its SHA-1
  x360tool::write_file(patch, damaged);
  wrong = x360::kernel::PrepareTitleUpdate(base, patch, kTo, cache);
  CHECK(wrong.xex.empty() && !wrong.error.empty());
  CHECK(x360::kernel::PrepareTitleUpdate(base, dir / "missing.xexp", kTo, cache).xex.empty());

  std::error_code ec;
  fs::remove_all(dir, ec);
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
