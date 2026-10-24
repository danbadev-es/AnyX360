#include "core/anyx.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "anyx360_version.h"
#include "core/catalog.h"
#include "core/hash.h"
#include "core/install.h"
#include "core/library.h"
#include "core/probe.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

namespace anyx360::launcher {
namespace {

constexpr uint64_t kMaxManifest = 16ull << 20;

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

// An archive's or a manifest's path, to compare: '/' between folders, ASCII letters in lower case
// (Windows' names don't tell case apart).
std::string Key(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  return Lower(path);
}

std::string Hex8(uint32_t value) { return Narrow(Hex32(value)); }

bool ParseHex8(const std::string& text, uint32_t& value) {
  if (text.size() != 8 || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isxdigit(c); }))
    return false;
  value = uint32_t(std::stoul(text, nullptr, 16));
  return true;
}

// A plain relative path: no drive, no "\", no empty, "." or ".." part, no control character.
bool PlainPath(const std::string& path) {
  if (path.empty() || path.front() == '/' || path.back() == '/') return false;
  for (unsigned char c : path)
    if (c < 0x20 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
      return false;
  for (size_t start = 0; start <= path.size();) {
    size_t end = path.find('/', start);
    if (end == std::string::npos) end = path.size();
    const std::string part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == "..") return false;
    start = end + 1;
  }
  return true;
}

// Whether `path` is `folder` or inside it (by the real paths, case aside).
bool Inside(const fs::path& path, const fs::path& folder) {
  std::error_code ec;
  std::wstring p = fs::weakly_canonical(fs::absolute(path, ec), ec).wstring();
  std::wstring f = fs::weakly_canonical(fs::absolute(folder, ec), ec).wstring();
  if (f.empty() || p.empty()) return false;
  if (f.back() != L'\\') f += L'\\';
  if (p.back() != L'\\') p += L'\\';
  return p.size() >= f.size() && _wcsnicmp(p.c_str(), f.c_str(), f.size()) == 0;
}

// Never in a backup: what Windows and macOS leave in folders.
bool Junk(const std::wstring& name) {
  return _wcsicmp(name.c_str(), L"Thumbs.db") == 0 || _wcsicmp(name.c_str(), L"desktop.ini") == 0 ||
         _wcsicmp(name.c_str(), L".DS_Store") == 0;
}

struct Walked {
  std::vector<std::pair<fs::path, AnyxFile>> files;  // where it is, what the manifest says
  std::vector<std::string> folders;
  uint64_t bytes = 0;
};

// The files of `folder` under `relative` in the backup, in name order. Links inside aren't followed;
// at the copy's root the console's system update ($SystemUpdate) is left out. False (with `error`) if
// a folder can't be read.
bool Walk(const fs::path& folder, const std::string& relative, bool copy_root, Walked& out, Message& error) {
  std::error_code ec;
  std::vector<fs::directory_entry> entries;
  for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
    entries.push_back(*it);
  if (ec) {
    error = {kTextErrorConvert, {Widen(ec.message()) + L": " + folder.wstring()}, true};
    return false;
  }
  std::sort(entries.begin(), entries.end(),
            [](const auto& a, const auto& b) { return _wcsicmp(a.path().filename().c_str(), b.path().filename().c_str()) < 0; });
  for (const fs::directory_entry& entry : entries) {
    const std::wstring name = entry.path().filename().wstring();
    if (IsLink(entry.path())) continue;
    const std::string path = relative + "/" + Narrow(name);
    if (entry.is_directory(ec)) {
      if (copy_root && _wcsicmp(name.c_str(), L"$SystemUpdate") == 0) continue;
      const size_t inside = out.files.size() + out.folders.size();
      if (!Walk(entry.path(), path, false, out, error)) return false;
      if (out.files.size() + out.folders.size() == inside) out.folders.push_back(path);
    } else if (entry.is_regular_file(ec) && !Junk(name)) {
      const uint64_t size = entry.file_size(ec);
      out.files.push_back({entry.path(), {path, size, {}}});
      out.bytes += size;
    }
  }
  return true;
}

// The junctions an export reads the copy and its content through, and its manifest and list:
// removed (the links only) when it ends, whatever happened.
class Staging {
 public:
  ~Staging() {
    for (auto it = links_.rbegin(); it != links_.rend(); ++it) RemoveDirectoryW(it->c_str());
    if (!dir_.empty()) RemoveAll(dir_);
  }
  // An empty staging folder at `dir`, with `game` linked to the copy; false if links can't be made
  // there (not NTFS) or to the copy (a network folder).
  bool Open(const fs::path& dir, const fs::path& copy) {
    if (!dir_.empty()) {
      RemoveAll(dir_);
      links_.clear();
    }
    dir_ = dir;
    RemoveAll(dir_);
    std::error_code ec;
    fs::create_directories(dir_ / L"content", ec);
    return Link(L"game", copy);
  }
  bool Link(const fs::path& relative, const fs::path& target) {
    std::error_code ec;
    fs::create_directories((dir_ / relative).parent_path(), ec);
    if (!CreateJunction(dir_ / relative, target)) return false;
    links_.push_back(dir_ / relative);
    return true;
  }
  const fs::path& dir() const { return dir_; }

 private:
  fs::path dir_;
  std::vector<fs::path> links_;
};

bool WriteText(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
  return bool(out);
}

}  // namespace

std::string WriteAnyxManifest(const AnyxManifest& m) {
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
  w.SetIndent(' ', 2);
  w.StartObject();
  w.Key("format"), w.String("anyx");
  w.Key("format_version"), w.Int(m.format_version);
  w.Key("written_by"), w.String(m.written_by.c_str());
  w.Key("game");
  w.StartObject();
  w.Key("id"), w.String(m.id.c_str());
  w.Key("title"), w.String(m.title.c_str());
  w.Key("title_id"), w.String(Hex8(m.title_id).c_str());
  w.Key("version"), w.String(Narrow(XexVersionText(m.version)).c_str());
  w.Key("media_id"), w.String(Hex8(m.media_id).c_str());
  w.EndObject();
  w.Key("content");
  w.StartArray();
  for (const std::string& package : m.content) w.String(package.c_str());
  w.EndArray();
  if (m.update) {
    w.Key("update");
    w.StartObject();
    w.Key("package"), w.String(m.update->package.c_str());
    w.Key("base_version"), w.String(Narrow(XexVersionText(m.update->base_version)).c_str());
    w.Key("version"), w.String(Narrow(XexVersionText(m.update->version)).c_str());
    w.EndObject();
  }
  w.Key("folders");
  w.StartArray();
  for (const std::string& folder : m.folders) w.String(folder.c_str());
  w.EndArray();
  w.Key("files");
  w.StartArray();
  for (const AnyxFile& file : m.files) {
    w.StartObject();
    w.Key("path"), w.String(file.path.c_str());
    w.Key("size"), w.Uint64(file.size);
    w.Key("sha256"), w.String(file.sha256.c_str());
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return std::string(buffer.GetString(), buffer.GetSize()) + "\n";
}

AnyxRead ReadAnyxManifest(const std::string& json, AnyxManifest& m) {
  m = {};
  rapidjson::Document doc;
  doc.Parse(json.data(), json.size());
  if (doc.HasParseError() || !doc.IsObject()) return AnyxRead::kNotAnyx;
  auto format = doc.FindMember("format");
  if (format == doc.MemberEnd() || !format->value.IsString() || std::strcmp(format->value.GetString(), "anyx") != 0)
    return AnyxRead::kNotAnyx;
  auto text = [](const rapidjson::Value& object, const char* key, std::string& out) {
    auto it = object.FindMember(key);
    if (it == object.MemberEnd() || !it->value.IsString()) return false;
    out.assign(it->value.GetString(), it->value.GetStringLength());
    return true;
  };
  text(doc, "written_by", m.written_by);
  auto version = doc.FindMember("format_version");
  if (version == doc.MemberEnd() || !version->value.IsInt() || version->value.GetInt() < 1) return AnyxRead::kDamaged;
  m.format_version = version->value.GetInt();
  // A later format may store things differently: only its version and writer are read.
  if (m.format_version > kAnyxFormatVersion) return AnyxRead::kNewer;
  auto game = doc.FindMember("game");
  if (game == doc.MemberEnd() || !game->value.IsObject()) return AnyxRead::kDamaged;
  std::string title_id, xex_version, media_id;
  if (!text(game->value, "id", m.id) || !text(game->value, "title", m.title) || !text(game->value, "title_id", title_id) ||
      !text(game->value, "version", xex_version) || !text(game->value, "media_id", media_id) ||
      !ParseHex8(title_id, m.title_id) || !ParseHex8(media_id, m.media_id) ||
      !ParseXexVersion(Widen(xex_version), m.version))
    return AnyxRead::kDamaged;
  auto strings = [&](const char* key, std::vector<std::string>& out) {
    auto it = doc.FindMember(key);
    if (it == doc.MemberEnd()) return true;
    if (!it->value.IsArray()) return false;
    for (const rapidjson::Value& item : it->value.GetArray()) {
      if (!item.IsString()) return false;
      out.emplace_back(item.GetString(), item.GetStringLength());
    }
    return true;
  };
  if (!strings("content", m.content) || !strings("folders", m.folders)) return AnyxRead::kDamaged;
  // Format version 2: the title update.
  auto update = doc.FindMember("update");
  if (update != doc.MemberEnd()) {
    AnyxUpdate u;
    std::string base_version, update_version;
    if (m.format_version < 2 || !update->value.IsObject() || !text(update->value, "package", u.package) ||
        !text(update->value, "base_version", base_version) || !text(update->value, "version", update_version) ||
        !ParseXexVersion(Widen(base_version), u.base_version) || !ParseXexVersion(Widen(update_version), u.version))
      return AnyxRead::kDamaged;
    m.update = std::move(u);
  }
  auto files = doc.FindMember("files");
  if (files == doc.MemberEnd() || !files->value.IsArray()) return AnyxRead::kDamaged;
  for (const rapidjson::Value& item : files->value.GetArray()) {
    AnyxFile file;
    if (!item.IsObject() || !text(item, "path", file.path) || !text(item, "sha256", file.sha256)) return AnyxRead::kDamaged;
    auto size = item.FindMember("size");
    if (size == item.MemberEnd() || !size->value.IsUint64()) return AnyxRead::kDamaged;
    file.size = size->value.GetUint64();
    if (file.sha256.size() != 64 ||
        !std::all_of(file.sha256.begin(), file.sha256.end(), [](unsigned char c) { return std::isxdigit(c); }))
      return AnyxRead::kDamaged;
    file.sha256 = Lower(file.sha256);
    m.files.push_back(std::move(file));
  }
  return AnyxRead::kOk;
}

bool CheckAnyxPaths(const AnyxManifest& m, std::string& bad) {
  std::set<std::string> packages;
  for (const std::string& package : m.content) {
    if (!PlainPath(package) || package.find('/') != std::string::npos || !packages.insert(Lower(package)).second) {
      bad = "content/" + package;
      return false;
    }
  }
  if (m.update && (!PlainPath(m.update->package) || m.update->package.find('/') != std::string::npos)) {
    bad = "update/" + m.update->package;
    return false;
  }
  // Under game/, under a listed package's folder, or under the update's.
  auto placed = [&](const std::string& path) {
    if (!PlainPath(path)) return false;
    if (path.rfind("game/", 0) == 0) return true;
    if (m.update && path.rfind("update/", 0) == 0) {
      const size_t slash = path.find('/', 7);
      return slash != std::string::npos && Lower(path.substr(7, slash - 7)) == Lower(m.update->package);
    }
    if (path.rfind("content/", 0) != 0) return false;
    const size_t slash = path.find('/', 8);
    return slash != std::string::npos && packages.count(Lower(path.substr(8, slash - 8)));
  };
  std::set<std::string> seen;
  for (const AnyxFile& file : m.files) {
    if (!placed(file.path) || !seen.insert(Key(file.path)).second) {
      bad = file.path;
      return false;
    }
  }
  for (const std::string& folder : m.folders) {
    if (!placed(folder)) {
      bad = folder;
      return false;
    }
  }
  if (!seen.count("game/default.xex")) {
    bad = "game/default.xex";
    return false;
  }
  return true;
}

bool CheckAnyxListing(const ArchiveListing& listing, const AnyxManifest& m, std::string& bad) {
  std::map<std::string, uint64_t> expected;
  for (const AnyxFile& file : m.files) expected[Key(file.path)] = file.size;
  size_t found = 0;
  for (const ArchiveEntry& e : listing.entries) {
    if (e.folder) continue;
    const std::string key = Key(e.path);
    if (key == "manifest.json") continue;
    auto it = expected.find(key);
    if (it == expected.end() || it->second != e.size) {
      bad = e.path;
      std::replace(bad.begin(), bad.end(), '\\', '/');
      return false;
    }
    ++found;
  }
  if (found != expected.size()) {
    // A file the manifest names isn't there.
    for (const AnyxFile& file : m.files) {
      bool there = false;
      for (const ArchiveEntry& e : listing.entries) there |= !e.folder && Key(e.path) == Key(file.path);
      if (!there) {
        bad = file.path;
        break;
      }
    }
    return false;
  }
  return true;
}

bool LooksLikeAnyx(const ArchiveListing& listing) {
  bool manifest = false, xex = false;
  for (const ArchiveEntry& e : listing.entries) {
    if (e.folder) continue;
    const std::string key = Key(e.path);
    manifest |= key == "manifest.json" && e.size < kMaxManifest;
    xex |= key == "game/default.xex";
  }
  return manifest && xex;
}

bool CreateJunction(const fs::path& link, const fs::path& target) {
  std::error_code ec;
  std::wstring absolute = fs::absolute(target, ec).wstring();
  if (ec || absolute.size() < 3 || absolute[1] != L':') return false;  // a local drive only (not \\server\share)
  while (absolute.size() > 3 && absolute.back() == L'\\') absolute.pop_back();
  if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
  // A mount point reparse buffer (ntifs.h's REPARSE_DATA_BUFFER): the target as "\??\C:\..." and as
  // shown, each followed by a null.
  const std::wstring substitute = L"\\??\\" + absolute;
  const USHORT substitute_bytes = USHORT(substitute.size() * sizeof(wchar_t));
  const USHORT print_bytes = USHORT(absolute.size() * sizeof(wchar_t));
  struct Header {
    DWORD tag;
    USHORT data_length;
    USHORT reserved;
    USHORT substitute_offset, substitute_length, print_offset, print_length;
  };
  std::vector<char> buffer(sizeof(Header) + substitute_bytes + print_bytes + 2 * sizeof(wchar_t));
  Header header{IO_REPARSE_TAG_MOUNT_POINT,
                USHORT(buffer.size() - 8),
                0,
                0,
                substitute_bytes,
                USHORT(substitute_bytes + sizeof(wchar_t)),
                print_bytes};
  std::memcpy(buffer.data(), &header, sizeof(header));
  std::memcpy(buffer.data() + sizeof(Header), substitute.c_str(), substitute_bytes);
  std::memcpy(buffer.data() + sizeof(Header) + substitute_bytes + sizeof(wchar_t), absolute.c_str(), print_bytes);
  HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  DWORD returned = 0;
  const bool made = handle != INVALID_HANDLE_VALUE &&
                    DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), DWORD(buffer.size()), nullptr, 0,
                                    &returned, nullptr);
  if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  if (!made) RemoveDirectoryW(link.c_str());
  return made;
}

namespace {

Outcome Export(Job& job, const CatalogGame& game, const fs::path& out) {
  job.Found(game);
  job.Report(Step::kSaving, 0);
  // The copy: extracted here, or in the player's folder.
  fs::path source = ExtractedDir(game);
  if (!Exists(source / L"default.xex")) {
    source = StoredLocation(game);
    if (source.empty()) return {{kTextErrorNotInLibrary, {game.title}, true}};
    if (!Exists(source / L"default.xex")) return {{kTextErrorLocationGone, {game.title, ShortPath(source)}, true}};
  }
  Message error;
  const CatalogGame* identified = IdentifyCopy(source, error);
  if (identified != &game) return {identified ? Message{kTextErrorNotGame, {}, true} : error};
  if (!CheckCopy(source, game, error)) return {error};
  XexIdentity identity;
  ReadXexIdentity(source / L"default.xex", identity);
  // Not where removing the game or preparing it again would delete it, nor inside the copy.
  std::error_code ec;
  const fs::path target = fs::absolute(out, ec);
  if (ec || target.filename().empty() || Inside(target, GamesDir()) || Inside(target, source))
    return {{kTextErrorSaveHere, {}, true}};

  // What goes in: the copy, then each downloadable content package the game takes.
  Walked walked;
  if (!Walk(source, "game", true, walked, error)) return {error};
  AnyxManifest manifest;
  const fs::path content = DownloadableContentDir(game);
  std::vector<fs::directory_entry> packages;
  for (auto it = fs::directory_iterator(content, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
    if (it->is_directory(ec) && !IsLink(it->path()) && !NeedsTitleUpdate(game, it->path())) packages.push_back(*it);
  std::sort(packages.begin(), packages.end(), [](const auto& a, const auto& b) { return a.path() < b.path(); });
  for (const fs::directory_entry& package : packages) {
    Walked one;
    const std::string name = Narrow(package.path().filename().wstring());
    if (!Walk(package.path(), "content/" + name, false, one, error)) return {error};
    if (one.files.empty()) continue;  // a package that was never extracted
    manifest.content.push_back(name);
    walked.files.insert(walked.files.end(), one.files.begin(), one.files.end());
    walked.folders.insert(walked.folders.end(), one.folders.begin(), one.folders.end());
    walked.bytes += one.bytes;
  }
  // The title update the game is played with, if it is installed (format version 2).
  const fs::path update = TitleUpdateDir(game);
  if (UsesTitleUpdate(game) && TitleUpdateInstalled(game) && !IsLink(update)) {
    Walked one;
    const std::string name = Narrow(game.update_package);
    if (!Walk(update, "update/" + name, false, one, error)) return {error};
    AnyxUpdate saved;
    saved.package = name;
    saved.base_version = identity.version;
    saved.version = TitleUpdateVersion(update);
    manifest.update = saved;
    manifest.format_version = 2;
    walked.files.insert(walked.files.end(), one.files.begin(), one.files.end());
    walked.folders.insert(walked.folders.end(), one.folders.begin(), one.folders.end());
    walked.bytes += one.bytes;
  }

  // Room for it, on a drive that holds files that big.
  fs::create_directories(target.parent_path(), ec);
  if (!EnoughSpaceFor({{target.parent_path(), walked.bytes + (64ull << 20)}}, error)) return {error};
  wchar_t volume[MAX_PATH] = {}, file_system[32] = {};
  if (walked.bytes + (1ull << 20) >= (4ull << 30) && GetVolumePathNameW(target.parent_path().c_str(), volume, MAX_PATH) &&
      GetVolumeInformationW(volume, nullptr, 0, nullptr, nullptr, nullptr, file_system, 32) &&
      (_wcsicmp(file_system, L"FAT32") == 0 || _wcsicmp(file_system, L"FAT") == 0))
    return {{kTextErrorFat32, {volume}, true}};

  // The manifest: every file's hash (0-30 %).
  uint64_t hashed = 0;
  int last = 0;
  for (auto& [path, file] : walked.files) {
    file.sha256 = Sha256File(path, [&](uint64_t bytes) {
      const int percent = walked.bytes ? int((hashed + bytes) * 30 / walked.bytes) : 30;
      if (percent > last) job.Report(Step::kSaving, last = percent);
      return !job.Cancelled();
    });
    if (job.Cancelled()) return {};
    if (file.sha256.empty()) return {{kTextErrorConvert, {path.wstring()}, true}};
    hashed += file.size;
    manifest.files.push_back(file);
  }
  manifest.folders = walked.folders;
  manifest.written_by = "AnyX360 " ANYX360_VERSION_FULL;
  manifest.id = Narrow(game.id);
  manifest.title = Narrow(game.title);
  manifest.title_id = identity.title_id;
  manifest.version = identity.version;
  manifest.media_id = identity.media_id;

  // 7-Zip reads the copy and the packages through links in a staging folder (both under the paths the
  // backup gives them), with the manifest and the list of what to take. On a drive without links
  // (not NTFS), the staging goes to the temporary folder.
  Staging staging;
  wchar_t temp[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, temp);
  if (!staging.Open(WorkDir() / L"export", source) &&
      !staging.Open(fs::path(temp) / (L"AnyX360-export-" + std::to_wstring(GetCurrentProcessId())), source))
    return {{kTextErrorSaveLink, {ShortPath(source)}, true}};
  for (const std::string& package : manifest.content)
    if (!staging.Link(fs::path(L"content") / Widen(package), content / Widen(package)))
      return {{kTextErrorSaveLink, {ShortPath(content)}, true}};
  if (manifest.update && !staging.Link(fs::path(L"update") / Widen(manifest.update->package), update))
    return {{kTextErrorSaveLink, {ShortPath(update)}, true}};
  std::string list = "manifest.json\n";
  auto add = [&](std::string path) {
    std::replace(path.begin(), path.end(), '/', '\\');
    list += path + "\n";
  };
  for (const AnyxFile& file : manifest.files) add(file.path);
  for (const std::string& folder : manifest.folders) add(folder);
  if (!WriteText(staging.dir() / L"manifest.json", WriteAnyxManifest(manifest)) || !WriteText(staging.dir() / L"files.lst", list))
    return {{kTextErrorSaveFailed, {L"manifest"}, true}};

  // The archive (30-99 %), written beside the file and renamed once it is complete and checked.
  fs::path partial = target;
  partial += L".partial";
  DeleteFileW(partial.c_str());
  int code = 0;
  const bool written = CreateArchive(job, partial, staging.dir(), staging.dir() / L"files.lst",
                                     [&](int percent) { job.Report(Step::kSaving, std::min(30 + percent * 69 / 100, 99)); },
                                     code);
  auto fail = [&](Message message) {
    DeleteFileW(partial.c_str());
    return Outcome{std::move(message)};
  };
  if (job.Cancelled()) return fail({});
  if (!written) return fail({kTextErrorSaveFailed, {std::to_wstring(code)}, true});
  ArchiveListing listing;
  std::string bad;
  if (ListArchive(partial, std::nullopt, listing, code) != ArchiveError::kNone || !CheckAnyxListing(listing, manifest, bad))
    return fail({kTextErrorSaveFailed, {bad.empty() ? std::to_wstring(code) : Widen(bad)}, true});
  if (!MoveFileExW(partial.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return fail({kTextErrorSaveFailed, {std::to_wstring(GetLastError())}, true});
  job.Report(Step::kFinishing, 100);
  return {{kTextSaved, {game.title, ShortPath(target), Gigabytes(fs::file_size(target, ec))}, false}, game.id};
}

}  // namespace

Outcome ExportAnyx(Job& job, const CatalogGame& game, const fs::path& out) {
  RemoveAll(WorkDir());
  Outcome outcome = Export(job, game, out);
  RemoveAll(WorkDir());
  return outcome;
}

Outcome ImportAnyx(Job& job, const fs::path& archive, const ArchiveListing& listing, const fs::path& work, int from,
                   bool& handled) {
  handled = false;
  // The manifest alone first.
  std::string manifest_entry;
  for (const ArchiveEntry& e : listing.entries)
    if (!e.folder && Key(e.path) == "manifest.json") manifest_entry = e.path;
  const fs::path probe = work / L"manifest";
  int code = 0;
  ArchiveError error = ExtractArchive(job, archive, job.Password(), probe, {manifest_entry}, from, from, code);
  if (job.Cancelled()) {
    handled = true;
    return {};
  }
  if (error != ArchiveError::kNone) return {};
  std::ifstream in(probe / Widen(manifest_entry), std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  AnyxManifest manifest;
  switch (ReadAnyxManifest(text.str(), manifest)) {
    case AnyxRead::kNotAnyx:
      return {};
    case AnyxRead::kNewer:
      handled = true;
      return {{kTextErrorAnyxNewer, {Widen(manifest.written_by)}, true}};
    case AnyxRead::kDamaged:
      handled = true;
      return {{kTextErrorAnyxDamaged, {L"manifest.json"}, true}};
    case AnyxRead::kOk:
      break;
  }
  handled = true;

  // Which game, of the version this release plays, and the archive as the manifest describes it.
  const CatalogGame* game = FindAvailableGame(manifest.title_id);
  if (!game) return {{kTextErrorUnsupported, {Hex32(manifest.title_id), JoinTitles(AvailableGames())}, true}};
  uint32_t expected = 0;
  if (ParseXexVersion(game->xex_version, expected) && manifest.version != expected)
    return {{kTextErrorVersion, {game->title, XexVersionText(manifest.version), game->xex_version}, true}};
  std::string bad;
  if (!CheckAnyxPaths(manifest, bad) || !CheckAnyxListing(listing, manifest, bad))
    return {{kTextErrorAnyxDamaged, {Widen(bad)}, true}};
  if (!job.ConfirmReplace(*game)) return {{kTextCancelled, {}, false}};
  job.Found(*game);
  uint64_t copy_bytes = 0, content_bytes = 0;
  for (const AnyxFile& file : manifest.files) (file.path.rfind("game/", 0) == 0 ? copy_bytes : content_bytes) += file.size;
  Message message;
  if (!EnoughSpaceFor({{Root(), copy_bytes + content_bytes + (512ull << 20)}, {LocalAppData(), content_bytes + (64ull << 20)}},
                      message))
    return {message};

  // Extracted (to 70 %), then every file checked against its hash (to 99 %).
  const fs::path files = work / L"files";
  job.Progress(from);
  error = ExtractArchive(job, archive, job.Password(), files, {}, from, 70, code);
  if (job.Cancelled()) return {};
  if (error != ArchiveError::kNone) return {{kTextErrorExtract, {std::to_wstring(code)}, true}};
  const uint64_t total = copy_bytes + content_bytes;
  uint64_t checked = 0;
  int last = 70;
  for (const AnyxFile& file : manifest.files) {
    const std::string hash = Sha256File(files / Widen(file.path), [&](uint64_t bytes) {
      const int percent = 70 + int(total ? (checked + bytes) * 29 / total : 29);
      if (percent > last) job.Report(Step::kVerifying, last = percent);
      return !job.Cancelled();
    });
    if (job.Cancelled()) return {};
    if (hash != file.sha256) return {{kTextErrorAnyxDamaged, {Widen(file.path)}, true}};
    checked += file.size;
  }
  std::error_code ec;
  for (const std::string& folder : manifest.folders) fs::create_directories(files / Widen(folder), ec);

  // The copy in place (the real default.xex says which game it is), then its content.
  const fs::path copy = files / L"game";
  const CatalogGame* identified = IdentifyCopy(copy, message);
  if (identified != game) return {identified ? Message{kTextErrorNotGame, {}, true} : message};
  if (!CheckCopy(copy, *game, message) || !InstallCopy(*game, copy, message)) return {message};
  job.Progress(100);
  SaveIcon(*game, ExtractedDir(*game));
  InstallAvatarPack(ExtractedDir(*game));
  int installed = 0, skipped = 0;
  for (const std::string& package : manifest.content) {
    const fs::path name = Widen(package);
    // A pack this release leaves out (it needs the title update) isn't put back.
    if (NeedsTitleUpdate(*game, name)) {
      ++skipped;
      continue;
    }
    const fs::path destination = DownloadableContentDir(*game) / name;
    RemoveAll(destination);
    if (MoveFolder(files / L"content" / name, destination)) ++installed;
  }
  Outcome outcome{installed ? Message{kTextAddedContent, {game->title, std::to_wstring(installed)}, false}
                            : Message{kTextAdded, {game->title}, false},
                  game->id};
  if (skipped) outcome.notes.push_back({kTextNoteContentSkipped, {std::to_wstring(skipped)}, false});
  // The title update (format version 2): back in place if it is the one the game is played with.
  if (manifest.update) {
    uint32_t expected = 0;
    ParseXexVersion(game->update_version, expected);
    if (!UsesTitleUpdate(*game)) {
      outcome.notes.push_back({kTextNoteUpdatesSkipped, {L"1"}, false});
    } else if (manifest.update->version != expected) {
      outcome.notes.push_back({kTextNoteUpdateOther, {XexVersionText(manifest.update->version), game->update_version}, false});
    } else {
      const fs::path destination = TitleUpdateDir(*game);
      RemoveAll(destination);
      if (MoveFolder(files / L"update" / Widen(manifest.update->package), destination))
        outcome.notes.push_back({kTextNoteUpdateAdded, {game->update_version}, false});
    }
  }
  // An older backup (or one saved without it) of a game now played with its update.
  if (!TitleUpdateInstalled(*game)) outcome.notes.push_back({kTextNeedsUpdate, {game->title, game->update_version}, false});
  return outcome;
}

}  // namespace anyx360::launcher
