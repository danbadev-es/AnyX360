// Big-endian helpers. The Xbox 360 (Xenon, PowerPC) is big-endian; the PC is little-endian.
// Every guest memory access and every on-disk console structure goes through these.
#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace x360 {

template <typename T>
constexpr T byteswap(T v) noexcept {
  static_assert(std::is_trivially_copyable_v<T>);
  if constexpr (sizeof(T) == 1) {
    return v;
  } else if constexpr (sizeof(T) == 2) {
    return std::bit_cast<T>(__builtin_bswap16(std::bit_cast<uint16_t>(v)));
  } else if constexpr (sizeof(T) == 4) {
    return std::bit_cast<T>(__builtin_bswap32(std::bit_cast<uint32_t>(v)));
  } else if constexpr (sizeof(T) == 8) {
    return std::bit_cast<T>(__builtin_bswap64(std::bit_cast<uint64_t>(v)));
  } else {
    static_assert(sizeof(T) == 0, "unsupported size");
  }
}

// Reads a big-endian value from an unaligned byte pointer.
template <typename T>
inline T load_be(const void* p) noexcept {
  T v;
  std::memcpy(&v, p, sizeof(T));
  return byteswap(v);
}

template <typename T>
inline void store_be(void* p, T v) noexcept {
  v = byteswap(v);
  std::memcpy(p, &v, sizeof(T));
}

// Storage type for big-endian fields inside guest structures (layout-compatible with the console).
template <typename T>
struct be {
  T raw;

  be() = default;
  be(T v) noexcept : raw(byteswap(v)) {}
  operator T() const noexcept { return byteswap(raw); }
  be& operator=(T v) noexcept {
    raw = byteswap(v);
    return *this;
  }
  T get() const noexcept { return byteswap(raw); }
};

static_assert(sizeof(be<uint32_t>) == 4);

}  // namespace x360
