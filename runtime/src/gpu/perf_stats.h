// Where a frame's time goes, for the 5 s log lines and the F1 overlay: the CPU time of each thread
// of the process (guest threads by the names the title gives them, the GPU command processor, the
// host's own threads), and the GPU's counters (draws, resolves, render target transfers, the GPU's
// time from timestamp queries and the command processor's waits for the GPU). Any thread; each
// sampler instance belongs to one caller.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace x360::gpu::perf {

struct ThreadLoad {
  std::string name;
  double busy = 0;  // share of one core over the interval (1 = a whole core)
};

struct CpuSample {
  double seconds = 0;  // interval
  double process = 0;  // cores used by the whole process
  std::vector<ThreadLoad> threads;  // busiest first
};

class ThreadCpuSampler {
 public:
  ~ThreadCpuSampler();
  // CPU time of every thread of the process since the previous call (the first call starts it).
  CpuSample Sample();

 private:
  struct Entry {
    void* handle = nullptr;
    uint64_t time = 0;  // kernel + user, 100 ns
    bool seen = false;
    std::string name;      // cached: reading it for every thread each time costs more than the rest
    uint32_t name_age = 0;  // samples since it was read (titles name threads after they start)
  };
  std::map<uint32_t, Entry> threads_;
  uint64_t last_qpc_ = 0;
};

struct GpuSample {
  double frames = 0;  // guest frames (swaps) in the interval
  double draws = 0, copies = 0, transfers = 0, submissions = 0;  // per frame
  double gpu_ms = 0;     // GPU time per frame (timestamps); 0 if unavailable
  double gpu_busy = 0;   // share of the interval the GPU spent on the guest's work
  double cp_work = 0;    // share of the interval the command processor executed commands
  double cp_wait = 0;    // share of the interval the command processor waited for the GPU
  double cp_wait_mem = 0;  // share it waited in WAIT_REG_MEM (for the title's CPU or a register)
  bool timestamps = false;
};

class GpuCounterSampler {
 public:
  // The counters since the previous call, per guest frame (`swaps`: the swap count now).
  GpuSample Sample(uint64_t swaps);

 private:
  uint64_t last_qpc_ = 0, swaps_ = 0, draws_ = 0, copies_ = 0, transfers_ = 0, submissions_ = 0,
           busy_ns_ = 0, timed_ = 0, wait_ns_ = 0, cp_busy_ns_ = 0, cp_wait_mem_ns_ = 0;
};

// "Main 98%, Server 74%, GPU Commands 61%..." (the `count` busiest), and the process total.
std::string FormatCpu(const CpuSample& sample, size_t count);
std::string FormatGpu(const GpuSample& sample);

}  // namespace x360::gpu::perf