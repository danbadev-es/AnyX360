#include "core/launch.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "games_catalog.h"

namespace anyx360::launcher {

namespace {

// Xenia's cache files (src/xenia/gpu/shader_storage.h). .xsh: an 8-byte header ('XESH', version),
// then each shader: a u64 hash, a u32 dword count (31 bits) and type, the microcode. .xpso: a 12-byte
// header ('XEPS', API, version), then 72-byte D3D12 pipeline descriptions, each starting with its hash.
constexpr uint32_t kShaderMagic = 0x48534558, kPipelineMagic = 0x53504558;
constexpr size_t kPipelineRecord = 72;

struct CacheFile {
  std::string header;
  std::vector<std::pair<uint64_t, std::string>> entries;  // whole entries only, by hash
};

bool ReadCacheFile(const fs::path& path, CacheFile& file) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, data.data() + at, 4); return v; };
  auto u64 = [&](size_t at) { uint64_t v; std::memcpy(&v, data.data() + at, 8); return v; };
  const bool shaders = path.extension() == L".xsh";
  const size_t header = shaders ? 8 : 12;
  if (data.size() < header || u32(0) != (shaders ? kShaderMagic : kPipelineMagic)) return false;
  file.header = data.substr(0, header);
  for (size_t pos = header; pos < data.size();) {
    const size_t size = shaders ? (pos + 12 <= data.size() ? 12 + size_t(u32(pos + 8) & 0x7FFFFFFF) * 4 : 0)
                                : kPipelineRecord;
    if (!size || pos + size > data.size()) break;  // a cut-off tail (the game closed mid-write)
    file.entries.emplace_back(u64(pos), data.substr(pos, size));
    pos += size;
  }
  return true;
}

}  // namespace

size_t MergeShaderCacheFile(const fs::path& source, const fs::path& target) {
  std::error_code ec;
  CacheFile from, to;
  if (!ReadCacheFile(source, from)) return 0;
  if (!Exists(target)) {
    fs::create_directories(target.parent_path(), ec);
    return fs::copy_file(source, target, ec) ? from.entries.size() : 0;
  }
  if (!ReadCacheFile(target, to) || to.header != from.header) return 0;
  std::unordered_set<uint64_t> known;
  for (const auto& entry : to.entries) known.insert(entry.first);
  std::string merged = to.header;
  for (const auto& entry : to.entries) merged += entry.second;
  size_t added = 0;
  for (const auto& entry : from.entries)
    if (known.insert(entry.first).second) merged += entry.second, ++added;
  if (!added) return 0;
  // Written aside and moved over, so a failure never leaves the player's cache half written.
  const fs::path temporary = target.wstring() + L".merging";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(merged.data(), std::streamsize(merged.size()));
    if (!out) return 0;
  }
  fs::rename(temporary, target, ec);
  return ec ? 0 : added;
}

void SeedShaderCache(const CatalogGame& game) {
  fs::path source = Root() / L"bin" / L"shader_cache" / game.id;
  fs::path local = LocalAppData();
  if (!Exists(source) || local.empty()) return;
  fs::path target = local / L"AnyX360" / game.user_dir / L"cache" / L"shaders";
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(source, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    fs::path destination = target / fs::relative(it->path(), source, ec);
    // The player's own cache keeps what it has and gets what it lacks: a player who first played
    // before a release shipped the game's cache (or with fewer of its shaders) gets them too.
    MergeShaderCacheFile(it->path(), destination);
  }
}

std::wstring GameCommandLine(const fs::path& exe, const fs::path& game_dir, const fs::path& key,
                             const LaunchOptions& options) {
  std::wstring command = Quote(exe) + L" --game " + Quote(game_dir);
  if (!key.empty()) command += L" --key " + Quote(key);
  if (options.dashboard_pid) command += L" --dashboard " + std::to_wstring(options.dashboard_pid);
  if (options.window) {
    const std::string rect = x360::dashboard::FormatWindowArg(*options.window);
    command += L" --window " + std::wstring(rect.begin(), rect.end());
  }
  if (options.fullscreen) command += L" --fullscreen";
  return command;
}

namespace {

// The folder of a process's executable (lowercase); empty if it can't be read.
std::wstring ProcessFolder(HANDLE process) {
  wchar_t path[MAX_PATH * 2];
  DWORD size = DWORD(std::size(path));
  if (!QueryFullProcessImageNameW(process, 0, path, &size)) return {};
  std::wstring folder = fs::path(std::wstring(path, size)).parent_path().wstring();
  for (wchar_t& c : folder) c = wchar_t(std::towlower(c));
  return folder;
}

// A job object and the processes of it that are followed, by ID. Its completion port's messages:
// a process started, one ended, none left.
struct Follower {
  HANDLE job = nullptr, port = nullptr;
  std::wstring folder;
  std::map<DWORD, HANDLE> followed;
  uint32_t last_code = 0;
  std::function<void(uint32_t)> exited;

  ~Follower() {
    for (auto& [pid, handle] : followed) CloseHandle(handle);
    if (job) CloseHandle(job);
    if (port) CloseHandle(port);
  }

  void Follow(DWORD pid, HANDLE handle) {
    if (followed.count(pid)) {
      CloseHandle(handle);
      return;
    }
    if (ProcessFolder(handle) != folder) {  // a helper (the pose bridge): not the game
      CloseHandle(handle);
      return;
    }
    followed[pid] = handle;
  }

  // Until the last followed process has ended (its port's thread).
  void Run() {
    for (;;) {
      DWORD message = 0;
      ULONG_PTR key = 0;
      LPOVERLAPPED data = nullptr;
      if (!GetQueuedCompletionStatus(port, &message, &key, &data, INFINITE)) break;
      const DWORD pid = DWORD(reinterpret_cast<ULONG_PTR>(data));      if (message == JOB_OBJECT_MSG_NEW_PROCESS) {
        if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid)) Follow(pid, process);
      } else if (message == JOB_OBJECT_MSG_EXIT_PROCESS || message == JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS) {
        auto found = followed.find(pid);
        if (found == followed.end()) continue;
        WaitForSingleObject(found->second, 5000);
        DWORD code = 0;
        GetExitCodeProcess(found->second, &code);
        last_code = code;
        CloseHandle(found->second);
        followed.erase(found);
        if (followed.empty()) break;
      } else if (message == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO) {
        // It can come before the processes' own messages: the code of the one that ended last.
        FILETIME latest{};
        for (auto& [id, handle] : followed) {
          FILETIME created, ended, kernel, user;
          DWORD code = 0;
          if (WaitForSingleObject(handle, 5000) == WAIT_OBJECT_0 && GetProcessTimes(handle, &created, &ended, &kernel, &user) &&
              CompareFileTime(&ended, &latest) >= 0 && GetExitCodeProcess(handle, &code)) {
            latest = ended;
            last_code = code;
          }
        }
        break;
      }
    }
    if (exited) exited(last_code);
  }
};

}  // namespace

uint32_t StartFollowed(std::wstring command, const fs::path& dir, void* std_in, void* std_out,
                       std::function<void(uint32_t code)> exited) {
  auto follower = std::make_shared<Follower>();
  follower->exited = std::move(exited);
  follower->job = CreateJobObjectW(nullptr, nullptr);
  follower->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
  if (!follower->job || !follower->port) return GetLastError();
  JOBOBJECT_ASSOCIATE_COMPLETION_PORT port{follower->job, follower->port};
  if (!SetInformationJobObject(follower->job, JobObjectAssociateCompletionPortInformation, &port, sizeof(port)))
    return GetLastError();
  STARTUPINFOW startup{sizeof(startup)};
  if (std_out) {
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = std_in;
    startup.hStdOutput = std_out;
    startup.hStdError = std_out;
  }
  // Suspended until it is in the job, so whatever it starts is in it too.
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                      dir.empty() ? nullptr : dir.c_str(), &startup, &process))
    return GetLastError();
  if (!AssignProcessToJobObject(follower->job, process.hProcess)) {
    const DWORD error = GetLastError();
    TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return error;
  }
  follower->folder = ProcessFolder(process.hProcess);
  follower->Follow(process.dwProcessId, process.hProcess);
  ResumeThread(process.hThread);
  CloseHandle(process.hThread);
  std::thread([follower] { follower->Run(); }).detach();
  return 0;
}

Message StartGame(const LibraryGame& game, const LaunchOptions& options, GameEvents events) {
  if (game.exe.empty() || !Exists(game.exe)) return {kTextErrorNoExe, {game.info->exe}, true};
  if (game.needs_update) return {kTextNeedsUpdate, {game.info->title, game.info->update_version}, true};
  SeedShaderCache(*game.info);
  std::error_code ec;
  fs::path logs = LogDir(*game.info);
  fs::create_directories(logs, ec);
  fs::path log_path = logs / L"last_run.log";
  if (Exists(log_path)) {
    fs::remove(logs / L"previous_run.log", ec);
    fs::rename(log_path, logs / L"previous_run.log", ec);
  }
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE log = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
                             nullptr);
  const std::wstring command = GameCommandLine(game.exe, game.dir, KeyFile(), options);
  // The dashboard's events (x360/dashboard.h): auto-reset, made before the game opens them. A thread
  // answers them until the game has ended.
  struct Session {
    HANDLE ready = nullptr, leaving = nullptr, shown = nullptr, stop = nullptr;
    ~Session() {
      for (HANDLE h : {ready, leaving, shown, stop})
        if (h) CloseHandle(h);
    }
  };
  auto session = std::make_shared<Session>();
  if (options.dashboard_pid) {
    using x360::dashboard::EventName;
    session->ready = CreateEventW(nullptr, FALSE, FALSE, EventName(options.dashboard_pid, L"ready").c_str());
    session->leaving = CreateEventW(nullptr, FALSE, FALSE, EventName(options.dashboard_pid, L"leaving").c_str());
    session->shown = CreateEventW(nullptr, FALSE, FALSE, EventName(options.dashboard_pid, L"shown").c_str());
    session->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  }
  auto exited = [session, on_exit = std::move(events.exited)](uint32_t code) {
    if (session->stop) SetEvent(session->stop);
    if (on_exit) on_exit(code);
  };
  const uint32_t error = StartFollowed(command, Root(), input, log != INVALID_HANDLE_VALUE ? log : nullptr, exited);
  if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
  CloseHandle(input);
  if (error) return {kTextErrorStart, {std::to_wstring(error)}, true};
  if (session->ready && session->leaving && session->shown && session->stop) {
    std::thread([session, ready = std::move(events.ready), leaving = std::move(events.leaving)] {
      for (;;) {
        HANDLE handles[] = {session->stop, session->ready, session->leaving};
        const DWORD which = WaitForMultipleObjects(3, handles, FALSE, INFINITE);
        if (which == WAIT_OBJECT_0 + 1) {
          if (ready) ready();
        } else if (which == WAIT_OBJECT_0 + 2) {
          HANDLE shown = session->shown;
          if (leaving) leaving([session, shown] { SetEvent(shown); });
          else SetEvent(shown);
        } else {
          return;
        }
      }
    }).detach();
  }
  return {};
}

}  // namespace anyx360::launcher
