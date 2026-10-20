// Putting a recognised copy in its place: games\<id>\game, the title's downloadable content, and
// copies the game can't run from (Games on Demand and Xbox Live Arcade packages, disc images)
// written out into files. Intermediate files go to games\.work, deleted when the job ends.
#pragma once

#include <cstdint>
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

void RemoveAll(const fs::path& path);
// Removes a folder quickly: renamed out of the way first, so a half-deleted copy never looks ready.
void RemoveFolder(const fs::path& path);

// Free space on the release's drive for `needed` bytes.
bool EnoughSpace(uint64_t needed, Message& error);

// Moves a checked copy (the folder holding default.xex) to games\<id>\game, replacing an earlier
// copy, extracted or used in place, but keeping the player's cover.
bool InstallCopy(const CatalogGame& game, const fs::path& found, Message& error);

// Writes a disc file system's files to `out`, reporting progress in [from, to]. False if it failed
// (`error`) or was cancelled.
bool ExtractVolume(Job& job, x360tool::XdvdfsImage& image, const fs::path& out, int from, int to, Message& error);

// A downloadable content package of `game` that needs its title update, which AnyX360 doesn't play:
// all of them for a game whose packs all do ("downloadable_content": false), else those game.json
// names ("content_needs_update", by package file name). They are left out.
bool NeedsTitleUpdate(const CatalogGame& game, const fs::path& package);

// Extracts downloadable content packages where the runtime looks for them:
// %LOCALAPPDATA%\AnyX360\<user dir>\content\00000002\<package name>\. Returns how many.
int InstallDownloadableContent(const CatalogGame& game, const std::vector<fs::path>& packages);

// The avatar asset pack that discs of avatar games carry (AvatarAssetPack in the game's folder),
// extracted where every game's avatar system looks for it (%LOCALAPPDATA%\AnyX360\system\): avatar
// games without one (Doritos Crash Course) get their avatars from it. Nothing if the copy has none.
void InstallAvatarPack(const fs::path& game_root);

// The copy written out or extracted into `extracted` (under WorkDir) becomes games\<id>\game, if it
// is `game` and complete; then its icon, and the title's downloadable content in `content_source`
// (empty: none). The packages there that aren't used are noted.
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
