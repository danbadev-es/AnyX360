#include "core/launch.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <thread>

#include "games_catalog.h"

namespace anyx360::launcher {

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
    if (Exists(destination)) continue;  // never replace the player's own (larger) cache
    fs::create_directories(destination.parent_path(), ec);
    fs::copy_file(it->path(), destination, ec);
  }
}

Message StartGame(const LibraryGame& game, std::function<void(uint32_t)> exited) {
  if (game.exe.empty() || !Exists(game.exe)) return {kTextErrorNoExe, {game.info->exe}, true};
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
  std::wstring command = Quote(game.exe) + L" --game " + Quote(game.dir);
  fs::path key = KeyFile();
  if (!key.empty()) command += L" --key " + Quote(key);
  STARTUPINFOW startup{sizeof(startup)};
  if (log != INVALID_HANDLE_VALUE) {
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = log;
    startup.hStdError = log;
  }
  PROCESS_INFORMATION process{};
  BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                Root().c_str(), &startup, &process);
  DWORD error = GetLastError();
  if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
  CloseHandle(input);
  if (!started) return {kTextErrorStart, {std::to_wstring(error)}, true};
  CloseHandle(process.hThread);
  std::thread([handle = process.hProcess, exited = std::move(exited)] {
    WaitForSingleObject(handle, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(handle, &code);
    CloseHandle(handle);
    if (exited) exited(uint32_t(code));
  }).detach();
  return {};
}

}  // namespace anyx360::launcher
