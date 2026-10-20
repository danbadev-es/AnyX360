// The launcher's logic (tools/launcher/core) in a sandbox: a release folder with stand-in game
// executables, its own %LOCALAPPDATA%, and synthetic copies in every format the launcher takes (a
// default.xex header with the title ID and the game's required files, as a folder, archives of
// several kinds, a disc image, an Xbox Live Arcade package with downloadable content and a title
// update). Archives use 7-Zip when it is installed.
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "core/catalog.h"
#include "core/install.h"
#include "core/jobs.h"
#include "core/library.h"
#include "core/probe.h"
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

void PutBe(std::string& b, size_t at, uint32_t v, int bytes = 4) {
  for (int i = 0; i < bytes; ++i) b[at + i] = char(v >> (8 * (bytes - 1 - i)));
}
void PutLe(std::string& b, size_t at, uint32_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) b[at + i] = char(v >> (8 * i));
}

// A default.xex header: 'XEX2', one optional header (execution info, 0x00040006) whose record holds
// the title ID at +0xC.
std::string Xex(uint32_t title_id) {
  std::string x(0x200, '\0');
  PutBe(x, 0, 0x58455832);
  PutBe(x, 0x14, 1);
  PutBe(x, 0x18, 0x00040006);
  PutBe(x, 0x1C, 0x100);
  PutBe(x, 0x10C, title_id);
  return x;
}

// A complete copy of `game`: path -> content ('/' between folders).
std::map<std::string, std::string> Copy(const CatalogGame& game, size_t level_size = 4096) {
  std::map<std::string, std::string> files{{"default.xex", Xex(game.title_id)},
                                           {"media/level.bin", std::string(level_size, 'x')}};
  std::wstring required = game.required_files;
  for (size_t start = 0; start < required.size();) {
    size_t end = required.find(L'|', start);
    if (end == std::wstring::npos) end = required.size();
    if (end > start) files[Narrow(required.substr(start, end - start))] = "data";
    start = end + 1;
  }
  return files;
}

void WriteCopy(const fs::path& folder, const std::map<std::string, std::string>& files) {
  for (const auto& [path, data] : files) WriteFile(folder / Widen(path), data);
}

// An Xbox 360 disc image ("xiso" layout: the volume descriptor at sector 32), padded to 1 MB.
std::string MakeIso(const std::map<std::string, std::string>& files) {
  constexpr size_t kSector = 2048;
  std::map<std::string, std::vector<std::string>> children;  // folder -> names in it ("name/": a folder)
  std::set<std::string> folders{""};
  for (const auto& [path, data] : files) {
    std::string parent;
    size_t start = 0;
    for (size_t slash; (slash = path.find('/', start)) != std::string::npos; start = slash + 1) {
      std::string folder = path.substr(0, slash);
      if (folders.insert(folder).second) children[parent].push_back(path.substr(start, slash - start) + "/");
      parent = folder;
    }
    children[parent].push_back(path.substr(start));
  }
  std::map<std::string, uint32_t> table_sector, file_sector;
  uint32_t next = 33;
  for (const std::string& folder : folders) table_sector[folder] = next++;
  for (const auto& [path, data] : files) {
    file_sector[path] = next;
    next += uint32_t(std::max<size_t>(1, (data.size() + kSector - 1) / kSector));
  }
  std::string image(std::max<size_t>(size_t(next) * kSector, 1 << 20), '\0');
  for (const std::string& folder : folders) {
    std::string table;
    std::vector<size_t> offsets;
    for (const std::string& name : children[folder]) {
      offsets.push_back(table.size());
      table.append((14 + name.size() + 3) & ~size_t(3), '\0');
    }
    for (size_t i = 0; i < children[folder].size(); ++i) {
      std::string name = children[folder][i];
      bool is_dir = name.back() == '/';
      if (is_dir) name.pop_back();
      std::string path = folder.empty() ? name : folder + "/" + name;
      size_t e = offsets[i];
      PutLe(table, e + 2, i + 1 < offsets.size() ? uint32_t(offsets[i + 1] / 4) : 0, 2);
      PutLe(table, e + 4, is_dir ? table_sector[path] : file_sector[path], 4);
      PutLe(table, e + 8, is_dir ? uint32_t(kSector) : uint32_t(files.at(path).size()), 4);
      table[e + 12] = char(is_dir ? 0x10 : 0x20);
      table[e + 13] = char(name.size());
      table.replace(e + 14, name.size(), name);
    }
    table.resize(kSector, '\xFF');
    image.replace(size_t(table_sector[folder]) * kSector, kSector, table);
  }
  for (const auto& [path, data] : files) image.replace(size_t(file_sector[path]) * kSector, data.size(), data);
  std::string descriptor(kSector, '\0');
  descriptor.replace(0, 20, "MICROSOFT*XBOX*MEDIA");
  PutLe(descriptor, 0x14, table_sector[""], 4);
  PutLe(descriptor, 0x18, uint32_t(kSector), 4);
  descriptor.replace(0x7EC, 20, "MICROSOFT*XBOX*MEDIA");
  image.replace(32 * kSector, kSector, descriptor);
  return image;
}

// A read-only STFS content package (one level-0 hash table, so under 170 blocks) of `content_type`
// for `title_id`, holding `files` (one level of folders).
std::string MakeStfs(uint32_t content_type, uint32_t title_id, const std::map<std::string, std::string>& files) {
  constexpr size_t kBlock = 0x1000;
  std::string table(kBlock, '\0');
  std::map<std::string, uint16_t> folder_index;
  std::vector<std::pair<size_t, std::string>> blocks{{0, ""}};  // (data block, content); block 0 is the table
  std::map<uint32_t, uint32_t> links{{0, 0xFFFFFF}};
  int entry = 0;
  auto add_entry = [&](const std::string& name, uint16_t parent, bool is_dir, uint32_t start, uint32_t count,
                       uint32_t size) {
    size_t e = size_t(entry++) * 0x40;
    table.replace(e, name.size(), name);
    table[e + 0x28] = char(name.size() | (is_dir ? 0x80 : 0));
    PutLe(table, e + 0x29, count, 3);
    PutLe(table, e + 0x2C, count, 3);
    PutLe(table, e + 0x2F, start, 3);
    PutBe(table, e + 0x32, parent, 2);
    PutBe(table, e + 0x34, size);
  };
  for (const auto& [path, data] : files) {
    size_t slash = path.find('/');
    if (slash != std::string::npos && !folder_index.count(path.substr(0, slash))) {
      folder_index[path.substr(0, slash)] = uint16_t(entry);
      add_entry(path.substr(0, slash), 0xFFFF, true, 0, 0, 0);
    }
  }
  uint32_t next = 1;
  for (const auto& [path, data] : files) {
    size_t slash = path.find('/');
    uint32_t count = uint32_t(std::max<size_t>(1, (data.size() + kBlock - 1) / kBlock));
    add_entry(slash == std::string::npos ? path : path.substr(slash + 1),
              slash == std::string::npos ? 0xFFFF : folder_index[path.substr(0, slash)], false, next, count,
              uint32_t(data.size()));
    for (uint32_t k = 0; k < count; ++k) {
      blocks.push_back({next + k, data.substr(k * kBlock, kBlock)});
      links[next + k] = k + 1 < count ? next + k + 1 : 0xFFFFFF;
    }
    next += count;
  }
  blocks[0].second = table;
  std::string hashes(kBlock, '\0');
  for (const auto& [block, link] : links) PutBe(hashes, block * 0x18 + 0x14, 0x80000000 | link);
  std::string header(0xA000, '\0');
  header.replace(0, 4, "LIVE");
  PutBe(header, 0x340, 0x971A);
  PutBe(header, 0x344, content_type);
  PutBe(header, 0x360, title_id);
  std::string descriptor(0x24, '\0');
  descriptor[0] = 0x24;
  descriptor[2] = 1;  // read-only
  PutLe(descriptor, 3, 1, 2);
  PutBe(descriptor, 0x1C, next);
  header.replace(0x379, descriptor.size(), descriptor);
  std::string data((size_t(next) + 1) * kBlock, '\0');
  data.replace(0, kBlock, hashes);
  for (const auto& [block, content] : blocks) data.replace((block + 1) * kBlock, content.size(), content);
  return header + data;
}

struct Observer : JobObserver {
  std::mutex mutex;
  std::vector<std::pair<Step, int>> progress;
  const CatalogGame* game = nullptr;
  int asked = 0;
  bool replace = true;
  std::vector<std::wstring> passwords;  // given in turn, then none
  int password_asks = 0, wrong_passwords = 0;
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
  std::optional<std::wstring> AskPassword(const fs::path&, bool wrong) override {
    wrong_passwords += wrong;
    if (password_asks >= int(passwords.size())) return std::nullopt;
    return passwords[size_t(password_asks++)];
  }
  void OnDone(const Outcome& outcome) override { done.set_value(outcome); }
  bool Saw(Step step) {
    std::lock_guard lock(mutex);
    for (const auto& [s, p] : progress)
      if (s == step) return true;
    return false;
  }
  // The percent never went back (the bar fills once).
  bool Monotonic() {
    std::lock_guard lock(mutex);
    for (size_t i = 1; i < progress.size(); ++i)
      if (progress[i].second < progress[i - 1].second) return false;
    return true;
  }
};

// Runs a job on this thread.
Outcome Run(Observer& observer, const std::function<Outcome(Job&)>& work) {
  Job job(observer);
  return work(job);
}

Outcome Add(const fs::path& source, Observer* observer = nullptr) {
  Observer fallback;
  return Run(observer ? *observer : fallback, [&](Job& job) { return AddGame(job, source); });
}

void Remove(const CatalogGame& game) {
  Observer observer;
  Run(observer, [&](Job& job) { return RemoveGame(job, game); });
}

int Run7z(const std::wstring& arguments) {
  return RunCaptured(Quote(SevenZip()) + L" " + arguments + L" -bso0 -bsp0", [](const std::string&) {}, nullptr);
}

bool Clean() { return !Exists(WorkDir()); }

bool Installed(const CatalogGame& game) {
  return Exists(ExtractedDir(game) / L"default.xex") && Exists(ExtractedDir(game) / L"media" / L"level.bin");
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

  {  // Archive names of console content.
    uint32_t title = 0;
    std::string folder;
    CHECK(ParseContentPath("Disc/4D5308ED/00007000/ABCDEF", "00007000", title, folder));
    CHECK(title == 0x4D5308ED && folder == "Disc/4D5308ED/00007000");
    CHECK(ParseContentPath("4d5308ed\\00000002\\pack", "00000002", title, folder) && title == 0x4D5308ED);
    CHECK(ParseContentPath("58410B79\\000D0000", "000d0000", title, folder) && title == 0x58410B79);
    CHECK(!ParseContentPath("X4D5308ED/00007000/A", "00007000", title, folder));
    CHECK(!ParseContentPath("4D5308EG/00007000/A", "00007000", title, folder));
    ArchiveListing listing;
    listing.entries = {{"58410B79\\000D0000\\CF27", 10}, {"58410B79\\00000002\\0FCE", 5},
                       {"58410B79\\000B0000\\tu00000002", 5}, {"TU_1C422RP", 5}, {"58410B79", 0, true}};
    ArchiveInfo info = ClassifyArchive(listing);
    CHECK(info.content_title == 0x58410B79 && info.arcade && !info.has_game);
    CHECK((info.content_folders == std::vector<std::string>{"58410B79\\000D0000", "58410B79\\00000002"}));
  }

  {  // Split archives open from their first part.
    for (const wchar_t* name : {L"g.part1.rar", L"g.part2.rar", L"h.part01.rar", L"h.part02.rar", L"s.7z.001",
                                L"s.7z.002", L"z.zip", L"z.z01"})
      WriteFile(player / L"volumes" / name, "x");
    fs::path v = player / L"volumes";
    CHECK(FirstVolume(v / L"g.part2.rar") == v / L"g.part1.rar");
    CHECK(FirstVolume(v / L"h.part02.rar") == v / L"h.part01.rar");
    CHECK(FirstVolume(v / L"s.7z.002") == v / L"s.7z.001");
    CHECK(FirstVolume(v / L"z.z01") == v / L"z.zip");
    CHECK(FirstVolume(v / L"g.part1.rar") == v / L"g.part1.rar");
  }

  {  // The catalog and an empty library.
    CHECK(AvailableGames().size() == std::size(kCatalog));
    CHECK(FindAvailableGame(game.title_id) == &game);
    CHECK(FindCatalogGame(game.id) == &game);
    auto library = ReadLibrary();
    CHECK(library.size() == std::size(kCatalog));
    CHECK(!library[0].installed && library[0].dir.empty());
  }

  const auto files = Copy(game);
  const fs::path copy = player / L"My Copy" / L"Disc";
  WriteCopy(copy, files);
  // Discs of avatar games carry the avatar asset pack (a package of title FFFE07DF).
  WriteFile(copy / L"AvatarAssetPack",
            MakeStfs(x360tool::content_type::kMarketplace, 0xFFFE07DF, {{"AvatarAssetPack.toc", "avatar pack"}}));
  const fs::path avatar_pack = LocalAppData() / L"AnyX360" / L"system" / L"AvatarAssetPack.toc";

  {  // Recognised by content.
    WriteFile(player / L"image.iso", MakeIso(files));
    WriteFile(player / L"package", MakeStfs(x360tool::content_type::kArcadeTitle, game.title_id, files));
    WriteFile(player / L"random.bin", std::string(4096, 'r'));
    CHECK(IdentifyFile(copy / L"default.xex") == FileKind::kXex);
    CHECK(IdentifyFile(player / L"image.iso") == FileKind::kDiscImage);
    CHECK(IdentifyFile(player / L"package") == FileKind::kPackage);
    CHECK(IdentifyFile(player / L"random.bin") == FileKind::kOther);
  }

  {  // A folder: remembered where it is.
    Observer observer;
    Outcome outcome = Add(player / L"My Copy", &observer);
    CHECK(outcome.message.id == kTextAddedFolder && !outcome.message.error);
    CHECK(outcome.select == game.id);
    CHECK(observer.game == &game);
    CHECK(observer.asked == 0);
    CHECK(IsInstalled(game));
    CHECK(fs::equivalent(StoredLocation(game), copy));
    CHECK(Exists(avatar_pack) && fs::file_size(avatar_pack) == std::size("avatar pack") - 1);
    auto library = ReadLibrary();
    CHECK(library[0].installed && !library[0].location_gone && fs::equivalent(library[0].dir, copy));
    CHECK(Add(copy / L"default.xex").message.id == kTextAddedFolder);  // default.xex itself
  }

  {  // Again: the player is asked, and can keep the one in the library.
    Observer observer;
    observer.replace = false;
    Outcome outcome = Add(copy, &observer);
    CHECK(observer.asked == 1);
    CHECK(outcome.message.id == kTextCancelled);
  }

  {  // Not a game, another title, an incomplete copy, a file nobody knows.
    fs::create_directories(player / L"Empty");
    CHECK(Add(player / L"Empty").message.id == kTextErrorNoXexFolder);
    WriteFile(player / L"Other" / L"default.xex", Xex(0x12345678));
    Outcome other = Add(player / L"Other");
    CHECK(other.message.id == kTextErrorUnsupported && other.message.args[0] == L"12345678");
    for (const CatalogGame& g : kCatalog) {
      std::wstring required = g.required_files;
      if (required.empty()) continue;
      fs::path incomplete = player / (L"Incomplete-" + std::wstring(g.id));
      WriteFile(incomplete / L"default.xex", Xex(g.title_id));
      Outcome missing = Add(incomplete);
      CHECK(missing.message.id == kTextErrorMissingFile);
      CHECK(missing.message.args[0] == required.substr(0, required.find(L'|')));
      break;
    }
    if (!seven_zip.empty()) CHECK(Add(player / L"random.bin").message.id == kTextErrorUnknownFormat);
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

  {  // A disc image: written out into games\<id>\game.
    Observer observer;
    Outcome outcome = Add(player / L"image.iso", &observer);
    CHECK(outcome.message.id == kTextAdded && outcome.select == game.id);
    CHECK(observer.game == &game && observer.Saw(Step::kFinishing));
    CHECK(Installed(game) && Clean());
    WriteFile(player / L"other.iso", MakeIso({{"default.xex", Xex(0x12345678)}}));
    CHECK(Add(player / L"other.iso").message.id == kTextErrorUnsupported);
    Remove(game);
  }

  {  // An Xbox Live Arcade game with its downloadable content, next to a title update.
    const std::wstring title = Hex32(game.title_id);
    const fs::path arcade = player / L"Arcade";
    WriteFile(arcade / title / L"000D0000" / L"GAMEPACKAGE",
              MakeStfs(x360tool::content_type::kArcadeTitle, game.title_id, files));
    WriteFile(arcade / title / L"00000002" / L"DLCPACKAGE",
              MakeStfs(x360tool::content_type::kMarketplace, game.title_id, {{"songs/song.bin", "song"}}));
    WriteFile(arcade / L"TU_1C422RP",
              MakeStfs(x360tool::content_type::kInstaller, game.title_id, {{"default.xexp", "patch"}}));
    Observer observer;
    Outcome outcome = Add(arcade, &observer);
    CHECK(outcome.message.id == kTextAddedContent && outcome.message.args[1] == L"1");
    CHECK(outcome.notes.size() == 1 && outcome.notes[0].id == kTextNoteUpdatesSkipped);
    CHECK(Installed(game) && Clean());
    const fs::path dlc = sandbox / L"local" / L"AnyX360" / game.user_dir / L"content" / L"00000002" / L"DLCPACKAGE";
    CHECK(Exists(dlc / L"songs" / L"song.bin"));
    // The package chosen directly; then its content alone, and the update alone.
    CHECK(Add(arcade / title / L"000D0000" / L"GAMEPACKAGE").message.id == kTextAddedContent);
    Outcome content = Add(arcade / title / L"00000002" / L"DLCPACKAGE");
    CHECK(content.message.id == kTextAddedContentOnly && content.message.args[1] == L"1");
    CHECK(Add(arcade / L"TU_1C422RP").message.id == kTextErrorUpdateOnly);
    Remove(game);
    CHECK(Add(arcade / title / L"00000002" / L"DLCPACKAGE").message.id == kTextErrorContentOnly);
  }

  if (seven_zip.empty()) {
    std::printf("SKIP archives: 7-Zip is not installed\n");
  } else {
    // The release's bin\7z.exe, as a release has it.
    fs::copy_file(seven_zip, release / L"bin" / L"7z.exe");
    fs::copy_file(seven_zip.parent_path() / L"7z.dll", release / L"bin" / L"7z.dll");
    CHECK(SevenZip() == release / L"bin" / L"7z.exe");
    const fs::path archive = sandbox / L"game.7z";
    CHECK(Run7z(L"a " + Quote(archive) + L" " + Quote(player / L"My Copy")) == 0);

    {  // An archive: extracted into games\<id>\game.
      Observer observer;
      Outcome outcome = Add(archive, &observer);
      CHECK(outcome.message.id == kTextAdded && outcome.select == game.id);
      CHECK(observer.game == &game);
      CHECK(observer.Saw(Step::kChecking) && observer.Saw(Step::kFinishing));
      CHECK(observer.Monotonic());
      CHECK(Installed(game) && Clean());
      auto library = ReadLibrary();
      CHECK(library[0].installed && library[0].dir == ExtractedDir(game));
    }

    {  // The extracted copy added as a folder is the same copy.
      Observer observer;
      Outcome outcome = Add(ExtractedDir(game), &observer);
      CHECK(outcome.message.id == kTextAdded && observer.asked == 0);
      CHECK(!Exists(LocationFile(game)));
      Remove(game);
    }

    {  // Archives in other formats, and archives inside archives.
      const fs::path tar = sandbox / L"game.tar", tgz = sandbox / L"game.tar.gz", zipped_iso = sandbox / L"iso.zip";
      CHECK(Run7z(L"a -ttar " + Quote(tar) + L" " + Quote(player / L"My Copy")) == 0);
      CHECK(Run7z(L"a -tgzip " + Quote(tgz) + L" " + Quote(tar)) == 0);
      CHECK(Run7z(L"a -tzip " + Quote(zipped_iso) + L" " + Quote(player / L"image.iso")) == 0);
      for (const fs::path& source : {tar, tgz, zipped_iso}) {
        Outcome outcome = Add(source);
        CHECK(outcome.message.id == kTextAdded);
        CHECK(Installed(game) && Clean());
        Remove(game);
      }
    }

    {  // A split archive, chosen by any part; a missing part.
      WriteCopy(player / L"Big", Copy(game, 300 * 1024));
      const fs::path split = sandbox / L"split" / L"big.7z";
      fs::create_directories(split.parent_path());
      CHECK(Run7z(L"a -mx0 -v100k " + Quote(split) + L" " + Quote(player / L"Big")) == 0);
      fs::path second = split;
      second += L".002";
      CHECK(Add(second).message.id == kTextAdded);
      CHECK(Installed(game) && Clean());
      Remove(game);
      fs::path third = split;
      third += L".003";
      fs::remove(third);
      Outcome missing = Add(second);
      CHECK(missing.message.id == kTextErrorMissingVolume || missing.message.id == kTextErrorExtract);
      CHECK(!IsInstalled(game) && Clean());
    }

    {  // Passwords: names encrypted too (asked before listing), or only the data (asked, then checked).
      const fs::path hidden = sandbox / L"hidden.7z", locked = sandbox / L"locked.zip";
      CHECK(Run7z(L"a -mhe=on -pSecret " + Quote(hidden) + L" " + Quote(player / L"My Copy")) == 0);
      CHECK(Run7z(L"a -tzip -pSecret " + Quote(locked) + L" " + Quote(player / L"My Copy")) == 0);
      for (const fs::path& source : {hidden, locked}) {
        Observer observer;
        observer.passwords = {L"wrong", L"Secret"};
        Outcome outcome = Add(source, &observer);
        CHECK(outcome.message.id == kTextAdded);
        CHECK(observer.password_asks == 2 && observer.wrong_passwords == 1);
        CHECK(observer.Monotonic());  // the retried extraction doesn't start the bar again
        CHECK(Installed(game) && Clean());
        Remove(game);
        Observer gives_up;
        CHECK(Add(source, &gives_up).message.id == kTextErrorPassword);
        CHECK(!IsInstalled(game) && Clean());
      }
    }

    {  // An Xbox Live Arcade archive: only the title's folders; the update in it is noted.
      const fs::path xbla = sandbox / L"xbla.7z";
      CHECK(Run7z(L"a " + Quote(xbla) + L" " + Quote(player / L"Arcade" / L"*")) == 0);
      Outcome outcome = Add(xbla);
      CHECK(outcome.message.id == kTextAddedContent);
      CHECK(Installed(game) && Clean());
      Remove(game);
    }

    {  // An archive without a game.
      WriteFile(player / L"notes.txt", "notes");
      const fs::path notes = sandbox / L"notes.zip";
      CHECK(Run7z(L"a " + Quote(notes) + L" " + Quote(player / L"notes.txt")) == 0);
      CHECK(Add(notes).message.id == kTextErrorUnknownFormat);
      CHECK(Clean());
    }

    {  // Cancelled before 7-Zip starts: it is ended as it starts, and nothing is left behind.
      Observer observer;
      Job job(observer);
      job.Cancel();
      Outcome outcome = AddGame(job, archive);
      CHECK(outcome.message.empty());
      CHECK(!IsInstalled(game) && Clean());
    }
  }

  {  // The worker: one job on its own thread, the outcome last; cancelling ends a running job.
    Worker worker;
    Observer observer;
    std::future<Outcome> done = observer.done.get_future();
    worker.Start(observer, [&](Job& job) { return AddGame(job, copy); });
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
