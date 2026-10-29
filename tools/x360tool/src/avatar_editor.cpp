// The avatar editor's model (see avatar_editor.h).
#include "avatar_editor.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace x360tool {

namespace {

constexpr uint32_t kRequired[] = {kAvatarHead, kAvatarBody, kAvatarHair, kAvatarShirt, kAvatarTrousers, kAvatarShoes};
constexpr uint32_t kAccessorySlots = kAvatarGloves | kAvatarWristwear | kAvatarEarrings | kAvatarRing;

constexpr uint32_t kSkins[] = {0xFFF2D6C2, 0xFFF2C9A6, 0xFFE8B894, 0xFFE0AC86, 0xFFD29B74, 0xFFC68A64, 0xFFB07650,
                               0xFF9C6644, 0xFF8D5A3B, 0xFF7A4A2E, 0xFF6A3E25, 0xFF5C3A24, 0xFF4A2C1A, 0xFF3B2216};
constexpr uint32_t kHairs[] = {0xFF151515, 0xFF2B1D14, 0xFF3D2817, 0xFF5A3A1E, 0xFF7A5230, 0xFFA87A3C,
                               0xFFC9A05A, 0xFFE3C88A, 0xFFF0E0B0, 0xFF8A2E1A, 0xFFB5482A, 0xFF9A9A9A,
                               0xFFD8D8D8, 0xFF5B3F8C, 0xFF2E5A9A, 0xFFC0407A};
constexpr uint32_t kEyeColors[] = {0xFF3A6EA5, 0xFF2E4F7A, 0xFF5B8FB0, 0xFF5B3A1E, 0xFF3E2716, 0xFF7A5530,
                                   0xFF4E7A3A, 0xFF6E8A4A, 0xFF6B6B6B, 0xFF8A9AA5, 0xFF9A6A2A, 0xFF5A3A6A};
constexpr uint32_t kLipColors[] = {0xFFB86A68, 0xFFA85A58, 0xFFC27A74, 0xFFD08A88, 0xFF9A4A4A,
                                   0xFF7A3A3A, 0xFFC25A6A, 0xFFD0707A, 0xFF8A5050, 0xFFE09A90};

// What a category edits: components in a slot (or the accessory slots), a face texture (its index
// in the description's six) or a feature (its index in the three).
struct Kind {
  uint32_t slots = 0;  // components whose own slot is one of these
  int face = -1;
  int feature = -1;
};

Kind KindOf(AvatarCategory category) {
  switch (category) {
    case AvatarCategory::kHair: return {kAvatarHair};
    case AvatarCategory::kEyes: return {0, 1};
    case AvatarCategory::kEyebrows: return {0, 2};
    case AvatarCategory::kMouth: return {0, 0};
    case AvatarCategory::kFacialHair: return {0, 3};
    case AvatarCategory::kChin: return {0, -1, 0};
    case AvatarCategory::kNose: return {0, -1, 1};
    case AvatarCategory::kEars: return {0, -1, 2};
    case AvatarCategory::kShirt: return {kAvatarShirt};
    case AvatarCategory::kTrousers: return {kAvatarTrousers};
    case AvatarCategory::kShoes: return {kAvatarShoes};
    case AvatarCategory::kHat: return {kAvatarHat};
    case AvatarCategory::kGlasses: return {kAvatarGlasses};
    case AvatarCategory::kAccessories: return {kAccessorySlots};
    default: return {};
  }
}

bool ExtraFits(const AvatarPart& p, uint32_t gender) { return p.gender == gender || p.gender == 3 || p.gender == 0; }

}  // namespace

std::span<const uint32_t> avatar_skin_palette() { return kSkins; }
std::span<const uint32_t> avatar_hair_palette() { return kHairs; }
std::span<const uint32_t> avatar_eye_palette() { return kEyeColors; }
std::span<const uint32_t> avatar_lip_palette() { return kLipColors; }

AvatarEditor::AvatarEditor(const AvatarCatalog& catalog, AvatarDescription start) : catalog_(catalog), d_(std::move(start)) {
  Normalize();
}

bool AvatarEditor::Optional(AvatarCategory category) const {
  switch (category) {
    case AvatarCategory::kFacialHair:
    case AvatarCategory::kChin:
    case AvatarCategory::kNose:
    case AvatarCategory::kEars:
    case AvatarCategory::kHat:
    case AvatarCategory::kGlasses:
    case AvatarCategory::kAccessories: return true;
    default: return false;
  }
}

std::vector<AvatarOption> AvatarEditor::options(AvatarCategory category) const {
  std::vector<AvatarOption> out;
  if (!catalog_.loaded()) return out;
  const Kind kind = KindOf(category);
  if (Optional(category)) out.push_back({kAvatarNone, {}});
  if (kind.slots) {
    for (const AvatarPart& p : catalog_.parts())
      if ((p.slot & kind.slots) && !p.for_hats && avatar_part_fits(p, d_.body)) out.push_back({p.index, catalog_.display_name(p.index, 1)});
  } else {
    const uint32_t wanted = kind.face >= 0 ? kAvatarFaceTextureKinds[kind.face] : kAvatarFeatureKinds[kind.feature];
    for (const AvatarPart& p : catalog_.extras())
      if (p.slot == wanted && ExtraFits(p, d_.body)) out.push_back({p.index, catalog_.display_name(p.index, 1)});
  }
  return out;
}

const AvatarDescription::Worn* AvatarEditor::Worn(AvatarCategory category) const {
  const Kind kind = KindOf(category);
  if (!kind.slots) return nullptr;
  const AvatarDescription::Worn* found = nullptr;
  for (const auto& worn : d_.parts)
    if (const AvatarPart* p = catalog_.find_part(worn.index); p && (p->slot & kind.slots)) found = &worn;
  return found;
}

AvatarDescription::Worn* AvatarEditor::Worn(AvatarCategory category) {
  return const_cast<AvatarDescription::Worn*>(static_cast<const AvatarEditor*>(this)->Worn(category));
}

uint32_t AvatarEditor::current(AvatarCategory category) const {
  const Kind kind = KindOf(category);
  if (kind.face >= 0) return d_.face[kind.face];
  if (kind.feature >= 0) return d_.features[kind.feature];
  const AvatarDescription::Worn* worn = Worn(category);
  return worn ? worn->index : kAvatarNone;
}

bool AvatarEditor::choose(AvatarCategory category, uint32_t index) {
  const Kind kind = KindOf(category);
  if (index == kAvatarNone) {
    if (!Optional(category)) return false;
    if (kind.face >= 0) d_.face[kind.face] = kAvatarNone;
    else if (kind.feature >= 0) d_.features[kind.feature] = kAvatarNone;
    else
      std::erase_if(d_.parts, [&](const AvatarDescription::Worn& w) {
        const AvatarPart* p = catalog_.find_part(w.index);
        return p && (p->slot & kind.slots);
      });
    Normalize();
    return true;
  }
  if (kind.slots) {
    const AvatarPart* p = catalog_.find_part(index);
    if (!p || !(p->slot & kind.slots) || p->for_hats || !avatar_part_fits(*p, d_.body)) return false;
    // It replaces whatever covers its slots (an outfit both the top and the bottom; the bottom then
    // a whole outfit, whose top becomes the default).
    std::erase_if(d_.parts, [&](const AvatarDescription::Worn& w) {
      const AvatarPart* worn = catalog_.find_part(w.index);
      return !worn || (worn->slots & p->slots);
    });
    d_.parts.push_back({index, {}});
  } else {
    const AvatarPart* p = catalog_.find_extra(index);
    const uint32_t wanted = kind.face >= 0 ? kAvatarFaceTextureKinds[kind.face] : kAvatarFeatureKinds[kind.feature];
    if (!p || p->slot != wanted || !ExtraFits(*p, d_.body)) return false;
    if (kind.face >= 0) d_.face[kind.face] = index;
    else d_.features[kind.feature] = index;
  }
  Normalize();
  return true;
}

uint32_t AvatarEditor::colorways(AvatarCategory category) const {
  const AvatarDescription::Worn* worn = Worn(category);
  const AvatarPart* p = worn ? catalog_.find_part(worn->index) : nullptr;
  // The body and hair take the skin and hair colours.
  if (!p || p->slot == kAvatarBody || p->slot == kAvatarHair) return 0;
  return p->colorways();
}

int AvatarEditor::colorway(AvatarCategory category) const {
  const AvatarDescription::Worn* worn = Worn(category);
  const AvatarPart* p = worn ? catalog_.find_part(worn->index) : nullptr;
  if (!p || (!worn->colors[0] && !worn->colors[1] && !worn->colors[2])) return -1;
  for (uint32_t c = 0; c < p->colorways(); ++c)
    if (p->colorway_color(c, 0) == worn->colors[0] && p->colorway_color(c, 1) == worn->colors[1] &&
        p->colorway_color(c, 2) == worn->colors[2])
      return int(c);
  return -1;
}

void AvatarEditor::choose_colorway(AvatarCategory category, uint32_t colorway) {
  AvatarDescription::Worn* worn = Worn(category);
  const AvatarPart* p = worn ? catalog_.find_part(worn->index) : nullptr;
  if (!p || colorway >= colorways(category)) return;
  for (uint32_t k = 0; k < 3; ++k) worn->colors[k] = p->colorway_color(colorway, k);
}

void AvatarEditor::set_gender(uint32_t body) {
  body = body == 2 ? 2 : 1;
  if (body == d_.body) return;
  d_.body = body;
  // The other body; Normalize replaces whatever doesn't fit.
  std::erase_if(d_.parts, [&](const AvatarDescription::Worn& w) {
    const AvatarPart* p = catalog_.find_part(w.index);
    return !p || p->slot == kAvatarBody;
  });
  if (const AvatarPart* p = choose_avatar_part(catalog_, kAvatarBody, body, nullptr)) d_.parts.push_back({p->index, {}});
  // The other gender's default hair, rather than whichever style happens to fit both.
  std::erase_if(d_.parts, [&](const AvatarDescription::Worn& w) {
    const AvatarPart* p = catalog_.find_part(w.index);
    return p && p->slot == kAvatarHair && p->style != 0 && p->style != 3;
  });
  Normalize();
}

void AvatarEditor::set_color(AvatarColor which, uint32_t argb) {
  if (which >= kAvatarColorCount) return;
  d_.colors[which] = argb | 0xFF000000;
  if (which == kAvatarHairColor) d_.colors[kAvatarEyebrows] = d_.colors[kAvatarFacialHair] = d_.colors[which];
}

void AvatarEditor::set_height(float value) { d_.height = std::clamp(std::round(value * 10) / 10, -1.0f, 1.0f); }
void AvatarEditor::set_weight(float value) { d_.weight = std::clamp(std::round(value * 10) / 10, -1.0f, 1.0f); }

void AvatarEditor::randomize(uint32_t seed) {
  d_ = make_avatar_description(catalog_, AvatarLook::kRandom, seed);
  Normalize();
}

void AvatarEditor::Normalize() {
  if (!catalog_.loaded()) return;
  // The gender is the body's (the last body worn), else the description's.
  for (const auto& worn : d_.parts)
    if (const AvatarPart* p = catalog_.find_part(worn.index); p && p->slot == kAvatarBody) d_.body = p->gender == 2 ? 2 : 1;
  d_.body = d_.body == 2 ? 2 : 1;
  // What isn't the pack's, doesn't fit the gender or is a hat's version of a hairstyle goes; of
  // parts covering the same slots the last put on stays.
  std::vector<AvatarDescription::Worn> kept;
  uint32_t covered = 0;
  for (auto it = d_.parts.rbegin(); it != d_.parts.rend(); ++it) {
    const AvatarPart* p = catalog_.find_part(it->index);
    if (!p || p->for_hats || !avatar_part_fits(*p, d_.body) || (p->slots & covered)) continue;
    covered |= p->slots;
    kept.push_back(*it);
  }
  for (uint32_t slot : kRequired) {
    if (covered & slot) continue;
    if (const AvatarPart* p = choose_avatar_part(catalog_, slot, d_.body, nullptr); p && !(p->slots & covered)) {
      covered |= p->slots;
      kept.push_back({p->index, {}});
    }
  }
  std::stable_sort(kept.begin(), kept.end(), [&](const auto& a, const auto& b) {
    return catalog_.find_part(a.index)->slot < catalog_.find_part(b.index)->slot;
  });
  if (kept.size() > kAvatarPartRecords) kept.resize(kAvatarPartRecords);
  d_.parts = std::move(kept);
  // Face textures and features of their kind that fit; eyes, eyebrows and a mouth always.
  for (uint32_t k = 0; k < kAvatarFaceTextureCount; ++k) {
    const AvatarPart* p = d_.face[k] != kAvatarNone ? catalog_.find_extra(d_.face[k]) : nullptr;
    if (!p || p->slot != kAvatarFaceTextureKinds[k] || !ExtraFits(*p, d_.body)) d_.face[k] = kAvatarNone;
    const uint32_t kind = kAvatarFaceTextureKinds[k];
    if (d_.face[k] == kAvatarNone && (kind == kAvatarEyesKind || kind == kAvatarEyebrowsKind || kind == kAvatarMouth))
      if (const AvatarPart* chosen = choose_avatar_extra(catalog_, kind, d_.body, nullptr)) d_.face[k] = chosen->index;
  }
  for (uint32_t k = 0; k < kAvatarFeatureCount; ++k) {
    const AvatarPart* p = d_.features[k] != kAvatarNone ? catalog_.find_extra(d_.features[k]) : nullptr;
    if (!p || p->slot != kAvatarFeatureKinds[k] || !ExtraFits(*p, d_.body)) d_.features[k] = kAvatarNone;
  }
  d_.height = std::clamp(d_.height, -1.0f, 1.0f);
  d_.weight = std::clamp(d_.weight, -1.0f, 1.0f);
}

bool avatar_description_valid(const AvatarCatalog& catalog, const AvatarDescription& d, std::string* why) {
  auto fail = [&](std::string reason) {
    if (why) *why = std::move(reason);
    return false;
  };
  if (d.body != 1 && d.body != 2) return fail("gender");
  uint32_t covered = 0;
  for (const auto& worn : d.parts) {
    const AvatarPart* p = catalog.find_part(worn.index);
    if (!p) return fail("unknown part " + std::to_string(worn.index));
    if (!avatar_part_fits(*p, d.body)) return fail(catalog.name(p->index) + " doesn't fit the gender");
    if (p->for_hats) return fail(catalog.name(p->index) + " is a hat's hairstyle");
    if (p->slots & covered) return fail(catalog.name(p->index) + " overlaps another part");
    if (p->slot == kAvatarBody && p->gender != d.body) return fail("the body isn't the gender's");
    covered |= p->slots;
  }
  for (uint32_t slot : kRequired)
    if (!(covered & slot)) return fail("slot " + std::to_string(slot) + " is empty");
  for (uint32_t k = 0; k < kAvatarFaceTextureCount; ++k) {
    if (d.face[k] == kAvatarNone) continue;
    const AvatarPart* p = catalog.find_extra(d.face[k]);
    if (!p || p->slot != kAvatarFaceTextureKinds[k] || !ExtraFits(*p, d.body)) return fail("face texture " + std::to_string(k));
  }
  for (uint32_t k = 0; k < kAvatarFeatureCount; ++k) {
    if (d.features[k] == kAvatarNone) continue;
    const AvatarPart* p = catalog.find_extra(d.features[k]);
    if (!p || p->slot != kAvatarFeatureKinds[k] || !ExtraFits(*p, d.body)) return fail("feature " + std::to_string(k));
  }
  if (d.height < -1 || d.height > 1 || d.weight < -1 || d.weight > 1) return fail("height or weight");
  return true;
}

}  // namespace x360tool
