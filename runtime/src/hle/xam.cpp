// xam.xex exports: users and profiles, content (saves), enumerators, notifications, input (see
// x360/input.h), loader, tasks and system UI. One local profile ("Player") is always signed in.
#include <chrono>
#include <deque>
#include <fstream>
#include <random>

#include "x360/input.h"
#include "x360/kernel.h"

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

constexpr uint64_t kXuid = 0xE000000012345678ull;  // offline XUID
constexpr const char* kGamertag = "Player";

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

uint32_t XamUserGetSigninState(uint32_t user_index) { return user_index == 0 ? 1 : 0; }  // 1 = signed in locally

uint32_t XamUserGetSigninInfo(uint32_t user_index, uint32_t flags, uint32_t info) {
  (void)flags;
  std::memset(Ptr(info), 0, 0x28);
  if (user_index != 0) return X_ERROR_NO_SUCH_USER;
  Store<uint64_t>(info + 0x00, kXuid);
  Store<uint32_t>(info + 0x0C, 1);  // signin state
  std::strcpy(Ptr<char>(info + 0x18), kGamertag);
  return X_ERROR_SUCCESS;
}

uint32_t XamUserGetXUID(uint32_t user_index, uint32_t type_mask, uint32_t xuid_ptr) {
  (void)type_mask;
  if (user_index != 0) return X_ERROR_NO_SUCH_USER;
  if (xuid_ptr) Store<uint64_t>(xuid_ptr, kXuid);
  return X_ERROR_SUCCESS;
}

uint32_t XamUserGetName(uint32_t user_index, uint32_t buffer, uint32_t size) {
  if (user_index != 0) return X_ERROR_NO_SUCH_USER;
  if (size) {
    std::string name = kGamertag;
    name.resize(std::min<size_t>(name.size(), size - 1));
    std::memcpy(Ptr(buffer), name.c_str(), name.size() + 1);
  }
  return X_ERROR_SUCCESS;
}

uint32_t XamUserCheckPrivilege(uint32_t user_index, uint32_t privilege, uint32_t result_ptr) {
  (void)privilege;
  if (user_index != 0 && user_index != 0xFF) return X_ERROR_NO_SUCH_USER;
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
  return user_index == 0 ? X_ERROR_SUCCESS : X_ERROR_NO_SUCH_USER;
}

// ---- profile settings -------------------------------------------------------------------------
// Setting id: type << 28 | size << 16 | id. Title-specific blobs are persisted on disk.

std::mutex g_profile_mutex;
std::map<uint32_t, std::vector<uint8_t>> g_profile_blobs;

fs::path ProfileDir() { return TitlePaths().user_root / "profile"; }

std::vector<uint8_t>* ProfileBlob(uint32_t id) {
  auto it = g_profile_blobs.find(id);
  if (it != g_profile_blobs.end()) return &it->second;
  char name[32];
  std::snprintf(name, sizeof(name), "%08X.bin", id);
  std::ifstream in(ProfileDir() / name, std::ios::binary);
  if (!in) return nullptr;
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
  return &(g_profile_blobs[id] = std::move(data));
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
    Store<uint32_t>(buffer_size_ptr, needed);
    return X_ERROR_INSUFFICIENT_BUFFER;
  }
  if (user_index != 0 && !xuids) return X_ERROR_NO_SUCH_USER;

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
      Store<uint64_t>(s + 0x04, kXuid);
    } else {
      Store<uint32_t>(s + 0x04, 0xFFFFFFFF);
      Store<uint32_t>(s + 0x08, user_index);
    }
    Store<uint32_t>(s + 0x0C, id);
    Store<uint8_t>(s + 0x10, uint8_t(type));
    if (type == 4 || type == 6) {
      std::vector<uint8_t>* blob = ProfileBlob(id);
      uint32_t length = blob ? std::min<uint32_t>(uint32_t(blob->size()), size) : 0;
      if (blob) {
        std::memcpy(Ptr(extra), blob->data(), length);
        Store<uint32_t>(s + 0x00, 2);  // source: title
      } else if ((id & 0xFFFF) >= 0x3FFD) {
        Store<uint32_t>(s + 0x00, 0);  // title-specific blob never written: no value
      }
      Store<uint32_t>(s + 0x18, length);
      Store<uint32_t>(s + 0x1C, extra);
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
  (void)user_index;
  std::lock_guard lock(g_profile_mutex);
  std::error_code ec;
  fs::create_directories(ProfileDir(), ec);
  for (uint32_t i = 0; i < setting_count; ++i) {
    uint32_t s = settings + i * 40;
    uint32_t id = Load<uint32_t>(s + 0x0C);
    uint32_t type = Load<uint8_t>(s + 0x10);
    if (type != 4 && type != 6) continue;
    uint32_t length = Load<uint32_t>(s + 0x18), data = Load<uint32_t>(s + 0x1C);
    std::vector<uint8_t> blob(Ptr(data), Ptr(data) + length);
    char name[32];
    std::snprintf(name, sizeof(name), "%08X.bin", id);
    std::ofstream(ProfileDir() / name, std::ios::binary).write(reinterpret_cast<const char*>(blob.data()), length);
    g_profile_blobs[id] = std::move(blob);
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

fs::path ContentPath(uint32_t content_data) {
  uint32_t type = Load<uint32_t>(content_data + 4);
  std::string file(Ptr<const char>(content_data + 0x108), strnlen(Ptr<const char>(content_data + 0x108), 42));
  char type_dir[16];
  std::snprintf(type_dir, sizeof(type_dir), "%08X", type);
  return TitlePaths().user_root / "content" / type_dir / file;
}

uint32_t XamContentCreateEx(uint32_t user_index, uint32_t root_name_ptr, uint32_t content_data, uint32_t flags,
                            uint32_t disposition_ptr, uint32_t license_mask_ptr, uint32_t cache_size,
                            uint64_t content_size, uint32_t overlapped) {
  (void)user_index, (void)cache_size, (void)content_size;
  std::string root = Ptr<const char>(root_name_ptr);
  fs::path path = ContentPath(content_data);
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
  KLOG_I("XamContentCreateEx %s: -> %s (%u)", root.c_str(), path.string().c_str(), result);
  if (disposition_ptr) Store<uint32_t>(disposition_ptr, disposition);
  if (license_mask_ptr) Store<uint32_t>(license_mask_ptr, 0xFFFFFFFF);
  return overlapped ? Complete(overlapped, result) : result;
}

uint32_t XamContentClose(uint32_t root_name_ptr, uint32_t overlapped) {
  UnmountContent(Ptr<const char>(root_name_ptr));
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

uint32_t XamContentDelete(uint32_t user_index, uint32_t content_data, uint32_t overlapped) {
  (void)user_index;
  std::error_code ec;
  fs::remove_all(ContentPath(content_data), ec);
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

uint32_t XamContentCreateEnumerator(uint32_t user_index, uint32_t device_id, uint32_t content_type,
                                    uint32_t content_flags, uint32_t items_per, uint32_t buffer_size_ptr,
                                    uint32_t handle_ptr) {
  (void)user_index, (void)content_flags;
  if (buffer_size_ptr) Store<uint32_t>(buffer_size_ptr, kContentDataSize * std::max<uint32_t>(items_per, 1));
  std::vector<std::vector<uint8_t>> items;
  if (device_id == 0 || device_id == kHddDeviceId) {
    char type_dir[16];
    std::snprintf(type_dir, sizeof(type_dir), "%08X", content_type);
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(TitlePaths().user_root / "content" / type_dir, ec)) {
      std::vector<uint8_t> item(kContentDataSize, 0);
      x360::store_be<uint32_t>(item.data() + 0, kHddDeviceId);
      x360::store_be<uint32_t>(item.data() + 4, content_type);
      std::string name = entry.path().filename().string().substr(0, 41);
      for (size_t i = 0; i < name.size(); ++i) x360::store_be<uint16_t>(item.data() + 8 + i * 2, uint8_t(name[i]));
      std::memcpy(item.data() + 0x108, name.data(), name.size());
      items.push_back(std::move(item));
    }
  }
  return CreateEnumerator(handle_ptr, items_per, std::move(items));
}

uint32_t XamContentGetCreator(uint32_t user_index, uint32_t content_data, uint32_t is_creator_ptr, uint32_t xuid_ptr,
                              uint32_t overlapped) {
  (void)user_index, (void)content_data;
  if (is_creator_ptr) Store<uint32_t>(is_creator_ptr, 1);
  if (xuid_ptr) Store<uint64_t>(xuid_ptr, kXuid);
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
}

uint32_t XamContentGetDeviceData(uint32_t device_id, uint32_t data) {
  if (device_id != kHddDeviceId) return X_ERROR_DEVICE_NOT_CONNECTED;
  std::memset(Ptr(data), 0, 0x50);
  Store<uint32_t>(data + 0x00, kHddDeviceId);
  Store<uint32_t>(data + 0x04, 1);  // device type: HDD
  Store<uint64_t>(data + 0x08, 20ull << 30);
  Store<uint64_t>(data + 0x10, 16ull << 30);
  const char* name = "Hard Drive";
  for (size_t i = 0; name[i]; ++i) Store<uint16_t>(data + 0x18 + uint32_t(i) * 2, uint8_t(name[i]));
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

struct Listener : Object {
  const char* TypeName() const override { return "NotifyListener"; }
  uint64_t mask = 0;
  std::deque<std::pair<uint32_t, uint32_t>> queue;
};
std::mutex g_listener_mutex;
std::vector<std::weak_ptr<Listener>> g_listeners;

uint32_t XamNotifyCreateListener(uint64_t mask, uint32_t unknown) {
  (void)unknown;
  auto listener = std::make_shared<Listener>();
  listener->mask = mask;
  // A real console reports the signed-in user and storage right after a listener is created.
  if (mask & 1) {
    listener->queue.push_back({0x0000000A, 1});  // XN_SYS_SIGNINCHANGED: user 0
    listener->queue.push_back({0x0000000B, 0});  // XN_SYS_STORAGEDEVICESCHANGED
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

// ---- loader / process -------------------------------------------------------------------------

uint32_t XamLoaderGetLaunchDataSize(uint32_t size_ptr) {
  if (size_ptr) Store<uint32_t>(size_ptr, 0);
  return X_ERROR_NOT_FOUND;
}
uint32_t XamLoaderGetLaunchData(uint32_t buffer, uint32_t size) {
  (void)buffer, (void)size;
  return X_ERROR_NOT_FOUND;
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

uint32_t XamShowSigninUI(uint32_t count, uint32_t flags) {
  (void)count, (void)flags;
  return X_ERROR_SUCCESS;
}

uint32_t XamShowMessageBoxUIEx(uint32_t user_index, uint32_t title, uint32_t text, uint32_t button_count,
                               uint32_t buttons, uint32_t focus, uint32_t flags, uint32_t result_ptr,
                               uint32_t overlapped) {
  (void)user_index, (void)button_count, (void)buttons, (void)flags;
  auto narrow = [](uint32_t w) {
    std::string s;
    for (uint32_t a = w; a && Load<uint16_t>(a); a += 2) s += char(Load<uint16_t>(a) & 0x7F);
    return s;
  };
  KLOG_I("XamShowMessageBoxUIEx: [%s] %s", narrow(title).c_str(), narrow(text).c_str());
  if (result_ptr) Store<uint32_t>(result_ptr, focus);
  return overlapped ? Complete(overlapped, X_ERROR_SUCCESS) : X_ERROR_SUCCESS;
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
uint32_t XamWriteGamerTile() { return X_ERROR_NOT_FOUND; }

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
        case 0x000B0014:  // XSessionStart
        case 0x000B0015:  // XSessionEnd
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
uint32_t XMsgCompleteIORequest() { return X_ERROR_SUCCESS; }
uint32_t XMsgCancelIORequest(uint32_t overlapped, uint32_t wait) {
  (void)wait;
  if (overlapped) Complete(overlapped, X_ERROR_CANCELLED);
  return X_ERROR_SUCCESS;
}

uint32_t XNetLogonGetTitleID() { return Title().title_id; }

uint32_t XGetAVPack() { return 6; }  // HDMI
uint32_t XGetGameRegion() { return 0xFFFF; }
uint32_t XGetLanguage() { return 1; }  // English

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
void InitXam() {}
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
X360_EXPORT(XamContentGetDeviceState, XamContentGetDeviceState)
X360_EXPORT(XamContentGetLicenseMask, XamContentGetLicenseMask)
X360_EXPORT(XamShowDeviceSelectorUI, XamShowDeviceSelectorUI)
X360_EXPORT(XamNotifyCreateListener, XamNotifyCreateListener)
X360_EXPORT(XNotifyGetNext, XNotifyGetNext)
X360_EXPORT(XNotifyPositionUI, XNotifyPositionUI)
X360_EXPORT(XamInputGetCapabilities, XamInputGetCapabilities)
X360_EXPORT(XamInputGetState, XamInputGetState)
X360_EXPORT(XamInputSetState, XamInputSetState)
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
X360_EXPORT(XamShowMarketplaceUI, XamShowUIStub)
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
