// Global profiles shared by the launcher and the games (see x360/profiles.h).
#include "x360/profiles.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <random>

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

namespace x360::profiles {

namespace {

constexpr int kFormat = 1;  // profile.json's and titles\*.json's "version"
constexpr char kDefaultGamertag[] = "Player";

std::optional<std::string> ReadFile(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string String(const rapidjson::Value& object, const char* key) {
  if (!object.IsObject()) return {};
  auto found = object.FindMember(key);
  return found != object.MemberEnd() && found->value.IsString() ? found->value.GetString() : std::string();
}

uint64_t Number(const rapidjson::Value& object, const char* key) {
  if (!object.IsObject()) return 0;
  auto found = object.FindMember(key);
  return found != object.MemberEnd() && found->value.IsUint64() ? found->value.GetUint64() : 0;
}

std::string Lower(std::string_view text) {
  std::string lower(text);
  for (char& c : lower) c = char(std::tolower(uint8_t(c)));
  return lower;
}

fs::path JsonFile(const fs::path& folder, uint64_t xuid) { return ProfileFolder(folder, xuid) / "profile.json"; }

fs::path TitleFile(const fs::path& folder, uint64_t xuid, uint32_t title_id) {
  char name[16];
  std::snprintf(name, sizeof(name), "%08X.json", title_id);
  return ProfileFolder(folder, xuid) / "titles" / name;
}

std::optional<Profile> Parse(const std::string& text) {
  rapidjson::Document doc;
  doc.Parse(text.c_str(), text.size());
  if (doc.HasParseError() || !doc.IsObject()) return std::nullopt;
  const std::optional<uint64_t> xuid = ParseXuid(String(doc, "xuid"));
  if (!xuid) return std::nullopt;
  Profile p;
  p.xuid = *xuid;
  p.gamertag = profile::CleanName(String(doc, "gamertag"));
  if (p.gamertag.empty()) p.gamertag = kDefaultGamertag;
  p.gamerpic = String(doc, "gamerpic");
  p.avatar = String(doc, "avatar");
  p.language = String(doc, "language");
  if (p.language.empty()) p.language = "auto";
  p.created = String(doc, "created");
  return p;
}

std::string Serialize(const Profile& p) {
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
  w.SetIndent(' ', 2);
  w.StartObject();
  w.Key("version"), w.Int(kFormat);
  w.Key("xuid"), w.String(XuidText(p.xuid).c_str());
  w.Key("gamertag"), w.String(p.gamertag.c_str());
  w.Key("gamerpic"), w.String(p.gamerpic.c_str());
  w.Key("avatar"), w.String(p.avatar.c_str());
  w.Key("language"), w.String(p.language.empty() ? "auto" : p.language.c_str());
  w.Key("created"), w.String(p.created.c_str());
  w.EndObject();
  return std::string(buffer.GetString(), buffer.GetSize()) + "\n";
}

// Another profile has this gamertag (any letter case).
bool Taken(const fs::path& folder, std::string_view gamertag, uint64_t except) {
  const std::string lower = Lower(gamertag);
  for (const Profile& p : List(folder))
    if (p.xuid != except && Lower(p.gamertag) == lower) return true;
  return false;
}

}  // namespace

std::string XuidText(uint64_t xuid) {
  char text[20];
  std::snprintf(text, sizeof(text), "%016llX", (unsigned long long)xuid);
  return text;
}

std::optional<uint64_t> ParseXuid(std::string_view text) {
  if (text.empty() || text.size() > 16) return std::nullopt;
  uint64_t value = 0;
  for (char c : text) {
    const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (digit < 0) return std::nullopt;
    value = value << 4 | uint64_t(digit);
  }
  if (!value) return std::nullopt;
  return value;
}

uint64_t RandomXuid() {
  uint64_t bits = 0;
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&bits), sizeof(bits), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    bits = (uint64_t(std::random_device{}()) << 32) ^ std::random_device{}();
  return 0xE000000000000000ull | (bits & 0x0000FFFFFFFFFFFFull);
}

std::string NowText() {
  const std::time_t now = std::time(nullptr);
  std::tm utc{};
  gmtime_s(&utc, &now);
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return text;
}

fs::path ProfilesFolder(const fs::path& folder) { return folder / "profiles"; }

fs::path ProfileFolder(const fs::path& folder, uint64_t xuid) { return ProfilesFolder(folder) / XuidText(xuid); }

fs::path UserFolder(const fs::path& user_root, uint64_t xuid) {
  if (xuid == kLegacyXuid) return user_root;
  if (const int guest = GuestNumber(xuid)) return GuestFolder(user_root, guest);
  return user_root / "players" / XuidText(xuid);
}

uint64_t GuestXuid(uint64_t sponsor_xuid, int number) {
  return 0x0009000000000000ull | (uint64_t(number & 3) << 54) | (sponsor_xuid & 0x0000FFFFFFFFFFFFull);
}

bool IsGuestXuid(uint64_t xuid) {
  const uint32_t high = uint32_t(xuid >> 48);
  return (high & 0x000F) == 0x9 && (high & 0x00C0) != 0;
}

int GuestNumber(uint64_t xuid) { return IsGuestXuid(xuid) ? int((xuid >> 54) & 3) : 0; }

std::string GuestGamertag(std::string_view sponsor_gamertag, int number) {
  if (sponsor_gamertag.empty()) return "Guest";
  const std::string suffix = " (" + std::to_string(number) + ")";
  std::string name(sponsor_gamertag.substr(0, profile::kNameLength - suffix.size()));
  while (!name.empty() && name.back() == ' ') name.pop_back();
  return name + suffix;
}

fs::path GuestFolder(const fs::path& user_root, int number) {
  return user_root / "players" / ("guest" + std::to_string(number));
}

bool ClearGuestFolder(const fs::path& user_root, int number) {
  if (user_root.empty() || number < 1 || number > kMaxGuests) return false;
  std::error_code ec;
  const fs::path folder = GuestFolder(user_root, number);
  fs::remove_all(folder, ec);
  return !fs::exists(folder, ec);
}

bool ClearGuestFolders(const fs::path& user_root) {
  bool cleared = true;
  for (int n = 1; n <= kMaxGuests; ++n) cleared = ClearGuestFolder(user_root, n) && cleared;
  return cleared;
}

bool WriteAtomic(const fs::path& file, std::string_view bytes) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  fs::path temporary = file;
  temporary += L".tmp";
  temporary += std::to_wstring(GetCurrentProcessId());
  HANDLE out = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (out == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(out, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size() &&
                  FlushFileBuffers(out);
  CloseHandle(out);
  if (ok && MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  DeleteFileW(temporary.c_str());
  return false;
}

std::optional<Profile> Load(const fs::path& folder, uint64_t xuid) {
  if (IsGuestXuid(xuid)) return std::nullopt;  // a guest is never a profile
  const std::optional<std::string> text = ReadFile(JsonFile(folder, xuid));
  if (!text) return std::nullopt;
  std::optional<Profile> p = Parse(*text);
  if (!p || p->xuid != xuid) return std::nullopt;  // a copied folder: its name is the XUID
  return p;
}

std::vector<Profile> List(const fs::path& folder) {
  std::vector<Profile> profiles;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(ProfilesFolder(folder), ec)) {
    if (!entry.is_directory(ec)) continue;
    const std::optional<uint64_t> xuid = ParseXuid(entry.path().filename().string());
    if (!xuid) continue;
    if (std::optional<Profile> p = Load(folder, *xuid)) profiles.push_back(std::move(*p));
  }
  std::sort(profiles.begin(), profiles.end(), [](const Profile& a, const Profile& b) {
    if (a.created != b.created) return a.created < b.created;
    return a.xuid < b.xuid;  // made together (the migration): in their players' order
  });
  return profiles;
}

bool Save(const fs::path& folder, const Profile& profile) {
  if (!profile.xuid) return false;
  return WriteAtomic(JsonFile(folder, profile.xuid), Serialize(profile));
}

Result Create(const fs::path& folder, std::string_view gamertag, std::string_view language) {
  Result result;
  result.profile.gamertag = profile::CleanName(gamertag);
  if (result.profile.gamertag.empty()) return {Error::kEmptyName};
  if (Taken(folder, result.profile.gamertag, 0)) return {Error::kNameTaken};
  do result.profile.xuid = RandomXuid();
  while ((result.profile.xuid >= kLegacyXuid && result.profile.xuid < kLegacyXuid + profile::kPlayers) ||
         fs::exists(ProfileFolder(folder, result.profile.xuid)));
  result.profile.language = language.empty() ? "auto" : std::string(language);
  result.profile.created = NowText();
  if (!Save(folder, result.profile)) return {Error::kWriteFailed};
  return result;
}

Result Rename(const fs::path& folder, uint64_t xuid, std::string_view gamertag) {
  std::optional<Profile> p = Load(folder, xuid);
  if (!p) return {Error::kNotFound};
  const std::string clean = profile::CleanName(gamertag);
  if (clean.empty()) return {Error::kEmptyName};
  if (Taken(folder, clean, xuid)) return {Error::kNameTaken};
  p->gamertag = clean;
  if (!Save(folder, *p)) return {Error::kWriteFailed};
  return {Error::kNone, *p};
}

Result Remove(const fs::path& folder, uint64_t xuid) {
  std::optional<Profile> p = Load(folder, xuid);
  if (!p) return {Error::kNotFound};
  const fs::path file = JsonFile(folder, xuid);
  if (!MoveFileExW(file.c_str(), (file.parent_path() / "profile.deleted.json").c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return {Error::kWriteFailed};
  profile::SignIn sign_in = profile::Read(profile::File(folder));
  bool signed_in = false;
  for (uint64_t& slot : sign_in.slots)
    if (slot == xuid) slot = 0, signed_in = true;
  if (signed_in && !profile::Write(profile::File(folder), sign_in)) return {Error::kWriteFailed};
  return {Error::kNone, *p};
}

profile::SignIn LoadSignIn(const fs::path& folder, bool create_default) {
  const fs::path file = profile::File(folder);
  profile::SignIn sign_in = profile::Read(file);
  std::error_code ec;
  // Migrated once: from then on the profiles folder exists (even with every profile deleted).
  if (fs::exists(ProfilesFolder(folder), ec)) return sign_in;
  const bool had_file = fs::exists(file, ec);
  if (!had_file && !create_default) return sign_in;

  if (had_file) {
    fs::path backup = file;
    backup += ".before-0.20.0";
    if (!fs::exists(backup, ec)) fs::copy_file(file, backup, ec);
  }
  // Players 2-4 without a name who have saves in a game (<game>\players\<their XUID>) played as
  // "Player 2"...: they get a profile of that name, not signed in, to choose in the games' picker.
  auto has_saves = [&](uint64_t xuid) {
    for (const auto& game : fs::directory_iterator(folder, ec))
      if (game.is_directory(ec) && fs::is_directory(UserFolder(game.path(), xuid), ec)) return true;
    return false;
  };
  const std::string created = NowText();
  for (int i = 0; i < profile::kPlayers; ++i) {
    const std::string& name = sign_in.names[i];
    const bool saves_only = i > 0 && name.empty();
    if (saves_only && !has_saves(kLegacyXuid + uint64_t(i))) continue;
    Profile p;
    p.xuid = kLegacyXuid + uint64_t(i);
    p.gamertag = !name.empty() ? name : i == 0 ? kDefaultGamertag : "Player " + std::to_string(i + 1);
    p.language = sign_in.language;
    p.created = created;
    if (!Load(folder, p.xuid)) Save(folder, p);
    // Signed in where they played before; a first start signs nobody in ("Who's playing?").
    if (had_file && !saves_only && !sign_in.slots[i]) sign_in.slots[i] = p.xuid;
  }
  fs::create_directories(ProfilesFolder(folder), ec);
  if (had_file) profile::Write(file, sign_in);
  sign_in.legacy = false;
  return sign_in;
}

SignedIn Resolve(const fs::path& folder, const profile::SignIn& sign_in) {
  SignedIn resolved;
  for (int i = 0; i < profile::kPlayers; ++i) {
    if (!sign_in.slots[i]) continue;
    bool earlier = false;  // one profile, one slot
    for (int j = 0; j < i; ++j) earlier |= sign_in.slots[j] == sign_in.slots[i];
    if (!earlier) resolved.slots[i] = Load(folder, sign_in.slots[i]);
  }
  return resolved;
}

bool SetSlot(const fs::path& folder, int slot, uint64_t xuid) {
  if (slot < 0 || slot >= profile::kPlayers || IsGuestXuid(xuid)) return false;
  profile::SignIn sign_in = profile::Read(profile::File(folder));
  for (uint64_t& other : sign_in.slots)
    if (xuid && other == xuid) other = 0;
  sign_in.slots[slot] = xuid;
  return profile::Write(profile::File(folder), sign_in);
}

std::optional<TitleStats> ReadTitle(const fs::path& folder, uint64_t xuid, uint32_t title_id) {
  const std::optional<std::string> text = ReadFile(TitleFile(folder, xuid, title_id));
  if (!text) return std::nullopt;
  rapidjson::Document doc;
  doc.Parse(text->c_str(), text->size());
  if (doc.HasParseError() || !doc.IsObject()) return std::nullopt;
  TitleStats stats;
  stats.title_id = title_id;
  stats.title = String(doc, "title");
  stats.seconds = Number(doc, "seconds_played");
  stats.last_played = String(doc, "last_played");
  return stats;
}

bool AddPlayTime(const fs::path& folder, uint64_t xuid, uint32_t title_id, std::string_view title, uint64_t seconds) {
  if (IsGuestXuid(xuid)) return false;  // a guest's time isn't kept
  TitleStats stats = ReadTitle(folder, xuid, title_id).value_or(TitleStats{title_id});
  stats.seconds += seconds;
  if (!title.empty()) stats.title = title;
  stats.last_played = NowText();
  char id[12];
  std::snprintf(id, sizeof(id), "%08X", title_id);
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
  w.SetIndent(' ', 2);
  w.StartObject();
  w.Key("version"), w.Int(kFormat);
  w.Key("title_id"), w.String(id);
  w.Key("title"), w.String(stats.title.c_str());
  w.Key("seconds_played"), w.Uint64(stats.seconds);
  w.Key("last_played"), w.String(stats.last_played.c_str());
  w.EndObject();
  return WriteAtomic(TitleFile(folder, xuid, title_id), std::string(buffer.GetString(), buffer.GetSize()) + "\n");
}

}  // namespace x360::profiles
