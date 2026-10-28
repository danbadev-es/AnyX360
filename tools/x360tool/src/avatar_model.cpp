// Avatar part models (see avatar_model.h).
#include "avatar_model.h"

#include <cmath>
#include <cstring>
#include <sstream>

namespace x360tool {

namespace {

// Bits LSB first out of little-endian bytes (the library's bit reader, Kinect Sports 82A8C620).
class BitReader {
 public:
  explicit BitReader(std::span<const uint8_t> data) : data_(data) {}
  // n <= 32; false past the end.
  bool get(uint64_t pos, uint32_t n, uint32_t& value) const {
    value = 0;
    if (n == 0) return true;
    if (pos + n > uint64_t(data_.size()) * 8) return false;
    uint64_t window = 0;
    const size_t first = size_t(pos >> 3);
    const size_t bytes = std::min<size_t>(data_.size() - first, 8);
    for (size_t i = 0; i < bytes; ++i) window |= uint64_t(data_[first + i]) << (8 * i);
    window >>= pos & 7;
    value = uint32_t(window & ((uint64_t(1) << n) - 1));
    return true;
  }

 private:
  std::span<const uint8_t> data_;
};

uint32_t le32(std::span<const uint8_t> d, size_t at) {
  return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}
void put32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
}
void put16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 8), p[1] = uint8_t(v); }
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t float_bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
float bits_float(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

int32_t sign_extend(uint32_t v, int bits) { return int32_t(v << (32 - bits)) >> (32 - bits); }

// The close-packed lattice positions are stored on (the library's static constants, set by its
// initialisers: 2, 2*sqrt(6)/3, sqrt(3)).
constexpr double kLattice[3] = {2.0, 1.6329931618554521, 1.7320508075688772};

struct Fail {
  std::string message;
};
[[noreturn]] void fail(const std::string& message) { throw Fail{message}; }

uint32_t need(const BitReader& r, uint64_t pos, uint32_t n) {
  uint32_t v;
  if (!r.get(pos, n, v)) fail("truncated");
  return v;
}

// A lattice vertex stream's header (without the count, which models store before it and shape
// overrides in their own header): scale, minimum, bits of each lattice coordinate, then base and
// bit width of four u32 fields (normal, blend weights, blend indices, colour). 402 bits.
struct Lattice {
  float scale = 0, minimum[3] = {};
  uint32_t n[3] = {}, base[4] = {}, nbits[4] = {};
  double step[3] = {};
  uint64_t vertex_bits() const { return uint64_t(n[0]) + n[1] + n[2] + nbits[0] + nbits[1] + nbits[2] + nbits[3]; }
};
constexpr uint32_t kLatticeHeaderBits = 402;

Lattice read_lattice(const BitReader& v, uint64_t at) {
  Lattice l;
  l.scale = bits_float(need(v, at, 32));
  for (int k = 0; k < 3; ++k) l.minimum[k] = bits_float(need(v, at + 32 + 32 * k, 32));
  for (int k = 0; k < 3; ++k) l.n[k] = need(v, at + 128 + 6 * k, 6);
  for (int k = 0; k < 4; ++k) {
    l.base[k] = need(v, at + 146 + 64 * k, 32);
    l.nbits[k] = need(v, at + 178 + 64 * k, 32);
    if (l.nbits[k] > 32) fail("field wider than 32 bits");
  }
  for (int k = 0; k < 3; ++k) l.step[k] = l.scale * kLattice[k];
  return l;
}

// One vertex at `q` (advanced past it): position and the four fields, mirrored in z if asked.
void read_lattice_vertex(const BitReader& v, uint64_t& q, const Lattice& l, bool left_handed, float position[3],
                         uint32_t field[4]) {
  const uint32_t qx = need(v, q, l.n[0]);
  q += l.n[0];
  const uint32_t qy = need(v, q, l.n[1]);
  q += l.n[1];
  const uint32_t qz = need(v, q, l.n[2]);
  q += l.n[2];
  for (int k = 0; k < 4; ++k) {
    field[k] = l.base[k] + need(v, q, l.nbits[k]);
    q += l.nbits[k];
  }
  position[1] = float(qy * l.step[1] + l.minimum[1]);
  position[2] = float(qz * l.step[2] + l.minimum[2] + ((qy & 1) ? l.step[2] / 3 : 0.0));
  position[0] = float(qx * l.step[0] + l.minimum[0] + (((qy ^ qz) & 1) ? l.step[0] / 2 : 0.0));
  if (left_handed) {
    position[2] = -position[2];
    const int32_t nz10 = sign_extend(field[0] >> 22, 10);
    field[0] = (field[0] & 0x3FFFFF) | (uint32_t(-nz10) & 0x3FF) << 22;
  }
}

uint32_t texture_block_bytes(uint32_t format) {
  switch (format & 0x3F) {
    case 0x12:  // DXT1
      return 8;
    case 0x13:  // DXT2/3
    case 0x14:  // DXT4/5
      return 16;
    default:
      return 0;
  }
}

void parse_texture_header(std::span<const uint8_t> d, size_t at, AvatarTexture& t) {
  if (at + 33 > d.size()) fail("texture header truncated");
  BitReader r(d.subspan(at, 33));
  t.format = need(r, 0, 32);
  t.width = need(r, 32, 32);
  t.height = need(r, 64, 32);
  t.size = need(r, 96, 32);
  t.slice_size = need(r, 128, 32);
  t.slice_count = need(r, 160, 32);
  t.blank = need(r, 192, 1) != 0;
  t.tiled = need(r, 193, 1) != 0;
  t.row_bytes = need(r, 194, 32);
  t.rows = need(r, 226, 32);
}

}  // namespace

bool parse_avatar_model(std::span<const uint8_t> d, AvatarModel& out, std::string& error, bool left_handed) {
  out = {};
  try {
    if (d.size() < 48) fail("header truncated");
    for (int i = 0; i < 12; ++i) out.header[i] = le32(d, i * 4);
    size_t p = 48;
    for (uint32_t m = 0; m < out.header[5]; ++m) {
      if (p + 33 > d.size()) fail("mesh header truncated");
      BitReader h(d.subspan(p, 33));
      AvatarMesh mesh;
      mesh.material = need(h, 0, 32);
      const uint32_t params = need(h, 32, 5);
      mesh.triangles = need(h, 37, 32);
      mesh.vertex_count = need(h, 69, 32);
      mesh.uv_sets = need(h, 101, 32);
      mesh.stride = need(h, 133, 32);
      mesh.unknown = need(h, 165, 32);
      mesh.vertex_offset = need(h, 197, 32);
      mesh.index_offset = need(h, 229, 32);
      p += 33;
      if (params > 20) fail("more than 20 parameters");
      if (mesh.stride != 28 + 4 * mesh.uv_sets) fail("unexpected vertex stride");
      for (uint32_t i = 0; i < params; ++i, p += 24) {
        if (p + 24 > d.size()) fail("parameter truncated");
        BitReader r(d.subspan(p, 24));
        AvatarParam param;
        param.kind = need(r, 0, 32);
        param.id = need(r, 32, 32);
        if (param.kind == 1) {
          param.values[0] = need(r, 64, 16);
          param.values[1] = need(r, 80, 16);
          param.values[2] = need(r, 96, 32);
        } else {
          for (int k = 0; k < 4; ++k) param.values[k] = need(r, 64 + 32 * k, 32);
        }
        mesh.params.push_back(param);
      }

      // Vertices: a 434-bit header, then each vertex's lattice position, four u32 fields (normal,
      // blend weights, blend indices, colour) as base + n bits, and uv_sets half2 coordinates.
      BitReader v(d.subspan(p));
      const uint32_t count = need(v, 0, 32);
      const Lattice lattice = read_lattice(v, 32);
      if (count != mesh.vertex_count) fail("vertex count mismatch");
      const uint64_t bits = lattice.vertex_bits() + 32ull * mesh.uv_sets;
      mesh.vertices.resize(size_t(count) * mesh.stride);
      uint64_t pos = 32 + kLatticeHeaderBits;
      for (uint32_t i = 0; i < count; ++i, pos += bits) {
        uint64_t q = pos;
        float position[3];
        uint32_t field[4];
        read_lattice_vertex(v, q, lattice, left_handed, position, field);
        uint8_t* out_v = &mesh.vertices[size_t(i) * mesh.stride];
        for (int k = 0; k < 3; ++k) put32(out_v + 4 * k, float_bits(position[k]));
        for (int k = 0; k < 4; ++k) put32(out_v + 12 + 4 * k, field[k]);
        for (uint32_t s = 0; s < mesh.uv_sets; ++s, q += 32) {
          put16(out_v + 28 + 4 * s, need(v, q, 16));
          put16(out_v + 30 + 4 * s, need(v, q + 16, 16));
        }
      }
      p += size_t((434 + uint64_t(count) * bits + 7) / 8);

      // Indices: count, base, bits, then each index as base + bits.
      if (p + 8 > d.size()) fail("index header truncated");
      const uint32_t index_count = le32(d, p);
      const uint32_t index_base = uint32_t(d[p + 4]) | uint32_t(d[p + 5]) << 8;
      const uint32_t index_bits = uint32_t(d[p + 6]) | uint32_t(d[p + 7]) << 8;
      if (index_count != mesh.triangles * 3 || index_bits > 16) fail("bad index stream");
      BitReader ix(d.subspan(p + 8));
      mesh.indices.resize(index_count);
      for (uint32_t i = 0; i < index_count; ++i) {
        mesh.indices[i] = uint16_t(index_base + need(ix, uint64_t(i) * index_bits, index_bits));
        if (mesh.indices[i] >= count) fail("index out of range");
      }
      p += 8 + size_t((uint64_t(index_count) * index_bits + 7) / 8);
      out.meshes.push_back(std::move(mesh));
    }

    for (uint32_t t = 0; t < out.header[6]; ++t) {
      if (p + 8 > d.size()) fail("texture truncated");
      AvatarTexture texture;
      texture.gpu_offset = le32(d, p);
      p += 8;
      parse_texture_header(d, p, texture);
      p += 33;
      if (!texture.blank) {
        const size_t bytes = size_t(texture.slice_count) * texture.rows * texture.row_bytes;
        if (p + bytes > d.size()) fail("texture data truncated");
        texture.data = d.subspan(p, bytes);
        p += bytes;
      }
      out.textures.push_back(texture);
    }

    // Welds: blocks of count, base, bits, then the values (byte offsets in the vertex region, in
    // source/destination pairs); a block with an odd count ends the list.
    std::vector<uint32_t> values;
    for (;;) {
      if (p + 12 > d.size()) fail("weld list not terminated");
      const uint32_t count = le32(d, p), base = le32(d, p + 4), bits = le32(d, p + 8);
      if (bits > 32) fail("bad weld block");
      BitReader r(d.subspan(p + 12));
      for (uint32_t i = 0; i < count; ++i) values.push_back(base + need(r, uint64_t(i) * bits, bits));
      p += 12 + size_t((uint64_t(count) * bits + 7) / 8);
      if (count & 1) break;
    }
    for (size_t i = 0; i + 1 < values.size(); i += 2) out.welds.emplace_back(values[i], values[i + 1]);
    const uint32_t vertex_region = out.header[3];
    for (const auto& [src, dst] : out.welds) {
      if (src + 24 > vertex_region || dst + 24 > vertex_region) fail("weld outside the vertices");
    }
  } catch (const Fail& f) {
    error = f.message;
    return false;
  }
  return true;
}

void write_avatar_texture(const AvatarTexture& t, std::span<uint8_t> gpu, const AvatarTileFn& tile) {
  if (t.blank || t.data.empty() || t.rows == 0) return;
  const uint32_t block_bytes = texture_block_bytes(t.format);
  for (uint32_t s = 0; s < t.slice_count; ++s) {
    const uint8_t* src = t.data.data() + size_t(s) * t.rows * t.row_bytes;
    uint8_t* dst = gpu.data() + size_t(s) * t.slice_size;
    if (size_t(s + 1) * t.slice_size > gpu.size()) return;
    if (t.tiled && block_bytes && tile) {
      const uint32_t pitch_blocks = ((t.width + 3) / 4 + 31) & ~31u;
      const uint32_t blocks_per_row = t.row_bytes / block_bytes;
      for (uint32_t y = 0; y < t.rows; ++y) {
        for (uint32_t x = 0; x < blocks_per_row; ++x) {
          const uint32_t offset = tile(x, y, pitch_blocks, block_bytes);
          if (offset + block_bytes <= t.slice_size)
            std::memcpy(dst + offset, src + size_t(y) * t.row_bytes + x * block_bytes, block_bytes);
        }
      }
    } else {
      const uint32_t pitch = t.slice_size / t.rows;
      for (uint32_t y = 0; y < t.rows; ++y)
        std::memcpy(dst + size_t(y) * pitch, src + size_t(y) * t.row_bytes, std::min(t.row_bytes, pitch));
    }
  }
}

void build_avatar_part(const AvatarModel& m, std::span<uint8_t> cpu, uint32_t cpu_address, std::span<uint8_t> gpu,
                       uint32_t gpu_address, std::span<uint8_t, 52> descriptor, const AvatarTileFn& tile) {
  std::memset(descriptor.data(), 0, 52);
  for (int i = 0; i < 7; ++i) put32(&descriptor[i * 4], m.header[i]);
  put32(&descriptor[28], cpu_address);
  put32(&descriptor[32], gpu_address);
  put32(&descriptor[36], gpu_address + m.header[7]);
  put32(&descriptor[40], gpu_address + m.header[8]);
  put32(&descriptor[44], cpu_address + m.header[9]);
  put32(&descriptor[48], cpu_address + m.header[10]);

  for (size_t i = 0; i < m.textures.size(); ++i) {
    const AvatarTexture& t = m.textures[i];
    uint8_t* r = cpu.data() + m.header[10] + i * 44;
    if (m.header[10] + (i + 1) * 44 > cpu.size()) break;
    const uint32_t fields[11] = {t.format, t.width, t.height, t.size, 0, t.slice_size, 0, 1, t.slice_count,
                                 gpu_address + t.gpu_offset, 0};
    for (int k = 0; k < 11; ++k) put32(r + k * 4, fields[k]);
    if (t.gpu_offset + t.size <= gpu.size()) write_avatar_texture(t, gpu.subspan(t.gpu_offset, t.size), tile);
  }

  for (size_t i = 0; i < m.meshes.size(); ++i) {
    const AvatarMesh& mesh = m.meshes[i];
    if (m.header[9] + (i + 1) * 512 > cpu.size()) break;
    uint8_t* r = cpu.data() + m.header[9] + i * 512;
    put32(r, mesh.material);
    for (size_t k = 0; k < mesh.params.size(); ++k) {
      uint8_t* slot = r + 4 + k * 24;
      const AvatarParam& p = mesh.params[k];
      put32(slot, p.kind);
      put32(slot + 4, p.id);
      if (p.kind == 1) {
        put16(slot + 8, p.values[0]);
        put16(slot + 10, p.values[1]);
        put32(slot + 12, p.values[2]);
      } else {
        for (int c = 0; c < 4; ++c) put32(slot + 8 + c * 4, p.values[c]);
      }
    }
    put32(r + 484, mesh.triangles);
    put32(r + 488, mesh.vertex_count);
    put32(r + 492, mesh.uv_sets);
    put32(r + 496, mesh.stride);
    put32(r + 500, mesh.unknown);
    put32(r + 504, gpu_address + mesh.vertex_offset);
    put32(r + 508, gpu_address + mesh.index_offset);
    if (mesh.vertex_offset + mesh.vertices.size() <= gpu.size())
      std::memcpy(gpu.data() + mesh.vertex_offset, mesh.vertices.data(), mesh.vertices.size());
    if (mesh.index_offset + mesh.indices.size() * 2 <= gpu.size())
      for (size_t k = 0; k < mesh.indices.size(); ++k) put16(gpu.data() + mesh.index_offset + k * 2, mesh.indices[k]);
  }

  // Seams: each destination vertex takes its source's position and skinning.
  uint8_t* vertices = gpu.data() + m.header[7];
  for (const auto& [src, dst] : m.welds) {
    if (m.header[7] + std::max(src, dst) + 24 > gpu.size()) continue;
    std::memcpy(vertices + dst, vertices + src, 12);
    std::memcpy(vertices + dst + 16, vertices + src + 16, 8);
  }
}

bool parse_avatar_texture(std::span<const uint8_t> block, AvatarTexture& out, std::string& error) {
  out = {};
  try {
    parse_texture_header(block, 0, out);
    if (!out.blank) {
      const size_t bytes = size_t(out.slice_count) * out.rows * out.row_bytes;
      if (33 + bytes > block.size()) fail("texture data truncated");
      out.data = block.subspan(33, bytes);
    }
  } catch (const Fail& f) {
    error = f.message;
    return false;
  }
  return true;
}

bool parse_avatar_shape_override(std::span<const uint8_t> d, AvatarShapeOverride& out, std::string& error,
                                 bool left_handed) {
  out = {};
  try {
    // Two sections, each {count, the target's index (or vertex) region size, the target's asset ID,
    // 0, a bit width}: triangles to hide, then vertices to replace.
    auto section_header = [&](size_t at, uint32_t& count, uint32_t& region, uint32_t& target, uint32_t& bits) {
      if (at + 32 > d.size()) fail("shape override truncated");
      count = le32(d, at);
      region = le32(d, at + 4);
      target = uint32_t(d[at + 12]) | uint32_t(d[at + 13]) << 8;  // the asset ID's entry (LE words)
      bits = le32(d, at + 28);
      if (bits > 32 || (count && bits == 0)) fail("bad shape override section");
    };
    uint32_t count, bits;
    section_header(0, count, out.index_bytes, out.target, bits);
    BitReader t(d.subspan(32));
    for (uint32_t i = 0; i < count; ++i) out.hidden_triangles.push_back(need(t, uint64_t(i) * bits, bits));
    size_t p = 32 + size_t((uint64_t(count) * bits + 7) / 8);
    uint32_t vertex_target;
    section_header(p, count, out.vertex_bytes, vertex_target, bits);
    p += 32;
    BitReader v(d.subspan(p));
    const Lattice lattice = read_lattice(v, 0);
    uint64_t q = kLatticeHeaderBits;
    for (uint32_t i = 0; i < count; ++i) {
      AvatarShapeOverride::Vertex vertex;
      vertex.offset = need(v, q, bits);
      q += bits;
      read_lattice_vertex(v, q, lattice, left_handed, vertex.position, vertex.fields);
      out.vertices.push_back(vertex);
    }
  } catch (const Fail& f) {
    error = f.message;
    return false;
  }
  return true;
}

uint32_t apply_avatar_shape_override(AvatarModel& model, const AvatarShapeOverride& shape) {
  uint32_t changed = 0;
  // Triangles are numbered through the part's index region (its meshes in order of their indices).
  const uint32_t index_region = model.header[8];
  for (uint32_t triangle : shape.hidden_triangles) {
    const uint32_t at = index_region + triangle * 6;
    for (auto& mesh : model.meshes) {
      if (at < mesh.index_offset || at + 6 > mesh.index_offset + mesh.indices.size() * 2) continue;
      const size_t first = (at - mesh.index_offset) / 2;
      mesh.indices[first + 1] = mesh.indices[first + 2] = mesh.indices[first];  // degenerate: not drawn
      ++changed;
      break;
    }
  }
  // Vertices are byte offsets in the vertex region; a replacement keeps the vertex's UVs.
  const uint32_t vertex_region = model.header[7];
  for (const auto& vertex : shape.vertices) {
    const uint32_t at = vertex_region + vertex.offset;
    for (auto& mesh : model.meshes) {
      if (at < mesh.vertex_offset || at >= mesh.vertex_offset + mesh.vertices.size()) continue;
      const uint32_t rel = at - mesh.vertex_offset;
      if (rel % mesh.stride) break;
      uint8_t* out = &mesh.vertices[rel];
      for (int k = 0; k < 3; ++k) put32(out + 4 * k, float_bits(vertex.position[k]));
      for (int k = 0; k < 4; ++k) put32(out + 12 + 4 * k, vertex.fields[k]);
      ++changed;
      break;
    }
  }
  return changed;
}

std::string avatar_model_obj(const AvatarModel& model) {
  std::ostringstream o;
  auto half = [](uint32_t h) {
    const uint32_t sign = (h >> 15) & 1, exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
    float value = exponent == 0 ? std::ldexp(float(mantissa), -24) : std::ldexp(float(mantissa | 0x400), int(exponent) - 25);
    return sign ? -value : value;
  };
  size_t first = 1;
  for (size_t m = 0; m < model.meshes.size(); ++m) {
    const AvatarMesh& mesh = model.meshes[m];
    o << "g mesh" << m << "_material" << mesh.material << "\n";
    for (uint32_t i = 0; i < mesh.vertex_count; ++i) {
      const uint8_t* v = &mesh.vertices[size_t(i) * mesh.stride];
      o << "v " << bits_float(get32(v)) << " " << bits_float(get32(v + 4)) << " " << bits_float(get32(v + 8)) << "\n";
      const uint32_t n = get32(v + 12);
      o << "vn " << sign_extend(n & 0x7FF, 11) / 1023.0 << " " << sign_extend((n >> 11) & 0x7FF, 11) / 1023.0 << " "
        << sign_extend(n >> 22, 10) / 511.0 << "\n";
      if (mesh.uv_sets) o << "vt " << half(uint32_t(v[28]) << 8 | v[29]) << " " << 1.0f - half(uint32_t(v[30]) << 8 | v[31]) << "\n";
    }
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
      o << "f";
      for (int k = 0; k < 3; ++k) {
        const size_t index = first + mesh.indices[i + k];
        o << " " << index << "/" << (mesh.uv_sets ? std::to_string(index) : "") << "/" << index;
      }
      o << "\n";
    }
    first += mesh.vertex_count;
  }
  return o.str();
}

}  // namespace x360tool
