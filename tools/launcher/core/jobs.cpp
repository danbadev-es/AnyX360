#include "core/jobs.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>

#include "core/anyx.h"
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

bool Job::ConfirmReplace(const CatalogGame& game) {
  if (confirmed_ == &game || !IsInstalled(game) || observer_.AskReplace(game)) {
    confirmed_ = &game;
    return true;
  }
  return false;
}

bool Job::AskPassword(const fs::path& archive, bool wrong) {
  if (Cancelled()) return false;
  std::optional<std::wstring> password = observer_.AskPassword(archive, wrong);
  if (!password) return false;
  password_ = std::move(password);
  return true;
}

void Job::SetProcess(void* process) {
  std::lock_guard lock(mutex_);
  process_ = process;
  if (process_ && cancel_) TerminateProcess(process_, 1);  // cancelled while it was starting
}

namespace {

// Archives inside archives (a .tar in a .gz, a disc image in a .rar...): how deep they are looked at.
constexpr int kMaxNesting = 3;

Outcome AddFile(Job& job, const fs::path& file, bool players, int depth, int from);
Outcome AddFolderSource(Job& job, const fs::path& folder, bool players, int depth, int from);
Outcome AddArchiveAt(Job& job, const fs::path& archive, int depth, int from);

Outcome Unsupported(uint32_t title_id) {
  return {{kTextErrorUnsupported, {Hex32(title_id), JoinTitles(AvailableGames())}, true}};
}

// The copy in the player's folder, remembered: it is played where it is.
Outcome UseInPlace(Job& job, fs::path root) {
  Message error;
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
  InstallAvatarPack(root);
  return {{kTextAddedFolder, {game->title, ShortPath(root)}, false}, game->id};
}

// A package found or chosen on its own. `content_source`: where the title's downloadable content
// may be.
Outcome AddPackage(Job& job, const FoundPackage& package, const fs::path& content_source, int from) {
  const uint32_t title = package.header.title_id;
  if (IsGamesOnDemand(package) || IsArcadeGame(package)) {
    const CatalogGame* game = FindAvailableGame(title);
    if (!game) return Unsupported(title);
    if (!job.ConfirmReplace(*game)) return {{kTextCancelled, {}, false}};
    return IsArcadeGame(package) ? AddArcadeGame(job, *game, package, content_source, from)
                                 : AddGamesOnDemand(job, *game, package, content_source, from);
  }
  if (package.header.content_type == x360tool::content_type::kMarketplace) {
    const CatalogGame* game = FindAvailableGame(title);
    if (!game) return {{kTextErrorContentNoGame, {Hex32(title)}, true}};
    // A pack that needs the game's title update isn't taken.
    if (NeedsTitleUpdate(*game, package.path)) return {{kTextErrorContentNoGame, {game->title}, true}};
    if (!IsInstalled(*game)) return {{kTextErrorContentOnly, {game->title}, true}};
    std::vector<fs::path> packages = FindDownloadableContent(content_source, title);
    if (std::find(packages.begin(), packages.end(), package.path) == packages.end()) packages = {package.path};
    std::erase_if(packages, [&](const fs::path& p) { return NeedsTitleUpdate(*game, p); });
    int installed = InstallDownloadableContent(*game, packages);
    return {{kTextAddedContentOnly, {game->title, std::to_wstring(installed)}, false}, game->id};
  }
  if (package.header.content_type == x360tool::content_type::kInstaller) return {{kTextErrorUpdateOnly, {}, true}};
  return {{kTextErrorUnknownFormat, {}, true}};
}

// Where a package's title's other content is: <TitleID>\<type>\<package> keeps it next to the type's
// folder.
fs::path ContentSource(const fs::path& package) {
  fs::path folder = package.parent_path();
  std::wstring name = folder.filename().wstring();
  bool type_folder = name.size() == 8 && std::all_of(name.begin(), name.end(), iswxdigit);
  return type_folder ? folder.parent_path() : folder;
}

Outcome AddFolderSource(Job& job, const fs::path& folder, bool players, int depth, int from) {
  // The disc's files.
  fs::path root = FindGameRoot(folder);
  if (!root.empty()) {
    if (players) return UseInPlace(job, root);
    Message error;
    const CatalogGame* game = IdentifyCopy(root, error);
    if (!game) return {error};
    if (!job.ConfirmReplace(*game)) return {{kTextCancelled, {}, false}};
    job.Found(*game);
    return InstallExtracted(job, *game, root, folder);
  }
  // A Games on Demand or Xbox Live Arcade copy: one of this release's titles, else the first found.
  std::vector<FoundPackage> packages = FindPackages(folder);
  const FoundPackage* chosen = nullptr;
  for (const FoundPackage& package : packages) {
    if (!IsGamesOnDemand(package) && !IsArcadeGame(package)) continue;
    if (!chosen || (!FindAvailableGame(chosen->header.title_id) && FindAvailableGame(package.header.title_id)))
      chosen = &package;
  }
  if (chosen) return AddPackage(job, *chosen, folder, from);
  // A disc image.
  std::vector<fs::path> images = FindFiles(folder, FileKind::kDiscImage, 2);
  if (!images.empty()) return AddDiscImage(job, images.front(), folder, from);
  // An archive: what an archive held, or the only one in the player's folder (they may keep many).
  if (depth < kMaxNesting) {
    std::vector<fs::path> archives = FindFiles(folder, FileKind::kArchive, players ? 1 : 2);
    if (!archives.empty() && (!players || archives.size() == 1)) return AddArchiveAt(job, archives.front(), depth + 1, from);
  }
  // Only content for a game, or updates.
  for (const FoundPackage& package : packages)
    if (package.header.content_type == x360tool::content_type::kMarketplace) return AddPackage(job, package, folder, from);
  for (const FoundPackage& package : packages)
    if (package.header.content_type == x360tool::content_type::kInstaller) return AddPackage(job, package, folder, from);
  return {{players ? kTextErrorNoXexFolder : kTextErrorUnknownFormat, {}, true}};
}

Outcome AddFile(Job& job, const fs::path& file, bool players, int depth, int from) {
  switch (IdentifyFile(file)) {
    case FileKind::kXex:
      return AddFolderSource(job, file.parent_path(), players, depth, from);
    case FileKind::kPackage: {
      auto header = x360tool::read_content_header(file);
      if (!header) return {{kTextErrorUnknownFormat, {}, true}};
      return AddPackage(job, {file, *header}, ContentSource(file), from);
    }
    case FileKind::kDiscImage:
      return AddDiscImage(job, file, {}, from);
    case FileKind::kArchive:
    case FileKind::kOther:  // 7-Zip may still open it
      return AddArchiveAt(job, file, depth, from);
  }
  return {{kTextErrorUnknownFormat, {}, true}};
}

// An extraction's error as a message.
Message ExtractError(const Job& job, ArchiveError error, int code, const fs::path& archive) {
  switch (error) {
    case ArchiveError::kNeedsPassword:
    case ArchiveError::kWrongPassword:
      return {job.Password() ? kTextErrorWrongPassword : kTextErrorPassword, {}, true};
    case ArchiveError::kMissingVolume:
      return {kTextErrorMissingVolume, {archive.filename().wstring()}, true};
    default:
      return {kTextErrorExtract, {std::to_wstring(code)}, true};
  }
}

// Extracts, asking for the password again while it is wrong.
ArchiveError Extract(Job& job, const fs::path& archive, const fs::path& out, const std::vector<std::string>& only,
                     int from, int to, int& code) {
  for (;;) {
    ArchiveError error = ExtractArchive(job, archive, job.Password(), out, only, from, to, code);
    if ((error != ArchiveError::kWrongPassword && error != ArchiveError::kNeedsPassword) || job.Cancelled() ||
        !job.AskPassword(archive, job.Password().has_value()))
      return error;
  }
}

// The folder in WorkDir an archive at `depth` is extracted into.
fs::path ExtractDir(int depth, const char* name) {
  fs::path dir = WorkDir() / (Widen(name) + std::to_wstring(depth));
  RemoveAll(dir);
  return dir;
}

Outcome AddArchiveAt(Job& job, const fs::path& chosen, int depth, int from) {
  if (SevenZip().empty()) return {{kTextMissing7Zip, {}, true}};
  const fs::path archive = FirstVolume(chosen);
  ArchiveListing listing;
  int code = 0;
  ArchiveError error = ListArchive(archive, job.Password(), listing, code);
  while (error == ArchiveError::kNeedsPassword || error == ArchiveError::kWrongPassword) {
    if (!job.AskPassword(archive, error == ArchiveError::kWrongPassword)) return {ExtractError(job, error, code, archive)};
    error = ListArchive(archive, job.Password(), listing, code);
  }
  if (error == ArchiveError::kMissingVolume) return {ExtractError(job, error, code, archive)};
  if (error == ArchiveError::kNotArchive) return {{kTextErrorUnknownFormat, {}, true}};
  if (error != ArchiveError::kNone) return {{kTextErrorList, {std::to_wstring(code)}, true}};
  // Only the files' data is encrypted: the password is asked now, and checked by the first extraction.
  if (listing.encrypted && !job.Password() && !job.AskPassword(archive, false)) return {{kTextErrorPassword, {}, true}};
  // An AnyX360 backup goes back as it was saved, with its downloadable content (a 7z that only has a
  // manifest.json of its own goes on as any other archive).
  if (LooksLikeAnyx(listing)) {
    bool handled = false;
    Outcome outcome = ImportAnyx(job, archive, listing, ExtractDir(depth, "anyx"), from, handled);
    if (handled || job.Cancelled()) return outcome;
  }
  const ArchiveInfo info = ClassifyArchive(listing);
  Message message;
  const int middle = from + (99 - from) * 6 / 10;

  if (info.has_game) {
    // The disc's files: default.xex alone first says which game it is.
    std::vector<std::string> xex;
    for (const ArchiveEntry& e : listing.entries) {
      fs::path name = Widen(e.path);
      if (!e.folder && _wcsicmp(name.filename().c_str(), L"default.xex") == 0) xex.push_back(e.path);
    }
    fs::path probe = ExtractDir(depth, "probe");
    error = Extract(job, archive, probe, xex, from, from, code);
    if (job.Cancelled()) return {};
    if (error != ArchiveError::kNone) return {ExtractError(job, error, code, archive)};
    fs::path probe_root = FindGameRoot(probe);
    if (probe_root.empty()) return {{kTextErrorNotGame, {}, true}};
    const CatalogGame* game = IdentifyCopy(probe_root, message);
    RemoveAll(probe);
    if (!game) return {message};
    if (!job.ConfirmReplace(*game)) return {{kTextCancelled, {}, false}};
    job.Found(*game);
    if (!EnoughSpace(listing.size + (512ull << 20), message)) return {message};
    fs::path partial = ExtractDir(depth, "archive");
    job.Progress(from);
    error = Extract(job, archive, partial, {}, from, 99, code);
    if (job.Cancelled()) return {};
    if (error != ArchiveError::kNone) return {ExtractError(job, error, code, archive)};
    job.Progress(100);
    return InstallExtracted(job, *game, partial, partial);
  }

  if (info.content_title) {
    // Console content: only the title's folders are extracted, then the package is written out.
    const CatalogGame* game = FindAvailableGame(info.content_title);
    if (!game) return Unsupported(info.content_title);
    if (!job.ConfirmReplace(*game)) return {{kTextCancelled, {}, false}};
    job.Found(*game);
    if (!EnoughSpace(listing.size * 2 + (512ull << 20), message)) return {message};
    fs::path partial = ExtractDir(depth, "archive");
    job.Progress(from);
    error = Extract(job, archive, partial, info.content_folders, from, middle, code);
    if (job.Cancelled()) return {};
    if (error != ArchiveError::kNone) return {ExtractError(job, error, code, archive)};
    Outcome outcome = AddFolderSource(job, partial, false, depth, middle);
    // What wasn't extracted: the title updates and other titles' content in the archive.
    int updates = 0, other = 0;
    std::vector<std::string> counted;
    for (const ArchiveEntry& e : listing.entries) {
      if (e.folder) continue;
      for (const char* type : {"000b0000", "00007000", "000d0000", "00000002"}) {
        uint32_t title = 0;
        std::string folder;
        if (!ParseContentPath(e.path, type, title, folder)) continue;
        std::string package = e.path.substr(folder.size());
        package = package.substr(0, package.find_first_of("\\/", 1));
        if (package.size() > 5 && package.compare(package.size() - 5, 5, ".data") == 0) package.resize(package.size() - 5);
        if (std::find(counted.begin(), counted.end(), folder + package) != counted.end()) break;
        counted.push_back(folder + package);
        if (std::string(type) == "000b0000") ++updates;
        else if (title != info.content_title) ++other;
        break;
      }
    }
    if (!outcome.message.error) {
      if (updates) outcome.notes.push_back({kTextNoteUpdatesSkipped, {std::to_wstring(updates)}, false});
      if (other) outcome.notes.push_back({kTextNoteOtherContent, {std::to_wstring(other)}, false});
    }
    return outcome;
  }

  // Anything else (a disc image, a package, another archive, the .tar in a .gz): all of it is
  // extracted and looked at.
  if (depth >= kMaxNesting) return {{kTextErrorUnknownFormat, {}, true}};
  if (!EnoughSpace(listing.size * 2 + (512ull << 20), message)) return {message};
  fs::path partial = ExtractDir(depth, "archive");
  job.Progress(from);
  error = Extract(job, archive, partial, {}, from, middle, code);
  if (job.Cancelled()) return {};
  if (error != ArchiveError::kNone) return {ExtractError(job, error, code, archive)};
  return AddFolderSource(job, partial, false, depth, middle);
}

}  // namespace

Outcome AddGame(Job& job, const fs::path& source) {
  job.Report(Step::kChecking);
  RemoveAll(WorkDir());
  std::error_code ec;
  Outcome outcome = fs::is_directory(source, ec) ? AddFolderSource(job, source, true, 0, 0)
                                                 : AddFile(job, source, true, 0, 0);
  RemoveAll(WorkDir());
  if (job.Cancelled() && (outcome.message.empty() || outcome.message.error)) return {};
  return outcome;
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
