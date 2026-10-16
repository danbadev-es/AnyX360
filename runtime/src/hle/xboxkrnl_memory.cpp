// xboxkrnl memory exports on top of xe::Memory's heaps (semantics follow Xenia's
// xboxkrnl_memory.cc, BSD licensed).
#include <algorithm>

#include "x360/kernel.h"
#include "xenia/memory.h"

using namespace x360::kernel;

namespace {

constexpr uint32_t X_MEM_COMMIT = 0x00001000;
constexpr uint32_t X_MEM_RESERVE = 0x00002000;
constexpr uint32_t X_MEM_DECOMMIT = 0x00004000;
constexpr uint32_t X_MEM_RELEASE = 0x00008000;
constexpr uint32_t X_MEM_RESET = 0x00080000;
constexpr uint32_t X_MEM_TOP_DOWN = 0x00100000;
constexpr uint32_t X_MEM_NOZERO = 0x00800000;
constexpr uint32_t X_MEM_LARGE_PAGES = 0x20000000;
constexpr uint32_t X_MEM_16MB_PAGES = 0x80000000;

constexpr uint32_t X_PAGE_NOACCESS = 0x01;
constexpr uint32_t X_PAGE_READONLY = 0x02;
constexpr uint32_t X_PAGE_READWRITE = 0x04;
constexpr uint32_t X_PAGE_EXECUTE_READ = 0x20;
constexpr uint32_t X_PAGE_EXECUTE_READWRITE = 0x40;
constexpr uint32_t X_PAGE_NOCACHE = 0x200;
constexpr uint32_t X_PAGE_WRITECOMBINE = 0x400;

constexpr uint32_t X_STATUS_MEMORY_NOT_ALLOCATED = 0xC00000A0;

uint32_t FromXdkProtect(uint32_t protect) {
  uint32_t result = 0;
  if (protect & (X_PAGE_READONLY | X_PAGE_EXECUTE_READ)) {
    result = xe::kMemoryProtectRead;
  } else if (protect & (X_PAGE_READWRITE | X_PAGE_EXECUTE_READWRITE)) {
    result = xe::kMemoryProtectRead | xe::kMemoryProtectWrite;
  }
  if (protect & X_PAGE_NOCACHE) result |= xe::kMemoryProtectNoCache;
  if (protect & X_PAGE_WRITECOMBINE) result |= xe::kMemoryProtectWriteCombine;
  return result;
}

uint32_t ToXdkProtect(uint32_t protect) {
  uint32_t result = 0;
  if ((protect & xe::kMemoryProtectRead) && !(protect & xe::kMemoryProtectWrite)) result = X_PAGE_READONLY;
  else if ((protect & xe::kMemoryProtectRead) && (protect & xe::kMemoryProtectWrite)) result = X_PAGE_READWRITE;
  else result = X_PAGE_NOACCESS;
  if (protect & xe::kMemoryProtectNoCache) result |= X_PAGE_NOCACHE;
  if (protect & xe::kMemoryProtectWriteCombine) result |= X_PAGE_WRITECOMBINE;
  return result;
}

uint32_t RoundUp(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

uint32_t NtAllocateVirtualMemory(uint32_t base_ptr, uint32_t size_ptr, uint32_t alloc_type, uint32_t protect_bits,
                                 uint32_t debug_memory) {
  (void)debug_memory;
  xe::Memory* memory = XeMemory();
  uint32_t base = Load<uint32_t>(base_ptr);
  uint32_t size = Load<uint32_t>(size_ptr);
  if (!size) return X_STATUS_INVALID_PARAMETER;
  if (!(alloc_type & (X_MEM_COMMIT | X_MEM_RESET | X_MEM_RESERVE))) return X_STATUS_INVALID_PARAMETER;

  uint32_t page_size;
  if (base) {
    xe::BaseHeap* heap = memory->LookupHeap(base);
    if (!heap || heap->heap_type() != xe::HeapType::kGuestVirtual) return X_STATUS_INVALID_PARAMETER;
    page_size = heap->page_size();
  } else {
    page_size = (alloc_type & X_MEM_LARGE_PAGES) ? 0x10000 : 0x1000;
  }
  uint32_t adjusted_base = base - (base % page_size);
  uint32_t adjusted_size = int32_t(size) < 0 ? uint32_t(-int32_t(size)) : size;
  adjusted_size = RoundUp(adjusted_size, adjusted_base ? page_size : 0x10000);

  uint32_t allocation_type = 0;
  if (alloc_type & X_MEM_RESERVE) allocation_type |= xe::kMemoryAllocationReserve;
  if (alloc_type & X_MEM_COMMIT) allocation_type |= xe::kMemoryAllocationCommit;
  uint32_t protect = FromXdkProtect(protect_bits);

  uint32_t address = 0;
  xe::BaseHeap* heap;
  bool was_committed = false;
  if (adjusted_base) {
    heap = memory->LookupHeap(adjusted_base);
    if (heap->page_size() != page_size) return X_STATUS_ACCESS_DENIED;
    xe::HeapAllocationInfo previous{};
    was_committed = heap->QueryRegionInfo(adjusted_base, &previous) && (previous.state & xe::kMemoryAllocationCommit);
    if (heap->AllocFixed(adjusted_base, adjusted_size, page_size, allocation_type, protect)) address = adjusted_base;
  } else {
    heap = memory->LookupHeapByType(false, page_size);
    heap->Alloc(adjusted_size, page_size, allocation_type, protect, (alloc_type & X_MEM_TOP_DOWN) != 0, &address);
  }
  if (!address) {
    KLOG_W("NtAllocateVirtualMemory: out of memory (base %08X, size %08X, type %08X) from %s", base, size, alloc_type,
           GuestBacktrace(6).c_str());
    return X_STATUS_NO_MEMORY;
  }

  if (!(alloc_type & X_MEM_NOZERO) && (alloc_type & X_MEM_COMMIT)) {
    if (!(protect & xe::kMemoryProtectWrite)) heap->Protect(address, adjusted_size, xe::kMemoryProtectRead | xe::kMemoryProtectWrite);
    if (!was_committed) memory->Zero(address, adjusted_size);
    if (!(protect & xe::kMemoryProtectWrite)) heap->Protect(address, adjusted_size, protect);
  }
  if (TraceEnabled() || adjusted_size >= 0x1000000) {
    KLOG_I("NtAllocateVirtualMemory(%08X, %08X, type %08X) -> %08X", base, size, alloc_type, address);
  }
  Store<uint32_t>(base_ptr, address);
  Store<uint32_t>(size_ptr, adjusted_size);
  return X_STATUS_SUCCESS;
}

uint32_t NtFreeVirtualMemory(uint32_t base_ptr, uint32_t size_ptr, uint32_t free_type, uint32_t debug_memory) {
  (void)debug_memory;
  uint32_t base = Load<uint32_t>(base_ptr);
  uint32_t size = size_ptr ? Load<uint32_t>(size_ptr) : 0;
  if (!base) return X_STATUS_MEMORY_NOT_ALLOCATED;
  xe::BaseHeap* heap = XeMemory()->LookupHeap(base);
  if (!heap || heap->heap_type() != xe::HeapType::kGuestVirtual) return X_STATUS_INVALID_PARAMETER;
  bool ok;
  if (free_type == X_MEM_DECOMMIT) {
    size = RoundUp(size, heap->page_size());
    ok = heap->Decommit(base, size);
  } else {
    ok = heap->Release(base, &size);
  }
  if (!ok) return X_STATUS_UNSUCCESSFUL;
  Store<uint32_t>(base_ptr, base);
  if (size_ptr) Store<uint32_t>(size_ptr, size);
  return X_STATUS_SUCCESS;
}

uint32_t NtQueryVirtualMemory(uint32_t address, uint32_t info_ptr, uint32_t debug_memory) {
  (void)debug_memory;
  xe::BaseHeap* heap = XeMemory()->LookupHeap(address);
  xe::HeapAllocationInfo info{};
  if (!heap || !heap->QueryRegionInfo(address, &info)) return X_STATUS_INVALID_PARAMETER;
  Store<uint32_t>(info_ptr + 0x00, info.base_address);
  Store<uint32_t>(info_ptr + 0x04, info.allocation_base);
  Store<uint32_t>(info_ptr + 0x08, ToXdkProtect(info.allocation_protect));
  Store<uint32_t>(info_ptr + 0x0C, info.region_size);
  // State: MEM_COMMIT / MEM_RESERVE / MEM_FREE.
  uint32_t state = 0;
  if (info.state & xe::kMemoryAllocationCommit) state = X_MEM_COMMIT;
  else if (info.state & xe::kMemoryAllocationReserve) state = X_MEM_RESERVE;
  else state = 0x10000;
  Store<uint32_t>(info_ptr + 0x10, state);
  Store<uint32_t>(info_ptr + 0x14, ToXdkProtect(info.protect));
  Store<uint32_t>(info_ptr + 0x18, 0x20000);  // MEM_PRIVATE
  return X_STATUS_SUCCESS;
}

uint32_t MmAllocatePhysicalMemoryEx(uint32_t flags, uint32_t size, uint32_t protect_bits, uint32_t min_address,
                                    uint32_t max_address, uint32_t alignment) {
  (void)flags;
  if (!(protect_bits & (X_PAGE_READONLY | X_PAGE_READWRITE))) return 0;
  uint32_t page = (protect_bits & X_MEM_LARGE_PAGES) ? 0x10000 : (protect_bits & X_MEM_16MB_PAGES) ? 0x1000000 : 0x1000;
  uint32_t address = PhysicalAlloc(size, page, alignment, min_address, max_address, FromXdkProtect(protect_bits));
  if (!address) KLOG_W("MmAllocatePhysicalMemoryEx: failed (%X bytes, page %X)", size, page);
  return address;
}

void MmFreePhysicalMemory(uint32_t type, uint32_t address) {
  (void)type;
  PhysicalFree(address);
}

uint32_t MmGetPhysicalAddress(uint32_t address) { return GuestToPhysical(address); }

uint32_t MmMapIoSpace(uint32_t physical, uint32_t size, uint32_t protect) {
  (void)size, (void)protect;
  return physical;
}

uint32_t MmQueryAddressProtect(uint32_t address) {
  xe::BaseHeap* heap = XeMemory()->LookupHeap(address);
  uint32_t protect = 0;
  if (!heap || !heap->QueryProtect(address, &protect)) return 0;
  return ToXdkProtect(protect);
}

void MmSetAddressProtect(uint32_t address, uint32_t size, uint32_t protect_bits) {
  xe::BaseHeap* heap = XeMemory()->LookupHeap(address);
  if (heap) heap->Protect(address, size, FromXdkProtect(protect_bits));
}

uint32_t ExAllocatePoolTypeWithTag(uint32_t size, uint32_t tag, uint32_t pool_type) {
  (void)tag, (void)pool_type;
  return XeMemory()->SystemHeapAlloc(std::max<uint32_t>(size, 4));
}
uint32_t ExAllocatePool(uint32_t size) { return ExAllocatePoolTypeWithTag(size, 0, 0); }
void ExFreePool(uint32_t address) { XeMemory()->SystemHeapFree(address); }

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
