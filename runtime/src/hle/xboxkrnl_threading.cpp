// xboxkrnl threading and synchronization exports.
#include <algorithm>
#include <set>
#include <bitset>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

#include "x360/kernel.h"
#include "x360/settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace x360::kernel;

namespace {

// ---- helpers ----------------------------------------------------------------------------------

std::string ObjectName(uint32_t object_attributes) {
  if (!object_attributes) return {};
  uint32_t name = Load<uint32_t>(object_attributes + 4);
  if (!name) return {};
  uint16_t length = Load<uint16_t>(name);
  uint32_t buffer = Load<uint32_t>(name + 4);
  if (!buffer || !length) return {};
  return std::string(Ptr<char>(buffer), length);
}

const int64_t* TimeoutPtr(uint32_t timeout_ptr, int64_t& storage) {
  if (!timeout_ptr) return nullptr;
  storage = Load<int64_t>(timeout_ptr);
  return &storage;
}

// Creates (or opens, for named objects) a dispatcher object with a guest body of `body_size`.
uint32_t CreateDispatcher(uint32_t handle_ptr, uint32_t object_attributes, uint32_t body_size,
                          const std::function<void(uint32_t)>& init) {
  std::string name = ObjectName(object_attributes);
  if (!name.empty()) {
    if (auto existing = LookupNamedObject(name)) {
      if (handle_ptr) Store<uint32_t>(handle_ptr, CreateHandle(existing));
      return X_STATUS_OBJECT_NAME_EXISTS;
    }
  }
  uint32_t body = SystemAlloc(body_size);
  if (!body) return X_STATUS_NO_MEMORY;
  init(body);
  auto object = std::make_shared<DispatcherObject>(body);
  object->name = name;
  if (!name.empty()) RegisterNamedObject(name, object);
  if (handle_ptr) Store<uint32_t>(handle_ptr, CreateHandle(object));
  return X_STATUS_SUCCESS;
}

// ---- threads ----------------------------------------------------------------------------------

uint32_t ExCreateThread(uint32_t handle_ptr, uint32_t stack_size, uint32_t thread_id_ptr, uint32_t xapi_startup,
                        uint32_t start_address, uint32_t start_context, uint32_t creation_flags) {
  std::shared_ptr<Thread> thread;
  uint32_t status = CreateThread(stack_size, xapi_startup, start_address, start_context, creation_flags, &thread);
  if (status != X_STATUS_SUCCESS) return status;
  // Creation flag 0x80: the caller gets the thread's KTHREAD instead of a handle, and starts it with
  // KeResumeThread (Forza Motorsport 4's audio thread, created suspended).
  if (handle_ptr) Store<uint32_t>(handle_ptr, (creation_flags & 0x80) ? thread->kthread : thread->handle);
  if (thread_id_ptr) Store<uint32_t>(thread_id_ptr, thread->thread_id);
  KLOG_I("ExCreateThread: id %X start %08X ctx %08X flags %08X%s", thread->thread_id, start_address, start_context,
         creation_flags, (creation_flags & 1) ? " (suspended)" : "");
  return X_STATUS_SUCCESS;
}

void ExTerminateThread(uint32_t exit_code) { ExitCurrentThread(exit_code); }

uint32_t NtResumeThread(uint32_t handle, uint32_t suspend_count_ptr) {
  auto thread = LookupHandleAs<Thread>(handle);
  if (!thread) return X_STATUS_INVALID_HANDLE;
  int previous = ResumeThread(*thread);
  if (suspend_count_ptr) Store<uint32_t>(suspend_count_ptr, uint32_t(previous));
  return X_STATUS_SUCCESS;
}

// The kernel-mode form takes the thread's KTHREAD (its id at 0x14C) and returns the previous suspend
// count. Modern Warfare 3 creates its render and worker threads suspended and starts them with it.
uint32_t KeResumeThread(uint32_t kthread) {
  auto thread = kthread ? ThreadById(Load<uint32_t>(kthread + 0x14C)) : nullptr;
  if (!thread || thread->kthread != kthread) return 0;
  return uint32_t(ResumeThread(*thread));
}

uint32_t NtSuspendThread(uint32_t handle, uint32_t suspend_count_ptr) {
  auto thread = LookupHandleAs<Thread>(handle);
  if (!thread) return X_STATUS_INVALID_HANDLE;
  int previous = SuspendThread(*thread);
  if (suspend_count_ptr) Store<uint32_t>(suspend_count_ptr, uint32_t(previous));
  return X_STATUS_SUCCESS;
}

// XSetThreadProcessor: the thread moves to that hardware thread. On the console r13 then points at
// that processor's PCR, so the thread reads its new number at PCR+0x10C; here each thread has a PCR
// of its own, which is updated. Modern Warfare 3 keeps per-processor data (indexed by that number)
// for the threads it places on each one; with every thread still reading its creator's number, its
// server and worker threads shared one and the level load crashed.
uint32_t KeSetAffinityThread(uint32_t kthread, uint32_t affinity, uint32_t previous_ptr) {
  if (previous_ptr) Store<uint32_t>(previous_ptr, 1u << Load<uint8_t>(kthread + 0xBF));
  for (uint8_t i = 0; i < 6; ++i) {
    if (affinity & (1u << i)) {
      Store<uint8_t>(kthread + 0xBF, i);
      if (auto thread = ThreadById(Load<uint32_t>(kthread + 0x14C)); thread && thread->kthread == kthread) {
        static std::mutex mutex;  // log each (thread, processor) once
        static std::set<std::pair<uint32_t, uint8_t>> seen;
        {
          std::lock_guard lock(mutex);
          if (seen.insert({thread->thread_id, i}).second)
            KLOG_I("KeSetAffinityThread: thread %X on processor %u", thread->thread_id, i);
        }
        thread->cpu = i;
        Store<uint8_t>(thread->pcr + 0x10C, i);
      }
      break;
    }
  }
  return X_STATUS_SUCCESS;
}

int32_t KeQueryBasePriorityThread(uint32_t kthread) { return int8_t(Load<uint8_t>(kthread + 0xB9)); }

// The increment is Win32-like (Gears of War 3 uses -1, 0, 1, 2 and 16/17 for its time-critical
// threads); never TIME_CRITICAL on the host, which could starve the system.
int HostThreadPriority(int32_t increment) {
  if (increment >= 15) return THREAD_PRIORITY_HIGHEST;
  if (increment >= 1) return THREAD_PRIORITY_ABOVE_NORMAL;
  if (increment == 0) return THREAD_PRIORITY_NORMAL;
  if (increment > -15) return THREAD_PRIORITY_BELOW_NORMAL;
  return THREAD_PRIORITY_LOWEST;
}

int32_t KeSetBasePriorityThread(uint32_t kthread, int32_t increment) {
  int32_t previous = int8_t(Load<uint8_t>(kthread + 0xB9));
  Store<uint8_t>(kthread + 0xB9, uint8_t(increment));
  uint32_t thread_id = Load<uint32_t>(kthread + 0x14C);
  {  // log each (thread, priority) once
    static std::mutex mutex;
    static std::set<std::pair<uint32_t, int32_t>> seen;
    std::lock_guard lock(mutex);
    if (seen.insert({thread_id, increment}).second)
      KLOG_I("KeSetBasePriorityThread(thread %X, %d) -> host priority %d", thread_id, increment,
             HostThreadPriority(increment));
  }
  static const bool apply = x360::settings::Get().pacing.thread_priorities;
  if (apply)
    if (auto thread = ThreadById(thread_id); thread && thread->host)
      ::SetThreadPriority(static_cast<HANDLE>(thread->host), HostThreadPriority(increment));
  return previous;
}

uint32_t KeDelayExecutionThread(uint32_t processor_mode, uint32_t alertable, uint32_t interval_ptr) {
  (void)processor_mode;
  // An alertable sleep (SleepEx(..., TRUE)) runs the thread's I/O completion routines instead:
  // Modern Warfare 3's fast file loader waits for its ReadFileEx completions that way.
  if (alertable && DeliverUserApcs()) return X_STATUS_USER_APC;
  int64_t interval = interval_ptr ? Load<int64_t>(interval_ptr) : 0;
  int64_t relative = interval < 0 ? -interval : int64_t(interval - SystemTime());

  if (relative <= 0) {
    std::this_thread::yield();
  } else {
    std::this_thread::sleep_for(std::chrono::microseconds(relative / 10));
  }
  return X_STATUS_SUCCESS;
}

uint32_t NtYieldExecution() {
  std::this_thread::yield();
  return X_STATUS_SUCCESS;
}

uint32_t g_process_type = 1;
void KeSetCurrentProcessType(uint32_t type) { g_process_type = type; }

void KeEnterCriticalRegion() {}
void KeLeaveCriticalRegion() {}
void KeLockL2() {}
void KeUnlockL2() {}
void KiApcNormalRoutineNop() {}

void KeInitializeDpc(uint32_t dpc, uint32_t routine, uint32_t context) {
  Store<uint8_t>(dpc + 0, 0x13);  // DpcObject
  Store<uint32_t>(dpc + 0x0C, routine);
  Store<uint32_t>(dpc + 0x10, context);
}

// KeInsertQueueDpc(dpc, argument 1, argument 2): the console runs routine(dpc, context, argument 1,
// argument 2) on the calling processor once its IRQL drops below DISPATCH_LEVEL, which for a caller at
// passive level is at once. There are no IRQLs here, so it runs on the calling thread before
// returning, as in Xenia. Modern Warfare 3's Direct3D signals an event of its own from one.
uint32_t KeInsertQueueDpc(uint32_t dpc, uint32_t argument1, uint32_t argument2) {
  Store<uint32_t>(dpc + 0x14, argument1);
  Store<uint32_t>(dpc + 0x18, argument2);
  if (uint32_t routine = Load<uint32_t>(dpc + 0x0C))
    CallGuest(routine, {dpc, Load<uint32_t>(dpc + 0x10), argument1, argument2});
  return 1;
}

// IRQLs are not modelled (KfAcquireSpinLock reports passive level as the previous one too).
uint32_t KeRaiseIrqlToDpcLevel() { return 0; }
void KfLowerIrql(uint32_t irql) { (void)irql; }

[[noreturn]] void KeBugCheckEx(uint32_t code, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  Fatal("KeBugCheckEx(%08X, %08X, %08X, %08X, %08X)", code, a, b, c, d);
}
[[noreturn]] void KeBugCheck(uint32_t code) { Fatal("KeBugCheck(%08X)", code); }

// ---- TLS --------------------------------------------------------------------------------------

std::mutex g_tls_mutex;
std::bitset<4096> g_tls_used;

uint32_t KeTlsAlloc() {
  std::lock_guard lock(g_tls_mutex);
  for (uint32_t i = 0; i < Title().tls_slot_count && i < g_tls_used.size(); ++i) {
    if (!g_tls_used[i]) {
      g_tls_used[i] = true;
      return i;
    }
  }
  return 0xFFFFFFFF;
}

uint32_t KeTlsFree(uint32_t index) {
  std::lock_guard lock(g_tls_mutex);
  if (index >= g_tls_used.size()) return 0;
  g_tls_used[index] = false;
  return 1;
}

uint32_t KeTlsGetValue(uint32_t index) {
  Thread* t = CurrentThread();
  if (!t || index >= Title().tls_slot_count) return 0;
  return Load<uint32_t>(t->tls_dynamic + index * 4);
}

uint32_t KeTlsSetValue(uint32_t index, uint32_t value) {
  Thread* t = CurrentThread();
  if (!t || index >= Title().tls_slot_count) return 0;
  Store<uint32_t>(t->tls_dynamic + index * 4, value);
  return 1;
}

// ---- spin locks -------------------------------------------------------------------------------

void AcquireSpinLock(uint32_t lock) {
  auto* word = reinterpret_cast<volatile LONG*>(Ptr(lock));
  Thread* t = CurrentThread();
  LONG value = LONG(_byteswap_ulong(t ? t->kthread : 1));
  for (unsigned spins = 0; InterlockedCompareExchange(word, value, 0) != 0; ++spins) {
    if (spins > 64) std::this_thread::yield();
  }
}

void ReleaseSpinLock(uint32_t lock) {
  InterlockedExchange(reinterpret_cast<volatile LONG*>(Ptr(lock)), 0);
}

uint32_t KfAcquireSpinLock(uint32_t lock) {
  AcquireSpinLock(lock);
  return 0;  // previous IRQL
}
void KfReleaseSpinLock(uint32_t lock, uint32_t old_irql) {
  (void)old_irql;
  ReleaseSpinLock(lock);
}
void KeAcquireSpinLockAtRaisedIrql(uint32_t lock) { AcquireSpinLock(lock); }
void KeReleaseSpinLockFromRaisedIrql(uint32_t lock) { ReleaseSpinLock(lock); }
// One attempt: 1 if the lock was free and is now the caller's, 0 if someone else holds it.
uint32_t KeTryToAcquireSpinLockAtRaisedIrql(uint32_t lock) {
  Thread* t = CurrentThread();
  LONG value = LONG(_byteswap_ulong(t ? t->kthread : 1));
  return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(Ptr(lock)), value, 0) == 0 ? 1 : 0;
}

// ---- events / semaphores / mutants / timers ----------------------------------------------------

uint32_t NtCreateEvent(uint32_t handle_ptr, uint32_t object_attributes, uint32_t event_type, uint32_t initial_state) {
  return CreateDispatcher(handle_ptr, object_attributes, 0x10, [&](uint32_t body) {
    InitializeDispatcher(body, event_type == 0 ? kEventNotification : kEventSynchronization, initial_state ? 1 : 0);
  });
}

uint32_t NtSetEvent(uint32_t handle, uint32_t previous_ptr) {
  uint32_t body = ObjectFromHandle(handle);
  if (!body) return X_STATUS_INVALID_HANDLE;
  int32_t previous = SetEvent(body);
  if (previous_ptr) Store<int32_t>(previous_ptr, previous);
  return X_STATUS_SUCCESS;
}

uint32_t NtClearEvent(uint32_t handle) {
  uint32_t body = ObjectFromHandle(handle);
  if (!body) return X_STATUS_INVALID_HANDLE;
  ResetEvent(body);
  return X_STATUS_SUCCESS;
}

uint32_t NtPulseEvent(uint32_t handle, uint32_t previous_ptr) {
  uint32_t body = ObjectFromHandle(handle);
  if (!body) return X_STATUS_INVALID_HANDLE;
  int32_t previous = PulseEvent(body);
  if (previous_ptr) Store<int32_t>(previous_ptr, previous);
  return X_STATUS_SUCCESS;
}

int32_t KeSetEvent(uint32_t event, uint32_t increment, uint32_t wait) {
  (void)increment;
  (void)wait;
  return SetEvent(event);
}

int32_t KeResetEvent(uint32_t event) { return ResetEvent(event); }

uint32_t NtCreateSemaphore(uint32_t handle_ptr, uint32_t object_attributes, int32_t initial, int32_t maximum) {
  return CreateDispatcher(handle_ptr, object_attributes, 0x14, [&](uint32_t body) {
    InitializeDispatcher(body, kSemaphore, initial);
    Store<int32_t>(body + 0x10, maximum);
  });
}

uint32_t NtReleaseSemaphore(uint32_t handle, int32_t count, uint32_t previous_ptr) {
  uint32_t body = ObjectFromHandle(handle);
  if (!body) return X_STATUS_INVALID_HANDLE;
  int32_t previous = 0;
  bool ok = ReleaseSemaphore(body, count, &previous);
  if (previous_ptr) Store<int32_t>(previous_ptr, previous);
  return ok ? X_STATUS_SUCCESS : X_STATUS_SEMAPHORE_LIMIT_EXCEEDED;
}

// Kernel-mode semaphores live in the title's memory (KSEMAPHORE: header, limit at 0x10) and are
// waited on by address with KeWaitFor*Object(s).
void KeInitializeSemaphore(uint32_t semaphore, int32_t count, int32_t limit) {
  InitializeDispatcher(semaphore, kSemaphore, count);
  Store<int32_t>(semaphore + 0x10, limit);
}

// KeReleaseSemaphore(semaphore, priority increment, count, wait): returns the previous count.
int32_t KeReleaseSemaphore(uint32_t semaphore, uint32_t increment, int32_t count, uint32_t wait) {
  (void)increment, (void)wait;
  int32_t previous = 0;
  if (!ReleaseSemaphore(semaphore, count, &previous)) KLOG_W("KeReleaseSemaphore(%08X): limit exceeded", semaphore);
  return previous;
}

uint32_t NtCreateMutant(uint32_t handle_ptr, uint32_t object_attributes, uint32_t initial_owner) {
  return CreateDispatcher(handle_ptr, object_attributes, 0x20, [&](uint32_t body) {
    InitializeDispatcher(body, kMutant, initial_owner ? 0 : 1);
    Store<uint32_t>(body + 0x10, body + 0x10);
    Store<uint32_t>(body + 0x14, body + 0x10);
    Store<uint32_t>(body + 0x18, initial_owner ? CurrentKthread() : 0);
  });
}

uint32_t NtReleaseMutant(uint32_t handle, uint32_t previous_ptr) {
  uint32_t body = ObjectFromHandle(handle);
  if (!body) return X_STATUS_INVALID_HANDLE;
  int32_t previous = 0;
  bool ok = ReleaseMutant(body, false, &previous);
  if (previous_ptr) Store<int32_t>(previous_ptr, previous);
  return ok ? X_STATUS_SUCCESS : X_STATUS_MUTANT_NOT_OWNED;
}

// Timers: a host thread signals them at their due time.
struct TimerState {
  uint32_t body;
  std::chrono::steady_clock::time_point due;
  std::chrono::milliseconds period;
  uint64_t generation;
};
std::mutex g_timer_mutex;
std::condition_variable g_timer_cv;
std::map<uint32_t, TimerState> g_timers;  // body -> state
uint64_t g_timer_generation = 0;
std::once_flag g_timer_thread_once;

void TimerThread() {
  std::unique_lock lock(g_timer_mutex);
  for (;;) {
    auto now = std::chrono::steady_clock::now();
    auto next = now + std::chrono::hours(1);
    for (auto it = g_timers.begin(); it != g_timers.end();) {
      TimerState& s = it->second;
      if (s.due <= now) {
        SignalDispatcherObject(s.body);
        if (s.period.count() > 0) {
          s.due += s.period;
          if (s.due < now) s.due = now + s.period;
        } else {
          it = g_timers.erase(it);
          continue;
        }
      }
      next = std::min(next, s.due);
      ++it;
    }
    g_timer_cv.wait_until(lock, next);
  }
}

uint32_t NtCreateTimer(uint32_t handle_ptr, uint32_t object_attributes, uint32_t timer_type) {
  return CreateDispatcher(handle_ptr, object_attributes, 0x28, [&](uint32_t body) {
    InitializeDispatcher(body, timer_type == 0 ? kTimerNotification : kTimerSynchronization, 0);
  });
}

uint32_t NtSetTimerEx(uint32_t handle, uint32_t due_time_ptr, uint32_t apc_routine, uint32_t apc_mode,
                      uint32_t apc_argument, uint32_t resume, int32_t period_ms, uint32_t previous_ptr) {
  (void)apc_mode;
  (void)apc_argument;
  (void)resume;
  uint32_t body = ObjectFromHandle(handle);
  if (!body) return X_STATUS_INVALID_HANDLE;
  if (apc_routine) KLOG_W("NtSetTimerEx: timer APC routines are not delivered (%08X)", apc_routine);
  std::call_once(g_timer_thread_once, [] { std::thread(TimerThread).detach(); });

  int64_t due = due_time_ptr ? Load<int64_t>(due_time_ptr) : 0;
  std::chrono::steady_clock::time_point deadline;
  TimeoutToDeadline(&due, &deadline);
  if (previous_ptr) Store<uint32_t>(previous_ptr, Ptr<DispatchHeader>(body)->signal_state.get() ? 1 : 0);
  {
    std::lock_guard dispatch(DispatcherLock());
    Ptr<DispatchHeader>(body)->signal_state = 0;
  }
  std::lock_guard lock(g_timer_mutex);
  g_timers[body] = TimerState{body, deadline, std::chrono::milliseconds(std::max(period_ms, 0)), ++g_timer_generation};
  g_timer_cv.notify_all();
  return X_STATUS_SUCCESS;
}

uint32_t NtCancelTimer(uint32_t handle, uint32_t previous_ptr) {
  uint32_t body = ObjectFromHandle(handle);
  if (!body) return X_STATUS_INVALID_HANDLE;
  if (previous_ptr) Store<uint32_t>(previous_ptr, Ptr<DispatchHeader>(body)->signal_state.get() ? 1 : 0);
  std::lock_guard lock(g_timer_mutex);
  g_timers.erase(body);
  return X_STATUS_SUCCESS;
}

// ---- waits ------------------------------------------------------------------------------------

uint32_t NtWaitForSingleObjectEx(uint32_t handle, uint32_t wait_mode, uint32_t alertable, uint32_t timeout_ptr) {
  (void)wait_mode;
  uint32_t body = ObjectFromHandle(handle);
  // A file: its I/O is always complete here (a read on an asynchronous file answers STATUS_PENDING
  // when it is already done, and XAPI then waits on the file), so it is signalled.
  if (!body) {
    auto object = LookupHandle(handle);
    return object && std::strcmp(object->TypeName(), "File") == 0 ? X_STATUS_SUCCESS : X_STATUS_INVALID_HANDLE;
  }
  int64_t timeout;
  return WaitForObject(body, TimeoutPtr(timeout_ptr, timeout), alertable != 0);
}

// ---- kernel stacks ----------------------------------------------------------------------------
// A title can run code on a stack of its own: MmCreateKernelStack gives one (its top address), and
// KeSetCurrentStackPointers (below, written by hand: it sets r1) switches the thread to it and back
// (Forza Motorsport 4).

uint32_t MmCreateKernelStack(uint32_t stack_size, uint32_t unknown) {
  (void)unknown;
  const uint32_t size = (stack_size + 0xFFF) & ~0xFFFu;
  const uint32_t stack = SystemAlloc(std::max<uint32_t>(size, 0x4000), (stack_size & 0xF000) ? 0x1000 : 0x10000);
  return stack ? stack + size : 0;
}

// MmDeleteKernelStack(stack base (high), stack end (low)).
uint32_t MmDeleteKernelStack(uint32_t stack_base, uint32_t stack_end) {
  (void)stack_base;
  SystemFree(stack_end);
  return X_STATUS_SUCCESS;
}

// Signals one object (an event is set, a semaphore released once, a mutant released) and waits on
// another (Forza Motorsport 4's video decoder hands frames between its threads with it).
uint32_t NtSignalAndWaitForSingleObjectEx(uint32_t signal_handle, uint32_t wait_handle, uint32_t alertable,
                                          uint32_t wait_mode, uint32_t timeout_ptr) {
  (void)wait_mode;
  const uint32_t signal = ObjectFromHandle(signal_handle);
  const uint32_t wait = ObjectFromHandle(wait_handle);
  if (!signal || !wait) return X_STATUS_INVALID_HANDLE;
  switch (Ptr<DispatchHeader>(signal)->type) {
    case kEventNotification:
    case kEventSynchronization:
      SetEvent(signal);
      break;
    case kSemaphore: {
      int32_t previous = 0;
      if (!ReleaseSemaphore(signal, 1, &previous)) return X_STATUS_SEMAPHORE_LIMIT_EXCEEDED;
      break;
    }
    case kMutant: {
      int32_t previous = 0;
      if (!ReleaseMutant(signal, false, &previous)) return X_STATUS_MUTANT_NOT_OWNED;
      break;
    }
    default:
      return X_STATUS_OBJECT_TYPE_MISMATCH;
  }
  int64_t timeout;
  return WaitForObject(wait, TimeoutPtr(timeout_ptr, timeout), alertable != 0);
}

uint32_t NtWaitForMultipleObjectsEx(uint32_t count, uint32_t handles_ptr, uint32_t wait_type, uint32_t wait_mode,
                                    uint32_t alertable, uint32_t timeout_ptr) {
  (void)wait_mode;
  if (count == 0 || count > 64) return X_STATUS_INVALID_PARAMETER;
  uint32_t objects[64];
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t handle = Load<uint32_t>(handles_ptr + i * 4);
    objects[i] = ObjectFromHandle(handle);
    if (!objects[i]) {
      static std::mutex logged_mutex;
      static std::set<uint32_t> logged;
      std::lock_guard lock(logged_mutex);
      if (logged.insert(handle).second)
        KLOG_W("NtWaitForMultipleObjectsEx: handle %u of %u is not an object (%08X)", i, count, handle);
      return X_STATUS_INVALID_HANDLE;
    }
  }
  int64_t timeout;
  return WaitForObjects(objects, count, wait_type == 0, TimeoutPtr(timeout_ptr, timeout), alertable != 0);
}

uint32_t KeWaitForSingleObject(uint32_t object, uint32_t reason, uint32_t mode, uint32_t alertable,
                               uint32_t timeout_ptr) {
  (void)reason;
  (void)mode;
  int64_t timeout;
  return WaitForObject(object, TimeoutPtr(timeout_ptr, timeout), alertable != 0);
}

uint32_t KeWaitForMultipleObjects(uint32_t count, uint32_t objects_ptr, uint32_t wait_type, uint32_t reason,
                                  uint32_t mode, uint32_t alertable, uint32_t timeout_ptr, uint32_t wait_blocks) {
  (void)reason;
  (void)mode;
  (void)wait_blocks;
  if (count == 0 || count > 64) return X_STATUS_INVALID_PARAMETER;
  uint32_t objects[64];
  for (uint32_t i = 0; i < count; ++i) objects[i] = Load<uint32_t>(objects_ptr + i * 4);
  int64_t timeout;
  return WaitForObjects(objects, count, wait_type == 0, TimeoutPtr(timeout_ptr, timeout), alertable != 0);
}

// ---- object manager ---------------------------------------------------------------------------

uint32_t NtClose(uint32_t handle) { return CloseHandle(handle) ? X_STATUS_SUCCESS : X_STATUS_INVALID_HANDLE; }

uint32_t NtDuplicateObject(uint32_t handle, uint32_t new_handle_ptr, uint32_t options) {
  uint32_t duplicate = DuplicateHandle(handle);
  if (!duplicate) return X_STATUS_INVALID_HANDLE;
  if (new_handle_ptr) Store<uint32_t>(new_handle_ptr, duplicate);
  if (options & 1) CloseHandle(handle);  // DUPLICATE_CLOSE_SOURCE
  return X_STATUS_SUCCESS;
}

uint32_t ObReferenceObjectByHandle(uint32_t handle, uint32_t object_type, uint32_t out_ptr) {
  (void)object_type;
  uint32_t body = ObjectFromHandle(handle);
  if (!body) return X_STATUS_INVALID_HANDLE;
  if (out_ptr) Store<uint32_t>(out_ptr, body);
  return X_STATUS_SUCCESS;
}

// ObCreateObject(object type, attributes, size, out object): an object of the title's own type (Forza
// Motorsport 4 makes some at start), after an X_OBJECT_HEADER {pointer count, handle count, type,
// flags}. The memory is the type's allocate routine's (X_OBJECT_TYPE: allocate, free, ...,
// pool tag at 0x18) when it has one, else the system heap's. A name isn't kept (the objects aren't
// looked up by name), and references aren't counted (ObDereferenceObject never frees).
uint32_t ObCreateObject(uint32_t object_type, uint32_t attributes, uint32_t size, uint32_t out_ptr) {
  (void)attributes;
  if (out_ptr) Store<uint32_t>(out_ptr, 0);
  const uint32_t total = size + 0x10;
  const uint32_t allocate = object_type ? Load<uint32_t>(object_type + 0x00) : 0;
  uint32_t memory = 0;
  if (allocate) {
    memory = uint32_t(CallGuest(allocate, {total, Load<uint32_t>(object_type + 0x18), 1}));
    if (memory) std::memset(Ptr(memory), 0, total);
  } else {
    memory = SystemAlloc(total);
  }
  if (!memory) return X_STATUS_NO_MEMORY;
  Store<int32_t>(memory + 0x0, 1);
  Store<int32_t>(memory + 0x4, 0);
  Store<uint32_t>(memory + 0x8, object_type);
  Store<uint16_t>(memory + 0xC, 1);  // OBJECT_HEADER_IS_TITLE_OBJECT
  if (out_ptr) Store<uint32_t>(out_ptr, memory + 0x10);
  return X_STATUS_SUCCESS;
}

void ObReferenceObject(uint32_t object) { (void)object; }
void ObDereferenceObject(uint32_t object) { (void)object; }

uint32_t ObLookupThreadByThreadId(uint32_t thread_id, uint32_t out_ptr) {
  auto thread = ThreadById(thread_id);
  if (!thread) return X_STATUS_INVALID_PARAMETER;
  if (out_ptr) Store<uint32_t>(out_ptr, thread->kthread);
  return X_STATUS_SUCCESS;
}

// A new handle to an object known by its body: a thread (its KTHREAD holds the thread id at 0x14C)
// or any dispatcher object.
uint32_t ObOpenObjectByPointer(uint32_t object, uint32_t handle_ptr) {
  if (!object) return X_STATUS_INVALID_PARAMETER;
  uint32_t handle = 0;
  if (auto thread = ThreadById(Load<uint32_t>(object + 0x14C)); thread && thread->kthread == object)
    handle = DuplicateHandle(thread->handle);
  else
    handle = CreateHandle(std::make_shared<DispatcherObject>(object));
  if (handle_ptr) Store<uint32_t>(handle_ptr, handle);
  return handle ? X_STATUS_SUCCESS : X_STATUS_INVALID_PARAMETER;
}
// Opens a named object (an event, semaphore...) created earlier with that name.
uint32_t ObOpenObjectByName(uint32_t object_attributes, uint32_t object_type, uint32_t unknown, uint32_t handle_ptr) {
  (void)object_type, (void)unknown;
  std::string name = ObjectName(object_attributes);
  auto object = name.empty() ? nullptr : LookupNamedObject(name);
  KLOG_I("ObOpenObjectByName(%s) -> %s", name.c_str(), object ? "found" : "not found");
  if (!object) return X_STATUS_OBJECT_NAME_NOT_FOUND;
  if (handle_ptr) Store<uint32_t>(handle_ptr, CreateHandle(object));
  return X_STATUS_SUCCESS;
}
uint32_t ObIsTitleObject(uint32_t object) {
  (void)object;
  return 1;
}

// ---- interlocked singly-linked lists (8-byte header: next, depth, sequence) --------------------

uint32_t HleInterlockedPopEntrySList(uint32_t head) {
  auto* word = reinterpret_cast<volatile LONG64*>(Ptr(head));
  for (;;) {
    LONG64 old = *word;
    uint64_t value = _byteswap_uint64(uint64_t(old));
    uint32_t next = uint32_t(value >> 32);
    if (!next) return 0;
    uint16_t depth = uint16_t(value >> 16), sequence = uint16_t(value);
    uint64_t updated = (uint64_t(Load<uint32_t>(next)) << 32) | (uint64_t(uint16_t(depth - 1)) << 16) |
                       uint16_t(sequence + 1);
    if (InterlockedCompareExchange64(word, LONG64(_byteswap_uint64(updated)), old) == old) return next;
  }
}

uint32_t HleInterlockedFlushSList(uint32_t head) {
  auto* word = reinterpret_cast<volatile LONG64*>(Ptr(head));
  for (;;) {
    LONG64 old = *word;
    uint64_t value = _byteswap_uint64(uint64_t(old));
    uint32_t first = uint32_t(value >> 32);
    uint64_t updated = uint16_t(value + 1);  // keep the sequence moving, depth 0, empty
    if (InterlockedCompareExchange64(word, LONG64(_byteswap_uint64(updated)), old) == old) return first;
  }
}

// ---- critical sections ------------------------------------------------------------------------
// X_RTL_CRITICAL_SECTION: header (0x10), lock_count (0x10, -1 = free), recursion (0x14), owner (0x18).

struct CsShard {
  std::mutex mutex;
  std::condition_variable cv;
};
CsShard g_cs_shards[64];
CsShard& ShardFor(uint32_t cs) { return g_cs_shards[(cs >> 4) & 63]; }

void RtlInitializeCriticalSection(uint32_t cs) {
  InitializeDispatcher(cs, kEventSynchronization, 0);
  Store<int32_t>(cs + 0x10, -1);
  Store<int32_t>(cs + 0x14, 0);
  Store<uint32_t>(cs + 0x18, 0);
}

uint32_t RtlInitializeCriticalSectionAndSpinCount(uint32_t cs, uint32_t spin_count) {
  RtlInitializeCriticalSection(cs);
  Store<uint8_t>(cs + 1, uint8_t((spin_count + 255) >> 8));
  return X_STATUS_SUCCESS;
}

bool TryAcquireCs(uint32_t cs, uint32_t kthread) {
  uint32_t owner = Load<uint32_t>(cs + 0x18);
  if (owner == 0) {
    Store<uint32_t>(cs + 0x18, kthread);
    Store<int32_t>(cs + 0x14, 1);
    Store<int32_t>(cs + 0x10, Load<int32_t>(cs + 0x10) + 1);
    return true;
  }
  if (owner == kthread) {
    Store<int32_t>(cs + 0x14, Load<int32_t>(cs + 0x14) + 1);
    Store<int32_t>(cs + 0x10, Load<int32_t>(cs + 0x10) + 1);
    return true;
  }
  return false;
}

void RtlEnterCriticalSection(uint32_t cs) {
  Thread* t = CurrentThread();
  uint32_t kthread = t ? t->kthread : 1;
  CsShard& shard = ShardFor(cs);
  std::unique_lock lock(shard.mutex);
  if (TryAcquireCs(cs, kthread)) return;
  Store<int32_t>(cs + 0x10, Load<int32_t>(cs + 0x10) + 1);  // count the waiter
  shard.cv.wait(lock, [&] { return Load<uint32_t>(cs + 0x18) == 0; });
  Store<int32_t>(cs + 0x10, Load<int32_t>(cs + 0x10) - 1);
  TryAcquireCs(cs, kthread);
}

uint32_t RtlTryEnterCriticalSection(uint32_t cs) {
  Thread* t = CurrentThread();
  CsShard& shard = ShardFor(cs);
  std::lock_guard lock(shard.mutex);
  return TryAcquireCs(cs, t ? t->kthread : 1) ? 1 : 0;
}

void RtlLeaveCriticalSection(uint32_t cs) {
  CsShard& shard = ShardFor(cs);
  std::lock_guard lock(shard.mutex);
  int32_t recursion = Load<int32_t>(cs + 0x14) - 1;
  Store<int32_t>(cs + 0x14, recursion);
  Store<int32_t>(cs + 0x10, Load<int32_t>(cs + 0x10) - 1);
  if (recursion <= 0) {
    Store<int32_t>(cs + 0x14, 0);
    Store<uint32_t>(cs + 0x18, 0);
    shard.cv.notify_all();
  }
}

// ---- time -------------------------------------------------------------------------------------

void KeQuerySystemTime(uint32_t time_ptr) {
  if (time_ptr) Store<uint64_t>(time_ptr, SystemTime());
}

void RtlTimeToTimeFields(uint32_t time_ptr, uint32_t fields_ptr) {
  uint64_t t = Load<uint64_t>(time_ptr);
  FILETIME ft{DWORD(t), DWORD(t >> 32)};
  SYSTEMTIME st;
  FileTimeToSystemTime(&ft, &st);
  Store<uint16_t>(fields_ptr + 0, st.wYear);
  Store<uint16_t>(fields_ptr + 2, st.wMonth);
  Store<uint16_t>(fields_ptr + 4, st.wDay);
  Store<uint16_t>(fields_ptr + 6, st.wHour);
  Store<uint16_t>(fields_ptr + 8, st.wMinute);
  Store<uint16_t>(fields_ptr + 10, st.wSecond);
  Store<uint16_t>(fields_ptr + 12, st.wMilliseconds);
  Store<uint16_t>(fields_ptr + 14, st.wDayOfWeek);
}

uint32_t RtlTimeFieldsToTime(uint32_t fields_ptr, uint32_t time_ptr) {
  SYSTEMTIME st{};
  st.wYear = Load<uint16_t>(fields_ptr + 0);
  st.wMonth = Load<uint16_t>(fields_ptr + 2);
  st.wDay = Load<uint16_t>(fields_ptr + 4);
  st.wHour = Load<uint16_t>(fields_ptr + 6);
  st.wMinute = Load<uint16_t>(fields_ptr + 8);
  st.wSecond = Load<uint16_t>(fields_ptr + 10);
  st.wMilliseconds = Load<uint16_t>(fields_ptr + 12);
  FILETIME ft;
  if (!SystemTimeToFileTime(&st, &ft)) return 0;
  Store<uint64_t>(time_ptr, (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
  return 1;
}

}  // namespace

namespace x360::kernel {

// KeTimeStampBundle: interrupt time and system time (100 ns), tick count (ms). The tick count
// advances every millisecond, as the console's clock interrupt does: Windows' own tick count moves in
// 15.6 ms steps, and Black Ops II's frame limiter (16 ms a frame, measured with GetTickCount) then
// waited for two steps every other frame (40 FPS).
void InitTime() {
  uint32_t bundle = KernelVariable("KeTimeStampBundle");
  if (!bundle) return;
  std::thread([bundle] {
    SetThreadDescription(GetCurrentThread(), L"KeTimeStampBundle");
    auto start = std::chrono::steady_clock::now();
    const uint64_t start_ticks = GetTickCount64();
    for (;;) {
      auto elapsed = std::chrono::steady_clock::now() - start;
      uint64_t interrupt = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() / 100);
      Store<uint64_t>(bundle + 0x00, interrupt);
      Store<uint64_t>(bundle + 0x08, SystemTime());
      Store<uint32_t>(bundle + 0x10, uint32_t(start_ticks + interrupt / 10000));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }).detach();
}

}  // namespace x360::kernel

X360_EXPORT(ExCreateThread, ExCreateThread)
X360_EXPORT(ExTerminateThread, ExTerminateThread)
X360_EXPORT(NtResumeThread, NtResumeThread)
X360_EXPORT(KeResumeThread, KeResumeThread)
X360_EXPORT(NtSuspendThread, NtSuspendThread)
X360_EXPORT(KeSetAffinityThread, KeSetAffinityThread)
X360_EXPORT(KeQueryBasePriorityThread, KeQueryBasePriorityThread)
X360_EXPORT(KeSetBasePriorityThread, KeSetBasePriorityThread)
X360_EXPORT(KeDelayExecutionThread, KeDelayExecutionThread)
X360_EXPORT(NtYieldExecution, NtYieldExecution)
X360_EXPORT(KeSetCurrentProcessType, KeSetCurrentProcessType)
X360_EXPORT(KeEnterCriticalRegion, KeEnterCriticalRegion)
X360_EXPORT(KeLeaveCriticalRegion, KeLeaveCriticalRegion)
X360_EXPORT(KeLockL2, KeLockL2)
X360_EXPORT(KeUnlockL2, KeUnlockL2)
X360_EXPORT(KiApcNormalRoutineNop, KiApcNormalRoutineNop)
X360_EXPORT(KeInitializeDpc, KeInitializeDpc)
X360_EXPORT(KeInsertQueueDpc, KeInsertQueueDpc)
X360_EXPORT(KeRaiseIrqlToDpcLevel, KeRaiseIrqlToDpcLevel)
X360_EXPORT(KfLowerIrql, KfLowerIrql)
X360_EXPORT(KeBugCheckEx, KeBugCheckEx)
X360_EXPORT(KeBugCheck, KeBugCheck)
X360_EXPORT(KeTlsAlloc, KeTlsAlloc)
X360_EXPORT(KeTlsFree, KeTlsFree)
X360_EXPORT(KeTlsGetValue, KeTlsGetValue)
X360_EXPORT(KeTlsSetValue, KeTlsSetValue)
X360_EXPORT(KfAcquireSpinLock, KfAcquireSpinLock)
X360_EXPORT(KfReleaseSpinLock, KfReleaseSpinLock)
X360_EXPORT(KeAcquireSpinLockAtRaisedIrql, KeAcquireSpinLockAtRaisedIrql)
X360_EXPORT(KeReleaseSpinLockFromRaisedIrql, KeReleaseSpinLockFromRaisedIrql)
X360_EXPORT(KeTryToAcquireSpinLockAtRaisedIrql, KeTryToAcquireSpinLockAtRaisedIrql)
X360_EXPORT(NtCreateEvent, NtCreateEvent)
X360_EXPORT(NtSetEvent, NtSetEvent)
X360_EXPORT(NtClearEvent, NtClearEvent)
X360_EXPORT(NtPulseEvent, NtPulseEvent)
X360_EXPORT(KeSetEvent, KeSetEvent)
X360_EXPORT(KeResetEvent, KeResetEvent)
X360_EXPORT(NtCreateSemaphore, NtCreateSemaphore)
X360_EXPORT(NtReleaseSemaphore, NtReleaseSemaphore)
X360_EXPORT(KeInitializeSemaphore, KeInitializeSemaphore)
X360_EXPORT(KeReleaseSemaphore, KeReleaseSemaphore)
X360_EXPORT(NtCreateMutant, NtCreateMutant)
X360_EXPORT(NtReleaseMutant, NtReleaseMutant)
X360_EXPORT(NtCreateTimer, NtCreateTimer)
X360_EXPORT(NtSetTimerEx, NtSetTimerEx)
X360_EXPORT(NtCancelTimer, NtCancelTimer)
X360_EXPORT(NtWaitForSingleObjectEx, NtWaitForSingleObjectEx)
X360_EXPORT(NtSignalAndWaitForSingleObjectEx, NtSignalAndWaitForSingleObjectEx)
X360_EXPORT(MmCreateKernelStack, MmCreateKernelStack)
X360_EXPORT(MmDeleteKernelStack, MmDeleteKernelStack)

// KeSetCurrentStackPointers(stack pointer, thread, stack allocation base, stack base (high), stack
// limit (low)): the thread's stack becomes the given one and r1 points into it.
PPC_FUNC(__imp__KeSetCurrentStackPointers) {
  (void)base;
  const uint32_t kthread = ctx.r4.u32;
  const uint32_t stack_base = ctx.r6.u32, stack_limit = ctx.r7.u32;
  if (kthread) {
    Store<uint32_t>(kthread + 0x5C, stack_base);
    Store<uint32_t>(kthread + 0x60, stack_limit);
  }
  if (Thread* t = CurrentThread(); t && t->kthread == kthread) {
    t->stack_base = stack_base;
    t->stack_limit = stack_limit;
    if (t->pcr) {
      Store<uint32_t>(t->pcr + 0x070, stack_base);
      Store<uint32_t>(t->pcr + 0x074, stack_limit);
    }
  }
  ctx.r1.u64 = ctx.r3.u32;
}
X360_EXPORT(NtWaitForMultipleObjectsEx, NtWaitForMultipleObjectsEx)
X360_EXPORT(KeWaitForSingleObject, KeWaitForSingleObject)
X360_EXPORT(KeWaitForMultipleObjects, KeWaitForMultipleObjects)
X360_EXPORT(NtClose, NtClose)
X360_EXPORT(NtDuplicateObject, NtDuplicateObject)
X360_EXPORT(ObReferenceObjectByHandle, ObReferenceObjectByHandle)
X360_EXPORT(ObCreateObject, ObCreateObject)
X360_EXPORT(ObReferenceObject, ObReferenceObject)
X360_EXPORT(ObDereferenceObject, ObDereferenceObject)
X360_EXPORT(ObLookupThreadByThreadId, ObLookupThreadByThreadId)
X360_EXPORT(ObOpenObjectByPointer, ObOpenObjectByPointer)
X360_EXPORT(ObOpenObjectByName, ObOpenObjectByName)
X360_EXPORT(ObIsTitleObject, ObIsTitleObject)
X360_EXPORT(InterlockedPopEntrySList, HleInterlockedPopEntrySList)
X360_EXPORT(InterlockedFlushSList, HleInterlockedFlushSList)
X360_EXPORT(RtlInitializeCriticalSection, RtlInitializeCriticalSection)
X360_EXPORT(RtlInitializeCriticalSectionAndSpinCount, RtlInitializeCriticalSectionAndSpinCount)
X360_EXPORT(RtlEnterCriticalSection, RtlEnterCriticalSection)
X360_EXPORT(RtlTryEnterCriticalSection, RtlTryEnterCriticalSection)
X360_EXPORT(RtlLeaveCriticalSection, RtlLeaveCriticalSection)
X360_EXPORT(KeQuerySystemTime, KeQuerySystemTime)
X360_EXPORT(RtlTimeToTimeFields, RtlTimeToTimeFields)
X360_EXPORT(RtlTimeFieldsToTime, RtlTimeFieldsToTime)
