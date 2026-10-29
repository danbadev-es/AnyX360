// .anyx: a backup of a prepared game, to take it back on this PC or another of the player's own
// (docs/ANYX.md). A 7z, stored without compression, holding manifest.json, game\ (the copy, as in
// games\<id>\game), content\<package>\ (the title's downloadable content, extracted as the
// launcher installs it) and, from format version 2, update\<package>\ (the title update a game is
// played with, extracted). Never saves, keys or covers. It is recognised by its manifest, not by its
// name, and every file is checked against the manifest's hashes when it is added back.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/jobs.h"
#include "core/sevenzip.h"

struct CatalogGame;

namespace anyx360::launcher {

// The newest format this release reads. It writes 2 for a backup with a title update and 1 for one
// without (so earlier releases still read those).
constexpr int kAnyxFormatVersion = 2;

struct AnyxFile {
  std::string path;  // UTF-8, '/' between folders: game/..., content/<package>/... or update/<package>/...
  uint64_t size = 0;
  std::string sha256;  // lowercase hex
};

// The title update in a backup (format version 2): its package's folder name, the version it updates
// (the copy's default.xex) and the one it makes.
struct AnyxUpdate {
  std::string package;
  uint32_t base_version = 0, version = 0;
};

struct AnyxManifest {
  int format_version = 1;
  std::string written_by;  // "AnyX360 0.14.0+1a2b3c4"
  std::string id, title;   // the catalog's
  uint32_t title_id = 0, version = 0, media_id = 0;  // default.xex's execution info
  std::vector<AnyxFile> files;
  std::vector<std::string> folders;  // empty folders, as the files' paths
  std::vector<std::string> content;  // the downloadable content packages (their folder names)
  // Their display names, where known ("content_names", optional: some titles find their packs by
  // name, see WriteDisplayName).
  std::map<std::string, std::string> content_names;
  std::optional<AnyxUpdate> update;
};

std::string WriteAnyxManifest(const AnyxManifest& manifest);
enum class AnyxRead {
  kOk,
  kNotAnyx,  // a manifest.json of something else
  kNewer,    // a later format: `written_by` says by what
  kDamaged,
};
AnyxRead ReadAnyxManifest(const std::string& json, AnyxManifest& manifest);
// Every path is a plain relative one under game/, a listed package's folder or the update's (no
// "..", no drive, no file twice) and default.xex is there; else false, with `bad` naming the path.
bool CheckAnyxPaths(const AnyxManifest& manifest, std::string& bad);
// The archive holds the manifest and exactly its files, of their sizes.
bool CheckAnyxListing(const ArchiveListing& listing, const AnyxManifest& manifest, std::string& bad);
// An archive whose listing looks like an .anyx: manifest.json and game\default.xex at its root.
bool LooksLikeAnyx(const ArchiveListing& listing);

// A folder link (an NTFS junction) at `link` to the local folder `target`.
bool CreateJunction(const fs::path& link, const fs::path& target);

// Writes `game`'s .anyx backup to `out`: its copy (extracted, or in the player's folder), its
// downloadable content and the title update it is played with, checked and hashed.
Outcome ExportAnyx(Job& job, const CatalogGame& game, const fs::path& out);

// Adds the game in the .anyx `archive` (its listing already read): checked against its manifest,
// then put in place as it was saved. A backup without the title update the game is now played with
// (an older one) brings back the copy and its content, and says the update is missing. `handled` is false when the archive only looks like one (a 7z
// with a manifest.json of its own): the caller goes on with it. `work` is a folder for it in
// WorkDir(); progress starts at `from`.
Outcome ImportAnyx(Job& job, const fs::path& archive, const ArchiveListing& listing, const fs::path& work, int from,
                   bool& handled);

}  // namespace anyx360::launcher
