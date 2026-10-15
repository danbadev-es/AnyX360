#include "x360/memory.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360 {

GuestMemory::GuestMemory() {
  base_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, kSize, MEM_RESERVE, PAGE_NOACCESS));
}

GuestMemory::~GuestMemory() {
  if (base_) {
    VirtualFree(base_, 0, MEM_RELEASE);
  }
}

bool GuestMemory::commit(guest_addr addr, uint32_t size) {
  if (!base_ || uint64_t(addr) + size > kSize) {
    return false;
  }
  return VirtualAlloc(base_ + addr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

bool GuestMemory::decommit(guest_addr addr, uint32_t size) {
  if (!base_ || uint64_t(addr) + size > kSize) {
    return false;
  }
  return VirtualFree(base_ + addr, size, MEM_DECOMMIT) != 0;
}

}  // namespace x360
