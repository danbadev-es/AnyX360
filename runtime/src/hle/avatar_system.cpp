// The avatar system (see avatar_system.h and docs/AVATARS.md).
//
// The result buffer is laid out as the avatar library's own builder lays out its built-in avatar
// (Kinect Sports 82A84B80): a 20-byte header {skeleton*, 0, part count, asset records*, part
// descriptors*}, the skeleton (count, joints*, then 72 joints of 96 bytes, filled by the title's
// skeleton builder), the 32-byte asset records, the 52-byte part descriptors and each part's CPU
// block; the GPU buffer holds each part's textures, vertices and indices, 4 KiB aligned. Pointers
// are absolute guest addresses.
#include "avatar_system.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <random>
#include <vector>

#include "avatar.h"
#include "avatar_model.h"
#include "stfs.h"
#include "util.h"
#include "x360/kernel.h"

namespace x360::avatar {

using namespace x360::kernel;

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kMagic = 0x41584156;  // 'AXAV': a description this system wrote
constexpr uint32_t kMaxParts = 13;       // Kinect Sports keeps headers for 13 parts
constexpr uint32_t kSlots = 13;          // component bits 0-12: head, body, hair, shirt, trousers,
                                         // shoes, hat, gloves, glasses, wristwear, earrings, ring,
                                         // carryable
constexpr uint32_t kJoints = 72;         // room the library reserves (71 used)
constexpr uint32_t kJointBytes = 96;
constexpr uint32_t kRecordBytes = 32;
constexpr uint32_t kDescriptorBytes = 52;

// Our description (big-endian, inside the 1000 bytes): the library itself reads height at +4 and
// weight at +8.
constexpr uint32_t kHeight = 4, kWeight = 8, kBodyType = 12, kPartCount = 16, kParts = 20;
constexpr uint32_t kColors = 84;     // skin, hair, lips, eyes, eyebrows (ARGB)
constexpr uint32_t kTextures = 104;  // the face's textures, one per head texture slot (pack entries)
constexpr uint32_t kNone = 0xFFFFFFFF;

// The head's six blank textures and the pack textures that fill them (the asset's first word): face
// paint (128x128), eye shadow, eyebrows (5 frames, both sides), eyes (14 frames, both sides), mouth
// (14 frames), facial hair.
constexpr uint32_t kFaceKinds[6] = {0x20000, 0x40000, 0x4000, 0x2000, 0x8000, 0x10000};

struct Part {
  uint32_t index = 0;   // pack entry
  uint32_t slots = 0;   // component bits it occupies
  uint32_t slot = 0;    // its own (lowest) bit
  uint32_t gender = 0;  // 1 male, 2 female, 3 both
  uint32_t cpu = 0, gpu = 0;
  uint32_t meshes = 0;
};

struct State {
  std::once_flag once;
  bool loaded = false;
  std::vector<uint8_t> toc;
  std::unique_ptr<x360tool::AvatarPack> pack;
  std::vector<Part> parts;
  std::vector<Part> textures;  // face textures: slot is the kind (kFaceKinds)
  uint32_t max_cpu[kSlots] = {}, max_gpu[kSlots] = {};
  bool left_handed = false;
} g;
std::mutex g_mutex;

uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

uint32_t LowestBit(uint32_t v) { return v & (~v + 1); }

fs::path SystemFolder() { return TitlePaths().user_root.parent_path() / "system"; }

// The pack's table: the shared copy, or the game's own package (copied to the shared folder for
// avatar games that don't carry one).
bool FindPack(fs::path& toc) {
  toc = SystemFolder() / "AvatarAssetPack.toc";
  std::error_code ec;
  if (fs::exists(toc, ec)) return true;
  const fs::path package = TitlePaths().game_root / "AvatarAssetPack";
  if (!fs::exists(package, ec)) return false;
  try {
    x360tool::StfsPackage stfs(package);
    for (const auto& entry : stfs.entries()) {
      if (entry.is_dir || entry.path != "AvatarAssetPack.toc") continue;
      fs::create_directories(toc.parent_path(), ec);
      if (stfs.extract(entry, toc)) {
        KLOG_I("avatars: copied the disc's avatar asset pack to %s", toc.string().c_str());
        return true;
      }
    }
  } catch (const std::exception& e) {
    KLOG_W("avatars: %s: %s", package.string().c_str(), e.what());
  }
  return false;
}

// The part's model file (unpacked) and its colour table, if any.
bool LoadPart(const x360tool::AvatarPackEntry& e, std::vector<uint8_t>& model, std::vector<uint8_t>& colors,
              std::string& error) {
  auto data = g.pack->data(e);
  x360tool::Strb strb;
  if (!x360tool::parse_strb(data, strb, error)) return false;
  bool found = false;
  for (const auto& block : strb.blocks) {
    auto bytes = data.subspan(block.offset, block.length);
    if (block.id == uint32_t(x360tool::StrbBlockId::kModel)) {
      uint32_t window = 0;
      if (x360tool::is_avatar_packed(bytes)) {
        if (!x360tool::avatar_unpack(bytes, model, window, error)) return false;
      } else {
        model.assign(bytes.begin(), bytes.end());
      }
      found = true;
    } else if (block.id == uint32_t(x360tool::StrbBlockId::kCustomColorTable)) {
      colors.assign(bytes.begin(), bytes.end());
    }
  }
  if (!found) error = "no model";
  return found;
}

void LoadPack() {
  fs::path toc;
  if (!FindPack(toc)) {
    KLOG_W("avatars: no avatar asset pack (it comes with discs of avatar games, e.g. Kinect Sports)");
    return;
  }
  try {
    g.toc = x360tool::read_file(toc);
    g.pack = std::make_unique<x360tool::AvatarPack>(g.toc);
  } catch (const std::exception& e) {
    KLOG_W("avatars: %s: %s", toc.string().c_str(), e.what());
    return;
  }
  for (const auto& e : g.pack->entries()) {
    const uint32_t slots = e.word0 & ((1u << kSlots) - 1);
    const bool face = std::find(std::begin(kFaceKinds), std::end(kFaceKinds), e.word0) != std::end(kFaceKinds);
    if (e.size == 0 || (slots == 0 && !face)) continue;
    auto data = g.pack->data(e);
    x360tool::Strb strb;
    std::string error;
    if (!x360tool::parse_strb(data, strb, error)) continue;
    x360tool::AvatarAssetMetadata meta, block_meta;
    for (const auto& block : strb.blocks) {
      if (x360tool::parse_avatar_metadata(data.subspan(block.offset, block.length), block.id, block_meta))
        meta = block_meta;
    }
    if (face && meta.type == 2) {
      g.textures.push_back(Part{e.index, e.word0, e.word0, meta.gender});
      continue;
    }
    const bool component = meta.type == 1;
    if (!component || slots == 0) continue;
    std::vector<uint8_t> model, colors;
    x360tool::AvatarModel parsed;
    if (!LoadPart(e, model, colors, error) || !x360tool::parse_avatar_model(model, parsed, error)) continue;
    Part part{e.index, slots, LowestBit(slots), meta.gender, parsed.cpu_size(), parsed.gpu_size(),
              uint32_t(parsed.meshes.size())};
    const int bit = std::countr_zero(part.slot);
    g.max_cpu[bit] = std::max(g.max_cpu[bit], part.cpu);
    g.max_gpu[bit] = std::max(g.max_gpu[bit], AlignUp(part.gpu, 0x1000));
    g.parts.push_back(part);
  }
  g.loaded = !g.parts.empty();
  KLOG_I("avatars: %zu parts and %zu face textures in %s", g.parts.size(), g.textures.size(), toc.string().c_str());
}

void EnsureLoaded() {
  std::call_once(g.once, LoadPack);
}

const Part* Choose(uint32_t slot, uint32_t gender, std::mt19937* rng) {
  std::vector<const Part*> candidates;
  for (const Part& p : g.parts) {
    if (p.slot == slot && (p.gender == gender || p.gender == 3)) candidates.push_back(&p);
  }
  if (candidates.empty()) return nullptr;
  if (rng) return candidates[(*rng)() % candidates.size()];
  // The plainest: the first that occupies only its own slot and has something to draw (not "Bald").
  for (const Part* p : candidates)
    if (p->slots == slot && p->meshes) return p;
  return candidates.front();
}

const Part* ChooseTexture(uint32_t kind, uint32_t gender, std::mt19937* rng) {
  std::vector<const Part*> candidates;
  for (const Part& p : g.textures) {
    if (p.slot == kind && (p.gender == gender || p.gender == 3 || p.gender == 0)) candidates.push_back(&p);
  }
  if (candidates.empty()) return nullptr;
  return rng ? candidates[(*rng)() % candidates.size()] : candidates.front();
}

// A face texture from the pack (its texture block, unpacked).
bool LoadTexture(const x360tool::AvatarPackEntry& e, std::vector<uint8_t>& file, x360tool::AvatarTexture& texture,
                 std::string& error) {
  auto data = g.pack->data(e);
  x360tool::Strb strb;
  if (!x360tool::parse_strb(data, strb, error)) return false;
  for (const auto& block : strb.blocks) {
    if (block.id != uint32_t(x360tool::StrbBlockId::kTexture)) continue;
    auto bytes = data.subspan(block.offset, block.length);
    uint32_t window = 0;
    if (x360tool::is_avatar_packed(bytes)) {
      if (!x360tool::avatar_unpack(bytes, file, window, error)) return false;
    } else {
      file.assign(bytes.begin(), bytes.end());
    }
    return x360tool::parse_avatar_texture(file, texture, error);
  }
  error = "no texture";
  return false;
}

float LoadFloat(uint32_t address) {
  const uint32_t bits = Load<uint32_t>(address);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
void StoreFloat(uint32_t address, float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  Store<uint32_t>(address, bits);
}

uint32_t FloatBits(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  return bits;
}

// An ARGB colour as the float4 (r, g, b, a) the shaders take.
void ColorValues(uint32_t argb, uint32_t values[4]) {
  values[0] = FloatBits(((argb >> 16) & 0xFF) / 255.0f);
  values[1] = FloatBits(((argb >> 8) & 0xFF) / 255.0f);
  values[2] = FloatBits((argb & 0xFF) / 255.0f);
  values[3] = FloatBits(((argb >> 24) & 0xFF) / 255.0f);
}

// Colour constants: 22-24 the part's own colours (its colour table), 27 the skin; the head's 13-21
// skin, hair, lips, eyes, eyebrows... (to be confirmed against the console).
void ApplyColors(x360tool::AvatarModel& model, std::span<const uint8_t> table, const uint32_t colors[5]) {
  uint32_t own[3] = {0xFF000000, 0xFF000000, 0xFF000000};
  if (table.size() >= 4 + 24) {
    for (int k = 0; k < 3; ++k)
      own[k] = uint32_t(table[4 + 8 * k]) | uint32_t(table[5 + 8 * k]) << 8 | uint32_t(table[6 + 8 * k]) << 16 |
               uint32_t(table[7 + 8 * k]) << 24;
  }
  for (auto& mesh : model.meshes) {
    for (auto& p : mesh.params) {
      if (p.kind == 1) continue;
      if (p.id >= 22 && p.id <= 24) ColorValues(own[p.id - 22], p.values);
      else if (p.id == 27 || p.id == 13) ColorValues(colors[0], p.values);
      else if (p.id >= 14 && p.id <= 17) ColorValues(colors[p.id - 13], p.values);
      else if (p.id >= 18 && p.id <= 21) ColorValues(colors[0], p.values);
    }
  }
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
  Store<uint32_t>(metadata, kMagic);
  std::mt19937 rng(seed);
  const bool random = look == Look::kRandom;
  const uint32_t body = look == Look::kFemale ? 2 : random ? 1 + rng() % 2 : 1;
  StoreFloat(metadata + kHeight, random ? 0.3f + (rng() % 41) / 100.0f : 0.5f);
  StoreFloat(metadata + kWeight, random ? 0.3f + (rng() % 41) / 100.0f : 0.5f);
  Store<uint32_t>(metadata + kBodyType, body);
  static const uint32_t kSkins[] = {0xFFF2C9A6, 0xFFE0AC86, 0xFFC68A64, 0xFF8D5A3B, 0xFF5C3A24};
  static const uint32_t kHairs[] = {0xFF2B1D14, 0xFF5A3A1E, 0xFFA87A3C, 0xFF151515, 0xFF8A2E1A};
  const uint32_t colors[5] = {random ? kSkins[rng() % 5] : kSkins[1], random ? kHairs[rng() % 5] : kHairs[0],
                              0xFFB86A68, 0xFF3A6EA5, random ? kHairs[rng() % 5] : kHairs[0]};
  for (int k = 0; k < 5; ++k) Store<uint32_t>(metadata + kColors + 4 * k, colors[k]);
  if (!Available()) return;
  std::lock_guard lock(g_mutex);
  // Head, body, hair, shirt, trousers, shoes.
  uint32_t count = 0;
  for (uint32_t slot : {0x1u, 0x2u, 0x4u, 0x8u, 0x10u, 0x20u}) {
    if (const Part* p = Choose(slot, body, random ? &rng : nullptr)) {
      Store<uint32_t>(metadata + kParts + 4 * count++, p->index);
    }
  }
  Store<uint32_t>(metadata + kPartCount, count);
  // The face: eyes, eyebrows and a mouth; a random avatar may also get facial hair or eye shadow.
  for (int k = 0; k < 6; ++k) {
    const uint32_t kind = kFaceKinds[k];
    const bool always = kind == 0x2000 || kind == 0x4000 || kind == 0x8000;
    const bool maybe = random && ((kind == 0x10000 && body == 1) || (kind == 0x40000 && body == 2)) && rng() % 3 == 0;
    const Part* t = (always || maybe) ? ChooseTexture(kind, body, random ? &rng : nullptr) : nullptr;
    Store<uint32_t>(metadata + kTextures + 4 * k, t ? t->index : kNone);
  }
}

uint32_t BodyType(uint32_t metadata) {
  if (!metadata || Load<uint32_t>(metadata) != kMagic) return 1;
  const uint32_t body = Load<uint32_t>(metadata + kBodyType);
  return body == 2 ? 2 : 1;
}

void ResultSizes(uint32_t mask, uint32_t& result, uint32_t& gpu) {
  result = gpu = 0;
  if (!Available()) return;
  uint32_t cpu = 0x40 + 16 + kJoints * kJointBytes + kMaxParts * (kRecordBytes + kDescriptorBytes);
  for (uint32_t bit = 0; bit < kSlots; ++bit) {
    if (!(mask & (1u << bit))) continue;
    cpu += AlignUp(g.max_cpu[bit], 16) + 16;
    gpu += g.max_gpu[bit];
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
  uint32_t body = 1, colors[5] = {0xFFE0AC86, 0xFF2B1D14, 0xFFB86A68, 0xFF3A6EA5, 0xFF2B1D14};
  float height = 0.5f, weight = 0.5f;
  uint32_t face[6] = {kNone, kNone, kNone, kNone, kNone, kNone};
  std::vector<uint32_t> indices;
  if (metadata && Load<uint32_t>(metadata) == kMagic) {
    body = BodyType(metadata);
    height = LoadFloat(metadata + kHeight);
    weight = LoadFloat(metadata + kWeight);
    const uint32_t count = std::min<uint32_t>(Load<uint32_t>(metadata + kPartCount), 16);
    for (uint32_t i = 0; i < count; ++i) indices.push_back(Load<uint32_t>(metadata + kParts + 4 * i));
    for (int k = 0; k < 5; ++k) colors[k] = Load<uint32_t>(metadata + kColors + 4 * k);
    for (int k = 0; k < 6; ++k) face[k] = Load<uint32_t>(metadata + kTextures + 4 * k);
  } else {
    // A description from elsewhere (the title's own, or none): the default avatar.
    for (uint32_t slot : {0x1u, 0x2u, 0x4u, 0x8u, 0x10u, 0x20u})
      if (const Part* p = Choose(slot, body, nullptr)) indices.push_back(p->index);
    for (int k = 0; k < 6; ++k) {
      const uint32_t kind = kFaceKinds[k];
      const Part* t = (kind == 0x2000 || kind == 0x4000 || kind == 0x8000) ? ChooseTexture(kind, body, nullptr) : nullptr;
      face[k] = t ? t->index : kNone;
    }
  }

  std::vector<const Part*> parts;
  for (uint32_t index : indices) {
    auto it = std::find_if(g.parts.begin(), g.parts.end(), [&](const Part& p) { return p.index == index; });
    if (it != g.parts.end() && (it->slot & mask) && parts.size() < kMaxParts) parts.push_back(&*it);
  }
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
  CallGuest(TitlePaths().avatar_skeleton, {skeleton, body}, {height, weight});

  const x360tool::AvatarTileFn tile = [](uint32_t x, uint32_t y, uint32_t pitch, uint32_t block_bytes) {
    return Tiled2D(x, y, pitch, block_bytes);
  };
  for (uint32_t i = 0; i < n; ++i) {
    const auto& entry = g.pack->entries()[parts[i]->index];
    std::vector<uint8_t> file, table;
    x360tool::AvatarModel model;
    if (!LoadPart(entry, file, table, error) || !x360tool::parse_avatar_model(file, model, error, g.left_handed)) {
      error = "part " + std::to_string(entry.index) + ": " + error;
      return false;
    }
    ApplyColors(model, table, colors);
    const uint32_t cpu_size = model.cpu_size(), part_gpu = AlignUp(model.gpu_size(), 0x1000);
    if (cpu + cpu_size > result + result_size || gpu_at + part_gpu > gpu_size) {
      error = "buffers too small";
      return false;
    }
    // The asset record: our ID ('AXAV', the pack entry), 3, a colour (as the library's own).
    const uint32_t record = records + i * kRecordBytes;
    Store<uint32_t>(record + 0, kMagic);
    Store<uint32_t>(record + 4, entry.index);
    Store<uint16_t>(record + 16, 3);
    Store<uint32_t>(record + 20, 0xFF008000);
    x360tool::build_avatar_part(model, std::span<uint8_t>(Ptr<uint8_t>(cpu), cpu_size), cpu,
                                std::span<uint8_t>(Ptr<uint8_t>(gpu + gpu_at), part_gpu), gpu + gpu_at,
                                std::span<uint8_t, 52>(Ptr<uint8_t>(descriptors + i * kDescriptorBytes), 52), tile);
    // The head's face textures, copied into its blank texture slots.
    if (parts[i]->slot == 0x1) {
      for (size_t k = 0; k < model.textures.size() && k < 6; ++k) {
        const auto& slot = model.textures[k];
        if (face[k] == kNone || face[k] >= g.pack->entries().size() || !slot.blank) continue;
        std::vector<uint8_t> texture_file;
        x360tool::AvatarTexture texture;
        std::string texture_error;
        if (!LoadTexture(g.pack->entries()[face[k]], texture_file, texture, texture_error)) {
          KLOG_W("avatars: face texture %u: %s", face[k], texture_error.c_str());
          continue;
        }
        if (texture.slice_count > slot.slice_count || texture.slice_size > slot.slice_size) continue;
        x360tool::write_avatar_texture(texture, std::span<uint8_t>(Ptr<uint8_t>(gpu + gpu_at + slot.gpu_offset), slot.size),
                                       tile);
      }
    }
    cpu = AlignUp(cpu + cpu_size, 16);
    gpu_at += part_gpu;
  }
  KLOG_I("avatars: built %u of %zu parts (mask %X, %s description, body %u, %u joints), %u + %u bytes", n,
         indices.size(), mask, metadata && Load<uint32_t>(metadata) == kMagic ? "our" : "another", body,
         Load<uint32_t>(skeleton), cpu - result, gpu_at);
  return true;
}

}  // namespace x360::avatar
