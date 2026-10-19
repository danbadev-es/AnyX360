// Where the launcher, the games and the player's things are.
//
//   AnyX360.exe                 the launcher
//   bin\<game>.exe (+ .pdb)     each recompiled game (the .pdb names functions in crash reports)
//   bin\7z.exe, bin\7z.dll      7-Zip, to extract archives
//   bin\shader_cache\<id>\      Xenia shader storage, copied once to the player's cache
//   keys\xex2_retail.key
//   games\<id>\game\            a copy extracted from an archive (the folder holding default.xex)
//   games\<id>\location.txt     or the folder of the player's copy, used where it is (UTF-8)
//   games\<id>\icon.png         the game's dashboard icon, read from default.xex when it is added
//   games\<id>\cover.png|.jpg   a cover the player chose instead
//   logs\<id>\                  last_run.log (the game's output) and previous_run.log
//
// Run from the build folder, the games are found where the build puts them.
#pragma once

#include <filesystem>
#include <string>

struct CatalogGame;

namespace anyx360::launcher {

namespace fs = std::filesystem;

// The launcher's folder (tests use a sandbox instead).
fs::path Root();
void SetRootForTesting(const fs::path& root);

bool Exists(const fs::path& path);

fs::path GamesDir();
fs::path GameRoot(const CatalogGame& game);
fs::path ExtractedDir(const CatalogGame& game);
fs::path LocationFile(const CatalogGame& game);
fs::path IconFile(const CatalogGame& game);
fs::path LogDir(const CatalogGame& game);
// The player's cover for the game, if they chose one.
fs::path CoverFile(const CatalogGame& game);

// `relative` in `start` or the closest folder above it that has it (the build folder is inside the
// repository, next to keys\).
fs::path FindUpwards(fs::path start, const fs::path& relative);
fs::path KeyFile();

// A one-line UTF-8 text file (paths may have any characters).
std::wstring ReadUtf8(const fs::path& path);
bool WriteUtf8(const fs::path& path, const std::wstring& text);

// The folder of a copy added in place, or empty.
fs::path StoredLocation(const CatalogGame& game);

// bin\ in a release, next to the launcher otherwise.
fs::path FindProgram(const wchar_t* name);
// The game's executable: the release's, else (running from the build folder) the build's.
fs::path FindGameExe(const CatalogGame& game);
// The bundled 7-Zip, else an installed one, else empty.
fs::path SevenZip();

// %LOCALAPPDATA%, as the game resolves it.
fs::path LocalAppData();
// The launcher's settings (the games keep theirs, and their saves, in their own folders there).
fs::path SettingsFile(const wchar_t* name);
std::wstring ReadSetting(const fs::path& path);
void WriteSetting(const fs::path& path, const std::wstring& value);

std::wstring Quote(const fs::path& path);

}  // namespace anyx360::launcher
