#include "util.h"

#include <fstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

namespace x360tool {

std::vector<uint8_t> read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    throw Error("cannot open " + path.string());
  }
  std::vector<uint8_t> data(static_cast<size_t>(in.tellg()));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!in) {
    throw Error("cannot read " + path.string());
  }
  return data;
}

void write_file(const fs::path& path, std::span<const uint8_t> data) {
  if (path.has_parent_path()) {
    fs::create_directories(path.parent_path());
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!out) {
    throw Error("cannot write " + path.string());
  }
}

std::string hex(std::span<const uint8_t> data) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string s;
  s.reserve(data.size() * 2);
  for (uint8_t b : data) {
    s += kDigits[b >> 4];
    s += kDigits[b & 0xF];
  }
  return s;
}

std::optional<std::vector<uint8_t>> parse_hex(std::string_view text) {
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::vector<uint8_t> out;
  int hi = -1;
  for (char c : text) {
    if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
    int n = nibble(c);
    if (n < 0) return std::nullopt;
    if (hi < 0) {
      hi = n;
    } else {
      out.push_back(static_cast<uint8_t>((hi << 4) | n));
      hi = -1;
    }
  }
  if (hi >= 0) return std::nullopt;
  return out;
}

void aes128_cbc_decrypt(std::span<const uint8_t, 16> key, std::span<uint8_t> data) {
  if (data.size() % 16 != 0) {
    throw Error("AES input is not a multiple of 16 bytes");
  }
  if (data.empty()) return;

  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_KEY_HANDLE hkey = nullptr;
  auto fail = [&](const char* what) {
    if (hkey) BCryptDestroyKey(hkey);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    throw Error(std::string("BCrypt: ") + what);
  };

  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) fail("open AES");
  if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                        sizeof(BCRYPT_CHAIN_MODE_CBC), 0) < 0)
    fail("set CBC");
  if (BCryptGenerateSymmetricKey(alg, &hkey, nullptr, 0, (PUCHAR)key.data(), 16, 0) < 0) fail("key");

  uint8_t iv[16] = {};
  ULONG written = 0;
  // BCrypt supports in-place operation when input and output buffers are identical.
  if (BCryptDecrypt(hkey, data.data(), static_cast<ULONG>(data.size()), nullptr, iv, sizeof(iv),
                    data.data(), static_cast<ULONG>(data.size()), &written, 0) < 0)
    fail("decrypt");

  BCryptDestroyKey(hkey);
  BCryptCloseAlgorithmProvider(alg, 0);
}

}  // namespace x360tool
