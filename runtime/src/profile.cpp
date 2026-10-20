// The player profile shared by the launcher and the games (see x360/profile.h).
#include "x360/profile.h"

#include <cctype>
#include <fstream>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::profile {

namespace {

constexpr Language kLanguages[] = {
    {"en", 1, L"English"},       {"ja", 2, L"日本語"},       {"de", 3, L"Deutsch"},
    {"fr", 4, L"Français"},      {"es", 5, L"Español"},      {"it", 6, L"Italiano"},
    {"ko", 7, L"한국어"},         {"zh-hant", 8, L"繁體中文"}, {"pt", 9, L"Português"},
    {"pl", 11, L"Polski"},       {"ru", 12, L"Русский"},     {"sv", 13, L"Svenska"},
    {"tr", 14, L"Türkçe"},       {"no", 15, L"Norsk"},       {"nl", 16, L"Nederlands"},
    {"zh-hans", 17, L"简体中文"},
};

// The console's country codes are the ISO 3166 codes in alphabetical order, from 1 (17 and 94 are
// unused).
constexpr const char* kCountries[] = {
    "",   "AE", "AL", "AM", "AR", "AT", "AU", "AZ", "BE", "BG", "BH", "BN", "BO", "BR", "BY", "BZ",
    "CA", "",   "CH", "CL", "CN", "CO", "CR", "CZ", "DE", "DK", "DO", "DZ", "EC", "EE", "EG", "ES",
    "FI", "FO", "FR", "GB", "GE", "GR", "GT", "HK", "HN", "HR", "HU", "ID", "IE", "IL", "IN", "IQ",
    "IR", "IS", "IT", "JM", "JO", "JP", "KE", "KG", "KR", "KW", "KZ", "LB", "LI", "LT", "LU", "LV",
    "LY", "MA", "MC", "MK", "MN", "MO", "MV", "MX", "MY", "NI", "NL", "NO", "NZ", "OM", "PA", "PE",
    "PH", "PK", "PL", "PR", "PT", "PY", "QA", "RO", "RU", "SA", "SE", "SG", "SI", "SK", "",   "SV",
    "SY", "TH", "TN", "TR", "TT", "TW", "UA", "US", "UY", "UZ", "VE", "VN", "YE", "ZA", "ZW",
};

// Where a language is most likely played, when Windows' region isn't one of the console's countries.
uint32_t UsualCountry(uint32_t language) {
  switch (language) {
    case 2: return 53;   // Japan
    case 3: return 24;   // Germany
    case 4: return 34;   // France
    case 5: return 31;   // Spain
    case 6: return 50;   // Italy
    case 7: return 56;   // Korea
    case 8: return 101;  // Taiwan
    case 9: return 84;   // Portugal
    case 11: return 82;  // Poland
    case 12: return 88;  // Russia
    case 13: return 90;  // Sweden
    case 14: return 99;  // Turkey
    case 15: return 75;  // Norway
    case 16: return 74;  // Netherlands
    case 17: return 20;  // China
    default: return 103;  // United States
  }
}

uint32_t WindowsCountry() {
  wchar_t name[16] = {};
  if (!GetUserDefaultGeoName(name, 16) || !name[0] || !name[1] || name[2]) return 0;
  const char iso[3] = {char(name[0]), char(name[1]), 0};
  for (uint32_t id = 1; id < std::size(kCountries); ++id)
    if (iso[0] == kCountries[id][0] && iso[1] == kCountries[id][1]) return id;
  return 0;
}

std::string Trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(uint8_t(s[a]))) ++a;
  while (b > a && std::isspace(uint8_t(s[b - 1]))) --b;
  return std::string(s.substr(a, b - a));
}

}  // namespace

std::filesystem::path File(const std::filesystem::path& folder) { return folder / "profile.txt"; }

Profile Read(const std::filesystem::path& file) {
  Profile profile;
  std::ifstream in(file, std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    const size_t equals = line.find('=');
    if (equals == std::string::npos) continue;
    const std::string key = Trim(std::string_view(line).substr(0, equals));
    const std::string value = Trim(std::string_view(line).substr(equals + 1));
    if (key.size() == 7 && key.compare(0, 6, "player") == 0 && key[6] >= '1' && key[6] <= '0' + kPlayers)
      profile.names[key[6] - '1'] = CleanName(value);
    else if (key == "language")
      profile.language = value.empty() ? "auto" : value;
  }
  return profile;
}

bool Write(const std::filesystem::path& file, const Profile& profile) {
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  std::ostringstream text;
  for (int i = 0; i < kPlayers; ++i) text << "player" << i + 1 << " = " << CleanName(profile.names[i]) << "\n";
  text << "language = " << (profile.language.empty() ? "auto" : profile.language) << "\n";
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << text.str();
  return bool(out);
}

std::string CleanName(std::string_view name) {
  std::string clean;
  for (char ch : Trim(name)) {
    const bool space = ch == ' ';
    if (!(std::isalnum(uint8_t(ch)) && uint8_t(ch) < 0x80) && !space) continue;
    if (space && (clean.empty() || clean.back() == ' ')) continue;  // no leading or double spaces
    if (clean.size() == kNameLength) break;
    clean += ch;
  }
  while (!clean.empty() && clean.back() == ' ') clean.pop_back();
  return clean;
}

std::span<const Language> Languages() { return kLanguages; }

uint32_t WindowsLanguage() {
  const LANGID id = GetUserDefaultUILanguage();
  switch (PRIMARYLANGID(id)) {
    case LANG_ENGLISH: return 1;
    case LANG_JAPANESE: return 2;
    case LANG_GERMAN: return 3;
    case LANG_FRENCH: return 4;
    case LANG_SPANISH: return 5;
    case LANG_ITALIAN: return 6;
    case LANG_KOREAN: return 7;
    case LANG_CHINESE: {
      const WORD sub = SUBLANGID(id);
      const bool traditional = sub == SUBLANG_CHINESE_TRADITIONAL || sub == SUBLANG_CHINESE_HONGKONG ||
                               sub == SUBLANG_CHINESE_MACAU;
      return traditional ? 8 : 17;
    }
    case LANG_PORTUGUESE: return 9;
    case LANG_POLISH: return 11;
    case LANG_RUSSIAN: return 12;
    case LANG_SWEDISH: return 13;
    case LANG_TURKISH: return 14;
    case LANG_NORWEGIAN: return 15;
    case LANG_DUTCH: return 16;
    default: return 0;
  }
}

Locale Resolve(std::string_view language) {
  uint32_t xbox = 0;
  for (const Language& l : kLanguages)
    if (language == l.tag) xbox = l.xbox;
  if (!xbox) xbox = WindowsLanguage();
  if (!xbox) xbox = 1;
  const uint32_t country = WindowsCountry();
  return {xbox, country ? country : UsualCountry(xbox)};
}

}  // namespace x360::profile
