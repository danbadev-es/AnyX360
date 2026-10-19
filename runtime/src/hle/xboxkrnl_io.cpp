// xboxkrnl file I/O over a small virtual file system.
//
// Guest paths name a device ("\Device\Cdrom0\GearGame\...") or go through symbolic links
// ("game:\...", "\??\d:\..."). Devices map to host folders: the game folder (read-only) and a
// writable user folder for the hard disk, the cache partition and saved content.
#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "x360/kernel.h"
#include "xenia/memory.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace x360::kernel;
namespace fs = std::filesystem;

namespace {

// ---- path resolution --------------------------------------------------------------------------

struct Device {
  std::string name;  // lower case, e.g. "\device\cdrom0"
  fs::path host;
  bool read_only;
};

std::mutex g_vfs_mutex;
std::vector<Device> g_devices;
std::unordered_map<std::string, std::string> g_links;  // lower-case link -> target device path

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

std::string ReadAnsi(uint32_t ansi_string) {
  if (!ansi_string) return {};
  uint16_t length = Load<uint16_t>(ansi_string);
  uint32_t buffer = Load<uint32_t>(ansi_string + 4);
  if (!buffer || !length) return {};
  return std::string(Ptr<char>(buffer), length);
}

// Host folders in Windows' long form (\\?\C:\...): a game's files deep in a folder tree (a release
// installed deep, downloadable content in the user folder) aren't limited to 260 characters.
fs::path LongPath(const fs::path& path) {
  std::error_code ec;
  std::wstring text = fs::absolute(path, ec).lexically_normal().make_preferred().wstring();
  if (text.rfind(L"\\\\", 0) == 0) return text;  // already \\?\ or a share
  return L"\\\\?\\" + text;
}

void AddDevice(const std::string& name, const fs::path& host, bool read_only) {
  std::error_code ec;
  if (!read_only) fs::create_directories(host, ec);
  g_devices.push_back({Lower(name), LongPath(host), read_only});
}

void AddLink(const std::string& link, const std::string& target) { g_links[Lower(link)] = target; }

struct Resolved {
  fs::path host;
  bool read_only = true;
  bool ok = false;
};

// Resolves a guest path (already ANSI) to a host path.
Resolved Resolve(std::string path) {
  std::replace(path.begin(), path.end(), '/', '\\');
  std::string lower = Lower(path);
  if (lower.rfind("\\??\\", 0) == 0) {
    path = path.substr(4);
    lower = lower.substr(4);
  }

  std::lock_guard lock(g_vfs_mutex);
  // Symbolic links: "game:" style prefixes, longest first.
  for (int depth = 0; depth < 4; ++depth) {
    bool replaced = false;
    for (const auto& [link, target] : g_links) {
      if (lower.rfind(link, 0) == 0 && (lower.size() == link.size() || lower[link.size()] == '\\')) {
        path = target + path.substr(link.size());
        lower = Lower(path);
        replaced = true;
        break;
      }
    }
    if (!replaced) break;
  }

  for (const Device& d : g_devices) {
    if (lower.rfind(d.name, 0) == 0 && (lower.size() == d.name.size() || lower[d.name.size()] == '\\')) {
      std::string rest = path.substr(d.name.size());
      while (!rest.empty() && rest.front() == '\\') rest.erase(rest.begin());
      fs::path relative = fs::path(rest).lexically_normal();
      if (!relative.empty() && relative.begin()->string() == "..") return {};
      Resolved r;
      r.host = rest.empty() ? d.host : d.host / relative;
      r.read_only = d.read_only;
      r.ok = true;
      return r;
    }
  }
  return {};
}

// ---- file objects -----------------------------------------------------------------------------

struct File : Object {
  const char* TypeName() const override { return "File"; }
  ~File() override {
    if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
  }
  HANDLE handle = INVALID_HANDLE_VALUE;
  fs::path host_path;
  std::string guest_path;
  bool directory = false;
  uint64_t position = 0;
  // Directory enumeration state.
  std::vector<WIN32_FIND_DATAW> entries;
  size_t next_entry = 0;
  bool enumerated = false;
};

uint64_t FileTimeValue(const FILETIME& ft) { return (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; }

uint32_t GuestAttributes(DWORD attributes) {
  uint32_t a = attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
                             FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE);
  return a ? a : FILE_ATTRIBUTE_NORMAL;
}

void SetIoStatus(uint32_t io_status, uint32_t status, uint32_t information) {
  if (!io_status) return;
  Store<uint32_t>(io_status + 0, status);
  Store<uint32_t>(io_status + 4, information);
}

void SignalEvent(uint32_t event_handle) {
  if (!event_handle) return;
  if (uint32_t body = ObjectFromHandle(event_handle)) SetEvent(body);
}

bool WriteNetworkOpenInfo(const fs::path& path, uint32_t info) {
  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
  uint64_t size = (uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
  Store<uint64_t>(info + 0x00, FileTimeValue(data.ftCreationTime));
  Store<uint64_t>(info + 0x08, FileTimeValue(data.ftLastAccessTime));
  Store<uint64_t>(info + 0x10, FileTimeValue(data.ftLastWriteTime));
  Store<uint64_t>(info + 0x18, FileTimeValue(data.ftLastWriteTime));
  Store<uint64_t>(info + 0x20, (size + 0xFFF) & ~uint64_t(0xFFF));
  Store<uint64_t>(info + 0x28, size);
  Store<uint32_t>(info + 0x30, GuestAttributes(data.dwFileAttributes));
  Store<uint32_t>(info + 0x34, 0);
  return true;
}

constexpr uint32_t kFileDirectoryFile = 0x1;
constexpr uint32_t kFileNonDirectoryFile = 0x40;

uint32_t OpenFile(uint32_t handle_ptr, uint32_t desired_access, uint32_t object_attributes, uint32_t io_status,
                  uint32_t disposition, uint32_t create_options) {
  if (!object_attributes) return X_STATUS_INVALID_PARAMETER;
  std::string guest = ReadAnsi(Load<uint32_t>(object_attributes + 4));
  uint32_t root = Load<uint32_t>(object_attributes + 0);
  if (root && root != 0xFFFFFFFD) {
    if (auto parent = LookupHandleAs<File>(root)) guest = parent->guest_path + "\\" + guest;
  }

  Resolved r = Resolve(guest);
  static const bool log_files = std::getenv("ANYX360_LOG_FILES") != nullptr;
  if (log_files) KLOG_I("open %s (access %08X, disposition %u, options %08X)", guest.c_str(), desired_access, disposition, create_options);
  if (!r.ok) {
    KLOG_W("NtCreateFile: unresolved path '%s'", guest.c_str());
    SetIoStatus(io_status, X_STATUS_OBJECT_PATH_NOT_FOUND, 0);
    return X_STATUS_OBJECT_PATH_NOT_FOUND;
  }

  const bool wants_write = (desired_access & (0x40000000 | 0x2 | 0x4 | 0x10000 /* DELETE */)) != 0 ||
                           disposition == 0 || disposition == 2 || disposition == 4 || disposition == 5;
  std::error_code ec;
  const bool exists = fs::exists(r.host, ec);
  const bool is_dir = exists && fs::is_directory(r.host, ec);

  // Directories.
  if ((create_options & kFileDirectoryFile) || is_dir) {
    if (!exists) {
      if (disposition == 1 || r.read_only) {
        SetIoStatus(io_status, X_STATUS_OBJECT_NAME_NOT_FOUND, 0);
        return X_STATUS_OBJECT_NAME_NOT_FOUND;
      }
      fs::create_directories(r.host, ec);
    } else if (!is_dir) {
      return X_STATUS_NOT_A_DIRECTORY;
    } else if (create_options & kFileNonDirectoryFile) {
      return X_STATUS_FILE_IS_A_DIRECTORY;
    }
    auto file = std::make_shared<File>();
    file->directory = true;
    file->host_path = r.host;
    file->guest_path = guest;
    Store<uint32_t>(handle_ptr, CreateHandle(file));
    SetIoStatus(io_status, X_STATUS_SUCCESS, exists ? 1 : 2);
    return X_STATUS_SUCCESS;
  }

  if (r.read_only && wants_write && disposition != 1 && disposition != 3) {
    SetIoStatus(io_status, X_STATUS_ACCESS_DENIED, 0);
    return X_STATUS_ACCESS_DENIED;
  }

  DWORD create;
  switch (disposition) {
    case 0: create = CREATE_ALWAYS; break;      // supersede
    case 1: create = OPEN_EXISTING; break;      // open
    case 2: create = CREATE_NEW; break;         // create
    case 3: create = OPEN_ALWAYS; break;        // open if
    case 4: create = TRUNCATE_EXISTING; break;  // overwrite
    case 5: create = CREATE_ALWAYS; break;      // overwrite if
    default: return X_STATUS_INVALID_PARAMETER;
  }
  if (r.read_only) create = OPEN_EXISTING;
  DWORD access = (!r.read_only && wants_write) ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
  if (create != OPEN_EXISTING) fs::create_directories(r.host.parent_path(), ec);

  HANDLE h = CreateFileW(r.host.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         create, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    uint32_t status = (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? X_STATUS_OBJECT_NAME_NOT_FOUND
                      : error == ERROR_FILE_EXISTS                                     ? X_STATUS_OBJECT_NAME_COLLISION
                                                                                       : X_STATUS_ACCESS_DENIED;
    KLOG_T("NtCreateFile '%s' -> %08X", guest.c_str(), status);
    SetIoStatus(io_status, status, 0);
    return status;
  }

  auto file = std::make_shared<File>();
  file->handle = h;
  file->host_path = r.host;
  file->guest_path = guest;
  Store<uint32_t>(handle_ptr, CreateHandle(file));
  uint32_t action = exists ? (disposition == 4 || disposition == 5 || disposition == 0 ? 3 : 1) : 2;
  SetIoStatus(io_status, X_STATUS_SUCCESS, action);
  KLOG_T("NtCreateFile '%s' -> %s", guest.c_str(), r.host.string().c_str());
  return X_STATUS_SUCCESS;
}

uint32_t NtCreateFile(uint32_t handle_ptr, uint32_t desired_access, uint32_t object_attributes, uint32_t io_status,
                      uint32_t allocation_size_ptr, uint32_t file_attributes, uint32_t share_access,
                      uint32_t disposition, uint32_t create_options) {
  (void)allocation_size_ptr;
  (void)file_attributes;
  (void)share_access;
  return OpenFile(handle_ptr, desired_access, object_attributes, io_status, disposition, create_options);
}

uint32_t NtOpenFile(uint32_t handle_ptr, uint32_t desired_access, uint32_t object_attributes, uint32_t io_status,
                    uint32_t share_access, uint32_t open_options) {
  (void)share_access;
  return OpenFile(handle_ptr, desired_access, object_attributes, io_status, 1, open_options);
}

// Reads `length` bytes at `offset` (or the current position) into guest memory.
uint32_t ReadAt(File& file, uint64_t* offset, uint32_t buffer, uint32_t length, uint32_t* read) {
  uint64_t at = offset ? *offset : file.position;
  OVERLAPPED ov{};
  ov.Offset = DWORD(at);
  ov.OffsetHigh = DWORD(at >> 32);
  DWORD got = 0;
  // The GPU write-protects physical pages it caches (textures, buffers). The host kernel does not
  // raise exceptions for those, ReadFile just fails, so read physical buffers through the
  // unprotected physical view and then tell the GPU the range changed (as Xenia's XFile does).
  xe::BaseHeap* heap = XeMemory()->LookupHeap(buffer);
  auto* physical = heap && heap->heap_type() == xe::HeapType::kGuestPhysical
                       ? static_cast<xe::PhysicalHeap*>(heap)
                       : nullptr;
  void* target = physical ? XeMemory()->TranslatePhysical(physical->GetPhysicalAddress(buffer))
                          : static_cast<void*>(Ptr(buffer));
  if (!ReadFile(file.handle, target, length, &got, &ov)) {
    DWORD error = GetLastError();
    if (error != ERROR_HANDLE_EOF) {
      KLOG_W("ReadFile(%s, %08X, %u) failed: %lu", file.guest_path.c_str(), buffer, length, error);
      return X_STATUS_UNSUCCESSFUL;
    }
  }
  if (physical && got) {
    physical->TriggerCallbacks(xe::global_critical_region::AcquireDirect(), buffer, got, true, true);
  }
  file.position = at + got;
  *read = got;
  return (got == 0 && length > 0) ? X_STATUS_END_OF_FILE : X_STATUS_SUCCESS;
}

uint64_t* OffsetArg(uint32_t byte_offset_ptr, uint64_t& storage) {
  if (!byte_offset_ptr) return nullptr;
  storage = Load<uint64_t>(byte_offset_ptr);
  if (storage == 0xFFFFFFFFFFFFFFFEull) return nullptr;  // FILE_USE_FILE_POINTER_POSITION
  return &storage;
}

uint32_t NtReadFile(uint32_t handle, uint32_t event, uint32_t apc_routine, uint32_t apc_context, uint32_t io_status,
                    uint32_t buffer, uint32_t length, uint32_t byte_offset_ptr) {
  (void)apc_context;
  auto file = LookupHandleAs<File>(handle);
  if (!file || file->directory) return X_STATUS_INVALID_HANDLE;
  if (apc_routine) KLOG_W("NtReadFile: APC completion routines are not delivered");
  uint64_t offset;
  uint32_t read = 0;
  uint32_t status = ReadAt(*file, OffsetArg(byte_offset_ptr, offset), buffer, length, &read);
  SetIoStatus(io_status, status, read);
  SignalEvent(event);
  return status;
}

uint32_t NtReadFileScatter(uint32_t handle, uint32_t event, uint32_t apc_routine, uint32_t apc_context,
                           uint32_t io_status, uint32_t segments, uint32_t length, uint32_t byte_offset_ptr) {
  (void)apc_routine;
  (void)apc_context;
  auto file = LookupHandleAs<File>(handle);
  if (!file) return X_STATUS_INVALID_HANDLE;
  uint64_t offset_storage;
  uint64_t* offset = OffsetArg(byte_offset_ptr, offset_storage);
  uint64_t at = offset ? *offset : file->position;
  uint32_t total = 0, status = X_STATUS_SUCCESS;
  for (uint32_t i = 0; total < length; ++i) {
    uint32_t page = uint32_t(Load<uint64_t>(segments + i * 8));
    uint32_t chunk = std::min<uint32_t>(0x1000, length - total);
    uint64_t chunk_offset = at + total;
    uint32_t read = 0;
    status = ReadAt(*file, &chunk_offset, page, chunk, &read);
    total += read;
    if (status != X_STATUS_SUCCESS || read < chunk) break;
  }
  if (total) status = X_STATUS_SUCCESS;
  SetIoStatus(io_status, status, total);
  SignalEvent(event);
  return status;
}

uint32_t WriteAt(File& file, uint64_t* offset, uint32_t buffer, uint32_t length, uint32_t* written) {
  uint64_t at = offset ? *offset : file.position;
  OVERLAPPED ov{};
  ov.Offset = DWORD(at);
  ov.OffsetHigh = DWORD(at >> 32);
  DWORD put = 0;
  if (!WriteFile(file.handle, Ptr(buffer), length, &put, &ov)) return X_STATUS_ACCESS_DENIED;
  file.position = at + put;
  *written = put;
  return X_STATUS_SUCCESS;
}

uint32_t NtWriteFile(uint32_t handle, uint32_t event, uint32_t apc_routine, uint32_t apc_context, uint32_t io_status,
                     uint32_t buffer, uint32_t length, uint32_t byte_offset_ptr) {
  (void)apc_routine;
  (void)apc_context;
  auto file = LookupHandleAs<File>(handle);
  if (!file || file->directory) return X_STATUS_INVALID_HANDLE;
  uint64_t offset;
  uint32_t written = 0;
  uint32_t status = WriteAt(*file, OffsetArg(byte_offset_ptr, offset), buffer, length, &written);
  static const bool log_files = std::getenv("ANYX360_LOG_FILES") != nullptr;
  if (log_files) KLOG_I("write %s: %u bytes -> %08X", file->guest_path.c_str(), length, status);
  SetIoStatus(io_status, status, written);
  SignalEvent(event);
  return status;
}

uint32_t NtWriteFileGather(uint32_t handle, uint32_t event, uint32_t apc_routine, uint32_t apc_context,
                           uint32_t io_status, uint32_t segments, uint32_t length, uint32_t byte_offset_ptr) {
  (void)apc_routine;
  (void)apc_context;
  auto file = LookupHandleAs<File>(handle);
  if (!file) return X_STATUS_INVALID_HANDLE;
  uint64_t offset_storage;
  uint64_t* offset = OffsetArg(byte_offset_ptr, offset_storage);
  uint64_t at = offset ? *offset : file->position;
  uint32_t total = 0, status = X_STATUS_SUCCESS;
  for (uint32_t i = 0; total < length; ++i) {
    uint32_t page = uint32_t(Load<uint64_t>(segments + i * 8));
    uint32_t chunk = std::min<uint32_t>(0x1000, length - total);
    uint64_t chunk_offset = at + total;
    uint32_t written = 0;
    status = WriteAt(*file, &chunk_offset, page, chunk, &written);
    total += written;
    if (status != X_STATUS_SUCCESS) break;
  }
  SetIoStatus(io_status, status, total);
  SignalEvent(event);
  return status;
}

uint64_t FileSize(File& file) {
  if (file.handle == INVALID_HANDLE_VALUE) return 0;
  LARGE_INTEGER size{};
  GetFileSizeEx(file.handle, &size);
  return uint64_t(size.QuadPart);
}

uint32_t NtQueryInformationFile(uint32_t handle, uint32_t io_status, uint32_t info, uint32_t length,
                                uint32_t info_class) {
  auto file = LookupHandleAs<File>(handle);
  if (!file) return X_STATUS_INVALID_HANDLE;
  uint32_t written = 0;
  switch (info_class) {
    case 4: {  // FileBasicInformation
      WIN32_FILE_ATTRIBUTE_DATA data{};
      GetFileAttributesExW(file->host_path.c_str(), GetFileExInfoStandard, &data);
      Store<uint64_t>(info + 0x00, FileTimeValue(data.ftCreationTime));
      Store<uint64_t>(info + 0x08, FileTimeValue(data.ftLastAccessTime));
      Store<uint64_t>(info + 0x10, FileTimeValue(data.ftLastWriteTime));
      Store<uint64_t>(info + 0x18, FileTimeValue(data.ftLastWriteTime));
      Store<uint64_t>(info + 0x20, GuestAttributes(data.dwFileAttributes));
      written = 0x28;
      break;
    }
    case 5: {  // FileStandardInformation
      uint64_t size = FileSize(*file);
      Store<uint64_t>(info + 0x00, (size + 0xFFF) & ~uint64_t(0xFFF));
      Store<uint64_t>(info + 0x08, size);
      Store<uint32_t>(info + 0x10, 1);
      Store<uint8_t>(info + 0x14, 0);
      Store<uint8_t>(info + 0x15, file->directory ? 1 : 0);
      written = 0x18;
      break;
    }
    case 6:  // FileInternalInformation
      Store<uint64_t>(info, std::hash<std::string>{}(file->host_path.string()));
      written = 8;
      break;
    case 14:  // FilePositionInformation
      Store<uint64_t>(info, file->position);
      written = 8;
      break;
    case 20:  // FileEndOfFileInformation
      Store<uint64_t>(info, FileSize(*file));
      written = 8;
      break;
    case 26:  // FileSectorInformation
      Store<uint32_t>(info, 0x800);
      written = 4;
      break;
    case 27:  // FileXctdCompressionInformation
      Store<uint32_t>(info, 0);
      written = 4;
      break;
    case 34:  // FileNetworkOpenInformation
      if (!WriteNetworkOpenInfo(file->host_path, info)) return X_STATUS_NO_SUCH_FILE;
      written = 56;
      break;
    default:
      KLOG_W("NtQueryInformationFile: unsupported class %u", info_class);
      SetIoStatus(io_status, X_STATUS_INVALID_INFO_CLASS, 0);
      return X_STATUS_INVALID_INFO_CLASS;
  }
  (void)length;
  SetIoStatus(io_status, X_STATUS_SUCCESS, written);
  return X_STATUS_SUCCESS;
}

uint32_t NtSetInformationFile(uint32_t handle, uint32_t io_status, uint32_t info, uint32_t length,
                              uint32_t info_class) {
  (void)length;
  auto file = LookupHandleAs<File>(handle);
  if (!file) return X_STATUS_INVALID_HANDLE;
  static const bool log_files = std::getenv("ANYX360_LOG_FILES") != nullptr;
  if (log_files) KLOG_I("set info %s: class %u", file->guest_path.c_str(), info_class);
  switch (info_class) {
    case 14:  // FilePositionInformation
      file->position = Load<uint64_t>(info);
      break;
    case 19:  // FileAllocationInformation
    case 20: {  // FileEndOfFileInformation
      LARGE_INTEGER size;
      size.QuadPart = LONGLONG(Load<uint64_t>(info));
      if (file->handle != INVALID_HANDLE_VALUE && info_class == 20) {
        SetFilePointerEx(file->handle, size, nullptr, FILE_BEGIN);
        SetEndOfFile(file->handle);
      }
      break;
    }
    case 13:  // FileDispositionInformation
      if (Load<uint8_t>(info)) {
        std::error_code ec;
        if (file->handle != INVALID_HANDLE_VALUE) {
          ::CloseHandle(file->handle);
          file->handle = INVALID_HANDLE_VALUE;
        }
        fs::remove(file->host_path, ec);
      }
      break;
    case 4:  // FileBasicInformation (timestamps/attributes): ignored
      break;
    default:
      KLOG_W("NtSetInformationFile: unsupported class %u", info_class);
      return X_STATUS_INVALID_INFO_CLASS;
  }
  SetIoStatus(io_status, X_STATUS_SUCCESS, 0);
  return X_STATUS_SUCCESS;
}

uint32_t NtQueryFullAttributesFile(uint32_t object_attributes, uint32_t info) {
  std::string guest = ReadAnsi(Load<uint32_t>(object_attributes + 4));
  uint32_t root = Load<uint32_t>(object_attributes + 0);
  if (root && root != 0xFFFFFFFD) {
    if (auto parent = LookupHandleAs<File>(root)) guest = parent->guest_path + "\\" + guest;
  }
  Resolved r = Resolve(guest);
  if (!r.ok || !WriteNetworkOpenInfo(r.host, info)) {
    KLOG_T("NtQueryFullAttributesFile '%s': not found", guest.c_str());
    return X_STATUS_OBJECT_NAME_NOT_FOUND;
  }
  return X_STATUS_SUCCESS;
}

uint32_t NtQueryVolumeInformationFile(uint32_t handle, uint32_t io_status, uint32_t info, uint32_t length,
                                      uint32_t info_class) {
  (void)length;
  auto file = LookupHandleAs<File>(handle);
  if (!file) return X_STATUS_INVALID_HANDLE;
  uint32_t written = 0;
  switch (info_class) {
    case 1:  // FileFsVolumeInformation
      Store<uint64_t>(info + 0x00, SystemTime());
      Store<uint32_t>(info + 0x08, 0x12345678);
      Store<uint32_t>(info + 0x0C, 0);
      Store<uint8_t>(info + 0x10, 0);
      written = 0x18;
      break;
    case 3:  // FileFsSizeInformation
      Store<uint64_t>(info + 0x00, 0x100000);  // allocation units
      Store<uint64_t>(info + 0x08, 0x80000);
      Store<uint32_t>(info + 0x10, 32);       // sectors per unit
      Store<uint32_t>(info + 0x14, 0x200);    // bytes per sector
      written = 0x18;
      break;
    case 5:  // FileFsAttributeInformation
      Store<uint32_t>(info + 0x00, 0);
      Store<int32_t>(info + 0x04, 255);
      Store<uint32_t>(info + 0x08, 4);
      std::memcpy(Ptr(info + 0x0C), "FATX", 4);
      written = 0x10;
      break;
    default:
      KLOG_W("NtQueryVolumeInformationFile: unsupported class %u", info_class);
      return X_STATUS_INVALID_INFO_CLASS;
  }
  SetIoStatus(io_status, X_STATUS_SUCCESS, written);
  return X_STATUS_SUCCESS;
}

bool MatchPattern(const std::string& pattern, const std::string& name) {
  // Case-insensitive '*' / '?' matching.
  size_t p = 0, n = 0, star = std::string::npos, mark = 0;
  while (n < name.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || std::tolower(pattern[p]) == std::tolower(name[n]))) {
      ++p;
      ++n;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      mark = n;
    } else if (star != std::string::npos) {
      p = star + 1;
      n = ++mark;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}

uint32_t NtQueryDirectoryFile(uint32_t handle, uint32_t event, uint32_t apc_routine, uint32_t apc_context,
                              uint32_t io_status, uint32_t info, uint32_t length, uint32_t file_name,
                              uint32_t restart_scan) {
  (void)apc_routine;
  (void)apc_context;
  auto dir = LookupHandleAs<File>(handle);
  if (!dir || !dir->directory) return X_STATUS_INVALID_HANDLE;

  if (!dir->enumerated || restart_scan) {
    std::string pattern = file_name ? ReadAnsi(file_name) : "*";
    if (pattern.empty()) pattern = "*";
    dir->entries.clear();
    dir->next_entry = 0;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((dir->host_path / L"*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
      do {
        std::wstring wname = data.cFileName;
        if (wname == L"." || wname == L"..") continue;
        std::string name = fs::path(wname).string();
        if (MatchPattern(pattern, name)) dir->entries.push_back(data);
      } while (FindNextFileW(find, &data));
      FindClose(find);
    }
    dir->enumerated = true;
  }

  if (dir->next_entry >= dir->entries.size()) {
    SetIoStatus(io_status, X_STATUS_NO_MORE_FILES, 0);
    SignalEvent(event);
    return X_STATUS_NO_MORE_FILES;
  }

  const WIN32_FIND_DATAW& e = dir->entries[dir->next_entry];
  std::string name = fs::path(e.cFileName).string();
  uint32_t needed = 0x40 + uint32_t(name.size());
  if (needed > length) return X_STATUS_BUFFER_OVERFLOW;
  ++dir->next_entry;

  uint64_t size = (uint64_t(e.nFileSizeHigh) << 32) | e.nFileSizeLow;
  Store<uint32_t>(info + 0x00, 0);
  Store<uint32_t>(info + 0x04, 0);
  Store<uint64_t>(info + 0x08, FileTimeValue(e.ftCreationTime));
  Store<uint64_t>(info + 0x10, FileTimeValue(e.ftLastAccessTime));
  Store<uint64_t>(info + 0x18, FileTimeValue(e.ftLastWriteTime));
  Store<uint64_t>(info + 0x20, FileTimeValue(e.ftLastWriteTime));
  Store<uint64_t>(info + 0x28, size);
  Store<uint64_t>(info + 0x30, (size + 0xFFF) & ~uint64_t(0xFFF));
  Store<uint32_t>(info + 0x38, GuestAttributes(e.dwFileAttributes));
  Store<uint32_t>(info + 0x3C, uint32_t(name.size()));
  std::memcpy(Ptr(info + 0x40), name.data(), name.size());
  SetIoStatus(io_status, X_STATUS_SUCCESS, needed);
  SignalEvent(event);
  return X_STATUS_SUCCESS;
}

uint32_t NtFlushBuffersFile(uint32_t handle, uint32_t io_status) {
  auto file = LookupHandleAs<File>(handle);
  if (file && file->handle != INVALID_HANDLE_VALUE) FlushFileBuffers(file->handle);
  SetIoStatus(io_status, X_STATUS_SUCCESS, 0);
  return X_STATUS_SUCCESS;
}

uint32_t NtDeviceIoControlFile(uint32_t handle, uint32_t event, uint32_t apc_routine, uint32_t apc_context,
                               uint32_t io_status, uint32_t code, uint32_t input, uint32_t input_length,
                               uint32_t output, uint32_t output_length) {
  (void)handle;
  (void)apc_routine;
  (void)apc_context;
  (void)input;
  (void)input_length;
  KLOG_T("NtDeviceIoControlFile: code %08X", code);
  if (output && output_length) std::memset(Ptr(output), 0, output_length);
  switch (code) {
    case 0x70000:  // IOCTL_DISK_GET_DRIVE_GEOMETRY
      if (output_length >= 8) {
        Store<uint32_t>(output + 0, 0x00100000);  // sectors
        Store<uint32_t>(output + 4, 0x200);       // bytes per sector
      }
      break;
    case 0x74004:  // IOCTL_DISK_GET_PARTITION_INFO
      if (output_length >= 16) {
        Store<uint64_t>(output + 0, 0);
        Store<uint64_t>(output + 8, 0x20000000);
      }
      break;
    default:
      break;
  }
  SetIoStatus(io_status, X_STATUS_SUCCESS, 0);
  SignalEvent(event);
  return X_STATUS_SUCCESS;
}

uint32_t ObCreateSymbolicLink(uint32_t path_ptr, uint32_t target_ptr) {
  std::string path = ReadAnsi(path_ptr), target = ReadAnsi(target_ptr);
  std::string link = Lower(path);
  if (link.rfind("\\??\\", 0) == 0) link = link.substr(4);
  KLOG_I("ObCreateSymbolicLink %s -> %s", path.c_str(), target.c_str());
  std::lock_guard lock(g_vfs_mutex);
  g_links[link] = target;
  return X_STATUS_SUCCESS;
}

uint32_t ObDeleteSymbolicLink(uint32_t path_ptr) {
  std::string link = Lower(ReadAnsi(path_ptr));
  if (link.rfind("\\??\\", 0) == 0) link = link.substr(4);
  std::lock_guard lock(g_vfs_mutex);
  g_links.erase(link);
  return X_STATUS_SUCCESS;
}

// Device/driver plumbing the title links against but does not need here.
uint32_t IoCheckShareAccess() { return X_STATUS_SUCCESS; }
void IoSetShareAccess() {}
void IoRemoveShareAccess() {}
void IoCompleteRequest() {}
uint32_t IoCreateDevice() { return X_STATUS_SUCCESS; }
void IoDeleteDevice() {}
uint32_t IoInvalidDeviceRequest() { return X_STATUS_NOT_SUPPORTED; }
uint32_t IoDismountVolume() { return X_STATUS_SUCCESS; }
uint32_t IoDismountVolumeByFileHandle() { return X_STATUS_SUCCESS; }
uint32_t FscSetCacheElementCount(uint32_t a, uint32_t b) {
  (void)a;
  (void)b;
  return X_STATUS_SUCCESS;
}
uint32_t StfsCreateDevice() { return X_STATUS_SUCCESS; }
uint32_t StfsControlDevice() { return X_STATUS_SUCCESS; }

}  // namespace

namespace x360::kernel {

void InitIo(const TitleInfo& info) {
  std::lock_guard lock(g_vfs_mutex);
  AddDevice("\\Device\\Cdrom0", info.game_root, true);
  AddDevice("\\Device\\Harddisk0\\Partition1", info.user_root / "hdd", false);
  AddDevice("\\Device\\Harddisk0\\Cache0", info.user_root / "cache", false);
  AddDevice("\\Device\\Harddisk0\\Cache1", info.user_root / "cache1", false);
  AddDevice("\\Device\\Content", info.user_root / "content", false);
  AddLink("game:", "\\Device\\Cdrom0");
  AddLink("d:", "\\Device\\Cdrom0");
  AddLink("hdd:", "\\Device\\Harddisk0\\Partition1");
  AddLink("cache:", "\\Device\\Harddisk0\\Cache0");
  KLOG_I("game:\\ -> %s", info.game_root.string().c_str());
  KLOG_I("hdd:\\  -> %s", (info.user_root / "hdd").string().c_str());
}

// Used by XAM content functions to mount save folders.
void MountContent(const std::string& root_name, const std::filesystem::path& host) {
  std::lock_guard lock(g_vfs_mutex);
  std::string device = "\\Device\\Content\\" + root_name;
  g_devices.erase(std::remove_if(g_devices.begin(), g_devices.end(),
                                 [&](const Device& d) { return d.name == Lower(device); }),
                  g_devices.end());
  // Most specific first.
  g_devices.insert(g_devices.begin(), Device{Lower(device), LongPath(host), false});
  std::error_code ec;
  fs::create_directories(host, ec);
  g_links[Lower(root_name + ":")] = device;
}

void UnmountContent(const std::string& root_name) {
  std::lock_guard lock(g_vfs_mutex);
  g_links.erase(Lower(root_name + ":"));
}

}  // namespace x360::kernel

X360_EXPORT(NtCreateFile, NtCreateFile)
X360_EXPORT(NtOpenFile, NtOpenFile)
X360_EXPORT(NtReadFile, NtReadFile)
X360_EXPORT(NtReadFileScatter, NtReadFileScatter)
X360_EXPORT(NtWriteFile, NtWriteFile)
X360_EXPORT(NtWriteFileGather, NtWriteFileGather)
X360_EXPORT(NtQueryInformationFile, NtQueryInformationFile)
X360_EXPORT(NtSetInformationFile, NtSetInformationFile)
X360_EXPORT(NtQueryFullAttributesFile, NtQueryFullAttributesFile)
X360_EXPORT(NtQueryVolumeInformationFile, NtQueryVolumeInformationFile)
X360_EXPORT(NtQueryDirectoryFile, NtQueryDirectoryFile)
X360_EXPORT(NtFlushBuffersFile, NtFlushBuffersFile)
X360_EXPORT(NtDeviceIoControlFile, NtDeviceIoControlFile)
X360_EXPORT(ObCreateSymbolicLink, ObCreateSymbolicLink)
X360_EXPORT(ObDeleteSymbolicLink, ObDeleteSymbolicLink)
X360_EXPORT(IoCheckShareAccess, IoCheckShareAccess)
X360_EXPORT(IoSetShareAccess, IoSetShareAccess)
X360_EXPORT(IoRemoveShareAccess, IoRemoveShareAccess)
X360_EXPORT(IoCompleteRequest, IoCompleteRequest)
X360_EXPORT(IoCreateDevice, IoCreateDevice)
X360_EXPORT(IoDeleteDevice, IoDeleteDevice)
X360_EXPORT(IoInvalidDeviceRequest, IoInvalidDeviceRequest)
X360_EXPORT(IoDismountVolume, IoDismountVolume)
X360_EXPORT(IoDismountVolumeByFileHandle, IoDismountVolumeByFileHandle)
X360_EXPORT(FscSetCacheElementCount, FscSetCacheElementCount)
X360_EXPORT(StfsCreateDevice, StfsCreateDevice)
X360_EXPORT(StfsControlDevice, StfsControlDevice)
