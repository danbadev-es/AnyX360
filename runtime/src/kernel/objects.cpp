// Handles, named objects, dispatcher objects and waits.
//
// Waitable objects keep their state in the guest dispatcher header (as the console does), so the
// same code serves Ke* functions that receive guest pointers and Nt* functions that receive
// handles. One global lock serializes all dispatcher state.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

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
//
// Each wait links one wait block per object into that object's FIFO list (as the NT kernel does)
// and sleeps on its thread's event. Signalling an object walks its list and satisfies, in order,
// the waits that can complete (acquiring for them), and wakes only those threads: an auto-reset
// event or a semaphore unit goes to the longest waiter, and a wait-all never takes part of its set.

namespace {
std::mutex g_dispatch_mutex;

constexpr uint32_t kPending = 0xFFFFFFFF;
constexpr uint32_t kMaxWaitObjects = 64;

struct Waiter;
struct WaitBlock {
  Waiter* waiter;
  uint32_t object;
  WaitBlock* prev;
  WaitBlock* next;
};
struct Waiter {
  const uint32_t* objects;
  uint32_t count;
  bool wait_all;
  uint32_t kthread;
  std::atomic<uint32_t> status{kPending};
  HANDLE wake;
  WaitBlock blocks[kMaxWaitObjects];
};
struct WaitList {
  WaitBlock* head = nullptr;
  WaitBlock* tail = nullptr;
};
std::unordered_map<uint32_t, WaitList> g_waits;  // object -> waits, oldest first
std::atomic<uint64_t> g_wakes{0}, g_missed_wakes{0};

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

// Completes the wait if it can (acquiring its objects) and sets `status`. Lock held.
bool TrySatisfy(const uint32_t* objects, uint32_t count, bool wait_all, uint32_t kthread, uint32_t& status) {
  if (wait_all) {
    for (uint32_t i = 0; i < count; ++i)
      if (!IsSignaled(objects[i], kthread)) return false;
    for (uint32_t i = 0; i < count; ++i) Acquire(objects[i], kthread);
    status = X_STATUS_WAIT_0;
    return true;
  }
  for (uint32_t i = 0; i < count; ++i) {
    if (!IsSignaled(objects[i], kthread)) continue;
    Acquire(objects[i], kthread);
    status = X_STATUS_WAIT_0 + i;
    return true;
  }
  return false;
}

void Link(Waiter& w) {
  for (uint32_t i = 0; i < w.count; ++i) {
    WaitList& list = g_waits[w.objects[i]];
    WaitBlock& b = w.blocks[i];
    b = {&w, w.objects[i], list.tail, nullptr};
    (list.tail ? list.tail->next : list.head) = &b;
    list.tail = &b;
  }
}

void Unlink(Waiter& w) {
  for (uint32_t i = 0; i < w.count; ++i) {
    WaitBlock& b = w.blocks[i];
    auto it = g_waits.find(b.object);
    if (it == g_waits.end()) continue;
    WaitList& list = it->second;
    (b.prev ? b.prev->next : list.head) = b.next;
    (b.next ? b.next->prev : list.tail) = b.prev;
    if (!list.head) g_waits.erase(it);
  }
}

// After `object` became signaled: satisfies the waits that can now complete, oldest first, and
// collects their threads' events (set them after unlocking). Lock held.
void WakeWaiters(uint32_t object, std::vector<HANDLE>& wake) {
  auto it = g_waits.find(object);
  if (it == g_waits.end()) return;
  bool mutant = Header(object)->type == kMutant;
  for (WaitBlock* b = it->second.head; b;) {
    Waiter* w = b->waiter;
    WaitBlock* next = b->next;
    uint32_t status;
    if (TrySatisfy(w->objects, w->count, w->wait_all, w->kthread, status)) {
      Unlink(*w);  // may erase this object's list; `next` stays valid
      // The same object can appear twice in one wait: skip the waiter's other (unlinked) blocks.
      while (next && next->waiter == w) next = next->next;
      wake.push_back(w->wake);
      ++g_wakes;
      // Last access to the waiter: it may return (and free it) as soon as it sees the status.
      w->status.store(status, std::memory_order_release);
    }
    // Nothing left for the others (a mutant may still be re-acquired by its owner).
    if (!mutant && Header(object)->signal_state.get() <= 0) break;
    b = next;
  }
}

void SetEvents(const std::vector<HANDLE>& wake) {
  for (HANDLE h : wake) ::SetEvent(h);
}

// The calling thread's wait event (auto-reset).
HANDLE ThreadWakeEvent() {
  struct Event {
    HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Event() { ::CloseHandle(handle); }
  };
  thread_local Event event;
  return event.handle;
}


// Infinite waits also wake up this often and re-check their objects: a signal path that doesn't
// wake waiters (a bug) costs a delay and a log line instead of a hang. ANYX360_DISPATCH_SAFETY_MS
// (0 disables).
DWORD SafetyNetMs() {
  static const DWORD ms = [] {
    char buf[16] = {};
    return GetEnvironmentVariableA("ANYX360_DISPATCH_SAFETY_MS", buf, sizeof(buf)) ? DWORD(std::atoi(buf)) : 250;
  }();
  return ms ? ms : INFINITE;
}
}  // namespace

std::mutex& DispatcherLock() { return g_dispatch_mutex; }

uint32_t CurrentKthread() {
  if (Thread* t = CurrentThread()) return t->kthread;
  static std::atomic<uint32_t> next{0xFFFF0000};
  thread_local uint32_t id = next++;
  return id;
}

void DispatcherCounters(uint64_t& wakes, uint64_t& missed) {
  wakes = g_wakes.load();
  missed = g_missed_wakes.load();
}

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
  if (!count || count > kMaxWaitObjects) return X_STATUS_INVALID_PARAMETER;
  // An alertable wait runs the thread's queued user APCs instead of waiting, even if the objects
  // are signalled, as on NT.
  if (alertable && DeliverUserApcs()) return X_STATUS_USER_APC;
  using clock = std::chrono::steady_clock;
  clock::time_point deadline;
  bool finite = TimeoutToDeadline(timeout, &deadline);

  Waiter w;
  w.objects = objects;
  w.count = count;
  w.wait_all = wait_all;
  w.kthread = CurrentKthread();
  {
    std::lock_guard lock(g_dispatch_mutex);
    uint32_t status;
    if (TrySatisfy(objects, count, wait_all, w.kthread, status)) return status;
    if (finite && clock::now() >= deadline) return X_STATUS_TIMEOUT;
    w.wake = ThreadWakeEvent();
    Link(w);
  }
  for (;;) {
    DWORD ms = SafetyNetMs();
    if (finite) {
      auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - clock::now()).count();
      ms = std::min<DWORD>(ms, DWORD(std::max<int64_t>(left, 0)));
    }
    WaitForSingleObject(w.wake, ms);
    uint32_t status = w.status.load(std::memory_order_acquire);
    if (status != kPending) return status;
    // Timed out, a safety-net tick, or a stale wake from an earlier wait of this thread.
    std::lock_guard lock(g_dispatch_mutex);
    status = w.status.load(std::memory_order_acquire);
    if (status != kPending) return status;  // satisfied meanwhile (already unlinked)
    if (TrySatisfy(objects, count, wait_all, w.kthread, status)) {
      Unlink(w);
      ++g_missed_wakes;
      KLOG_W("dispatcher: missed wakeup on %08X", objects[0]);
      return status;
    }
    if (finite && clock::now() >= deadline) {
      Unlink(w);
      return X_STATUS_TIMEOUT;
    }
  }
}

uint32_t WaitForObject(uint32_t object, const int64_t* timeout, bool alertable) {
  return WaitForObjects(&object, 1, false, timeout, alertable);
}

void SignalDispatcherObject(uint32_t object) {
  std::vector<HANDLE> wake;
  {
    std::lock_guard lock(g_dispatch_mutex);
    Header(object)->signal_state = 1;
    WakeWaiters(object, wake);
  }
  SetEvents(wake);
}

int32_t SetEvent(uint32_t event) {
  std::vector<HANDLE> wake;
  int32_t previous;
  {
    std::lock_guard lock(g_dispatch_mutex);
    DispatchHeader* h = Header(event);
    previous = h->signal_state.get();
    h->signal_state = 1;
    WakeWaiters(event, wake);
  }
  SetEvents(wake);
  return previous;
}

int32_t ResetEvent(uint32_t event) {
  std::lock_guard lock(g_dispatch_mutex);
  DispatchHeader* h = Header(event);
  int32_t previous = h->signal_state.get();
  h->signal_state = 0;
  return previous;
}

// Satisfies the current waiters (all of them for a notification event, one for a synchronization
// event) and leaves the event reset.
int32_t PulseEvent(uint32_t event) {
  std::vector<HANDLE> wake;
  int32_t previous;
  {
    std::lock_guard lock(g_dispatch_mutex);
    DispatchHeader* h = Header(event);
    previous = h->signal_state.get();
    h->signal_state = 1;
    WakeWaiters(event, wake);
    h->signal_state = 0;
  }
  SetEvents(wake);
  return previous;
}

bool ReleaseSemaphore(uint32_t semaphore, int32_t count, int32_t* previous) {
  std::vector<HANDLE> wake;
  {
    std::lock_guard lock(g_dispatch_mutex);
    DispatchHeader* h = Header(semaphore);
    int32_t limit = Load<int32_t>(semaphore + 0x10);
    int32_t current = h->signal_state.get();
    if (previous) *previous = current;
    if (count <= 0 || current + count > limit) return false;
    h->signal_state = current + count;
    WakeWaiters(semaphore, wake);
  }
  SetEvents(wake);
  return true;
}

bool ReleaseMutant(uint32_t mutant, bool abandon, int32_t* previous) {
  std::vector<HANDLE> wake;
  {
    std::lock_guard lock(g_dispatch_mutex);
    DispatchHeader* h = Header(mutant);
    int32_t current = h->signal_state.get();
    if (previous) *previous = current;
    if (!abandon && MutantOwner(mutant) != CurrentKthread()) return false;
    current = abandon ? 1 : current + 1;
    h->signal_state = current;
    if (current > 0) {
      Store<uint32_t>(mutant + 0x18, 0);
      WakeWaiters(mutant, wake);
    }
  }
  SetEvents(wake);
  return true;
}

}  // namespace x360::kernel
