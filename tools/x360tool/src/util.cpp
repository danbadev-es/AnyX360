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

fs::path long_path(const fs::path& path) {
  std::wstring text = fs::absolute(path).lexically_normal().make_preferred().wstring();
  if (text.rfind(L"\\\\", 0) == 0) return text;  // already \\?\ or a share
  return L"\\\\?\\" + text;
}

std::ofstream create_file(const fs::path& path) {
  fs::path target = long_path(path);
  if (target.has_parent_path()) fs::create_directories(target.parent_path());
  std::ofstream out(target, std::ios::binary | std::ios::trunc);
  if (!out) throw Error("cannot write " + path.string());
  return out;
}

void write_file(const fs::path& path, std::span<const uint8_t> data) {
  std::ofstream out = create_file(path);
  out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!out) {
    throw Error("cannot write " + path.string());
  }
}

std::string cp1252_to_utf8(std::string_view name) {
  // 0x80-0x9F differ from Latin-1 (0 where Windows-1252 has no character); the rest is Latin-1.
  static constexpr uint16_t k80[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                       0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
                                       0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                       0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
  std::string out;
  for (unsigned char c : name) {
    uint32_t code = c < 0x80 ? c : c < 0xA0 ? k80[c - 0x80] : c;
    if (!code) code = 0xFFFD;
    if (code < 0x80) {
      out += char(code);
    } else if (code < 0x800) {
      out += char(0xC0 | (code >> 6));
      out += char(0x80 | (code & 0x3F));
    } else {
      out += char(0xE0 | (code >> 12));
      out += char(0x80 | ((code >> 6) & 0x3F));
      out += char(0x80 | (code & 0x3F));
    }
  }
  return out;
}

fs::path utf8_path(std::string_view utf8) {
  int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
  std::wstring text(size_t(length > 0 ? length : 0), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), text.data(), length);
  return fs::path(text);
}

std::array<uint8_t, 20> sha1(std::span<const uint8_t> data) {
  static const BCRYPT_ALG_HANDLE alg = [] {
    BCRYPT_ALG_HANDLE handle = nullptr;
    if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0) throw Error("BCrypt: open SHA-1");
    return handle;
  }();
  std::array<uint8_t, 20> digest{};
  if (BCryptHash(alg, nullptr, 0, const_cast<PUCHAR>(data.data()), ULONG(data.size()), digest.data(),
                 ULONG(digest.size())) < 0)
    throw Error("BCrypt: SHA-1");
  return digest;
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

std::optional<std::array<uint8_t, 16>> load_key(const fs::path& path, bool required) {
  if (!fs::exists(path)) {
    if (required) throw Error("key file not found: " + path.string());
    return std::nullopt;
  }
  auto raw = read_file(path);
  std::vector<uint8_t> key;
  if (raw.size() == 16) {
    key = raw;
  } else if (auto parsed = parse_hex(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()))) {
    key = *parsed;
  }
  if (key.size() != 16) throw Error("key file must contain 16 bytes or 32 hex characters: " + path.string());
  std::array<uint8_t, 16> out;
  std::copy(key.begin(), key.end(), out.begin());
  return out;
}

}  // namespace x360tool
