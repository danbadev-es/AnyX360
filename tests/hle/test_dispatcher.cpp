// Dispatcher semantics: which waiters a signal wakes, wait-all/wait-any, mutant ownership,
// timeouts, and a ping-pong stress test. Waiters are host threads with dispatcher objects in a real
// guest address space.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "x360/hle.h"
#include "x360/kernel.h"
#include "x360/memory.h"

PPCFuncMapping PPCFuncMappings[] = {{0, nullptr}};

using namespace x360::kernel;
using namespace std::chrono_literals;

static int failures = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    if (cond) {                                                     \
      std::printf("PASS %s\n", #cond);                              \
    } else {                                                        \
      std::printf("FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++failures;                                                   \
    }                                                               \
  } while (0)

namespace {

constexpr uint32_t kObjects = 0x40000000;
uint32_t g_next_object = kObjects;

uint32_t NewObject(uint8_t type, int32_t state, int32_t limit = 0) {
  uint32_t body = g_next_object;
  g_next_object += 0x40;
  InitializeDispatcher(body, type, state);
  if (type == kSemaphore) Store<int32_t>(body + 0x10, limit);
  if (type == kMutant) Store<uint32_t>(body + 0x18, 0);
  return body;
}

int32_t State(uint32_t object) { return Ptr<DispatchHeader>(object)->signal_state.get(); }

// Threads that wait once on `objects` and count how many have returned.
struct Waiters {
  std::atomic<int> done{0};
  std::vector<std::thread> threads;
  std::vector<uint32_t> statuses;
  Waiters(int n, std::vector<uint32_t> objects, bool wait_all = false) : statuses(n, 0xFFFFFFFF) {
    for (int i = 0; i < n; ++i)
      threads.emplace_back([this, i, objects, wait_all] {
        statuses[i] = WaitForObjects(objects.data(), uint32_t(objects.size()), wait_all, nullptr, false);
        ++done;
      });
    std::this_thread::sleep_for(100ms);  // all of them waiting
  }
  ~Waiters() {
    for (auto& t : threads) t.join();
  }
};

int Settle(const std::atomic<int>& counter) {
  std::this_thread::sleep_for(100ms);
  return counter.load();
}

}  // namespace

int main() {
  x360::GuestMemory mem;
  CHECK(mem.valid());
  CHECK(mem.commit(kObjects, 0x10000));
  x360::hle::set_guest_base(mem.base());
  g_base = mem.base();

  {  // A synchronization event wakes exactly one waiter per signal.
    uint32_t e = NewObject(kEventSynchronization, 0);
    Waiters w(3, {e});
    CHECK(w.done == 0);
    SetEvent(e);
    CHECK(Settle(w.done) == 1);
    CHECK(State(e) == 0);
    SetEvent(e);
    CHECK(Settle(w.done) == 2);
    SetEvent(e);
    CHECK(Settle(w.done) == 3);
  }
  {  // A notification event wakes all and stays signaled.
    uint32_t e = NewObject(kEventNotification, 0);
    Waiters w(4, {e});
    SetEvent(e);
    CHECK(Settle(w.done) == 4);
    CHECK(State(e) == 1);
  }
  {  // PulseEvent wakes the current waiters and leaves the event reset; with none, just resets.
    uint32_t e = NewObject(kEventNotification, 0);
    Waiters w(3, {e});
    PulseEvent(e);
    CHECK(Settle(w.done) == 3);
    CHECK(State(e) == 0);
    uint32_t s = NewObject(kEventSynchronization, 0);
    PulseEvent(s);
    CHECK(State(s) == 0);
    Waiters one(2, {s});
    PulseEvent(s);
    CHECK(Settle(one.done) == 1);
    CHECK(State(s) == 0);
    SetEvent(s);
  }
  {  // A semaphore released by 3 wakes 3 of 5 waiters.
    uint32_t sem = NewObject(kSemaphore, 0, 10);
    Waiters w(5, {sem});
    int32_t previous = -1;
    CHECK(ReleaseSemaphore(sem, 3, &previous) && previous == 0);
    CHECK(Settle(w.done) == 3);
    CHECK(State(sem) == 0);
    CHECK(!ReleaseSemaphore(sem, 11, nullptr));  // over the limit
    CHECK(ReleaseSemaphore(sem, 2, nullptr));
    CHECK(Settle(w.done) == 5);
  }
  {  // Mutant: recursion by the owner, another thread gets it after the last release.
    uint32_t m = NewObject(kMutant, 1);
    CHECK(WaitForObject(m, nullptr, false) == X_STATUS_WAIT_0);
    CHECK(WaitForObject(m, nullptr, false) == X_STATUS_WAIT_0);  // recursive
    CHECK(State(m) == -1);
    Waiters w(1, {m});
    CHECK(ReleaseMutant(m, false, nullptr));
    CHECK(Settle(w.done) == 0);  // still owned once
    CHECK(ReleaseMutant(m, false, nullptr));
    CHECK(Settle(w.done) == 1);
    CHECK(!ReleaseMutant(m, false, nullptr));  // owned by the other thread now
  }
  {  // Wait-all consumes nothing until every object is signaled.
    uint32_t a = NewObject(kEventSynchronization, 1), b = NewObject(kEventSynchronization, 0);
    uint32_t both[] = {a, b};
    int64_t timeout = -200 * 10000;  // 200 ms
    CHECK(WaitForObjects(both, 2, true, &timeout, false) == X_STATUS_TIMEOUT);
    CHECK(State(a) == 1);
    Waiters w(1, {a, b}, true);
    CHECK(w.done == 0);
    SetEvent(b);
    CHECK(Settle(w.done) == 1);
    CHECK(w.statuses[0] == X_STATUS_WAIT_0);
    CHECK(State(a) == 0 && State(b) == 0);
  }
  {  // Wait-any returns the index of the signaled object; same object twice is fine.
    uint32_t a = NewObject(kEventSynchronization, 0), b = NewObject(kEventSynchronization, 1);
    uint32_t objects[] = {a, b};
    CHECK(WaitForObjects(objects, 2, false, nullptr, false) == X_STATUS_WAIT_0 + 1);
    Waiters w(2, {a, a, b});
    SetEvent(a);
    CHECK(Settle(w.done) == 1);
    SetEvent(b);
    CHECK(Settle(w.done) == 2);
    CHECK((w.statuses[0] == X_STATUS_WAIT_0 && w.statuses[1] == X_STATUS_WAIT_0 + 2) ||
          (w.statuses[1] == X_STATUS_WAIT_0 && w.statuses[0] == X_STATUS_WAIT_0 + 2));
  }
  {  // Timeouts: zero, relative, and a signal after a timed-out wait doesn't leak into the next.
    uint32_t e = NewObject(kEventSynchronization, 0);
    int64_t zero = 0;
    CHECK(WaitForObject(e, &zero, false) == X_STATUS_TIMEOUT);
    int64_t timeout = -50 * 10000;
    auto start = std::chrono::steady_clock::now();
    CHECK(WaitForObject(e, &timeout, false) == X_STATUS_TIMEOUT);
    auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed >= 49ms && elapsed < 150ms);
    SetEvent(e);
    CHECK(WaitForObject(e, &zero, false) == X_STATUS_WAIT_0);
    CHECK(WaitForObject(e, &timeout, false) == X_STATUS_TIMEOUT);
  }
  {  // Ping-pong: 16 pairs of threads hand control back and forth 2000 times.
    constexpr int kPairs = 16, kRounds = 2000;
    std::vector<uint32_t> ping(kPairs), pong(kPairs);
    for (int i = 0; i < kPairs; ++i) ping[i] = NewObject(kEventSynchronization, 0), pong[i] = NewObject(kEventSynchronization, 0);
    std::atomic<int> finished{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < kPairs; ++i) {
      threads.emplace_back([&, i] {
        for (int r = 0; r < kRounds; ++r) {
          SetEvent(ping[i]);
          WaitForObject(pong[i], nullptr, false);
        }
        ++finished;
      });
      threads.emplace_back([&, i] {
        for (int r = 0; r < kRounds; ++r) {
          WaitForObject(ping[i], nullptr, false);
          SetEvent(pong[i]);
        }
        ++finished;
      });
    }
    auto deadline = std::chrono::steady_clock::now() + 20s;
    while (finished < 2 * kPairs && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(10ms);
    CHECK(finished == 2 * kPairs);
    if (finished == 2 * kPairs)
      for (auto& t : threads) t.join();
    else
      for (auto& t : threads) t.detach();
  }

  uint64_t wakes, missed;
  DispatcherCounters(wakes, missed);
  std::printf("%llu wakes, %llu missed\n", (unsigned long long)wakes, (unsigned long long)missed);
  CHECK(missed == 0);
  std::printf("\n%s\n", failures ? "FAILED" : "OK");
  std::fflush(stdout);
  std::_Exit(failures ? 1 : 0);
}
