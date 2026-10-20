#include "core/install.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <optional>

#include "avatar.h"
#include "core/catalog.h"
#include "core/library.h"
#include "stfs.h"
#include "svod.h"
#include "xdvdfs.h"

namespace anyx360::launcher {

fs::path WorkDir() { return GamesDir() / L".work"; }

void RemoveAll(const fs::path& path) {
  std::error_code ec;
  fs::remove_all(path, ec);
}

void RemoveFolder(const fs::path& path) {
  if (!Exists(path)) return;
  std::error_code ec;
  fs::path old = GamesDir() / (L".old-" + path.filename().wstring());
  RemoveAll(old);
  fs::rename(path, old, ec);
  RemoveAll(ec ? path : old);
}

bool EnoughSpace(uint64_t needed, Message& error) {
  ULARGE_INTEGER free_bytes{};
  if (GetDiskFreeSpaceExW(Root().c_str(), &free_bytes, nullptr, nullptr) && free_bytes.QuadPart < needed) {
    error = {kTextErrorSpace, {Gigabytes(needed), Gigabytes(free_bytes.QuadPart)}, true};
    return false;
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

int InstallDownloadableContent(const CatalogGame& game, const std::vector<fs::path>& packages) {
  int installed = 0;
  for (const fs::path& path : packages) {
    try {
      x360tool::StfsPackage package(path);
      fs::path out = LocalAppData() / L"AnyX360" / game.user_dir / L"content" / L"00000002" / path.filename();
      RemoveAll(out);
      for (const x360tool::StfsEntry& e : package.entries())
        if (!e.is_dir) package.extract(e, out / x360tool::utf8_path(e.path));
      ++installed;
    } catch (const std::exception&) {
      // A damaged pack is skipped: the game runs without it.
    }
  }
  return installed;
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
  if (content_source.empty()) return outcome;
  std::vector<fs::path> content;
  int updates = 0, other = 0;
  for (const FoundPackage& package : FindPackages(content_source)) {
    if (IsDownloadableContent(package, game.title_id)) {
      content.push_back(package.path);
    } else if (package.header.content_type == x360tool::content_type::kInstaller) {
      ++updates;
    } else if (package.header.title_id != game.title_id) {
      ++other;
    }
  }
  if (int installed = InstallDownloadableContent(game, content))
    outcome.message = {kTextAddedContent, {game.title, std::to_wstring(installed)}, false};
  if (updates) outcome.notes.push_back({kTextNoteUpdatesSkipped, {std::to_wstring(updates)}, false});
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
