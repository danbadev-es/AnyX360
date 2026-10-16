// Guest memory: a 4 GiB host reservation where guest address N lives at g_base + N.
//
// 0x00000000-0x9FFFFFFF is ordinary committed memory. The three physical windows are views of one
// 512 MiB section, so writes through 0xA0000000 are visible through 0xC0000000/0xE0000000, as on the
// console (the GPU only sees physical addresses). The 0xE0000000 window is offset by 4 KiB, matching
// the console's mapping.
#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>

namespace x360::kernel {

uint8_t* g_base = nullptr;

namespace {

constexpr uint64_t kGuestSize = 0x1'0000'0000ull;

using VirtualAlloc2Fn = PVOID(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
using MapViewOfFile3Fn = PVOID(WINAPI*)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG,
                                        MEM_EXTENDED_PARAMETER*, ULONG);

PageHeap* g_v4k = nullptr;
PageHeap* g_v64k = nullptr;
PageHeap* g_system = nullptr;
PageHeap* g_physical = nullptr;  // in physical address space [0, 512 MiB)

struct Region {
  uint64_t start, end;  // guest addresses
  int view;             // -1 = private memory, otherwise section offset
  uint64_t section_offset;
};

uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

}  // namespace

// ---- PageHeap ---------------------------------------------------------------------------------

PageHeap::PageHeap(const char* name, uint32_t base, uint32_t size, uint32_t page_size)
    : name_(name), base_(base), size_(size), page_size_(page_size) {
  free_[base] = size;
}

uint32_t PageHeap::Alloc(uint32_t size, uint32_t alignment, bool top_down, uint32_t min_address,
                         uint32_t max_address) {
  if (size == 0) return 0;
  size = AlignUp(size, page_size_);
  alignment = std::max(AlignUp(alignment ? alignment : page_size_, page_size_), page_size_);

  std::lock_guard lock(mutex_);
  auto try_block = [&](uint32_t block, uint32_t block_size, uint32_t& out) -> bool {
    uint64_t lo = std::max<uint64_t>(block, min_address);
    uint64_t hi = std::min<uint64_t>(uint64_t(block) + block_size, uint64_t(max_address) + 1);
    if (hi <= lo || hi - lo < size) return false;
    uint64_t addr;
    if (top_down) {
      addr = (hi - size) & ~uint64_t(alignment - 1);
      if (addr < lo) return false;
    } else {
      addr = (lo + alignment - 1) & ~uint64_t(alignment - 1);
      if (addr + size > hi) return false;
    }
    out = uint32_t(addr);
    return true;
  };

  uint32_t found = 0;
  std::map<uint32_t, uint32_t>::iterator block = free_.end();
  if (top_down) {
    for (auto it = free_.rbegin(); it != free_.rend(); ++it) {
      if (try_block(it->first, it->second, found)) {
        block = std::prev(it.base());
        break;
      }
    }
  } else {
    for (auto it = free_.begin(); it != free_.end(); ++it) {
      if (try_block(it->first, it->second, found)) {
        block = it;
        break;
      }
    }
  }
  if (block == free_.end()) return 0;

  uint32_t block_start = block->first, block_size = block->second;
  free_.erase(block);
  if (found > block_start) free_[block_start] = found - block_start;
  uint64_t end = uint64_t(found) + size, block_end = uint64_t(block_start) + block_size;
  if (end < block_end) free_[uint32_t(end)] = uint32_t(block_end - end);
  used_[found] = size;
  return found;
}

bool PageHeap::AllocFixed(uint32_t address, uint32_t size) {
  address &= ~(page_size_ - 1);
  size = AlignUp(size, page_size_);
  std::lock_guard lock(mutex_);
  auto it = free_.upper_bound(address);
  if (it == free_.begin()) return false;
  --it;
  uint64_t block_end = uint64_t(it->first) + it->second;
  if (address < it->first || uint64_t(address) + size > block_end) return false;
  uint32_t block_start = it->first;
  free_.erase(it);
  if (address > block_start) free_[block_start] = address - block_start;
  if (uint64_t(address) + size < block_end) free_[address + size] = uint32_t(block_end - (uint64_t(address) + size));
  used_[address] = size;
  return true;
}

bool PageHeap::Free(uint32_t address, uint32_t* freed_size) {
  std::lock_guard lock(mutex_);
  auto it = used_.find(address);
  if (it == used_.end()) return false;
  uint32_t start = it->first, size = it->second;
  used_.erase(it);
  if (freed_size) *freed_size = size;

  // Coalesce with neighbours.
  auto next = free_.lower_bound(start);
  if (next != free_.end() && start + size == next->first) {
    size += next->second;
    next = free_.erase(next);
  }
  if (next != free_.begin()) {
    auto prev = std::prev(next);
    if (prev->first + prev->second == start) {
      start = prev->first;
      size += prev->second;
      free_.erase(prev);
    }
  }
  free_[start] = size;
  return true;
}

bool PageHeap::Query(uint32_t address, uint32_t* alloc_base, uint32_t* alloc_size) const {
  std::lock_guard lock(mutex_);
  auto it = used_.upper_bound(address);
  if (it == used_.begin()) return false;
  --it;
  if (address >= it->first + uint64_t(it->second)) return false;
  if (alloc_base) *alloc_base = it->first;
  if (alloc_size) *alloc_size = it->second;
  return true;
}

// ---- Address space ----------------------------------------------------------------------------

bool InitMemory() {
  HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
  auto virtual_alloc2 = reinterpret_cast<VirtualAlloc2Fn>(GetProcAddress(kernelbase, "VirtualAlloc2"));
  auto map_view3 = reinterpret_cast<MapViewOfFile3Fn>(GetProcAddress(kernelbase, "MapViewOfFile3"));
  if (!virtual_alloc2 || !map_view3) {
    KLOG_E("VirtualAlloc2/MapViewOfFile3 unavailable (Windows 10 1803+ required)");
    return false;
  }

  // Reserve the whole guest space as one placeholder.
  auto* base = static_cast<uint8_t*>(virtual_alloc2(nullptr, nullptr, kGuestSize,
                                                    MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS,
                                                    nullptr, 0));
  if (!base) {
    KLOG_E("could not reserve 4 GiB of address space (error %lu)", GetLastError());
    return false;
  }

  const Region regions[] = {
      {0x00000000, 0xA0000000, -1, 0},
      {0xA0000000, 0xC0000000, 1, 0},
      {0xC0000000, 0xE0000000, 1, 0},
      {0xE0000000, 0xFFFFF000, 1, 0x1000},
      {0xFFFFF000, 0x100000000ull, -2, 0},  // stays reserved
  };

  // Split the placeholder at every region boundary.
  for (const Region& r : regions) {
    if (r.end - r.start == kGuestSize) continue;
    if (!VirtualFree(base + r.start, r.end - r.start, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
      // Splitting the last piece fails harmlessly once it is already its own placeholder.
      DWORD error = GetLastError();
      if (error != ERROR_INVALID_PARAMETER && error != ERROR_INVALID_ADDRESS) {
        KLOG_E("placeholder split failed at %08llX (error %lu)", r.start, GetLastError());
        return false;
      }
    }
  }

  HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, 0, kPhysicalSize,
                                      nullptr);
  if (!section) {
    KLOG_E("could not create the 512 MiB physical memory section (error %lu)", GetLastError());
    return false;
  }

  for (const Region& r : regions) {
    void* at = base + r.start;
    SIZE_T size = SIZE_T(r.end - r.start);
    if (r.view == -1) {
      if (!virtual_alloc2(nullptr, at, size, MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE,
                          nullptr, 0)) {
        KLOG_E("commit of %08llX-%08llX failed (error %lu)", r.start, r.end, GetLastError());
        return false;
      }
    } else if (r.view == 1) {
      if (!map_view3(section, GetCurrentProcess(), at, r.section_offset, size, MEM_REPLACE_PLACEHOLDER,
                     PAGE_READWRITE, nullptr, 0)) {
        KLOG_E("physical view at %08llX failed (error %lu)", r.start, GetLastError());
        return false;
      }
    }
  }

  // Catch guest null-pointer accesses.
  DWORD old;
  VirtualProtect(base, 0x1000, PAGE_NOACCESS, &old);

  g_base = base;
  hle::set_guest_base(base);

  g_v4k = new PageHeap("v4k", 0x00010000, 0x40000000 - 0x00010000, 0x1000);
  g_v64k = new PageHeap("v64k", 0x40000000, 0x30000000, 0x10000);
  g_system = new PageHeap("system", 0x70000000, 0x0F000000, 0x1000);
  // Keep the first 64 KiB of physical memory out of reach (and the E0 window offset page).
  g_physical = new PageHeap("physical", 0x00010000, kPhysicalSize - 0x00010000, 0x1000);
  KLOG_I("guest memory at %p", base);
  return true;
}

PageHeap& HeapV4k() { return *g_v4k; }
PageHeap& HeapV64k() { return *g_v64k; }
PageHeap& HeapSystem() { return *g_system; }

// ---- Small kernel allocations -----------------------------------------------------------------

namespace {
std::mutex g_small_mutex;
uint32_t g_small_cursor = 0, g_small_end = 0;
}  // namespace

uint32_t SystemAlloc(uint32_t size, uint32_t alignment) {
  size = AlignUp(std::max<uint32_t>(size, 1), 16);
  alignment = std::max<uint32_t>(alignment, 16);
  if (size >= 0x4000) {
    uint32_t addr = g_system->Alloc(size, alignment);
    if (addr) std::memset(Ptr(addr), 0, size);
    return addr;
  }
  std::lock_guard lock(g_small_mutex);
  uint32_t addr = AlignUp(g_small_cursor, alignment);
  if (!g_small_cursor || addr + size > g_small_end) {
    uint32_t chunk = g_system->Alloc(0x100000);
    if (!chunk) return 0;
    g_small_cursor = chunk;
    g_small_end = chunk + 0x100000;
    addr = AlignUp(g_small_cursor, alignment);
  }
  g_small_cursor = addr + size;
  std::memset(Ptr(addr), 0, size);
  return addr;
}

void SystemFree(uint32_t address) {
  // Small blocks are never reused; large ones go back to the heap.
  g_system->Free(address);
}

// ---- Physical memory --------------------------------------------------------------------------

uint32_t PhysicalAlloc(uint32_t size, uint32_t page_size, uint32_t alignment, uint32_t min_physical,
                       uint32_t max_physical) {
  page_size = page_size >= 0x1000000 ? 0x1000000 : page_size >= 0x10000 ? 0x10000 : 0x1000;
  size = AlignUp(size, page_size);
  alignment = std::max(alignment, page_size);
  if (max_physical == 0 || max_physical >= kPhysicalSize) max_physical = kPhysicalSize - 1;
  uint32_t phys = g_physical->Alloc(size, alignment, false, min_physical & 0x1FFFFFFF, max_physical);
  if (!phys) return 0;
  uint32_t guest;
  if (page_size == 0x1000) {
    if (phys < 0x1000) return 0;
    guest = 0xE0000000u + (phys - 0x1000);
  } else if (page_size == 0x10000) {
    guest = 0xA0000000u + phys;
  } else {
    guest = 0xC0000000u + phys;
  }
  std::memset(Ptr(guest), 0, size);
  return guest;
}

uint32_t GuestToPhysical(uint32_t guest) {
  if (guest >= 0xE0000000u) return (guest - 0xE0000000u) + 0x1000;
  if (guest >= 0xA0000000u) return guest & 0x1FFFFFFF;
  return guest;  // not physical memory
}

bool PhysicalFree(uint32_t guest_address) { return g_physical->Free(GuestToPhysical(guest_address)); }

}  // namespace x360::kernel
