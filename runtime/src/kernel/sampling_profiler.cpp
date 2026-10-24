// ANYX360_PROFILE="<thread>[|<thread>...]:<start s>:<seconds>": a sampling profiler for named threads of
// the process (guest threads by the names the title gives them, "GPU Commands", "Backend"...), for
// finding what limits the frame rate without an external profiler. Every 0.5 ms it suspends each
// thread in turn, reads its instruction pointer and unwinds up to 24 frames, then logs, per thread,
// the functions where the samples landed (self) and the functions on the stacks (inclusive), named
// through the executable's .pdb (recompiled guest functions are sub_XXXXXXXX).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#pragma comment(lib, "dbghelp.lib")

namespace x360::kernel {

namespace {

struct Target {
  std::string name;
  std::vector<HANDLE> threads;
  uint64_t samples = 0;
  std::unordered_map<uint64_t, uint64_t> self;       // address -> samples
  std::unordered_map<uint64_t, uint64_t> inclusive;  // function start -> samples
  std::map<std::pair<uint64_t, uint64_t>, uint64_t> callers;  // (address, caller's address) -> samples
};

std::string Describe(HANDLE thread) {
  PWSTR wide = nullptr;
  std::string name;
  if (SUCCEEDED(GetThreadDescription(thread, &wide)) && wide) {
    char utf8[128];
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr) > 0) name = utf8;
    LocalFree(wide);
  }
  return name;
}

// Threads of this process whose description starts with `name`.
std::vector<HANDLE> FindThreads(const std::string& name) {
  std::vector<HANDLE> out;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return out;
  THREADENTRY32 te{};
  te.dwSize = sizeof(te);
  for (BOOL ok = Thread32First(snapshot, &te); ok; ok = Thread32Next(snapshot, &te)) {
    if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE,
                          te.th32ThreadID);
    if (!h) continue;
    if (Describe(h).rfind(name, 0) == 0)
      out.push_back(h);
    else
      ::CloseHandle(h);
  }
  ::CloseHandle(snapshot);
  return out;
}

// Function start of an address (for inclusive counts) from its unwind data; 0 if none.
uint64_t FunctionStart(uint64_t pc) {
  DWORD64 image = 0;
  if (PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(pc, &image, nullptr)) return image + f->BeginAddress;
  return 0;
}

// Unwinds the suspended thread's stack (the same address space) into `frames`. A frame without unwind
// data (a leaf) returns through [rsp]. A sample taken in a prologue or in code without unwind data can
// lead the unwinder astray: a fault ends the walk.
int Unwind(CONTEXT& context, uint64_t* frames, int max) {
  int count = 0;
  __try {
    for (; count < max && context.Rip; ++count) {
      frames[count] = context.Rip;
      DWORD64 image = 0;
      PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
      if (!f) {
        context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
        context.Rsp += 8;
      } else {
        void* handler_data = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, f, &context, &handler_data, &establisher, nullptr);
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return count;
}

void SampleOnce(Target& target, HANDLE thread) {
  if (::SuspendThread(thread) == DWORD(-1)) return;
  CONTEXT context{};
  context.ContextFlags = CONTEXT_FULL;
  uint64_t frames[24];
  int count = 0;
  if (GetThreadContext(thread, &context)) count = Unwind(context, frames, 24);
  ::ResumeThread(thread);
  if (!count) return;
  ++target.samples;
  ++target.self[frames[0]];
  if (count > 1) ++target.callers[{frames[0], frames[1]}];
  std::set<uint64_t> seen;
  for (int i = 0; i < count; ++i)
    if (uint64_t start = FunctionStart(frames[i]); start && seen.insert(start).second) ++target.inclusive[start];
}

std::string Symbol(uint64_t address, bool with_offset) {
  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = 255;
  DWORD64 displacement = 0;
  char text[320];
  if (SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)) {
    if (with_offset)
      std::snprintf(text, sizeof(text), "%s+0x%llX", symbol->Name, (unsigned long long)displacement);
    else
      std::snprintf(text, sizeof(text), "%s", symbol->Name);
  } else {
    HMODULE module = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module))
      GetModuleFileNameA(module, path, MAX_PATH);
    const char* base = std::strrchr(path, '\\');
    std::snprintf(text, sizeof(text), "%s!%llX", base ? base + 1 : path, (unsigned long long)address);
  }
  return text;
}

void Report(Target& target, double seconds) {
  KLOG_I("profile: %s: %llu samples in %.0f s (%zu threads)", target.name.c_str(),
         (unsigned long long)target.samples, seconds, target.threads.size());
  if (!target.samples) return;
  // Self time by function (addresses grouped by their symbol).
  std::map<std::string, uint64_t> self;
  for (auto& [address, n] : target.self) self[Symbol(address, false)] += n;
  std::vector<std::pair<uint64_t, std::string>> sorted;
  for (auto& [name, n] : self) sorted.push_back({n, name});
  std::sort(sorted.rbegin(), sorted.rend());
  for (size_t i = 0; i < std::min<size_t>(40, sorted.size()); ++i)
    KLOG_I("profile: %s self %5.1f%% %s", target.name.c_str(), 100.0 * sorted[i].first / target.samples,
           sorted[i].second.c_str());
  // Who calls the busiest functions (the linker folds identical functions, so a name alone can
  // mislead).
  std::map<std::string, std::map<std::string, uint64_t>> callers;
  for (auto& [pair, n] : target.callers) callers[Symbol(pair.first, false)][Symbol(pair.second, false)] += n;
  for (size_t i = 0; i < std::min<size_t>(12, sorted.size()); ++i) {
    std::vector<std::pair<uint64_t, std::string>> from;
    for (auto& [name, n] : callers[sorted[i].second]) from.push_back({n, name});
    std::sort(from.rbegin(), from.rend());
    for (size_t j = 0; j < std::min<size_t>(3, from.size()); ++j)
      KLOG_I("profile: %s from  %5.1f%% %s <- %s", target.name.c_str(), 100.0 * from[j].first / target.samples,
             sorted[i].second.c_str(), from[j].second.c_str());
  }
  sorted.clear();
  for (auto& [start, n] : target.inclusive) sorted.push_back({n, Symbol(start, false)});
  std::sort(sorted.rbegin(), sorted.rend());
  for (size_t i = 0; i < std::min<size_t>(50, sorted.size()); ++i)
    KLOG_I("profile: %s incl %5.1f%% %s", target.name.c_str(), 100.0 * sorted[i].first / target.samples,
           sorted[i].second.c_str());
}

}  // namespace

void StartSamplingProfiler() {
  const char* spec = std::getenv("ANYX360_PROFILE");
  if (!spec) return;
  std::string text = spec;
  // "<names>:<start>:<seconds>", names separated by '|'.
  size_t last = text.rfind(':');
  size_t middle = last == std::string::npos ? std::string::npos : text.rfind(':', last - 1);
  if (middle == std::string::npos) {
    KLOG_W("ANYX360_PROFILE: expected \"<thread>[|<thread>]:<start s>:<seconds>\"");
    return;
  }
  const double start = std::atof(text.c_str() + middle + 1), seconds = std::atof(text.c_str() + last + 1);
  std::vector<std::string> names;
  for (size_t pos = 0; pos < middle;) {
    size_t end = std::min(text.find('|', pos), middle);
    names.push_back(text.substr(pos, end - pos));
    pos = end + 1;
  }
  std::thread([names, start, seconds] {
    SetThreadDescription(GetCurrentThread(), L"Profiler");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    HandleOwnFaultsOnThisThread();
    std::this_thread::sleep_for(std::chrono::duration<double>(start));
    // The crash handler has initialised the symbol handler (InstallCrashHandler).
    std::vector<Target> targets;
    for (const std::string& name : names) {
      Target t;
      t.name = name;
      t.threads = FindThreads(name);
      KLOG_I("profile: %s: %zu threads", name.c_str(), t.threads.size());
      targets.push_back(std::move(t));
    }
    const auto begin = std::chrono::steady_clock::now();
    const auto end = begin + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
      for (Target& t : targets)
        for (HANDLE h : t.threads) SampleOnce(t, h);
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    for (Target& t : targets) {
      Report(t, elapsed);
      for (HANDLE h : t.threads) ::CloseHandle(h);
    }
  }).detach();
}

}  // namespace x360::kernel