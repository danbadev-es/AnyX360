// Guest address space. The Xbox 360 uses 32-bit virtual addresses; we reserve a contiguous 4 GiB
// host region so that a guest address maps to host memory with a single add (base + guest_addr).
// Recompiled code receives `base` and performs all loads/stores through it with byte swapping.
#pragma once

#include <cstddef>
#include <cstdint>

#include "x360/endian.h"

namespace x360 {

using guest_addr = uint32_t;

class GuestMemory {
 public:
  static constexpr uint64_t kSize = 0x1'0000'0000ull;  // 4 GiB

  GuestMemory();
  ~GuestMemory();
  GuestMemory(const GuestMemory&) = delete;
  GuestMemory& operator=(const GuestMemory&) = delete;

  uint8_t* base() const noexcept { return base_; }
  bool valid() const noexcept { return base_ != nullptr; }

  // Commits (makes readable/writable) a guest range. Returns false on failure.
  bool commit(guest_addr addr, uint32_t size);
  // Decommits a guest range, returning it to the reserved state.
  bool decommit(guest_addr addr, uint32_t size);

  template <typename T = uint8_t>
  T* translate(guest_addr addr) const noexcept {
    return reinterpret_cast<T*>(base_ + addr);
  }

  template <typename T>
  T read(guest_addr addr) const noexcept {
    return load_be<T>(base_ + addr);
  }

  template <typename T>
  void write(guest_addr addr, T v) noexcept {
    store_be<T>(base_ + addr, v);
  }

  guest_addr to_guest(const void* host) const noexcept {
    return static_cast<guest_addr>(static_cast<const uint8_t*>(host) - base_);
  }

 private:
  uint8_t* base_ = nullptr;
};

}  // namespace x360
