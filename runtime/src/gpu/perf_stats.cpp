#include "perf_stats.h"

#include <algorithm>
#include <cstdio>

#include "xenia/gpu/anyx360_counters.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

namespace x360::gpu::perf {

namespace {

uint64_t Qpc() {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return uint64_t(now.QuadPart);
}

double QpcSeconds(uint64_t ticks) {
  static const double frequency = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return double(f.QuadPart);
  }();
  return double(ticks) / frequency;
}

uint64_t FileTime(const FILETIME& t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; }

std::string ThreadName(HANDLE thread, uint32_t id) {
  PWSTR wide = nullptr;
  std::string name;
  if (SUCCEEDED(GetThreadDescription(thread, &wide)) && wide) {
    char utf8[128];
    if (wide[0] && WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr) > 0) name = utf8;
    LocalFree(wide);
  }
  if (name.empty()) {
    char text[32];
    std::snprintf(text, sizeof(text), "thread %u", id);
    name = text;
  }
  return name;
}

}  // namespace

ThreadCpuSampler::~ThreadCpuSampler() {
  for (auto& [id, entry] : threads_) CloseHandle(entry.handle);
}

CpuSample ThreadCpuSampler::Sample() {
  CpuSample sample;
  const uint64_t now = Qpc();
  const bool first = last_qpc_ == 0;
  sample.seconds = first ? 0 : QpcSeconds(now - last_qpc_);
  last_qpc_ = now;
  for (auto& [id, entry] : threads_) entry.seen = false;

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return sample;
  const DWORD process = GetCurrentProcessId();
  THREADENTRY32 te{};
  te.dwSize = sizeof(te);
  uint64_t total = 0;
  for (BOOL ok = Thread32First(snapshot, &te); ok; ok = Thread32Next(snapshot, &te)) {
    if (te.th32OwnerProcessID != process) continue;
    auto [it, added] = threads_.try_emplace(te.th32ThreadID);
    Entry& entry = it->second;
    if (added) entry.handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
    entry.seen = true;
    if (!entry.handle) continue;
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(entry.handle, &created, &exited, &kernel, &user)) continue;
    const uint64_t time = FileTime(kernel) + FileTime(user);
    const uint64_t delta = added ? time : time - std::min(time, entry.time);  // new: since it started
    entry.time = time;
    if (first || !delta) continue;
    total += delta;
    if (entry.name.empty() || ++entry.name_age >= 10) {
      entry.name = ThreadName(entry.handle, te.th32ThreadID);
      entry.name_age = 0;
    }
    sample.threads.push_back({entry.name, double(delta) * 1e-7 / sample.seconds});
  }
  CloseHandle(snapshot);
  for (auto it = threads_.begin(); it != threads_.end();) {
    if (it->second.seen) {
      ++it;
      continue;
    }
    CloseHandle(it->second.handle);
    it = threads_.erase(it);
  }
  if (first) return sample;
  sample.process = double(total) * 1e-7 / sample.seconds;
  std::sort(sample.threads.begin(), sample.threads.end(),
            [](const ThreadLoad& a, const ThreadLoad& b) { return a.busy > b.busy; });
  return sample;
}

GpuSample GpuCounterSampler::Sample(uint64_t swaps) {
  const xe::gpu::AnyX360Counters& c = xe::gpu::GetAnyX360Counters();
  const uint64_t now = Qpc();
  const uint64_t draws = c.draws.load(), copies = c.copies.load(), transfers = c.transfers.load(),
                 submissions = c.submissions.load(), busy_ns = c.gpu_busy_ns.load(),
                 timed = c.gpu_timed_submissions.load(), wait_ns = c.gpu_wait_ns.load(),
                 cp_busy_ns = c.cp_busy_ns.load(), cp_wait_mem_ns = c.cp_wait_mem_ns.load();
  GpuSample s;
  if (last_qpc_) {
    const double seconds = QpcSeconds(now - last_qpc_);
    s.frames = double(swaps - swaps_);
    const double per = s.frames > 0 ? 1.0 / s.frames : 0.0;
    s.draws = double(draws - draws_) * per;
    s.copies = double(copies - copies_) * per;
    s.transfers = double(transfers - transfers_) * per;
    s.submissions = double(submissions - submissions_) * per;
    s.timestamps = timed > timed_;
    s.gpu_ms = double(busy_ns - busy_ns_) * 1e-6 * per;
    s.gpu_busy = seconds > 0 ? double(busy_ns - busy_ns_) * 1e-9 / seconds : 0;
    s.cp_wait = seconds > 0 ? double(wait_ns - wait_ns_) * 1e-9 / seconds : 0;
    s.cp_wait_mem = seconds > 0 ? double(cp_wait_mem_ns - cp_wait_mem_ns_) * 1e-9 / seconds : 0;
    s.cp_work = seconds > 0 ? std::max(0.0, double(cp_busy_ns - cp_busy_ns_) * 1e-9 / seconds - s.cp_wait -
                                                s.cp_wait_mem)
                            : 0;
  }
  last_qpc_ = now, swaps_ = swaps, draws_ = draws, copies_ = copies, transfers_ = transfers,
  submissions_ = submissions, busy_ns_ = busy_ns, timed_ = timed, wait_ns_ = wait_ns, cp_busy_ns_ = cp_busy_ns,
  cp_wait_mem_ns_ = cp_wait_mem_ns;
  return s;
}

std::string FormatCpu(const CpuSample& sample, size_t count) {
  char text[96];
  std::snprintf(text, sizeof(text), "%.2f cores;", sample.process);
  std::string out = text;
  for (size_t i = 0; i < std::min(count, sample.threads.size()); ++i) {
    std::snprintf(text, sizeof(text), "%s %s %.0f%%", i ? "," : "", sample.threads[i].name.c_str(),
                  sample.threads[i].busy * 100);
    out += text;
  }
  return out;
}

std::string FormatGpu(const GpuSample& s) {
  char text[320], gpu[64] = "no GPU timestamps";
  if (s.timestamps) std::snprintf(gpu, sizeof(gpu), "%.1f ms GPU per frame (busy %.0f%%)", s.gpu_ms, s.gpu_busy * 100);
  std::snprintf(text, sizeof(text),
                "%s; command processor working %.0f%%, waiting for the GPU %.0f%%, for memory %.0f%%; per frame "
                "%.0f draws, %.1f resolves, %.1f transfers, %.1f submissions",
                gpu, s.cp_work * 100, s.cp_wait * 100, s.cp_wait_mem * 100, s.draws, s.copies, s.transfers,
                s.submissions);
  return text;
}

}  // namespace x360::gpu::perf