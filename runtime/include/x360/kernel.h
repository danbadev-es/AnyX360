// Core of the AnyX360 kernel: guest memory and heaps, kernel objects and handles, waits, guest
// threads and logging. Semantics follow the Xbox 360 kernel as documented by Xenia.
//
// Compiled per game (it includes the game's ppc_config.h through hle.h).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "x360/hle.h"

namespace x360::kernel {

// ---------------------------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------------------------

enum class LogLevel { Trace, Info, Warn, Error };
void Log(LogLevel level, const char* fmt, ...);
bool TraceEnabled();  // ANYX360_TRACE=1: log every HLE call

#define KLOG_T(...) do { if (::x360::kernel::TraceEnabled()) ::x360::kernel::Log(::x360::kernel::LogLevel::Trace, __VA_ARGS__); } while (0)
#define KLOG_I(...) ::x360::kernel::Log(::x360::kernel::LogLevel::Info, __VA_ARGS__)
#define KLOG_W(...) ::x360::kernel::Log(::x360::kernel::LogLevel::Warn, __VA_ARGS__)
#define KLOG_E(...) ::x360::kernel::Log(::x360::kernel::LogLevel::Error, __VA_ARGS__)

[[noreturn]] void Fatal(const char* fmt, ...);

// ---------------------------------------------------------------------------------------------
// Status codes
// ---------------------------------------------------------------------------------------------

enum : uint32_t {
  X_STATUS_SUCCESS = 0x00000000,
  X_STATUS_WAIT_0 = 0x00000000,
  X_STATUS_ABANDONED_WAIT_0 = 0x00000080,
  X_STATUS_USER_APC = 0x000000C0,
  X_STATUS_ALERTED = 0x00000101,
  X_STATUS_TIMEOUT = 0x00000102,
  X_STATUS_PENDING = 0x00000103,
  X_STATUS_OBJECT_NAME_EXISTS = 0x40000000,
  X_STATUS_BUFFER_OVERFLOW = 0x80000005,
  X_STATUS_NO_MORE_FILES = 0x80000006,
  X_STATUS_UNSUCCESSFUL = 0xC0000001,
  X_STATUS_NOT_IMPLEMENTED = 0xC0000002,
  X_STATUS_INVALID_INFO_CLASS = 0xC0000003,
  X_STATUS_INFO_LENGTH_MISMATCH = 0xC0000004,
  X_STATUS_INVALID_HANDLE = 0xC0000008,
  X_STATUS_INVALID_PARAMETER = 0xC000000D,
  X_STATUS_NO_SUCH_FILE = 0xC000000F,
  X_STATUS_END_OF_FILE = 0xC0000011,
  X_STATUS_NO_MEMORY = 0xC0000017,
  X_STATUS_ACCESS_DENIED = 0xC0000022,
  X_STATUS_BUFFER_TOO_SMALL = 0xC0000023,
  X_STATUS_OBJECT_TYPE_MISMATCH = 0xC0000024,
  X_STATUS_OBJECT_NAME_INVALID = 0xC0000033,
  X_STATUS_OBJECT_NAME_NOT_FOUND = 0xC0000034,
  X_STATUS_OBJECT_NAME_COLLISION = 0xC0000035,
  X_STATUS_OBJECT_PATH_NOT_FOUND = 0xC000003A,
  X_STATUS_MUTANT_NOT_OWNED = 0xC0000046,
  X_STATUS_SEMAPHORE_LIMIT_EXCEEDED = 0xC0000047,
  X_STATUS_FILE_IS_A_DIRECTORY = 0xC00000BA,
  X_STATUS_NOT_SUPPORTED = 0xC00000BB,
  X_STATUS_NOT_A_DIRECTORY = 0xC0000103,
  X_STATUS_DEVICE_NOT_CONNECTED = 0xC000009D,
};

// Win32 error codes returned by XAM.
enum : uint32_t {
  X_ERROR_SUCCESS = 0,
  X_ERROR_FILE_NOT_FOUND = 2,
  X_ERROR_PATH_NOT_FOUND = 3,
  X_ERROR_ACCESS_DENIED = 5,
  X_ERROR_INVALID_HANDLE = 6,
  X_ERROR_NO_MORE_FILES = 18,
  X_ERROR_INVALID_PARAMETER = 87,
  X_ERROR_INSUFFICIENT_BUFFER = 122,
  X_ERROR_ALREADY_EXISTS = 183,
  X_ERROR_IO_PENDING = 997,
  X_ERROR_FUNCTION_FAILED = 1627,
  X_ERROR_DEVICE_NOT_CONNECTED = 1167,
  X_ERROR_NOT_FOUND = 1168,
  X_ERROR_CANCELLED = 1223,
  X_ERROR_NOT_LOGGED_ON = 1245,
  X_ERROR_NO_SUCH_USER = 1317,
  X_ERROR_EMPTY = 4306,
};

// ---------------------------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------------------------

// Guest address map (see docs/ARCHITECTURE.md):
//   0x00010000-0x3FFFFFFF  4 KiB virtual heap  (NtAllocateVirtualMemory)
//   0x40000000-0x6FFFFFFF  64 KiB virtual heap (NtAllocateVirtualMemory, MEM_LARGE_PAGES)
//   0x70000000-0x7EFFFFFF  system heap (kernel objects, stacks, PCRs)
//   0x7FC00000-0x7FFFFFFF  GPU/APU register space (plain memory, polled by the runtime)
//   0x80000000-0x9FFFFFFF  executable image + recompiled function table
//   0xA0000000/0xC0000000/0xE0000000  three views of the same 512 MiB "physical" memory
constexpr uint32_t kPhysicalSize = 0x20000000;
constexpr uint32_t kGpuRegisterBase = 0x7FC80000;

extern uint8_t* g_base;

template <typename T = uint8_t>
inline T* Ptr(uint32_t addr) {
  return addr ? reinterpret_cast<T*>(g_base + addr) : nullptr;
}
inline uint32_t Addr(const void* p) { return p ? uint32_t(static_cast<const uint8_t*>(p) - g_base) : 0; }

template <typename T>
inline T Load(uint32_t addr) {
  return load_be<T>(g_base + addr);
}
template <typename T>
inline void Store(uint32_t addr, T v) {
  store_be<T>(g_base + addr, v);
}

// First-fit page allocator over a guest range. Thread-safe.
class PageHeap {
 public:
  PageHeap(const char* name, uint32_t base, uint32_t size, uint32_t page_size);

  // Returns 0 on failure. `alignment` is rounded up to the page size.
  uint32_t Alloc(uint32_t size, uint32_t alignment = 0, bool top_down = false, uint32_t min_address = 0,
                 uint32_t max_address = 0xFFFFFFFF);
  bool AllocFixed(uint32_t address, uint32_t size);
  bool Free(uint32_t address, uint32_t* freed_size = nullptr);
  // Finds the allocation containing `address`.
  bool Query(uint32_t address, uint32_t* alloc_base, uint32_t* alloc_size) const;

  bool Contains(uint32_t address) const { return address >= base_ && address - base_ < size_; }
  uint32_t base() const { return base_; }
  uint32_t size() const { return size_; }
  uint32_t page_size() const { return page_size_; }
  const char* name() const { return name_; }

 private:
  const char* name_;
  uint32_t base_, size_, page_size_;
  mutable std::mutex mutex_;
  std::map<uint32_t, uint32_t> free_;  // address -> size
  std::map<uint32_t, uint32_t> used_;  // address -> size
};

bool InitMemory();
PageHeap& HeapV4k();
PageHeap& HeapV64k();
PageHeap& HeapSystem();

// Small zeroed allocations for kernel structures (16-byte aligned, from the system heap).
uint32_t SystemAlloc(uint32_t size, uint32_t alignment = 16);
void SystemFree(uint32_t address);

// Physical memory: allocates in physical space and returns the guest address in the window that
// matches the page size (0xE0000000 for 4 KiB, 0xA0000000 for 64 KiB, 0xC0000000 for 16 MiB).
uint32_t PhysicalAlloc(uint32_t size, uint32_t page_size, uint32_t alignment, uint32_t min_physical,
                       uint32_t max_physical);
bool PhysicalFree(uint32_t guest_address);
// Physical address (as seen by the GPU) of a guest address, and the inverse (0xA0000000 window).
uint32_t GuestToPhysical(uint32_t guest_address);
inline uint32_t PhysicalToGuest(uint32_t physical) { return 0xA0000000u + (physical & 0x1FFFFFFF); }

// ---------------------------------------------------------------------------------------------
// Objects and handles
// ---------------------------------------------------------------------------------------------

enum ObjectType : uint8_t {
  kEventNotification = 0,
  kEventSynchronization = 1,
  kMutant = 2,
  kProcess = 3,
  kQueue = 4,
  kSemaphore = 5,
  kThread = 6,
  kTimerNotification = 8,
  kTimerSynchronization = 9,
};

struct DispatchHeader {
  uint8_t type;
  uint8_t absolute;
  uint8_t size;
  uint8_t inserted;
  be<int32_t> signal_state;
  be<uint32_t> wait_list_flink;
  be<uint32_t> wait_list_blink;
};
static_assert(sizeof(DispatchHeader) == 16);

// Host-side object behind a handle. `guest` is the guest body (dispatcher header first for
// waitable objects), 0 for objects without one (files, enumerators...).
struct Object {
  virtual ~Object() = default;
  virtual const char* TypeName() const = 0;
  uint32_t guest = 0;
  std::string name;
};

constexpr uint32_t kCurrentThreadHandle = 0xFFFFFFFE;
constexpr uint32_t kCurrentProcessHandle = 0xFFFFFFFF;

uint32_t CreateHandle(std::shared_ptr<Object> object);
std::shared_ptr<Object> LookupHandle(uint32_t handle);
template <typename T>
std::shared_ptr<T> LookupHandleAs(uint32_t handle) {
  return std::dynamic_pointer_cast<T>(LookupHandle(handle));
}
bool CloseHandle(uint32_t handle);
uint32_t DuplicateHandle(uint32_t handle);
// Named objects (NtCreateEvent with a name, etc.).
std::shared_ptr<Object> LookupNamedObject(const std::string& name);
void RegisterNamedObject(const std::string& name, std::shared_ptr<Object> object);

// A waitable object living in guest memory (event, semaphore, mutant, timer...).
struct DispatcherObject : Object {
  explicit DispatcherObject(uint32_t body) { guest = body; }
  const char* TypeName() const override { return "Dispatcher"; }
};

// Guest-address keyed waits. Everything is serialized by one dispatcher lock; any state change
// wakes all waiters, which re-check their conditions.
std::mutex& DispatcherLock();
void NotifyDispatcherChange();  // caller holds the lock

// timeout: nullptr = infinite; NT semantics otherwise (negative relative 100 ns, positive absolute).
uint32_t WaitForObjects(const uint32_t* objects, uint32_t count, bool wait_all, const int64_t* timeout,
                        bool alertable);
uint32_t WaitForObject(uint32_t object, const int64_t* timeout, bool alertable);
// Guest body for a handle (nullptr-safe), handling the pseudo current-thread handle.
uint32_t ObjectFromHandle(uint32_t handle);

void InitializeDispatcher(uint32_t body, uint8_t type, int32_t signal_state);
int32_t SetEvent(uint32_t event);
int32_t ResetEvent(uint32_t event);
int32_t PulseEvent(uint32_t event);
bool ReleaseSemaphore(uint32_t semaphore, int32_t count, int32_t* previous);
bool ReleaseMutant(uint32_t mutant, bool abandon, int32_t* previous);

// ---------------------------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------------------------

struct Thread : Object {
  const char* TypeName() const override { return "Thread"; }

  uint32_t thread_id = 0;
  uint32_t handle = 0;
  uint32_t kthread = 0;  // guest X_KTHREAD (== guest body)
  uint32_t pcr = 0;
  uint32_t stack_base = 0;   // high address
  uint32_t stack_limit = 0;  // low address
  uint32_t tls_static = 0;
  uint32_t tls_dynamic = 0;
  uint32_t start_address = 0;
  uint32_t start_context = 0;
  uint32_t xapi_startup = 0;
  uint32_t exit_code = 0;
  uint8_t cpu = 0;

  void* host = nullptr;  // Windows HANDLE
  PPCContext* context = nullptr;
  std::function<void()> host_body;  // host-driven threads (interrupts, audio)

  std::mutex suspend_mutex;
  std::condition_variable suspend_cv;
  int suspend_count = 0;
};

Thread* CurrentThread();
std::shared_ptr<Thread> CurrentThreadShared();
std::shared_ptr<Thread> ThreadById(uint32_t id);

// Creates and starts (unless suspended) a guest thread. Returns X_STATUS.
uint32_t CreateThread(uint32_t stack_size, uint32_t xapi_startup, uint32_t start_address, uint32_t start_context,
                      uint32_t creation_flags, std::shared_ptr<Thread>* out);
// Starts the title's main thread and blocks until it exits.
uint32_t RunMainThread(uint32_t entry_point, uint32_t stack_size);
// Calls a guest function on the current guest thread (callbacks from HLE code).
uint64_t CallGuest(uint32_t function, std::initializer_list<uint64_t> args);
// A host thread that owns a guest context, so host code (interrupts, audio) can call guest code.
std::shared_ptr<Thread> CreateHostDrivenThread(const char* name, uint8_t cpu, std::function<void()> body);
// Ends the calling guest thread (ExTerminateThread). Never returns.
[[noreturn]] void ExitCurrentThread(uint32_t exit_code);
// Returns the new suspend count.
int ResumeThread(Thread& thread);
int SuspendThread(Thread& thread);

// ---------------------------------------------------------------------------------------------
// Title / loader
// ---------------------------------------------------------------------------------------------

struct TitleInfo {
  std::filesystem::path xex_path;     // default.xex
  std::filesystem::path game_root;    // folder mounted as game:\ and d:\ (contains default.xex)
  std::filesystem::path user_root;    // writable folder for hdd, cache and saves
  std::filesystem::path key_path;     // keys/xex2_retail.key
};

struct LoadedTitle {
  uint32_t entry_point = 0;
  uint32_t stack_size = 0x40000;
  uint32_t image_base = 0;
  uint32_t image_size = 0;
  uint32_t xex_header = 0;  // guest copy of the XEX header (for RtlImageXexHeaderField)
  uint32_t ldr_entry = 0;   // X_LDR_DATA_TABLE_ENTRY
  uint32_t title_id = 0;
  uint32_t tls_slot_count = 64;
  uint32_t tls_raw_address = 0;
  uint32_t tls_data_size = 0;
  uint32_t tls_raw_size = 0;
};

const LoadedTitle& Title();
const TitleInfo& TitlePaths();

// Loads the executable, patches imports and starts the kernel services. Returns false on error.
bool LoadTitle(const TitleInfo& info);
// Looks up a guest address of an xboxkrnl/xam variable export set up by the loader.
uint32_t KernelVariable(const char* name);

// Entry point used by game executables: loads the title and runs its main thread.
int Run(const TitleInfo& info);

// Subsystems started by the loader.
void InstallCrashHandler();
// Guest call stack of the current thread (LR, then saved return addresses).
std::string GuestBacktrace(int max_frames = 12);
void InitTime();
void InitIo(const TitleInfo& info);
void InitXam();
void InitVideo();
void InitAudio();

// 100 ns ticks since 1601 (Windows FILETIME), as the 360 reports.
uint64_t SystemTime();
// Converts an NT timeout to an absolute host deadline; returns false for "infinite".
bool TimeoutToDeadline(const int64_t* timeout, std::chrono::steady_clock::time_point* deadline);

}  // namespace x360::kernel
