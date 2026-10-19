// Archives through 7-Zip (bin\7z.exe in a release, else an installed one), run without a window.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/paths.h"
#include "core/strings.h"

namespace anyx360::launcher {

class Job;

// Runs a console program without a window and passes its output (stdout and stderr) to `output`
// as it arrives. With a `job`, the process is the job's extractor (cancelling ends it). Returns the
// exit code, or -1 if the program could not start.
int RunCaptured(std::wstring command, const std::function<void(const std::string&)>& output, Job* job);

struct ArchiveInfo {
  uint64_t size = 0;      // uncompressed
  bool has_game = false;  // contains default.xex
  // A Games on Demand copy (<TitleID>\00007000\<package>): its title, and the folders to extract
  // (the package and the title's downloadable content, not other titles' demos or updates).
  uint32_t god_title = 0;
  std::vector<std::string> god_folders;
};

// "...\4D5308ED\00007000\..." in an archive's file name: the title ID and the path up to the
// content type's folder.
bool ParseContentPath(const std::string& name, const char* type_folder, uint32_t& title_id, std::string& folder);

bool ListArchive(const fs::path& archive, ArchiveInfo& info, Message& error);

// Extracts `archive` (only the folders `only`, when given) into `out`, reporting its progress
// scaled into [from, to] percent. Returns 7-Zip's exit code.
int ExtractArchive(Job& job, const fs::path& archive, const fs::path& out, const std::vector<std::string>& only,
                   int from, int to);

}  // namespace anyx360::launcher
