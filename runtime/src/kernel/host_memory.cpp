// The host's memory, for the 5 s log line, the F1 overlay and fatal errors: this process's commit,
// Windows' commit charge and limit, the RAM free, and which processes use the most.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

namespace x360::kernel {

bool HostMemory::NearCommitLimit() const {
  return commit_limit && system_commit > commit_limit - commit_limit / 10;
}

bool HostMemory::Low() const { return NearCommitLimit() || (ram_total && ram_available < ram_total / 20); }

HostMemory QueryHostMemory() {
  HostMemory m;
  PROCESS_MEMORY_COUNTERS_EX process{};
  process.cb = sizeof(process);
  if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process),
                           sizeof(process))) {
    m.commit = process.PrivateUsage;
    m.working_set = process.WorkingSetSize;
  }
  PERFORMANCE_INFORMATION system{};
  system.cb = sizeof(system);
  if (GetPerformanceInfo(&system, sizeof(system))) {
    const uint64_t page = system.PageSize;
    m.system_commit = uint64_t(system.CommitTotal) * page;
    m.commit_limit = uint64_t(system.CommitLimit) * page;
    m.ram_available = uint64_t(system.PhysicalAvailable) * page;
    m.ram_total = uint64_t(system.PhysicalTotal) * page;
  }
  return m;
}

std::string FormatHostMemory(const HostMemory& m) {
  constexpr double kGb = 1024.0 * 1024.0 * 1024.0;
  char text[160];
  std::snprintf(text, sizeof(text), "this process %.2f GB; Windows %.1f of %.1f GB committed, %.1f of %.1f GB RAM free",
                m.commit / kGb, m.system_commit / kGb, m.commit_limit / kGb, m.ram_available / kGb,
                m.ram_total / kGb);
  return text;
}

std::string LargestProcesses(size_t count) {
  std::vector<DWORD> ids(1024);
  DWORD bytes = 0;
  for (;;) {
    if (!EnumProcesses(ids.data(), DWORD(ids.size() * sizeof(DWORD)), &bytes)) return {};
    if (bytes < ids.size() * sizeof(DWORD)) break;
    ids.resize(ids.size() * 2);
  }
  ids.resize(bytes / sizeof(DWORD));
  struct Entry {
    uint64_t commit;
    std::string name;
  };
  std::vector<Entry> entries;
  for (DWORD id : ids) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!process) continue;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
      wchar_t path[MAX_PATH];
      DWORD length = MAX_PATH;
      std::string name = "pid " + std::to_string(id);
      if (QueryFullProcessImageNameW(process, 0, path, &length)) {
        const wchar_t* file = path;
        for (const wchar_t* p = path; *p; ++p)
          if (*p == L'\\' || *p == L'/') file = p + 1;
        char utf8[MAX_PATH * 3];
        if (WideCharToMultiByte(CP_UTF8, 0, file, -1, utf8, sizeof(utf8), nullptr, nullptr) > 0) name = utf8;
      }
      entries.push_back({counters.PrivateUsage, std::move(name)});
    }
    ::CloseHandle(process);
  }
  const size_t n = std::min(count, entries.size());
  std::partial_sort(entries.begin(), entries.begin() + n, entries.end(),
                    [](const Entry& a, const Entry& b) { return a.commit > b.commit; });
  std::string out;
  char text[48];
  for (size_t i = 0; i < n; ++i) {
    std::snprintf(text, sizeof(text), " %.1f GB", entries[i].commit / (1024.0 * 1024.0 * 1024.0));
    out += (i ? ", " : "") + entries[i].name + text;
  }
  return out;
}

}  // namespace x360::kernel
