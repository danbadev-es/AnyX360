#include "core/jobs.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "core/catalog.h"
#include "core/install.h"
#include "core/library.h"
#include "core/probe.h"
#include "core/sevenzip.h"

namespace anyx360::launcher {

void Job::Cancel() {
  cancel_ = true;
  std::lock_guard lock(mutex_);
  if (process_) TerminateProcess(process_, 1);
}

bool Job::ConfirmReplace(const CatalogGame& game) { return !IsInstalled(game) || observer_.AskReplace(game); }

void Job::SetProcess(void* process) {
  std::lock_guard lock(mutex_);
  process_ = process;
  if (process_ && cancel_) TerminateProcess(process_, 1);  // cancelled while it was starting
}

namespace {

// A Games on Demand archive. Its title comes from the listing; only that title's folders are
// extracted (into games\.partial), then the package is converted.
Outcome AddGamesOnDemandArchive(Job& job, const fs::path& archive, const ArchiveInfo& info) {
  Message error;
  const CatalogGame* game = FindAvailableGame(info.god_title);
  if (!game) return {{kTextErrorUnsupported, {Hex32(info.god_title), JoinTitles(AvailableGames())}, true}};
  if (!job.ConfirmReplace(*game)) return {{kTextCancelled, {}, false}};
  job.Found(*game);
  // The extracted package and the game's files written out from it.
  if (!EnoughSpace(info.size * 2 + (512ull << 20), error)) return {error};
  fs::path partial = GamesDir() / L".partial";
  RemoveAll(partial);
  job.Progress(0);
  int code = ExtractArchive(job, archive, partial, info.god_folders, 0, 60);
  if (job.Cancelled() || code != 0) {
    RemoveAll(partial);
    return job.Cancelled() ? Outcome{} : Outcome{{kTextErrorExtract, {std::to_wstring(code)}, true}};
  }
  x360tool::ContentPackage package;
  fs::path header = FindGamesOnDemand(partial, &package);
  if (header.empty()) {
    RemoveAll(partial);
    return {{kTextErrorNoXex, {}, true}};
  }
  Outcome outcome = AddGamesOnDemand(job, partial, header, package, 60);
  RemoveAll(partial);
  return job.Cancelled() ? Outcome{} : outcome;
}

}  // namespace

Outcome AddArchive(Job& job, const fs::path& archive) {
  job.Report(Step::kChecking);
  ArchiveInfo info;
  Message error;
  if (!ListArchive(archive, info, error)) return {error};
  if (!info.has_game && info.god_title) return AddGamesOnDemandArchive(job, archive, info);
  if (!info.has_game) return {{kTextErrorNotGame, {}, true}};

  fs::path probe = GamesDir() / L".probe";
  RemoveAll(probe);
  int code = RunCaptured(Quote(SevenZip()) + L" x -y -r -bso0 -bse1 -sccUTF-8 -o" + Quote(probe) + L" " +
                             Quote(archive) + L" default.xex",
                         [](const std::string&) {}, &job);
  if (job.Cancelled()) {
    RemoveAll(probe);
    return {};
  }
  fs::path probe_root = code == 0 ? FindGameRoot(probe) : fs::path();
  if (probe_root.empty()) {
    RemoveAll(probe);
    return {code == 0 ? Message{kTextErrorNotGame, {}, true} : Message{kTextErrorExtract, {std::to_wstring(code)}, true}};
  }
  const CatalogGame* game = IdentifyCopy(probe_root, error);
  RemoveAll(probe);
  if (!game) return {error};
  if (!job.ConfirmReplace(*game)) return {{kTextCancelled, {}, false}};
  job.Found(*game);

  if (!EnoughSpace(info.size + (512ull << 20), error)) return {error};

  fs::path partial = GamesDir() / L".partial";
  RemoveAll(partial);  // a previous attempt
  job.Progress(0);
  code = ExtractArchive(job, archive, partial, {}, 0, 100);
  if (job.Cancelled()) {
    RemoveAll(partial);
    return {};
  }
  if (code != 0) {
    RemoveAll(partial);
    return {{kTextErrorExtract, {std::to_wstring(code)}, true}};
  }
  job.Progress(100);

  fs::path found = FindGameRoot(partial);
  if (found.empty()) {
    RemoveAll(partial);
    return {{kTextErrorNoXex, {}, true}};
  }
  if (IdentifyCopy(found, error) != game || !CheckCopy(found, *game, error) || !InstallCopy(*game, found, error)) {
    RemoveAll(partial);
    return {error};
  }
  RemoveAll(partial);
  SaveIcon(*game, ExtractedDir(*game));
  return {{kTextAdded, {game->title}, false}, game->id};
}

Outcome AddFolder(Job& job, const fs::path& folder) {
  job.Report(Step::kChecking);
  fs::path root = FindGameRoot(folder);
  Message error;
  if (root.empty()) {
    x360tool::ContentPackage package;
    fs::path header = FindGamesOnDemand(folder, &package);
    if (header.empty()) return {{kTextErrorNoXexFolder, {}, true}};
    const CatalogGame* game = FindAvailableGame(package.title_id);
    if (game && !job.ConfirmReplace(*game)) return {{kTextCancelled, {}, false}};
    std::error_code ec;
    if (!EnoughSpace(fs::file_size(header, ec) + uint64_t(package.data_file_count) * 0xA290000, error)) return {error};
    job.Progress(0);
    Outcome outcome = AddGamesOnDemand(job, folder, header, package, 0);
    return job.Cancelled() ? Outcome{} : outcome;
  }
  const CatalogGame* game = IdentifyCopy(root, error);
  if (!game) return {error};
  job.Found(*game);
  if (!CheckCopy(root, *game, error)) return {error};
  std::error_code ec;
  root = fs::absolute(root, ec);
  auto relative = root.lexically_relative(fs::absolute(ExtractedDir(*game), ec));
  if (!relative.empty() && *relative.begin() != L"..") {  // the extracted copy itself
    fs::remove(LocationFile(*game), ec);
    return {{kTextAdded, {game->title}, false}, game->id};
  }
  if (!job.ConfirmReplace(*game)) return {{kTextCancelled, {}, false}};
  job.Report(Step::kFinishing, 100);
  RemoveFolder(ExtractedDir(*game));
  fs::create_directories(GameRoot(*game), ec);
  if (!WriteUtf8(LocationFile(*game), root.wstring())) return {{kTextErrorMove, {L"location.txt"}, true}};
  SaveIcon(*game, root);
  return {{kTextAddedFolder, {game->title, ShortPath(root)}, false}, game->id};
}

Outcome RemoveGame(Job& job, const CatalogGame& game) {
  job.Found(game);
  job.Report(Step::kRemoving);
  RemoveFolder(GameRoot(game));
  return {{kTextRemoved, {game.title}, false}};
}

void Worker::Start(JobObserver& observer, std::function<Outcome(Job&)> work) {
  Join();
  job_ = std::make_unique<Job>(observer);
  std::error_code ec;
  fs::create_directories(GamesDir(), ec);
  thread_ = std::thread([job = job_.get(), &observer, work = std::move(work)] {
    Outcome outcome = work(*job);
    if (job->Cancelled() && outcome.message.empty()) outcome.message = {kTextCancelled, {}, false};
    observer.OnDone(outcome);
  });
}

void Worker::Cancel() {
  if (job_) job_->Cancel();
}

void Worker::Join() {
  if (thread_.joinable()) thread_.join();
}

}  // namespace anyx360::launcher
