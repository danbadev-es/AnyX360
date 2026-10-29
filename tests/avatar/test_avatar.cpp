// Avatars (x360tool/src/avatar_look.h, avatar_editor.h) on a made-up avatar asset pack: the
// catalog, descriptions in the console's format and their round trip through a profile's
// profile.json, the default and random avatars, and the editor's rules (a part fits the gender, a
// part replaces what covers its slots, the body, head, hair, top, bottom and shoes are never empty).
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "avatar_editor.h"
#include "avatar_look.h"
#include "test_pack.h"
#include "x360/profiles.h"

static int failures = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    if (cond) {                                                     \
      std::printf("PASS %s\n", #cond);                              \
    } else {                                                        \
      std::printf("FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++failures;                                                   \
    }                                                               \
  } while (0)

using namespace x360tool;
using namespace test_pack;
namespace fs = std::filesystem;

namespace {

bool Wears(const AvatarDescription& d, uint32_t index) {
  for (const auto& worn : d.parts)
    if (worn.index == index) return true;
  return false;
}

bool Offers(const std::vector<AvatarOption>& options, uint32_t index) {
  for (const auto& option : options)
    if (option.index == index) return true;
  return false;
}

}  // namespace

int main() {
  AvatarCatalog catalog;
  std::string error;
  CHECK(catalog.load(MakeToc(), error));
  // Components with a model, face textures and features; not the template, not the hat's hairstyle
  // as a choice.
  CHECK(catalog.find_part(kMaleBody) && catalog.find_part(kDress) && catalog.find_part(kEarrings));
  CHECK(catalog.find_extra(kEyes) && catalog.find_extra(kNose) && !catalog.find_part(kTeeTemplate));
  CHECK(catalog.find_part(kCrewCut)->hat_version == kCrewCutHat && catalog.find_part(kCrewCutHat)->for_hats);
  CHECK(catalog.find_part(kDress)->slots == 0x18 && catalog.find_part(kDress)->slot == 0x8);
  CHECK(catalog.find_part(kMaleTee)->colorways() == 3);
  CHECK(catalog.name(kShoulderLength) == "Shoulder Length");

  // The defaults: male and female, each with the default look's hairstyle, eyes, eyebrows and mouth.
  const AvatarDescription male = make_avatar_description(catalog, AvatarLook::kMale);
  const AvatarDescription female = make_avatar_description(catalog, AvatarLook::kFemale);
  CHECK(male.body == 1 && Wears(male, kMaleBody) && Wears(male, kCrewCut) && Wears(male, kMaleTee) && Wears(male, kJeans));
  CHECK(female.body == 2 && Wears(female, kFemaleBody) && Wears(female, kShoulderLength) && Wears(female, kFemaleTop));
  CHECK(male.face[1] == kEyes && male.face[2] == kEyebrows && male.face[0] == kMouth);
  CHECK(avatar_description_valid(catalog, male) && avatar_description_valid(catalog, female));

  // The console's format: 1000 bytes, asset IDs (category bits, entry, gender, the stock tail),
  // colours at +252, parts at +288.
  {
    AvatarDescription d = male;
    d.height = 0.5f, d.weight = -0.3f;
    d.features[1] = kNose;
    d.colors[kAvatarSkin] = 0xFF8D5A3B;
    d.parts[3].colors[0] = 0xFF123456;
    const std::vector<uint8_t> bytes = avatar_description_bytes(catalog, d);
    CHECK(bytes.size() == kAvatarDescriptionSize);
    CHECK(bytes[252] == 0xFF && bytes[253] == 0x8D && bytes[254] == 0x5A && bytes[255] == 0x3B);
    // The first part (the head: component order) at +288: category 1, entry 2, gender 3.
    CHECK(bytes[291] == 0x01 && bytes[292] == 0 && bytes[293] == kHead && bytes[295] == 3 && bytes[296] == 0xC1);
    CHECK(bytes[288 + 17] == 0x01);  // its component bits
    // The nose at +28.
    CHECK(bytes[28 + 3] == 0x00 && bytes[28 + 1] == 0x08 && bytes[28 + 5] == kNose);
    AvatarDescription back;
    CHECK(read_avatar_description(catalog, bytes, back));
    CHECK(back == d);
    // Not a description: no component the pack has.
    std::vector<uint8_t> empty(kAvatarDescriptionSize, 0);
    CHECK(!read_avatar_description(catalog, empty, back));
    std::vector<uint8_t> other = bytes;
    other[296] = 0;  // not a stock asset ID
    for (uint32_t k = 0; k < kAvatarPartRecords; ++k) other[288 + 32 * k + 8] = 0;
    CHECK(!read_avatar_description(catalog, other, back));
  }

  // Through profile.json (base64 in "avatar"), as the launcher saves it and the games read it.
  {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const fs::path root = fs::path(temp) / ("anyx360_test_avatar_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    const x360::profiles::Result created = x360::profiles::Create(root, "Ana");
    CHECK(bool(created));
    AvatarEditor editor(catalog, female);
    CHECK(editor.choose(AvatarCategory::kShirt, kDress));
    editor.set_color(kAvatarHairColor, 0xFF8A2E1A);
    editor.set_height(0.3f);
    x360::profiles::Profile p = created.profile;
    p.avatar = base64_encode(editor.bytes());
    CHECK(x360::profiles::Save(root, p));
    const std::optional<x360::profiles::Profile> loaded = x360::profiles::Load(root, p.xuid);
    CHECK(loaded && loaded->avatar == p.avatar && loaded->avatar.size() == 1336);
    const std::vector<uint8_t> decoded = base64_decode(loaded ? loaded->avatar : std::string());
    CHECK(decoded == editor.bytes());
    AvatarDescription back;
    CHECK(read_avatar_description(catalog, decoded, back) && back == editor.description());
    CHECK(Wears(back, kDress) && !Wears(back, kSkirt) && back.colors[kAvatarEyebrows] == 0xFF8A2E1A);
    fs::remove_all(root, ec);
  }
  CHECK(base64_decode("QUJD") == std::vector<uint8_t>({'A', 'B', 'C'}));
  CHECK(base64_decode("QUI=") == std::vector<uint8_t>({'A', 'B'}));
  CHECK(base64_encode(std::vector<uint8_t>({'A'})) == "QQ==");
  CHECK(base64_decode("QU=I").empty() && base64_decode("QUJ").empty() && base64_decode("Q!JD").empty());

  // The editor's rules.
  {
    AvatarEditor editor(catalog, male);
    // Only what fits the gender is offered or taken.
    const auto shirts = editor.options(AvatarCategory::kShirt);
    CHECK(Offers(shirts, kMaleTee) && !Offers(shirts, kFemaleTop) && !Offers(shirts, kDress));
    CHECK(!editor.choose(AvatarCategory::kShirt, kFemaleTop) && editor.current(AvatarCategory::kShirt) == kMaleTee);
    CHECK(!editor.choose(AvatarCategory::kShirt, kJeans));  // not a top
    // Hairstyles: the male group and either, never a hat's version.
    const auto hair = editor.options(AvatarCategory::kHair);
    CHECK(Offers(hair, kCrewCut) && !Offers(hair, kShoulderLength) && !Offers(hair, kCrewCutHat));
    CHECK(!editor.choose(AvatarCategory::kHair, kCrewCutHat));
    // Optional categories start with none; the required ones can't be emptied.
    CHECK(editor.options(AvatarCategory::kHat).front().index == kAvatarNone);
    CHECK(!editor.choose(AvatarCategory::kShirt, kAvatarNone) && editor.current(AvatarCategory::kShirt) == kMaleTee);
    CHECK(editor.choose(AvatarCategory::kHat, kCap) && editor.current(AvatarCategory::kHat) == kCap);
    CHECK(editor.current(AvatarCategory::kHair) == kCrewCut);  // the hat's version only when built
    CHECK(editor.choose(AvatarCategory::kHat, kAvatarNone) && editor.current(AvatarCategory::kHat) == kAvatarNone);
    // Face textures, features.
    CHECK(editor.choose(AvatarCategory::kFacialHair, kBeard) && editor.description().face[3] == kBeard);
    CHECK(!editor.choose(AvatarCategory::kEyes, kFemaleEyes));
    CHECK(editor.choose(AvatarCategory::kNose, kNose) && editor.description().features[1] == kNose);
    CHECK(editor.choose(AvatarCategory::kNose, kAvatarNone) && editor.description().features[1] == kAvatarNone);
    CHECK(!editor.choose(AvatarCategory::kMouth, kAvatarNone));
    // Colourways: the part's own colours, then one picked.
    CHECK(editor.colorways(AvatarCategory::kShirt) == 3 && editor.colorway(AvatarCategory::kShirt) == -1);
    editor.choose_colorway(AvatarCategory::kShirt, 2);
    CHECK(editor.colorway(AvatarCategory::kShirt) == 2);
    CHECK(editor.colorways(AvatarCategory::kHair) == 0);  // the hair colour instead
    // Accessories: several slots in one category.
    CHECK(editor.choose(AvatarCategory::kAccessories, kWatch) && Wears(editor.description(), kWatch));
    CHECK(!editor.choose(AvatarCategory::kAccessories, kEarrings));  // female only
    // Height and weight in steps of 0.1, within -1 to 1.
    editor.set_height(0.34f);
    editor.set_weight(-7);
    CHECK(editor.height() == 0.3f && editor.weight() == -1.0f);
    CHECK(avatar_description_valid(catalog, editor.description()));

    // A new gender: the body, and whatever doesn't fit becomes the default (the tee, the jeans, the
    // crew cut, the beard go; the sneakers, the watch and the colours stay).
    const uint32_t skin = editor.color(kAvatarSkin);
    editor.set_gender(2);
    const AvatarDescription& d = editor.description();
    std::string why;
    CHECK(d.body == 2 && Wears(d, kFemaleBody) && !Wears(d, kMaleBody));
    CHECK(Wears(d, kFemaleTop) && Wears(d, kSkirt) && Wears(d, kShoulderLength) && Wears(d, kSneakers) && Wears(d, kWatch));
    CHECK(!Wears(d, kMaleTee) && !Wears(d, kJeans) && !Wears(d, kCrewCut) && d.face[3] == kAvatarNone);
    CHECK(editor.color(kAvatarSkin) == skin);
    CHECK(avatar_description_valid(catalog, d, &why));

    // An outfit covers the top and the bottom; a bottom put on instead brings the default top back.
    CHECK(editor.choose(AvatarCategory::kShirt, kDress));
    CHECK(Wears(editor.description(), kDress) && !Wears(editor.description(), kSkirt) &&
          !Wears(editor.description(), kFemaleTop));
    CHECK(editor.current(AvatarCategory::kTrousers) == kAvatarNone);
    CHECK(avatar_description_valid(catalog, editor.description()));
    CHECK(editor.choose(AvatarCategory::kTrousers, kSkirt));
    CHECK(Wears(editor.description(), kSkirt) && Wears(editor.description(), kFemaleTop) &&
          !Wears(editor.description(), kDress));
    CHECK(avatar_description_valid(catalog, editor.description()));
    CHECK(editor.choose(AvatarCategory::kAccessories, kEarrings));
    editor.set_gender(1);
    CHECK(!Wears(editor.description(), kEarrings) && Wears(editor.description(), kMaleTee));
    CHECK(avatar_description_valid(catalog, editor.description()));
  }

  // Random avatars: valid, of both genders, and the same bytes back.
  {
    int genders[3] = {};
    bool all_valid = true, all_round_trip = true;
    for (uint32_t seed = 0; seed < 300; ++seed) {
      AvatarEditor editor(catalog, male);
      editor.randomize(seed);
      const AvatarDescription& d = editor.description();
      ++genders[d.body];
      std::string why;
      if (!avatar_description_valid(catalog, d, &why)) {
        all_valid = false;
        std::printf("  seed %u: %s\n", seed, why.c_str());
      }
      AvatarDescription back;
      if (!read_avatar_description(catalog, editor.bytes(), back) || !(back == d)) all_round_trip = false;
      // Random descriptions as the games make them are valid as they are.
      if (!avatar_description_valid(catalog, make_avatar_description(catalog, AvatarLook::kRandom, seed))) all_valid = false;
    }
    CHECK(all_valid && all_round_trip);
    CHECK(genders[1] > 50 && genders[2] > 50);
  }

  // ANYX360_AVATAR's syntax.
  {
    AvatarDescription d = male;
    customize_avatar(catalog, d, "1,8,22");
    CHECK(d.body == 2 && Wears(d, kFemaleBody) && Wears(d, kDress) && !Wears(d, kMaleTee) && !Wears(d, kJeans));
    CHECK(d.face[1] == kFemaleEyes);
  }

  std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
