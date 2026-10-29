// Xbox Avatars as the system makes them (see avatar_look.h).
#include "avatar_look.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <iterator>

#include "x360/endian.h"

namespace x360tool {

namespace {

constexpr uint32_t kHeight = 4, kWeight = 8, kFeatures = 12, kFaceTextures = 60, kColors = 252, kParts = 288;
constexpr uint32_t kRecordBytes = 32;
constexpr uint32_t kIdTailHigh = 0xC1C8F109, kIdTailLow = 0xA19CB2E0;
constexpr uint32_t kColorwayBytes = 24;

// The head's six blank textures, and the face textures that fill them: face paint (128x128), facial
// hair (128x64), eyebrows (5 frames, both sides), eyes (14 frames, both sides), mouth (14 frames), eye
// shadow (128x64). Each texture's UV set puts it in its place (the beard on the jaw, eye shadow at
// eye level).
constexpr uint32_t kHeadTextureKinds[kAvatarFaceTextureCount] = {0x20000, 0x10000, 0x4000, 0x2000, 0x8000, 0x40000};

// The default look's hairstyles (the pack's English names), per body type.
const std::u16string_view kDefaultHair[3] = {u"", u"Crew Cut", u"Shoulder Length"};

uint32_t LowestBit(uint32_t v) { return v & (~v + 1); }

bool IsExtraKind(uint32_t kind) {
  return std::find(std::begin(kAvatarFaceTextureKinds), std::end(kAvatarFaceTextureKinds), kind) !=
             std::end(kAvatarFaceTextureKinds) ||
         std::find(std::begin(kAvatarFeatureKinds), std::end(kAvatarFeatureKinds), kind) != std::end(kAvatarFeatureKinds);
}

// A block of the entry's STRB file, unpacked if it is packed; false if it has none.
bool LoadBlock(const AvatarPack& pack, const AvatarPackEntry& e, StrbBlockId id, std::vector<uint8_t>& out,
               std::string& error) {
  auto data = pack.data(e);
  Strb strb;
  if (!parse_strb(data, strb, error)) return false;
  for (const auto& block : strb.blocks) {
    if (block.id != uint32_t(id)) continue;
    auto bytes = data.subspan(block.offset, block.length);
    uint32_t window = 0;
    if (is_avatar_packed(bytes)) {
      if (!avatar_unpack(bytes, out, window, error)) return false;
    } else {
      out.assign(bytes.begin(), bytes.end());
    }
    return true;
  }
  error = "no block " + std::to_string(uint32_t(id));
  return false;
}

// A shape override from the pack (its block 4): a face shape, or a template clothes link to.
bool LoadShape(const AvatarPack& pack, uint32_t index, bool left_handed, AvatarShapeOverride& shape, std::string& error) {
  if (index >= pack.entries().size()) {
    error = "no such entry";
    return false;
  }
  std::vector<uint8_t> block;
  if (!LoadBlock(pack, pack.entries()[index], StrbBlockId::kShapeOverrides, block, error)) return false;
  return parse_avatar_shape_override(block, shape, error, left_handed);
}

uint32_t FloatBits(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  return bits;
}
float BitsFloat(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

void StoreAssetId(const AvatarCatalog& catalog, uint8_t* at, uint32_t index, uint32_t gender) {
  x360::store_be<uint32_t>(at, catalog.pack().entries()[index].word0);
  x360::store_be<uint16_t>(at + 4, uint16_t(index));
  x360::store_be<uint16_t>(at + 6, uint16_t(gender));
  x360::store_be<uint32_t>(at + 8, kIdTailHigh);
  x360::store_be<uint32_t>(at + 12, kIdTailLow);
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
void ApplyColors(AvatarModel& model, const AvatarPart& part, const uint32_t worn[3],
                 const uint32_t colors[kAvatarColorCount]) {
  uint32_t own[3] = {0xFF000000, 0xFF000000, 0xFF000000};
  if (part.slot == kAvatarBody) {
    own[0] = own[1] = own[2] = colors[kAvatarSkin];
  } else if (part.slot == kAvatarHair) {
    own[0] = own[1] = own[2] = colors[kAvatarHairColor];
  } else if (worn[0] || worn[1] || worn[2]) {
    for (int k = 0; k < 3; ++k) own[k] = worn[k];
  } else if (part.colorways()) {
    for (uint32_t k = 0; k < 3; ++k) own[k] = part.colorway_color(0, k);
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

// What the system changes in a part for the rest of the avatar, as the console does: the body loses
// the triangles under the clothes worn and takes their shape (each component links to its hiding or
// body modification template in the pack's table), the head takes the description's chin, nose and
// ears.
void ApplyShapes(const AvatarCatalog& catalog, AvatarModel& model, const AvatarPart& part, const AvatarDescription& d,
                 bool left_handed, std::vector<std::string>* warnings) {
  const auto& entries = catalog.pack().entries();
  std::vector<uint32_t> shapes;
  if (part.slot == kAvatarBody) {
    for (const auto& worn : d.parts) {
      if (worn.index >= entries.size()) continue;
      const uint32_t linked = entries[worn.index].linked;
      if (linked < entries.size() && entries[linked].word0 == kAvatarTemplateKind) shapes.push_back(linked);
    }
  } else if (part.slot == kAvatarHead) {
    for (uint32_t feature : d.features)
      if (feature != kAvatarNone) shapes.push_back(feature);
  }
  for (uint32_t index : shapes) {
    AvatarShapeOverride shape;
    std::string error;
    if (!LoadShape(catalog.pack(), index, left_handed, shape, error)) {
      if (warnings) warnings->push_back("shape override " + std::to_string(index) + ": " + error);
      continue;
    }
    // Only those made for this part (the male or the female body, the head).
    if (shape.target != part.index || !shape.fits(model)) continue;
    apply_avatar_shape_override(model, shape);
  }
}

}  // namespace

uint32_t AvatarPart::colorways() const {
  if (table.size() < 4) return 0;
  const uint32_t count = uint32_t(table[0]) | uint32_t(table[1]) << 8 | uint32_t(table[2]) << 16 |
                         uint32_t(table[3]) << 24;
  return std::min<uint32_t>(count, uint32_t((table.size() - 4) / kColorwayBytes));
}

uint32_t AvatarPart::colorway_color(uint32_t colorway, uint32_t k) const {
  if (colorway >= colorways() || k >= 3) return 0;
  const uint8_t* c = table.data() + 4 + colorway * kColorwayBytes + k * 8;
  return uint32_t(c[0]) | uint32_t(c[1]) << 8 | uint32_t(c[2]) << 16 | uint32_t(c[3]) << 24;
}

bool AvatarCatalog::load(std::vector<uint8_t> toc, std::string& error) {
  parts_.clear();
  extras_.clear();
  std::fill(std::begin(max_cpu_), std::end(max_cpu_), 0);
  std::fill(std::begin(max_gpu_), std::end(max_gpu_), 0);
  try {
    pack_ = std::make_unique<AvatarPack>(std::move(toc));
  } catch (const std::exception& e) {
    pack_.reset();
    error = e.what();
    return false;
  }
  for (const auto& e : pack_->entries()) {
    const uint32_t slots = e.word0 & ((1u << kAvatarSlots) - 1);
    const bool extra = IsExtraKind(e.word0);
    if (e.size == 0 || (slots == 0 && !extra)) continue;
    auto data = pack_->data(e);
    Strb strb;
    std::string part_error;
    if (!parse_strb(data, strb, part_error)) continue;
    AvatarAssetMetadata meta, block_meta;
    for (const auto& block : strb.blocks) {
      if (parse_avatar_metadata(data.subspan(block.offset, block.length), block.id, block_meta)) meta = block_meta;
    }
    if (extra) {
      // Face textures (type 2) and shape overrides (type 3).
      if (meta.type == 2 || meta.type == 3) {
        AvatarPart part;
        part.index = e.index;
        part.slots = part.slot = e.word0;
        part.gender = meta.gender;
        extras_.push_back(std::move(part));
      }
      continue;
    }
    if (meta.type != 1 || slots == 0) continue;
    std::vector<uint8_t> model, colors;
    AvatarModel parsed;
    if (!LoadBlock(*pack_, e, StrbBlockId::kModel, model, part_error) ||
        !parse_avatar_model(model, parsed, part_error))
      continue;
    LoadBlock(*pack_, e, StrbBlockId::kCustomColorTable, colors, part_error);
    AvatarPart part;
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
    max_cpu_[bit] = std::max(max_cpu_[bit], part.cpu);
    max_gpu_[bit] = std::max(max_gpu_[bit], (part.gpu + 0xFFFu) & ~0xFFFu);
    parts_.push_back(std::move(part));
  }
  // Hairstyles link to their version for under a hat (e.g. "Peak" to "Peak (Hat)"): those are worn
  // with hats, not chosen on their own.
  for (AvatarPart& part : parts_) {
    const uint32_t linked = pack_->entries()[part.index].linked;
    if (part.slot != kAvatarHair || linked == part.index || linked >= pack_->entries().size()) continue;
    auto it = std::find_if(parts_.begin(), parts_.end(), [&](const AvatarPart& p) { return p.index == linked; });
    if (it == parts_.end() || it->slot != kAvatarHair) continue;
    part.hat_version = linked;
    it->for_hats = true;
  }
  if (parts_.empty()) {
    error = "no avatar parts in the pack";
    return false;
  }
  return true;
}

const AvatarPart* AvatarCatalog::find_part(uint32_t index) const {
  auto it = std::find_if(parts_.begin(), parts_.end(), [&](const AvatarPart& p) { return p.index == index; });
  return it != parts_.end() ? &*it : nullptr;
}

const AvatarPart* AvatarCatalog::find_extra(uint32_t index) const {
  auto it = std::find_if(extras_.begin(), extras_.end(), [&](const AvatarPart& p) { return p.index == index; });
  return it != extras_.end() ? &*it : nullptr;
}

namespace {

std::string Utf8(std::u16string_view text) {
  std::string out;
  for (char16_t c : text) {
    // UTF-8 (the pack's names are in the Basic Multilingual Plane).
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

}  // namespace

std::string AvatarCatalog::name(uint32_t index) const {
  if (!pack_ || index >= pack_->entries().size()) return {};
  return Utf8(pack_->entries()[index].name);
}

std::string AvatarCatalog::display_name(uint32_t index, uint32_t language) const {
  if (!pack_ || index >= pack_->entries().size()) return {};
  // The 13 offsets at +0xBC: the pack's own name, then XLanguage 1 (English) to 12.
  const std::span<const uint8_t> entry = pack_->raw_entry(index);
  const std::span<const uint8_t> toc = pack_->toc();
  auto read = [&](uint32_t k) {
    std::u16string text;
    if (k >= 13) return text;
    for (size_t at = x360::load_be<uint32_t>(&entry[0xBC + 4 * k]); at && at + 1 < toc.size(); at += 2) {
      const char16_t c = char16_t(x360::load_be<uint16_t>(&toc[at]));
      if (c == 0) break;
      text.push_back(c);
    }
    // A name that is only spaces is none (the pack's untranslated names).
    if (text.find_first_not_of(u' ') == std::u16string::npos) text.clear();
    return text;
  };
  std::u16string text = read(language);
  if (text.empty()) text = read(1);
  if (text.empty()) text = pack_->entries()[index].name;
  return Utf8(text);
}

uint32_t avatar_asset_entry(const AvatarCatalog& catalog, std::span<const uint8_t, 16> id) {
  if (x360::load_be<uint32_t>(&id[8]) != kIdTailHigh || x360::load_be<uint32_t>(&id[12]) != kIdTailLow) return kAvatarNone;
  const uint32_t index = x360::load_be<uint16_t>(&id[4]);
  const auto& entries = catalog.pack().entries();
  if (index >= entries.size() || entries[index].word0 != x360::load_be<uint32_t>(&id[0])) return kAvatarNone;
  return index;
}

bool read_avatar_description(const AvatarCatalog& catalog, std::span<const uint8_t> bytes, AvatarDescription& d) {
  if (!catalog.loaded() || bytes.size() < kAvatarDescriptionSize) return false;
  d = {};
  auto id = [&](uint32_t at) { return avatar_asset_entry(catalog, std::span<const uint8_t, 16>(bytes.data() + at, 16)); };
  const auto& entries = catalog.pack().entries();
  d.height = std::clamp(BitsFloat(x360::load_be<uint32_t>(&bytes[kHeight])), -1.0f, 1.0f);
  d.weight = std::clamp(BitsFloat(x360::load_be<uint32_t>(&bytes[kWeight])), -1.0f, 1.0f);
  if (!(d.height == d.height)) d.height = 0;  // NaN
  if (!(d.weight == d.weight)) d.weight = 0;
  for (uint32_t k = 0; k < kAvatarFeatureCount; ++k) {
    const uint32_t index = id(kFeatures + 16 * k);
    if (index != kAvatarNone && entries[index].word0 == kAvatarFeatureKinds[k]) d.features[k] = index;
  }
  for (uint32_t k = 0; k < kAvatarFaceTextureCount; ++k) {
    const uint32_t index = id(kFaceTextures + kRecordBytes * k);
    if (index == kAvatarNone) continue;
    const uint32_t kind = entries[index].word0;
    for (uint32_t s = 0; s < kAvatarFaceTextureCount; ++s)
      if (kAvatarFaceTextureKinds[s] == kind) d.face[s] = index;
  }
  for (uint32_t k = 0; k < kAvatarColorCount; ++k) d.colors[k] = x360::load_be<uint32_t>(&bytes[kColors + 4 * k]);
  for (uint32_t k = 0; k < kAvatarPartRecords; ++k) {
    const uint32_t at = kParts + kRecordBytes * k;
    const uint32_t index = id(at);
    const AvatarPart* p = index != kAvatarNone ? catalog.find_part(index) : nullptr;
    if (!p) continue;
    AvatarDescription::Worn worn;
    worn.index = index;
    for (int c = 0; c < 3; ++c) worn.colors[c] = x360::load_be<uint32_t>(&bytes[at + 20 + 4 * c]);
    d.parts.push_back(worn);
    if (p->slot == kAvatarBody) d.body = p->gender == 2 ? 2 : 1;
  }
  return !d.parts.empty();
}

void write_avatar_description(const AvatarCatalog& catalog, const AvatarDescription& d, std::span<uint8_t> bytes) {
  if (bytes.size() < kAvatarDescriptionSize) return;
  std::memset(bytes.data(), 0, kAvatarDescriptionSize);
  if (!catalog.loaded()) return;
  x360::store_be<uint32_t>(&bytes[kHeight], FloatBits(d.height));
  x360::store_be<uint32_t>(&bytes[kWeight], FloatBits(d.weight));
  const auto gender = [&](uint32_t index) {
    const AvatarPart* p = catalog.find_extra(index);
    return p ? p->gender : 3;
  };
  const size_t entries = catalog.pack().entries().size();
  for (uint32_t k = 0; k < kAvatarFeatureCount; ++k)
    if (d.features[k] < entries) StoreAssetId(catalog, &bytes[kFeatures + 16 * k], d.features[k], gender(d.features[k]));
  for (uint32_t k = 0; k < kAvatarFaceTextureCount; ++k)
    if (d.face[k] < entries)
      StoreAssetId(catalog, &bytes[kFaceTextures + kRecordBytes * k], d.face[k], gender(d.face[k]));
  for (uint32_t k = 0; k < kAvatarColorCount; ++k) x360::store_be<uint32_t>(&bytes[kColors + 4 * k], d.colors[k]);
  uint32_t record = 0;
  for (const auto& worn : d.parts) {
    if (record >= kAvatarPartRecords) break;
    const AvatarPart* p = catalog.find_part(worn.index);
    if (!p) continue;
    uint8_t* at = &bytes[kParts + kRecordBytes * record++];
    StoreAssetId(catalog, at, p->index, p->gender);
    x360::store_be<uint16_t>(at + 16, uint16_t(p->slots));
    for (int c = 0; c < 3; ++c) x360::store_be<uint32_t>(at + 20 + 4 * c, worn.colors[c]);
  }
}

std::vector<uint8_t> avatar_description_bytes(const AvatarCatalog& catalog, const AvatarDescription& d) {
  std::vector<uint8_t> bytes(kAvatarDescriptionSize);
  write_avatar_description(catalog, d, bytes);
  return bytes;
}

bool avatar_part_fits(const AvatarPart& p, uint32_t gender) {
  if (p.gender != gender && p.gender != 3) return false;
  if (p.slot == kAvatarHair && p.style != gender && p.style != 0 && p.style != 3) return false;
  return true;
}

const AvatarPart* choose_avatar_part(const AvatarCatalog& catalog, uint32_t slot, uint32_t gender, std::mt19937* rng) {
  std::vector<const AvatarPart*> candidates;
  for (const AvatarPart& p : catalog.parts()) {
    if (p.slot != slot || !avatar_part_fits(p, gender) || p.for_hats) continue;
    candidates.push_back(&p);
  }
  if (candidates.empty()) return nullptr;
  if (rng) return candidates[(*rng)() % candidates.size()];
  if (slot == kAvatarHair) {
    for (const AvatarPart* p : candidates)
      if (catalog.pack().entries()[p->index].name == kDefaultHair[gender == 2 ? 2 : 1]) return p;
  }
  // The plainest: the first that occupies only its own slot and has something to draw (not "Bald").
  for (const AvatarPart* p : candidates)
    if (p->slots == slot && p->meshes) return p;
  return candidates.front();
}

const AvatarPart* choose_avatar_extra(const AvatarCatalog& catalog, uint32_t kind, uint32_t gender, std::mt19937* rng) {
  std::vector<const AvatarPart*> candidates;
  for (const AvatarPart& p : catalog.extras()) {
    if (p.slot == kind && (p.gender == gender || p.gender == 3 || p.gender == 0)) candidates.push_back(&p);
  }
  if (candidates.empty()) return nullptr;
  return rng ? candidates[(*rng)() % candidates.size()] : candidates.front();
}

AvatarDescription make_avatar_description(const AvatarCatalog& catalog, AvatarLook look, uint32_t seed) {
  AvatarDescription d;
  if (!catalog.loaded()) return d;
  std::mt19937 rng(seed);
  const bool random = look == AvatarLook::kRandom;
  std::mt19937* pick = random ? &rng : nullptr;
  d.body = look == AvatarLook::kFemale ? 2 : random ? 1 + rng() % 2 : 1;
  if (random) {
    d.height = (int(rng() % 11) - 5) / 10.0f;
    d.weight = (int(rng() % 11) - 5) / 10.0f;
    static const uint32_t kSkins[] = {0xFFF2C9A6, 0xFFE0AC86, 0xFFC68A64, 0xFF8D5A3B, 0xFF5C3A24};
    static const uint32_t kHairs[] = {0xFF2B1D14, 0xFF5A3A1E, 0xFFA87A3C, 0xFF151515, 0xFF8A2E1A};
    static const uint32_t kEyeColors[] = {0xFF3A6EA5, 0xFF5B3A1E, 0xFF4E7A3A, 0xFF6B6B6B};
    d.colors[kAvatarSkin] = kSkins[rng() % 5];
    d.colors[kAvatarHairColor] = d.colors[kAvatarEyebrows] = d.colors[kAvatarFacialHair] = kHairs[rng() % 5];
    d.colors[kAvatarEyes] = kEyeColors[rng() % 4];
    d.colors[kAvatarEyeShadow] = d.body == 2 ? 0xFF6A4A7A : 0xFF000000;
  }
  // Chin, nose and ears change the head's shape: the default avatars keep the head's own (the pack's
  // first ears are "Fantasy Ears"), a random one may get any.
  for (uint32_t k = 0; random && k < kAvatarFeatureCount; ++k)
    if (const AvatarPart* p = rng() % 2 ? choose_avatar_extra(catalog, kAvatarFeatureKinds[k], d.body, pick) : nullptr)
      d.features[k] = p->index;
  // Eyes, eyebrows and a mouth; a random avatar may also get facial hair or eye shadow.
  for (uint32_t k = 0; k < kAvatarFaceTextureCount; ++k) {
    const uint32_t kind = kAvatarFaceTextureKinds[k];
    const bool always = kind == kAvatarEyesKind || kind == kAvatarEyebrowsKind || kind == kAvatarMouth;
    const bool maybe = random && ((kind == kAvatarFacialHairKind && d.body == 1) || (kind == kAvatarEyeShadowKind && d.body == 2)) &&
                       rng() % 3 == 0;
    if (const AvatarPart* p = (always || maybe) ? choose_avatar_extra(catalog, kind, d.body, pick) : nullptr)
      d.face[k] = p->index;
  }
  // Head, body, hair, shirt, trousers, shoes; a random avatar's in one of their colourways.
  uint32_t covered = 0;
  for (uint32_t slot : {kAvatarHead, kAvatarBody, kAvatarHair, kAvatarShirt, kAvatarTrousers, kAvatarShoes}) {
    if (covered & slot) continue;  // a random outfit (shirt and trousers in one)
    const AvatarPart* p = choose_avatar_part(catalog, slot, d.body, pick);
    if (!p) continue;
    AvatarDescription::Worn worn;
    worn.index = p->index;
    if (const uint32_t count = random ? p->colorways() : 0) {
      const uint32_t colorway = rng() % count;
      for (uint32_t c = 0; c < 3; ++c) worn.colors[c] = p->colorway_color(colorway, c);
    }
    covered |= p->slots;
    d.parts.push_back(worn);
  }
  return d;
}

void customize_avatar(const AvatarCatalog& catalog, AvatarDescription& d, std::string_view text) {
  if (text.empty() || !catalog.loaded()) return;
  if (text.rfind("random:", 0) == 0) {
    d = make_avatar_description(catalog, AvatarLook::kRandom,
                                uint32_t(std::strtoul(std::string(text.substr(7)).c_str(), nullptr, 0)));
    return;
  }
  for (size_t at = 0; at < text.size();) {
    const size_t end = std::min(text.find(',', at), text.size());
    const uint32_t index = uint32_t(std::strtoul(std::string(text.substr(at, end - at)).c_str(), nullptr, 0));
    at = end + 1;
    if (const AvatarPart* p = catalog.find_part(index)) {
      std::erase_if(d.parts, [&](const AvatarDescription::Worn& w) {
        const AvatarPart* worn = catalog.find_part(w.index);
        return !worn || (worn->slots & p->slots) != 0;
      });
      d.parts.push_back({index, {}});
      if (p->slot == kAvatarBody) d.body = p->gender == 2 ? 2 : 1;
      continue;
    }
    if (index >= catalog.pack().entries().size()) continue;
    const uint32_t kind = catalog.pack().entries()[index].word0;
    for (uint32_t k = 0; k < kAvatarFaceTextureCount; ++k)
      if (kAvatarFaceTextureKinds[k] == kind) d.face[k] = index;
    for (uint32_t k = 0; k < kAvatarFeatureCount; ++k)
      if (kAvatarFeatureKinds[k] == kind) d.features[k] = index;
  }
}

bool build_avatar(const AvatarCatalog& catalog, const AvatarDescription& d, const AvatarBuildOptions& options,
                  std::vector<AvatarBuiltPart>& out, std::string& error, std::vector<std::string>* warnings) {
  out.clear();
  if (!catalog.loaded()) {
    error = "no avatar asset pack";
    return false;
  }
  const auto& entries = catalog.pack().entries();
  std::vector<std::pair<const AvatarPart*, const AvatarDescription::Worn*>> parts;
  bool hat = false;
  for (const auto& worn : d.parts) {
    const AvatarPart* p = catalog.find_part(worn.index);
    if (p && (p->slots & kAvatarHat)) hat = true;
  }
  for (const auto& worn : d.parts) {
    const AvatarPart* p = catalog.find_part(worn.index);
    // Under a hat, the hairstyle's version made for hats (cut where the hat sits).
    if (p && hat && p->hat_version != kAvatarNone)
      if (const AvatarPart* version = catalog.find_part(p->hat_version); version && version->meshes) p = version;
    if (p && p->meshes && (p->slot & options.mask)) parts.emplace_back(p, &worn);
  }
  std::stable_sort(parts.begin(), parts.end(), [](const auto& a, const auto& b) { return a.first->slot < b.first->slot; });

  for (const auto& [part, worn] : parts) {
    AvatarBuiltPart built;
    built.part = part;
    std::copy(std::begin(worn->colors), std::end(worn->colors), built.worn_colors);
    const auto& entry = entries[part->index];
    if (!LoadBlock(catalog.pack(), entry, StrbBlockId::kModel, built.file, error) ||
        !parse_avatar_model(built.file, built.model, error, options.left_handed)) {
      error = "part " + std::to_string(entry.index) + ": " + error;
      return false;
    }
    ApplyColors(built.model, *part, worn->colors, d.colors);
    if (options.shapes) ApplyShapes(catalog, built.model, *part, d, options.left_handed, warnings);
    // The head's blank texture slots take the description's face textures. The slots' headers are
    // placeholders (the eyes', eyebrows' and mouth's say 64x64): the textures keep their own size
    // (128x64 for those, a whole eye or mouth per frame), or the GPU shows only their left half.
    if (part->slot == kAvatarHead) {
      for (size_t k = 0; k < built.model.textures.size() && k < kAvatarFaceTextureCount; ++k) {
        auto& slot = built.model.textures[k];
        uint32_t index = kAvatarNone;
        for (uint32_t s = 0; s < kAvatarFaceTextureCount; ++s)
          if (kAvatarFaceTextureKinds[s] == kHeadTextureKinds[k]) index = d.face[s];
        if (index >= entries.size() || !slot.blank) continue;
        std::vector<uint8_t> texture_file;
        AvatarTexture texture;
        std::string texture_error;
        if (!LoadBlock(catalog.pack(), entries[index], StrbBlockId::kTexture, texture_file, texture_error) ||
            !parse_avatar_texture(texture_file, texture, texture_error)) {
          if (warnings) warnings->push_back("face texture " + std::to_string(index) + ": " + texture_error);
          continue;
        }
        if (texture.blank || texture.size > slot.size || texture.slice_count > slot.slice_count) {
          if (warnings)
            warnings->push_back("face texture " + std::to_string(index) + " (" + std::to_string(texture.width) + "x" +
                                std::to_string(texture.height) + ") doesn't fit its slot");
          continue;
        }
        texture.gpu_offset = slot.gpu_offset;
        slot = texture;
        built.face_files.push_back(std::move(texture_file));  // the texture's data points into it
      }
    }
    out.push_back(std::move(built));
  }
  return true;
}

std::string base64_encode(std::span<const uint8_t> bytes) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  for (size_t i = 0; i < bytes.size(); i += 3) {
    const uint32_t n = uint32_t(bytes[i]) << 16 | (i + 1 < bytes.size() ? uint32_t(bytes[i + 1]) << 8 : 0) |
                       (i + 2 < bytes.size() ? uint32_t(bytes[i + 2]) : 0);
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
    out += i + 1 < bytes.size() ? kAlphabet[(n >> 6) & 63] : '=';
    out += i + 2 < bytes.size() ? kAlphabet[n & 63] : '=';
  }
  return out;
}

std::vector<uint8_t> base64_decode(std::string_view text) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<uint8_t> out;
  if (text.size() % 4) return {};
  for (size_t i = 0; i < text.size(); i += 4) {
    int v[4];
    int padding = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = text[i + k];
      if (c == '=' && i + 4 == text.size() && k >= 2) {
        v[k] = 0;
        ++padding;
      } else if (padding || (v[k] = value(c)) < 0) {
        return {};
      }
    }
    const uint32_t n = uint32_t(v[0]) << 18 | uint32_t(v[1]) << 12 | uint32_t(v[2]) << 6 | uint32_t(v[3]);
    out.push_back(uint8_t(n >> 16));
    if (padding < 2) out.push_back(uint8_t(n >> 8));
    if (padding < 1) out.push_back(uint8_t(n));
  }
  return out;
}

}  // namespace x360tool
