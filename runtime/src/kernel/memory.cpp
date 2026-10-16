// Guest memory, backed by Xenia's xe::Memory (BSD): the 4 GiB guest address space at g_base with
// the console's heaps (4 KiB / 64 KiB virtual, XEX, and the three aliased physical windows), page
// protection, MMIO ranges (the GPU registers at 0x7FC80000) and physical write watches used by
// the GPU's texture cache.
#include <algorithm>

#include "x360/kernel.h"
#include "xenia/memory.h"

namespace x360::kernel {

uint8_t* g_base = nullptr;

namespace {

xe::Memory* g_memory = nullptr;
PageHeap* g_v4k = nullptr;
PageHeap* g_v64k = nullptr;
PageHeap* g_system = nullptr;

constexpr uint32_t kRw = xe::kMemoryProtectRead | xe::kMemoryProtectWrite;
constexpr uint32_t kReserveCommit = xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit;

uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

xe::BaseHeap* XeHeap(void* p) { return static_cast<xe::BaseHeap*>(p); }

}  // namespace

xe::Memory* XeMemory() { return g_memory; }

// ---- PageHeap: a view of an xe heap, optionally restricted to a sub-range --------------------

PageHeap::PageHeap(const char* name, uint32_t base, uint32_t size, uint32_t page_size)
    : name_(name), base_(base), size_(size), page_size_(page_size) {
  heap_ = g_memory->LookupHeap(base);
}

uint32_t PageHeap::Alloc(uint32_t size, uint32_t alignment, bool top_down, uint32_t min_address,
                         uint32_t max_address) {
  if (!size || !heap_) return 0;
  size = AlignUp(size, page_size_);
  alignment = std::max(AlignUp(alignment ? alignment : page_size_, page_size_), page_size_);
  uint32_t lo = std::max(base_, min_address);
  uint32_t hi = std::min<uint64_t>(uint64_t(base_) + size_ - 1, max_address);
  uint32_t address = 0;
  if (!XeHeap(heap_)->AllocRange(lo, hi, size, alignment, kReserveCommit, kRw, top_down, &address)) return 0;
  return address;
}

bool PageHeap::AllocFixed(uint32_t address, uint32_t size) {
  if (!heap_) return false;
  address &= ~(page_size_ - 1);
  return XeHeap(heap_)->AllocFixed(address, AlignUp(size, page_size_), page_size_, kReserveCommit, kRw);
}

bool PageHeap::Free(uint32_t address, uint32_t* freed_size) {
  if (!heap_) return false;
  uint32_t size = 0;
  bool ok = XeHeap(heap_)->Release(address, &size);
  if (freed_size) *freed_size = size;
  return ok;
}

bool PageHeap::Query(uint32_t address, uint32_t* alloc_base, uint32_t* alloc_size) const {
  if (!heap_) return false;
  xe::HeapAllocationInfo info;
  if (!XeHeap(heap_)->QueryRegionInfo(address, &info) || !(info.state & xe::kMemoryAllocationReserve)) return false;
  if (alloc_base) *alloc_base = info.allocation_base;
  if (alloc_size) *alloc_size = info.allocation_size;
  return true;
}

// ---- address space -----------------------------------------------------------------------------

bool InitMemory() {
  g_memory = new xe::Memory();
  if (!g_memory->Initialize()) {
    KLOG_E("could not set up the guest address space");
    return false;
  }
  g_base = g_memory->virtual_membase();
  hle::set_guest_base(g_base);

  g_v4k = new PageHeap("v4k", 0x00010000, 0x40000000 - 0x00010000, 0x1000);
  g_v64k = new PageHeap("v64k", 0x40000000, 0x30000000, 0x10000);
  g_system = new PageHeap("system", 0x70000000, 0x0F000000, 0x10000);
  KLOG_I("guest memory at %p (physical view %p)", g_base, g_memory->physical_membase());
  return true;
}

bool ReserveImage(uint32_t base, uint32_t size) {
  xe::BaseHeap* heap = g_memory->LookupHeap(base);
  if (!heap) return false;
  uint32_t page = heap->page_size();
  return heap->AllocFixed(base & ~(page - 1), AlignUp(size + (base & (page - 1)), page), page, kReserveCommit, kRw);
}

PageHeap& HeapV4k() { return *g_v4k; }
PageHeap& HeapV64k() { return *g_v64k; }
PageHeap& HeapSystem() { return *g_system; }

// ---- small kernel allocations -----------------------------------------------------------------

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

void SystemFree(uint32_t address) { g_system->Free(address); }

// ---- physical memory (semantics of Xenia's MmAllocatePhysicalMemoryEx) -------------------------

uint32_t PhysicalAlloc(uint32_t size, uint32_t page_size, uint32_t alignment, uint32_t min_physical,
                       uint32_t max_physical, uint32_t protect) {
  page_size = page_size >= 0x1000000 ? 0x1000000 : page_size >= 0x10000 ? 0x10000 : 0x1000;
  // 4 KB physical pages live at 0xE0000000 on the console, where the CPU view is offset by 0x1000
  // from physical memory. Recompiled code accesses guest memory without that offset, so the GPU
  // and the CPU would disagree about such pages; serve them from the (4 KB granular) 0xA0000000
  // window instead, which maps physical memory one to one.
  auto* heap = static_cast<xe::PhysicalHeap*>(
      g_memory->LookupHeapByType(true, page_size == 0x1000 ? 0x10000 : page_size));
  uint32_t heap_base = heap->heap_base();
  uint32_t offset = heap->GetPhysicalAddress(heap_base);
  auto sat_sub = [](uint32_t a, uint32_t b) { return a > b ? a - b : 0u; };
  if (!max_physical) max_physical = 0xFFFFFFFF;
  uint32_t lo = heap_base + std::min(sat_sub(min_physical, offset), heap->heap_size() - 1);
  uint32_t hi = heap_base + std::min(sat_sub(max_physical, offset), heap->heap_size() - 1);
  uint32_t address = 0;
  if (!heap->AllocRange(lo, hi, AlignUp(size, page_size), AlignUp(std::max(alignment, page_size), page_size),
                        kReserveCommit, protect ? protect : kRw, true, &address)) {
    return 0;
  }
  return address;
}

uint32_t GuestToPhysical(uint32_t guest_address) { return g_memory->GetPhysicalAddress(guest_address); }

bool PhysicalFree(uint32_t guest_address) {
  xe::BaseHeap* heap = g_memory->LookupHeap(guest_address);
  return heap && heap->Release(guest_address);
}

}  // namespace x360::kernel
