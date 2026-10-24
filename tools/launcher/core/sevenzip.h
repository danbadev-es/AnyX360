// Archives through 7-Zip (bin\7z.exe in a release, else an installed one), run without a window:
// 7z, zip, rar (rar5, multi-part), split .001 sets, tar, gz, bz2, xz, zst, cab, wim and whatever
// else 7-Zip opens. A split archive is opened from its first part.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/paths.h"
#include "core/strings.h"

namespace anyx360::launcher {

class Job;

// Runs a console program without a window (in `folder`, if given) and passes its output (stdout and
// stderr) to `output` as it arrives. With a `job`, the process is the job's extractor (cancelling
// ends it). Returns the exit code, or -1 if the program could not start.
int RunCaptured(std::wstring command, const std::function<void(const std::string&)>& output, Job* job,
                const fs::path& folder = {});

struct ArchiveEntry {
  std::string path;  // UTF-8, as 7-Zip names it ('\' between folders)
  uint64_t size = 0;
  bool folder = false;
  bool encrypted = false;
};

struct ArchiveListing {
  std::string type;  // 7-Zip's name for the format: "7z", "Rar5", "zip", "gzip", "tar"...
  std::vector<ArchiveEntry> entries;
  uint64_t size = 0;       // the files' uncompressed size
  bool encrypted = false;  // some file's data needs a password
};

enum class ArchiveError {
  kNone,
  kNotArchive,     // 7-Zip doesn't open it
  kNeedsPassword,  // its names are encrypted too: nothing is listed without the password
  kWrongPassword,
  kMissingVolume,  // a part of a split archive isn't there
  kFailed,         // anything else (the exit code says what)
};

// The first part of a split archive (x.part2.rar -> x.part1.rar, x.r00 -> x.rar, x.003 -> x.001,
// x.z01 -> x.zip) when it is there; otherwise `archive`.
fs::path FirstVolume(const fs::path& archive);

// Lists `archive` (7z l -slt). `password`: the one the player gave, if any. On failure, `code` is
// 7-Zip's exit code.
ArchiveError ListArchive(const fs::path& archive, const std::optional<std::wstring>& password, ArchiveListing& listing,
                         int& code);

// Extracts `archive` (only the paths or wildcards `only`, when given) into `out`, reporting its
// progress scaled into [from, to] percent. On failure, `code` is 7-Zip's exit code.
ArchiveError ExtractArchive(Job& job, const fs::path& archive, const std::optional<std::wstring>& password,
                            const fs::path& out, const std::vector<std::string>& only, int from, int to, int& code);

// Writes the 7z `archive` (store only: game data is compressed already, and storing makes it as quick
// to read back as a copy) from the files and empty folders `list` names (UTF-8, one a line, relative
// to `folder`), reporting its percent to `report`. A file that can't be read fails it. On failure,
// `code` is 7-Zip's exit code.
bool CreateArchive(Job& job, const fs::path& archive, const fs::path& folder, const fs::path& list,
                   const std::function<void(int)>& report, int& code);

// "...\4D5308ED\00007000\..." in an archive's file name: the title ID and the path up to the
// content type's folder.
bool ParseContentPath(const std::string& name, const char* type_folder, uint32_t& title_id, std::string& folder);

// What an archive's listing says about a console copy in it.
struct ArchiveInfo {
  bool has_game = false;  // contains default.xex
  // Console HDD content (<TitleID>\00007000\<package> for Games on Demand, <TitleID>\000D0000\
  // for Xbox Live Arcade): its title, and the folders (and files) to extract: the package, the
  // title's downloadable content and title updates (<TitleID>\000B0000\, and the cache's TU_ files),
  // not other titles' demos or content.
  uint32_t content_title = 0;
  bool arcade = false;
  std::vector<std::string> content_folders;
};
ArchiveInfo ClassifyArchive(const ArchiveListing& listing);

}  // namespace anyx360::launcher
