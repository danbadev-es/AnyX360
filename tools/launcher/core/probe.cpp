#include "core/probe.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <fstream>

#include "core/catalog.h"
#include "xdvdfs.h"

namespace anyx360::launcher {
namespace {

// Walks `folder` up to `max_depth` levels down, skipping Games on Demand data folders.
template <typename Visit>
void Walk(const fs::path& folder, int max_depth, Visit visit) {
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it->is_directory(ec)) {
      std::wstring name = it->path().filename().wstring();
      if (it.depth() >= max_depth || (name.size() > 5 && _wcsicmp(name.c_str() + name.size() - 5, L".data") == 0))
        it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(ec)) visit(it->path());
  }
}

}  // namespace

FileKind IdentifyFile(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  uint8_t head[264] = {};
  in.read(reinterpret_cast<char*>(head), sizeof(head));
  size_t size = size_t(in.gcount());
  auto starts = [&](const char* magic, size_t length, size_t at = 0) {
    return size >= at + length && std::memcmp(head + at, magic, length) == 0;
  };
  if (starts("XEX2", 4)) return FileKind::kXex;
  if (starts("CON ", 4) || starts("LIVE", 4) || starts("PIRS", 4)) return FileKind::kPackage;
  if (starts("7z\xBC\xAF\x27\x1C", 6) || starts("Rar!\x1A\x07", 6) || starts("PK\x03\x04", 4) ||
      starts("PK\x05\x06", 4) || starts("PK\x07\x08", 4) || starts("\x1F\x8B", 2) || starts("BZh", 3) ||
      starts("\xFD" "7zXZ\0", 6) || starts("\x28\xB5\x2F\xFD", 4) || starts("MSCF", 4) || starts("MSWIM\0\0\0", 8) ||
      starts("ustar", 5, 257))
    return FileKind::kArchive;
  // The smallest disc image holds its volume descriptor at 64 KB; real ones are gigabytes.
  std::error_code ec;
  if (fs::file_size(file, ec) >= (1u << 20) && !ec && x360tool::XdvdfsImage::is_disc_image(file))
    return FileKind::kDiscImage;
  return FileKind::kOther;
}

fs::path FindGameRoot(const fs::path& folder) {
  if (Exists(folder / L"default.xex")) return folder;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it.depth() > 4) {
      it.disable_recursion_pending();
      continue;
    }
    std::wstring name = it->path().filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(), ::towlower);
    if (name == L"default.xex" && it->is_regular_file(ec)) return it->path().parent_path();
  }
  return {};
}

// The execution info (optional header 0x00040006): the media ID at offset 0, the version at 4, the
// base version at 8 and the title ID at 0xC. All big-endian.
bool ReadXexIdentity(const fs::path& xex, XexIdentity& identity) {
  std::ifstream in(xex, std::ios::binary);
  std::vector<uint8_t> header(0x10000);
  in.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size()));
  size_t size = size_t(in.gcount());
  auto be32 = [&](size_t at) -> uint32_t {
    if (at + 4 > size) return 0;
    return uint32_t(header[at]) << 24 | uint32_t(header[at + 1]) << 16 | uint32_t(header[at + 2]) << 8 | header[at + 3];
  };
  if (be32(0) != 0x58455832) return false;  // 'XEX2'
  uint32_t count = std::min<uint32_t>(be32(0x14), 256);
  for (uint32_t i = 0; i < count; ++i) {
    if (be32(0x18 + i * 8) != 0x00040006) continue;
    uint32_t info = be32(0x1C + i * 8);
    if (size_t(info) + 0x10 > size) return false;
    identity = {be32(info), be32(info + 4), be32(info + 8), be32(info + 0xC)};
    return true;
  }
  return false;
}

bool ReadTitleId(const fs::path& xex, uint32_t& title_id) {
  XexIdentity identity;
  if (!ReadXexIdentity(xex, identity)) return false;
  title_id = identity.title_id;
  return true;
}

bool ParseXexVersion(std::wstring_view text, uint32_t& version) {
  unsigned major = 0, minor = 0, build = 0, qfe = 0;
  wchar_t extra = 0;
  if (swscanf_s(std::wstring(text).c_str(), L"%u.%u.%u.%u%c", &major, &minor, &build, &qfe, &extra, 1) != 4 ||
      major > 0xF || minor > 0xF || build > 0xFFFF || qfe > 0xFF)
    return false;
  version = major << 28 | minor << 24 | build << 8 | qfe;
  return true;
}

std::wstring XexVersionText(uint32_t version) {
  return std::to_wstring(version >> 28) + L"." + std::to_wstring((version >> 24) & 0xF) + L"." +
         std::to_wstring((version >> 8) & 0xFFFF) + L"." + std::to_wstring(version & 0xFF);
}

namespace {

// An executable of `game` of the version it was recompiled from, else the error.
bool SameVersion(const CatalogGame& game, const XexIdentity& identity, Message& error) {
  uint32_t expected = 0;
  if (!ParseXexVersion(game.xex_version, expected) || identity.version == expected) return true;
  error = {kTextErrorVersion, {game.title, XexVersionText(identity.version), game.xex_version}, true};
  return false;
}

}  // namespace

const CatalogGame* IdentifyCopy(const fs::path& root, Message& error) {
  XexIdentity identity;
  if (!ReadXexIdentity(root / L"default.xex", identity)) {
    error = {kTextErrorNotGame, {}, true};
    return nullptr;
  }
  const CatalogGame* game = FindAvailableGame(identity.title_id);
  if (!game) {
    error = {kTextErrorUnsupported, {Hex32(identity.title_id), JoinTitles(AvailableGames())}, true};
    return nullptr;
  }
  return SameVersion(*game, identity, error) ? game : nullptr;
}

bool CheckCopy(const fs::path& root, const CatalogGame& game, Message& error) {
  std::wstring files = game.required_files;
  for (size_t start = 0; start < files.size();) {
    size_t end = files.find(L'|', start);
    if (end == std::wstring::npos) end = files.size();
    std::wstring file = files.substr(start, end - start);
    start = end + 1;
    if (file.empty()) continue;
    if (!Exists(root / file)) {
      error = {kTextErrorMissingFile, {file}, true};
      return false;
    }
    // The game's other executables (multiplayer) come from the same disc.
    XexIdentity identity;
    if (file.size() > 4 && _wcsicmp(file.c_str() + file.size() - 4, L".xex") == 0 &&
        (!ReadXexIdentity(root / file, identity) || !SameVersion(game, identity, error))) {
      if (error.empty()) error = {kTextErrorMissingFile, {file}, true};
      return false;
    }
  }
  return true;
}

std::vector<FoundPackage> FindPackages(const fs::path& folder) {
  std::vector<FoundPackage> packages;
  Walk(folder, 4, [&](const fs::path& file) {
    // read_content_header checks the magic first.
    if (auto header = x360tool::read_content_header(file)) packages.push_back({file, *header});
  });
  return packages;
}

bool IsGamesOnDemand(const FoundPackage& package) {
  fs::path data = package.path;
  data += L".data";
  return package.header.content_type == x360tool::content_type::kGamesOnDemand &&
         package.header.volume_type == x360tool::kVolumeSvod && Exists(data);
}

bool IsArcadeGame(const FoundPackage& package) {
  return package.header.content_type == x360tool::content_type::kArcadeTitle &&
         package.header.volume_type == x360tool::kVolumeStfs;
}

bool IsDownloadableContent(const FoundPackage& package, uint32_t title_id) {
  return package.header.content_type == x360tool::content_type::kMarketplace &&
         package.header.title_id == title_id && package.header.volume_type == x360tool::kVolumeStfs;
}

bool IsTitleUpdate(const FoundPackage& package, uint32_t title_id) {
  return package.header.content_type == x360tool::content_type::kInstaller && package.header.title_id == title_id &&
         package.header.volume_type == x360tool::kVolumeStfs;
}

fs::path FindGamesOnDemand(const fs::path& folder, x360tool::ContentPackage* package) {
  for (const FoundPackage& found : FindPackages(folder)) {
    if (!IsGamesOnDemand(found)) continue;
    if (package) *package = found.header;
    return found.path;
  }
  return {};
}

std::vector<fs::path> FindDownloadableContent(const fs::path& folder, uint32_t title_id) {
  std::vector<fs::path> packages;
  for (const FoundPackage& found : FindPackages(folder))
    if (IsDownloadableContent(found, title_id)) packages.push_back(found.path);
  return packages;
}

std::vector<fs::path> FindFiles(const fs::path& folder, FileKind kind, int max_depth) {
  std::vector<std::pair<uint64_t, fs::path>> found;
  Walk(folder, max_depth, [&](const fs::path& file) {
    if (IdentifyFile(file) != kind) return;
    std::error_code ec;
    found.push_back({fs::file_size(file, ec), file});
  });
  std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<fs::path> files;
  for (auto& [size, file] : found) files.push_back(std::move(file));
  return files;
}

}  // namespace anyx360::launcher
