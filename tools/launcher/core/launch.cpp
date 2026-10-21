#include "core/launch.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <fstream>
#include <iterator>
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
