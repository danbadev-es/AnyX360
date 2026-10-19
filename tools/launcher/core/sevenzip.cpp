#include "core/sevenzip.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "core/jobs.h"

namespace anyx360::launcher {

int RunCaptured(std::wstring command, const std::function<void(const std::string&)>& output, Job* job) {
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE read = nullptr, write = nullptr;
  if (!CreatePipe(&read, &write, &inherit, 0)) return -1;
  SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
  HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
                             nullptr);
  STARTUPINFOW startup{sizeof(startup)};
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input;
  startup.hStdOutput = write;
  startup.hStdError = write;
  PROCESS_INFORMATION process{};
  BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                &startup, &process);
  CloseHandle(write);
  CloseHandle(input);
  if (!started) {
    CloseHandle(read);
    return -1;
  }
  if (job) job->SetProcess(process.hProcess);
  char buffer[4096];
  DWORD length = 0;
  while (ReadFile(read, buffer, sizeof(buffer), &length, nullptr) && length) output(std::string(buffer, length));
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  if (job) job->SetProcess(nullptr);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CloseHandle(read);
  return int(code);
}

bool ParseContentPath(const std::string& name, const char* type_folder, uint32_t& title_id, std::string& folder) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  std::replace(lower.begin(), lower.end(), '/', '\\');
  std::string needle = std::string("\\") + type_folder + "\\";
  size_t at = lower.find(needle);
  if (at == std::string::npos || at < 8) return false;
  size_t start = at - 8;
  if (start > 0 && lower[start - 1] != '\\') return false;
  for (size_t i = start; i < at; ++i)
    if (!std::isxdigit(static_cast<unsigned char>(lower[i]))) return false;
  title_id = uint32_t(std::stoul(lower.substr(start, 8), nullptr, 16));
  folder = name.substr(0, at + 1 + std::strlen(type_folder));
  return true;
}

bool ListArchive(const fs::path& archive, ArchiveInfo& info, Message& error) {
  std::string output;
  int code = RunCaptured(Quote(SevenZip()) + L" l -sccUTF-8 " + Quote(archive),
                         [&](const std::string& text) { output += text; }, nullptr);
  if (code != 0) {
    error = {kTextErrorList, {std::to_wstring(code)}, true};
    return false;
  }
  std::vector<std::string> lines;
  for (size_t start = 0; start < output.size();) {
    size_t end = output.find('\n', start);
    if (end == std::string::npos) end = output.size();
    std::string line = output.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
    start = end + 1;
  }
  size_t summary = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    std::string lower = lines[i];
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (lower.size() >= 11 && lower.compare(lower.size() - 11, 11, "default.xex") == 0) info.has_game = true;
    if (lines[i].rfind("-------------------", 0) == 0) summary = i + 1;
    // File lines: "<date> <time> <attributes> <size> <compressed>  <name>", the name at column 53.
    uint32_t title = 0;
    std::string folder;
    if (lines[i].size() > 53 && ParseContentPath(lines[i].substr(53), "00007000", title, folder)) {
      info.god_title = title;
      if (std::find(info.god_folders.begin(), info.god_folders.end(), folder) == info.god_folders.end())
        info.god_folders.push_back(folder);
    }
  }
  // The title's downloadable content goes with it.
  if (info.god_title) {
    for (const std::string& line : lines) {
      uint32_t title = 0;
      std::string folder;
      if (line.size() > 53 && ParseContentPath(line.substr(53), "00000002", title, folder) &&
          title == info.god_title &&
          std::find(info.god_folders.begin(), info.god_folders.end(), folder) == info.god_folders.end())
        info.god_folders.push_back(folder);
    }
  }
  // Totals line after the last separator: "<date> <time> <size> <compressed> <n> files, ...".
  if (summary && summary < lines.size()) {
    std::vector<std::string> tokens;
    for (size_t start = 0; start < lines[summary].size();) {
      size_t begin = lines[summary].find_first_not_of(' ', start);
      if (begin == std::string::npos) break;
      size_t end = lines[summary].find(' ', begin);
      if (end == std::string::npos) end = lines[summary].size();
      tokens.push_back(lines[summary].substr(begin, end - begin));
      start = end;
    }
    if (tokens.size() > 2 && !tokens[2].empty() &&
        std::all_of(tokens[2].begin(), tokens[2].end(), [](char c) { return c >= '0' && c <= '9'; }))
      info.size = std::stoull(tokens[2]);
  }
  if (!info.size) {
    std::error_code ec;
    info.size = fs::file_size(archive, ec);
  }
  return true;
}

int ExtractArchive(Job& job, const fs::path& archive, const fs::path& out, const std::vector<std::string>& only,
                   int from, int to) {
  std::wstring command =
      Quote(SevenZip()) + L" x -y -bsp1 -bso0 -bse1 -sccUTF-8 -o" + Quote(out) + L" " + Quote(archive);
  for (const std::string& folder : only) command += L" " + Quote(Widen(folder));
  int last = 0;
  std::string tail;
  return RunCaptured(
      command,
      [&](const std::string& text) {
        // Progress lines ("  42% 120 - <file>") overwrite each other with backspaces.
        std::string chunk = tail + text;
        int percent = -1;
        for (size_t i = 0; i < chunk.size(); ++i) {
          if (chunk[i] != '%') continue;
          size_t begin = i;
          while (begin > 0 && i - begin < 3 && std::isdigit(static_cast<unsigned char>(chunk[begin - 1]))) --begin;
          if (begin < i) percent = std::stoi(chunk.substr(begin, i - begin));
        }
        tail = chunk.size() > 8 ? chunk.substr(chunk.size() - 8) : chunk;
        // Only forward: the bar fills once even if 7-Zip's count restarts (multi-volume archives).
        if (percent > last) {
          last = percent;
          job.Progress(std::min(from + percent * (to - from) / 100, 99));
        }
      },
      &job);
}

}  // namespace anyx360::launcher
