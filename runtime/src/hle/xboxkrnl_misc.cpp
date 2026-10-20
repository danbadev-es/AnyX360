// xboxkrnl modules, console configuration, crypto and power exports.
#include <mutex>

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

uint32_t XexGetModuleSection(uint32_t module, uint32_t name_ptr, uint32_t data_ptr, uint32_t size_ptr) {
  (void)module;
  uint32_t address, size;
  if (!FindXexResource(Ptr<const char>(name_ptr), address, size)) return X_STATUS_OBJECT_NAME_NOT_FOUND;
  if (data_ptr) Store<uint32_t>(data_ptr, address);
  if (size_ptr) Store<uint32_t>(size_ptr, size);
  return X_STATUS_SUCCESS;
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
    case 0x00030009: put32(TitlePaths().language); break;  // language (the player profile's)
    case 0x0003000A: put32(0x00040000); break;  // video flags: widescreen
    case 0x0003000B: put32(0x00010001); break;  // audio flags: stereo / Dolby Digital
    case 0x0003000C: put32(0x40); break;        // retail flags: dashboard setup complete
    case 0x0003000E: value[0] = uint8_t(TitlePaths().country); size = 1; break;  // country
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

void XeCryptRandom(uint32_t buffer, uint32_t length) {
  if (buffer && length) BCryptGenRandom(nullptr, Ptr(buffer), length, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

// AES-128. XECRYPT_AES_STATE (0x160 bytes) holds the console's key schedule; here it holds the key
// itself in its first 16 bytes (only these functions read it). Encrypted game data (Dance Central 3's
// songs) is read through them.
constexpr uint32_t kAesStateSize = 0x160;

BCRYPT_ALG_HANDLE AesAlgorithm(bool cbc) {
  static BCRYPT_ALG_HANDLE handles[2] = {};
  static std::once_flag once;
  std::call_once(once, [] {
    for (int i = 0; i < 2; ++i) {
      if (BCryptOpenAlgorithmProvider(&handles[i], BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) continue;
      const wchar_t* mode = i ? BCRYPT_CHAIN_MODE_CBC : BCRYPT_CHAIN_MODE_ECB;
      BCryptSetProperty(handles[i], BCRYPT_CHAINING_MODE, (PUCHAR)mode, ULONG((wcslen(mode) + 1) * sizeof(wchar_t)), 0);
    }
  });
  return handles[cbc ? 1 : 0];
}

// Runs AES over `size` bytes (a multiple of 16) in place; with CBC, `iv` is updated to chain on.
void AesInPlace(uint32_t state, bool cbc, bool encrypt, uint8_t* data, uint32_t size, uint8_t* iv) {
  BCRYPT_KEY_HANDLE key = nullptr;
  if (BCryptGenerateSymmetricKey(AesAlgorithm(cbc), &key, nullptr, 0, Ptr<uint8_t>(state), 16, 0) < 0) {
    KLOG_E("XeCryptAes: no AES key");
    return;
  }
  ULONG done = 0;
  if (encrypt)
    BCryptEncrypt(key, data, size, nullptr, iv, iv ? 16 : 0, data, size, &done, 0);
  else
    BCryptDecrypt(key, data, size, nullptr, iv, iv ? 16 : 0, data, size, &done, 0);
  BCryptDestroyKey(key);
}

void XeCryptAesKey(uint32_t state, uint32_t key) {
  std::memset(Ptr(state), 0, kAesStateSize);
  std::memcpy(Ptr(state), Ptr(key), 16);
}

void XeCryptAesEcb(uint32_t state, uint32_t input, uint32_t output, uint32_t encrypt) {
  std::memmove(Ptr(output), Ptr(input), 16);
  AesInPlace(state, false, encrypt != 0, Ptr<uint8_t>(output), 16, nullptr);
}

// XeCryptAesCbc(state, input, size, output, feed, encrypt): `feed` is the IV, left as the last
// ciphertext block for the next call.
void XeCryptAesCbc(uint32_t state, uint32_t input, uint32_t size, uint32_t output, uint32_t feed, uint32_t encrypt) {
  size &= ~15u;
  if (!size) return;
  std::memmove(Ptr(output), Ptr(input), size);
  uint8_t iv[16];
  std::memcpy(iv, Ptr(feed), 16);
  AesInPlace(state, true, encrypt != 0, Ptr<uint8_t>(output), size, iv);
  std::memcpy(Ptr(feed), iv, 16);
}

// A made-up console ID: 5 raw bytes and the same as 12 decimal digits (not NUL-terminated).
uint32_t XeKeysGetConsoleID(uint32_t raw_ptr, uint32_t text_ptr) {
  static const uint8_t kRaw[5] = {0x39, 0x10, 0x40, 0x66, 0x7E};
  if (raw_ptr) std::memcpy(Ptr(raw_ptr), kRaw, sizeof(kRaw));
  if (text_ptr) std::memcpy(Ptr(text_ptr), "245149100000", 12);
  return X_STATUS_SUCCESS;
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
X360_EXPORT(XeCryptRandom, XeCryptRandom)
X360_EXPORT(XeCryptAesKey, XeCryptAesKey)
X360_EXPORT(XeCryptAesEcb, XeCryptAesEcb)
X360_EXPORT(XeCryptAesCbc, XeCryptAesCbc)
X360_EXPORT(XeKeysGetConsoleID, XeKeysGetConsoleID)
X360_EXPORT(XeKeysConsolePrivateKeySign, XeKeysConsolePrivateKeySign)
X360_EXPORT(XeKeysConsoleSignatureVerification, XeKeysConsoleSignatureVerification)
