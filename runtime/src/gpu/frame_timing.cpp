#include "frame_timing.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::gpu::timing {

namespace {

constexpr size_t kRingSize = 1 << 16;

// Seqlock-style slot: `seq` is the event's index + 1 once the slot is complete, 0 while written.
struct Slot {
  std::atomic<uint64_t> seq{0};
  std::atomic<int64_t> time{0};
  std::atomic<int32_t> payload{0};
  std::atomic<uint8_t> event{0};
};

Slot g_ring[kRingSize];
std::atomic<uint64_t> g_next{0};

int64_t Now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

const int64_t g_frequency = [] {
  LARGE_INTEGER f;
  QueryPerformanceFrequency(&f);
  return f.QuadPart;
}();
const int64_t g_start = Now();

struct Entry {
  uint64_t index;
  int64_t time;
  int32_t payload;
  Event event;
};

// The complete events with index >= `from`, oldest first.
std::vector<Entry> Snapshot(uint64_t from = 0) {
  uint64_t end = g_next.load(std::memory_order_acquire);
  uint64_t begin = std::max(from, end > kRingSize ? end - kRingSize : 0);
  std::vector<Entry> out;
  out.reserve(size_t(end - begin));
  for (uint64_t i = begin; i < end; ++i) {
    Slot& slot = g_ring[i & (kRingSize - 1)];
    if (slot.seq.load(std::memory_order_acquire) != i + 1) continue;
    Entry r{i, slot.time.load(std::memory_order_relaxed), slot.payload.load(std::memory_order_relaxed),
              Event(slot.event.load(std::memory_order_relaxed))};
    std::atomic_thread_fence(std::memory_order_acquire);
    if (slot.seq.load(std::memory_order_relaxed) != i + 1) continue;
    out.push_back(r);
  }
  return out;
}

double Ms(int64_t ticks) { return double(ticks) * 1000.0 / double(g_frequency); }

bool IsPresent(Event e) { return e == Event::kPresentGpuThread || e == Event::kPresentUiThread; }

double Percentile(std::vector<double>& values, double p) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  return values[std::min(values.size() - 1, size_t(p * double(values.size() - 1) + 0.5))];
}

}  // namespace

void Record(Event event, int32_t payload) {
  uint64_t index = g_next.fetch_add(1, std::memory_order_relaxed);
  Slot& slot = g_ring[index & (kRingSize - 1)];
  slot.seq.store(0, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  slot.time.store(Now(), std::memory_order_relaxed);
  slot.payload.store(payload, std::memory_order_relaxed);
  slot.event.store(uint8_t(event), std::memory_order_relaxed);
  slot.seq.store(index + 1, std::memory_order_release);
}

Summary Summarize(double seconds) {
  Summary s;
  s.seconds = seconds;
  int64_t since = Now() - int64_t(seconds * double(g_frequency));
  std::vector<double> swaps, presents, late;
  int64_t last_swap = 0, last_present = 0;
  for (const Entry& r : Snapshot()) {
    if (r.time < since) continue;
    switch (r.event) {
      case Event::kVblank:
        ++s.vblanks;
        late.push_back(double(r.payload));
        break;
      case Event::kVdSwap:
        ++s.vdswaps;
        if (last_swap) swaps.push_back(Ms(r.time - last_swap));
        last_swap = r.time;
        break;
      case Event::kGpuSwap: ++s.gpu_swaps; break;
      case Event::kPresentGpuThread:
      case Event::kPresentUiThread:
        ++(r.event == Event::kPresentGpuThread ? s.presents_gpu_thread : s.presents_ui_thread);
        if (!r.payload) {
          ++s.ui_repaints;
          break;
        }
        if (last_present) presents.push_back(Ms(r.time - last_present));
        last_present = r.time;
        break;
      default: break;
    }
  }
  if (!swaps.empty()) {
    double sum = 0, squares = 0;
    for (double v : swaps) sum += v, squares += v * v;
    s.swap_mean_ms = sum / double(swaps.size());
    s.swap_stddev_ms = std::sqrt(std::max(0.0, squares / double(swaps.size()) - s.swap_mean_ms * s.swap_mean_ms));
    s.swap_p50_ms = Percentile(swaps, 0.5);
    s.swap_p99_ms = Percentile(swaps, 0.99);
    s.swap_max_ms = swaps.back();
  }
  if (!presents.empty()) {
    s.present_p99_ms = Percentile(presents, 0.99);
    s.present_max_ms = presents.back();
  }
  if (!late.empty()) {
    s.vblank_late_p99_us = Percentile(late, 0.99);
    s.vblank_late_max_us = late.back();
  }
  return s;
}

size_t RecentIntervals(Event event, float* out, size_t max) {
  // Walks back from the newest event (cheap enough to call every UI frame).
  std::vector<int64_t> times;
  uint64_t end = g_next.load(std::memory_order_acquire);
  uint64_t begin = end > kRingSize ? end - kRingSize : 0;
  for (uint64_t i = end; i-- > begin && times.size() <= max && end - i < 8192;) {
    Slot& slot = g_ring[i & (kRingSize - 1)];
    if (slot.seq.load(std::memory_order_acquire) != i + 1) continue;
    Event e = Event(slot.event.load(std::memory_order_relaxed));
    bool match = IsPresent(event) ? IsPresent(e) && slot.payload.load(std::memory_order_relaxed) : e == event;
    if (match) times.push_back(slot.time.load(std::memory_order_relaxed));
  }
  size_t count = times.size() > 1 ? times.size() - 1 : 0;
  for (size_t i = 0; i < count; ++i) out[count - 1 - i] = float(Ms(times[i] - times[i + 1]));
  return count;
}

void AppendCsv(const char* path) {
  static uint64_t next = 0;
  static bool first = true;
  std::vector<Entry> records = Snapshot(next);
  if (records.empty() && !first) return;
  FILE* f = std::fopen(path, first ? "w" : "a");
  if (!f) return;
  static const char* const kNames[] = {"vblank", "vdswap", "gpuswap", "present_gpu", "present_ui"};
  if (first) std::fputs("time_ms,event,payload\n", f);
  first = false;
  for (const Entry& r : records)
    std::fprintf(f, "%.3f,%s,%d\n", Ms(r.time - g_start), kNames[size_t(r.event)], r.payload);
  std::fclose(f);
  if (!records.empty()) next = records.back().index + 1;
}

}  // namespace x360::gpu::timing
