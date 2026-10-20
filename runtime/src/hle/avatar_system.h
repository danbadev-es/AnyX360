// The avatar system's part of the avatar API (what xam does for XamAvatar*): avatars made from the
// console's avatar asset pack, which discs of avatar games carry. See docs/AVATARS.md.
#pragma once

#include <cstdint>
#include <string>

namespace x360::avatar {

constexpr uint32_t kMetadataSize = 1000;  // XAVATAR_METADATA

// XamAvatarInitialize: the title's coordinate system (1 right-handed, else left-handed).
void Initialize(uint32_t coordinate_system);

// Whether avatars can be made: the title says how to build a skeleton
// (GameDescriptor::avatar_skeleton) and an avatar asset pack was found (the game's own, or one
// another game left in the shared system folder).
bool Available();

// Writes an avatar description, in the console's format: the default male or female avatar, or a
// random one.
enum class Look { kMale, kFemale, kRandom };
void WriteMetadata(uint32_t metadata, Look look, uint32_t seed);

// The body type (1 male, 2 female) of a description (1 if it names no body).
uint32_t BodyType(uint32_t metadata);

// Buffer sizes that suffice for any avatar with these components (XamAvatarGetAssetsResultSize).
void ResultSizes(uint32_t component_mask, uint32_t& result, uint32_t& gpu);

// Fills the result and GPU buffers (XamAvatarGetAssets) for the avatar's parts in the mask. Runs
// the title's skeleton builder, so it must be called on a guest thread.
bool BuildAssets(uint32_t metadata, uint32_t component_mask, uint32_t result, uint32_t result_size, uint32_t gpu,
                 uint32_t gpu_size, std::string& error);

}  // namespace x360::avatar
