// Xbox Avatars as the system makes them, shared by the games' avatar system (runtime/src/hle/
// avatar_system.cpp), the launcher's avatar editor and viewer, and the tests: the usable parts of
// the avatar asset pack (AvatarCatalog), an avatar's description in the console's format
// (XAVATAR_METADATA, 1000 bytes, big-endian), the default and random avatars, and the parts built
// for drawing (colours, face textures and shape overrides applied). See docs/AVATARS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "avatar.h"
#include "avatar_model.h"

namespace x360tool {

constexpr uint32_t kAvatarDescriptionSize = 1000;  // XAVATAR_METADATA
constexpr uint32_t kAvatarNone = 0xFFFFFFFF;
// Component bits 0-12: head, body, hair, shirt, trousers, shoes, hat, gloves, glasses, wristwear,
// earrings, ring, carryable.
constexpr uint32_t kAvatarSlots = 13;
constexpr uint32_t kAvatarHead = 0x1, kAvatarBody = 0x2, kAvatarHair = 0x4, kAvatarShirt = 0x8, kAvatarTrousers = 0x10,
                   kAvatarShoes = 0x20, kAvatarHat = 0x40, kAvatarGloves = 0x80, kAvatarGlasses = 0x100,
                   kAvatarWristwear = 0x200, kAvatarEarrings = 0x400, kAvatarRing = 0x800, kAvatarCarryable = 0x1000;
constexpr uint32_t kAvatarTemplateKind = 0x01000000;  // hiding and body modification templates
constexpr uint32_t kAvatarColorCount = 9, kAvatarFeatureCount = 3, kAvatarFaceTextureCount = 6;
constexpr uint32_t kAvatarPartRecords = 15;

// The description's colours, which are also the head's colour constants 13-21 in this order.
enum AvatarColor {
  kAvatarSkin,
  kAvatarHairColor,
  kAvatarLips,
  kAvatarEyes,
  kAvatarEyebrows,
  kAvatarEyeShadow,
  kAvatarFacialHair,
  kAvatarFacePaint1,
  kAvatarFacePaint2
};

// Chin, nose, ears (the description's features: shape overrides of the head).
inline constexpr uint32_t kAvatarFeatureKinds[kAvatarFeatureCount] = {0x100000, 0x80000, 0x200000};
// The face textures in a description's order: mouth, eyes, eyebrows, facial hair, eye shadow, face
// paint.
inline constexpr uint32_t kAvatarFaceTextureKinds[kAvatarFaceTextureCount] = {0x8000, 0x2000, 0x4000,
                                                                             0x10000, 0x40000, 0x20000};
constexpr uint32_t kAvatarMouth = 0x8000, kAvatarEyesKind = 0x2000, kAvatarEyebrowsKind = 0x4000,
                   kAvatarFacialHairKind = 0x10000, kAvatarEyeShadowKind = 0x40000, kAvatarFacePaintKind = 0x20000;
constexpr uint32_t kAvatarChin = 0x100000, kAvatarNose = 0x80000, kAvatarEars = 0x200000;

// A part of the pack a description can name: a component (models), or a face texture or feature
// (an "extra": `slot` is its kind).
struct AvatarPart {
  uint32_t index = 0;   // pack entry
  uint32_t slots = 0;   // component bits it occupies (for extras, their kind)
  uint32_t slot = 0;    // its own (lowest) bit
  uint32_t gender = 0;  // 1 male, 2 female, 3 both
  uint32_t cpu = 0, gpu = 0;
  uint32_t meshes = 0;
  std::vector<uint8_t> table;  // its colour table (block 7), if any
  // Hairstyles: the second byte of the entry's second word. Every hairstyle's metadata says "both
  // genders", but this byte groups them: 1 partings, crops and spikes, 2 bobs, buns, ponytails and
  // plaits, 0 and 3 either.
  uint32_t style = 0;
  // A hairstyle's version for under a hat (the pack entry's link), and whether this is one.
  uint32_t hat_version = kAvatarNone;
  bool for_hats = false;

  // The colourways the player can pick (block 7: a little-endian count, then per colourway three
  // {BGRA, word}), and colour k (0-2) of one, as ARGB.
  uint32_t colorways() const;
  uint32_t colorway_color(uint32_t colorway, uint32_t k) const;
};

// The pack's parts a description can use.
class AvatarCatalog {
 public:
  // Reads the pack's table (AvatarAssetPack.toc, whole): every component with a model that parses,
  // every face texture and feature. False with `error` if it isn't one or has no part.
  bool load(std::vector<uint8_t> toc, std::string& error);
  bool loaded() const { return pack_ && !parts_.empty(); }

  const AvatarPack& pack() const { return *pack_; }
  const std::vector<AvatarPart>& parts() const { return parts_; }
  const std::vector<AvatarPart>& extras() const { return extras_; }
  const AvatarPart* find_part(uint32_t index) const;
  const AvatarPart* find_extra(uint32_t index) const;
  // The entry's name (the pack's own, first), as UTF-8 ("" if there's no such entry).
  std::string name(uint32_t index) const;
  // The name players see, in an XLanguage (1 English, 2 Japanese, 3 German, 4 French, 5 Spanish...:
  // the entry's 13 names after its own), else English, else its own.
  std::string display_name(uint32_t index, uint32_t language) const;
  // The largest CPU and GPU block of a part in component bit `bit`.
  uint32_t max_cpu(uint32_t bit) const { return bit < kAvatarSlots ? max_cpu_[bit] : 0; }
  uint32_t max_gpu(uint32_t bit) const { return bit < kAvatarSlots ? max_gpu_[bit] : 0; }

 private:
  std::unique_ptr<AvatarPack> pack_;
  std::vector<AvatarPart> parts_;
  std::vector<AvatarPart> extras_;
  uint32_t max_cpu_[kAvatarSlots] = {}, max_gpu_[kAvatarSlots] = {};
};

// A description, decoded.
struct AvatarDescription {
  uint32_t body = 1;  // 1 male, 2 female
  float height = 0.0f, weight = 0.0f;  // -1 to 1
  uint32_t colors[kAvatarColorCount] = {0xFFE0AC86, 0xFF2B1D14, 0xFFB86A68, 0xFF5B3A1E, 0xFF2B1D14,
                                        0xFF000000, 0xFF2B1D14, 0xFF000000, 0xFF000000};
  uint32_t features[kAvatarFeatureCount] = {kAvatarNone, kAvatarNone, kAvatarNone};  // chin, nose, ears
  uint32_t face[kAvatarFaceTextureCount] = {kAvatarNone, kAvatarNone, kAvatarNone,
                                            kAvatarNone, kAvatarNone, kAvatarNone};  // by kAvatarFaceTextureKinds
  struct Worn {
    uint32_t index = 0;
    uint32_t colors[3] = {};  // ARGB; 0: the asset's own (its first colourway)
    bool operator==(const Worn&) const = default;
  };
  std::vector<Worn> parts;  // components, at most kAvatarPartRecords
  bool operator==(const AvatarDescription&) const = default;
};

// A description in the console's format (`bytes`: kAvatarDescriptionSize, big-endian):
//   +4 height and +8 weight, -1 to 1 (the avatar library reads them too);
//   +12, +28, +44 the head's chin, nose and ears (shape overrides): asset IDs;
//   +60 six face textures (mouth, eyes, eyebrows, facial hair, eye shadow, face paint), 32 bytes
//       each: an asset ID and 16 bytes;
//   +252 nine colours (ARGB): skin, hair, lips, eyes, eyebrows, eye shadow, facial hair and two for
//       face paint;
//   +288 fifteen parts, 32 bytes each: an asset ID, the component bits it covers (16 bits), 0, and
//       12 bytes for the part's three colours (0: the asset's own).
// An asset ID is the asset's category bits (the pack entry's first word), its number (the pack
// entry, 16 bits), its gender (16 bits: 1 male, 2 female, 3 both) and the same 8 bytes for every
// stock asset (C1C8F109A19CB2E0).
//
// False if it names no component the catalog has (not a description, or not of the pack's assets).
bool read_avatar_description(const AvatarCatalog& catalog, std::span<const uint8_t> bytes, AvatarDescription& out);
void write_avatar_description(const AvatarCatalog& catalog, const AvatarDescription& description,
                              std::span<uint8_t> bytes);
std::vector<uint8_t> avatar_description_bytes(const AvatarCatalog& catalog, const AvatarDescription& description);
// An asset ID's pack entry, if it names one of the catalog's assets (kAvatarNone otherwise).
uint32_t avatar_asset_entry(const AvatarCatalog& catalog, std::span<const uint8_t, 16> id);

// The default male or female avatar, or a random one.
enum class AvatarLook { kMale, kFemale, kRandom };
AvatarDescription make_avatar_description(const AvatarCatalog& catalog, AvatarLook look, uint32_t seed = 0);

// A component for `slot` (one bit) a `gender` avatar can wear: the default (`rng` null: the plainest,
// or the default look's hairstyle) or a random one. Hairstyles for hats are never chosen.
const AvatarPart* choose_avatar_part(const AvatarCatalog& catalog, uint32_t slot, uint32_t gender, std::mt19937* rng);
// Whether a `gender` avatar can wear it (hairstyles: their style group too).
bool avatar_part_fits(const AvatarPart& part, uint32_t gender);
// A face texture or feature of `kind` for `gender`: the first, or a random one.
const AvatarPart* choose_avatar_extra(const AvatarCatalog& catalog, uint32_t kind, uint32_t gender, std::mt19937* rng);

// Testing (ANYX360_AVATAR): "random:<seed>" makes the avatar random, or a comma-separated list of
// pack entries puts those on: a component takes its slots' place (a body changes the gender), a face
// texture or a face shape its place in the face.
void customize_avatar(const AvatarCatalog& catalog, AvatarDescription& description, std::string_view spec);

// The parts of an avatar, built for drawing.
struct AvatarBuiltPart {
  const AvatarPart* part = nullptr;
  uint32_t worn_colors[3] = {};
  std::vector<uint8_t> file;  // the model file (the model's texture data points into it)
  AvatarModel model;
  std::vector<std::vector<uint8_t>> face_files;  // the face textures put in the head's blank slots
};
struct AvatarBuildOptions {
  uint32_t mask = (1u << kAvatarSlots) - 1;  // the components wanted
  bool left_handed = false;                  // the title's coordinate system (mirrors z)
  bool shapes = true;                        // apply hiding templates and face shapes
};
// The description's parts in `mask`, in component order (the head first, the body second): under a
// hat a hairstyle is its version for hats; parts with nothing to draw ("Bald") are left out. Each
// model has its colours set (the recipe in docs/AVATARS.md), the body its hiding and body
// modification templates and the head the face shapes and face textures (each keeping its own
// texture header: 128x64 for eyes, eyebrows and mouths). Problems with a face texture or a shape
// are `warnings`; false with `error` if a part can't be read.
bool build_avatar(const AvatarCatalog& catalog, const AvatarDescription& description, const AvatarBuildOptions& options,
                  std::vector<AvatarBuiltPart>& out, std::string& error, std::vector<std::string>* warnings = nullptr);

// Base64 (RFC 4648, with padding), for the description in a profile's profile.json.
std::string base64_encode(std::span<const uint8_t> bytes);
// Empty if `text` isn't base64.
std::vector<uint8_t> base64_decode(std::string_view text);

}  // namespace x360tool
