// The avatar system's part of the avatar API (what xam does for XamAvatar*): avatars made from the
// console's avatar asset pack, which discs of avatar games carry. See docs/AVATARS.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace x360::avatar {

constexpr uint32_t kMetadataSize = 1000;  // XAVATAR_METADATA

// XamAvatarInitialize: the title's coordinate system (1 right-handed, else left-handed).
void Initialize(uint32_t coordinate_system);

// Whether avatars can be made: the title says how to build a skeleton
// (GameDescriptor::avatar_skeleton) and an avatar asset pack was found (the game's own, or one
// another game left in the shared system folder).
bool Available();

// Writes an avatar description, in the console's format: the default male or female avatar, or a
// random one. ANYX360_AVATAR changes the defaults (testing).
enum class Look { kMale, kFemale, kRandom };
void WriteMetadata(uint32_t metadata, Look look, uint32_t seed);

// ANYX360_AVATAR is set: the players' avatars are the defaults it changes, whatever their profiles
// have.
bool Overridden();

// The avatar a profile was given in the launcher's avatar editor (profile.json "avatar", decoded:
// kMetadataSize bytes); none if it has none (the defaults) or the profile doesn't exist.
std::optional<std::vector<uint8_t>> ProfileDescription(const std::filesystem::path& profiles_root, uint64_t xuid);

// What player `user_index` (0-3) gets from XamAvatarGetManifestLocalUser: the avatar of the profile
// signed in on the slot (`profile_xuid`; 0: none, or a development run's name), if it has one and
// ANYX360_AVATAR (`overridden`) isn't set; else the defaults, the male avatar for players 1 and 3
// and the female one for 2 and 4.
struct Manifest {
  std::optional<std::vector<uint8_t>> description;  // the profile's
  Look look = Look::kMale;                          // the default, without one
};
Manifest ChooseManifest(const std::filesystem::path& profiles_root, uint32_t user_index, uint64_t profile_xuid,
                        bool overridden);

// Writes a profile's description (kMetadataSize bytes) as it is; false (nothing written) if it names
// none of this pack's components.
bool WriteDescription(uint32_t metadata, std::span<const uint8_t> description);

// The body type (1 male, 2 female) of a description (1 if it names no body).
uint32_t BodyType(uint32_t metadata);

// Buffer sizes that suffice for any avatar with these components (XamAvatarGetAssetsResultSize).
void ResultSizes(uint32_t component_mask, uint32_t& result, uint32_t& gpu);

// Fills the result and GPU buffers (XamAvatarGetAssets) for the avatar's parts in the mask. Runs
// the title's skeleton builder, so it must be called on a guest thread.
bool BuildAssets(uint32_t metadata, uint32_t component_mask, uint32_t result, uint32_t result_size, uint32_t gpu,
                 uint32_t gpu_size, std::string& error);

}  // namespace x360::avatar
