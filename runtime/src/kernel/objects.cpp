// Handles, named objects, dispatcher objects and waits.
//
// Waitable objects keep their state in the guest dispatcher header (as the console does), so the
// same code serves Ke* functions that receive guest pointers and Nt* functions that receive
// handles. One global lock serializes all dispatcher state; any change wakes every waiter.
#include <chrono>
#include <cstdio>
#include <unordered_map>

#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::kernel {

// ---- Logging ----------------------------------------------------------------------------------

namespace {
std::mutex g_log_mutex;
}

bool TraceEnabled() {
  static const bool enabled = [] {
    char buf[8] = {};
    return GetEnvironmentVariableA("ANYX360_TRACE", buf, sizeof(buf)) > 0 && buf[0] == '1';
  }();
  return enabled;
}

void Log(LogLevel level, const char* fmt, ...) {
  static const char* kPrefix[] = {"T", "I", "W", "E"};
  char message[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);
  Thread* t = CurrentThread();
  std::lock_guard lock(g_log_mutex);
  std::fprintf(stderr, "[%s][%04X] %s\n", kPrefix[int(level)], t ? t->thread_id : 0, message);
  std::fflush(stderr);
}

[[noreturn]] void Fatal(const char* fmt, ...) {
  char message[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);
  KLOG_E("FATAL: %s", message);
  std::fflush(stderr);
  std::_Exit(3);
}

// ---- Handles ----------------------------------------------------------------------------------

namespace {
std::mutex g_handle_mutex;
std::unordered_map<uint32_t, std::shared_ptr<Object>> g_handles;
std::unordered_map<std::string, std::weak_ptr<Object>> g_named;
uint32_t g_next_handle = 0xF8000004;
}  // namespace

uint32_t CreateHandle(std::shared_ptr<Object> object) {
  std::lock_guard lock(g_handle_mutex);
  uint32_t handle = g_next_handle;
  g_next_handle += 4;
  g_handles[handle] = std::move(object);
  return handle;
}

std::shared_ptr<Object> LookupHandle(uint32_t handle) {
  if (handle == kCurrentThreadHandle) return CurrentThreadShared();
  std::lock_guard lock(g_handle_mutex);
  auto it = g_handles.find(handle);
  return it == g_handles.end() ? nullptr : it->second;
}

bool CloseHandle(uint32_t handle) {
  std::lock_guard lock(g_handle_mutex);
  return g_handles.erase(handle) != 0;
}

uint32_t DuplicateHandle(uint32_t handle) {
  auto object = LookupHandle(handle);
  return object ? CreateHandle(object) : 0;
}

std::shared_ptr<Object> LookupNamedObject(const std::string& name) {
  std::lock_guard lock(g_handle_mutex);
  auto it = g_named.find(name);
  return it == g_named.end() ? nullptr : it->second.lock();
}

void RegisterNamedObject(const std::string& name, std::shared_ptr<Object> object) {
  std::lock_guard lock(g_handle_mutex);
  g_named[name] = object;
}

uint32_t ObjectFromHandle(uint32_t handle) {
  auto object = LookupHandle(handle);
  return object ? object->guest : 0;
}

// ---- Dispatcher -------------------------------------------------------------------------------

namespace {
std::mutex g_dispatch_mutex;
std::condition_variable g_dispatch_cv;

DispatchHeader* Header(uint32_t object) { return Ptr<DispatchHeader>(object); }

// Mutant layout: header (0x10), list entry (0x10), owner (0x18), abandoned (0x1C).
uint32_t MutantOwner(uint32_t mutant) { return Load<uint32_t>(mutant + 0x18); }

bool IsSignaled(uint32_t object, uint32_t kthread) {
  DispatchHeader* h = Header(object);
  switch (h->type) {
    case kMutant:
      return h->signal_state.get() > 0 || MutantOwner(object) == kthread;
    case kSemaphore:
      return h->signal_state.get() > 0;
    default:
      return h->signal_state.get() != 0;
  }
}

void Acquire(uint32_t object, uint32_t kthread) {
  DispatchHeader* h = Header(object);
  switch (h->type) {
    case kEventSynchronization:
    case kTimerSynchronization:
      h->signal_state = 0;
      break;
    case kSemaphore:
      h->signal_state = h->signal_state.get() - 1;
      break;
    case kMutant:
      h->signal_state = h->signal_state.get() - 1;
      Store<uint32_t>(object + 0x18, kthread);
      break;
    default:
      break;  // notification objects, threads, processes stay signaled
  }
}
}  // namespace

std::mutex& DispatcherLock() { return g_dispatch_mutex; }
void NotifyDispatcherChange() { g_dispatch_cv.notify_all(); }

void InitializeDispatcher(uint32_t body, uint8_t type, int32_t signal_state) {
  DispatchHeader* h = Header(body);
  h->type = type;
  h->absolute = 0;
  h->size = 0;
  h->inserted = 0;
  h->signal_state = signal_state;
  // Empty wait list points to itself.
  h->wait_list_flink = body + 8;
  h->wait_list_blink = body + 8;
}

uint64_t SystemTime() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  return (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

bool TimeoutToDeadline(const int64_t* timeout, std::chrono::steady_clock::time_point* deadline) {
  if (!timeout) return false;
  int64_t value = *timeout;
  int64_t relative_100ns = value < 0 ? -value : int64_t(value - SystemTime());
  if (relative_100ns < 0) relative_100ns = 0;
  *deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(relative_100ns / 10);
  return true;
}

uint32_t WaitForObjects(const uint32_t* objects, uint32_t count, bool wait_all, const int64_t* timeout,
                        bool alertable) {
  (void)alertable;  // APCs are not delivered yet
  Thread* thread = CurrentThread();
  uint32_t kthread = thread ? thread->kthread : 0;

  std::chrono::steady_clock::time_point deadline;
  bool finite = TimeoutToDeadline(timeout, &deadline);

  // Returns true and sets `status` when the wait is satisfied (acquiring the objects).
  auto try_satisfy = [&](uint32_t& status) {
    if (wait_all) {
      for (uint32_t i = 0; i < count; ++i) {
        if (!IsSignaled(objects[i], kthread)) return false;
      }
      for (uint32_t i = 0; i < count; ++i) Acquire(objects[i], kthread);
      status = X_STATUS_WAIT_0;
    } else {
      uint32_t i = 0;
      while (i < count && !IsSignaled(objects[i], kthread)) ++i;
      if (i == count) return false;
      Acquire(objects[i], kthread);
      status = X_STATUS_WAIT_0 + i;
    }
    NotifyDispatcherChange();
    return true;
  };

  std::unique_lock lock(g_dispatch_mutex);
  uint32_t status = X_STATUS_TIMEOUT;
  for (;;) {
    if (try_satisfy(status)) return status;
    if (!finite) {
      g_dispatch_cv.wait(lock);
    } else if (g_dispatch_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
      return try_satisfy(status) ? status : X_STATUS_TIMEOUT;
    }
  }
}

uint32_t WaitForObject(uint32_t object, const int64_t* timeout, bool alertable) {
  return WaitForObjects(&object, 1, false, timeout, alertable);
}

int32_t SetEvent(uint32_t event) {
  std::lock_guard lock(g_dispatch_mutex);
  DispatchHeader* h = Header(event);
  int32_t previous = h->signal_state.get();
  h->signal_state = 1;
  NotifyDispatcherChange();
  return previous;
}

int32_t ResetEvent(uint32_t event) {
  std::lock_guard lock(g_dispatch_mutex);
  DispatchHeader* h = Header(event);
  int32_t previous = h->signal_state.get();
  h->signal_state = 0;
  return previous;
}

int32_t PulseEvent(uint32_t event) {
  // Wake current waiters, then reset. With a single lock and broadcast wakeups the best
  // approximation is to signal, let waiters run, and reset afterwards.
  int32_t previous = SetEvent(event);
  std::this_thread::yield();
  ResetEvent(event);
  return previous;
}

bool ReleaseSemaphore(uint32_t semaphore, int32_t count, int32_t* previous) {
  std::lock_guard lock(g_dispatch_mutex);
  DispatchHeader* h = Header(semaphore);
  int32_t limit = Load<int32_t>(semaphore + 0x10);
  int32_t current = h->signal_state.get();
  if (previous) *previous = current;
  if (count <= 0 || current + count > limit) return false;
  h->signal_state = current + count;
  NotifyDispatcherChange();
  return true;
}

bool ReleaseMutant(uint32_t mutant, bool abandon, int32_t* previous) {
  std::lock_guard lock(g_dispatch_mutex);
  DispatchHeader* h = Header(mutant);
  Thread* thread = CurrentThread();
  int32_t current = h->signal_state.get();
  if (previous) *previous = current;
  if (!abandon && MutantOwner(mutant) != (thread ? thread->kthread : 0)) return false;
  current = abandon ? 1 : current + 1;
  h->signal_state = current;
  if (current > 0) Store<uint32_t>(mutant + 0x18, 0);
  NotifyDispatcherChange();
  return true;
}

}  // namespace x360::kernel
