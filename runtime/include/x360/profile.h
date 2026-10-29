// Who is signed in, in %LOCALAPPDATA%\AnyX360\profile.txt: the XUID of the profile on each of the
// four controller slots (x360/profiles.h; empty: nobody) and the language the games are told. The
// launcher signs player 1 in ("Who's playing?"); the games' sign-in picker signs players 2-4 in.
// One setting a line, "key = value", UTF-8:
//
//   slot1 = E000000012345678
//   slot2 = E0003A41C29F0B57
//   slot3 =
//   slot4 =
//   language = auto
//
// Before 0.20.0 the file held the four players' names (player1 = Daniel ...): they are read as
// `names` and become profiles (profiles::LoadSignIn).
//
// `language` is "auto" (Windows' display language) or one of Languages()' tags. Games read it once,
// when they start.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace x360::profile {

constexpr int kPlayers = 4;
constexpr size_t kNameLength = 15;  // a gamertag's

struct SignIn {
  uint64_t slots[kPlayers] = {};  // the profile signed in on each slot; 0: nobody
  std::string language = "auto";
  // A file from before 0.20.0: the players' names (empty: the game's own), only read.
  bool legacy = false;
  std::string names[kPlayers];
};

// profile.txt in `folder`: AnyX360's folder in %LOCALAPPDATA%, the parent of each game's user folder.
std::filesystem::path File(const std::filesystem::path& folder);
// A missing or unreadable file reads as the defaults.
SignIn Read(const std::filesystem::path& file);
// Writes the slots and the language (atomically).
bool Write(const std::filesystem::path& file, const SignIn& sign_in);

// A name as a gamertag can be: letters, digits and spaces, trimmed, at most kNameLength.
std::string CleanName(std::string_view name);

// The languages the console tells games (XGetLanguage), with the tags profile.txt uses.
struct Language {
  const char* tag;
  uint32_t xbox;           // XLanguage: 1 English, 2 Japanese, 3 German, 4 French, 5 Spanish...
  const wchar_t* name;     // in that language, for the launcher's list
};
std::span<const Language> Languages();

// What games are told. `language`: "auto" is Windows' display language (English if the console has
// none like it); the country is Windows' region, else the language's usual country.
struct Locale {
  uint32_t language;  // XLanguage
  uint32_t country;   // the console's country code (XConfig): 103 United States, 31 Spain...
};
Locale Resolve(std::string_view language);

// The console's language for Windows' display language, 0 if it has none like it.
uint32_t WindowsLanguage();

}  // namespace x360::profile
