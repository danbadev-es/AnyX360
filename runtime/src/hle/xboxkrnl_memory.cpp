// xboxkrnl memory exports.
#include "x360/kernel.h"

using namespace x360::kernel;

namespace {

constexpr uint32_t kMemCommit = 0x1000;
constexpr uint32_t kMemReserve = 0x2000;
constexpr uint32_t kMemDecommit = 0x4000;
constexpr uint32_t kMemRelease = 0x8000;
constexpr uint32_t kMemTopDown = 0x100000;
constexpr uint32_t kMemLargePages = 0x20000000;
constexpr uint32_t kMem16MbPages = 0x80000000;
constexpr uint32_t kPageReadWrite = 0x04;

PageHeap* HeapFor(uint32_t address) {
  if (HeapV4k().Contains(address)) return &HeapV4k();
  if (HeapV64k().Contains(address)) return &HeapV64k();
  if (HeapSystem().Contains(address)) return &HeapSystem();
  return nullptr;
}

uint32_t NtAllocateVirtualMemory(uint32_t base_ptr, uint32_t size_ptr, uint32_t alloc_type, uint32_t protect,
                                 uint32_t debug_memory) {
  (void)protect;
  (void)debug_memory;
  uint32_t base = Load<uint32_t>(base_ptr);
  uint32_t size = Load<uint32_t>(size_ptr);
  if (size >= 0x1000000 || TraceEnabled()) {
    KLOG_I("NtAllocateVirtualMemory(base %08X, size %08X, type %08X, protect %08X) from %s", base, size, alloc_type,
           protect, GuestBacktrace().c_str());
  }
  if (!size) return X_STATUS_INVALID_PARAMETER;
  if (!(alloc_type & (kMemCommit | kMemReserve))) return X_STATUS_INVALID_PARAMETER;

  if (base) {
    PageHeap* heap = HeapFor(base);
    if (!heap) return X_STATUS_INVALID_PARAMETER;
    uint32_t alloc_base, alloc_size;
    if (heap->Query(base, &alloc_base, &alloc_size)) {
      // Committing pages of an existing reservation: memory is always backed already.
      uint32_t page = heap->page_size();
      uint32_t aligned = base & ~(page - 1);
      size = ((base + size + page - 1) & ~(page - 1)) - aligned;
      Store<uint32_t>(base_ptr, aligned);
      Store<uint32_t>(size_ptr, size);
      return X_STATUS_SUCCESS;
    }
    if (!heap->AllocFixed(base, size)) return X_STATUS_NO_MEMORY;
    uint32_t page = heap->page_size();
    base &= ~(page - 1);
    size = (size + page - 1) & ~(page - 1);
    std::memset(Ptr(base), 0, size);
  } else {
    PageHeap& heap = (alloc_type & (kMemLargePages | kMem16MbPages)) ? HeapV64k() : HeapV4k();
    size = (size + heap.page_size() - 1) & ~(heap.page_size() - 1);
    base = heap.Alloc(size, 0, (alloc_type & kMemTopDown) != 0);
    if (!base) {
      KLOG_W("NtAllocateVirtualMemory: out of memory (%X bytes, %s)", size, heap.name());
      return X_STATUS_NO_MEMORY;
    }
    std::memset(Ptr(base), 0, size);
  }
  Store<uint32_t>(base_ptr, base);
  Store<uint32_t>(size_ptr, size);
  return X_STATUS_SUCCESS;
}

uint32_t NtFreeVirtualMemory(uint32_t base_ptr, uint32_t size_ptr, uint32_t free_type, uint32_t debug_memory) {
  (void)debug_memory;
  uint32_t base = Load<uint32_t>(base_ptr);
  PageHeap* heap = HeapFor(base);
  if (!heap) return X_STATUS_INVALID_PARAMETER;
  if (free_type & kMemDecommit) {
    uint32_t size = size_ptr ? Load<uint32_t>(size_ptr) : 0;
    if (size) std::memset(Ptr(base), 0, size);
    return X_STATUS_SUCCESS;
  }
  if (!(free_type & kMemRelease)) return X_STATUS_INVALID_PARAMETER;
  uint32_t freed = 0;
  if (!heap->Free(base, &freed)) return X_STATUS_INVALID_PARAMETER;
  if (size_ptr) Store<uint32_t>(size_ptr, freed);
  return X_STATUS_SUCCESS;
}

uint32_t NtQueryVirtualMemory(uint32_t address, uint32_t info_ptr, uint32_t debug_memory) {
  (void)debug_memory;
  PageHeap* heap = HeapFor(address);
  uint32_t alloc_base = 0, alloc_size = 0;
  bool allocated = heap && heap->Query(address, &alloc_base, &alloc_size);
  uint32_t page = heap ? heap->page_size() : 0x1000;
  Store<uint32_t>(info_ptr + 0x00, address & ~(page - 1));
  Store<uint32_t>(info_ptr + 0x04, allocated ? alloc_base : 0);
  Store<uint32_t>(info_ptr + 0x08, allocated ? kPageReadWrite : 0);
  Store<uint32_t>(info_ptr + 0x0C, allocated ? alloc_base + alloc_size - (address & ~(page - 1)) : page);
  Store<uint32_t>(info_ptr + 0x10, allocated ? kMemCommit : 0x10000 /* MEM_FREE */);
  Store<uint32_t>(info_ptr + 0x14, allocated ? kPageReadWrite : 1 /* PAGE_NOACCESS */);
  Store<uint32_t>(info_ptr + 0x18, allocated ? 0x20000 /* MEM_PRIVATE */ : 0);
  return X_STATUS_SUCCESS;
}

uint32_t MmAllocatePhysicalMemoryEx(uint32_t flags, uint32_t size, uint32_t protect, uint32_t min_address,
                                    uint32_t max_address, uint32_t alignment) {
  (void)flags;
  uint32_t page = (protect & kMem16MbPages) ? 0x1000000 : (protect & kMemLargePages) ? 0x10000 : 0x1000;
  uint32_t address = PhysicalAlloc(size, page, alignment, min_address, max_address);
  if (!address) KLOG_W("MmAllocatePhysicalMemoryEx: failed (%X bytes, page %X)", size, page);
  return address;
}

void MmFreePhysicalMemory(uint32_t type, uint32_t address) {
  (void)type;
  PhysicalFree(address);
}

uint32_t MmGetPhysicalAddress(uint32_t address) { return GuestToPhysical(address); }

uint32_t MmMapIoSpace(uint32_t physical, uint32_t size, uint32_t protect) {
  (void)size;
  (void)protect;
  return physical;  // device register ranges are addressed directly
}

uint32_t MmQueryAddressProtect(uint32_t address) {
  (void)address;
  return kPageReadWrite;
}

void MmSetAddressProtect(uint32_t address, uint32_t size, uint32_t protect) {
  (void)address;
  (void)size;
  (void)protect;
}

uint32_t ExAllocatePoolTypeWithTag(uint32_t size, uint32_t tag, uint32_t pool_type) {
  (void)tag;
  (void)pool_type;
  uint32_t address = HeapSystem().Alloc(size);
  if (address) std::memset(Ptr(address), 0, (size + 0xFFF) & ~0xFFFu);
  return address;
}

uint32_t ExAllocatePool(uint32_t size) { return ExAllocatePoolTypeWithTag(size, 0, 0); }

void ExFreePool(uint32_t address) { HeapSystem().Free(address); }

}  // namespace

X360_EXPORT(NtAllocateVirtualMemory, NtAllocateVirtualMemory)
X360_EXPORT(NtFreeVirtualMemory, NtFreeVirtualMemory)
X360_EXPORT(NtQueryVirtualMemory, NtQueryVirtualMemory)
X360_EXPORT(MmAllocatePhysicalMemoryEx, MmAllocatePhysicalMemoryEx)
X360_EXPORT(MmFreePhysicalMemory, MmFreePhysicalMemory)
X360_EXPORT(MmGetPhysicalAddress, MmGetPhysicalAddress)
X360_EXPORT(MmMapIoSpace, MmMapIoSpace)
X360_EXPORT(MmQueryAddressProtect, MmQueryAddressProtect)
X360_EXPORT(MmSetAddressProtect, MmSetAddressProtect)
X360_EXPORT(ExAllocatePoolTypeWithTag, ExAllocatePoolTypeWithTag)
X360_EXPORT(ExAllocatePool, ExAllocatePool)
X360_EXPORT(ExFreePool, ExFreePool)
