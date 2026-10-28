// Avatar part models (the pack's model blocks, unpacked; see avatar.h and docs/AVATARS.md) and the
// structures the avatar library draws a part from, laid out as the library's own loader does
// (Kinect Sports 82A8ACB0) so the system's XamAvatarGetAssets can return them.
//
// A model file is read with a bit reader (bits LSB first out of little-endian bytes): a header of
// twelve u32s; per mesh a 33-byte header, its parameter records (24 bytes each), a vertex stream and
// an index stream; per texture its GPU offset and size, a 33-byte texture header and the data
// (none for a "blank" texture the system fills later); then "weld" blocks pairing vertices whose
// position and skinning must match across seams.
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace x360tool {

struct AvatarParam {
  uint32_t kind = 0;  // 1 texture binding, 2/3 a float4 constant
  uint32_t id = 0;
  // kind 1: texture index, UV set, flags; otherwise four u32 (floats).
  uint32_t values[4] = {};
};

struct AvatarMesh {
  uint32_t material = 0;  // 0 body/clothing, 1 hair, 2 glasses/earrings, 3 shades, 4 head
  uint32_t triangles = 0, vertex_count = 0, uv_sets = 0, stride = 0, unknown = 0;
  uint32_t vertex_offset = 0, index_offset = 0;  // from the part's GPU base
  std::vector<AvatarParam> params;
  std::vector<uint8_t> vertices;  // as the GPU reads them (big-endian), stride bytes each
  std::vector<uint16_t> indices;  // triangle list
};

struct AvatarTexture {
  uint32_t gpu_offset = 0;  // from the part's GPU base
  uint32_t format = 0, width = 0, height = 0, size = 0, slice_size = 0, slice_count = 0;
  bool blank = false, tiled = false;
  uint32_t row_bytes = 0, rows = 0;  // of the data, per slice
  std::span<const uint8_t> data;     // slice_count * rows * row_bytes, unless blank
};

struct AvatarModel {
  uint32_t header[12] = {};  // CPU size, GPU size, texture/vertex/index region sizes, mesh and
                             // texture counts, vertex and index region offsets, mesh and texture
                             // record offsets (CPU), tiling scratch size
  std::vector<AvatarMesh> meshes;
  std::vector<AvatarTexture> textures;
  std::vector<std::pair<uint32_t, uint32_t>> welds;  // (source, destination) vertex byte offsets

  uint32_t cpu_size() const { return header[0]; }
  uint32_t gpu_size() const { return header[1]; }
};

// Parses a model; `file` must outlive the model (texture data points into it). `left_handed`
// mirrors positions and normals in z as the library does for left-handed titles.
bool parse_avatar_model(std::span<const uint8_t> file, AvatarModel& out, std::string& error, bool left_handed = false);

// Writes one block of a (linear) texture slice's data at its GPU place; tiled textures need the
// GPU's tiled layout, which the caller provides (x and y in blocks, pitch in blocks).
using AvatarTileFn = std::function<uint32_t(uint32_t x, uint32_t y, uint32_t pitch_blocks, uint32_t block_bytes)>;

// Lays the part out as the library does: in `cpu` (cpu_size() bytes at guest address cpu_address)
// the texture records (44 bytes) and mesh records (512 bytes), in `gpu` (gpu_size() bytes at
// gpu_address) textures, vertices and indices; and the 52-byte part descriptor. Values are
// big-endian. Blank textures are left zero.
void build_avatar_part(const AvatarModel& model, std::span<uint8_t> cpu, uint32_t cpu_address, std::span<uint8_t> gpu,
                       uint32_t gpu_address, std::span<uint8_t, 52> descriptor, const AvatarTileFn& tile);

// Copies a texture's data (a texture block, or a model's texture) into GPU memory at `gpu`, as its
// header describes (pitch = slice size / rows for linear data).
void write_avatar_texture(const AvatarTexture& texture, std::span<uint8_t> gpu, const AvatarTileFn& tile);

// Parses a texture block (the pack's textures: a 33-byte texture header and the data).
bool parse_avatar_texture(std::span<const uint8_t> block, AvatarTexture& out, std::string& error);

// A shape override (the pack's block 4: face shapes such as chins, noses and ears, and the hiding and
// body modification templates clothes link to): triangles of the target part to hide and vertices
// of it to replace. Little-endian; two sections, each {count, the target's index (vertex) region
// size, the target's asset ID, 0, a bit width}: first `count` triangle numbers of that width, then a
// lattice vertex stream (as in models, without UVs) whose vertices each start with their byte offset
// in the target's vertex region.
struct AvatarShapeOverride {
  uint32_t target = 0;  // the target's pack entry (MaleBody, FemaleBody, Head)
  uint32_t index_bytes = 0, vertex_bytes = 0;  // the target's index and vertex region sizes
  std::vector<uint32_t> hidden_triangles;
  struct Vertex {
    uint32_t offset = 0;  // in the target's vertex region
    float position[3] = {};
    uint32_t fields[4] = {};  // normal, blend weights, blend indices, colour
  };
  std::vector<Vertex> vertices;
  // Whether it fits a model (its regions are the sizes it was made for).
  bool fits(const AvatarModel& m) const { return m.header[4] == index_bytes && m.header[3] == vertex_bytes; }
};
bool parse_avatar_shape_override(std::span<const uint8_t> block, AvatarShapeOverride& out, std::string& error,
                                 bool left_handed = false);
// Hides its triangles (made degenerate) and replaces its vertices (keeping their UVs) in a parsed
// model; returns how many it changed.
uint32_t apply_avatar_shape_override(AvatarModel& model, const AvatarShapeOverride& shape);

// The model as Wavefront OBJ (positions, normals, UVs of set 0, one group per mesh), for checking.
std::string avatar_model_obj(const AvatarModel& model);

}  // namespace x360tool
