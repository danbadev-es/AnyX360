#include "core/paths.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <fstream>
#include <iterator>

#include "games_catalog.h"

namespace anyx360::launcher {
namespace {

fs::path& RootOverride() {
  static fs::path root;
  return root;
}

}  // namespace

fs::path Root() {
  if (!RootOverride().empty()) return RootOverride();
  static const fs::path root = [] {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
  }();
  return root;
}

void SetRootForTesting(const fs::path& root) { RootOverride() = root; }

bool Exists(const fs::path& path) {
  std::error_code ec;
  return fs::exists(path, ec);
}

fs::path GamesDir() { return Root() / L"games"; }
fs::path GameRoot(const CatalogGame& game) { return GamesDir() / game.id; }
fs::path ExtractedDir(const CatalogGame& game) { return GameRoot(game) / L"game"; }
fs::path LocationFile(const CatalogGame& game) { return GameRoot(game) / L"location.txt"; }
fs::path IconFile(const CatalogGame& game) { return GameRoot(game) / L"icon.png"; }
fs::path LogDir(const CatalogGame& game) { return Root() / L"logs" / game.id; }

fs::path CoverFile(const CatalogGame& game) {
  for (const wchar_t* name : {L"cover.png", L"cover.jpg"})
    if (Exists(GameRoot(game) / name)) return GameRoot(game) / name;
  return {};
}

fs::path FindUpwards(fs::path start, const fs::path& relative) {
  for (fs::path dir = start; !dir.empty(); dir = dir.parent_path()) {
    if (Exists(dir / relative)) return dir / relative;
    if (dir == dir.root_path()) break;
  }
  return {};
}

fs::path KeyFile() { return FindUpwards(Root(), L"keys/xex2_retail.key"); }

std::wstring ReadUtf8(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.rfind("\xEF\xBB\xBF", 0) == 0) bytes.erase(0, 3);  // saved by an editor with a BOM
  while (!bytes.empty() && (bytes.back() == '\n' || bytes.back() == '\r')) bytes.pop_back();
  int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), int(bytes.size()), nullptr, 0);
  std::wstring text(size_t(std::max(length, 0)), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, bytes.data(), int(bytes.size()), text.data(), length);
  return text;
}

bool WriteUtf8(const fs::path& path, const std::wstring& text) {
  int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string bytes(size_t(std::max(length, 0)), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), bytes.data(), length, nullptr, nullptr);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << bytes << "\n";
  return bool(out);
}

fs::path StoredLocation(const CatalogGame& game) {
  return Exists(LocationFile(game)) ? fs::path(ReadUtf8(LocationFile(game))) : fs::path();
}

fs::path FindProgram(const wchar_t* name) {
  for (const fs::path& path : {Root() / L"bin" / name, Root() / name})
    if (Exists(path)) return path;
  return {};
}

fs::path FindGameExe(const CatalogGame& game) {
  fs::path exe = FindProgram(game.exe);
  if (exe.empty() && Exists(game.dev_exe)) exe = fs::path(game.dev_exe).make_preferred();
  return exe;
}

fs::path SevenZip() {
  fs::path bundled = FindProgram(L"7z.exe");
  if (!bundled.empty()) return bundled;
  wchar_t* program_files = nullptr;
  fs::path installed;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &program_files)))
    installed = fs::path(program_files) / L"7-Zip" / L"7z.exe";
  CoTaskMemFree(program_files);
  return Exists(installed) ? installed : fs::path();
}

fs::path LocalAppData() {
  wchar_t path[MAX_PATH];
  DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
  if (length && length < MAX_PATH) return path;
  wchar_t* known = nullptr;
  fs::path result;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known))) result = known;
  CoTaskMemFree(known);
  return result;
}

fs::path SettingsFile(const wchar_t* name) { return LocalAppData() / L"AnyX360" / name; }

std::wstring ReadSetting(const fs::path& path) {
  std::wifstream in(path);
  std::wstring value;
  std::getline(in, value);
  return value;
}

void WriteSetting(const fs::path& path, const std::wstring& value) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::wofstream(path) << value << L"\n";
}

std::wstring Quote(const fs::path& path) { return L"\"" + path.wstring() + L"\""; }

}  // namespace anyx360::launcher
