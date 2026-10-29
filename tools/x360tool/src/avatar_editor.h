// The avatar editor's model (the launcher's Avatar page; no drawing here): what can be changed in an
// avatar, by category, with the console's rules: a part must fit the avatar's gender (hairstyles
// their style group too), a part replaces whatever covers the same component slots, and the body,
// head, hair, top, bottom and shoes are never left empty. See docs/AVATARS.md.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "avatar_look.h"

namespace x360tool {

enum class AvatarCategory {
  kHair,
  kEyes,
  kEyebrows,
  kMouth,
  kFacialHair,
  kNose,
  kChin,
  kEars,
  kShirt,
  kTrousers,
  kShoes,
  kHat,
  kGlasses,
  kAccessories,  // gloves, wristwear, earrings, rings
  kCount
};
constexpr int kAvatarCategories = int(AvatarCategory::kCount);

// One choice of a category: a pack entry, or kAvatarNone (none, or for a nose, chin or ears the
// head's own shape).
struct AvatarOption {
  uint32_t index = kAvatarNone;
  std::string name;  // its English name, as players see it ("" for none)
};

// The colours the player picks from, as the console's palettes.
std::span<const uint32_t> avatar_skin_palette();
std::span<const uint32_t> avatar_hair_palette();
std::span<const uint32_t> avatar_eye_palette();
std::span<const uint32_t> avatar_lip_palette();

class AvatarEditor {
 public:
  // Editing `start` (made valid: see Normalize).
  AvatarEditor(const AvatarCatalog& catalog, AvatarDescription start);

  const AvatarDescription& description() const { return d_; }
  std::vector<uint8_t> bytes() const { return avatar_description_bytes(catalog_, d_); }

  // The category's choices for the avatar's gender (optional categories start with none).
  std::vector<AvatarOption> options(AvatarCategory category) const;
  // What the avatar has in it (kAvatarNone: none, or the head's own shape).
  uint32_t current(AvatarCategory category) const;
  // Puts `index` on (kAvatarNone: takes the category off, if it can be). False if it doesn't fit.
  bool choose(AvatarCategory category, uint32_t index);

  // The colourways of what the category has on (0: none to choose), and which one it wears (-1: its
  // own colours, the first).
  uint32_t colorways(AvatarCategory category) const;
  int colorway(AvatarCategory category) const;
  void choose_colorway(AvatarCategory category, uint32_t colorway);

  // 1 male, 2 female: the body changes, and whatever doesn't fit the new gender becomes the default.
  uint32_t gender() const { return d_.body; }
  void set_gender(uint32_t body);

  // Skin, hair (the eyebrows and facial hair follow it), eyes, lips.
  uint32_t color(AvatarColor which) const { return d_.colors[which]; }
  void set_color(AvatarColor which, uint32_t argb);
  // -1 to 1, in steps of 0.1.
  float height() const { return d_.height; }
  float weight() const { return d_.weight; }
  void set_height(float value);
  void set_weight(float value);

  // A random avatar (x360tool::make_avatar_description).
  void randomize(uint32_t seed);

 private:
  // Fills the components that can't be empty and drops what doesn't fit the gender or overlaps.
  void Normalize();
  bool Optional(AvatarCategory category) const;
  // The worn component of the category, if any.
  const AvatarDescription::Worn* Worn(AvatarCategory category) const;
  AvatarDescription::Worn* Worn(AvatarCategory category);

  const AvatarCatalog& catalog_;
  AvatarDescription d_;
};

// Whether a description is one a player could make: a body that gives its gender, every component
// fitting it and covering no other's slots, the body, head, hair, top, bottom and shoes there, face
// textures and features of their kind. For the tests and the editor's checks.
bool avatar_description_valid(const AvatarCatalog& catalog, const AvatarDescription& description, std::string* why = nullptr);

}  // namespace x360tool
