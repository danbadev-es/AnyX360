#include "core/probe.h"

#include <algorithm>
#include <cwctype>
#include <fstream>

#include "core/catalog.h"

namespace anyx360::launcher {

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

// The execution info (optional header 0x00040006) holds the title ID at offset 0xC. All big-endian.
bool ReadTitleId(const fs::path& xex, uint32_t& title_id) {
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
    title_id = be32(info + 0xC);
    return true;
  }
  return false;
}

const CatalogGame* IdentifyCopy(const fs::path& root, Message& error) {
  uint32_t title_id = 0;
  if (!ReadTitleId(root / L"default.xex", title_id)) {
    error = {kTextErrorNotGame, {}, true};
    return nullptr;
  }
  const CatalogGame* game = FindAvailableGame(title_id);
  if (!game) error = {kTextErrorUnsupported, {Hex32(title_id), JoinTitles(AvailableGames())}, true};
  return game;
}

bool CheckCopy(const fs::path& root, const CatalogGame& game, Message& error) {
  std::wstring files = game.required_files;
  for (size_t start = 0; start < files.size();) {
    size_t end = files.find(L'|', start);
    if (end == std::wstring::npos) end = files.size();
    std::wstring file = files.substr(start, end - start);
    start = end + 1;
    if (!file.empty() && !Exists(root / file)) {
      error = {kTextErrorMissingFile, {file}, true};
      return false;
    }
  }
  return true;
}

fs::path FindGamesOnDemand(const fs::path& folder, x360tool::ContentPackage* package) {
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it.depth() > 4) {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec) || it->path().parent_path().filename() != L"00007000") continue;
    fs::path data = it->path();
    data += L".data";
    if (!Exists(data)) continue;
    auto header = x360tool::read_content_header(it->path());
    if (!header || header->content_type != x360tool::content_type::kGamesOnDemand ||
        header->volume_type != x360tool::kVolumeSvod)
      continue;
    if (package) *package = *header;
    return it->path();
  }
  return {};
}

std::vector<fs::path> FindDownloadableContent(const fs::path& folder, uint32_t title_id) {
  std::vector<fs::path> packages;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it.depth() > 4) {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec) || it->path().parent_path().filename() != L"00000002") continue;
    auto header = x360tool::read_content_header(it->path());
    if (header && header->content_type == x360tool::content_type::kMarketplace && header->title_id == title_id &&
        header->volume_type == x360tool::kVolumeStfs)
      packages.push_back(it->path());
  }
  return packages;
}

}  // namespace anyx360::launcher
