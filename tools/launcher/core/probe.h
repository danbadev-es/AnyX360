// What a copy is, recognised by content, not by names: default.xex and the folder holding it, disc
// images, content packages by their header (Games on Demand, Xbox Live Arcade games, downloadable
// content, title updates), archives by their signature.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "content.h"
#include "core/paths.h"
#include "core/strings.h"

struct CatalogGame;

namespace anyx360::launcher {

enum class FileKind {
  kOther,
  kXex,        // an executable (default.xex)
  kPackage,    // a content package ("CON ", "LIVE", "PIRS")
  kDiscImage,  // an Xbox or Xbox 360 disc image (XDVDFS at one of the known offsets)
  kArchive,    // a format 7-Zip opens, by its signature
};
FileKind IdentifyFile(const fs::path& file);

// The folder holding default.xex: `folder` itself or one up to 4 levels below it (archives and copies
// often have the disc inside a folder or two).
fs::path FindGameRoot(const fs::path& folder);
// An executable's execution info (optional header 0x00040006): its title, disc and version.
struct XexIdentity {
  uint32_t media_id = 0;
  uint32_t version = 0;  // major << 28 | minor << 24 | build << 8 | qfe
  uint32_t base_version = 0;
  uint32_t title_id = 0;
};
bool ReadXexIdentity(const fs::path& xex, XexIdentity& identity);
// The title ID in default.xex's header.
bool ReadTitleId(const fs::path& xex, uint32_t& title_id);
// A version as game.json writes it ("a.b.c.d") and back.
bool ParseXexVersion(std::wstring_view text, uint32_t& version);
std::wstring XexVersionText(uint32_t version);
// Which of this release's games the copy in `root` is, or an error. It must be the version the game
// was recompiled from: another version (an update, or a re-release under the same title ID) has
// other code.
const CatalogGame* IdentifyCopy(const fs::path& root, Message& error);
// A copy must be complete: the game's required files are there (its other executables, the same
// version).
bool CheckCopy(const fs::path& root, const CatalogGame& game, Message& error);

struct FoundPackage {
  fs::path path;
  x360tool::ContentPackage header;
};
// The content packages in `folder`, up to 4 levels down (a Games on Demand package's data files are
// skipped), by their headers.
std::vector<FoundPackage> FindPackages(const fs::path& folder);

// A Games on Demand copy is the console's HDD content: a package header (SVOD) with the disc's files
// in <package>.data\Data0000... (x360tool's svod.cpp reads them). An Xbox Live Arcade game is one
// STFS package. Either may come with the title's downloadable content.
bool IsGamesOnDemand(const FoundPackage& package);
bool IsArcadeGame(const FoundPackage& package);
bool IsDownloadableContent(const FoundPackage& package, uint32_t title_id);
// A title update of `title_id` (content type 0x000B0000), in either form: <TitleID>\000B0000\tu...
// or the console cache's TU_... file.
bool IsTitleUpdate(const FoundPackage& package, uint32_t title_id);

// The first Games on Demand package in `folder`, or empty.
fs::path FindGamesOnDemand(const fs::path& folder, x360tool::ContentPackage* package = nullptr);
// The title's downloadable content packages in `folder`.
std::vector<fs::path> FindDownloadableContent(const fs::path& folder, uint32_t title_id);

// Files of `kind` in `folder`, up to `max_depth` levels down, largest first.
std::vector<fs::path> FindFiles(const fs::path& folder, FileKind kind, int max_depth);

}  // namespace anyx360::launcher
