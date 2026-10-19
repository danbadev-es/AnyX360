// The launcher's logic (tools/launcher/core) in a sandbox: a release folder with stand-in game
// executables, its own %LOCALAPPDATA%, and synthetic copies (a default.xex header with the title
// ID, the game's required files). Archives use 7-Zip when it is installed.
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "core/catalog.h"
#include "core/jobs.h"
#include "core/library.h"
#include "core/sevenzip.h"
#include "core/strings.h"
#include "core/terms.h"

using namespace anyx360::launcher;

static int failures = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    if (cond) {                                                     \
      std::printf("PASS %s\n", #cond);                              \
    } else {                                                        \
      std::printf("FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++failures;                                                   \
    }                                                               \
  } while (0)

namespace {

void WriteFile(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << bytes;
}

// A default.xex header: 'XEX2', one optional header (execution info, 0x00040006) whose record holds
// the title ID at +0xC.
void WriteXex(const fs::path& path, uint32_t title_id) {
  std::string x(0x200, '\0');
  auto put = [&](size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) x[at + i] = char(v >> (24 - 8 * i));
  };
  put(0, 0x58455832);
  put(0x14, 1);
  put(0x18, 0x00040006);
  put(0x1C, 0x100);
  put(0x10C, title_id);
  WriteFile(path, x);
}

// A complete copy of `game` in `folder`.
void WriteCopy(const fs::path& folder, const CatalogGame& game) {
  WriteXex(folder / L"default.xex", game.title_id);
  std::wstring files = game.required_files;
  for (size_t start = 0; start < files.size();) {
    size_t end = files.find(L'|', start);
    if (end == std::wstring::npos) end = files.size();
    if (end > start) WriteFile(folder / files.substr(start, end - start), "data");
    start = end + 1;
  }
  WriteFile(folder / L"media" / L"level.bin", std::string(4096, 'x'));
}

struct Observer : JobObserver {
  std::mutex mutex;
  std::vector<std::pair<Step, int>> progress;
  const CatalogGame* game = nullptr;
  int asked = 0;
  bool replace = true;
  std::promise<Outcome> done;

  void OnProgress(Step step, int percent) override {
    std::lock_guard lock(mutex);
    progress.push_back({step, percent});
  }
  void OnGame(const CatalogGame& found) override { game = &found; }
  bool AskReplace(const CatalogGame&) override {
    ++asked;
    return replace;
  }
  void OnDone(const Outcome& outcome) override { done.set_value(outcome); }
  bool Saw(Step step) {
    std::lock_guard lock(mutex);
    for (const auto& [s, p] : progress)
      if (s == step) return true;
    return false;
  }
};

// Runs a job on this thread.
Outcome Run(Observer& observer, const std::function<Outcome(Job&)>& work) {
  Job job(observer);
  return work(job);
}

int Run7z(const std::wstring& arguments) {
  return RunCaptured(Quote(SevenZip()) + L" " + arguments, [](const std::string&) {}, nullptr);
}

}  // namespace

int main() {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  const fs::path sandbox = fs::path(temp) / (L"anyx360_launcher_core_" + std::to_wstring(GetCurrentProcessId()));
  fs::remove_all(sandbox);
  const fs::path release = sandbox / L"release", player = sandbox / L"player";
  fs::create_directories(release / L"bin");
  SetEnvironmentVariableW(L"LOCALAPPDATA", (sandbox / L"local").c_str());
  const fs::path seven_zip = SevenZip();  // before the root moves: the build's or an installed one
  SetRootForTesting(release);
  for (const CatalogGame& game : kCatalog) WriteFile(release / L"bin" / game.exe, "stand-in");
  const CatalogGame& game = kCatalog[0];

  {  // Text.
    SetLanguage(Language::kEnglish);
    CHECK(Format(L"{0}-{1}-{0}", {L"a", L"b"}) == L"a-b-a");
    CatalogGame a{}, b{}, c{};
    a.title = L"A", b.title = L"B", c.title = L"C";
    CHECK(JoinTitles({&a}) == L"A");
    CHECK(JoinTitles({&a, &b, &c}) == L"A, B and C");
    CHECK(Tr(kTextPlay) == L"Play");
    SetLanguage(Language::kSpanish);
    CHECK(JoinTitles({&a, &b}) == L"A y B");
    CHECK(Tr(kTextPlay) == L"Jugar");
    CHECK((Message{kTextAdded, {L"X"}}.Render() == L"X está preparado."));
    CHECK(Message{}.Render().empty());
    SaveLanguage(Language::kSpanish);
    SetLanguage(Language::kEnglish);
    LoadLanguage();
    CHECK(CurrentLanguage() == Language::kSpanish);
    SaveLanguage(Language::kEnglish);
    CHECK(Hex32(0x4D5308ED) == L"4D5308ED");
  }

  {  // Terms: accepted once per version.
    CHECK(!TermsAccepted());
    AcceptTerms();
    CHECK(TermsAccepted());
    std::wstring text = TermsText(Language::kEnglish);
    CHECK(!text.empty() && text.find(L'\r') == std::wstring::npos);
    CHECK(TermsText(Language::kSpanish) != text);
  }

  {  // Archive names of Games on Demand content.
    uint32_t title = 0;
    std::string folder;
    CHECK(ParseContentPath("Disc/4D5308ED/00007000/ABCDEF", "00007000", title, folder));
    CHECK(title == 0x4D5308ED && folder == "Disc/4D5308ED/00007000");
    CHECK(ParseContentPath("4d5308ed\\00000002\\pack", "00000002", title, folder) && title == 0x4D5308ED);
    CHECK(!ParseContentPath("X4D5308ED/00007000/A", "00007000", title, folder));
    CHECK(!ParseContentPath("4D5308EG/00007000/A", "00007000", title, folder));
  }

  {  // The catalog and an empty library.
    CHECK(AvailableGames().size() == std::size(kCatalog));
    CHECK(FindAvailableGame(game.title_id) == &game);
    CHECK(FindCatalogGame(game.id) == &game);
    auto library = ReadLibrary();
    CHECK(library.size() == std::size(kCatalog));
    CHECK(!library[0].installed && library[0].dir.empty());
  }

  const fs::path copy = player / L"My Copy" / L"Disc";
  WriteCopy(copy, game);

  {  // A folder: remembered where it is.
    Observer observer;
    Outcome outcome = Run(observer, [&](Job& job) { return AddFolder(job, player / L"My Copy"); });
    CHECK(outcome.message.id == kTextAddedFolder && !outcome.message.error);
    CHECK(outcome.select == game.id);
    CHECK(observer.game == &game);
    CHECK(observer.asked == 0);
    CHECK(IsInstalled(game));
    CHECK(fs::equivalent(StoredLocation(game), copy));
    auto library = ReadLibrary();
    CHECK(library[0].installed && !library[0].location_gone && fs::equivalent(library[0].dir, copy));
  }

  {  // Again: the player is asked, and can keep the one in the library.
    Observer observer;
    observer.replace = false;
    Outcome outcome = Run(observer, [&](Job& job) { return AddFolder(job, copy); });
    CHECK(observer.asked == 1);
    CHECK(outcome.message.id == kTextCancelled);
  }

  {  // Not a game, another title, an incomplete copy.
    Observer observer;
    fs::create_directories(player / L"Empty");
    CHECK(Run(observer, [&](Job& job) { return AddFolder(job, player / L"Empty"); }).message.id ==
          kTextErrorNoXexFolder);
    WriteXex(player / L"Other" / L"default.xex", 0x12345678);
    Outcome other = Run(observer, [&](Job& job) { return AddFolder(job, player / L"Other"); });
    CHECK(other.message.id == kTextErrorUnsupported && other.message.args[0] == L"12345678");
    for (const CatalogGame& g : kCatalog) {
      std::wstring files = g.required_files;
      if (files.empty()) continue;
      fs::path incomplete = player / (L"Incomplete-" + std::wstring(g.id));
      WriteXex(incomplete / L"default.xex", g.title_id);
      Outcome missing = Run(observer, [&](Job& job) { return AddFolder(job, incomplete); });
      CHECK(missing.message.id == kTextErrorMissingFile);
      CHECK(missing.message.args[0] == files.substr(0, files.find(L'|')));
      break;
    }
  }

  {  // The player's cover.
    WriteFile(player / L"cover.png", "png");
    CHECK(SetPlayerCover(game, player / L"cover.png"));
    CHECK(CoverFile(game).filename() == L"cover.png");
    CHECK(ReadLibrary()[0].cover.filename() == L"cover.png");
    RestoreCover(game);
    CHECK(CoverFile(game).empty());
  }

  {  // Removing it forgets it; the player's folder stays.
    Observer observer;
    Outcome outcome = Run(observer, [&](Job& job) { return RemoveGame(job, game); });
    CHECK(outcome.message.id == kTextRemoved);
    CHECK(observer.Saw(Step::kRemoving));
    CHECK(!IsInstalled(game));
    CHECK(Exists(copy / L"default.xex"));
  }

  if (seven_zip.empty()) {
    std::printf("SKIP archives: 7-Zip is not installed\n");
  } else {
    // The release's bin\7z.exe, as a release has it.
    fs::copy_file(seven_zip, release / L"bin" / L"7z.exe");
    fs::copy_file(seven_zip.parent_path() / L"7z.dll", release / L"bin" / L"7z.dll");
    CHECK(SevenZip() == release / L"bin" / L"7z.exe");
    const fs::path archive = sandbox / L"game.7z";
    CHECK(Run7z(L"a -bso0 " + Quote(archive) + L" " + Quote(player / L"My Copy")) == 0);

    {  // An archive: extracted into games\<id>\game.
      Observer observer;
      Outcome outcome = Run(observer, [&](Job& job) { return AddArchive(job, archive); });
      CHECK(outcome.message.id == kTextAdded && outcome.select == game.id);
      CHECK(observer.game == &game);
      CHECK(observer.Saw(Step::kChecking) && observer.Saw(Step::kFinishing));
      CHECK(Exists(ExtractedDir(game) / L"default.xex"));
      CHECK(Exists(ExtractedDir(game) / L"media" / L"level.bin"));
      CHECK(!Exists(GamesDir() / L".partial") && !Exists(GamesDir() / L".probe"));
      auto library = ReadLibrary();
      CHECK(library[0].installed && library[0].dir == ExtractedDir(game));
    }

    {  // The extracted copy added as a folder is the same copy.
      Observer observer;
      Outcome outcome = Run(observer, [&](Job& job) { return AddFolder(job, ExtractedDir(game)); });
      CHECK(outcome.message.id == kTextAdded && observer.asked == 0);
      CHECK(!Exists(LocationFile(game)));
    }

    {  // An archive without a game, and one that isn't an archive.
      WriteFile(player / L"notes.txt", "notes");
      const fs::path notes = sandbox / L"notes.zip";
      CHECK(Run7z(L"a -bso0 " + Quote(notes) + L" " + Quote(player / L"notes.txt")) == 0);
      Observer observer;
      CHECK(Run(observer, [&](Job& job) { return AddArchive(job, notes); }).message.id == kTextErrorNotGame);
      CHECK(Run(observer, [&](Job& job) { return AddArchive(job, player / L"notes.txt"); }).message.id ==
            kTextErrorList);
    }

    {  // Cancelled before 7-Zip starts: it is ended as it starts, and nothing is left behind.
      Observer observer;
      Run(observer, [&](Job& job) { return RemoveGame(job, game); });
      Job job(observer);
      job.Cancel();
      Outcome outcome = AddArchive(job, archive);
      CHECK(outcome.message.empty());
      CHECK(!IsInstalled(game));
      CHECK(!Exists(GamesDir() / L".probe") && !Exists(GamesDir() / L".partial"));
    }
  }

  {  // The worker: one job on its own thread, the outcome last; cancelling ends a running job.
    Worker worker;
    Observer observer;
    std::future<Outcome> done = observer.done.get_future();
    worker.Start(observer, [&](Job& job) { return AddFolder(job, copy); });
    CHECK(done.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK(done.get().message.id == kTextAddedFolder);
    worker.Join();

    Observer waiting;
    std::future<Outcome> cancelled = waiting.done.get_future();
    worker.Start(waiting, [](Job& job) {
      while (!job.Cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      return Outcome{};
    });
    worker.Cancel();
    CHECK(cancelled.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK(cancelled.get().message.id == kTextCancelled);
  }

  std::error_code ec;
  fs::remove_all(sandbox, ec);
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
