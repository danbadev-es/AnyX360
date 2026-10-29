// Achievements, like the console's: each title's list (from its SPA, the XDBF resource of its XEX)
// and each profile's unlocks, in AnyX360's folder of %LOCALAPPDATA% (the `folder` every function
// takes, as x360/profiles.h's):
//
//   titles\<TitleID>\achievements.json      the title's list, its strings in every language it has
//   titles\<TitleID>\<ImageID>.png          their pictures
//   profiles\<XUID>\achievements\<TitleID>.json  what the profile unlocked, and when
//
// A game writes its title's list when it starts (from its loaded image, title update included) if
// the list there came from another SPA; the launcher reads it, or the base executable's SPA for a
// game never played. docs/ACHIEVEMENTS.md describes the files. Every file is written atomically
// (profiles::WriteAtomic).
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace x360::achievements {

namespace fs = std::filesystem;

// The flags of an achievement (XACH, XACHIEVEMENT_DETAILS).
constexpr uint32_t kTypeMask = 0x7;            // completion, leveling, unlock, event...
constexpr uint32_t kShowUnachieved = 0x8;      // its locked description is shown (else "secret")
constexpr uint32_t kAchievedOnline = 0x10000;  // unlocked while signed in to Xbox Live
constexpr uint32_t kAchieved = 0x20000;        // unlocked

struct Strings {
  std::string name;      // UTF-8
  std::string locked;    // the description before it is unlocked (how to)
  std::string unlocked;  // and after
};

struct Achievement {
  uint32_t id = 0;
  uint32_t gamerscore = 0;
  uint32_t image_id = 0;  // its picture: titles\<TitleID>\<ImageID>.png (8 upper-case hex digits)
  uint32_t flags = 0;     // the XACH flags (type, kShowUnachieved)
  // By language tag (x360/profile.h's: "en", "es", "ja"...).
  std::map<std::string, Strings> strings;
  // The strings in `language`, else English, else any; empty if it has none.
  const Strings& In(std::string_view language) const;
  bool Secret() const { return !(flags & kShowUnachieved); }
};

struct TitleList {
  uint32_t title_id = 0;
  std::string title;           // the title's name, English
  std::string source;          // what it was read from (a hash of the SPA): written again when it changes
  std::vector<Achievement> achievements;  // in the SPA's order
  uint32_t TotalGamerscore() const;
  const Achievement* Find(uint32_t id) const;
};

// The language tag of an XDBF/XLanguage number (1 "en", 5 "es"...; empty for none) and back (0).
std::string LanguageTag(uint32_t xdbf_language);
uint32_t XdbfLanguage(std::string_view tag);

fs::path TitleFolder(const fs::path& folder, uint32_t title_id);  // <folder>\titles\<TitleID>
fs::path ImageFile(const fs::path& folder, uint32_t title_id, uint32_t image_id);
std::optional<TitleList> ReadTitleList(const fs::path& folder, uint32_t title_id);
// Writes the list and its pictures (by image ID, PNG bytes). Pictures already there are kept.
bool WriteTitleList(const fs::path& folder, const TitleList& list,
                    const std::map<uint32_t, std::vector<uint8_t>>& images);

// One unlock in a profile.
struct Unlock {
  uint32_t id = 0;
  uint32_t gamerscore = 0;  // the achievement's, kept so totals need no title list
  std::string date;         // ISO 8601, UTC
};

struct TitleUnlocks {
  uint32_t title_id = 0;
  std::string title;
  std::vector<Unlock> unlocks;  // in the order they were unlocked
  const Unlock* Find(uint32_t id) const;
  uint32_t Gamerscore() const;
};

fs::path UnlocksFile(const fs::path& folder, uint64_t xuid, uint32_t title_id);
// The profile's unlocks in the title (none: an empty list).
TitleUnlocks ReadUnlocks(const fs::path& folder, uint64_t xuid, uint32_t title_id);

enum class Result { kUnlocked, kAlready, kFailed };
// Records the unlock now, once: an achievement already unlocked keeps its first date.
Result Record(const fs::path& folder, uint64_t xuid, uint32_t title_id, std::string_view title, uint32_t id,
              uint32_t gamerscore, std::string_view date = {});

// The profile's gamerscore and unlocks in every title (profiles\<XUID>\achievements\*.json).
struct Totals {
  uint32_t gamerscore = 0;
  uint32_t unlocked = 0;
  uint32_t titles = 0;  // titles with at least one unlock
};
Totals ProfileTotals(const fs::path& folder, uint64_t xuid);

// An ISO 8601 UTC time ("2026-10-29T05:04:00Z") as a FILETIME (100 ns since 1601), 0 if it isn't one.
uint64_t FileTime(std::string_view iso);

}  // namespace x360::achievements
