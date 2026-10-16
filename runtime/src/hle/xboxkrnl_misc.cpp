// xboxkrnl modules, console configuration, crypto and power exports.
#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

using namespace x360::kernel;

namespace {

// What the console kernel returns for an export it does not have; the XDK's wrappers for optional
// exports (e.g. XamMediaVerificationVerify) treat the resulting ERROR_PROC_NOT_FOUND as "absent".
constexpr uint32_t kStatusDriverEntrypointNotFound = 0xC0000263;
constexpr uint32_t kKernelModuleHandle = 0x80010000;
constexpr uint32_t kXamModuleHandle = 0x80020000;

uint32_t XexCheckExecutablePrivilege(uint32_t privilege) {
  // Privileges 0-31 are bits of the XEX SYSTEM_FLAGS header.
  uint32_t flags = 0;
  const LoadedTitle& t = Title();
  uint32_t count = Load<uint32_t>(t.xex_header + 0x14);
  for (uint32_t i = 0; i < count; ++i) {
    if (Load<uint32_t>(t.xex_header + 0x18 + i * 8) == 0x00030000) flags = Load<uint32_t>(t.xex_header + 0x1C + i * 8);
  }
  return privilege < 32 ? (flags >> privilege) & 1 : 0;
}

uint32_t XexGetModuleHandle(uint32_t name_ptr, uint32_t handle_ptr) {
  uint32_t handle = 0;
  if (!name_ptr) {
    handle = Title().ldr_entry;
  } else {
    std::string name = Ptr<const char>(name_ptr);
    for (char& c : name) c = char(std::tolower(uint8_t(c)));
    if (name == "xboxkrnl.exe") handle = kKernelModuleHandle;
    else if (name == "xam.xex") handle = kXamModuleHandle;
    else if (name == "default.xex") handle = Title().ldr_entry;
  }
  if (handle_ptr) Store<uint32_t>(handle_ptr, handle);
  return handle ? X_STATUS_SUCCESS : X_STATUS_NO_SUCH_FILE;
}

// Resources are listed in the XEX RESOURCE_INFO header: {char name[8]; address; size}[].
uint32_t XexGetModuleSection(uint32_t module, uint32_t name_ptr, uint32_t data_ptr, uint32_t size_ptr) {
  (void)module;
  const LoadedTitle& t = Title();
  std::string wanted = Ptr<const char>(name_ptr);
  uint32_t count = Load<uint32_t>(t.xex_header + 0x14);
  for (uint32_t i = 0; i < count; ++i) {
    if (Load<uint32_t>(t.xex_header + 0x18 + i * 8) != 0x000002FF) continue;
    uint32_t info = t.xex_header + Load<uint32_t>(t.xex_header + 0x1C + i * 8);
    uint32_t entries = (Load<uint32_t>(info) - 4) / 16;
    for (uint32_t e = 0; e < entries; ++e) {
      uint32_t entry = info + 4 + e * 16;
      std::string name(Ptr<const char>(entry), strnlen(Ptr<const char>(entry), 8));
      if (name != wanted) continue;
      if (data_ptr) Store<uint32_t>(data_ptr, Load<uint32_t>(entry + 8));
      if (size_ptr) Store<uint32_t>(size_ptr, Load<uint32_t>(entry + 12));
      return X_STATUS_SUCCESS;
    }
  }
  return X_STATUS_OBJECT_NAME_NOT_FOUND;
}

uint32_t XexGetProcedureAddress(uint32_t module, uint32_t ordinal, uint32_t out_ptr) {
  KLOG_W("XexGetProcedureAddress(%08X, %u) not supported", module, ordinal);
  if (out_ptr) Store<uint32_t>(out_ptr, 0);
  return kStatusDriverEntrypointNotFound;
}

uint32_t XexLoadImage(uint32_t name_ptr, uint32_t flags, uint32_t min_version, uint32_t handle_ptr) {
  (void)flags;
  (void)min_version;
  KLOG_W("XexLoadImage(%s) not supported", name_ptr ? Ptr<const char>(name_ptr) : "");
  if (handle_ptr) Store<uint32_t>(handle_ptr, 0);
  return X_STATUS_NO_SUCH_FILE;
}
uint32_t XexLoadImageHeaders() { return X_STATUS_NO_SUCH_FILE; }
uint32_t XexUnloadImage(uint32_t handle) {
  (void)handle;
  return X_STATUS_SUCCESS;
}

[[noreturn]] void HalReturnToFirmware(uint32_t routine) {
  KLOG_I("HalReturnToFirmware(%u): title requested exit", routine);
  std::fflush(nullptr);
  std::_Exit(0);
}

void ExRegisterTitleTerminateNotification(uint32_t registration, uint32_t create) {
  (void)registration;
  (void)create;
}

// Console settings (category, id) -> value. Values follow a US retail console in English, 720p,
// widescreen, with no parental controls.
uint32_t ExGetXConfigSetting(uint32_t category, uint32_t setting, uint32_t buffer, uint32_t buffer_size,
                             uint32_t required_ptr) {
  category &= 0xFFFF;
  setting &= 0xFFFF;
  buffer_size &= 0xFFFF;
  uint8_t value[16] = {};
  uint32_t size = 4;
  auto put32 = [&](uint32_t v) { x360::store_be<uint32_t>(value, v); size = 4; };
  switch ((category << 16) | setting) {
    case 0x00020002: put32(0x00001000); break;  // secured: AV region (NTSC-M)
    case 0x00020003: put32(0x000000FF); break;  // secured: game region (all)
    case 0x00030001: put32(0); break;           // user: time zone bias
    case 0x00030002: std::memcpy(value, "UTC", 4); break;
    case 0x00030003: std::memcpy(value, "UTC", 4); break;
    case 0x00030004: put32(0); break;
    case 0x00030005: put32(0); break;
    case 0x00030006: put32(0); break;
    case 0x00030007: put32(0); break;
    case 0x00030009: put32(1); break;           // language: English
    case 0x0003000A: put32(0x00040000); break;  // video flags: widescreen
    case 0x0003000B: put32(0x00010001); break;  // audio flags: stereo / Dolby Digital
    case 0x0003000C: put32(0x40); break;        // retail flags: dashboard setup complete
    case 0x0003000E: value[0] = 103; size = 1; break;  // country: United States
    case 0x0003000F: value[0] = 0; size = 1; break;    // parental control: off
    default:
      KLOG_W("ExGetXConfigSetting(%04X, %04X): unknown setting, returning zeroes", category, setting);
      break;
  }
  if (required_ptr) Store<uint16_t>(required_ptr, uint16_t(size));
  if (buffer) {
    if (buffer_size < size) return X_STATUS_BUFFER_TOO_SMALL;
    std::memcpy(Ptr(buffer), value, size);
  }
  return X_STATUS_SUCCESS;
}

// XeCryptSha(input1, size1, input2, size2, input3, size3, output, output_size)
void XeCryptSha(uint32_t in1, uint32_t size1, uint32_t in2, uint32_t size2, uint32_t in3, uint32_t size3,
                uint32_t out, uint32_t out_size) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  uint8_t digest[20] = {};
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) >= 0 &&
      BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
    if (in1 && size1) BCryptHashData(hash, Ptr(in1), size1, 0);
    if (in2 && size2) BCryptHashData(hash, Ptr(in2), size2, 0);
    if (in3 && size3) BCryptHashData(hash, Ptr(in3), size3, 0);
    BCryptFinishHash(hash, digest, sizeof(digest), 0);
  }
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  if (out) std::memcpy(Ptr(out), digest, std::min<uint32_t>(out_size, 20));
}

uint32_t XeKeysConsolePrivateKeySign(uint32_t hash, uint32_t signature) {
  (void)hash;
  if (signature) std::memset(Ptr(signature), 0, 0x228);
  return 1;
}

uint32_t XeKeysConsoleSignatureVerification(uint32_t hash, uint32_t signature, uint32_t compare_result) {
  (void)hash;
  (void)signature;
  if (compare_result) Store<uint32_t>(compare_result, 0);
  return 1;
}

}  // namespace

X360_EXPORT(XexCheckExecutablePrivilege, XexCheckExecutablePrivilege)
X360_EXPORT(XexGetModuleHandle, XexGetModuleHandle)
X360_EXPORT(XexGetModuleSection, XexGetModuleSection)
X360_EXPORT(XexGetProcedureAddress, XexGetProcedureAddress)
X360_EXPORT(XexLoadImage, XexLoadImage)
X360_EXPORT(XexLoadImageHeaders, XexLoadImageHeaders)
X360_EXPORT(XexUnloadImage, XexUnloadImage)
X360_EXPORT(HalReturnToFirmware, HalReturnToFirmware)
X360_EXPORT(ExRegisterTitleTerminateNotification, ExRegisterTitleTerminateNotification)
X360_EXPORT(ExGetXConfigSetting, ExGetXConfigSetting)
X360_EXPORT(XeCryptSha, XeCryptSha)
X360_EXPORT(XeKeysConsolePrivateKeySign, XeKeysConsolePrivateKeySign)
X360_EXPORT(XeKeysConsoleSignatureVerification, XeKeysConsoleSignatureVerification)
