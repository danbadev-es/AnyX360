// xboxkrnl modules, console configuration, crypto and power exports.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

#include "x360/gpu.h"
#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

using namespace x360::kernel;

// xam.cpp
PPC_EXTERN_FUNC(__imp__XamMediaVerificationCreate);
PPC_EXTERN_FUNC(__imp__XamMediaVerificationClose);
PPC_EXTERN_FUNC(__imp__XamMediaVerificationVerify);
PPC_EXTERN_FUNC(__imp__XamMediaVerificationFailedBlocks);
PPC_EXTERN_FUNC(__imp__XamPartyGetUserList);
PPC_EXTERN_FUNC(__imp__XamPartySendGameInvites);
PPC_EXTERN_FUNC(__imp__XamPartySetCustomData);
PPC_EXTERN_FUNC(__imp__XamPartyGetBandwidth);
PPC_EXTERN_FUNC(__imp__XamShowPartyUI);
PPC_EXTERN_FUNC(__imp__XamShowCommunitySessionsUI);
PPC_EXTERN_FUNC(__imp__XamContentAggregateCreateEnumerator);

namespace {

// What the console kernel returns for an export it does not have; the XDK's wrappers for optional
// exports (e.g. XamVoiceSetMicArrayIdleUsers) treat the resulting ERROR_PROC_NOT_FOUND as "absent".
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
    std::string title = TitlePaths().xex_path.filename().string();  // "default.xex"
    for (char& c : title) c = char(std::tolower(uint8_t(c)));
    if (name == "xboxkrnl.exe") handle = kKernelModuleHandle;
    else if (name == "xam.xex") handle = kXamModuleHandle;
    else if (name == title) handle = Title().ldr_entry;
    else handle = TitleModuleHandle(name);
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

// The exports titles look up by ordinal rather than import, among those implemented. Any other
// ordinal is reported missing, which the XDK's wrappers take as "feature absent".
struct ProcedureExport {
  uint32_t module;
  uint32_t ordinal;
  PPCFunc* function;
};
const ProcedureExport kProcedureExports[] = {
    {kXamModuleHandle, 0x9BB, __imp__XamMediaVerificationCreate},
    {kXamModuleHandle, 0x9BC, __imp__XamMediaVerificationClose},
    {kXamModuleHandle, 0x9BD, __imp__XamMediaVerificationVerify},
    {kXamModuleHandle, 0x9BE, __imp__XamMediaVerificationFailedBlocks},
    // The XDK's party library (XParty*), at every title's start.
    {kXamModuleHandle, 0xAFF, __imp__XamPartyGetUserList},
    {kXamModuleHandle, 0xB00, __imp__XamPartySendGameInvites},
    {kXamModuleHandle, 0xB0B, __imp__XamPartySetCustomData},
    {kXamModuleHandle, 0xB10, __imp__XamPartyGetBandwidth},
    {kXamModuleHandle, 0x305, __imp__XamShowPartyUI},
    {kXamModuleHandle, 0x30B, __imp__XamShowCommunitySessionsUI},
    // Resident Evil 6 enumerates its downloadable content with it.
    {kXamModuleHandle, 0x279, __imp__XamContentAggregateCreateEnumerator},
};

uint32_t XexGetProcedureAddress(uint32_t module, uint32_t ordinal, uint32_t out_ptr) {
  uint32_t address = 0;
  if (IsTitleModule(module)) {
    address = TitleModuleExport(module, ordinal);
    if (!address) KLOG_W("XexGetProcedureAddress(%08X, %u): the title DLL has no such export", module, ordinal);
    if (out_ptr) Store<uint32_t>(out_ptr, address);
    return address ? X_STATUS_SUCCESS : kStatusDriverEntrypointNotFound;
  }
  for (const ProcedureExport& e : kProcedureExports) {
    if (e.module == module && e.ordinal == ordinal) address = GuestAddressOf(e.function);
  }
  if (!address) KLOG_W("XexGetProcedureAddress(%08X, %u) not supported", module, ordinal);
  if (out_ptr) Store<uint32_t>(out_ptr, address);
  return address ? X_STATUS_SUCCESS : kStatusDriverEntrypointNotFound;
}

// xam.xex and xboxkrnl.exe are always loaded: "loading" them gives their handle, as XexGetModuleHandle
// does (the XDK's party library loads xam.xex to look up its exports by ordinal). The title's own
// DLLs recompiled into the executable (GameDescriptor::modules) are loaded; other modules aren't
// supported.
uint32_t XexLoadImage(uint32_t name_ptr, uint32_t flags, uint32_t min_version, uint32_t handle_ptr) {
  (void)flags;
  (void)min_version;
  std::string name = name_ptr ? Ptr<const char>(name_ptr) : "";
  for (char& c : name) c = char(std::tolower(uint8_t(c)));
  uint32_t handle = name == "xam.xex" ? kXamModuleHandle : name == "xboxkrnl.exe" ? kKernelModuleHandle : 0;
  if (!handle) handle = LoadTitleModule(name);
  if (!handle) KLOG_W("XexLoadImage(%s) not supported", name.c_str());
  if (handle_ptr) Store<uint32_t>(handle_ptr, handle);
  return handle ? X_STATUS_SUCCESS : X_STATUS_NO_SUCH_FILE;
}
uint32_t XexLoadImageHeaders() { return X_STATUS_NO_SUCH_FILE; }
uint32_t XexUnloadImage(uint32_t handle) {
  (void)handle;
  return X_STATUS_SUCCESS;
}

[[noreturn]] void HalReturnToFirmware(uint32_t routine) {
  KLOG_I("HalReturnToFirmware(%u): title requested exit", routine);
  x360::gpu::PrepareToLeave();
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

// Incremental SHA-1 and SHA-2 (XeCryptSha*Init/Update/Final). The context lives in guest memory, as
// on the console, so a title may copy it (to hash a shared prefix once): the byte count, the chaining
// state as big-endian words, then the partial block. SHA-1 and SHA-256 use 32-bit words and count
// (0x58 and 0x64 bytes); SHA-384 and SHA-512 64-bit ones (0xC8 bytes), the layouts Xenia uses.
struct ShaKind {
  uint32_t word_size;    // 4 or 8; the block is 16 words
  uint32_t words;        // chaining words: 5 or 8
  uint32_t digest_size;  // bytes of the chaining state the digest takes
  void (*compress)(uint64_t* state, const uint8_t* block);
  uint64_t initial[8];
};

uint32_t Rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
uint32_t Rotr32(uint32_t v, int n) { return (v >> n) | (v << (32 - n)); }
uint64_t Rotr64(uint64_t v, int n) { return (v >> n) | (v << (64 - n)); }

void Sha1Compress(uint64_t* h, const uint8_t* block) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i) w[i] = x360::load_be<uint32_t>(block + i * 4);
  for (int i = 16; i < 80; ++i) w[i] = Rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = uint32_t(h[0]), b = uint32_t(h[1]), c = uint32_t(h[2]), d = uint32_t(h[3]), e = uint32_t(h[4]);
  for (int i = 0; i < 80; ++i) {
    uint32_t f, k;
    if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
    else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
    else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
    else f = b ^ c ^ d, k = 0xCA62C1D6;
    uint32_t t = Rotl32(a, 5) + f + e + k + w[i];
    e = d, d = c, c = Rotl32(b, 30), b = a, a = t;
  }
  const uint32_t add[5] = {a, b, c, d, e};
  for (int i = 0; i < 5; ++i) h[i] = uint32_t(h[i] + add[i]);
}

void Sha256Compress(uint64_t* h, const uint8_t* block) {
  static constexpr uint32_t k[64] = {
      0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
      0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
      0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
      0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
      0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
      0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
      0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
      0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2};
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) w[i] = x360::load_be<uint32_t>(block + i * 4);
  for (int i = 16; i < 64; ++i) {
    uint32_t s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t v[8];
  for (int i = 0; i < 8; ++i) v[i] = uint32_t(h[i]);
  for (int i = 0; i < 64; ++i) {
    uint32_t s1 = Rotr32(v[4], 6) ^ Rotr32(v[4], 11) ^ Rotr32(v[4], 25);
    uint32_t t1 = v[7] + s1 + ((v[4] & v[5]) ^ (~v[4] & v[6])) + k[i] + w[i];
    uint32_t s0 = Rotr32(v[0], 2) ^ Rotr32(v[0], 13) ^ Rotr32(v[0], 22);
    uint32_t t2 = s0 + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
    for (int j = 7; j > 0; --j) v[j] = v[j - 1];
    v[4] += t1;
    v[0] = t1 + t2;
  }
  for (int i = 0; i < 8; ++i) h[i] = uint32_t(h[i] + v[i]);
}

void Sha512Compress(uint64_t* h, const uint8_t* block) {
  static constexpr uint64_t k[80] = {
      0x428A2F98D728AE22, 0x7137449123EF65CD, 0xB5C0FBCFEC4D3B2F, 0xE9B5DBA58189DBBC, 0x3956C25BF348B538,
      0x59F111F1B605D019, 0x923F82A4AF194F9B, 0xAB1C5ED5DA6D8118, 0xD807AA98A3030242, 0x12835B0145706FBE,
      0x243185BE4EE4B28C, 0x550C7DC3D5FFB4E2, 0x72BE5D74F27B896F, 0x80DEB1FE3B1696B1, 0x9BDC06A725C71235,
      0xC19BF174CF692694, 0xE49B69C19EF14AD2, 0xEFBE4786384F25E3, 0x0FC19DC68B8CD5B5, 0x240CA1CC77AC9C65,
      0x2DE92C6F592B0275, 0x4A7484AA6EA6E483, 0x5CB0A9DCBD41FBD4, 0x76F988DA831153B5, 0x983E5152EE66DFAB,
      0xA831C66D2DB43210, 0xB00327C898FB213F, 0xBF597FC7BEEF0EE4, 0xC6E00BF33DA88FC2, 0xD5A79147930AA725,
      0x06CA6351E003826F, 0x142929670A0E6E70, 0x27B70A8546D22FFC, 0x2E1B21385C26C926, 0x4D2C6DFC5AC42AED,
      0x53380D139D95B3DF, 0x650A73548BAF63DE, 0x766A0ABB3C77B2A8, 0x81C2C92E47EDAEE6, 0x92722C851482353B,
      0xA2BFE8A14CF10364, 0xA81A664BBC423001, 0xC24B8B70D0F89791, 0xC76C51A30654BE30, 0xD192E819D6EF5218,
      0xD69906245565A910, 0xF40E35855771202A, 0x106AA07032BBD1B8, 0x19A4C116B8D2D0C8, 0x1E376C085141AB53,
      0x2748774CDF8EEB99, 0x34B0BCB5E19B48A8, 0x391C0CB3C5C95A63, 0x4ED8AA4AE3418ACB, 0x5B9CCA4F7763E373,
      0x682E6FF3D6B2B8A3, 0x748F82EE5DEFB2FC, 0x78A5636F43172F60, 0x84C87814A1F0AB72, 0x8CC702081A6439EC,
      0x90BEFFFA23631E28, 0xA4506CEBDE82BDE9, 0xBEF9A3F7B2C67915, 0xC67178F2E372532B, 0xCA273ECEEA26619C,
      0xD186B8C721C0C207, 0xEADA7DD6CDE0EB1E, 0xF57D4F7FEE6ED178, 0x06F067AA72176FBA, 0x0A637DC5A2C898A6,
      0x113F9804BEF90DAE, 0x1B710B35131C471B, 0x28DB77F523047D84, 0x32CAAB7B40C72493, 0x3C9EBE0A15C9BEBC,
      0x431D67C49C100D4C, 0x4CC5D4BECB3E42B6, 0x597F299CFC657E2A, 0x5FCB6FAB3AD6FAEC, 0x6C44198C4A475817};
  uint64_t w[80];
  for (int i = 0; i < 16; ++i) w[i] = x360::load_be<uint64_t>(block + i * 8);
  for (int i = 16; i < 80; ++i) {
    uint64_t s0 = Rotr64(w[i - 15], 1) ^ Rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
    uint64_t s1 = Rotr64(w[i - 2], 19) ^ Rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint64_t v[8];
  for (int i = 0; i < 8; ++i) v[i] = h[i];
  for (int i = 0; i < 80; ++i) {
    uint64_t s1 = Rotr64(v[4], 14) ^ Rotr64(v[4], 18) ^ Rotr64(v[4], 41);
    uint64_t t1 = v[7] + s1 + ((v[4] & v[5]) ^ (~v[4] & v[6])) + k[i] + w[i];
    uint64_t s0 = Rotr64(v[0], 28) ^ Rotr64(v[0], 34) ^ Rotr64(v[0], 39);
    uint64_t t2 = s0 + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
    for (int j = 7; j > 0; --j) v[j] = v[j - 1];
    v[4] += t1;
    v[0] = t1 + t2;
  }
  for (int i = 0; i < 8; ++i) h[i] += v[i];
}

constexpr ShaKind kSha1 = {4, 5, 20, Sha1Compress, {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}};
constexpr ShaKind kSha256 = {4, 8, 32, Sha256Compress,
                             {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB,
                              0x5BE0CD19}};
constexpr ShaKind kSha384 = {8, 8, 48, Sha512Compress,
                             {0xCBBB9D5DC1059ED8, 0x629A292A367CD507, 0x9159015A3070DD17, 0x152FECD8F70E5939,
                              0x67332667FFC00B31, 0x8EB44A8768581511, 0xDB0C2E0D64F98FA7, 0x47B5481DBEFA4FA4}};
constexpr ShaKind kSha512 = {8, 8, 64, Sha512Compress,
                             {0x6A09E667F3BCC908, 0xBB67AE8584CAA73B, 0x3C6EF372FE94F82B, 0xA54FF53A5F1D36F1,
                              0x510E527FADE682D1, 0x9B05688C2B3E6C1F, 0x1F83D9ABFB41BD6B, 0x5BE0CD19137E2179}};

// The guest context, loaded into host words and stored back.
struct ShaContext {
  const ShaKind& kind;
  uint32_t address;
  uint64_t count = 0;
  uint64_t state[8] = {};

  ShaContext(const ShaKind& k, uint32_t a) : kind(k), address(a) {}
  uint32_t BlockSize() const { return kind.word_size * 16; }
  uint8_t* Buffer() const { return Ptr<uint8_t>(address + kind.word_size * (1 + kind.words)); }
  void Load() {
    count = kind.word_size == 8 ? x360::kernel::Load<uint64_t>(address) : x360::kernel::Load<uint32_t>(address);
    for (uint32_t i = 0; i < kind.words; ++i)
      state[i] = kind.word_size == 8 ? x360::kernel::Load<uint64_t>(address + 8 + i * 8)
                                     : x360::kernel::Load<uint32_t>(address + 4 + i * 4);
  }
  void Save() const {
    if (kind.word_size == 8) x360::kernel::Store<uint64_t>(address, count);
    else x360::kernel::Store<uint32_t>(address, uint32_t(count));
    for (uint32_t i = 0; i < kind.words; ++i) {
      if (kind.word_size == 8) x360::kernel::Store<uint64_t>(address + 8 + i * 8, state[i]);
      else x360::kernel::Store<uint32_t>(address + 4 + i * 4, uint32_t(state[i]));
    }
  }
};

void ShaInit(const ShaKind& kind, uint32_t context) {
  ShaContext sha(kind, context);
  std::memset(Ptr(context), 0, kind.word_size * (1 + kind.words) + sha.BlockSize());
  std::memcpy(sha.state, kind.initial, sizeof(sha.state));
  sha.Save();
}

void ShaUpdate(const ShaKind& kind, uint32_t context, uint32_t input, uint32_t size) {
  ShaContext sha(kind, context);
  sha.Load();
  const uint32_t block = sha.BlockSize();
  uint32_t used = uint32_t(sha.count % block);
  sha.count += size;
  const uint8_t* data = size ? Ptr<const uint8_t>(input) : nullptr;
  while (size) {
    uint32_t n = std::min(block - used, size);
    std::memcpy(sha.Buffer() + used, data, n);
    used += n, data += n, size -= n;
    if (used == block) {
      kind.compress(sha.state, sha.Buffer());
      used = 0;
    }
  }
  sha.Save();
}

// Pads in place, as the console does: the last block compressed and the final state stay in the
// context.
void ShaFinal(const ShaKind& kind, uint32_t context, uint32_t output, uint32_t output_size) {
  ShaContext sha(kind, context);
  sha.Load();
  const uint32_t block = sha.BlockSize();
  const uint32_t length_size = kind.word_size * 2;  // the message length in bits: 8 or 16 bytes
  uint8_t* buffer = sha.Buffer();
  uint32_t used = uint32_t(sha.count % block);
  buffer[used++] = 0x80;
  if (used > block - length_size) {
    std::memset(buffer + used, 0, block - used);
    kind.compress(sha.state, buffer);
    used = 0;
  }
  std::memset(buffer + used, 0, block - used);
  x360::store_be<uint64_t>(buffer + block - 8, sha.count * 8);
  kind.compress(sha.state, buffer);
  sha.Save();
  uint8_t digest[64];
  for (uint32_t i = 0; i < kind.words; ++i) {
    if (kind.word_size == 8) x360::store_be<uint64_t>(digest + i * 8, sha.state[i]);
    else x360::store_be<uint32_t>(digest + i * 4, uint32_t(sha.state[i]));
  }
  if (output) std::memcpy(Ptr(output), digest, std::min(output_size, kind.digest_size));
}

void XeCryptShaInit(uint32_t context) { ShaInit(kSha1, context); }
void XeCryptShaUpdate(uint32_t context, uint32_t input, uint32_t size) { ShaUpdate(kSha1, context, input, size); }
void XeCryptShaFinal(uint32_t context, uint32_t out, uint32_t out_size) { ShaFinal(kSha1, context, out, out_size); }
void XeCryptSha256Init(uint32_t context) { ShaInit(kSha256, context); }
void XeCryptSha256Update(uint32_t context, uint32_t input, uint32_t size) { ShaUpdate(kSha256, context, input, size); }
void XeCryptSha256Final(uint32_t context, uint32_t out, uint32_t out_size) { ShaFinal(kSha256, context, out, out_size); }
void XeCryptSha384Init(uint32_t context) { ShaInit(kSha384, context); }
void XeCryptSha384Update(uint32_t context, uint32_t input, uint32_t size) { ShaUpdate(kSha384, context, input, size); }
void XeCryptSha384Final(uint32_t context, uint32_t out, uint32_t out_size) { ShaFinal(kSha384, context, out, out_size); }
void XeCryptSha512Init(uint32_t context) { ShaInit(kSha512, context); }
void XeCryptSha512Update(uint32_t context, uint32_t input, uint32_t size) { ShaUpdate(kSha512, context, input, size); }
void XeCryptSha512Final(uint32_t context, uint32_t out, uint32_t out_size) { ShaFinal(kSha512, context, out, out_size); }

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

// Keys a title obscures with the console's key obfuscation key (key 0x18 in the hypervisor) and
// later uses by their obscured form. Any fixed key does: this one is all zeros, as Xenia's, so the
// data a title encrypts with them (Forza Motorsport 4's saves) is the same as in Xenia.
const uint8_t kKeyObfuscationKey[16] = {};

// An AES state in guest memory for these functions, one per thread (allocated once).
uint32_t ObfuscationState() {
  thread_local const uint32_t state = SystemAlloc(kAesStateSize);
  std::memset(Ptr(state), 0, kAesStateSize);
  std::memcpy(Ptr(state), kKeyObfuscationKey, 16);
  return state;
}

uint32_t XeKeysObscureKey(uint32_t input, uint32_t output) {
  XeCryptAesEcb(ObfuscationState(), input, output, 1);
  return X_STATUS_SUCCESS;
}

// XeKeysAesCbcUsingKey(obscured key, input, size, output, feed, encrypt).
uint32_t XeKeysAesCbcUsingKey(uint32_t obscured_key, uint32_t input, uint32_t size, uint32_t output, uint32_t feed,
                              uint32_t encrypt) {
  const uint32_t state = ObfuscationState();
  uint8_t key[16];
  std::memcpy(key, Ptr(obscured_key), 16);
  AesInPlace(state, false, false, key, 16, nullptr);
  std::memcpy(Ptr(state), key, 16);
  XeCryptAesCbc(state, input, size, output, feed, encrypt);
  return X_STATUS_SUCCESS;
}

// The security engine's key slots a title can set (XeKeysSetKey) and then use by number
// (XeKeysAesCbc). The XDK's XKeysSetKey/XKeysAesCbc put a title's keys in slots 0xE0-0xE7; Dance
// Central 2 and 3 derive their songs' keys this way. Only keys set here can be used: the console's
// own keys (the other slots) aren't known. A title that gives its keys in the console's obscured
// form (GameDescriptor::title_keys) gets their real values from keys\title_keys.txt.
struct KeySlot {
  bool set = false;
  uint8_t key[16] = {};
};
std::mutex g_key_slots_mutex;
KeySlot g_key_slots[256];

// Title keys (GameDescriptor::title_keys): a title's keys as the console obscures them, kept in its
// data, mapped to their real values (keys\title_keys.txt). The console's hypervisor turns them back
// itself; AnyX360 can't, so it looks them up. Dance Central 2 and 3 key their songs this way.
std::mutex g_title_keys_mutex;
bool g_title_keys_loaded = false;
std::map<std::array<uint8_t, 16>, std::array<uint8_t, 16>> g_title_keys;

bool ParseHexKey(const std::string& text, std::array<uint8_t, 16>& key) {
  if (text.size() != 32) return false;
  for (size_t i = 0; i < 16; ++i) {
    unsigned value = 0;
    for (size_t j = 0; j < 2; ++j) {
      const char c = text[i * 2 + j];
      value <<= 4;
      if (c >= '0' && c <= '9') value |= unsigned(c - '0');
      else if (c >= 'a' && c <= 'f') value |= unsigned(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value |= unsigned(c - 'A' + 10);
      else return false;
    }
    key[i] = uint8_t(value);
  }
  return true;
}

uint32_t XeKeysSetKey(uint32_t key_num, uint32_t key, uint32_t size) {
  if (key_num > 0xFF || !key || size != 16) return X_STATUS_INVALID_PARAMETER;
  uint8_t value[16];
  std::memcpy(value, Ptr(key), 16);
  bool uses_title_keys = false;
  if (!FindTitleKey(value, value, uses_title_keys) && uses_title_keys) {
    // The game would wait for data it can't decrypt (Dance Central 3 stays on a loading screen).
    const std::string path = TitlePaths().title_keys_path.string();
    std::fprintf(stderr,
                 "%s: a key the game needs (XeKeysSetKey slot %#x) isn't in %s. Make the file again from a copy "
                 "that holds the real keys (tools/scripts/title_keys.py, keys/README.md).\n",
                 TitlePaths().title_name.c_str(), key_num, path.c_str());
    std::fflush(stderr);
    ShowPlayerError(TitlePaths().title_name + " can't go on: a key it needs isn't in " + path +
                    ". Add the AnyX360 release's keys\\title_keys.txt again.");
    std::_Exit(3);
  }
  std::lock_guard lock(g_key_slots_mutex);
  g_key_slots[key_num].set = true;
  std::memcpy(g_key_slots[key_num].key, value, 16);
  return X_STATUS_SUCCESS;
}

// XeKeysAesCbc(key number, input, size, output, feed, encrypt): AES-CBC with a slot's key. Without
// a feed the IV is zero (one block: ECB).
uint32_t XeKeysAesCbc(uint32_t key_num, uint32_t input, uint32_t size, uint32_t output, uint32_t feed, uint32_t encrypt) {
  uint8_t key[16];
  {
    std::lock_guard lock(g_key_slots_mutex);
    if (key_num > 0xFF || !g_key_slots[key_num].set) {
      KLOG_W("XeKeysAesCbc: key %#x wasn't set by the title", key_num);
      return X_STATUS_INVALID_PARAMETER;
    }
    std::memcpy(key, g_key_slots[key_num].key, 16);
  }
  size &= ~15u;
  if (!input || !output || !size) return X_STATUS_INVALID_PARAMETER;
  thread_local const uint32_t state = SystemAlloc(kAesStateSize);
  std::memset(Ptr(state), 0, kAesStateSize);
  std::memcpy(Ptr(state), key, 16);
  std::memmove(Ptr(output), Ptr(input), size);
  uint8_t iv[16] = {};
  if (feed) std::memcpy(iv, Ptr(feed), 16);
  AesInPlace(state, true, encrypt != 0, Ptr<uint8_t>(output), size, iv);
  if (feed) std::memcpy(Ptr(feed), iv, 16);
  return X_STATUS_SUCCESS;
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

bool x360::kernel::LoadTitleKeys(const std::filesystem::path& path, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = "it wasn't found";
    return false;
  }
  std::map<std::array<uint8_t, 16>, std::array<uint8_t, 16>> keys;
  std::string line;
  for (int number = 1; std::getline(in, line); ++number) {
    if (const size_t hash = line.find('#'); hash != std::string::npos) line.resize(hash);
    std::istringstream fields(line);
    std::string obscured_text, real_text, extra;
    if (!(fields >> obscured_text)) continue;  // blank or a comment
    std::array<uint8_t, 16> obscured{}, real{};
    if (!(fields >> real_text) || (fields >> extra) || !ParseHexKey(obscured_text, obscured) ||
        !ParseHexKey(real_text, real)) {
      error = "line " + std::to_string(number) + " isn't \"<obscured> <real>\" (32 hex digits each)";
      return false;
    }
    keys[obscured] = real;
  }
  if (keys.empty()) {
    error = "it holds no key";
    return false;
  }
  std::lock_guard lock(g_title_keys_mutex);
  g_title_keys = std::move(keys);
  g_title_keys_loaded = true;
  KLOG_I("title keys: %zu from %s", g_title_keys.size(), path.string().c_str());
  return true;
}

bool x360::kernel::FindTitleKey(const uint8_t obscured[16], uint8_t real[16], bool& uses_title_keys) {
  std::lock_guard lock(g_title_keys_mutex);
  uses_title_keys = g_title_keys_loaded;
  if (!g_title_keys_loaded) return false;
  std::array<uint8_t, 16> key;
  std::memcpy(key.data(), obscured, 16);
  const auto found = g_title_keys.find(key);
  if (found != g_title_keys.end()) {
    std::memcpy(real, found->second.data(), 16);
    return true;
  }
  // A copy whose executable already holds the real keys (a re-packed dump) gives them as they are.
  for (const auto& [obscured_key, real_key] : g_title_keys) {
    if (real_key == key) {
      std::memcpy(real, key.data(), 16);
      return true;
    }
  }
  return false;
}

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
X360_EXPORT(XeCryptShaInit, XeCryptShaInit)
X360_EXPORT(XeCryptShaUpdate, XeCryptShaUpdate)
X360_EXPORT(XeCryptShaFinal, XeCryptShaFinal)
X360_EXPORT(XeCryptSha256Init, XeCryptSha256Init)
X360_EXPORT(XeCryptSha256Update, XeCryptSha256Update)
X360_EXPORT(XeCryptSha256Final, XeCryptSha256Final)
X360_EXPORT(XeCryptSha384Init, XeCryptSha384Init)
X360_EXPORT(XeCryptSha384Update, XeCryptSha384Update)
X360_EXPORT(XeCryptSha384Final, XeCryptSha384Final)
X360_EXPORT(XeCryptSha512Init, XeCryptSha512Init)
X360_EXPORT(XeCryptSha512Update, XeCryptSha512Update)
X360_EXPORT(XeCryptSha512Final, XeCryptSha512Final)
X360_EXPORT(XeCryptRandom, XeCryptRandom)
X360_EXPORT(XeCryptAesKey, XeCryptAesKey)
X360_EXPORT(XeCryptAesEcb, XeCryptAesEcb)
X360_EXPORT(XeCryptAesCbc, XeCryptAesCbc)
X360_EXPORT(XeKeysGetConsoleID, XeKeysGetConsoleID)
X360_EXPORT(XeKeysObscureKey, XeKeysObscureKey)
X360_EXPORT(XeKeysAesCbcUsingKey, XeKeysAesCbcUsingKey)
X360_EXPORT(XeKeysSetKey, XeKeysSetKey)
X360_EXPORT(XeKeysAesCbc, XeKeysAesCbc)
X360_EXPORT(XeKeysConsolePrivateKeySign, XeKeysConsolePrivateKeySign)
X360_EXPORT(XeKeysConsoleSignatureVerification, XeKeysConsoleSignatureVerification)
