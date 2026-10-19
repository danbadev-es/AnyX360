#include "core/install.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>

#include "core/catalog.h"
#include "core/library.h"
#include "core/probe.h"
#include "stfs.h"
#include "svod.h"
#include "xdvdfs.h"

namespace anyx360::launcher {

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

bool ExtractGamesOnDemand(Job& job, const fs::path& header, const fs::path& out, int from, int to, Message& error) {
  try {
    auto image = x360tool::open_games_on_demand(header);
    std::vector<x360tool::XdvdfsEntry> files;
    uint64_t total = 0;
    image->walk([&](const x360tool::XdvdfsEntry& e) {
      if (!e.is_dir) files.push_back(e), total += e.size;
    });
    uint64_t done = 0;
    int last = -1;
    for (const x360tool::XdvdfsEntry& e : files) {
      if (job.Cancelled()) return false;
      image->extract(e, out / fs::path(Widen(e.path)));
      done += e.size;
      int percent = from + int(total ? done * uint64_t(to - from) / total : uint64_t(to - from));
      if (percent > last) {
        last = percent;
        job.Progress(std::min(percent, 99));
      }
    }
    return true;
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
        if (!e.is_dir) package.extract(e, out / fs::path(Widen(e.path)));
      ++installed;
    } catch (const std::exception&) {
      // A damaged pack is skipped: the game runs without it.
    }
  }
  return installed;
}

Outcome AddGamesOnDemand(Job& job, const fs::path& source, const fs::path& header,
                         const x360tool::ContentPackage& package, int from) {
  Message error;
  const CatalogGame* game = FindAvailableGame(package.title_id);
  if (!game) return {{kTextErrorUnsupported, {Hex32(package.title_id), JoinTitles(AvailableGames())}, true}};
  job.Found(*game);
  fs::path converted = GamesDir() / L".converted";
  RemoveAll(converted);
  if (!ExtractGamesOnDemand(job, header, converted, from, 99, error) || job.Cancelled()) {
    RemoveAll(converted);
    return {error};
  }
  fs::path found = FindGameRoot(converted);
  if (found.empty()) {
    RemoveAll(converted);
    return {{kTextErrorNoXex, {}, true}};
  }
  if (IdentifyCopy(found, error) != game || !CheckCopy(found, *game, error) || !InstallCopy(*game, found, error)) {
    RemoveAll(converted);
    return {error};
  }
  RemoveAll(converted);
  job.Progress(100);
  SaveIcon(*game, ExtractedDir(*game));
  int content = InstallDownloadableContent(*game, FindDownloadableContent(source, package.title_id));
  if (content) return {{kTextAddedContent, {game->title, std::to_wstring(content)}, false}, game->id};
  return {{kTextAdded, {game->title}, false}, game->id};
}

}  // namespace anyx360::launcher
