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
#include <string_view>
#include <vector>

#include "avatar.h"
#include "avatar_model.h"
#include "util.h"
#include "x360/kernel.h"

namespace x360::avatar {

using namespace x360::kernel;

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kMaxParts = 13;  // Kinect Sports keeps headers for 13 parts
constexpr uint32_t kSlots = 13;     // component bits 0-12: head, body, hair, shirt, trousers, shoes,
                                    // hat, gloves, glasses, wristwear, earrings, ring, carryable
constexpr uint32_t kJoints = 72;    // room the library reserves (71 used)
constexpr uint32_t kJointBytes = 96;
constexpr uint32_t kRecordBytes = 32;
constexpr uint32_t kDescriptorBytes = 52;
constexpr uint32_t kNone = 0xFFFFFFFF;

// A description (XAVATAR_METADATA, big-endian), as titles pass them (Kinect Sports' guests) and as
// this system writes them:
//   +4 height and +8 weight, -1 to 1 (the avatar library reads them too);
//   +12, +28, +44 the head's chin, nose and ears (shape overrides): asset IDs;
//   +60 six face textures (mouth, eyes, eyebrows, facial hair, ...), 32 bytes each: an asset ID and
//       16 bytes (1.0 or 0);
//   +252 nine colours (ARGB): skin, hair, lips, eyes, eyebrows, eye shadow, facial hair and two for
//       face paint;
//   +288 fifteen parts, 32 bytes each: an asset ID, the component bits it covers (16 bits), 0, and
//       12 bytes this system uses for the part's three colours (0: the asset's own).
// An asset ID is the asset's category bits (the pack entry's first word), its number (the pack
// entry), its gender (1 male, 2 female, 3 both) and the same 8 bytes for every stock asset.
constexpr uint32_t kHeight = 4, kWeight = 8, kFeatures = 12, kFaceTextures = 60, kColors = 252, kParts = 288;
constexpr uint32_t kFeatureCount = 3, kFaceTextureCount = 6, kColorCount = 9, kPartRecords = 15;
constexpr uint32_t kIdTailHigh = 0xC1C8F109, kIdTailLow = 0xA19CB2E0;

// The colours, which are also the head's colour constants 13-21 in this order.
enum Color { kSkin, kHair, kLips, kEyes, kEyebrows, kEyeShadow, kFacialHair, kFacePaint1, kFacePaint2 };

// Chin, nose, ears.
constexpr uint32_t kFeatureKinds[kFeatureCount] = {0x100000, 0x80000, 0x200000};
// The face textures in a description's order: mouth, eyes, eyebrows, facial hair, eye shadow, face
// paint.
constexpr uint32_t kFaceTextureKinds[kFaceTextureCount] = {0x8000, 0x2000, 0x4000, 0x10000, 0x40000, 0x20000};
// The head's six blank textures, and the face textures that fill them: face paint (128x128), facial
// hair (128x64), eyebrows (5 frames, both sides), eyes (14 frames, both sides), mouth (14 frames), eye
// shadow (128x64). Each texture's UV set puts it in its place (the beard on the jaw, eye shadow at
// eye level).
constexpr uint32_t kHeadTextureKinds[kFaceTextureCount] = {0x20000, 0x10000, 0x4000, 0x2000, 0x8000, 0x40000};

struct Part {
  uint32_t index = 0;   // pack entry
  uint32_t slots = 0;   // component bits it occupies (for face textures and features, their kind)
  uint32_t slot = 0;    // its own (lowest) bit
  uint32_t gender = 0;  // 1 male, 2 female, 3 both
  uint32_t cpu = 0, gpu = 0;
  uint32_t meshes = 0;
  std::vector<uint8_t> table;  // its colour table, if any
  // Hairstyles: the second byte of the entry's second word. Every hairstyle's metadata says "both
  // genders", but this byte groups them: 1 partings, crops and spikes, 2 bobs, buns, ponytails
  // and plaits, 0 and 3 either.
  uint32_t style = 0;
};

// The default look's hairstyles (the pack's English names), per body type.
const std::u16string_view kDefaultHair[3] = {u"", u"Crew Cut", u"Shoulder Length"};

// What a description says, decoded.
struct Description {
  uint32_t body = 1;
  float height = 0.0f, weight = 0.0f;
  uint32_t colors[kColorCount] = {0xFFE0AC86, 0xFF2B1D14, 0xFFB86A68, 0xFF5B3A1E, 0xFF2B1D14,
                                  0xFF000000, 0xFF2B1D14, 0xFF000000, 0xFF000000};
  uint32_t features[kFeatureCount] = {kNone, kNone, kNone};
  uint32_t face[kFaceTextureCount] = {kNone, kNone, kNone, kNone, kNone, kNone};  // by kFaceTextureKinds
  struct Worn {
    uint32_t index = 0;
    uint32_t colors[3] = {};  // 0: the asset's own
  };
  std::vector<Worn> parts;
};

struct State {
  std::once_flag once;
  bool loaded = false;
  std::vector<uint8_t> toc;
  std::unique_ptr<x360tool::AvatarPack> pack;
  std::vector<Part> parts;
  std::vector<Part> extras;  // face textures and features: slot is the kind
  uint32_t max_cpu[kSlots] = {}, max_gpu[kSlots] = {};
  bool left_handed = false;
} g;
std::mutex g_mutex;

uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

uint32_t LowestBit(uint32_t v) { return v & (~v + 1); }

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

// A colour table (little-endian): a count, then per entry (a colourway the player can pick) three
// colours, each BGRA and a word.
constexpr uint32_t kColorwayBytes = 24;

uint32_t Colorways(std::span<const uint8_t> table) {
  if (table.size() < 4) return 0;
  const uint32_t count = uint32_t(table[0]) | uint32_t(table[1]) << 8 | uint32_t(table[2]) << 16 |
                         uint32_t(table[3]) << 24;
  return std::min<uint32_t>(count, uint32_t((table.size() - 4) / kColorwayBytes));
}

uint32_t ColorwayColor(std::span<const uint8_t> table, uint32_t colorway, uint32_t k) {
  const uint8_t* c = table.data() + 4 + colorway * kColorwayBytes + k * 8;
  return uint32_t(c[0]) | uint32_t(c[1]) << 8 | uint32_t(c[2]) << 16 | uint32_t(c[3]) << 24;
}

bool IsExtraKind(uint32_t kind) {
  return std::find(std::begin(kFaceTextureKinds), std::end(kFaceTextureKinds), kind) != std::end(kFaceTextureKinds) ||
         std::find(std::begin(kFeatureKinds), std::end(kFeatureKinds), kind) != std::end(kFeatureKinds);
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
    const bool extra = IsExtraKind(e.word0);
    if (e.size == 0 || (slots == 0 && !extra)) continue;
    auto data = g.pack->data(e);
    x360tool::Strb strb;
    std::string error;
    if (!x360tool::parse_strb(data, strb, error)) continue;
    x360tool::AvatarAssetMetadata meta, block_meta;
    for (const auto& block : strb.blocks) {
      if (x360tool::parse_avatar_metadata(data.subspan(block.offset, block.length), block.id, block_meta))
        meta = block_meta;
    }
    if (extra) {
      // Face textures (type 2) and shape overrides (type 3).
      if (meta.type == 2 || meta.type == 3) {
        Part part;
        part.index = e.index;
        part.slots = part.slot = e.word0;
        part.gender = meta.gender;
        g.extras.push_back(std::move(part));
      }
      continue;
    }
    if (meta.type != 1 || slots == 0) continue;
    std::vector<uint8_t> model, colors;
    x360tool::AvatarModel parsed;
    if (!LoadPart(e, model, colors, error) || !x360tool::parse_avatar_model(model, parsed, error)) continue;
    Part part;
    part.index = e.index;
    part.slots = slots;
    part.slot = LowestBit(slots);
    part.gender = meta.gender;
    part.cpu = parsed.cpu_size();
    part.gpu = parsed.gpu_size();
    part.meshes = uint32_t(parsed.meshes.size());
    part.table = std::move(colors);
    part.style = (e.word1 >> 16) & 0xFF;
    const int bit = std::countr_zero(part.slot);
    g.max_cpu[bit] = std::max(g.max_cpu[bit], part.cpu);
    g.max_gpu[bit] = std::max(g.max_gpu[bit], AlignUp(part.gpu, 0x1000));
    g.parts.push_back(std::move(part));
  }
  g.loaded = !g.parts.empty();
  KLOG_I("avatars: %zu parts and %zu face textures and features in %s", g.parts.size(), g.extras.size(),
         toc.string().c_str());
}

void EnsureLoaded() {
  std::call_once(g.once, LoadPack);
}

const Part* FindPart(uint32_t index) {
  auto it = std::find_if(g.parts.begin(), g.parts.end(), [&](const Part& p) { return p.index == index; });
  return it != g.parts.end() ? &*it : nullptr;
}

const Part* Choose(uint32_t slot, uint32_t gender, std::mt19937* rng) {
  std::vector<const Part*> candidates;
  for (const Part& p : g.parts) {
    if (p.slot != slot || (p.gender != gender && p.gender != 3)) continue;
    if (slot == 0x4 && p.style != gender && p.style != 0 && p.style != 3) continue;
    candidates.push_back(&p);
  }
  if (candidates.empty()) return nullptr;
  if (rng) return candidates[(*rng)() % candidates.size()];
  if (slot == 0x4) {
    for (const Part* p : candidates)
      if (g.pack->entries()[p->index].name == kDefaultHair[gender == 2 ? 2 : 1]) return p;
  }
  // The plainest: the first that occupies only its own slot and has something to draw (not "Bald").
  for (const Part* p : candidates)
    if (p->slots == slot && p->meshes) return p;
  return candidates.front();
}

const Part* ChooseExtra(uint32_t kind, uint32_t gender, std::mt19937* rng) {
  std::vector<const Part*> candidates;
  for (const Part& p : g.extras) {
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

// An asset ID (16 bytes) for a pack entry, and back: the entry, if the ID names one of the pack's
// assets (kNone otherwise).
void StoreAssetId(uint32_t at, uint32_t index, uint32_t gender) {
  Store<uint32_t>(at, g.pack->entries()[index].word0);
  Store<uint16_t>(at + 4, uint16_t(index));
  Store<uint16_t>(at + 6, uint16_t(gender));
  Store<uint32_t>(at + 8, kIdTailHigh);
  Store<uint32_t>(at + 12, kIdTailLow);
}

uint32_t LoadAssetId(uint32_t at) {
  if (Load<uint32_t>(at + 8) != kIdTailHigh || Load<uint32_t>(at + 12) != kIdTailLow) return kNone;
  const uint32_t index = Load<uint16_t>(at + 4);
  if (index >= g.pack->entries().size() || g.pack->entries()[index].word0 != Load<uint32_t>(at)) return kNone;
  return index;
}

// The description's parts and features; false if it names no part this system knows (not a
// description, or not one of the pack's assets).
bool ReadDescription(uint32_t metadata, Description& d) {
  if (!metadata) return false;
  d.height = std::clamp(LoadFloat(metadata + kHeight), -1.0f, 1.0f);
  d.weight = std::clamp(LoadFloat(metadata + kWeight), -1.0f, 1.0f);
  for (uint32_t k = 0; k < kFeatureCount; ++k) d.features[k] = LoadAssetId(metadata + kFeatures + 16 * k);
  for (uint32_t k = 0; k < kFaceTextureCount; ++k) {
    const uint32_t index = LoadAssetId(metadata + kFaceTextures + kRecordBytes * k);
    if (index == kNone) continue;
    const uint32_t kind = g.pack->entries()[index].word0;
    for (uint32_t s = 0; s < kFaceTextureCount; ++s)
      if (kFaceTextureKinds[s] == kind) d.face[s] = index;
  }
  for (uint32_t k = 0; k < kColorCount; ++k) d.colors[k] = Load<uint32_t>(metadata + kColors + 4 * k);
  d.parts.clear();
  for (uint32_t k = 0; k < kPartRecords; ++k) {
    const uint32_t at = metadata + kParts + kRecordBytes * k;
    const uint32_t index = LoadAssetId(at);
    const Part* p = index != kNone ? FindPart(index) : nullptr;
    if (!p) continue;
    Description::Worn worn;
    worn.index = index;
    for (int c = 0; c < 3; ++c) worn.colors[c] = Load<uint32_t>(at + 20 + 4 * c);
    d.parts.push_back(worn);
    if (p->slot == 0x2) d.body = p->gender == 2 ? 2 : 1;
  }
  return !d.parts.empty();
}

void WriteDescription(uint32_t metadata, const Description& d) {
  std::memset(Ptr<uint8_t>(metadata), 0, kMetadataSize);
  StoreFloat(metadata + kHeight, d.height);
  StoreFloat(metadata + kWeight, d.weight);
  const auto gender = [](uint32_t index, const std::vector<Part>& list) {
    auto it = std::find_if(list.begin(), list.end(), [&](const Part& p) { return p.index == index; });
    return it != list.end() ? it->gender : 3;
  };
  for (uint32_t k = 0; k < kFeatureCount; ++k)
    if (d.features[k] != kNone) StoreAssetId(metadata + kFeatures + 16 * k, d.features[k], gender(d.features[k], g.extras));
  for (uint32_t k = 0; k < kFaceTextureCount; ++k)
    if (d.face[k] != kNone) StoreAssetId(metadata + kFaceTextures + kRecordBytes * k, d.face[k], gender(d.face[k], g.extras));
  for (uint32_t k = 0; k < kColorCount; ++k) Store<uint32_t>(metadata + kColors + 4 * k, d.colors[k]);
  for (size_t k = 0; k < d.parts.size() && k < kPartRecords; ++k) {
    const uint32_t at = metadata + kParts + kRecordBytes * uint32_t(k);
    const Part* p = FindPart(d.parts[k].index);
    if (!p) continue;
    StoreAssetId(at, p->index, p->gender);
    Store<uint16_t>(at + 16, uint16_t(p->slots));
    for (int c = 0; c < 3; ++c) Store<uint32_t>(at + 20 + 4 * c, d.parts[k].colors[c]);
  }
}

// The default male or female avatar, or a random one.
Description MakeDescription(Look look, uint32_t seed) {
  Description d;
  std::mt19937 rng(seed);
  const bool random = look == Look::kRandom;
  std::mt19937* pick = random ? &rng : nullptr;
  d.body = look == Look::kFemale ? 2 : random ? 1 + rng() % 2 : 1;
  if (random) {
    d.height = (int(rng() % 11) - 5) / 10.0f;
    d.weight = (int(rng() % 11) - 5) / 10.0f;
    static const uint32_t kSkins[] = {0xFFF2C9A6, 0xFFE0AC86, 0xFFC68A64, 0xFF8D5A3B, 0xFF5C3A24};
    static const uint32_t kHairs[] = {0xFF2B1D14, 0xFF5A3A1E, 0xFFA87A3C, 0xFF151515, 0xFF8A2E1A};
    static const uint32_t kEyeColors[] = {0xFF3A6EA5, 0xFF5B3A1E, 0xFF4E7A3A, 0xFF6B6B6B};
    d.colors[kSkin] = kSkins[rng() % 5];
    d.colors[kHair] = d.colors[kEyebrows] = d.colors[kFacialHair] = kHairs[rng() % 5];
    d.colors[kEyes] = kEyeColors[rng() % 4];
    d.colors[kEyeShadow] = d.body == 2 ? 0xFF6A4A7A : 0xFF000000;
  }
  for (uint32_t k = 0; k < kFeatureCount; ++k)
    if (const Part* p = ChooseExtra(kFeatureKinds[k], d.body, pick)) d.features[k] = p->index;
  // Eyes, eyebrows and a mouth; a random avatar may also get facial hair or eye shadow.
  for (uint32_t k = 0; k < kFaceTextureCount; ++k) {
    const uint32_t kind = kFaceTextureKinds[k];
    const bool always = kind == 0x2000 || kind == 0x4000 || kind == 0x8000;
    const bool maybe = random && ((kind == 0x10000 && d.body == 1) || (kind == 0x40000 && d.body == 2)) && rng() % 3 == 0;
    if (const Part* p = (always || maybe) ? ChooseExtra(kind, d.body, pick) : nullptr) d.face[k] = p->index;
  }
  // Head, body, hair, shirt, trousers, shoes; a random avatar's in one of their colourways.
  for (uint32_t slot : {0x1u, 0x2u, 0x4u, 0x8u, 0x10u, 0x20u}) {
    const Part* p = Choose(slot, d.body, pick);
    if (!p) continue;
    Description::Worn worn;
    worn.index = p->index;
    if (const uint32_t count = random ? Colorways(p->table) : 0) {
      const uint32_t colorway = rng() % count;
      for (uint32_t c = 0; c < 3; ++c) worn.colors[c] = ColorwayColor(p->table, colorway, c);
    }
    d.parts.push_back(worn);
  }
  return d;
}

// An ARGB colour as the float4 (r, g, b, a) the shaders take.
void ColorValues(uint32_t argb, uint32_t values[4]) {
  values[0] = FloatBits(((argb >> 16) & 0xFF) / 255.0f);
  values[1] = FloatBits(((argb >> 8) & 0xFF) / 255.0f);
  values[2] = FloatBits((argb & 0xFF) / 255.0f);
  values[3] = FloatBits(((argb >> 24) & 0xFF) / 255.0f);
}

// The colours the system fills in (the pack leaves them black), as Kinect Sports' avatar shaders
// use them (constants assigned at 8259D2F0). A part's second texture is a mask whose red, green and
// blue pick colours 22, 23 and 24 and whose alpha lays them over its first texture: the body's mask
// is all red, for the skin, the hair's for the hair colour, clothes take the description's colours
// or their colour table's first colourway. The head's six textures each blend their own colour over
// the skin: 13-21 are the description's nine colours. 25 and 26 are the asset's own, 27 a rim light
// (black in every asset) is left as it is.
void ApplyColors(x360tool::AvatarModel& model, const Part& part, const uint32_t worn[3],
                 const uint32_t colors[kColorCount]) {
  uint32_t own[3] = {0xFF000000, 0xFF000000, 0xFF000000};
  if (part.slot == 0x2) {
    own[0] = own[1] = own[2] = colors[kSkin];
  } else if (part.slot == 0x4) {
    own[0] = own[1] = own[2] = colors[kHair];
  } else if (worn[0] || worn[1] || worn[2]) {
    for (int k = 0; k < 3; ++k) own[k] = worn[k];
  } else if (Colorways(part.table)) {
    for (uint32_t k = 0; k < 3; ++k) own[k] = ColorwayColor(part.table, 0, k);
  }
  for (auto& mesh : model.meshes) {
    for (auto& p : mesh.params) {
      if (p.kind == 1) continue;
      if (p.id >= 22 && p.id <= 24) {
        ColorValues(own[p.id - 22], p.values);
      } else if (p.id >= 13 && p.id <= 21) {
        ColorValues(colors[p.id - 13], p.values);
      }
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
  if (!Available()) return;
  std::lock_guard lock(g_mutex);
  WriteDescription(metadata, MakeDescription(look, seed));
}

uint32_t BodyType(uint32_t metadata) {
  if (!Available()) return 1;
  std::lock_guard lock(g_mutex);
  Description d;
  return ReadDescription(metadata, d) ? d.body : 1;
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
  Description d;
  const bool described = ReadDescription(metadata, d);
  // Not a description of the pack's assets: the default avatar.
  if (!described) d = MakeDescription(Look::kMale, 0);

  // The parts in the mask, in component order: Kinect Sports expects the head first and the body
  // second. Parts with nothing to draw ("Bald") stay out: it stops drawing an avatar at the first
  // part without indices (8259E750).
  std::vector<std::pair<const Part*, const Description::Worn*>> parts;
  for (const auto& worn : d.parts) {
    const Part* p = FindPart(worn.index);
    if (p && p->meshes && (p->slot & mask) && parts.size() < kMaxParts) parts.emplace_back(p, &worn);
  }
  std::stable_sort(parts.begin(), parts.end(), [](const auto& a, const auto& b) { return a.first->slot < b.first->slot; });
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
    const Part& part = *parts[i].first;
    const auto& entry = g.pack->entries()[part.index];
    std::vector<uint8_t> file, table;
    x360tool::AvatarModel model;
    if (!LoadPart(entry, file, table, error) || !x360tool::parse_avatar_model(file, model, error, g.left_handed)) {
      error = "part " + std::to_string(entry.index) + ": " + error;
      return false;
    }
    ApplyColors(model, part, parts[i].second->colors, d.colors);
    const uint32_t cpu_size = model.cpu_size(), part_gpu = AlignUp(model.gpu_size(), 0x1000);
    if (cpu + cpu_size > result + result_size || gpu_at + part_gpu > gpu_size) {
      error = "buffers too small";
      return false;
    }
    // The asset record: the asset's ID, 3, a colour (as the library's own).
    const uint32_t record = records + i * kRecordBytes;
    StoreAssetId(record, part.index, part.gender);
    Store<uint16_t>(record + 16, 3);
    Store<uint32_t>(record + 20, 0xFF008000);
    x360tool::build_avatar_part(model, std::span<uint8_t>(Ptr<uint8_t>(cpu), cpu_size), cpu,
                                std::span<uint8_t>(Ptr<uint8_t>(gpu + gpu_at), part_gpu), gpu + gpu_at,
                                std::span<uint8_t, 52>(Ptr<uint8_t>(descriptors + i * kDescriptorBytes), 52), tile);
    // The head's face textures, copied into its blank texture slots.
    if (part.slot == 0x1) {
      for (size_t k = 0; k < model.textures.size() && k < kFaceTextureCount; ++k) {
        const auto& slot = model.textures[k];
        uint32_t index = kNone;
        for (uint32_t s = 0; s < kFaceTextureCount; ++s)
          if (kFaceTextureKinds[s] == kHeadTextureKinds[k]) index = d.face[s];
        if (index == kNone || !slot.blank) continue;
        std::vector<uint8_t> texture_file;
        x360tool::AvatarTexture texture;
        std::string texture_error;
        if (!LoadTexture(g.pack->entries()[index], texture_file, texture, texture_error)) {
          KLOG_W("avatars: face texture %u: %s", index, texture_error.c_str());
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
  std::string names;
  for (const auto& [part, worn] : parts) {
    if (!names.empty()) names += ", ";
    for (char16_t c : g.pack->entries()[part->index].name) names += c < 0x80 ? char(c) : '?';
  }
  KLOG_I("avatars: built %u parts (mask %X, %s, body %u, %u joints): %s; %u + %u bytes", n, mask,
         described ? "described" : "no description: default", d.body, Load<uint32_t>(skeleton), names.c_str(),
         cpu - result, gpu_at);
  return true;
}

}  // namespace x360::avatar
