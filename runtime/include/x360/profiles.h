// Global profiles, like the console's: one per player, shared by every game and the launcher, in
// AnyX360's folder of %LOCALAPPDATA% (the `folder` every function takes, the parent of the games'
// user folders):
//
//   profiles\<XUID>\profile.json          the profile (Profile below)
//   profiles\<XUID>\titles\<TitleID>.json  time played in a title (TitleStats below)
//   profile.txt                            who is signed in on each slot (x360/profile.h)
//
// <XUID> is the profile's offline XUID in 16 upper-case hex digits. Saves stay in each game's own
// folder, keyed by XUID (UserFolder). docs/PROFILES.md describes the files and the migration from
// the names of profile.txt before 0.20.0.
//
// Every file is written atomically (a temporary file, then a rename), so a game and the launcher
// never read half of one.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "x360/profile.h"

namespace x360::profiles {

namespace fs = std::filesystem;

// Player n's offline XUID before profiles existed (kLegacyXuid + n - 1). The profile with this
// XUID keeps each game's root folder for its saves; the others' existing players\<XUID> folders
// match the profiles migrated for them.
constexpr uint64_t kLegacyXuid = 0xE000000012345678ull;

struct Profile {
  uint64_t xuid = 0;
  std::string gamertag;            // CleanName: letters, digits and spaces, at most 15
  std::string gamerpic;            // a picture's file name in the profile's folder; empty: none yet
  std::string avatar;              // the avatar description (1000 bytes), base64; empty: the defaults
  std::string language = "auto";   // x360/profile.h's tags
  std::string created;             // ISO 8601, UTC ("2026-10-29T05:04:00Z")
};

// Time played in one title by one profile.
struct TitleStats {
  uint32_t title_id = 0;
  std::string title;        // its name when it was last played
  uint64_t seconds = 0;     // played in all
  std::string last_played;  // ISO 8601, UTC; empty: never
};

std::string XuidText(uint64_t xuid);                    // "E000000012345678"
std::optional<uint64_t> ParseXuid(std::string_view text);  // 1-16 hex digits; 0 isn't one
// A new offline XUID: E000 and 48 random bits.
uint64_t RandomXuid();
// The current time as Profile::created and TitleStats::last_played write it.
std::string NowText();

fs::path ProfilesFolder(const fs::path& folder);              // <folder>\profiles
fs::path ProfileFolder(const fs::path& folder, uint64_t xuid);  // <folder>\profiles\<XUID>
// Where a game keeps a profile's saves and settings: its root folder for kLegacyXuid, a guest's
// <user_root>\players\guest<n> (cleared, ClearGuestFolders), else <user_root>\players\<XUID>.
fs::path UserFolder(const fs::path& user_root, uint64_t xuid);

// Guests, as on the console: a player signed in from another's profile on their own controller
// ("Daniel (1)"), not a profile: nothing of theirs is kept (no achievements, no time played) and
// their saves last until the game starts again. Their XUID follows the console's guest form: the
// online form's 0x0009 in the top 16 bits with the guest number (1-3) in bits 54-55 (the XDK's
// IsGuestXUID: (xuid >> 48) & 0x000F == 9 and (xuid >> 48) & 0x00C0 != 0), and the sponsor's low
// 48 bits, so no two players share one.
constexpr int kMaxGuests = 3;
uint64_t GuestXuid(uint64_t sponsor_xuid, int number);  // number 1-3
bool IsGuestXuid(uint64_t xuid);
int GuestNumber(uint64_t xuid);  // 1-3; 0: not a guest's
// "<sponsor> (n)", the sponsor's gamertag shortened to fit 15 characters; "Guest" without one.
std::string GuestGamertag(std::string_view sponsor_gamertag, int number);
// <user_root>\players\guest<n>: removed (the folder and everything in it) at the game's start and
// when a guest signs in with that number. Nothing else is touched. False if one stays.
fs::path GuestFolder(const fs::path& user_root, int number);
bool ClearGuestFolder(const fs::path& user_root, int number);
bool ClearGuestFolders(const fs::path& user_root);

// Writes `bytes` to a temporary file next to `file`, flushed, then renames it over `file`.
bool WriteAtomic(const fs::path& file, std::string_view bytes);

// The profile with this XUID (none: no such profile, or it was deleted).
std::optional<Profile> Load(const fs::path& folder, uint64_t xuid);
// Every profile, the oldest first.
std::vector<Profile> List(const fs::path& folder);
bool Save(const fs::path& folder, const Profile& profile);

enum class Error { kNone, kEmptyName, kNameTaken, kNotFound, kWriteFailed };
struct Result {
  Error error = Error::kNone;
  Profile profile;
  explicit operator bool() const { return error == Error::kNone; }
};
// A new profile with a random XUID. The gamertag is cleaned (CleanName) and must be another
// profile's in no letter case.
Result Create(const fs::path& folder, std::string_view gamertag, std::string_view language = "auto");
Result Rename(const fs::path& folder, uint64_t xuid, std::string_view gamertag);
// The profile no longer exists: its profile.json is renamed profile.deleted.json (time played and
// the games' saves stay where they are) and it is signed out of profile.txt.
Result Remove(const fs::path& folder, uint64_t xuid);

// Who is signed in (profile.txt), first making the profiles if there are none yet: the names of a
// profile.txt from before 0.20.0 become profiles with their old XUIDs (kLegacyXuid + slot), and
// player 1 always gets one (named "Player" without a name), so each game's root folder and its
// players\<XUID> folders keep their saves; players 2-4 without a name who have saves in a game get
// one too ("Player 2"...), not signed in. The old file is copied to profile.txt.before-0.20.0 first;
// nothing is moved or deleted. With no profile.txt either, a profile is made only if
// `create_default` (the launcher; a game run without it plays as "Player" on its own), and nobody
// is signed in.
profile::SignIn LoadSignIn(const fs::path& folder, bool create_default);

// The profiles signed in on each slot (none: the slot has no profile, or it no longer exists).
struct SignedIn {
  std::optional<Profile> slots[profile::kPlayers];
};
SignedIn Resolve(const fs::path& folder, const profile::SignIn& sign_in);

// Signs `xuid` in on `slot` in profile.txt (0: signs the slot out); a profile signed in on another
// slot moves.
bool SetSlot(const fs::path& folder, int slot, uint64_t xuid);

std::optional<TitleStats> ReadTitle(const fs::path& folder, uint64_t xuid, uint32_t title_id);
// Adds `seconds` to the profile's time in the title and makes now its last played time.
bool AddPlayTime(const fs::path& folder, uint64_t xuid, uint32_t title_id, std::string_view title, uint64_t seconds);

}  // namespace x360::profiles
