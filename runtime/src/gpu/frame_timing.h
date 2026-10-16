// Frame timing: a lock-free ring of timestamped events (vblanks, guest swaps, GPU swaps, presents)
// for the performance overlay, the periodic GPU log line and ANYX360_FRAMETIMES CSV dumps.
#pragma once

#include <cstddef>
#include <cstdint>

namespace x360::gpu::timing {

enum class Event : uint8_t {
  kVblank,          // payload: microseconds late against the schedule
  kVdSwap,          // the title submitted a frame
  kGpuSwap,         // the command processor executed the frame's swap
  kPresentGpuThread,  // payload: 1 if a new guest frame was presented, 0 if a repaint
  kPresentUiThread,   // same payload
  kCount,
};

// Any thread, wait-free.
void Record(Event event, int32_t payload = 0);

// Statistics over the last `seconds`.
struct Summary {
  double seconds = 0;
  uint32_t vblanks = 0, vdswaps = 0, gpu_swaps = 0;
  uint32_t presents_gpu_thread = 0, presents_ui_thread = 0, ui_repaints = 0;
  // VdSwap intervals in milliseconds.
  double swap_mean_ms = 0, swap_stddev_ms = 0, swap_p50_ms = 0, swap_p99_ms = 0, swap_max_ms = 0;
  // Intervals between presents of new frames, in milliseconds.
  double present_p99_ms = 0, present_max_ms = 0;
  // Vblank lateness in microseconds.
  double vblank_late_p99_us = 0, vblank_late_max_us = 0;
};
Summary Summarize(double seconds);

// Fills `out` with the intervals (ms) between the most recent events of one kind (presents count
// only when they show a new frame), oldest first; returns how many were written.
size_t RecentIntervals(Event event, float* out, size_t max);

// Appends the events recorded since the previous call to a CSV file (time_ms,event,payload).
void AppendCsv(const char* path);

}  // namespace x360::gpu::timing
