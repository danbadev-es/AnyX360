// The launcher's logic (tools/launcher/core) in a sandbox: a release folder with stand-in game
// executables, its own %LOCALAPPDATA%, and synthetic copies in every format the launcher takes (a
// default.xex header with the title ID and the game's required files, as a folder, archives of
// several kinds, a disc image, an Xbox Live Arcade package with downloadable content and a title
// update). Archives use 7-Zip when it is installed.
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <future>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "core/anyx.h"
#include "core/catalog.h"
#include "core/hash.h"
#include "core/install.h"
#include "core/jobs.h"
#include "core/launch.h"
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
// the media ID at +0, the version at +4, the base version at +8 and the title ID at +0xC.
std::string Xex(uint32_t title_id, uint32_t version = 0, uint32_t media_id = 0) {
  std::string x(0x200, '\0');
  PutBe(x, 0, 0x58455832);
  PutBe(x, 0x14, 1);
  PutBe(x, 0x18, 0x00040006);
  PutBe(x, 0x1C, 0x100);
  PutBe(x, 0x100, media_id);
  PutBe(x, 0x104, version);
  PutBe(x, 0x108, version);
  PutBe(x, 0x10C, title_id);
  return x;
}

// A title update's default.xexp header: 'XEX2', headers 0x400 bytes, the security info at 0x200 (no
// pages), one optional header (the delta patch descriptor, 0x000005FF) whose record holds its size,
// the version it makes and the version it updates.
std::string Xexp(uint32_t from, uint32_t to) {
  std::string x(0x400, '\0');
  PutBe(x, 0, 0x58455832);
  PutBe(x, 0x04, 0x50);  // a delta patch
  PutBe(x, 0x08, 0x400);
  PutBe(x, 0x10, 0x200);
  PutBe(x, 0x14, 1);
  PutBe(x, 0x18, 0x000005FF);
  PutBe(x, 0x1C, 0x100);
  PutBe(x, 0x100, 0x60);
  PutBe(x, 0x104, to);
  PutBe(x, 0x108, from);
  return x;
}

// The version a game was recompiled from (game.json's "xex").
uint32_t Version(const CatalogGame& game) {
  uint32_t version = 0;
  ParseXexVersion(game.xex_version, version);
  return version;
}

// A complete copy of `game`: path -> content ('/' between folders). Its executables are the version
// the game plays.
std::map<std::string, std::string> Copy(const CatalogGame& game, size_t level_size = 4096) {
  std::map<std::string, std::string> files{{"default.xex", Xex(game.title_id, Version(game), game.xex_media_id)},
                                           {"media/level.bin", std::string(level_size, 'x')}};
  std::wstring required = game.required_files;
  for (size_t start = 0; start < required.size();) {
    size_t end = required.find(L'|', start);
    if (end == std::wstring::npos) end = required.size();
    const std::string file = Narrow(required.substr(start, end - start));
    if (end > start)
      files[file] = file.size() > 4 && file.substr(file.size() - 4) == ".xex" ? Xex(game.title_id, Version(game)) : "data";
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
  // Folders at any depth (Resident Evil 6's nativeXenon/arc/X360/Title.arc), each under its parent.
  auto parent_of = [&](const std::string& path) -> uint16_t {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? 0xFFFF : folder_index[path.substr(0, slash)];
  };
  for (const auto& [path, data] : files) {
    for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1)) {
      const std::string folder = path.substr(0, slash);
      if (folder_index.count(folder)) continue;
      const uint16_t parent = parent_of(folder);
      folder_index[folder] = uint16_t(entry);
      add_entry(folder.substr(folder.rfind('/') == std::string::npos ? 0 : folder.rfind('/') + 1), parent, true, 0,
                0, 0);
    }
  }
  uint32_t next = 1;
  for (const auto& [path, data] : files) {
    size_t slash = path.rfind('/');
    uint32_t count = uint32_t(std::max<size_t>(1, (data.size() + kBlock - 1) / kBlock));
    add_entry(slash == std::string::npos ? path : path.substr(slash + 1), parent_of(path), false, next, count,
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

// The game's entry in the library (which lists the catalog in its order).
LibraryGame Entry(const CatalogGame& game) {
  for (const LibraryGame& entry : ReadLibrary())
    if (entry.info == &game) return entry;
  return {};
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
  // The first game that takes its downloadable content (the tests add some) and is played without a
  // title update (the tests add copies without one).
  const CatalogGame& game = *std::find_if(std::begin(kCatalog), std::end(kCatalog),
                                          [](const CatalogGame& g) { return g.content && !UsesTitleUpdate(g); });

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
    // The title's content, its updates (both forms), not other titles'.
    CHECK((info.content_folders == std::vector<std::string>{"58410B79\\000D0000", "58410B79\\00000002",
                                                            "58410B79\\000B0000", "TU_1C422RP"}));
    listing.entries.push_back({"4D5308ED\\000B0000\\tu00000001", 5});
    listing.entries.push_back({"Updates\\tu_16L627D_0000", 5});
    info = ClassifyArchive(listing);
    CHECK(info.content_folders.size() == 5 && info.content_folders[4] == "Updates\\tu_16L627D_0000");
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
    CHECK(ReadLibrary().size() == std::size(kCatalog));
    CHECK(Entry(game).info == &game && !Entry(game).installed && Entry(game).dir.empty());
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
    const LibraryGame entry = Entry(game);
    CHECK(entry.installed && !entry.location_gone && fs::equivalent(entry.dir, copy));
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
      WriteFile(incomplete / L"default.xex", Xex(g.title_id, Version(g)));
      Outcome missing = Add(incomplete);
      CHECK(missing.message.id == kTextErrorMissingFile);
      CHECK(missing.message.args[0] == required.substr(0, required.find(L'|')));
      break;
    }
    if (!seven_zip.empty()) CHECK(Add(player / L"random.bin").message.id == kTextErrorUnknownFormat);
  }

  {  // The version a game plays: another one (an update, a re-release under the same title ID) has
     // other code, and is refused.
    uint32_t version = 0;
    CHECK(ParseXexVersion(L"0.0.0.14", version) && version == 14);
    CHECK(ParseXexVersion(L"2.0.17349.0", version) && XexVersionText(version) == L"2.0.17349.0");
    CHECK(!ParseXexVersion(L"1.2.3", version) && !ParseXexVersion(L"1.2.3.4x", version) &&
          !ParseXexVersion(L"16.0.0.0", version));
    WriteFile(player / L"Identity" / L"default.xex", Xex(0x4D5308C9, 0x10000002, 0x655F2429));
    XexIdentity identity;
    CHECK(ReadXexIdentity(player / L"Identity" / L"default.xex", identity));
    CHECK(identity.title_id == 0x4D5308C9 && identity.version == 0x10000002 && identity.media_id == 0x655F2429);
    auto other_version = Copy(game);
    other_version["default.xex"] = Xex(game.title_id, Version(game) + 1);
    WriteCopy(player / L"Other Version", other_version);
    Outcome refused = Add(player / L"Other Version");
    CHECK(refused.message.id == kTextErrorVersion);
    CHECK(refused.message.args.size() == 3 && refused.message.args[1] == XexVersionText(Version(game) + 1) &&
          refused.message.args[2] == game.xex_version);
    CHECK(fs::equivalent(StoredLocation(game), copy));  // the copy in the library stays
    // A second executable (multiplayer) of another version.
    for (const CatalogGame& g : kCatalog) {
      if (std::wstring(g.required_files).find(L".xex") == std::wstring::npos) continue;
      auto mixed = Copy(g);
      for (auto& [path, data] : mixed)
        if (path != "default.xex" && path.size() > 4 && path.substr(path.size() - 4) == ".xex")
          data = Xex(g.title_id, Version(g) + 1);
      WriteCopy(player / L"Mixed", mixed);
      CHECK(Add(player / L"Mixed").message.id == kTextErrorVersion && !IsInstalled(g));
      break;
    }
  }

  {  // SHA-256, and a folder with a link inside removed: the link goes, not what it points to.
    CHECK(Sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    WriteFile(player / L"Keep" / L"sentinel.txt", "keep");
    CHECK(Sha256File(player / L"Keep" / L"sentinel.txt") == Sha256("keep"));
    const fs::path links = sandbox / L"links";
    fs::create_directories(links / L"inner");
    CHECK(CreateJunction(links / L"inner" / L"to-keep", player / L"Keep"));
    CHECK(Exists(links / L"inner" / L"to-keep" / L"sentinel.txt") && IsLink(links / L"inner" / L"to-keep"));
    RemoveAll(links);
    CHECK(!Exists(links) && Exists(player / L"Keep" / L"sentinel.txt"));
  }

  {  // The .anyx manifest.
    AnyxManifest m;
    m.written_by = "AnyX360 test";
    m.id = "ks";
    m.title = "Kinect Sports";
    m.title_id = 0x4D5308C9, m.version = 2, m.media_id = 0x655F2429;
    m.files = {{"game/default.xex", 512, Sha256("x")}, {"content/PACK/a.bin", 1, Sha256("a")}};
    m.folders = {"game/empty"};
    m.content = {"PACK"};
    AnyxManifest read;
    CHECK(ReadAnyxManifest(WriteAnyxManifest(m), read) == AnyxRead::kOk);
    CHECK(read.id == "ks" && read.title_id == m.title_id && read.version == 2 && read.media_id == m.media_id);
    CHECK(read.files.size() == 2 && read.files[1].path == "content/PACK/a.bin" && read.files[0].sha256 == Sha256("x"));
    CHECK(read.folders == m.folders && read.content == m.content);
    std::string bad;
    CHECK(CheckAnyxPaths(read, bad));
    for (const char* path : {"game/../x", "/game/x", "C:/x", "content/OTHER/x", "game\\x", "x"}) {
      AnyxManifest wrong = read;
      wrong.files.push_back({path, 1, Sha256("x")});
      CHECK(!CheckAnyxPaths(wrong, bad) && bad == path);
    }
    AnyxManifest twice = read;
    twice.files.push_back({"GAME/Default.xex", 1, Sha256("x")});
    CHECK(!CheckAnyxPaths(twice, bad));
    CHECK(ReadAnyxManifest(R"({"format": "anyx", "format_version": 3, "written_by": "AnyX360 9"})", read) == AnyxRead::kNewer &&
          read.written_by == "AnyX360 9");
    CHECK(ReadAnyxManifest(R"({"format": "anyx", "format_version": 1})", read) == AnyxRead::kDamaged);
    CHECK(ReadAnyxManifest(R"({"name": "something else"})", read) == AnyxRead::kNotAnyx);
    CHECK(ReadAnyxManifest("not json", read) == AnyxRead::kNotAnyx);

    // Format version 2: the title update, under update/<package>/.
    AnyxManifest updated = m;
    updated.format_version = 2;
    updated.update = AnyxUpdate{"TU_PACKAGE", 2, 0x00000102};
    updated.files.push_back({"update/TU_PACKAGE/default.xexp", 3, Sha256("tu!")});
    const std::string json = WriteAnyxManifest(updated);
    CHECK(json.find("\"format_version\": 2") != std::string::npos && json.find("\"base_version\": \"0.0.0.2\"") != std::string::npos);
    CHECK(ReadAnyxManifest(json, read) == AnyxRead::kOk);
    CHECK(read.format_version == 2 && read.update && read.update->package == "TU_PACKAGE" &&
          read.update->base_version == 2 && read.update->version == 0x102 && read.files.size() == 3);
    CHECK(CheckAnyxPaths(read, bad));
    for (const char* path : {"update/OTHER/default.xexp", "update/default.xexp", "update/TU_PACKAGE/../x"}) {
      AnyxManifest wrong = read;
      wrong.files.push_back({path, 1, Sha256("x")});
      CHECK(!CheckAnyxPaths(wrong, bad) && bad == path);
    }
    AnyxManifest no_update = m;  // update/ files without an update in the manifest
    no_update.files.push_back({"update/TU_PACKAGE/default.xexp", 3, Sha256("tu!")});
    CHECK(!CheckAnyxPaths(no_update, bad));
    std::string v1 = json;
    v1.replace(v1.find("\"format_version\": 2"), 19, "\"format_version\": 1");
    CHECK(ReadAnyxManifest(v1, read) == AnyxRead::kDamaged);  // an update needs version 2

    // The packages' display names (optional; a trailing space is part of a name).
    AnyxManifest named = m;
    named.content_names = {{"PACK", "Skin Pack 2 "}};
    CHECK(ReadAnyxManifest(WriteAnyxManifest(named), read) == AnyxRead::kOk && read.content_names == named.content_names);
    CHECK(ReadAnyxManifest(WriteAnyxManifest(m), read) == AnyxRead::kOk && read.content_names.empty());
  }

  {  // A downloadable content package's display name, in content\Headers\<type>\<package>.header.
    const fs::path content = sandbox / L"names" / L"content" / L"00000002";
    fs::create_directories(content);
    CHECK(ReadDisplayName(content, content / L"PACK").empty());
    WriteDisplayName(content, content / L"PACK", "Marvel Guardians of the Galaxy ");
    CHECK(Exists(sandbox / L"names" / L"content" / L"Headers" / L"00000002" / L"PACK.header"));
    CHECK(fs::file_size(sandbox / L"names" / L"content" / L"Headers" / L"00000002" / L"PACK.header") == 0x100);
    CHECK(ReadDisplayName(content, content / L"PACK") == "Marvel Guardians of the Galaxy ");
  }

  {  // The player's cover.
    WriteFile(player / L"cover.png", "png");
    CHECK(SetPlayerCover(game, player / L"cover.png"));
    CHECK(CoverFile(game).filename() == L"cover.png");
    CHECK(Entry(game).cover.filename() == L"cover.png");
    RestoreCover(game);
    CHECK(CoverFile(game).empty());
  }

  {  // Shader caches: the player's keeps its entries and gets the shipped ones it lacks.
    auto shader = [](uint64_t hash, uint32_t dwords) {
      std::string entry(12 + dwords * 4, char(hash));
      std::memcpy(entry.data(), &hash, 8);
      std::memcpy(entry.data() + 8, &dwords, 4);
      return entry;
    };
    auto pipeline = [](uint64_t hash) {
      std::string record(72, char(hash));
      std::memcpy(record.data(), &hash, 8);
      return record;
    };
    auto write = [](const fs::path& path, const std::string& data) {
      fs::create_directories(path.parent_path());
      std::ofstream(path, std::ios::binary | std::ios::trunc).write(data.data(), std::streamsize(data.size()));
    };
    auto read = [](const fs::path& path) {
      std::ifstream in(path, std::ios::binary);
      return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    const std::string xsh("XESH\x19\x12\x20\x20", 8), xpso("XEPSD3DC\x20\x09\x26\x20", 12);
    const fs::path shipped = sandbox / L"cache_shipped", player = sandbox / L"cache_player";
    write(shipped / L"415608CB.xsh", xsh + shader(1, 1) + shader(2, 3));
    write(player / L"415608CB.xsh", xsh + shader(1, 1) + shader(3, 2) + "cut-o");  // a cut-off tail
    CHECK(MergeShaderCacheFile(shipped / L"415608CB.xsh", player / L"415608CB.xsh") == 1);
    CHECK(read(player / L"415608CB.xsh") == xsh + shader(1, 1) + shader(3, 2) + shader(2, 3));
    CHECK(MergeShaderCacheFile(shipped / L"415608CB.xsh", player / L"415608CB.xsh") == 0);  // nothing new
    write(shipped / L"415608CB.rtv.d3d12.xpso", xpso + pipeline(7) + pipeline(8));
    write(player / L"415608CB.rtv.d3d12.xpso", xpso + pipeline(8));
    CHECK(MergeShaderCacheFile(shipped / L"415608CB.rtv.d3d12.xpso", player / L"415608CB.rtv.d3d12.xpso") == 1);
    CHECK(read(player / L"415608CB.rtv.d3d12.xpso") == xpso + pipeline(8) + pipeline(7));
    // Another format version stays as it is; a player without a cache gets the shipped one.
    const std::string older = std::string("XESH\x01\x01\x01\x01", 8) + shader(4, 1);
    write(player / L"4D5308AB.xsh", older);
    CHECK(MergeShaderCacheFile(shipped / L"415608CB.xsh", player / L"4D5308AB.xsh") == 0);
    CHECK(read(player / L"4D5308AB.xsh") == older);
    CHECK(MergeShaderCacheFile(shipped / L"415608CB.xsh", player / L"new" / L"415608CB.xsh") == 2);
    CHECK(read(player / L"new" / L"415608CB.xsh") == read(shipped / L"415608CB.xsh"));
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

  // A game whose downloadable content needs its title update (Black Ops II) doesn't take it.
  for (const CatalogGame& g : kCatalog) {
    if (g.content) continue;
    const fs::path pack = player / L"NoContent" / Hex32(g.title_id) / L"00000002" / L"DLCPACKAGE";
    WriteFile(pack, MakeStfs(x360tool::content_type::kMarketplace, g.title_id, {{"dlc1.id", "id"}}));
    Outcome outcome = Add(pack);
    CHECK(outcome.message.id == kTextErrorContentNoGame && outcome.message.args[0] == g.title);
    CHECK(!Exists(sandbox / L"local" / L"AnyX360" / g.user_dir / L"content" / L"00000002" / L"DLCPACKAGE"));
    break;
  }

  // A game played with its title update (game.json "update", Kinect Adventures): the update is a
  // package of type 0x000B0000, as <TitleID>\000B0000\tu... or the console cache's TU_... file. The
  // game is ready only with it, of the version it names.
  const CatalogGame* updated = nullptr;
  for (const CatalogGame& g : kCatalog)
    if (UsesTitleUpdate(g) && !updated) updated = &g;
  uint32_t update_version = 0;
  if (!updated) {
    std::printf("SKIP title updates: no game of this build is played with one\n");
  } else {
    const CatalogGame& g = *updated;
    CHECK(ParseXexVersion(g.update_version, update_version) && update_version != Version(g));
    const std::wstring title = Hex32(g.title_id);
    // Its patch for each executable (default.xexp; default_mp.xexp too for a game with two).
    const auto tu = [&](uint32_t makes) {
      std::map<std::string, std::string> files{{"GfxUI.int", "strings"}};
      for (const std::wstring& patch : TitleUpdatePatches(g)) files[Narrow(patch)] = Xexp(Version(g), makes);
      return MakeStfs(x360tool::content_type::kInstaller, g.title_id, files);
    };
    const fs::path updates = player / L"Updates";
    WriteFile(updates / L"TU_16L627D_000000C000000.0000000000083", tu(update_version));
    WriteFile(updates / L"TU_OTHERVERSION", tu(update_version + 1));
    const fs::path installed = TitleUpdateDir(g);
    CHECK(installed.parent_path().filename() == L"000B0000" && installed.filename() == g.update_package);

    // The copy without its update: added, not ready, and Play says how to add it.
    const fs::path bare = player / L"Without update";
    WriteCopy(bare / L"Disc", Copy(g));
    Outcome added = Add(bare);
    CHECK(added.message.id == kTextAddedFolder);
    CHECK(!added.notes.empty() && added.notes.back().id == kTextNeedsUpdate && added.notes.back().args[1] == g.update_version);
    CHECK(Entry(g).installed && Entry(g).needs_update && !TitleUpdateInstalled(g));
    const Message refused = StartGame(Entry(g), nullptr);
    CHECK(refused.id == kTextNeedsUpdate && refused.error);

    // The update alone, as the cache's TU_ file: installed, the game is ready.
    Outcome alone = Add(updates / L"TU_16L627D_000000C000000.0000000000083");
    CHECK(alone.message.id == kTextAddedUpdate && !alone.message.error && alone.select == g.id);
    CHECK(TitleUpdateInstalled(g) && !Entry(g).needs_update);
    CHECK(TitleUpdateVersion(installed) == update_version && Exists(installed / L"GfxUI.int"));
    // Another version: refused, and the one installed stays.
    Outcome other = Add(updates / L"TU_OTHERVERSION");
    CHECK(other.message.id == kTextErrorUpdateVersion && other.message.args[1] == XexVersionText(update_version + 1) &&
          other.message.args[2] == g.update_version);
    CHECK(TitleUpdateVersion(installed) == update_version && !Exists(fs::path(installed.wstring() + L".new")));
    // Not a package that can be read.
    WriteFile(updates / L"Damaged" / L"TU_DAMAGED", MakeStfs(x360tool::content_type::kInstaller, g.title_id, {{"x.bin", "x"}}));
    CHECK(Add(updates / L"Damaged" / L"TU_DAMAGED").message.id == kTextErrorUpdateDamaged && TitleUpdateInstalled(g));
    // A game with two executables: an update without the second one's patch (or of another version)
    // isn't taken, and the one installed stays.
    const std::vector<std::wstring> patches = TitleUpdatePatches(g);
    if (patches.size() > 1) {
      WriteFile(updates / L"Half" / L"TU_HALF", MakeStfs(x360tool::content_type::kInstaller, g.title_id,
                                                          {{"default.xexp", Xexp(Version(g), update_version)}}));
      CHECK(Add(updates / L"Half" / L"TU_HALF").message.id == kTextErrorUpdateDamaged);
      std::map<std::string, std::string> mixed{{"default.xexp", Xexp(Version(g), update_version)}};
      for (size_t i = 1; i < patches.size(); ++i) mixed[Narrow(patches[i])] = Xexp(Version(g), update_version + 1);
      WriteFile(updates / L"Mixed" / L"TU_MIXED", MakeStfs(x360tool::content_type::kInstaller, g.title_id, mixed));
      CHECK(Add(updates / L"Mixed" / L"TU_MIXED").message.id == kTextErrorUpdateDamaged);
      CHECK(TitleUpdateInstalled(g) && TitleUpdateVersion(installed, patches[1].c_str()) == update_version);
      // The second patch removed: the game needs its update again.
      fs::remove(installed / patches[1]);
      CHECK(!TitleUpdateInstalled(g) && Entry(g).needs_update);
      CHECK(Add(updates / L"TU_16L627D_000000C000000.0000000000083").message.id == kTextAddedUpdate && TitleUpdateInstalled(g));
    } else {
      std::printf("SKIP two-executable update: %ls has one\n", g.title);
    }

    // The game removed: its update alone is refused until the game is back.
    Remove(g);
    RemoveAll(installed);
    CHECK(Add(updates / L"TU_16L627D_000000C000000.0000000000083").message.id == kTextErrorUpdateNoGame);
    CHECK(!TitleUpdateInstalled(g));

    // A folder with the copy and both forms of update, one of another version: the right one is
    // installed, the other noted.
    const fs::path both = player / L"With update";
    WriteCopy(both / L"Disc", Copy(g));
    WriteFile(both / title / L"000B0000" / L"tu00000001_00000000", tu(update_version));
    WriteFile(both / L"TU_OTHERVERSION", tu(update_version + 1));
    Outcome with = Add(both);
    CHECK(with.message.id == kTextAddedFolder);
    bool noted_added = false, noted_other = false, noted_missing = false;
    for (const Message& note : with.notes) {
      noted_added |= note.id == kTextNoteUpdateAdded;
      noted_other |= note.id == kTextNoteUpdateOther && note.args[0] == XexVersionText(update_version + 1);
      noted_missing |= note.id == kTextNeedsUpdate;
    }
    CHECK(noted_added && noted_other && !noted_missing);
    CHECK(TitleUpdateInstalled(g) && !Entry(g).needs_update);
    Remove(g);
    RemoveAll(installed);

    // Its downloadable content needs no leaving out: the game is played with its update.
    CHECK(!NeedsTitleUpdate(g, L"ANYPACKAGE"));
  }

  // A game with a pack that needs its title update (Resident Evil 6's Siege, named by its package
  // file name in game.json): the others are installed, that one is left out and noted.
  for (const CatalogGame& g : kCatalog) {
    if (!g.content || !*g.content_needs_update) continue;
    std::wstring needs_update = g.content_needs_update;
    needs_update.resize(std::min(needs_update.size(), needs_update.find(L'|')));
    std::wstring lower = needs_update;
    for (wchar_t& c : lower) c = wchar_t(towlower(c));
    CHECK(NeedsTitleUpdate(g, fs::path(L"x") / lower) && !NeedsTitleUpdate(g, L"OTHERPACKAGE"));
    const std::wstring title = Hex32(g.title_id);
    const fs::path folder = player / L"PartlyUpdate", content = folder / title / L"00000002";
    WriteFile(folder / title / L"000D0000" / L"GAMEPACKAGE", MakeStfs(x360tool::content_type::kArcadeTitle, g.title_id, Copy(g)));
    WriteFile(content / L"UNLOCKPACKAGE", MakeStfs(x360tool::content_type::kMarketplace, g.title_id, {{"data/unlock.xml", "unlock"}}));
    WriteFile(content / needs_update, MakeStfs(x360tool::content_type::kMarketplace, g.title_id, {{"archive.bpk", "cars"}}));
    Outcome outcome = Add(folder);
    CHECK(outcome.message.id == kTextAddedContent && outcome.message.args[1] == L"1");
    CHECK(outcome.notes.size() == 1 && outcome.notes[0].id == kTextNoteContentSkipped && outcome.notes[0].args[0] == L"1");
    const fs::path installed = sandbox / L"local" / L"AnyX360" / g.user_dir / L"content" / L"00000002";
    CHECK(Exists(installed / L"UNLOCKPACKAGE" / L"data" / L"unlock.xml") && !Exists(installed / needs_update));
    // Chosen on its own, it isn't taken; the other is.
    CHECK(Add(content / needs_update).message.id == kTextErrorContentNoGame);
    CHECK(Add(content / L"UNLOCKPACKAGE").message.id == kTextAddedContentOnly && !Exists(installed / needs_update));
    Remove(g);
    break;
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
      const LibraryGame entry = Entry(game);
      CHECK(entry.installed && entry.dir == ExtractedDir(game));
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

    {  // A disc's listing is long (thousands of files, megabytes of 7-Zip output): all of it is read.
      const fs::path many = sandbox / L"many";
      for (int i = 0; i < 3000; ++i)
        WriteFile(many / (L"CookedXbox360_" + std::wstring(60, L'x') + L"_" + std::to_wstring(i) + L".xxx"), "x");
      const fs::path big_listing = sandbox / L"many.7z";
      CHECK(Run7z(L"a -mx0 " + Quote(big_listing) + L" " + Quote(many / L"*")) == 0);
      ArchiveListing listing;
      int code = 0;
      CHECK(ListArchive(big_listing, std::nullopt, listing, code) == ArchiveError::kNone);
      CHECK(listing.entries.size() == 3000 && listing.size == 3000);
      RemoveAll(many);
    }

    {  // .anyx backups: saved with the downloadable content, put back as they were.
      CHECK(Add(archive).message.id == kTextAdded);  // an extracted copy
      // What a backup leaves out: the console's system update, Windows' thumbnails, the saves.
      WriteFile(ExtractedDir(game) / L"$SystemUpdate" / L"su.bin", "update");
      WriteFile(ExtractedDir(game) / L"media" / L"Thumbs.db", "thumbs");
      fs::create_directories(ExtractedDir(game) / L"empty" / L"inner");
      const fs::path content = sandbox / L"local" / L"AnyX360" / game.user_dir / L"content";
      WriteFile(content / L"00000002" / L"DLCPACKAGE" / L"songs" / L"song.bin", "song");
      WriteFile(content / L"00000001" / L"SAVE" / L"s.dat", "save");
      const fs::path backup = sandbox / L"backups" / L"game.anyx";
      fs::path partial = backup;
      partial += L".partial";
      Observer saving;
      Outcome saved = Run(saving, [&](Job& job) { return ExportAnyx(job, game, backup); });
      CHECK(saved.message.id == kTextSaved && saved.select == game.id);
      CHECK(saving.Saw(Step::kSaving) && saving.Monotonic());
      CHECK(Exists(backup) && !Exists(partial) && Clean());
      ArchiveListing listing;
      int code = 0;
      CHECK(ListArchive(backup, std::nullopt, listing, code) == ArchiveError::kNone);
      std::set<std::string> paths;
      for (const ArchiveEntry& e : listing.entries) {
        std::string path = e.path;
        std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        paths.insert(path);
      }
      CHECK(paths.count("manifest.json") && paths.count("game\\default.xex") && paths.count("game\\avatarassetpack"));
      CHECK(paths.count("content\\dlcpackage\\songs\\song.bin") && paths.count("game\\empty\\inner"));
      CHECK(!paths.count("game\\$systemupdate\\su.bin") && !paths.count("game\\media\\thumbs.db"));
      CHECK(std::none_of(paths.begin(), paths.end(), [](const std::string& p) { return p.find("s.dat") != std::string::npos; }));

      // Removed with its content, then added back from the backup.
      Remove(game);
      RemoveAll(content / L"00000002");
      fs::remove(avatar_pack);
      Observer adding;
      Outcome added = Add(backup, &adding);
      CHECK(added.message.id == kTextAddedContent && added.message.args[1] == L"1");
      CHECK(adding.Saw(Step::kVerifying) && adding.Monotonic());
      CHECK(Installed(game) && Clean());
      CHECK(Exists(ExtractedDir(game) / L"empty" / L"inner") && !Exists(ExtractedDir(game) / L"$SystemUpdate"));
      CHECK(Exists(content / L"00000002" / L"DLCPACKAGE" / L"songs" / L"song.bin"));
      CHECK(Exists(content / L"00000001" / L"SAVE" / L"s.dat") && Exists(avatar_pack));
      // Recognised by its manifest, whatever its name.
      const fs::path renamed = sandbox / L"backups" / L"backup.dat";
      fs::copy_file(backup, renamed);
      Observer keep;
      keep.replace = false;
      CHECK(Add(renamed, &keep).message.id == kTextCancelled && keep.asked == 1);

      // Changed after it was saved: refused, and the copy in the library stays.
      const fs::path unpacked = sandbox / L"unpacked";
      CHECK(Run7z(L"x -o" + Quote(unpacked) + L" " + Quote(backup)) == 0);
      std::string manifest;
      {
        std::ifstream in(unpacked / L"manifest.json", std::ios::binary);
        manifest.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      }
      auto repack = [&](const std::string& text, const fs::path& name) {
        WriteFile(unpacked / L"manifest.json", text);
        fs::remove(name);
        CHECK(Run7z(L"a " + Quote(name) + L" " + Quote(unpacked / L"*")) == 0);
        return name;
      };
      auto replace = [](std::string text, const std::string& from, const std::string& to) {
        const size_t at = text.find(from);
        if (at != std::string::npos) text.replace(at, from.size(), to);
        return text;
      };
      const std::string level = Copy(game)["media/level.bin"];
      WriteFile(unpacked / L"game" / L"media" / L"level.bin", std::string(level.size(), 'y'));  // same size, other bytes
      Outcome tampered = Add(repack(manifest, sandbox / L"tampered.anyx"));
      CHECK(tampered.message.id == kTextErrorAnyxDamaged && tampered.message.args[0] == L"game/media/level.bin");
      CHECK(Installed(game) && Clean());
      WriteFile(unpacked / L"game" / L"media" / L"level.bin", level);
      WriteFile(unpacked / L"game" / L"extra.bin", "extra");
      Outcome extra = Add(repack(manifest, sandbox / L"extra.anyx"));
      CHECK(extra.message.id == kTextErrorAnyxDamaged && extra.message.args[0] == L"game/extra.bin");
      fs::remove(unpacked / L"game" / L"extra.bin");
      Outcome newer = Add(repack(replace(manifest, "\"format_version\": 1", "\"format_version\": 3"), sandbox / L"newer.anyx"));
      CHECK(newer.message.id == kTextErrorAnyxNewer);
      Outcome version = Add(repack(replace(manifest, "\"version\": \"" + Narrow(game.xex_version) + "\"", "\"version\": \"9.9.9.9\""),
                                   sandbox / L"version.anyx"));
      CHECK(version.message.id == kTextErrorVersion && version.message.args[1] == L"9.9.9.9");
      Outcome escape = Add(repack(replace(manifest, "\"game/media/level.bin\"", "\"game/../level.bin\""), sandbox / L"escape.anyx"));
      CHECK(escape.message.id == kTextErrorAnyxDamaged);
      CHECK(Installed(game) && Clean());
      // A 7z whose manifest.json is something else's is an ordinary archive.
      CHECK(Add(repack(R"({"name": "notes"})", sandbox / L"foreign.7z")).message.id == kTextAdded);
      CHECK(Installed(game) && Clean());

      // Refused: inside the games folder (removing the game would delete it), a game not added.
      Observer refusing;
      CHECK(Run(refusing, [&](Job& job) { return ExportAnyx(job, game, GamesDir() / L"inside.anyx"); }).message.id ==
            kTextErrorSaveHere);
      for (const CatalogGame& g : kCatalog) {
        if (&g == &game || IsInstalled(g)) continue;
        CHECK(Run(refusing, [&](Job& job) { return ExportAnyx(job, g, sandbox / L"backups" / L"none.anyx"); }).message.id ==
              kTextErrorNotInLibrary);
        break;
      }
      {  // Cancelled: nothing written, the copy stays.
        struct Cancelling : Observer {
          Job* job = nullptr;
          void OnProgress(Step step, int percent) override {
            Observer::OnProgress(step, percent);
            if (step == Step::kSaving && job) job->Cancel();
          }
        } cancelling;
        Job job(cancelling);
        cancelling.job = &job;
        const fs::path never = sandbox / L"backups" / L"never.anyx";
        fs::path never_partial = never;
        never_partial += L".partial";
        CHECK(ExportAnyx(job, game, never).message.empty());
        CHECK(!Exists(never) && !Exists(never_partial) && Clean() && Installed(game));
      }

      // A copy in the player's folder: saved from there, with no link left in it; added back
      // extracted.
      CHECK(Add(player / L"My Copy").message.id == kTextAddedFolder);
      const fs::path in_place = sandbox / L"backups" / L"in-place.anyx";
      Observer from_folder;
      CHECK(Run(from_folder, [&](Job& job) { return ExportAnyx(job, game, in_place); }).message.id == kTextSaved);
      bool links = false;
      for (const auto& e : fs::recursive_directory_iterator(player / L"My Copy")) links |= IsLink(e.path());
      CHECK(!links && Exists(copy / L"default.xex") && Clean());
      Outcome back = Add(in_place);
      CHECK(back.message.id == kTextAddedContent && !Exists(LocationFile(game)) && Installed(game) && Clean());

      // A game whose packs this release leaves out (they need the title update): the game comes back
      // without them.
      for (const CatalogGame& g : kCatalog) {
        if (g.content) continue;
        const fs::path staged = sandbox / L"handmade";
        AnyxManifest m;
        m.written_by = "test";
        m.id = Narrow(g.id);
        m.title = Narrow(g.title);
        m.title_id = g.title_id, m.version = Version(g), m.media_id = g.xex_media_id;
        for (const auto& [path, data] : Copy(g)) {
          WriteFile(staged / L"game" / Widen(path), data);
          m.files.push_back({"game/" + path, data.size(), Sha256(data)});
        }
        WriteFile(staged / L"content" / L"DLCPACKAGE" / L"dlc1.id", "id");
        m.files.push_back({"content/DLCPACKAGE/dlc1.id", 2, Sha256("id")});
        m.content = {"DLCPACKAGE"};
        WriteFile(staged / L"manifest.json", WriteAnyxManifest(m));
        const fs::path handmade = sandbox / L"handmade.anyx";
        CHECK(Run7z(L"a " + Quote(handmade) + L" " + Quote(staged / L"*")) == 0);
        Outcome outcome = Add(handmade);
        CHECK(outcome.message.id == kTextAdded && outcome.notes.size() == 1 && outcome.notes[0].id == kTextNoteContentSkipped);
        CHECK(!Exists(sandbox / L"local" / L"AnyX360" / g.user_dir / L"content" / L"00000002" / L"DLCPACKAGE") && Clean());
        Remove(g);
        break;
      }
      Remove(game);
      RemoveAll(content / L"00000002");
    }

    if (updated) {  // Title updates in archives, and in .anyx backups (format version 2).
      const CatalogGame& g = *updated;
      const std::wstring title = Hex32(g.title_id);
      const fs::path installed = TitleUpdateDir(g), content = DownloadableContentDir(g);
      const auto has_note = [](const Outcome& outcome, Text id) {
        return std::any_of(outcome.notes.begin(), outcome.notes.end(), [&](const Message& m) { return m.id == id; });
      };
      // The game's package, its pack and the cache's TU_ file at the root (Kinect Adventures' Games on
      // Demand archive has this layout).
      const fs::path staged = sandbox / L"tu-archive";
      WriteFile(staged / title / L"000D0000" / L"GAMEPACKAGE", MakeStfs(x360tool::content_type::kArcadeTitle, g.title_id, Copy(g)));
      WriteFile(staged / title / L"00000002" / L"DLCPACKAGE",
                MakeStfs(x360tool::content_type::kMarketplace, g.title_id, {{"songs/song.bin", "song"}}));
      std::map<std::string, std::string> tu_files{{"GfxUI.int", "strings"}};
      for (const std::wstring& patch : TitleUpdatePatches(g)) tu_files[Narrow(patch)] = Xexp(Version(g), update_version);
      WriteFile(staged / L"TU_16L627D_000000C000000.0000000000083",
                MakeStfs(x360tool::content_type::kInstaller, g.title_id, tu_files));
      const fs::path with_update = sandbox / L"with-update.7z";
      CHECK(Run7z(L"a " + Quote(with_update) + L" " + Quote(staged / L"*")) == 0);
      Outcome outcome = Add(with_update);
      CHECK(outcome.message.id == kTextAddedContent && outcome.message.args[1] == L"1");
      CHECK(has_note(outcome, kTextNoteUpdateAdded) && !has_note(outcome, kTextNeedsUpdate));
      CHECK(Installed(g) && TitleUpdateInstalled(g) && Exists(installed / L"GfxUI.int") && Clean());

      // Saved with its update (format version 2), removed, and added back with it.
      const fs::path backup = sandbox / L"backups" / L"updated.anyx";
      Observer saving;
      CHECK(Run(saving, [&](Job& job) { return ExportAnyx(job, g, backup); }).message.id == kTextSaved);
      ArchiveListing listing;
      int code = 0;
      CHECK(ListArchive(backup, std::nullopt, listing, code) == ArchiveError::kNone);
      const std::string expected = "update\\" + Narrow(g.update_package) + "\\default.xexp";
      CHECK(std::any_of(listing.entries.begin(), listing.entries.end(), [&](const ArchiveEntry& e) { return e.path == expected; }));
      const fs::path unpacked = sandbox / L"updated-manifest";
      CHECK(Run7z(L"x -o" + Quote(unpacked) + L" " + Quote(backup) + L" manifest.json") == 0);
      std::ifstream in(unpacked / L"manifest.json", std::ios::binary);
      const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      AnyxManifest read;
      CHECK(ReadAnyxManifest(text, read) == AnyxRead::kOk);
      CHECK(read.format_version == 2 && read.update && read.update->package == Narrow(g.update_package) &&
            read.update->base_version == Version(g) && read.update->version == update_version);
      Remove(g);
      RemoveAll(installed);
      RemoveAll(content);
      Outcome back = Add(backup);
      CHECK(back.message.id == kTextAddedContent && has_note(back, kTextNoteUpdateAdded) && !has_note(back, kTextNeedsUpdate));
      CHECK(Installed(g) && TitleUpdateInstalled(g) && TitleUpdateVersion(installed) == update_version && Clean());
      CHECK(Exists(content / L"DLCPACKAGE" / L"songs" / L"song.bin"));
      Remove(g);
      RemoveAll(installed);
      RemoveAll(content);

      // A version 1 backup (saved before the game was played with its update): the copy and its
      // content come back, and the missing update is said.
      const fs::path old = sandbox / L"old-anyx";
      AnyxManifest m;
      m.written_by = "AnyX360 0.15.0";
      m.id = Narrow(g.id);
      m.title = Narrow(g.title);
      m.title_id = g.title_id, m.version = Version(g), m.media_id = g.xex_media_id;
      for (const auto& [path, data] : Copy(g)) {
        WriteFile(old / L"game" / Widen(path), data);
        m.files.push_back({"game/" + path, data.size(), Sha256(data)});
      }
      WriteFile(old / L"content" / L"DLCPACKAGE" / L"song.bin", "song");
      m.files.push_back({"content/DLCPACKAGE/song.bin", 4, Sha256("song")});
      m.content = {"DLCPACKAGE"};
      const std::string v1 = WriteAnyxManifest(m);
      CHECK(v1.find("\"format_version\": 1") != std::string::npos && v1.find("\"update\"") == std::string::npos);
      WriteFile(old / L"manifest.json", v1);
      const fs::path old_backup = sandbox / L"old.anyx";
      CHECK(Run7z(L"a " + Quote(old_backup) + L" " + Quote(old / L"*")) == 0);
      Outcome restored = Add(old_backup);
      CHECK(restored.message.id == kTextAddedContent && !restored.notes.empty() && restored.notes.back().id == kTextNeedsUpdate);
      CHECK(Installed(g) && !TitleUpdateInstalled(g) && Entry(g).needs_update && Clean());
      // Its update added after: ready.
      CHECK(Add(player / L"Updates" / L"TU_16L627D_000000C000000.0000000000083").message.id == kTextAddedUpdate);
      CHECK(!Entry(g).needs_update);
      Remove(g);
      RemoveAll(installed);
      RemoveAll(content);
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
