// Putting a recognised copy in its place: games\<id>\game, the title's downloadable content, and
// copies the game can't run from (Games on Demand and Xbox Live Arcade packages, disc images)
// written out into files. Intermediate files go to games\.work, deleted when the job ends.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "core/jobs.h"
#include "core/probe.h"

struct CatalogGame;

namespace x360tool {
class XdvdfsImage;
}

namespace anyx360::launcher {

// games\.work: archives extracted, packages and images written out, before the copy moves into place.
fs::path WorkDir();

// A link (a junction, a symbolic link): a folder that is somewhere else.
bool IsLink(const fs::path& path);
// Deletes a file or a folder with everything in it. A link inside is deleted, never what it points
// to (an export's staging links to the player's own folders).
void RemoveAll(const fs::path& path);
// Removes a folder quickly: renamed out of the way first, so a half-deleted copy never looks ready.
void RemoveFolder(const fs::path& path);
// Moves a folder, across drives too (copied, then deleted).
bool MoveFolder(const fs::path& from, const fs::path& to);

// Free space on the release's drive for `needed` bytes.
bool EnoughSpace(uint64_t needed, Message& error);
// Free space for each (place, bytes), on whatever drive each place is: amounts on the same drive
// are added.
bool EnoughSpaceFor(const std::vector<std::pair<fs::path, uint64_t>>& needs, Message& error);

// Moves a checked copy (the folder holding default.xex) to games\<id>\game, replacing an earlier
// copy, extracted or used in place, but keeping the player's cover.
bool InstallCopy(const CatalogGame& game, const fs::path& found, Message& error);

// Writes a disc file system's files to `out`, reporting progress in [from, to]. False if it failed
// (`error`) or was cancelled.
bool ExtractVolume(Job& job, x360tool::XdvdfsImage& image, const fs::path& out, int from, int to, Message& error);

// A downloadable content package of `game` that needs its title update, which AnyX360 doesn't play
// for that game: all of them for a game whose packs all do ("downloadable_content": false), else
// those game.json names ("content_needs_update", by package file name). They are left out. None
// for a game played with its update (game.json "update").
bool NeedsTitleUpdate(const CatalogGame& game, const fs::path& package);

// Title updates (content type 0x000B0000). A game recompiled from its update (game.json "update")
// is ready only with that update installed, in TitleUpdateDir(game): %LOCALAPPDATA%\AnyX360\<user
// dir>\content\000B0000\<package> (where the runtime looks for it and mounts it as update:\).
fs::path TitleUpdateDir(const CatalogGame& game);
bool UsesTitleUpdate(const CatalogGame& game);
// The update's patch for each of the game's executables (default.xexp; default_mp.xexp too for
// Modern Warfare 3 and Black Ops II).
std::vector<std::wstring> TitleUpdatePatches(const CatalogGame& game);
// True for a game that doesn't use one; else every patch of TitleUpdatePatches is installed.
bool TitleUpdateInstalled(const CatalogGame& game);
// The version an extracted update makes (the delta descriptor of its default.xexp, or of another
// patch); 0 if it has none.
uint32_t TitleUpdateVersion(const fs::path& folder, const wchar_t* patch = L"default.xexp");

enum class UpdateResult {
  kInstalled,
  kOtherVersion,  // an update to another version than the one game.json names: left out
  kDamaged,       // not a package, or no default.xexp in it
};
// Extracts the title update `package` of `game` (one that uses its update) into TitleUpdateDir(game)
// if it is the version game.json names; `version` is the one it makes (0 if unknown).
UpdateResult InstallTitleUpdate(const CatalogGame& game, const fs::path& package, uint32_t& version);
// The title update packages of `game` found with a copy: the right one installed and noted, the
// others noted (another version, or a game that plays without its update); then, if the game is
// still without the update it needs, a note saying how to add it.
void InstallTitleUpdates(const CatalogGame& game, const std::vector<fs::path>& packages, Outcome& outcome);

// Where the runtime looks for the title's downloadable content, one folder per package:
// %LOCALAPPDATA%\AnyX360\<user dir>\content\00000002 (the saves are next to it, in 00000001).
fs::path DownloadableContentDir(const CatalogGame& game);

// Extracts downloadable content packages where the runtime looks for them:
// DownloadableContentDir(game)\<package name>\. Returns how many.
int InstallDownloadableContent(const CatalogGame& game, const std::vector<fs::path>& packages);
// A downloadable content package's display name (UTF-8), kept for the runtime in
// <content_folder>\..\Headers\<type>\<package>.header; empty if there is none.
void WriteDisplayName(const fs::path& content_folder, const fs::path& package, const std::string& name_utf8);
std::string ReadDisplayName(const fs::path& content_folder, const fs::path& package);

// The avatar asset pack that discs of avatar games carry (AvatarAssetPack in the game's folder),
// extracted where every game's avatar system looks for it (%LOCALAPPDATA%\AnyX360\system\): avatar
// games without one (Doritos Crash Course) get their avatars from it. Nothing if the copy has none.
void InstallAvatarPack(const fs::path& game_root);

// The copy written out or extracted into `extracted` (under WorkDir) becomes games\<id>\game, if it
// is `game` and complete; then its icon, and the title's downloadable content and title update in
// `content_source` (empty: none). The packages there that aren't used are noted, and so is an
// update the game needs and didn't come with.
Outcome InstallExtracted(Job& job, const CatalogGame& game, const fs::path& extracted, const fs::path& content_source);

// The copies the game can't run from, written out. The player has already been asked about
// replacing `game` (Job::ConfirmReplace). Progress goes from `from` to 99.
Outcome AddGamesOnDemand(Job& job, const CatalogGame& game, const FoundPackage& package, const fs::path& content_source,
                         int from);
Outcome AddArcadeGame(Job& job, const CatalogGame& game, const FoundPackage& package, const fs::path& content_source,
                      int from);
// A disc image: its default.xex says which game it is first.
Outcome AddDiscImage(Job& job, const fs::path& image, const fs::path& content_source, int from);

}  // namespace anyx360::launcher
