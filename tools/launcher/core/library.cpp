#include "core/library.h"

#include <algorithm>
#include <cwctype>

#include "core/catalog.h"
#include "core/install.h"
#include "util.h"
#include "xdbf.h"
#include "xex.h"

namespace anyx360::launcher {

std::vector<LibraryGame> ReadLibrary() {
  std::vector<LibraryGame> games;
  for (const CatalogGame& info : kCatalog) {
    LibraryGame game;
    game.info = &info;
    game.exe = FindGameExe(info);
    if (game.exe.empty()) continue;
    if (Exists(ExtractedDir(info) / L"default.xex")) {
      game.installed = true;
      game.dir = ExtractedDir(info);
    } else if (Exists(LocationFile(info))) {
      game.installed = true;
      game.dir = StoredLocation(info);
      game.location_gone = !Exists(game.dir / L"default.xex");
    }
    if (game.installed) {
      game.needs_update = !TitleUpdateInstalled(info);
      if (Exists(IconFile(info))) game.icon = IconFile(info);
      game.cover = CoverFile(info);
      game.needs_icon = game.icon.empty() && !game.location_gone;
    }
    games.push_back(std::move(game));
  }
  return games;
}

bool SaveIcon(const CatalogGame& game, const fs::path& game_dir) {
  try {
    x360tool::Xex xex(x360tool::read_file(game_dir / L"default.xex"));
    fs::path key = KeyFile();
    auto image = xex.extract_image(key.empty() ? std::nullopt : x360tool::load_key(key, false));
    auto png = x360tool::xex_title_image(xex, image);
    if (!png) return false;
    std::error_code ec;
    fs::create_directories(GameRoot(game), ec);
    x360tool::write_file(IconFile(game), *png);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::optional<x360::achievements::TitleList> ReadAchievements(const LibraryGame& game, const fs::path& folder) {
  const uint32_t title_id = game.info->title_id;
  if (auto cached = x360::achievements::ReadTitleList(folder, title_id)) return cached;
  if (!game.installed || game.location_gone) return std::nullopt;
  try {
    x360tool::Xex xex(x360tool::read_file(game.dir / L"default.xex"));
    fs::path key = KeyFile();
    auto image = xex.extract_image(key.empty() ? std::nullopt : x360tool::load_key(key, false));
    x360::achievements::TitleList list;
    list.title_id = title_id;
    std::map<uint32_t, std::vector<uint8_t>> images;
    if (auto xdbf = x360tool::xex_title_xdbf(xex, image)) list = x360tool::xdbf_title_list(*xdbf, title_id, &images);
    if (!list.achievements.empty()) x360::achievements::WriteTitleList(folder, list, images);
    return list;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool SetPlayerCover(const CatalogGame& game, const fs::path& image) {
  std::wstring extension = image.extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
  fs::path target = GameRoot(game) / (extension == L".png" ? L"cover.png" : L"cover.jpg");
  RestoreCover(game);
  std::error_code ec;
  fs::create_directories(GameRoot(game), ec);
  return fs::copy_file(image, target, fs::copy_options::overwrite_existing, ec) && !ec;
}

void RestoreCover(const CatalogGame& game) {
  std::error_code ec;
  for (const wchar_t* name : {L"cover.png", L"cover.jpg"}) fs::remove(GameRoot(game) / name, ec);
}

}  // namespace anyx360::launcher
