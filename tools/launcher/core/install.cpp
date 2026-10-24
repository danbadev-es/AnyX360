#include "core/install.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "avatar.h"
#include "core/catalog.h"
#include "core/library.h"
#include "stfs.h"
#include "svod.h"
#include "xdvdfs.h"
#include "xex.h"

namespace anyx360::launcher {

fs::path WorkDir() { return GamesDir() / L".work"; }

bool IsLink(const fs::path& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

void RemoveAll(const fs::path& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) return;
  if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    // Only the link itself goes.
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(path.c_str());
    else DeleteFileW(path.c_str());
    return;
  }
  std::error_code ec;
  if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
    std::vector<fs::path> children;
    for (auto it = fs::directory_iterator(path, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
      children.push_back(it->path());
    for (const fs::path& child : children) RemoveAll(child);
  }
  fs::remove(path, ec);
}

void RemoveFolder(const fs::path& path) {
  if (!Exists(path)) return;
  std::error_code ec;
  fs::path old = GamesDir() / (L".old-" + path.filename().wstring());
  RemoveAll(old);
  fs::rename(path, old, ec);
  RemoveAll(ec ? path : old);
}

bool MoveFolder(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  fs::rename(from, to, ec);
  if (!ec) return true;
  ec.clear();
  fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  if (ec) {
    RemoveAll(to);
    return false;
  }
  RemoveAll(from);
  return true;
}

bool EnoughSpace(uint64_t needed, Message& error) {
  ULARGE_INTEGER free_bytes{};
  if (GetDiskFreeSpaceExW(Root().c_str(), &free_bytes, nullptr, nullptr) && free_bytes.QuadPart < needed) {
    error = {kTextErrorSpace, {Gigabytes(needed), Gigabytes(free_bytes.QuadPart)}, true};
    return false;
  }
  return true;
}

bool EnoughSpaceFor(const std::vector<std::pair<fs::path, uint64_t>>& needs, Message& error) {
  // The drive (volume mount point) of each place, by the closest folder that exists.
  std::vector<std::pair<std::wstring, uint64_t>> volumes;
  for (const auto& [place, bytes] : needs) {
    std::error_code ec;
    fs::path existing = fs::absolute(place, ec);
    while (!existing.empty() && !Exists(existing) && existing.has_parent_path() && existing.parent_path() != existing)
      existing = existing.parent_path();
    wchar_t volume[MAX_PATH] = {};
    if (!GetVolumePathNameW(existing.c_str(), volume, MAX_PATH)) continue;
    auto same = std::find_if(volumes.begin(), volumes.end(), [&](const auto& v) { return _wcsicmp(v.first.c_str(), volume) == 0; });
    if (same == volumes.end()) volumes.push_back({volume, bytes});
    else same->second += bytes;
  }
  for (const auto& [volume, bytes] : volumes) {
    ULARGE_INTEGER free_bytes{};
    if (GetDiskFreeSpaceExW(volume.c_str(), &free_bytes, nullptr, nullptr) && free_bytes.QuadPart < bytes) {
      error = {kTextErrorSpaceOn, {Gigabytes(bytes), Gigabytes(free_bytes.QuadPart), volume}, true};
      return false;
    }
  }
  return true;
}

bool InstallCopy(const CatalogGame& game, const fs::path& found, Message& error) {
  RemoveFolder(ExtractedDir(game));
  std::error_code ec;
  fs::remove(LocationFile(game), ec);
  fs::create_directories(GameRoot(game), ec);
  fs::rename(found, ExtractedDir(game), ec);
  if (ec) {
    error = {kTextErrorMove, {Widen(ec.message())}, true};
    return false;
  }
  return true;
}

bool ExtractVolume(Job& job, x360tool::XdvdfsImage& image, const fs::path& out, int from, int to, Message& error) {
  try {
    std::vector<x360tool::XdvdfsEntry> files;
    uint64_t total = 0;
    image.walk([&](const x360tool::XdvdfsEntry& e) {
      if (!e.is_dir) files.push_back(e), total += e.size;
    });
    uint64_t done = 0;
    int last = -1;
    auto report = [&](uint64_t bytes) {
      int percent = from + int(total ? bytes * uint64_t(to - from) / total : uint64_t(to - from));
      if (percent > last) job.Progress(std::min(last = percent, 99));
      return !job.Cancelled();
    };
    for (const x360tool::XdvdfsEntry& e : files) {
      if (!image.extract(e, out / x360tool::utf8_path(e.path), [&](uint64_t bytes) { return report(done + bytes); }))
        return false;
      done += e.size;
    }
    return !job.Cancelled();
  } catch (const std::exception& e) {
    error = {kTextErrorConvert, {Widen(e.what())}, true};
    return false;
  }
}

bool NeedsTitleUpdate(const CatalogGame& game, const fs::path& package) {
  if (UsesTitleUpdate(game)) return false;  // played with it: its packs are too
  if (!game.content) return true;
  const std::wstring name = package.filename().wstring();
  const std::wstring_view list = game.content_needs_update ? game.content_needs_update : L"";
  for (size_t start = 0; start < list.size();) {
    size_t end = list.find(L'|', start);
    if (end == std::wstring_view::npos) end = list.size();
    const std::wstring_view entry = list.substr(start, end - start);
    if (entry.size() == name.size() &&
        std::equal(entry.begin(), entry.end(), name.begin(), [](wchar_t a, wchar_t b) { return towupper(a) == towupper(b); }))
      return true;
    start = end + 1;
  }
  return false;
}

fs::path DownloadableContentDir(const CatalogGame& game) {
  return LocalAppData() / L"AnyX360" / game.user_dir / L"content" / L"00000002";
}

int InstallDownloadableContent(const CatalogGame& game, const std::vector<fs::path>& packages) {
  int installed = 0;
  for (const fs::path& path : packages) {
    const fs::path out = DownloadableContentDir(game) / path.filename();
    try {
      x360tool::StfsPackage package(path);
      RemoveAll(out);
      for (const x360tool::StfsEntry& e : package.entries())
        if (!e.is_dir) package.extract(e, out / x360tool::utf8_path(e.path));
      ++installed;
    } catch (const std::exception&) {
      // A damaged pack is skipped: the game runs without it (and no half of it stays).
      RemoveAll(out);
    }
  }
  return installed;
}

fs::path TitleUpdateDir(const CatalogGame& game) {
  return LocalAppData() / L"AnyX360" / game.user_dir / L"content" / L"000B0000" / game.update_package;
}

bool UsesTitleUpdate(const CatalogGame& game) { return game.update_package && *game.update_package; }

bool TitleUpdateInstalled(const CatalogGame& game) {
  return !UsesTitleUpdate(game) || Exists(TitleUpdateDir(game) / L"default.xexp");
}

uint32_t TitleUpdateVersion(const fs::path& folder) {
  // The headers are enough: the descriptor is one of them (the patch's data can be large).
  std::ifstream in(folder / L"default.xexp", std::ios::binary);
  std::vector<uint8_t> header(0x18);
  if (!in.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size()))) return 0;
  const uint32_t size = uint32_t(header[8]) << 24 | uint32_t(header[9]) << 16 | uint32_t(header[10]) << 8 | header[11];
  if (size < 0x18 || size > (16u << 20)) return 0;
  header.resize(size);
  if (!in.read(reinterpret_cast<char*>(header.data()) + 0x18, std::streamsize(size - 0x18))) return 0;
  try {
    const auto patch = x360tool::Xex(std::move(header)).delta_patch();
    return patch ? patch->target_version.raw : 0;
  } catch (const std::exception&) {
    return 0;
  }
}

UpdateResult InstallTitleUpdate(const CatalogGame& game, const fs::path& package, uint32_t& version) {
  version = 0;
  const fs::path destination = TitleUpdateDir(game);
  // Extracted beside it first: an update already installed stays until this one is checked.
  fs::path staged = destination;
  staged += L".new";
  RemoveAll(staged);
  try {
    x360tool::StfsPackage stfs(package);
    for (const x360tool::StfsEntry& e : stfs.entries())
      if (!e.is_dir) stfs.extract(e, staged / x360tool::utf8_path(e.path));
  } catch (const std::exception&) {
    RemoveAll(staged);
    return UpdateResult::kDamaged;
  }
  version = TitleUpdateVersion(staged);
  uint32_t expected = 0;
  ParseXexVersion(game.update_version, expected);
  if (!version || version != expected) {
    RemoveAll(staged);
    return version ? UpdateResult::kOtherVersion : UpdateResult::kDamaged;
  }
  RemoveAll(destination);
  std::error_code ec;
  fs::rename(staged, destination, ec);
  if (ec) {
    RemoveAll(staged);
    return UpdateResult::kDamaged;
  }
  return UpdateResult::kInstalled;
}

void InstallTitleUpdates(const CatalogGame& game, const std::vector<fs::path>& packages, Outcome& outcome) {
  if (!UsesTitleUpdate(game)) {
    if (!packages.empty()) outcome.notes.push_back({kTextNoteUpdatesSkipped, {std::to_wstring(packages.size())}, false});
    return;
  }
  bool installed = false, damaged = false;
  std::vector<uint32_t> others;
  for (const fs::path& package : packages) {
    uint32_t version = 0;
    switch (InstallTitleUpdate(game, package, version)) {
      case UpdateResult::kInstalled:  // the same update in two forms (TU_ and 000B0000) is installed twice
        installed = true;
        break;
      case UpdateResult::kOtherVersion:
        if (std::find(others.begin(), others.end(), version) == others.end()) others.push_back(version);
        break;
      case UpdateResult::kDamaged:
        damaged = true;
        break;
    }
  }
  if (installed) outcome.notes.push_back({kTextNoteUpdateAdded, {game.update_version}, false});
  for (uint32_t version : others)
    outcome.notes.push_back({kTextNoteUpdateOther, {XexVersionText(version), game.update_version}, false});
  if (damaged && !installed) outcome.notes.push_back({kTextErrorUpdateDamaged, {}, false});
  if (!TitleUpdateInstalled(game)) outcome.notes.push_back({kTextNeedsUpdate, {game.title, game.update_version}, false});
}

void InstallAvatarPack(const fs::path& game_root) {
  const fs::path package = game_root / x360tool::kAvatarPackPackage;
  if (!Exists(package)) return;
  std::string error;
  // A damaged pack is skipped: avatar games then run without avatars.
  x360tool::extract_avatar_pack(package, LocalAppData() / L"AnyX360" / L"system" / x360tool::kAvatarPackToc, error);
}

Outcome InstallExtracted(Job& job, const CatalogGame& game, const fs::path& extracted, const fs::path& content_source) {
  Message error;
  fs::path found = FindGameRoot(extracted);
  if (found.empty()) return {{kTextErrorNoXex, {}, true}};
  const CatalogGame* identified = IdentifyCopy(found, error);
  if (identified != &game) return {identified ? Message{kTextErrorNotGame, {}, true} : error};
  if (!CheckCopy(found, game, error) || !InstallCopy(game, found, error)) return {error};
  job.Progress(100);
  SaveIcon(game, ExtractedDir(game));
  InstallAvatarPack(ExtractedDir(game));
  Outcome outcome{{kTextAdded, {game.title}, false}, game.id};
  std::vector<fs::path> content, updates;
  int other = 0, needs_update = 0;
  if (!content_source.empty()) {
    for (const FoundPackage& package : FindPackages(content_source)) {
      if (IsDownloadableContent(package, game.title_id)) {
        // Packs that need the title update (which isn't played) are left out.
        if (NeedsTitleUpdate(game, package.path)) ++needs_update;
        else content.push_back(package.path);
      } else if (IsTitleUpdate(package, game.title_id)) {
        updates.push_back(package.path);
      } else if (package.header.title_id != game.title_id) {
        ++other;
      }
    }
  }
  if (int installed = InstallDownloadableContent(game, content))
    outcome.message = {kTextAddedContent, {game.title, std::to_wstring(installed)}, false};
  if (needs_update) outcome.notes.push_back({kTextNoteContentSkipped, {std::to_wstring(needs_update)}, false});
  InstallTitleUpdates(game, updates, outcome);
  if (other) outcome.notes.push_back({kTextNoteOtherContent, {std::to_wstring(other)}, false});
  return outcome;
}

namespace {

// A fresh folder in WorkDir for a conversion's output.
fs::path ConvertedDir() {
  fs::path converted = WorkDir() / L"converted";
  for (int n = 2; Exists(converted); ++n) converted = WorkDir() / (L"converted" + std::to_wstring(n));
  return converted;
}

}  // namespace

Outcome AddGamesOnDemand(Job& job, const CatalogGame& game, const FoundPackage& package, const fs::path& content_source,
                         int from) {
  Message error;
  job.Found(game);
  if (!EnoughSpace(uint64_t(package.header.data_file_count) * 0xA290000 + (512ull << 20), error)) return {error};
  fs::path converted = ConvertedDir();
  try {
    auto image = x360tool::open_games_on_demand(package.path);
    if (!ExtractVolume(job, *image, converted, from, 99, error)) return {error};
  } catch (const std::exception& e) {
    return {{kTextErrorConvert, {Widen(e.what())}, true}};
  }
  return InstallExtracted(job, game, converted, content_source);
}

Outcome AddArcadeGame(Job& job, const CatalogGame& game, const FoundPackage& package, const fs::path& content_source,
                      int from) {
  Message error;
  job.Found(game);
  std::error_code ec;
  if (!EnoughSpace(fs::file_size(package.path, ec) + (512ull << 20), error)) return {error};
  fs::path converted = ConvertedDir();
  try {
    x360tool::StfsPackage stfs(package.path);
    uint64_t total = 0, done = 0;
    for (const x360tool::StfsEntry& e : stfs.entries())
      if (!e.is_dir) total += e.size;
    int last = -1;
    for (const x360tool::StfsEntry& e : stfs.entries()) {
      if (e.is_dir) continue;
      bool finished = stfs.extract(e, converted / x360tool::utf8_path(e.path), [&](uint64_t bytes) {
        int percent = from + int(total ? (done + bytes) * uint64_t(99 - from) / total : 0);
        if (percent > last) job.Progress(std::min(last = percent, 99));
        return !job.Cancelled();
      });
      if (!finished) return {};
      done += e.size;
    }
  } catch (const std::exception& e) {
    return {{kTextErrorConvert, {Widen(e.what())}, true}};
  }
  return InstallExtracted(job, game, converted, content_source);
}

Outcome AddDiscImage(Job& job, const fs::path& image_path, const fs::path& content_source, int from) {
  try {
    x360tool::XdvdfsImage image(image_path);
    std::optional<x360tool::XdvdfsEntry> xex;
    uint64_t total = 0;
    image.walk([&](const x360tool::XdvdfsEntry& e) {
      if (e.is_dir) return;
      total += e.size;
      if (_wcsicmp(x360tool::utf8_path(e.path).c_str(), L"default.xex") == 0) xex = e;
    });
    if (!xex) return {{kTextErrorNotGame, {}, true}};
    fs::path probe = WorkDir() / L"probe";
    RemoveAll(probe);
    image.extract(*xex, probe / L"default.xex");
    Message error;
    const CatalogGame* game = IdentifyCopy(probe, error);
    RemoveAll(probe);
    if (!game) return {error};
    if (!job.ConfirmReplace(*game)) return {{kTextCancelled, {}, false}};
    job.Found(*game);
    if (!EnoughSpace(total + (512ull << 20), error)) return {error};
    fs::path converted = ConvertedDir();
    job.Progress(from);
    if (!ExtractVolume(job, image, converted, from, 99, error)) return {error};
    return InstallExtracted(job, *game, converted, content_source);
  } catch (const std::exception& e) {
    return {{kTextErrorConvert, {Widen(e.what())}, true}};
  }
}

}  // namespace anyx360::launcher
