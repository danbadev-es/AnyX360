// Title keys (GameDescriptor::title_keys, keys\title_keys.txt): the file read as the runtime reads
// it, made-up keys only. A title that hasn't loaded the file doesn't use them; a loaded one finds the
// real value of each obscured key it lists, and nothing for the others; damaged files are refused
// with the line that is wrong.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

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

void Write(const fs::path& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }

}  // namespace

int main() {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  const fs::path dir = fs::path(temp) / (L"anyx360_title_keys_" + std::to_wstring(GetCurrentProcessId()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  const fs::path file = dir / "title_keys.txt";

  const uint8_t obscured[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  const uint8_t expected[16] = {0x0F, 0x1E, 0x2D, 0x3C, 0x4B, 0x5A, 0x69, 0x78,
                                0x87, 0x96, 0xA5, 0xB4, 0xC3, 0xD2, 0xE1, 0xF0};
  uint8_t real[16] = {};
  bool uses = true;

  // Before any file: the title doesn't use title keys.
  CHECK(!x360::kernel::FindTitleKey(obscured, real, uses) && !uses);

  // Refused: missing, empty (comments only), a line that isn't two keys.
  std::string error;
  CHECK(!x360::kernel::LoadTitleKeys(dir / "missing.txt", error) && !error.empty());
  Write(file, "# nothing yet\n\n");
  CHECK(!x360::kernel::LoadTitleKeys(file, error) && error.find("no key") != std::string::npos);
  Write(file, "# keys\n00112233445566778899AABBCCDDEEFF\n");
  CHECK(!x360::kernel::LoadTitleKeys(file, error) && error.find("line 2") != std::string::npos);
  Write(file, "00112233445566778899AABBCCDDEEFF 0F1E2D3C4B5A69788796A5B4C3D2E1FZ\n");
  CHECK(!x360::kernel::LoadTitleKeys(file, error) && error.find("line 1") != std::string::npos);
  Write(file, "00112233445566778899AABBCCDDEEFF 0F1E2D3C4B5A69788796A5B4C3D2E1F0 extra\n");
  CHECK(!x360::kernel::LoadTitleKeys(file, error));
  CHECK(!x360::kernel::FindTitleKey(obscured, real, uses) && !uses);  // still none loaded

  // Read: comments, blank lines, CRLF and lower case; the real key found by the obscured one.
  Write(file,
        "# AnyX360 title keys\r\n\r\n"
        "00112233445566778899aabbccddeeff 0F1E2D3C4B5A69788796A5B4C3D2E1F0  # made up 0\r\n"
        "FFEEDDCCBBAA99887766554433221100 00000000000000000000000000000001\r\n");
  error.clear();
  CHECK(x360::kernel::LoadTitleKeys(file, error) && error.empty());
  CHECK(x360::kernel::FindTitleKey(obscured, real, uses) && uses && std::memcmp(real, expected, 16) == 0);
  // In place, as XeKeysSetKey asks.
  uint8_t key[16];
  std::memcpy(key, obscured, 16);
  CHECK(x360::kernel::FindTitleKey(key, key, uses) && std::memcmp(key, expected, 16) == 0);
  // A key the file doesn't have: the title uses title keys, and this one is missing.
  uint8_t other[16] = {1};
  std::memset(real, 0, sizeof(real));
  CHECK(!x360::kernel::FindTitleKey(other, real, uses) && uses && real[0] == 0);

  std::error_code ec;
  fs::remove_all(dir, ec);
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
