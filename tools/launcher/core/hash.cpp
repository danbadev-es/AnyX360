#include "core/hash.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <vector>

namespace anyx360::launcher {
namespace {

// One SHA-256 computation; the algorithm provider is opened once.
class Sha256Hash {
 public:
  Sha256Hash() {
    static BCRYPT_ALG_HANDLE algorithm = [] {
      BCRYPT_ALG_HANDLE handle = nullptr;
      BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
      return handle;
    }();
    if (algorithm) BCryptCreateHash(algorithm, &hash_, nullptr, 0, nullptr, 0, 0);
  }
  ~Sha256Hash() {
    if (hash_) BCryptDestroyHash(hash_);
  }
  bool ok() const { return hash_ != nullptr; }
  void Add(const void* data, size_t size) { BCryptHashData(hash_, PUCHAR(data), ULONG(size), 0); }
  std::string Hex() {
    unsigned char digest[32];
    if (BCryptFinishHash(hash_, digest, sizeof(digest), 0) != 0) return {};
    static const char kDigits[] = "0123456789abcdef";
    std::string text;
    for (unsigned char byte : digest) text += kDigits[byte >> 4], text += kDigits[byte & 15];
    return text;
  }

 private:
  BCRYPT_HASH_HANDLE hash_ = nullptr;
};

}  // namespace

std::string Sha256(std::string_view data) {
  Sha256Hash hash;
  if (!hash.ok()) return {};
  hash.Add(data.data(), data.size());
  return hash.Hex();
}

std::string Sha256File(const fs::path& file, const std::function<bool(uint64_t)>& progress) {
  // Long paths (a copy deep in the player's folders) need the \\?\ form without a long-path manifest.
  std::wstring name = fs::absolute(file).wstring();
  if (name.size() >= 240 && name.rfind(L"\\\\", 0) != 0) name = L"\\\\?\\" + name;
  HANDLE handle = CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return {};
  Sha256Hash hash;
  std::vector<char> block(1 << 20);
  uint64_t done = 0;
  DWORD read = 0;
  bool ok = hash.ok();
  while (ok) {
    if (!ReadFile(handle, block.data(), DWORD(block.size()), &read, nullptr)) ok = false;
    if (!ok || !read) break;
    hash.Add(block.data(), read);
    done += read;
    if (progress && !progress(done)) ok = false;
  }
  CloseHandle(handle);
  return ok ? hash.Hex() : std::string();
}

}  // namespace anyx360::launcher
