#include "core/sevenzip.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>

#include "core/jobs.h"

namespace anyx360::launcher {
namespace {

// One argument for CreateProcess, quoted the way CommandLineToArgvW reads it back (a password may
// hold quotes and backslashes).
std::wstring Argument(const std::wstring& text) {
  std::wstring quoted = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : text) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    quoted.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
    backslashes = 0;
    quoted += c;
  }
  quoted.append(backslashes * 2, L'\\');
  return quoted + L"\"";
}

// Always a password: without one 7-Zip would ask on its input. A wrong one only matters for
// encrypted archives, and then it reads as "wrong password".
std::wstring PasswordSwitch(const std::optional<std::wstring>& password) {
  return L" " + Argument(L"-p" + (password ? *password : std::wstring(L"AnyX360-no-password")));
}

// A part of a split archive, by its name (x.part2.rar, x.r00, x.001, x.z01).
bool IsVolumeName(const fs::path& archive) {
  return std::regex_match(archive.filename().wstring(),
                          std::wregex(L".*\\.(part\\d+\\.rar|r\\d\\d|\\d{3}|z\\d\\d)", std::regex::icase));
}

ArchiveError ErrorFrom(const std::string& output, bool password_given, const fs::path& archive) {
  auto has = [&](const char* text) { return output.find(text) != std::string::npos; };
  if (has("Wrong password") || has("encrypted archive")) {
    return password_given ? ArchiveError::kWrongPassword : ArchiveError::kNeedsPassword;
  }
  // A split 7z set without one of its parts ends early.
  if (has("Missing volume") || (has("Unexpected end of archive") && IsVolumeName(archive)))
    return ArchiveError::kMissingVolume;
  if (has("open the file as archive") || has("Can not open file as archive")) return ArchiveError::kNotArchive;
  return ArchiveError::kFailed;
}

// The output kept for reading errors: the start and the last part (progress lines are many).
void Keep(std::string& kept, const std::string& text) {
  kept += text;
  if (kept.size() > 256 * 1024) kept.erase(4096, kept.size() - 128 * 1024);
}

// 7-Zip's percent (-bsp1) in its output: progress lines ("  42% 120 - <file>") overwrite each other
// with backspaces, and a line may arrive in pieces.
class PercentReader {
 public:
  // The last percent in `text`, or -1.
  int Read(const std::string& text) {
    std::string chunk = tail_ + text;
    int percent = -1;
    for (size_t i = 0; i < chunk.size(); ++i) {
      if (chunk[i] != '%') continue;
      size_t begin = i;
      while (begin > 0 && i - begin < 3 && std::isdigit(static_cast<unsigned char>(chunk[begin - 1]))) --begin;
      if (begin < i) percent = std::stoi(chunk.substr(begin, i - begin));
    }
    tail_ = chunk.size() > 8 ? chunk.substr(chunk.size() - 8) : chunk;
    return percent;
  }

 private:
  std::string tail_;
};

}  // namespace

int RunCaptured(std::wstring command, const std::function<void(const std::string&)>& output, Job* job,
                const fs::path& folder) {
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
  BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                folder.empty() ? nullptr : folder.c_str(), &startup, &process);
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

fs::path FirstVolume(const fs::path& archive) {
  const std::wstring name = archive.filename().wstring();
  std::wsmatch m;
  std::wstring first;
  if (std::regex_match(name, m, std::wregex(L"(.*\\.part)(\\d+)(\\.rar)", std::regex::icase))) {
    std::wstring one = std::wstring(m[2].length() - 1, L'0') + L"1";
    first = m[1].str() + one + m[3].str();
  } else if (std::regex_match(name, m, std::wregex(L"(.*\\.)(\\d{3})", std::regex::icase))) {
    first = m[1].str() + L"001";
  } else if (std::regex_match(name, m, std::wregex(L"(.*\\.)r\\d\\d", std::regex::icase))) {
    first = m[1].str() + L"rar";
  } else if (std::regex_match(name, m, std::wregex(L"(.*\\.)z\\d\\d", std::regex::icase))) {
    first = m[1].str() + L"zip";
  }
  if (first.empty() || first == name || !Exists(archive.parent_path() / first)) return archive;
  return archive.parent_path() / first;
}

ArchiveError ListArchive(const fs::path& archive, const std::optional<std::wstring>& password, ArchiveListing& listing,
                         int& code) {
  // All of it: a disc's listing runs to megabytes (Keep would drop entries from its middle).
  std::string output;
  code = RunCaptured(Quote(SevenZip()) + L" l -slt -sccUTF-8" + PasswordSwitch(password) + L" " + Quote(archive),
                     [&](const std::string& text) { output += text; }, nullptr);
  if (code != 0) return ErrorFrom(output, password.has_value(), archive);
  // "Key = Value" lines: the archive's after "--", then one block per entry after "----------",
  // separated by empty lines.
  listing = {};
  enum { kBefore, kArchive, kEntries } section = kBefore;
  ArchiveEntry entry;
  bool in_entry = false;
  auto finish_entry = [&] {
    if (!in_entry) return;
    if (!entry.folder) listing.size += entry.size;
    listing.encrypted |= entry.encrypted;
    listing.entries.push_back(entry);
    entry = {};
    in_entry = false;
  };
  for (size_t start = 0; start <= output.size();) {
    size_t end = output.find('\n', start);
    if (end == std::string::npos) end = output.size();
    std::string line = output.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == "--" && section == kBefore) {
      section = kArchive;
      continue;
    }
    if (line == "----------") {
      section = kEntries;
      continue;
    }
    if (line.empty()) {
      if (section == kEntries) finish_entry();
      continue;
    }
    size_t equals = line.find(" = ");
    if (equals == std::string::npos) continue;
    std::string key = line.substr(0, equals), value = line.substr(equals + 3);
    if (section == kArchive) {
      if (key == "Type") listing.type = value;
    } else if (section == kEntries) {
      in_entry = true;
      if (key == "Path") entry.path = value;
      if (key == "Size" && !value.empty() && std::isdigit(static_cast<unsigned char>(value[0])))
        entry.size = std::stoull(value);
      if (key == "Folder") entry.folder = value == "+";
      if (key == "Attributes" && !value.empty() && value[0] == 'D') entry.folder = true;
      if (key == "Encrypted") entry.encrypted = value == "+";
    }
    if (start > output.size()) break;
  }
  finish_entry();
  if (!listing.size) {
    std::error_code ec;
    listing.size = fs::file_size(archive, ec);
  }
  return ArchiveError::kNone;
}

ArchiveError ExtractArchive(Job& job, const fs::path& archive, const std::optional<std::wstring>& password,
                            const fs::path& out, const std::vector<std::string>& only, int from, int to, int& code) {
  std::wstring command = Quote(SevenZip()) + L" x -y -bsp1 -bso0 -bse1 -sccUTF-8" + PasswordSwitch(password) + L" -o" +
                         Quote(out) + L" " + Quote(archive);
  for (const std::string& path : only) command += L" " + Argument(Widen(path));
  int last = 0;
  std::string kept;
  PercentReader reader;
  code = RunCaptured(
      command,
      [&](const std::string& text) {
        Keep(kept, text);
        // Only forward: the bar fills once even if 7-Zip's count restarts (multi-volume archives).
        const int percent = reader.Read(text);
        if (percent > last) {
          last = percent;
          job.Progress(std::min(from + percent * (to - from) / 100, 99));
        }
      },
      &job);
  return code == 0 ? ArchiveError::kNone : ErrorFrom(kept, password.has_value(), archive);
}

bool CreateArchive(Job& job, const fs::path& archive, const fs::path& folder, const fs::path& list,
                   const std::function<void(int)>& report, int& code) {
  // -ms=off: one stream per file, so the manifest reads back alone at once; -sse: a file that can't
  // be read stops it (else 7-Zip leaves it out with a warning).
  const std::wstring command = Quote(SevenZip()) + L" a -t7z -mx0 -ms=off -sse -bsp1 -bso0 -bse1 -sccUTF-8 -scsUTF-8 " +
                               Quote(archive) + L" " + Argument(L"@" + list.wstring());
  int last = -1;
  PercentReader reader;
  code = RunCaptured(
      command,
      [&](const std::string& text) {
        const int percent = reader.Read(text);
        if (percent > last) report(last = percent);
      },
      &job, folder);
  return code == 0;
}

bool ParseContentPath(const std::string& name, const char* type_folder, uint32_t& title_id, std::string& folder) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  std::replace(lower.begin(), lower.end(), '/', '\\');
  std::string needle = std::string("\\") + type_folder;
  for (size_t at = lower.find(needle); at != std::string::npos; at = lower.find(needle, at + 1)) {
    size_t after = at + needle.size();
    if (after != lower.size() && lower[after] != '\\') continue;  // the folder itself, or a path in it
    if (at < 8) continue;
    size_t start = at - 8;
    if (start > 0 && lower[start - 1] != '\\') continue;
    if (!std::all_of(lower.begin() + start, lower.begin() + at, [](char c) { return std::isxdigit(uint8_t(c)); }))
      continue;
    title_id = uint32_t(std::stoul(lower.substr(start, 8), nullptr, 16));
    folder = name.substr(0, after);
    return true;
  }
  return false;
}

ArchiveInfo ClassifyArchive(const ArchiveListing& listing) {
  ArchiveInfo info;
  auto add_folder = [&](const std::string& folder) {
    if (std::find(info.content_folders.begin(), info.content_folders.end(), folder) == info.content_folders.end())
      info.content_folders.push_back(folder);
  };
  for (const ArchiveEntry& e : listing.entries) {
    std::string lower = e.path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (!e.folder && (lower == "default.xex" || (lower.size() > 12 && lower.compare(lower.size() - 12, 12, "\\default.xex") == 0)))
      info.has_game = true;
  }
  // Games on Demand first (a Games on Demand copy may also hold the title's arcade demo), then
  // Xbox Live Arcade: the package's folder and the title's downloadable content.
  for (const char* type : {"00007000", "000d0000"}) {
    for (const ArchiveEntry& e : listing.entries) {
      uint32_t title = 0;
      std::string folder;
      if (!ParseContentPath(e.path, type, title, folder) || (info.content_title && title != info.content_title))
        continue;
      info.content_title = title;
      info.arcade = std::strcmp(type, "000d0000") == 0;
      add_folder(folder);
    }
    if (info.content_title) break;
  }
  if (info.content_title) {
    for (const char* type : {"00000002", "000b0000"}) {
      for (const ArchiveEntry& e : listing.entries) {
        uint32_t title = 0;
        std::string folder;
        if (ParseContentPath(e.path, type, title, folder) && title == info.content_title) add_folder(folder);
      }
    }
    // Title updates as the console's cache keeps them: TU_<...> files, of whichever title (their
    // header says; the copy's own is installed, others are noted).
    for (const ArchiveEntry& e : listing.entries) {
      if (e.folder) continue;
      const size_t slash = e.path.find_last_of("\\/");
      const std::string name = slash == std::string::npos ? e.path : e.path.substr(slash + 1);
      if (name.size() > 3 && (name[0] == 'T' || name[0] == 't') && (name[1] == 'U' || name[1] == 'u') && name[2] == '_')
        add_folder(e.path);
    }
  }
  return info;
}

}  // namespace anyx360::launcher
