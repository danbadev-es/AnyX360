// xam.xex exports: users and profiles, content (saves), enumerators, notifications, input (see
// x360/input.h), loader, tasks and system UI.
//
// Local players: player 1 ("Player") is always signed in; players 2-4 sign in when the title shows
// the sign-in UI (players.sign_in = "on_demand"), at startup ("at_start") or from the settings
// panel, if their slot has a connected device. Player 1 keeps the original paths (<user>\profile,
// <user>\content); the others use <user>\players\<XUID>\.
#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <optional>
#include <random>
#include <set>
#include <tuple>

#include "avatar_system.h"
#include "x360/input.h"
#include "x360/kernel.h"
#include "x360/settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

using namespace x360::kernel;
namespace fs = std::filesystem;

namespace x360::kernel {
void MountContent(const std::string& root_name, const std::filesystem::path& host);
void UnmountContent(const std::string& root_name);
}  // namespace x360::kernel

namespace {

constexpr uint64_t kXuid = 0xE000000012345678ull;  // player 1's offline XUID; player n: kXuid + n - 1

// XNotify IDs (area 0, system).
constexpr uint32_t kNotifySystemUi = 0x00000009;
constexpr uint32_t kNotifySigninChanged = 0x0000000A;
constexpr uint32_t kNotifyStorageDevicesChanged = 0x0000000B;
constexpr uint32_t kNotifyInputDevicesChanged = 0x00000012;

void Broadcast(uint32_t id, uint32_t param);
thread_local bool g_in_signin_ui = false;  // XamShowSigninUI is broadcasting the UI notifications

// ---- overlapped -------------------------------------------------------------------------------
// XOVERLAPPED: result (0), length (4), context (8), event (0xC), completion routine (0x10),
// completion context (0x14), extended error (0x18).

uint32_t Complete(uint32_t overlapped, uint32_t result, uint32_t length = 0, uint32_t extended = 0) {
  if (!overlapped) return result;
  Store<uint32_t>(overlapped + 0x00, result);
  Store<uint32_t>(overlapped + 0x04, length);
  Store<uint32_t>(overlapped + 0x18, extended ? extended : (result ? 0x80070000u | result : 0));
  if (uint32_t event = Load<uint32_t>(overlapped + 0x0C)) {
    if (uint32_t body = ObjectFromHandle(event)) SetEvent(body);
  }
  if (Load<uint32_t>(overlapped + 0x10)) KLOG_W("XOVERLAPPED completion routines are not delivered");
  return X_ERROR_IO_PENDING;
}

uint32_t XamGetOverlappedResult(uint32_t overlapped, uint32_t length_ptr, uint32_t wait) {
  uint32_t result = Load<uint32_t>(overlapped);
  if (result == X_ERROR_IO_PENDING && wait) {
    if (uint32_t event = Load<uint32_t>(overlapped + 0x0C)) {
      if (uint32_t body = ObjectFromHandle(event)) WaitForObject(body, nullptr, false);
    }
    result = Load<uint32_t>(overlapped);
  }
  if (result == X_ERROR_IO_PENDING) return X_ERROR_IO_PENDING;
  if (length_ptr) Store<uint32_t>(length_ptr, Load<uint32_t>(overlapped + 4));
  return result;
}

// ---- enumerators ------------------------------------------------------------------------------

struct Enumerator : Object {
  const char* TypeName() const override { return "Enumerator"; }
  std::vector<std::vector<uint8_t>> items;
  size_t next = 0;
  uint32_t items_per_enumerate = 1;
};

uint32_t XamEnumerate(uint32_t handle, uint32_t flags, uint32_t buffer, uint32_t buffer_size,
                      uint32_t items_returned_ptr, uint32_t overlapped) {
  (void)flags;
  auto e = LookupHandleAs<Enumerator>(handle);
  if (!e) return X_ERROR_INVALID_HANDLE;
  uint32_t count = 0, offset = 0;
  while (e->next < e->items.size() && count < e->items_per_enumerate) {
    const auto& item = e->items[e->next];
    if (offset + item.size() > buffer_size) break;
    std::memcpy(Ptr(buffer + offset), item.data(), item.size());
    offset += uint32_t(item.size());
    ++count;
    ++e->next;
  }
  if (items_returned_ptr) Store<uint32_t>(items_returned_ptr, count);
  uint32_t result = count ? X_ERROR_SUCCESS : X_ERROR_NO_MORE_FILES;
  if (overlapped) return Complete(overlapped, result, count);
  return result;
}

uint32_t CreateEnumerator(uint32_t handle_ptr, uint32_t items_per, std::vector<std::vector<uint8_t>> items) {
  auto e = std::make_shared<Enumerator>();
  e->items = std::move(items);
  e->items_per_enumerate = std::max<uint32_t>(items_per, 1);
  if (handle_ptr) Store<uint32_t>(handle_ptr, CreateHandle(e));
  return X_ERROR_SUCCESS;
}

uint32_t XamCreateEnumeratorHandle(uint32_t user, uint32_t app, uint32_t message, uint32_t message2,
                                   uint32_t extra_size, uint32_t item_count, uint32_t flags, uint32_t handle_ptr) {
  (void)user, (void)app, (void)message, (void)message2, (void)extra_size, (void)flags;
  return CreateEnumerator(handle_ptr, item_count, {});
}

uint32_t XamGetPrivateEnumStructureFromHandle(uint32_t handle, uint32_t out_ptr) {
  (void)handle;
  if (out_ptr) Store<uint32_t>(out_ptr, 0);
  return X_STATUS_UNSUCCESSFUL;
}

// ---- users ------------------------------------------------------------------------------------

struct User {
  bool signed_in = false;
  uint64_t xuid = 0;
  std::string name;
};
std::mutex g_user_mutex;
User g_users[4];

void InitUsers() {
  x360::settings::Settings s = x360::settings::Get();
  std::lock_guard lock(g_user_mutex);
  for (uint32_t i = 0; i < 4; ++i) {
    g_users[i].xuid = kXuid + i;
    // The launcher's profile first, then settings.toml (development runs).
    const std::string& chosen = TitlePaths().player_names[i];
    g_users[i].name = !chosen.empty()              ? chosen
                      : s.players[i].name.empty() ? "Player " + std::to_string(i + 1)
                                                  : s.players[i].name;
    g_users[i].signed_in = i == 0 || (s.sign_in == "at_start" && x360::input::IsConnected(i));
    if (i && g_users[i].signed_in) KLOG_I("users: player %u (%s) signed in at startup", i + 1, g_users[i].name.c_str());
  }
}

uint32_t SignedInMask() {
  std::lock_guard lock(g_user_mutex);
  uint32_t mask = 0;
  for (uint32_t i = 0; i < 4; ++i)
    if (g_users[i].signed_in) mask |= 1u << i;
  return mask;
}

std::optional<User> GetUser(uint32_t user_index) {
  if (user_index >= 4) return std::nullopt;
  std::lock_guard lock(g_user_mutex);
  if (!g_users[user_index].signed_in) return std::nullopt;
  return g_users[user_index];
}

// The player whose XUID this is (-1 if none).
int UserFromXuid(uint64_t xuid) {
  std::lock_guard lock(g_user_mutex);
  for (int i = 0; i < 4; ++i)
    if (g_users[i].xuid == xuid) return i;
  return -1;
}

// Logs calls about players 2-4 once per (function, player, result), to see what the title does
// when another player joins.
void LogOtherUser(const char* function, uint32_t user_index, uint32_t result) {
  if (user_index == 0 || user_index >= 4) return;
  static std::mutex mutex;
  static std::set<std::tuple<const char*, uint32_t, uint32_t>> seen;
  std::lock_guard lock(mutex);
  if (seen.insert({function, user_index, result}).second)
    KLOG_I("users: %s(player %u) -> %08X", function, user_index + 1, result);
}

uint32_t XamUserGetSigninState(uint32_t user_index) {
  uint32_t state = GetUser(user_index) ? 1 : 0;  // 1 = signed in locally
  LogOtherUser("XamUserGetSigninState", user_index, state);
  return state;
}

// XUSER_SIGNIN_INFO: xuid (0), flags (8), state (0xC), guest number (0x10), sponsor (0x14),
// name (char[16], 0x18).
uint32_t XamUserGetSigninInfo(uint32_t user_index, uint32_t flags, uint32_t info) {
  (void)flags;
  std::memset(Ptr(info), 0, 0x28);
  auto user = GetUser(user_index);
  LogOtherUser("XamUserGetSigninInfo", user_index, user ? 0 : X_ERROR_NO_SUCH_USER);
  if (!user) return X_ERROR_NO_SUCH_USER;
  Store<uint64_t>(info + 0x00, user->xuid);
  Store<uint32_t>(info + 0x0C, 1);
  std::strncpy(Ptr<char>(info + 0x18), user->name.c_str(), 15);
  return X_ERROR_SUCCESS;
}

uint32_t XamUserGetXUID(uint32_t user_index, uint32_t type_mask, uint32_t xuid_ptr) {
  (void)type_mask;
  auto user = GetUser(user_index);
  LogOtherUser("XamUserGetXUID", user_index, user ? 0 : X_ERROR_NO_SUCH_USER);
  if (!user) return X_ERROR_NO_SUCH_USER;
  if (xuid_ptr) Store<uint64_t>(xuid_ptr, user->xuid);
  return X_ERROR_SUCCESS;
}

uint32_t XamUserGetName(uint32_t user_index, uint32_t buffer, uint32_t size) {
  auto user = GetUser(user_index);
  LogOtherUser("XamUserGetName", user_index, user ? 0 : X_ERROR_NO_SUCH_USER);
  if (!user) return X_ERROR_NO_SUCH_USER;
  if (size) {
    std::string name = user->name;
    name.resize(std::min<size_t>(name.size(), size - 1));
    std::memcpy(Ptr(buffer), name.c_str(), name.size() + 1);
  }
  return X_ERROR_SUCCESS;
}

uint32_t XamUserCheckPrivilege(uint32_t user_index, uint32_t privilege, uint32_t result_ptr) {
  (void)privilege;
  if (user_index != 0xFF && !GetUser(user_index)) return X_ERROR_NO_SUCH_USER;
  LogOtherUser("XamUserCheckPrivilege", user_index, 0);
  if (result_ptr) Store<uint32_t>(result_ptr, 1);
  return X_ERROR_SUCCESS;
}

uint32_t XamUserAreUsersFriends(uint32_t user_index, uint32_t xuids, uint32_t count, uint32_t result_ptr,
                                uint32_t overlapped) {
  (void)user_index, (void)xuids, (void)count;
  if (result_ptr) Store<uint32_t>(result_ptr, 0);
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

uint32_t XamUserGetDeviceContext(uint32_t user_index, uint32_t unknown, uint32_t out_ptr) {
  (void)unknown;
  if (out_ptr) Store<uint32_t>(out_ptr, 0);
  return GetUser(user_index) ? X_ERROR_SUCCESS : X_ERROR_NO_SUCH_USER;
}

constexpr uint32_t kHrNoSuchUser = 0x80070525;  // HRESULT_FROM_WIN32(ERROR_NO_SUCH_USER)

uint32_t XamUserGetIndexFromXUID(uint64_t xuid, uint32_t flags, uint32_t index_ptr) {
  (void)flags;
  int user = UserFromXuid(xuid);
  if (user < 0 || !GetUser(uint32_t(user))) return kHrNoSuchUser;
  if (index_ptr) Store<uint32_t>(index_ptr, uint32_t(user));
  return X_ERROR_SUCCESS;
}

// Offline profiles: no Xbox Live membership, the console's country, no cached flags.
uint32_t XamUserGetMembershipTierFromXUID(uint64_t xuid) {
  (void)xuid;
  return 0;
}
uint32_t XamUserGetOnlineCountryFromXUID(uint64_t xuid) {
  (void)xuid;
  return TitlePaths().country;  // as ExGetXConfigSetting reports
}
uint32_t XamUserGetCachedUserFlags(uint32_t user_index) {
  (void)user_index;
  return 0;
}

// ---- profile settings -------------------------------------------------------------------------
// Setting id: type << 28 | size << 16 | id. Title-specific blobs are persisted on disk.
//
// XUSER_PROFILE_SETTING (40 bytes, the XDK's natural alignment): source (0), user index or XUID
// (8), setting id (0x10), XUSER_DATA: type (0x18), value (0x20; binary and strings: size at 0x20,
// pointer at 0x24). Read and written packed (id at 0x0C) until 0.7.0, no title's blob was ever saved.

std::mutex g_profile_mutex;
std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> g_profile_blobs;  // (player, id)

// Player 1 keeps the original folders; players 2-4 have theirs under players\<XUID>.
fs::path UserRoot(uint32_t user_index) {
  if (user_index == 0 || user_index >= 4) return TitlePaths().user_root;
  char xuid[24];
  std::snprintf(xuid, sizeof(xuid), "%016llX", (unsigned long long)(kXuid + user_index));
  return TitlePaths().user_root / "players" / xuid;
}

fs::path ProfileDir(uint32_t user_index) { return UserRoot(user_index) / "profile"; }

std::vector<uint8_t>* ProfileBlob(uint32_t user_index, uint32_t id) {
  auto it = g_profile_blobs.find({user_index, id});
  if (it != g_profile_blobs.end()) return &it->second;
  char name[32];
  std::snprintf(name, sizeof(name), "%08X.bin", id);
  std::ifstream in(ProfileDir(user_index) / name, std::ios::binary);
  if (!in) return nullptr;
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
  return &(g_profile_blobs[{user_index, id}] = std::move(data));
}

uint32_t ReadProfileSettings(uint32_t title_id, uint32_t user_index, uint32_t xuid_count, uint32_t xuids,
                             uint32_t setting_count, uint32_t setting_ids, uint32_t buffer_size_ptr, uint32_t buffer,
                             uint32_t overlapped) {
  (void)title_id;
  (void)xuids;
  if (setting_count < 1 || setting_count > 64 || !buffer_size_ptr) return X_ERROR_INVALID_PARAMETER;
  uint32_t users = xuids ? std::max<uint32_t>(xuid_count, 1) : 1;
  uint32_t header = 8 + setting_count * 40 * users;
  uint32_t data = 0;
  for (uint32_t i = 0; i < setting_count; ++i) {
    uint32_t id = Load<uint32_t>(setting_ids + i * 4);
    uint32_t type = id >> 28;
    if (type == 4 || type == 6) data += (id >> 16) & 0xFFF;
  }
  uint32_t needed = header + data * users;
  uint32_t buffer_size = Load<uint32_t>(buffer_size_ptr);
  if (!buffer || buffer_size < needed) {
    if (buffer)
      KLOG_I("XamUserReadProfileSettings(%u settings, first %08X): buffer %u bytes, %u needed", setting_count,
             Load<uint32_t>(setting_ids), buffer_size, needed);
    Store<uint32_t>(buffer_size_ptr, needed);
    return X_ERROR_INSUFFICIENT_BUFFER;
  }
  // The settings of one player: by index, or by the first XUID.
  if (xuids) {
    int from_xuid = UserFromXuid(Load<uint64_t>(xuids));
    user_index = from_xuid >= 0 ? uint32_t(from_xuid) : 0;
  }
  auto user = GetUser(user_index);
  LogOtherUser("XamUserReadProfileSettings", user_index, user ? 0 : X_ERROR_NO_SUCH_USER);
  if (!user) return X_ERROR_NO_SUCH_USER;

  std::lock_guard lock(g_profile_mutex);
  std::memset(Ptr(buffer), 0, needed);
  uint32_t settings = buffer + 8;
  uint32_t extra = buffer + header;
  Store<uint32_t>(buffer + 0, setting_count);
  Store<uint32_t>(buffer + 4, settings);
  for (uint32_t i = 0; i < setting_count; ++i) {
    uint32_t id = Load<uint32_t>(setting_ids + i * 4);
    uint32_t s = settings + i * 40;
    uint32_t type = id >> 28, size = (id >> 16) & 0xFFF;
    Store<uint32_t>(s + 0x00, 1);  // source: default
    if (xuids) {
      Store<uint64_t>(s + 0x08, user->xuid);
    } else {
      Store<uint32_t>(s + 0x08, user_index);
      Store<uint32_t>(s + 0x0C, 0xFFFFFFFF);
    }
    Store<uint32_t>(s + 0x10, id);
    Store<uint8_t>(s + 0x18, uint8_t(type));
    if (type == 4 || type == 6) {
      std::vector<uint8_t>* blob = ProfileBlob(user_index, id);
      uint32_t length = blob ? std::min<uint32_t>(uint32_t(blob->size()), size) : 0;
      if (blob) {
        std::memcpy(Ptr(extra), blob->data(), length);
        Store<uint32_t>(s + 0x00, 2);  // source: title
      } else if ((id & 0xFFFF) >= 0x3FFD) {
        Store<uint32_t>(s + 0x00, 0);  // title-specific blob never written: no value
      }
      Store<uint32_t>(s + 0x20, length);
      Store<uint32_t>(s + 0x24, extra);
      extra += size;
    }
  }
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

uint32_t XamUserReadProfileSettings(uint32_t title_id, uint32_t user_index, uint32_t xuid_count, uint32_t xuids,
                                    uint32_t setting_count, uint32_t setting_ids, uint32_t buffer_size_ptr,
                                    uint32_t buffer, uint32_t overlapped) {
  return ReadProfileSettings(title_id, user_index, xuid_count, xuids, setting_count, setting_ids, buffer_size_ptr,
                             buffer, overlapped);
}

uint32_t XamUserWriteProfileSettings(uint32_t title_id, uint32_t user_index, uint32_t setting_count,
                                     uint32_t settings, uint32_t overlapped) {
  (void)title_id;
  if (user_index >= 4) user_index = 0;
  LogOtherUser("XamUserWriteProfileSettings", user_index, 0);
  KLOG_I("XamUserWriteProfileSettings(player %u, %u settings, overlapped %08X)", user_index + 1, setting_count,
         overlapped);
  std::lock_guard lock(g_profile_mutex);
  std::error_code ec;
  fs::create_directories(ProfileDir(user_index), ec);
  for (uint32_t i = 0; i < setting_count; ++i) {
    uint32_t s = settings + i * 40;
    uint32_t id = Load<uint32_t>(s + 0x10);
    uint32_t type = Load<uint8_t>(s + 0x18);
    if (type != 4 && type != 6) continue;
    uint32_t length = std::min<uint32_t>(Load<uint32_t>(s + 0x20), (id >> 16) & 0xFFF);
    uint32_t data = Load<uint32_t>(s + 0x24);
    std::vector<uint8_t> blob(Ptr(data), Ptr(data) + length);
    char name[32];
    std::snprintf(name, sizeof(name), "%08X.bin", id);
    std::ofstream(ProfileDir(user_index) / name, std::ios::binary)
        .write(reinterpret_cast<const char*>(blob.data()), length);
    g_profile_blobs[{user_index, id}] = std::move(blob);
  }
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

uint32_t XamUserCreateAchievementEnumerator(uint32_t title_id, uint32_t user_index, uint32_t xuid, uint32_t flags,
                                            uint32_t offset, uint32_t count, uint32_t buffer_size_ptr,
                                            uint32_t handle_ptr) {
  (void)title_id, (void)user_index, (void)xuid, (void)flags, (void)offset;
  if (buffer_size_ptr) Store<uint32_t>(buffer_size_ptr, count * 0x1C);
  return CreateEnumerator(handle_ptr, count, {});
}

uint32_t XamUserCreateStatsEnumerator(uint32_t title_id, uint32_t user_index, uint32_t xuid, uint32_t count,
                                      uint32_t specs, uint32_t specs_ptr, uint32_t buffer_size_ptr,
                                      uint32_t handle_ptr) {
  (void)title_id, (void)user_index, (void)xuid, (void)specs, (void)specs_ptr;
  if (buffer_size_ptr) Store<uint32_t>(buffer_size_ptr, 0x100);
  return CreateEnumerator(handle_ptr, count, {});
}

// ---- content (saves) --------------------------------------------------------------------------
// XCONTENT_DATA: device id (0), content type (4), display name (UTF-16, 8), file name (char[42], 0x108).

constexpr uint32_t kContentDataSize = 0x134;
constexpr uint32_t kHddDeviceId = 1;

constexpr uint32_t kContentMarketplace = 0x00000002;

// <user>\content\<type>. Downloadable content belongs to the console, not a player: every player
// sees the shared folder's (the launcher extracts a game's DLC there).
fs::path ContentFolder(uint32_t user_index, uint32_t type) {
  char type_dir[16];
  std::snprintf(type_dir, sizeof(type_dir), "%08X", type);
  return UserRoot(type == kContentMarketplace ? 0 : user_index) / "content" / type_dir;
}

fs::path ContentPath(uint32_t user_index, uint32_t content_data) {
  uint32_t type = Load<uint32_t>(content_data + 4);
  std::string file(Ptr<const char>(content_data + 0x108), strnlen(Ptr<const char>(content_data + 0x108), 42));
  return ContentFolder(user_index, type) / file;
}

uint32_t XamContentCreateEx(uint32_t user_index, uint32_t root_name_ptr, uint32_t content_data, uint32_t flags,
                            uint32_t disposition_ptr, uint32_t license_mask_ptr, uint32_t cache_size,
                            uint64_t content_size, uint32_t overlapped) {
  (void)cache_size, (void)content_size;
  std::string root = Ptr<const char>(root_name_ptr);
  fs::path path = ContentPath(user_index, content_data);
  std::error_code ec;
  bool exists = fs::exists(path, ec);
  uint32_t mode = flags & 0xF;  // 1 create new, 2 create always, 3 open existing, 4 open always, 5 truncate
  uint32_t result = X_ERROR_SUCCESS;
  uint32_t disposition = 0;
  if ((mode == 3 || mode == 5) && !exists) {
    result = X_ERROR_PATH_NOT_FOUND;
  } else if (mode == 1 && exists) {
    result = X_ERROR_ALREADY_EXISTS;
  } else {
    if (exists && (mode == 2 || mode == 5)) fs::remove_all(path, ec);
    disposition = exists && mode != 2 && mode != 5 ? 2 : 1;  // 1 created, 2 opened
    MountContent(root, path);
  }
  KLOG_I("XamContentCreateEx %s (user %u): -> %s (%u)", root.c_str(), user_index, path.string().c_str(), result);
  if (disposition_ptr) Store<uint32_t>(disposition_ptr, disposition);
  if (license_mask_ptr) Store<uint32_t>(license_mask_ptr, 0xFFFFFFFF);
  return overlapped ? Complete(overlapped, result) : result;
}

uint32_t XamContentClose(uint32_t root_name_ptr, uint32_t overlapped) {
  UnmountContent(Ptr<const char>(root_name_ptr));
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

uint32_t XamContentDelete(uint32_t user_index, uint32_t content_data, uint32_t overlapped) {
  std::error_code ec;
  fs::remove_all(ContentPath(user_index, content_data), ec);
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

// The system's own content (no player): kept in the title's base folder.
uint32_t XamContentCreateInternal(uint32_t root_name_ptr, uint32_t content_data, uint32_t flags,
                                  uint32_t disposition_ptr, uint32_t license_mask_ptr, uint32_t cache_size,
                                  uint64_t content_size, uint32_t overlapped) {
  return XamContentCreateEx(0xFE, root_name_ptr, content_data, flags, disposition_ptr, license_mask_ptr, cache_size,
                            content_size, overlapped);
}
uint32_t XamContentDeleteInternal(uint32_t content_data, uint32_t overlapped) {
  return XamContentDelete(0xFE, content_data, overlapped);
}
uint32_t XamContentFlush(uint32_t root_name_ptr, uint32_t overlapped) {
  (void)root_name_ptr;
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}
uint32_t XamContentOpenFile() { return X_ERROR_FILE_NOT_FOUND; }

uint32_t XamContentCreateEnumerator(uint32_t user_index, uint32_t device_id, uint32_t content_type,
                                    uint32_t content_flags, uint32_t items_per, uint32_t buffer_size_ptr,
                                    uint32_t handle_ptr) {
  (void)content_flags;
  LogOtherUser("XamContentCreateEnumerator", user_index, 0);
  if (buffer_size_ptr) Store<uint32_t>(buffer_size_ptr, kContentDataSize * std::max<uint32_t>(items_per, 1));
  std::vector<std::vector<uint8_t>> items;
  if (device_id == 0 || device_id == kHddDeviceId) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(ContentFolder(user_index, content_type), ec)) {
      std::vector<uint8_t> item(kContentDataSize, 0);
      x360::store_be<uint32_t>(item.data() + 0, kHddDeviceId);
      x360::store_be<uint32_t>(item.data() + 4, content_type);
      // The file name field holds 42 characters with no terminator: a package's name (as
      // downloadable content has, "81FC9DE0...4D") uses all of them.
      std::string name = entry.path().filename().string().substr(0, 42);
      for (size_t i = 0; i < name.size() && i < 41; ++i)
        x360::store_be<uint16_t>(item.data() + 8 + i * 2, uint8_t(name[i]));
      std::memcpy(item.data() + 0x108, name.data(), name.size());
      items.push_back(std::move(item));
    }
  }
  return CreateEnumerator(handle_ptr, items_per, std::move(items));
}

uint32_t XamContentGetCreator(uint32_t user_index, uint32_t content_data, uint32_t is_creator_ptr, uint32_t xuid_ptr,
                              uint32_t overlapped) {
  (void)content_data;
  if (is_creator_ptr) Store<uint32_t>(is_creator_ptr, 1);
  if (xuid_ptr) Store<uint64_t>(xuid_ptr, kXuid + (user_index < 4 ? user_index : 0));
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

uint32_t XamContentGetDeviceData(uint32_t device_id, uint32_t data) {
  if (device_id != kHddDeviceId) return X_ERROR_DEVICE_NOT_CONNECTED;
  std::memset(Ptr(data), 0, 0x50);
  Store<uint32_t>(data + 0x00, kHddDeviceId);
  Store<uint32_t>(data + 0x04, 1);  // device type: HDD
  // Sizes just under multiples of 4 GiB: Kinect Sports compares twice its save's size with only the
  // low 32 bits of the free space, and with exactly 16 GiB free said there wasn't room.
  Store<uint64_t>(data + 0x08, (20ull << 30) - (1u << 20));
  Store<uint64_t>(data + 0x10, (16ull << 30) - (1u << 20));
  const char* name = "Hard Drive";
  for (size_t i = 0; name[i]; ++i) Store<uint16_t>(data + 0x18 + uint32_t(i) * 2, uint8_t(name[i]));
  return X_ERROR_SUCCESS;
}

// The device's name (UTF-16) into a buffer of `capacity` characters, terminator included.
uint32_t XamContentGetDeviceName(uint32_t device_id, uint32_t buffer, uint32_t capacity) {
  if (device_id != kHddDeviceId) return X_ERROR_DEVICE_NOT_CONNECTED;
  const char* name = "Hard Drive";
  uint32_t length = uint32_t(std::strlen(name));
  if (capacity < length + 1) return X_ERROR_INSUFFICIENT_BUFFER;
  for (uint32_t i = 0; i <= length; ++i) Store<uint16_t>(buffer + i * 2, uint8_t(name[i]));
  return X_ERROR_SUCCESS;
}

uint32_t XamContentGetDeviceState(uint32_t device_id, uint32_t overlapped) {
  uint32_t result = device_id == kHddDeviceId || device_id == 0 ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  return overlapped ? Complete(overlapped, result) : result;
}

uint32_t XamContentGetLicenseMask(uint32_t mask_ptr, uint32_t overlapped) {
  if (mask_ptr) Store<uint32_t>(mask_ptr, 0xFFFFFFFF);
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

uint32_t XamShowDeviceSelectorUI(uint32_t user_index, uint32_t content_type, uint32_t content_flags,
                                 uint64_t total_requested, uint32_t device_id_ptr, uint32_t overlapped) {
  (void)user_index, (void)content_type, (void)content_flags, (void)total_requested;
  if (device_id_ptr) Store<uint32_t>(device_id_ptr, kHddDeviceId);
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

// ---- notifications ----------------------------------------------------------------------------

// A listener is waitable, as on the console: its guest body is a notification event, signalled
// while notifications are queued (Kinect Adventures waits on it with the title's other events).
struct Listener : Object {
  Listener() {
    guest = SystemAlloc(sizeof(DispatchHeader));
    InitializeDispatcher(guest, kEventNotification, 0);
  }
  const char* TypeName() const override { return "NotifyListener"; }
  uint64_t mask = 0;
  std::deque<std::pair<uint32_t, uint32_t>> queue;

  void Push(uint32_t id, uint32_t param) {
    queue.push_back({id, param});
    SetEvent(guest);
  }
};
std::mutex g_listener_mutex;
std::vector<std::weak_ptr<Listener>> g_listeners;

// Queues a notification for every listener of its area (bits 25+ of the ID).
void Broadcast(uint32_t id, uint32_t param) {
  KLOG_I("notify: %08X (%08X)", id, param);
  uint64_t bit = 1ull << ((id >> 25) & 0x3F);
  std::lock_guard lock(g_listener_mutex);
  std::erase_if(g_listeners, [](const std::weak_ptr<Listener>& l) { return l.expired(); });
  for (const auto& weak : g_listeners)
    if (auto listener = weak.lock(); listener && (listener->mask & bit)) listener->Push(id, param);
}

uint32_t XamNotifyCreateListener(uint64_t mask, uint32_t unknown) {
  (void)unknown;
  auto listener = std::make_shared<Listener>();
  listener->mask = mask;
  // A real console reports the signed-in users and storage right after a listener is created.
  if (mask & 1) {
    listener->Push(kNotifySigninChanged, SignedInMask());
    listener->Push(kNotifyStorageDevicesChanged, 0);
  }
  {
    std::lock_guard lock(g_listener_mutex);
    g_listeners.push_back(listener);
  }
  uint32_t handle = CreateHandle(listener);
  KLOG_I("XamNotifyCreateListener(mask %llX) -> %08X", (unsigned long long)mask, handle);
  return handle;
}

uint32_t XNotifyGetNext(uint32_t handle, uint32_t match_id, uint32_t id_ptr, uint32_t param_ptr) {
  auto listener = LookupHandleAs<Listener>(handle);
  if (!listener) return 0;
  std::lock_guard lock(g_listener_mutex);
  for (auto it = listener->queue.begin(); it != listener->queue.end(); ++it) {
    if (match_id && it->first != match_id) continue;
    if (id_ptr) Store<uint32_t>(id_ptr, it->first);
    if (param_ptr) Store<uint32_t>(param_ptr, it->second);
    listener->queue.erase(it);
    if (listener->queue.empty()) ResetEvent(listener->guest);
    return 1;
  }
  return 0;
}

void XNotifyPositionUI(uint32_t position) { (void)position; }

// ---- input ------------------------------------------------------------------------------------
// X_INPUT_GAMEPAD: buttons (be16), left/right trigger (u8), thumbs lx/ly/rx/ry (be16).

bool ReadPad(uint32_t user_index, XINPUT_GAMEPAD& pad) {
  x360::input::Pad p;
  if (!x360::input::GetPad(user_index, p)) return false;
  pad = {p.buttons, p.left_trigger, p.right_trigger, p.lx, p.ly, p.rx, p.ry};
  return true;
}

void StorePad(uint32_t at, const XINPUT_GAMEPAD& pad) {
  Store<uint16_t>(at + 0, pad.wButtons);
  Store<uint8_t>(at + 2, pad.bLeftTrigger);
  Store<uint8_t>(at + 3, pad.bRightTrigger);
  Store<int16_t>(at + 4, pad.sThumbLX);
  Store<int16_t>(at + 6, pad.sThumbLY);
  Store<int16_t>(at + 8, pad.sThumbRX);
  Store<int16_t>(at + 10, pad.sThumbRY);
}

uint32_t XamInputGetCapabilities(uint32_t user_index, uint32_t flags, uint32_t caps) {
  (void)flags;
  user_index &= 0xFF;
  XINPUT_GAMEPAD pad;
  if (user_index >= 4 || !ReadPad(user_index, pad)) return X_ERROR_DEVICE_NOT_CONNECTED;
  std::memset(Ptr(caps), 0, 0x14);
  Store<uint8_t>(caps + 0, 1);  // XINPUT_DEVTYPE_GAMEPAD
  Store<uint8_t>(caps + 1, 1);  // XINPUT_DEVSUBTYPE_GAMEPAD
  XINPUT_GAMEPAD full{0xFFFF, 0xFF, 0xFF, -1, -1, -1, -1};
  StorePad(caps + 4, full);
  Store<uint16_t>(caps + 0x10, 0xFFFF);
  Store<uint16_t>(caps + 0x12, 0xFFFF);
  return X_ERROR_SUCCESS;
}

uint32_t XamInputGetState(uint32_t user_index, uint32_t flags, uint32_t state) {
  static std::atomic<int> logged{0};
  if (logged++ < 4) KLOG_I("XamInputGetState(%u, %X, %08X)", user_index, flags, state);
  user_index &= 0xFF;
  static std::atomic<uint32_t> packet[4] = {1, 1, 1, 1};
  XINPUT_GAMEPAD pad;
  if (user_index >= 4 || !ReadPad(user_index, pad)) return X_ERROR_DEVICE_NOT_CONNECTED;
  if (state) {
    Store<uint32_t>(state, packet[user_index]++);
    StorePad(state + 4, pad);
  }
  return X_ERROR_SUCCESS;
}

uint32_t XamInputSetState(uint32_t user_index, uint32_t unknown, uint32_t vibration) {
  (void)unknown;
  user_index &= 0xFF;
  if (user_index >= 4) return X_ERROR_DEVICE_NOT_CONNECTED;
  x360::input::SetVibration(user_index, Load<uint16_t>(vibration), Load<uint16_t>(vibration + 2));
  return X_ERROR_SUCCESS;
}

// X_INPUT_CAPABILITIES_EX: the 0x14-byte capabilities followed by 0xC bytes of device details.
uint32_t XamInputGetCapabilitiesEx(uint32_t version, uint32_t user_index, uint32_t flags, uint32_t caps) {
  if (version > 1) return X_ERROR_NOT_SUPPORTED;
  std::memset(Ptr(caps), 0, 0x20);
  if ((user_index & 0xFF) == 0xFF) user_index = 0;  // any player
  return XamInputGetCapabilities(user_index, flags, caps);
}
uint32_t XamInputSendStayAliveRequest() { return X_ERROR_SUCCESS; }

// Keystrokes (X_INPUT_KEYSTROKE, 8 bytes: text from a chatpad or keyboard, and the pad's buttons as
// virtual keys): there are none; games read the pad through XamInputGetState.
uint32_t XamInputGetKeystroke(uint32_t user_index, uint32_t flags, uint32_t keystroke) {
  (void)user_index, (void)flags;
  if (keystroke) std::memset(Ptr(keystroke), 0, 8);
  return X_ERROR_EMPTY;
}
uint32_t XamInputGetKeystrokeEx(uint32_t user_index_ptr, uint32_t flags, uint32_t keystroke) {
  (void)user_index_ptr, (void)flags;
  if (keystroke) std::memset(Ptr(keystroke), 0, 8);
  return X_ERROR_EMPTY;
}
uint32_t XamInputControl() { return X_ERROR_SUCCESS; }
uint32_t XamInputRawState() { return X_ERROR_DEVICE_NOT_CONNECTED; }

// Inactivity (the dashboard's screen dimming) doesn't apply.
uint32_t XamEnableInactivityProcessing() { return X_ERROR_SUCCESS; }
void XamResetInactivity() {}

// ---- loader / process -------------------------------------------------------------------------

// Launch data: what a title leaves for the next one it starts (XamLoaderSetLaunchData, then
// XamLoaderLaunchTitle). The title is never started with any, but it reads back what it set itself,
// as on the console and in Xenia.
std::mutex g_launch_data_mutex;
std::vector<uint8_t> g_launch_data;

uint32_t XamLoaderSetLaunchData(uint32_t data, uint32_t size) {
  std::lock_guard lock(g_launch_data_mutex);
  g_launch_data.assign(Ptr<uint8_t>(data), Ptr<uint8_t>(data) + (data ? size : 0));
  return X_ERROR_SUCCESS;
}
uint32_t XamLoaderGetLaunchDataSize(uint32_t size_ptr) {
  std::lock_guard lock(g_launch_data_mutex);
  if (size_ptr) Store<uint32_t>(size_ptr, uint32_t(g_launch_data.size()));
  return g_launch_data.empty() ? X_ERROR_NOT_FOUND : X_ERROR_SUCCESS;
}
uint32_t XamLoaderGetLaunchData(uint32_t buffer, uint32_t size) {
  std::lock_guard lock(g_launch_data_mutex);
  if (g_launch_data.empty()) return X_ERROR_NOT_FOUND;
  std::memcpy(Ptr(buffer), g_launch_data.data(), std::min<size_t>(size, g_launch_data.size()));
  return size < g_launch_data.size() ? X_ERROR_INSUFFICIENT_BUFFER : X_ERROR_SUCCESS;
}
[[noreturn]] void XamLoaderLaunchTitle(uint32_t name_ptr, uint32_t flags) {
  KLOG_I("XamLoaderLaunchTitle(%s, %X): exiting", name_ptr ? Ptr<const char>(name_ptr) : "", flags);
  std::fflush(nullptr);
  std::_Exit(0);
}
[[noreturn]] void XamLoaderTerminateTitle() {
  KLOG_I("XamLoaderTerminateTitle: exiting");
  std::fflush(nullptr);
  std::_Exit(0);
}

uint32_t XamGetExecutionId(uint32_t info_ptr) {
  // Point at the EXECUTION_INFO record inside our copy of the XEX header.
  const LoadedTitle& t = Title();
  uint32_t count = Load<uint32_t>(t.xex_header + 0x14);
  for (uint32_t i = 0; i < count; ++i) {
    if (Load<uint32_t>(t.xex_header + 0x18 + i * 8) == 0x00040006) {
      Store<uint32_t>(info_ptr, t.xex_header + Load<uint32_t>(t.xex_header + 0x1C + i * 8));
      return X_STATUS_SUCCESS;
    }
  }
  return X_STATUS_UNSUCCESSFUL;
}

uint32_t XamGetSystemVersion() { return (2u << 28) | (0u << 24) | (17559u << 8); }

uint32_t XamAlloc(uint32_t flags, uint32_t size, uint32_t out_ptr) {
  (void)flags;
  uint32_t address = HeapSystem().Alloc(size);
  if (!address) return X_ERROR_NOT_FOUND;
  std::memset(Ptr(address), 0, (size + 0xFFF) & ~0xFFFu);
  Store<uint32_t>(out_ptr, address);
  return X_ERROR_SUCCESS;
}
uint32_t XamFree(uint32_t address) {
  HeapSystem().Free(address);
  return X_ERROR_SUCCESS;
}

// XamTaskSchedule(callback, message, unknown, handle_ptr): runs callback(message) on a new thread.
uint32_t XamTaskSchedule(uint32_t callback, uint32_t message, uint32_t unknown, uint32_t handle_ptr) {
  (void)unknown;
  std::shared_ptr<Thread> thread;
  uint32_t status = CreateThread(0x10000, 0, callback, message, 0, &thread);
  if (status != X_STATUS_SUCCESS) return status;
  if (handle_ptr) Store<uint32_t>(handle_ptr, thread->handle);
  return X_STATUS_SUCCESS;
}
uint32_t XamTaskShouldExit(uint32_t handle) {
  (void)handle;
  return 0;
}
uint32_t XamTaskCloseHandle(uint32_t handle) {
  CloseHandle(handle);
  return X_STATUS_SUCCESS;
}

// ---- system UI --------------------------------------------------------------------------------

// The sign-in UI: signs in up to `count` more players whose slot has a connected device (unless
// players.sign_in is "manual"), as if they had picked their profile.
uint32_t XamShowSigninUI(uint32_t count, uint32_t flags) {
  KLOG_I("XamShowSigninUI(%u, %X)", count, flags);
  Broadcast(kNotifySystemUi, 1);
  g_in_signin_ui = true;
  if (x360::settings::Get().sign_in != "manual") {
    uint32_t remaining = std::max<uint32_t>(count, 1);
    for (uint32_t i = 1; i < 4 && remaining; ++i) {
      if (IsUserSignedIn(i) || !x360::input::IsConnected(i)) continue;
      SignInUser(i);
      --remaining;
    }
  }
  g_in_signin_ui = false;
  Broadcast(kNotifySystemUi, 0);
  return X_ERROR_SUCCESS;
}

// A message box: nobody sees it, the button with the focus is pressed at once. The text is logged.
// XamShowMessageBoxUI(user, title, text, button count, buttons, focus, flags, result*, overlapped);
// the Ex form has one more argument before result* (the result and the overlapped are then the 9th
// and 10th arguments, on the stack).
uint32_t ShowMessageBox(const char* name, uint32_t title, uint32_t text, uint32_t focus, uint32_t result_ptr,
                        uint32_t overlapped) {
  auto narrow = [](uint32_t w) {
    std::string s;
    for (uint32_t a = w; a && Load<uint16_t>(a); a += 2) s += char(Load<uint16_t>(a) & 0x7F);
    return s;
  };
  KLOG_I("%s: [%s] %s", name, narrow(title).c_str(), narrow(text).c_str());
  if (result_ptr) Store<uint32_t>(result_ptr, focus);
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}
uint32_t XamShowMessageBoxUI(uint32_t user_index, uint32_t title, uint32_t text, uint32_t button_count,
                             uint32_t buttons, uint32_t focus, uint32_t flags, uint32_t result_ptr,
                             uint32_t overlapped) {
  (void)user_index, (void)button_count, (void)buttons, (void)flags;
  return ShowMessageBox("XamShowMessageBoxUI", title, text, focus, result_ptr, overlapped);
}
uint32_t XamShowMessageBoxUIEx(uint32_t user_index, uint32_t title, uint32_t text, uint32_t button_count,
                               uint32_t buttons, uint32_t focus, uint32_t flags, uint32_t unknown,
                               uint32_t result_ptr, uint32_t overlapped) {
  (void)user_index, (void)button_count, (void)buttons, (void)flags, (void)unknown;
  return ShowMessageBox("XamShowMessageBoxUIEx", title, text, focus, result_ptr, overlapped);
}

uint32_t XamShowKeyboardUI(uint32_t user_index, uint32_t flags, uint32_t default_text, uint32_t title,
                           uint32_t description, uint32_t buffer, uint32_t buffer_length, uint32_t overlapped) {
  (void)user_index, (void)flags, (void)default_text, (void)title, (void)description, (void)buffer, (void)buffer_length;
  return overlapped ? Complete(overlapped, X_ERROR_CANCELLED) : X_ERROR_CANCELLED;
}

[[noreturn]] void XamShowDirtyDiscErrorUI(uint32_t user_index) {
  (void)user_index;
  Fatal("the title reported a disc read error (XamShowDirtyDiscErrorUI): check the game files");
}

// The remaining UI entry points show Live/guide screens that do not exist here.
uint32_t XamShowUIStub() { return X_ERROR_SUCCESS; }

// The marketplace: there is none, but the title sees the system UI open and close, as when the
// player backs out. The license stays full (XamContentGetLicenseMask), so an arcade title is never
// a trial and its "unlock full game" offer has nothing to do.
uint32_t XamShowMarketplaceUI(uint32_t user_index, uint32_t ui_type, uint64_t offer_id) {
  KLOG_I("XamShowMarketplaceUI(%u, %u, %016llX)", user_index, ui_type, (unsigned long long)offer_id);
  Broadcast(kNotifySystemUi, 1);
  Broadcast(kNotifySystemUi, 0);
  return X_ERROR_SUCCESS;
}
uint32_t XamShowNuiMarketplaceUI(uint32_t tracking_id, uint32_t user_index, uint32_t ui_type, uint64_t offer_id) {
  (void)tracking_id;
  return XamShowMarketplaceUI(user_index, ui_type, offer_id);
}
// {user, UI type, offers (u64[]), offer count, HRESULT*, overlapped}: nothing downloaded.
uint32_t XamShowMarketplaceDownloadItemsUI(uint32_t user_index, uint32_t ui_type, uint32_t offers,
                                           uint32_t offer_count, uint32_t hresult_ptr, uint32_t overlapped) {
  (void)offers;
  KLOG_I("XamShowMarketplaceDownloadItemsUI(%u, %u, %u offers)", user_index, ui_type, offer_count);
  Broadcast(kNotifySystemUi, 1);
  Broadcast(kNotifySystemUi, 0);
  if (hresult_ptr) Store<uint32_t>(hresult_ptr, 0);
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

// Kinect versions of the system UI take the tracking ID of the skeleton that asked first.
uint32_t XamShowNuiSigninUI(uint32_t tracking_id, uint32_t count, uint32_t flags) {
  (void)tracking_id;
  return XamShowSigninUI(count, flags);
}
uint32_t XamShowNuiDeviceSelectorUI(uint32_t tracking_id, uint32_t user_index, uint32_t content_type,
                                    uint32_t content_flags, uint64_t total_requested, uint32_t device_id_ptr,
                                    uint32_t overlapped) {
  (void)tracking_id;
  return XamShowDeviceSelectorUI(user_index, content_type, content_flags, total_requested, device_id_ptr, overlapped);
}
[[noreturn]] void XamShowNuiDirtyDiscErrorUI(uint32_t tracking_id, uint32_t user_index) {
  (void)tracking_id;
  XamShowDirtyDiscErrorUI(user_index);
}
// The Kinect Guide (a Kinect title shows it when a player holds the guide gesture, the left arm
// down at 45 degrees): there is no overlay here, but the title still sees the system UI open and
// close, and pauses as it would on the console.
uint32_t XamShowNuiGuideUI(uint32_t tracking_id) {
  KLOG_I("XamShowNuiGuideUI(%u)", tracking_id);
  Broadcast(kNotifySystemUi, 1);
  Broadcast(kNotifySystemUi, 0);
  return X_ERROR_SUCCESS;
}
std::atomic<bool> g_kinect_guide_open{false};  // SetKinectGuideOpen
uint32_t XamIsUIActive() { return g_kinect_guide_open ? 1 : 0; }

// ---- sessions, voice, messaging, misc --------------------------------------------------------

struct Session : Object {
  const char* TypeName() const override { return "Session"; }
};

uint32_t XamSessionCreateHandle(uint32_t handle_ptr) {
  auto session = std::make_shared<Session>();
  session->guest = SystemAlloc(0x100);
  if (handle_ptr) Store<uint32_t>(handle_ptr, CreateHandle(session));
  return X_ERROR_SUCCESS;
}

uint32_t XamSessionRefObjByHandle(uint32_t handle, uint32_t out_ptr) {
  uint32_t body = ObjectFromHandle(handle);
  if (out_ptr) Store<uint32_t>(out_ptr, body);
  return body ? X_ERROR_SUCCESS : X_ERROR_INVALID_HANDLE;
}

uint32_t XamVoiceCreate(uint32_t a, uint32_t b, uint32_t out_ptr) {
  (void)a, (void)b;
  if (out_ptr) Store<uint32_t>(out_ptr, 0);
  return X_ERROR_ACCESS_DENIED;
}
uint32_t XamVoiceClose(uint32_t voice) {
  (void)voice;
  return 0;
}
uint32_t XamVoiceHeadsetPresent(uint32_t voice) {
  (void)voice;
  return 0;
}
uint32_t XamVoiceIsActiveProcess() { return 0; }
uint32_t XamVoiceSubmitPacket() { return 0; }

uint32_t XamReadTileToTexture() { return X_ERROR_NOT_FOUND; }
// (user, title, big tile, small tile, for enumeration, overlapped): the title sets the player's gamer
// picture to one of its own. There is none to change: done. Refused, Fruit Ninja Kinect said its
// progress couldn't be saved.
uint32_t XamWriteGamerTile(uint32_t user_index, uint32_t title_id, uint32_t big_tile, uint32_t small_tile,
                           uint32_t for_enumerate, uint32_t overlapped) {
  (void)title_id, (void)big_tile, (void)small_tile, (void)for_enumerate;
  uint32_t result = user_index < 4 ? X_ERROR_SUCCESS : X_ERROR_INVALID_PARAMETER;
  if (overlapped) return Complete(overlapped, result);
  // Called without an overlapped, it leaves its result as the thread's last error (KTHREAD+0x160),
  // which Fruit Ninja Kinect reads instead of the return value; a stale one made it fail.
  if (Thread* thread = CurrentThread()) Store<uint32_t>(thread->kthread + 0x160, result);
  return result;
}

uint32_t XamGetCurrentTitleId() { return Title().title_id; }
uint32_t XamBackgroundDownloadSetMode(uint32_t mode) {
  (void)mode;
  return X_ERROR_SUCCESS;
}

// Xbox SmartGlass ("LRC"): no companion devices ever connect.
uint32_t XamLrcSetTitlePort(uint32_t port) {
  (void)port;
  return X_ERROR_SUCCESS;
}
uint32_t XamLrcFail() { return 0x80004005; }

// ---- avatars ------------------------------------------------------------------------------------
// Avatars are made from the console's avatar asset pack (avatar_system.cpp, docs/AVATARS.md) for
// titles that say how to build a skeleton (GameDescriptor::avatar_skeleton). For the others there's
// no avatar system: initializing it fails (as Xenia's default does) and titles go without avatars.
// Answered as successes with nothing written, the calls below made Kinect Adventures read asset data
// that was never there (a crash loading Reflex Ridge); they say there's none. A title that can't run
// without the system (Kinect Sports dereferences its avatar manager, which it only creates when
// XAvatarInitialize succeeds) gets it started (GameDescriptor::avatar_system); its asset requests
// then succeed with an empty result, as it reads the result whether or not the request worked.
constexpr uint32_t kAvatarFail = 0x80004005;  // E_FAIL, the extended error
constexpr uint32_t kAvatarEmptyAssets = 0x100;  // an XAvatarGetAssets result with no components

// The avatar library calls it as (4, its coordinate system, a processor, its callbacks, a buffer and
// its size) (Kinect Sports 82A820DC): the coordinate system is the second argument, 1 right-handed.
uint32_t XamAvatarInitialize(uint32_t, uint32_t coordinate_system) {
  if (!TitlePaths().avatar_system) return 0xFFFFFFFF;
  x360::avatar::Initialize(coordinate_system);
  return X_ERROR_SUCCESS;
}
void XamAvatarShutdown() {}
uint32_t AvatarFailed(uint32_t overlapped) {
  return overlapped ? Complete(overlapped, X_ERROR_FUNCTION_FAILED, 0, kAvatarFail) : kAvatarFail;
}
uint32_t AvatarDone(uint32_t overlapped) { return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS; }

// A player's avatar: player 1 the default male avatar, the others alternate.
uint32_t XamAvatarGetManifestLocalUser(uint32_t user_index, uint32_t metadata, uint32_t overlapped) {
  if (!x360::avatar::Available() || !metadata) return AvatarFailed(overlapped);
  x360::avatar::WriteMetadata(metadata, (user_index & 1) ? x360::avatar::Look::kFemale : x360::avatar::Look::kMale, 0);
  return AvatarDone(overlapped);
}
uint32_t XamAvatarGetManifestsByXuid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t overlapped) {
  return AvatarFailed(overlapped);
}
uint32_t XamAvatarGetAssetsResultSize(uint32_t component_mask, uint32_t result_size, uint32_t gpu_size) {
  uint32_t result = 0, gpu = 0;
  if (x360::avatar::Available()) {
    x360::avatar::ResultSizes(component_mask, result, gpu);
  } else if (TitlePaths().avatar_system) {
    result = gpu = kAvatarEmptyAssets;
  }
  if (result_size) Store<uint32_t>(result_size, result);
  if (gpu_size) Store<uint32_t>(gpu_size, gpu);
  return X_ERROR_SUCCESS;
}
// (metadata, component mask, flags, result buffer, GPU resource buffer, overlapped): the buffers are
// at least as large as XamAvatarGetAssetsResultSize said (the library checks).
uint32_t XamAvatarGetAssets(uint32_t metadata, uint32_t component_mask, uint32_t flags, uint32_t result,
                            uint32_t gpu_resources, uint32_t overlapped) {
  (void)flags;
  if (!TitlePaths().avatar_system || !result || !gpu_resources) return AvatarFailed(overlapped);
  if (x360::avatar::Available()) {
    uint32_t result_size = 0, gpu_size = 0;
    x360::avatar::ResultSizes(component_mask, result_size, gpu_size);
    std::string error;
    if (x360::avatar::BuildAssets(metadata, component_mask, result, result_size, gpu_resources, gpu_size, error))
      return AvatarDone(overlapped);
    KLOG_W("avatars: %s", error.c_str());
    std::memset(Ptr(result), 0, result_size);
    return AvatarDone(overlapped);
  }
  std::memset(Ptr(result), 0, kAvatarEmptyAssets);
  std::memset(Ptr(gpu_resources), 0, kAvatarEmptyAssets);
  return AvatarDone(overlapped);
}
// Players without a profile get the default avatars (male, female).
uint32_t XamAvatarGetMetadataSignedOutProfileCount(uint32_t count, uint32_t overlapped) {
  if (count) Store<uint32_t>(count, x360::avatar::Available() ? 2 : 0);
  return AvatarDone(overlapped);
}
uint32_t XamAvatarGetMetadataSignedOutProfile(uint32_t profile_index, uint32_t metadata, uint32_t overlapped) {
  if (!x360::avatar::Available() || !metadata) return AvatarFailed(overlapped);
  x360::avatar::WriteMetadata(metadata, (profile_index & 1) ? x360::avatar::Look::kFemale : x360::avatar::Look::kMale, 0);
  return AvatarDone(overlapped);
}
// (body types: 1 male, 2 female, 3 either; count; descriptions; overlapped)
uint32_t XamAvatarGetMetadataRandom(uint32_t body_types, uint32_t count, uint32_t metadata, uint32_t overlapped) {
  if (!x360::avatar::Available() || !metadata) return AvatarFailed(overlapped);
  static std::atomic<uint32_t> seed{0x5EED};
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t m = metadata + i * x360::avatar::kMetadataSize;
    for (;;) {
      x360::avatar::WriteMetadata(m, x360::avatar::Look::kRandom, seed++);
      if ((body_types & 3) == 0 || (body_types & x360::avatar::BodyType(m))) break;
    }
  }
  return AvatarDone(overlapped);
}
uint32_t XamAvatarLoadAnimation(uint32_t, uint32_t, uint32_t, uint32_t overlapped) { return AvatarFailed(overlapped); }
uint32_t XamAvatarManifestGetBodyType(uint32_t metadata) { return x360::avatar::BodyType(metadata); }
uint32_t XamAvatarGenerateMipMaps(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t overlapped) {
  return AvatarFailed(overlapped);
}
uint32_t XamAvatarSetCustomAsset() { return X_ERROR_SUCCESS; }

// ---- Kinect system services ---------------------------------------------------------------------
// The skeleton, cameras and microphones belong to the NUI library linked into the title, which the
// virtual sensor replaces (x360/nui.h). The system only reports a connected sensor and no
// microphone array.

// X_NUI_DEVICE_STATUS: camera, tilt and microphone status (NTSTATUS), status bits.
uint32_t XamNuiGetDeviceStatus(uint32_t status) {
  std::memset(Ptr(status), 0, 0x10);
  Store<uint32_t>(status + 0x0C, 1);  // ready
  return X_ERROR_SUCCESS;
}
uint32_t XamVoiceGetMicArrayStatus() { return 0; }

// ---- XAM "apps" reached through XMsg* (message semantics from Xenia's xam/apps) ----------------

constexpr uint32_t kHrSuccess = 0x00000000;
constexpr uint32_t kHrFail = 0x80004005;
constexpr uint32_t kHrNotLoggedOn = 0x80151802;  // X_ONLINE_E_LOGON_NOT_LOGGED_ON

uint32_t DispatchXamApp(uint32_t app, uint32_t message, uint32_t buffer, uint32_t length) {
  (void)length;
  switch (app) {
    case 0xFC:  // XLiveBase
      switch (message) {
        case 0x00058003: return 0x001510F1;  // logon HR: disconnected
        case 0x00058004:                     // logon id
          if (buffer) Store<uint32_t>(buffer, 1);
          return kHrSuccess;
        case 0x00058006:  // NAT type
        case 0x00058007:  // service info
          return kHrNotLoggedOn;
        case 0x00058037:  // XPresenceInitialize
        case 0x00058046:  // needed to detect the signed-in profile
          return kHrSuccess;
        default:
          return kHrFail;  // friends enumeration, messaging, dashboard queries
      }
    case 0xFB:  // XGI: contexts, properties, achievements, local sessions
      switch (message) {
        case 0x000B0006:  // XUserSetContext
        case 0x000B0007:  // XUserSetPropertyEx
        case 0x000B0008:  // XUserWriteAchievements
        case 0x000B0010:  // XSessionCreate (offline sessions are allowed)
        case 0x000B0011:  // XSessionDelete
        case 0x000B0012:  // XSessionJoinLocal
        case 0x000B0013:  // XSessionLeaveLocal
        case 0x000B0014:  // XSessionStart
        case 0x000B0015:  // XSessionEnd
        case 0x000B0025:  // XSessionWriteStats (kept nowhere: there are no leaderboards)
        case 0x000B0071:  // XUserAwardAvatarAssets
          return kHrSuccess;
        case 0x000B0021:  // XUserReadStats
          return kHrNotLoggedOn;
        default:
          return kHrFail;
      }
    case 0xFA:  // XMP (background music player): always idle
      switch (message) {
        case 0x00070009:  // XMPGetStatus {client, state_ptr}
          if (uint32_t state = Load<uint32_t>(buffer + 4)) Store<uint32_t>(state, 0);
          return kHrSuccess;
        case 0x0007000B: {  // XMPGetVolume {client, volume_ptr}
          float one = 1.0f;
          uint32_t bits;
          std::memcpy(&bits, &one, 4);
          if (uint32_t volume = Load<uint32_t>(buffer + 4)) Store<uint32_t>(volume, bits);
          return kHrSuccess;
        }
        default:
          return kHrSuccess;
      }
    case 0xFE:  // XAM: title support, Kinect
      switch (message) {
        case 0x00021012:  // XEnableGuestSignin {enabled} (Kinect Adventures, every frame)
          return kHrSuccess;
        case 0x00020021:  // XContentQueryVolumeDeviceType {root name[64], title process, type*, overlapped}
          // The title's volume: the hard drive for an arcade title (a download), else the disc.
          if (uint32_t type = Load<uint32_t>(buffer + 0x44)) Store<uint32_t>(type, TitlePaths().arcade ? 1 : 4);
          return kHrSuccess;
        case 0x00022005:  // XTitleGetDeploymentType {type*, overlapped}: 0 the disc, 2 a download
          if (uint32_t type = Load<uint32_t>(buffer)) Store<uint32_t>(type, TitlePaths().arcade ? 2 : 0);
          return kHrSuccess;
        case 0x0002B001:  // XNuiDelayUI {milliseconds}
        case 0x0002B003:  // XNuiTitleInitialize {xam version, XTL version, flags}
        case 0x0002B004:  // XNuiTitleShutdown
        case 0x0002C009:  // identity callback (register or remove)
          return kHrSuccess;
        default:
          return kHrFail;
      }
    default:
      return kHrFail;
  }
}

uint32_t XMsgStartIORequest(uint32_t app, uint32_t message, uint32_t overlapped, uint32_t buffer, uint32_t size) {
  uint32_t hr = DispatchXamApp(app, message, buffer, size);
  if (hr != kHrSuccess) KLOG_I("XMsgStartIORequest(app %X, message %08X) -> %08X", app, message, hr);
  if (overlapped) return Complete(overlapped, hr == kHrSuccess ? X_ERROR_SUCCESS : X_ERROR_FUNCTION_FAILED, 0, hr);
  return hr;
}
uint32_t XMsgStartIORequestEx(uint32_t app, uint32_t message, uint32_t overlapped, uint32_t buffer, uint32_t size,
                              uint32_t unknown) {
  (void)unknown;
  return XMsgStartIORequest(app, message, overlapped, buffer, size);
}
uint32_t XMsgInProcessCall(uint32_t app, uint32_t message, uint32_t arg1, uint32_t arg2) {
  uint32_t hr = DispatchXamApp(app, message, arg1, arg2);
  if (hr != kHrSuccess) KLOG_I("XMsgInProcessCall(app %X, message %08X) -> %08X", app, message, hr);
  return hr;
}
uint32_t XMsgSystemProcessCall(uint32_t app, uint32_t message, uint32_t buffer, uint32_t length) {
  uint32_t hr = DispatchXamApp(app, message, buffer, length);
  if (hr != kHrSuccess) KLOG_I("XMsgSystemProcessCall(app %X, message %08X) -> %08X", app, message, hr);
  return hr;
}
uint32_t XMsgCompleteIORequest() { return X_ERROR_SUCCESS; }
uint32_t XMsgCancelIORequest(uint32_t overlapped, uint32_t wait) {
  (void)wait;
  if (overlapped) Complete(overlapped, X_ERROR_CANCELLED);
  return X_ERROR_SUCCESS;
}

uint32_t XNetLogonGetTitleID() { return Title().title_id; }

uint32_t XGetAVPack() { return 6; }  // HDMI
uint32_t XGetGameRegion() { return 0xFFFF; }
// The player profile's language (Windows' by default): a game without it chooses its own.
uint32_t XGetLanguage() { return TitlePaths().language; }

void XGetVideoMode(uint32_t mode) {
  std::memset(Ptr(mode), 0, 0x30);
  Store<uint32_t>(mode + 0x00, 1280);  // display width
  Store<uint32_t>(mode + 0x04, 720);   // display height
  Store<uint32_t>(mode + 0x08, 0);     // interlaced
  Store<uint32_t>(mode + 0x0C, 1);     // widescreen
  Store<uint32_t>(mode + 0x10, 1);     // HD
  float refresh = 60.0f;
  uint32_t bits;
  std::memcpy(&bits, &refresh, 4);
  Store<uint32_t>(mode + 0x14, bits);
  Store<uint32_t>(mode + 0x18, 8);  // video standard: HDMI-ish
}

}  // namespace

namespace x360::kernel {

void InitXam() {
  InitUsers();
  x360::input::OnDevicesChanged([] { Broadcast(kNotifyInputDevicesChanged, 0); });
}

bool IsUserSignedIn(uint32_t user_index) { return GetUser(user_index).has_value(); }

void SignInUser(uint32_t user_index) {
  if (user_index >= 4) return;
  {
    std::lock_guard lock(g_user_mutex);
    if (g_users[user_index].signed_in) return;
    g_users[user_index].signed_in = true;
  }
  KLOG_I("users: player %u signed in", user_index + 1);
  // As if picked in the system's sign-in UI, which is open while the sign-in changes.
  if (!g_in_signin_ui) Broadcast(kNotifySystemUi, 1);
  Broadcast(kNotifySigninChanged, SignedInMask());
  if (!g_in_signin_ui) Broadcast(kNotifySystemUi, 0);
}

void SetKinectGuideOpen(bool open) {
  if (g_kinect_guide_open.exchange(open) == open) return;
  KLOG_I("nui: the Kinect guide gesture: the system UI %s", open ? "opens" : "closes");
  Broadcast(kNotifySystemUi, open ? 1 : 0);
}

void Notify(uint32_t id, uint32_t param) { Broadcast(id, param); }

void SignOutUser(uint32_t user_index) {
  if (user_index == 0 || user_index >= 4) return;
  {
    std::lock_guard lock(g_user_mutex);
    if (!g_users[user_index].signed_in) return;
    g_users[user_index].signed_in = false;
  }
  KLOG_I("users: player %u signed out", user_index + 1);
  Broadcast(kNotifySystemUi, 1);
  Broadcast(kNotifySigninChanged, SignedInMask());
  Broadcast(kNotifySystemUi, 0);
}

}  // namespace x360::kernel

X360_EXPORT(XamGetOverlappedResult, XamGetOverlappedResult)
X360_EXPORT(XamEnumerate, XamEnumerate)
X360_EXPORT(XamCreateEnumeratorHandle, XamCreateEnumeratorHandle)
X360_EXPORT(XamGetPrivateEnumStructureFromHandle, XamGetPrivateEnumStructureFromHandle)
X360_EXPORT(XamUserGetSigninState, XamUserGetSigninState)
X360_EXPORT(XamUserGetSigninInfo, XamUserGetSigninInfo)
X360_EXPORT(XamUserGetXUID, XamUserGetXUID)
X360_EXPORT(XamUserGetName, XamUserGetName)
X360_EXPORT(XamUserCheckPrivilege, XamUserCheckPrivilege)
X360_EXPORT(XamUserAreUsersFriends, XamUserAreUsersFriends)
X360_EXPORT(XamUserGetDeviceContext, XamUserGetDeviceContext)
X360_EXPORT(XamUserReadProfileSettings, XamUserReadProfileSettings)
X360_EXPORT(XamUserWriteProfileSettings, XamUserWriteProfileSettings)
X360_EXPORT(XamUserCreateAchievementEnumerator, XamUserCreateAchievementEnumerator)
X360_EXPORT(XamUserCreateStatsEnumerator, XamUserCreateStatsEnumerator)
X360_EXPORT(XamContentCreateEx, XamContentCreateEx)
X360_EXPORT(XamContentClose, XamContentClose)
X360_EXPORT(XamContentDelete, XamContentDelete)
X360_EXPORT(XamContentCreateEnumerator, XamContentCreateEnumerator)
X360_EXPORT(XamContentGetCreator, XamContentGetCreator)
X360_EXPORT(XamContentGetDeviceData, XamContentGetDeviceData)
X360_EXPORT(XamContentGetDeviceName, XamContentGetDeviceName)
X360_EXPORT(XamContentGetDeviceState, XamContentGetDeviceState)
X360_EXPORT(XamContentGetLicenseMask, XamContentGetLicenseMask)
X360_EXPORT(XamShowDeviceSelectorUI, XamShowDeviceSelectorUI)
X360_EXPORT(XamNotifyCreateListener, XamNotifyCreateListener)
X360_EXPORT(XNotifyGetNext, XNotifyGetNext)
X360_EXPORT(XNotifyPositionUI, XNotifyPositionUI)
X360_EXPORT(XamInputGetCapabilities, XamInputGetCapabilities)
X360_EXPORT(XamInputGetState, XamInputGetState)
X360_EXPORT(XamInputSetState, XamInputSetState)
X360_EXPORT(XamInputGetKeystroke, XamInputGetKeystroke)
X360_EXPORT(XamInputGetKeystrokeEx, XamInputGetKeystrokeEx)
X360_EXPORT(XamLoaderSetLaunchData, XamLoaderSetLaunchData)
X360_EXPORT(XamLoaderGetLaunchDataSize, XamLoaderGetLaunchDataSize)
X360_EXPORT(XamLoaderGetLaunchData, XamLoaderGetLaunchData)
X360_EXPORT(XamLoaderLaunchTitle, XamLoaderLaunchTitle)
X360_EXPORT(XamLoaderTerminateTitle, XamLoaderTerminateTitle)
X360_EXPORT(XamGetExecutionId, XamGetExecutionId)
X360_EXPORT(XamGetSystemVersion, XamGetSystemVersion)
X360_EXPORT(XamAlloc, XamAlloc)
X360_EXPORT(XamFree, XamFree)
X360_EXPORT(XamTaskSchedule, XamTaskSchedule)
X360_EXPORT(XamTaskShouldExit, XamTaskShouldExit)
X360_EXPORT(XamTaskCloseHandle, XamTaskCloseHandle)
X360_EXPORT(XamShowSigninUI, XamShowSigninUI)
X360_EXPORT(XamShowMessageBoxUI, XamShowMessageBoxUI)
X360_EXPORT(XamShowMessageBoxUIEx, XamShowMessageBoxUIEx)
X360_EXPORT(XamShowKeyboardUI, XamShowKeyboardUI)
X360_EXPORT(XamShowDirtyDiscErrorUI, XamShowDirtyDiscErrorUI)
X360_EXPORT(XamShowAchievementsUI, XamShowUIStub)
X360_EXPORT(XamShowCustomMessageComposeUI, XamShowUIStub)
X360_EXPORT(XamShowCustomPlayerListUI, XamShowUIStub)
X360_EXPORT(XamShowFriendRequestUI, XamShowUIStub)
X360_EXPORT(XamShowFriendsUI, XamShowUIStub)
X360_EXPORT(XamShowGameInviteUI, XamShowUIStub)
X360_EXPORT(XamShowGamerCardUIForXUID, XamShowUIStub)
X360_EXPORT(XamShowMarketplaceUI, XamShowMarketplaceUI)
X360_EXPORT(XamShowMarketplaceUIEx, XamShowMarketplaceUI)
X360_EXPORT(XamShowMessageComposeUI, XamShowUIStub)
X360_EXPORT(XamShowMessagesUI, XamShowUIStub)
X360_EXPORT(XamShowPlayerReviewUI, XamShowUIStub)
X360_EXPORT(XamShowPlayersUI, XamShowUIStub)
X360_EXPORT(XamShowQuickChatUI, XamShowUIStub)
X360_EXPORT(XamSessionCreateHandle, XamSessionCreateHandle)
X360_EXPORT(XamSessionRefObjByHandle, XamSessionRefObjByHandle)
X360_EXPORT(XamVoiceCreate, XamVoiceCreate)
X360_EXPORT(XamVoiceClose, XamVoiceClose)
X360_EXPORT(XamVoiceHeadsetPresent, XamVoiceHeadsetPresent)
X360_EXPORT(XamVoiceIsActiveProcess, XamVoiceIsActiveProcess)
X360_EXPORT(XamVoiceSubmitPacket, XamVoiceSubmitPacket)
X360_EXPORT(XamReadTileToTexture, XamReadTileToTexture)
X360_EXPORT(XamWriteGamerTile, XamWriteGamerTile)
X360_EXPORT(XMsgStartIORequest, XMsgStartIORequest)
X360_EXPORT(XMsgStartIORequestEx, XMsgStartIORequestEx)
X360_EXPORT(XMsgInProcessCall, XMsgInProcessCall)
X360_EXPORT(XMsgCompleteIORequest, XMsgCompleteIORequest)
X360_EXPORT(XMsgCancelIORequest, XMsgCancelIORequest)
X360_EXPORT(XNetLogonGetTitleID, XNetLogonGetTitleID)
X360_EXPORT(XGetAVPack, XGetAVPack)
X360_EXPORT(XGetGameRegion, XGetGameRegion)
X360_EXPORT(XGetLanguage, XGetLanguage)
X360_EXPORT(XGetVideoMode, XGetVideoMode)
X360_EXPORT(XMsgSystemProcessCall, XMsgSystemProcessCall)
X360_EXPORT(XamGetCurrentTitleId, XamGetCurrentTitleId)
X360_EXPORT(XamUserGetIndexFromXUID, XamUserGetIndexFromXUID)
X360_EXPORT(XamUserGetMembershipTierFromXUID, XamUserGetMembershipTierFromXUID)
X360_EXPORT(XamUserGetOnlineCountryFromXUID, XamUserGetOnlineCountryFromXUID)
X360_EXPORT(XamUserGetCachedUserFlags, XamUserGetCachedUserFlags)
X360_EXPORT(XamContentCreateInternal, XamContentCreateInternal)
X360_EXPORT(XamContentDeleteInternal, XamContentDeleteInternal)
X360_EXPORT(XamContentFlush, XamContentFlush)
X360_EXPORT(XamContentOpenFile, XamContentOpenFile)
X360_EXPORT(XamInputGetCapabilitiesEx, XamInputGetCapabilitiesEx)
X360_EXPORT(XamInputSendStayAliveRequest, XamInputSendStayAliveRequest)
X360_EXPORT(XamInputControl, XamInputControl)
X360_EXPORT(XamInputRawState, XamInputRawState)
X360_EXPORT(XamEnableInactivityProcessing, XamEnableInactivityProcessing)
X360_EXPORT(XamResetInactivity, XamResetInactivity)
X360_EXPORT(XamIsUIActive, XamIsUIActive)
X360_EXPORT(XamShowNuiSigninUI, XamShowNuiSigninUI)
X360_EXPORT(XamShowNuiDeviceSelectorUI, XamShowNuiDeviceSelectorUI)
X360_EXPORT(XamShowNuiDirtyDiscErrorUI, XamShowNuiDirtyDiscErrorUI)
X360_EXPORT(XamShowNuiGuideUI, XamShowNuiGuideUI)
X360_EXPORT(XamShowNuiTroubleshooterUI, XamShowUIStub)
X360_EXPORT(XamShowNuiFriendsUI, XamShowUIStub)
X360_EXPORT(XamShowNuiGamerCardUIForXUID, XamShowUIStub)
X360_EXPORT(XamShowNuiMarketplaceUI, XamShowNuiMarketplaceUI)
X360_EXPORT(XamShowNuiPartyUI, XamShowUIStub)
X360_EXPORT(XamShowNuiControllerRequiredUI, XamShowUIStub)
X360_EXPORT(XamShowMarketplaceDownloadItemsUI, XamShowMarketplaceDownloadItemsUI)
X360_EXPORT(XamBackgroundDownloadSetMode, XamBackgroundDownloadSetMode)
X360_EXPORT(XamLrcSetTitlePort, XamLrcSetTitlePort)
X360_EXPORT(XamLrcVerifyClientId, XamLrcFail)
X360_EXPORT(XamLrcEncryptDecryptTitleMessage, XamLrcFail)
X360_EXPORT(XamAvatarInitialize, XamAvatarInitialize)
X360_EXPORT(XamAvatarShutdown, XamAvatarShutdown)
X360_EXPORT(XamAvatarGetManifestLocalUser, XamAvatarGetManifestLocalUser)
X360_EXPORT(XamAvatarGetManifestsByXuid, XamAvatarGetManifestsByXuid)
X360_EXPORT(XamAvatarGetAssetsResultSize, XamAvatarGetAssetsResultSize)
X360_EXPORT(XamAvatarGetAssets, XamAvatarGetAssets)
X360_EXPORT(XamAvatarGetMetadataSignedOutProfileCount, XamAvatarGetMetadataSignedOutProfileCount)
X360_EXPORT(XamAvatarGetMetadataSignedOutProfile, XamAvatarGetMetadataSignedOutProfile)
X360_EXPORT(XamAvatarGetMetadataRandom, XamAvatarGetMetadataRandom)
X360_EXPORT(XamAvatarLoadAnimation, XamAvatarLoadAnimation)
X360_EXPORT(XamAvatarManifestGetBodyType, XamAvatarManifestGetBodyType)
X360_EXPORT(XamAvatarGenerateMipMaps, XamAvatarGenerateMipMaps)
X360_EXPORT(XamAvatarSetCustomAsset, XamAvatarSetCustomAsset)
X360_EXPORT(XamNuiGetDeviceStatus, XamNuiGetDeviceStatus)
X360_EXPORT(XamVoiceGetMicArrayStatus, XamVoiceGetMicArrayStatus)
