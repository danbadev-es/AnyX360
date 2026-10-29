// The avatar system (see avatar_system.h and docs/AVATARS.md). The pack's parts, the descriptions
// and the parts' models are x360tool's (avatar_look.h, shared with the launcher's avatar editor);
// this file lays them out in guest memory.
//
// The result buffer is laid out as the avatar library's own builder lays out its built-in avatar
// (Kinect Sports 82A84B80): a 20-byte header {skeleton*, 0, part count, asset records*, part
// descriptors*}, the skeleton (count, joints*, then 72 joints of 96 bytes, filled by the title's
// skeleton builder), the 32-byte asset records, the 52-byte part descriptors and each part's CPU
// block; the GPU buffer holds each part's textures, vertices and indices, 4 KiB aligned. Pointers
// are absolute guest addresses.
#include "avatar_system.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <vector>

#include "avatar.h"
#include "avatar_look.h"
#include "avatar_model.h"
#include "util.h"
#include "x360/kernel.h"
#include "x360/profiles.h"

namespace x360::avatar {

using namespace x360::kernel;
using x360tool::AvatarDescription;
using x360tool::AvatarPart;

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kMaxParts = 13;  // Kinect Sports keeps headers for 13 parts
constexpr uint32_t kJoints = 72;    // room the library reserves (71 used)
constexpr uint32_t kJointBytes = 96;
constexpr uint32_t kRecordBytes = 32;
constexpr uint32_t kDescriptorBytes = 52;

struct State {
  std::once_flag once;
  bool loaded = false;
  x360tool::AvatarCatalog catalog;
  bool left_handed = false;
} g;
std::mutex g_mutex;

uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

fs::path SystemFolder() { return TitlePaths().user_root.parent_path() / "system"; }

// The pack's table: the shared copy (the launcher writes it when it adds a game that has one), or
// the game's own package (copied to the shared folder for avatar games that don't carry one).
bool FindPack(fs::path& toc) {
  toc = SystemFolder() / x360tool::kAvatarPackToc;
  std::error_code ec;
  if (fs::exists(toc, ec)) return true;
  const fs::path package = TitlePaths().game_root / x360tool::kAvatarPackPackage;
  if (!fs::exists(package, ec)) return false;
  std::string error;
  if (!x360tool::extract_avatar_pack(package, toc, error)) {
    KLOG_W("avatars: %s: %s", package.string().c_str(), error.c_str());
    return false;
  }
  KLOG_I("avatars: copied the disc's avatar asset pack to %s", toc.string().c_str());
  return true;
}

void LoadPack() {
  fs::path toc;
  if (!FindPack(toc)) {
    KLOG_W("avatars: no avatar asset pack (it comes with discs of avatar games, e.g. Kinect Sports)");
    return;
  }
  std::string error;
  try {
    if (!g.catalog.load(x360tool::read_file(toc), error)) {
      KLOG_W("avatars: %s: %s", toc.string().c_str(), error.c_str());
      return;
    }
  } catch (const std::exception& e) {
    KLOG_W("avatars: %s: %s", toc.string().c_str(), e.what());
    return;
  }
  g.loaded = true;
  KLOG_I("avatars: %zu parts and %zu face textures and features in %s", g.catalog.parts().size(),
         g.catalog.extras().size(), toc.string().c_str());
}

void EnsureLoaded() {
  std::call_once(g.once, LoadPack);
}

// Testing: ANYX360_AVATAR changes the players' avatars (not the random ones titles ask for), whatever
// their profiles have (x360tool::customize_avatar).
const char* OverrideSpec() {
  const char* spec = std::getenv("ANYX360_AVATAR");
  return spec && *spec ? spec : nullptr;
}

AvatarDescription Describe(Look look, uint32_t seed) {
  const x360tool::AvatarLook which = look == Look::kMale     ? x360tool::AvatarLook::kMale
                                     : look == Look::kFemale ? x360tool::AvatarLook::kFemale
                                                             : x360tool::AvatarLook::kRandom;
  return x360tool::make_avatar_description(g.catalog, which, seed);
}

void Write(uint32_t metadata, const AvatarDescription& d) {
  x360tool::write_avatar_description(g.catalog, d, std::span<uint8_t>(Ptr<uint8_t>(metadata), kMetadataSize));
}

bool Read(uint32_t metadata, AvatarDescription& d) {
  return metadata && x360tool::read_avatar_description(
                         g.catalog, std::span<const uint8_t>(Ptr<uint8_t>(metadata), kMetadataSize), d);
}

// The GPU's tiled layout of 2D textures (Xenia's texture_address::Tiled2D): byte offset of block
// (x, y) for a pitch in blocks (aligned to 32).
uint32_t Tiled2D(uint32_t x, uint32_t y, uint32_t pitch, uint32_t block_bytes) {
  const uint32_t bpb_log2 = block_bytes == 8 ? 3 : block_bytes == 16 ? 4 : block_bytes == 4 ? 2 : 0;
  const uint32_t outer = ((y >> 5) * (pitch >> 5) + (x >> 5)) << 6;
  const uint32_t inner = (((y >> 1) & 7) << 3) | (x & 7);
  const uint32_t bytes = (outer | inner) << bpb_log2;
  const uint32_t bank = (y >> 4) & 1;
  const uint32_t pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1);
  return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (bytes & 15) | (((bytes >> 4) & 1) << 5) |
         (((bytes >> 5) & 7) << 8) | ((bytes >> 8) << 12);
}

}  // namespace

void Initialize(uint32_t coordinate_system) {
  g.left_handed = coordinate_system != 1;
  if (TitlePaths().avatar_skeleton) EnsureLoaded();
}

bool Available() {
  if (!TitlePaths().avatar_skeleton) return false;
  EnsureLoaded();
  return g.loaded;
}

void WriteMetadata(uint32_t metadata, Look look, uint32_t seed) {
  std::memset(Ptr<uint8_t>(metadata), 0, kMetadataSize);
  if (!Available()) return;
  std::lock_guard lock(g_mutex);
  AvatarDescription d = Describe(look, seed);
  if (look != Look::kRandom)
    if (const char* spec = OverrideSpec()) x360tool::customize_avatar(g.catalog, d, spec);
  Write(metadata, d);
}

bool Overridden() { return OverrideSpec() != nullptr; }

std::optional<std::vector<uint8_t>> ProfileDescription(const fs::path& profiles_root, uint64_t xuid) {
  if (!xuid) return std::nullopt;
  const std::optional<profiles::Profile> profile = profiles::Load(profiles_root, xuid);
  if (!profile || profile->avatar.empty()) return std::nullopt;
  std::vector<uint8_t> bytes = x360tool::base64_decode(profile->avatar);
  if (bytes.size() != kMetadataSize) {
    KLOG_W("avatars: profile %s: its avatar isn't a description (%zu bytes)", profiles::XuidText(xuid).c_str(),
           bytes.size());
    return std::nullopt;
  }
  return bytes;
}

Manifest ChooseManifest(const fs::path& profiles_root, uint32_t user_index, uint64_t profile_xuid, bool overridden) {
  Manifest manifest;
  manifest.look = (user_index & 1) ? Look::kFemale : Look::kMale;
  if (!overridden) manifest.description = ProfileDescription(profiles_root, profile_xuid);
  return manifest;
}

bool WriteDescription(uint32_t metadata, std::span<const uint8_t> description) {
  if (!metadata || description.size() != kMetadataSize || !Available()) return false;
  std::lock_guard lock(g_mutex);
  AvatarDescription d;
  // Only what names the pack's own assets: a description of parts this pack doesn't have would draw
  // the default avatar anyway.
  if (!x360tool::read_avatar_description(g.catalog, description, d)) return false;
  std::memcpy(Ptr<uint8_t>(metadata), description.data(), kMetadataSize);
  return true;
}

uint32_t BodyType(uint32_t metadata) {
  if (!Available()) return 1;
  std::lock_guard lock(g_mutex);
  AvatarDescription d;
  return Read(metadata, d) ? d.body : 1;
}

void ResultSizes(uint32_t mask, uint32_t& result, uint32_t& gpu) {
  result = gpu = 0;
  if (!Available()) return;
  uint32_t cpu = 0x40 + 16 + kJoints * kJointBytes + kMaxParts * (kRecordBytes + kDescriptorBytes);
  for (uint32_t bit = 0; bit < x360tool::kAvatarSlots; ++bit) {
    if (!(mask & (1u << bit))) continue;
    cpu += AlignUp(g.catalog.max_cpu(bit), 16) + 16;
    gpu += g.catalog.max_gpu(bit);
  }
  result = AlignUp(cpu, 16);
}

bool BuildAssets(uint32_t metadata, uint32_t mask, uint32_t result, uint32_t result_size, uint32_t gpu,
                 uint32_t gpu_size, std::string& error) {
  if (!Available()) {
    error = "no avatar asset pack";
    return false;
  }
  std::lock_guard lock(g_mutex);
  AvatarDescription d;
  const bool described = Read(metadata, d);
  // Not a description of the pack's assets: the default avatar.
  if (!described) d = Describe(Look::kMale, 0);

  // The parts in the mask, in component order: Kinect Sports expects the head first and the body
  // second. Parts with nothing to draw ("Bald") stay out: it stops drawing an avatar at the first
  // part without indices (8259E750).
  // Testing: ANYX360_AVATAR_SHAPES=0 leaves the parts as they are, to compare.
  static const bool shapes = [] {
    const char* v = std::getenv("ANYX360_AVATAR_SHAPES");
    return !(v && *v == '0');
  }();
  std::vector<x360tool::AvatarBuiltPart> parts;
  std::vector<std::string> warnings;
  x360tool::AvatarBuildOptions options;
  options.mask = mask;
  options.left_handed = g.left_handed;
  options.shapes = shapes;
  if (!x360tool::build_avatar(g.catalog, d, options, parts, error, &warnings)) return false;
  for (const std::string& warning : warnings) KLOG_W("avatars: %s", warning.c_str());
  if (parts.size() > kMaxParts) parts.resize(kMaxParts);
  const uint32_t n = uint32_t(parts.size());

  // Layout.
  const uint32_t skeleton = AlignUp(result + 20, 16);
  const uint32_t records = skeleton + 16 + kJoints * kJointBytes;
  const uint32_t descriptors = AlignUp(records + n * kRecordBytes, 4);
  uint32_t cpu = AlignUp(descriptors + n * kDescriptorBytes, 16);
  uint32_t gpu_at = 0;
  std::memset(Ptr<uint8_t>(result), 0, result_size);
  Store<uint32_t>(result + 0, skeleton);
  Store<uint32_t>(result + 4, 0);
  Store<uint32_t>(result + 8, n);
  Store<uint32_t>(result + 12, records);
  Store<uint32_t>(result + 16, descriptors);

  // The skeleton, made by the title's avatar library for this body.
  Store<uint32_t>(skeleton + 0, 0);
  Store<uint32_t>(skeleton + 4, skeleton + 16);
  CallGuest(TitlePaths().avatar_skeleton, {skeleton, d.body}, {d.height, d.weight});

  const x360tool::AvatarTileFn tile = [](uint32_t x, uint32_t y, uint32_t pitch, uint32_t block_bytes) {
    return Tiled2D(x, y, pitch, block_bytes);
  };
  for (uint32_t i = 0; i < n; ++i) {
    const AvatarPart& part = *parts[i].part;
    const x360tool::AvatarModel& model = parts[i].model;
    const uint32_t cpu_size = model.cpu_size(), part_gpu = AlignUp(model.gpu_size(), 0x1000);
    if (cpu + cpu_size > result + result_size || gpu_at + part_gpu > gpu_size) {
      error = "buffers too small";
      return false;
    }
    // The asset record: the asset's ID, 3, a colour (as the library's own).
    const uint32_t record = records + i * kRecordBytes;
    const auto& entry = g.catalog.pack().entries()[part.index];
    Store<uint32_t>(record, entry.word0);
    Store<uint16_t>(record + 4, uint16_t(part.index));
    Store<uint16_t>(record + 6, uint16_t(part.gender));
    Store<uint32_t>(record + 8, 0xC1C8F109);
    Store<uint32_t>(record + 12, 0xA19CB2E0);
    Store<uint16_t>(record + 16, 3);
    Store<uint32_t>(record + 20, 0xFF008000);
    x360tool::build_avatar_part(model, std::span<uint8_t>(Ptr<uint8_t>(cpu), cpu_size), cpu,
                                std::span<uint8_t>(Ptr<uint8_t>(gpu + gpu_at), part_gpu), gpu + gpu_at,
                                std::span<uint8_t, 52>(Ptr<uint8_t>(descriptors + i * kDescriptorBytes), 52), tile);
    cpu = AlignUp(cpu + cpu_size, 16);
    gpu_at += part_gpu;
  }
  std::string names;
  for (const auto& part : parts) {
    if (!names.empty()) names += ", ";
    names += g.catalog.name(part.part->index);
  }
  KLOG_I("avatars: built %u parts (mask %X, %s, body %u, %u joints): %s; %u + %u bytes at %08X, %08X", n, mask,
         described ? "described" : "no description: default", d.body, Load<uint32_t>(skeleton), names.c_str(),
         cpu - result, gpu_at, result, gpu);
  return true;
}

}  // namespace x360::avatar
