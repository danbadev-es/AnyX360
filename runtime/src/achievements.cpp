// Achievements: the titles' lists and the profiles' unlocks (see x360/achievements.h).
#include "x360/achievements.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "x360/profile.h"
#include "x360/profiles.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::achievements {

namespace {

constexpr int kFormat = 1;  // achievements.json's and the unlocks' "version"

// XDBF string tables are numbered as XLanguage up to 9; then simplified Chinese, Polish, Russian.
constexpr struct {
  uint32_t xdbf;
  const char* tag;
} kXdbfLanguages[] = {{1, "en"}, {2, "ja"}, {3, "de"},       {4, "fr"},       {5, "es"},  {6, "it"},
                      {7, "ko"}, {8, "zh-hant"}, {9, "pt"}, {10, "zh-hans"}, {11, "pl"}, {12, "ru"}};

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

uint32_t Number(const rapidjson::Value& object, const char* key) {
  if (!object.IsObject()) return 0;
  auto found = object.FindMember(key);
  return found != object.MemberEnd() && found->value.IsUint() ? found->value.GetUint() : 0;
}

std::string Hex(uint32_t value) {
  char text[12];
  std::snprintf(text, sizeof(text), "%08X", value);
  return text;
}

uint32_t ParseHex(const std::string& text) {
  uint32_t value = 0;
  for (char c : text) {
    const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (digit < 0) return 0;
    value = value << 4 | uint32_t(digit);
  }
  return value;
}

using Writer = rapidjson::PrettyWriter<rapidjson::StringBuffer>;

std::string Finish(const rapidjson::StringBuffer& buffer) { return std::string(buffer.GetString(), buffer.GetSize()) + "\n"; }

std::mutex g_unlock_mutex;  // one read-modify-write of a profile's file at a time (in this process)

}  // namespace

const Strings& Achievement::In(std::string_view language) const {
  static const Strings kNone;
  auto found = strings.find(std::string(language));
  if (found != strings.end() && !found->second.name.empty()) return found->second;
  found = strings.find("en");
  if (found != strings.end()) return found->second;
  return strings.empty() ? kNone : strings.begin()->second;
}

uint32_t TitleList::TotalGamerscore() const {
  uint32_t total = 0;
  for (const Achievement& a : achievements) total += a.gamerscore;
  return total;
}

const Achievement* TitleList::Find(uint32_t id) const {
  for (const Achievement& a : achievements)
    if (a.id == id) return &a;
  return nullptr;
}

const Unlock* TitleUnlocks::Find(uint32_t id) const {
  for (const Unlock& u : unlocks)
    if (u.id == id) return &u;
  return nullptr;
}

uint32_t TitleUnlocks::Gamerscore() const {
  uint32_t total = 0;
  for (const Unlock& u : unlocks) total += u.gamerscore;
  return total;
}

std::string LanguageTag(uint32_t xdbf_language) {
  for (const auto& l : kXdbfLanguages)
    if (l.xdbf == xdbf_language) return l.tag;
  return {};
}

uint32_t XdbfLanguage(std::string_view tag) {
  for (const auto& l : kXdbfLanguages)
    if (tag == l.tag) return l.xdbf;
  return 0;
}

fs::path TitleFolder(const fs::path& folder, uint32_t title_id) { return folder / "titles" / Hex(title_id); }

fs::path ImageFile(const fs::path& folder, uint32_t title_id, uint32_t image_id) {
  return TitleFolder(folder, title_id) / (Hex(image_id) + ".png");
}

std::optional<TitleList> ReadTitleList(const fs::path& folder, uint32_t title_id) {
  const std::optional<std::string> text = ReadFile(TitleFolder(folder, title_id) / "achievements.json");
  if (!text) return std::nullopt;
  rapidjson::Document doc;
  doc.Parse(text->c_str(), text->size());
  if (doc.HasParseError() || !doc.IsObject() || ParseHex(String(doc, "title_id")) != title_id) return std::nullopt;
  TitleList list;
  list.title_id = title_id;
  list.title = String(doc, "title");
  list.source = String(doc, "source");
  auto items = doc.FindMember("achievements");
  if (items == doc.MemberEnd() || !items->value.IsArray()) return list;
  for (const rapidjson::Value& item : items->value.GetArray()) {
    if (!item.IsObject()) continue;
    Achievement a;
    a.id = Number(item, "id");
    a.gamerscore = Number(item, "gamerscore");
    a.image_id = ParseHex(String(item, "image"));
    a.flags = Number(item, "flags");
    auto strings = item.FindMember("strings");
    if (strings != item.MemberEnd() && strings->value.IsObject()) {
      for (auto language = strings->value.MemberBegin(); language != strings->value.MemberEnd(); ++language) {
        if (!language->value.IsObject()) continue;
        a.strings[language->name.GetString()] = {String(language->value, "name"), String(language->value, "locked"),
                                                 String(language->value, "unlocked")};
      }
    }
    list.achievements.push_back(std::move(a));
  }
  return list;
}

bool WriteTitleList(const fs::path& folder, const TitleList& list,
                    const std::map<uint32_t, std::vector<uint8_t>>& images) {
  const fs::path dir = TitleFolder(folder, list.title_id);
  std::error_code ec;
  fs::create_directories(dir, ec);
  // The pictures first: a list read never names one not written yet.
  bool ok = true;
  for (const auto& [id, png] : images) {
    const fs::path file = ImageFile(folder, list.title_id, id);
    if (fs::exists(file, ec) && fs::file_size(file, ec) == png.size()) continue;
    ok &= profiles::WriteAtomic(file, std::string_view(reinterpret_cast<const char*>(png.data()), png.size()));
  }
  rapidjson::StringBuffer buffer;
  Writer w(buffer);
  w.SetIndent(' ', 2);
  w.StartObject();
  w.Key("version"), w.Int(kFormat);
  w.Key("title_id"), w.String(Hex(list.title_id).c_str());
  w.Key("title"), w.String(list.title.c_str());
  w.Key("source"), w.String(list.source.c_str());
  w.Key("achievements");
  w.StartArray();
  for (const Achievement& a : list.achievements) {
    w.StartObject();
    w.Key("id"), w.Uint(a.id);
    w.Key("gamerscore"), w.Uint(a.gamerscore);
    w.Key("image"), w.String(Hex(a.image_id).c_str());
    w.Key("flags"), w.Uint(a.flags);
    w.Key("strings");
    w.StartObject();
    for (const auto& [language, s] : a.strings) {
      w.Key(language.c_str());
      w.StartObject();
      w.Key("name"), w.String(s.name.c_str());
      w.Key("locked"), w.String(s.locked.c_str());
      w.Key("unlocked"), w.String(s.unlocked.c_str());
      w.EndObject();
    }
    w.EndObject();
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return profiles::WriteAtomic(dir / "achievements.json", Finish(buffer)) && ok;
}

fs::path UnlocksFile(const fs::path& folder, uint64_t xuid, uint32_t title_id) {
  return profiles::ProfileFolder(folder, xuid) / "achievements" / (Hex(title_id) + ".json");
}

TitleUnlocks ReadUnlocks(const fs::path& folder, uint64_t xuid, uint32_t title_id) {
  TitleUnlocks result;
  result.title_id = title_id;
  const std::optional<std::string> text = ReadFile(UnlocksFile(folder, xuid, title_id));
  if (!text) return result;
  rapidjson::Document doc;
  doc.Parse(text->c_str(), text->size());
  if (doc.HasParseError() || !doc.IsObject()) return result;
  result.title = String(doc, "title");
  auto items = doc.FindMember("unlocks");
  if (items == doc.MemberEnd() || !items->value.IsArray()) return result;
  for (const rapidjson::Value& item : items->value.GetArray()) {
    if (!item.IsObject() || !item.HasMember("id")) continue;
    Unlock u{Number(item, "id"), Number(item, "gamerscore"), String(item, "date")};
    if (!result.Find(u.id)) result.unlocks.push_back(std::move(u));
  }
  return result;
}

Result Record(const fs::path& folder, uint64_t xuid, uint32_t title_id, std::string_view title, uint32_t id,
              uint32_t gamerscore, std::string_view date) {
  if (!xuid || profiles::IsGuestXuid(xuid)) return Result::kFailed;  // a guest keeps nothing
  std::lock_guard lock(g_unlock_mutex);
  TitleUnlocks unlocks = ReadUnlocks(folder, xuid, title_id);
  if (unlocks.Find(id)) return Result::kAlready;
  if (!title.empty()) unlocks.title = title;
  unlocks.unlocks.push_back({id, gamerscore, date.empty() ? profiles::NowText() : std::string(date)});
  rapidjson::StringBuffer buffer;
  Writer w(buffer);
  w.SetIndent(' ', 2);
  w.StartObject();
  w.Key("version"), w.Int(kFormat);
  w.Key("title_id"), w.String(Hex(title_id).c_str());
  w.Key("title"), w.String(unlocks.title.c_str());
  w.Key("unlocks");
  w.StartArray();
  for (const Unlock& u : unlocks.unlocks) {
    w.StartObject();
    w.Key("id"), w.Uint(u.id);
    w.Key("gamerscore"), w.Uint(u.gamerscore);
    w.Key("date"), w.String(u.date.c_str());
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return profiles::WriteAtomic(UnlocksFile(folder, xuid, title_id), Finish(buffer)) ? Result::kUnlocked
                                                                                   : Result::kFailed;
}

Totals ProfileTotals(const fs::path& folder, uint64_t xuid) {
  Totals totals;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(profiles::ProfileFolder(folder, xuid) / "achievements", ec)) {
    if (entry.path().extension() != ".json") continue;
    const std::string stem = entry.path().stem().string();
    if (stem.size() != 8) continue;
    const uint32_t title_id = ParseHex(stem);
    if (!title_id) continue;
    const TitleUnlocks unlocks = ReadUnlocks(folder, xuid, title_id);
    if (unlocks.unlocks.empty()) continue;
    totals.gamerscore += unlocks.Gamerscore();
    totals.unlocked += uint32_t(unlocks.unlocks.size());
    ++totals.titles;
  }
  return totals;
}

uint64_t FileTime(std::string_view iso) {
  SYSTEMTIME utc{};
  const std::string text(iso);
  if (sscanf_s(text.c_str(), "%4hu-%2hu-%2huT%2hu:%2hu:%2hu", &utc.wYear, &utc.wMonth, &utc.wDay, &utc.wHour,
               &utc.wMinute, &utc.wSecond) != 6)
    return 0;
  FILETIME ft{};
  if (!SystemTimeToFileTime(&utc, &ft)) return 0;
  return uint64_t(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
}

}  // namespace x360::achievements
