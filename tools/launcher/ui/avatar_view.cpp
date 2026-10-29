// The avatar viewer (see avatar_view.h): Direct3D 11, offscreen, multisampled, read back to BGRA.
//
// Each mesh of a built part is drawn as the games' avatar shaders draw it (docs/AVATARS.md):
//   - body, clothes, hair and glasses: lerp(texture 1, c22 x mask.r + c23 x mask.g + c24 x mask.b,
//     mask.a), mask = texture 2, then texture 3 laid over by its alpha;
//   - the head: the skin (c13) under six layers, each its texture's red times its colour, green white
//     and blue the skin, laid over by its alpha: face paint (c20, c21), facial hair (c19), the mouth
//     (c15), the eyes (c16), eye shadow (c18), the eyebrows (c17); the face textures' first frame
//     (the eyes open, the mouth closed);
//   - lenses (material 3) over the rest, half transparent.
// Textures are the pack's DXT1/DXT3/DXT5 blocks (8-in-16 endian) uploaded as BC1/BC2/BC3. Height
// and weight are the skeleton's work in the games; here the avatar is scaled (taller, wider below
// the neck) to show them.
#include "ui/avatar_view.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace anyx360::launcher::ui {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {

constexpr int kUvSets = 6;
constexpr int kSlots = 6;
constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

const char kShader[] = R"(
cbuffer Frame : register(b0) {
  float4x4 view_proj;
  float4 light;      // xyz: towards the light
  float4 shape;      // x: height scale, y: weight scale, z: the neck's y, w: the feet's y
  float4 turn;       // x: cos, y: sin of the yaw
};
cbuffer Mesh : register(b1) {
  float4 colors[16];  // constants 13-28
  int4 uv_set[2];     // per texture slot (-1: none)
  int4 wrap[2];       // per texture slot: 1 wrap, 0 clamp
  int4 mode;          // x: 0 body/clothes/hair, 1 head, 2 lens; y: alpha test
};
Texture2D slots[6] : register(t0);
SamplerState wrap_sampler : register(s0);
SamplerState clamp_sampler : register(s1);

struct VsIn {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float2 uv[6] : TEXCOORD0;
};
struct VsOut {
  float4 position : SV_Position;
  float3 normal : NORMAL;
  float2 uv[6] : TEXCOORD0;
};

VsOut vs_main(VsIn i) {
  VsOut o;
  float3 p = i.position;
  // Weight: wider below the neck (smoothly), height: taller from the feet.
  float below = saturate((shape.z - p.y) / 0.08);
  p.xz *= 1.0 + (shape.y - 1.0) * below;
  p.y = shape.w + (p.y - shape.w) * shape.x;
  float3 r = float3(p.x * turn.x + p.z * turn.y, p.y, -p.x * turn.y + p.z * turn.x);
  float3 n = float3(i.normal.x * turn.x + i.normal.z * turn.y, i.normal.y, -i.normal.x * turn.y + i.normal.z * turn.x);
  o.position = mul(float4(r, 1), view_proj);
  o.normal = n;
  [unroll] for (int k = 0; k < 6; ++k) o.uv[k] = i.uv[k];
  return o;
}

float4 Sample(VsOut i, int k) {
  int set = uv_set[k / 4][k % 4];
  if (set < 0) return float4(0, 0, 0, 0);
  float2 uv = i.uv[0];
  [unroll] for (int s = 1; s < 6; ++s) if (set == s) uv = i.uv[s];
  return wrap[k / 4][k % 4] ? slots[k].Sample(wrap_sampler, uv) : slots[k].Sample(clamp_sampler, uv);
}

float3 Layer(float3 under, float4 t, float3 c, float3 c2, float3 skin) {
  float3 layer = t.r * c + t.g * c2 + t.b * skin;
  return lerp(under, layer, t.a);
}

float4 ps_main(VsOut i) : SV_Target {
  float3 c;
  float alpha = 1;
  if (mode.x == 1) {
    float3 skin = colors[0].rgb;
    float3 white = float3(1, 1, 1);
    c = skin;
    c = Layer(c, Sample(i, 0), colors[7].rgb, colors[8].rgb, skin);  // face paint
    c = Layer(c, Sample(i, 1), colors[6].rgb, white, skin);          // facial hair
    c = Layer(c, Sample(i, 2), colors[2].rgb, white, skin);          // mouth
    c = Layer(c, Sample(i, 3), colors[3].rgb, white, skin);          // eyes
    c = Layer(c, Sample(i, 4), colors[5].rgb, white, skin);          // eye shadow
    c = Layer(c, Sample(i, 5), colors[4].rgb, white, skin);          // eyebrows
  } else {
    float4 base = uv_set[0].x >= 0 ? Sample(i, 0) : float4(1, 1, 1, 1);
    float4 mask = Sample(i, 1);
    float4 over = Sample(i, 2);
    float3 tint = colors[9].rgb * mask.r + colors[10].rgb * mask.g + colors[11].rgb * mask.b;
    c = lerp(base.rgb, tint, mask.a);
    c = lerp(c, over.rgb, over.a);
    if (mode.y != 0 && base.a < 0.4) discard;
    if (mode.x == 2) alpha = 0.55;
  }
  float3 n = normalize(i.normal);
  float diffuse = saturate(dot(n, normalize(light.xyz)));
  float fill = saturate(dot(n, normalize(float3(-0.6, 0.2, 0.5)))) * 0.25;
  float3 lit = c * (0.42 + 0.62 * diffuse + fill);
  return float4(lit * alpha, alpha);
}
)";

struct Vertex {
  float position[3];
  float normal[3];
  float uv[kUvSets][2];
};

struct FrameConstants {
  XMFLOAT4X4 view_proj;
  float light[4];
  float shape[4];
  float turn[4];
};

struct MeshConstants {
  float colors[16][4];
  int32_t uv_set[8];
  int32_t wrap[8];
  int32_t mode[4];
};

struct Mesh {
  ComPtr<ID3D11Buffer> vertices, indices, constants;
  uint32_t index_count = 0;
  uint32_t material = 0;
  ComPtr<ID3D11ShaderResourceView> slots[kSlots];
};

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
float BeFloat(const uint8_t* p) {
  const uint32_t bits = Be32(p);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
int32_t SignExtend(uint32_t v, int bits) { return int32_t(v << (32 - bits)) >> (32 - bits); }
float Half(uint16_t h) {
  const uint32_t sign = (h >> 15) & 1, exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
  const float value = exponent == 0 ? std::ldexp(float(mantissa), -24) : std::ldexp(float(mantissa | 0x400), int(exponent) - 25);
  return sign ? -value : value;
}

float ParamFloat(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

}  // namespace

struct AvatarRenderer::Impl {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11PixelShader> ps;
  ComPtr<ID3D11InputLayout> layout;
  ComPtr<ID3D11Buffer> frame;
  ComPtr<ID3D11SamplerState> wrap, clamp;
  ComPtr<ID3D11RasterizerState> raster;
  ComPtr<ID3D11DepthStencilState> depth_on, depth_test_only;
  ComPtr<ID3D11BlendState> opaque, blend;
  ComPtr<ID3D11ShaderResourceView> empty;
  // The targets, for the last size drawn.
  uint32_t width = 0, height = 0;
  ComPtr<ID3D11Texture2D> color, resolved, staging, depth;
  ComPtr<ID3D11RenderTargetView> rtv;
  ComPtr<ID3D11DepthStencilView> dsv;
  UINT samples = 1;

  std::vector<Mesh> meshes;
  float min[3] = {}, max[3] = {}, head_min[3] = {}, head_max[3] = {};
  bool has_head = false;
  float height_scale = 1, weight_scale = 1;
  bool failed = false;

  bool Init(std::string& error) {
    if (device) return true;
    if (failed) {
      error = "Direct3D 11 isn't available";
      return false;
    }
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   UINT(std::size(levels)), D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr))
      hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                             UINT(std::size(levels)), D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr)) {
      failed = true;
      error = "Direct3D 11 isn't available";
      return false;
    }
    ComPtr<ID3DBlob> vs_code, ps_code, messages;
    const char* target_vs = device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0 ? "vs_5_0" : "vs_4_0";
    const char* target_ps = device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0 ? "ps_5_0" : "ps_4_0";
    if (FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "avatar", nullptr, nullptr, "vs_main", target_vs,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs_code, &messages)) ||
        FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "avatar", nullptr, nullptr, "ps_main", target_ps,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps_code, &messages))) {
      error = messages ? std::string(static_cast<const char*>(messages->GetBufferPointer()), messages->GetBufferSize())
                       : "the avatar shaders don't compile";
      device.Reset();
      failed = true;
      return false;
    }
    device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vs);
    device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &ps);
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0}};
    for (UINT k = 0; k < kUvSets; ++k)
      elements.push_back({"TEXCOORD", k, DXGI_FORMAT_R32G32_FLOAT, 0, UINT(offsetof(Vertex, uv) + 8 * k),
                          D3D11_INPUT_PER_VERTEX_DATA, 0});
    device->CreateInputLayout(elements.data(), UINT(elements.size()), vs_code->GetBufferPointer(), vs_code->GetBufferSize(),
                              &layout);
    D3D11_BUFFER_DESC cb{sizeof(FrameConstants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    device->CreateBuffer(&cb, nullptr, &frame);
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sampler, &wrap);
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampler, &clamp);
    D3D11_RASTERIZER_DESC raster_desc{};
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_NONE;
    raster_desc.DepthClipEnable = TRUE;
    raster_desc.MultisampleEnable = TRUE;
    device->CreateRasterizerState(&raster_desc, &raster);
    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    device->CreateDepthStencilState(&ds, &depth_on);
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    device->CreateDepthStencilState(&ds, &depth_test_only);
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&bd, &opaque);
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;  // premultiplied
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    device->CreateBlendState(&bd, &blend);
    // An unused slot: transparent.
    const uint32_t clear = 0;
    D3D11_TEXTURE2D_DESC td{1, 1, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE};
    D3D11_SUBRESOURCE_DATA data{&clear, 4, 4};
    ComPtr<ID3D11Texture2D> texture;
    device->CreateTexture2D(&td, &data, &texture);
    device->CreateShaderResourceView(texture.Get(), nullptr, &empty);
    for (UINT count : {8u, 4u, 2u}) {
      UINT quality = 0;
      if (SUCCEEDED(device->CheckMultisampleQualityLevels(kColorFormat, count, &quality)) && quality > 0) {
        samples = count;
        break;
      }
    }
    return vs && ps && layout && frame;
  }

  // A texture of the part (its first slice), as a BC texture.
  ComPtr<ID3D11ShaderResourceView> Texture(const x360tool::AvatarTexture& t) {
    if (t.blank || t.data.empty() || !t.rows || !t.width || !t.height) return nullptr;
    DXGI_FORMAT format;
    uint32_t block_bytes;
    switch (t.format & 0x3F) {
      case 0x12: format = DXGI_FORMAT_BC1_UNORM, block_bytes = 8; break;
      case 0x13: format = DXGI_FORMAT_BC2_UNORM, block_bytes = 16; break;
      case 0x14: format = DXGI_FORMAT_BC3_UNORM, block_bytes = 16; break;
      default: return nullptr;
    }
    const uint32_t blocks_w = (t.width + 3) / 4, blocks_h = (t.height + 3) / 4;
    const uint32_t pitch = blocks_w * block_bytes;
    std::vector<uint8_t> blocks(size_t(pitch) * blocks_h, 0);
    for (uint32_t y = 0; y < blocks_h && y < t.rows; ++y) {
      const uint8_t* src = t.data.data() + size_t(y) * t.row_bytes;
      uint8_t* dst = blocks.data() + size_t(y) * pitch;
      const uint32_t n = std::min(pitch, t.row_bytes) & ~1u;
      for (uint32_t i = 0; i < n; i += 2) dst[i] = src[i + 1], dst[i + 1] = src[i];  // 8-in-16
    }
    D3D11_TEXTURE2D_DESC td{t.width, t.height, 1, 1, format, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE};
    D3D11_SUBRESOURCE_DATA data{blocks.data(), pitch, UINT(blocks.size())};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(device->CreateTexture2D(&td, &data, &texture)) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)))
      return nullptr;
    return view;
  }

  bool Upload(const x360tool::AvatarBuiltPart& built, std::string& error) {
    const x360tool::AvatarModel& model = built.model;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures;
    for (const auto& t : model.textures) textures.push_back(Texture(t));
    // Seams: each weld's destination vertex takes its source's position (as the system does when it
    // lays a part out). Offsets are in the part's vertex region.
    std::vector<std::vector<uint8_t>> vertices;
    for (const auto& mesh : model.meshes) vertices.push_back(mesh.vertices);
    auto locate = [&](uint32_t offset) -> uint8_t* {
      const uint32_t at = model.header[7] + offset;
      for (size_t m = 0; m < model.meshes.size(); ++m) {
        const auto& mesh = model.meshes[m];
        if (at >= mesh.vertex_offset && at + 12 <= mesh.vertex_offset + mesh.vertices.size())
          return vertices[m].data() + (at - mesh.vertex_offset);
      }
      return nullptr;
    };
    for (const auto& [src, dst] : model.welds) {
      const uint8_t* from = locate(src);
      uint8_t* to = locate(dst);
      if (from && to) std::memcpy(to, from, 12);
    }
    const bool head = built.part->slot == x360tool::kAvatarHead;
    for (size_t m = 0; m < model.meshes.size(); ++m) {
      const auto& mesh = model.meshes[m];
      if (mesh.indices.empty() || !mesh.vertex_count) continue;
      std::vector<Vertex> out(mesh.vertex_count);
      for (uint32_t i = 0; i < mesh.vertex_count; ++i) {
        const uint8_t* v = vertices[m].data() + size_t(i) * mesh.stride;
        Vertex& o = out[i];
        for (int k = 0; k < 3; ++k) o.position[k] = BeFloat(v + 4 * k);
        const uint32_t n = Be32(v + 12);
        o.normal[0] = SignExtend(n & 0x7FF, 11) / 1023.0f;
        o.normal[1] = SignExtend((n >> 11) & 0x7FF, 11) / 1023.0f;
        o.normal[2] = SignExtend(n >> 22, 10) / 511.0f;
        for (uint32_t s = 0; s < kUvSets; ++s) {
          if (s < mesh.uv_sets) {
            o.uv[s][0] = Half(uint16_t(v[28 + 4 * s] << 8 | v[29 + 4 * s]));
            o.uv[s][1] = Half(uint16_t(v[30 + 4 * s] << 8 | v[31 + 4 * s]));
          } else {
            o.uv[s][0] = o.uv[s][1] = 0;
          }
        }
        for (int k = 0; k < 3; ++k) {
          min[k] = std::min(min[k], o.position[k]);
          max[k] = std::max(max[k], o.position[k]);
          if (head) {
            head_min[k] = std::min(head_min[k], o.position[k]);
            head_max[k] = std::max(head_max[k], o.position[k]);
          }
        }
      }
      has_head |= head;
      Mesh gpu;
      gpu.material = mesh.material;
      gpu.index_count = uint32_t(mesh.indices.size());
      D3D11_BUFFER_DESC vb{UINT(out.size() * sizeof(Vertex)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER};
      D3D11_SUBRESOURCE_DATA vd{out.data()};
      D3D11_BUFFER_DESC ib{UINT(mesh.indices.size() * 2), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER};
      D3D11_SUBRESOURCE_DATA id{mesh.indices.data()};
      if (FAILED(device->CreateBuffer(&vb, &vd, &gpu.vertices)) || FAILED(device->CreateBuffer(&ib, &id, &gpu.indices))) {
        error = "out of video memory";
        return false;
      }
      MeshConstants c{};
      for (int k = 0; k < 16; ++k) c.colors[k][0] = c.colors[k][1] = c.colors[k][2] = c.colors[k][3] = 0;
      for (int k = 0; k < 8; ++k) c.uv_set[k] = -1, c.wrap[k] = 0;
      for (const auto& p : mesh.params) {
        if (p.kind != 1) {
          if (p.id >= 13 && p.id <= 28)
            for (int k = 0; k < 4; ++k) c.colors[p.id - 13][k] = ParamFloat(p.values[k]);
          continue;
        }
        // Which slot the texture is in: the clothes' 1-3; the head's layers by their ids.
        int slot = -1;
        if (head) {
          switch (p.id) {
            case 5: slot = 0; break;            // face paint
            case 6: slot = 1; break;            // facial hair
            case 12: slot = 2; break;           // mouth
            case 9: case 10: slot = 3; break;   // eyes
            case 11: slot = 4; break;           // eye shadow
            case 7: case 8: slot = 5; break;    // eyebrows
          }
        } else if (p.id >= 1 && p.id <= 3) {
          slot = int(p.id) - 1;
        }
        if (slot < 0 || p.values[1] >= uint32_t(kUvSets) || p.values[0] >= textures.size()) continue;
        if (!textures[p.values[0]]) continue;
        gpu.slots[slot] = textures[p.values[0]];
        c.uv_set[slot] = int32_t(p.values[1]);
        c.wrap[slot] = (p.values[2] & 3) ? 1 : 0;
      }
      c.mode[0] = head ? 1 : mesh.material == 3 ? 2 : 0;
      c.mode[1] = mesh.material == 1 ? 1 : 0;  // hair: its strands' alpha
      D3D11_BUFFER_DESC cb{sizeof(MeshConstants), D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER};
      D3D11_SUBRESOURCE_DATA cd{&c};
      device->CreateBuffer(&cb, &cd, &gpu.constants);
      meshes.push_back(std::move(gpu));
    }
    return true;
  }

  bool Targets(uint32_t w, uint32_t h) {
    if (w == width && h == height && color) return true;
    color.Reset(), resolved.Reset(), staging.Reset(), depth.Reset(), rtv.Reset(), dsv.Reset();
    D3D11_TEXTURE2D_DESC td{w, h, 1, 1, kColorFormat, {samples, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET};
    if (FAILED(device->CreateTexture2D(&td, nullptr, &color))) return false;
    td.SampleDesc = {1, 0};
    td.BindFlags = 0;
    if (samples > 1 && FAILED(device->CreateTexture2D(&td, nullptr, &resolved))) return false;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &staging))) return false;
    D3D11_TEXTURE2D_DESC dd{w, h, 1, 1, DXGI_FORMAT_D32_FLOAT, {samples, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL};
    if (FAILED(device->CreateTexture2D(&dd, nullptr, &depth))) return false;
    if (FAILED(device->CreateRenderTargetView(color.Get(), nullptr, &rtv)) ||
        FAILED(device->CreateDepthStencilView(depth.Get(), nullptr, &dsv)))
      return false;
    width = w, height = h;
    return true;
  }
};

AvatarRenderer::AvatarRenderer() : impl_(std::make_unique<Impl>()) {}
AvatarRenderer::~AvatarRenderer() = default;

bool AvatarRenderer::HasAvatar() const { return !impl_->meshes.empty(); }

bool AvatarRenderer::SetAvatar(const x360tool::AvatarCatalog& catalog, const x360tool::AvatarDescription& description,
                               std::string& error) {
  Impl& r = *impl_;
  if (!r.Init(error)) return false;
  std::vector<x360tool::AvatarBuiltPart> parts;
  x360tool::AvatarBuildOptions options;  // every component, right-handed, with the shapes
  if (!x360tool::build_avatar(catalog, description, options, parts, error)) return false;
  r.meshes.clear();
  for (int k = 0; k < 3; ++k) r.min[k] = r.head_min[k] = 1e9f, r.max[k] = r.head_max[k] = -1e9f;
  r.has_head = false;
  for (const auto& part : parts)
    if (!r.Upload(part, error)) return false;
  // The skeleton's height and weight, roughly: up to an eighth taller or shorter, a fifth wider or
  // thinner.
  r.height_scale = 1.0f + 0.12f * description.height;
  r.weight_scale = 1.0f + 0.2f * description.weight;
  return true;
}

bool AvatarRenderer::Render(uint32_t width, uint32_t height, float yaw, float zoom, anyx360::ui::Image& out,
                            uint32_t background) {
  Impl& r = *impl_;
  if (!r.device || r.meshes.empty() || !width || !height || !r.Targets(width, height)) return false;
  ID3D11DeviceContext* ctx = r.context.Get();
  const float a = ((background >> 24) & 0xFF) / 255.0f;
  const float clear[4] = {((background >> 16) & 0xFF) / 255.0f * a, ((background >> 8) & 0xFF) / 255.0f * a,
                          (background & 0xFF) / 255.0f * a, a};
  ctx->ClearRenderTargetView(r.rtv.Get(), clear);
  ctx->ClearDepthStencilView(r.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
  ctx->OMSetRenderTargets(1, r.rtv.GetAddressOf(), r.dsv.Get());
  D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
  ctx->RSSetViewports(1, &viewport);

  // The camera: the whole avatar (zoom 0) to its face (1), from the front.
  const float feet = r.min[1];
  const float tall = (r.max[1] - r.min[1]) * r.height_scale;
  const float aspect = float(width) / float(height);
  const float fov = XMConvertToRadians(28.0f);
  auto distance_for = [&](float h, float w) {
    const float by_height = (h / 2) / std::tan(fov / 2);
    const float by_width = (w / 2) / (std::tan(fov / 2) * aspect);
    return std::max(by_height, by_width);
  };
  const float body_cy = feet + tall / 2;
  const float body_d = distance_for(tall * 1.08f, (r.max[0] - r.min[0]) * 1.1f) + (r.max[2] - r.min[2]) / 2;
  float face_cy = body_cy, face_d = body_d;
  if (r.has_head) {
    const float head_h = (r.head_max[1] - r.head_min[1]) * 1.25f;
    face_cy = feet + ((r.head_min[1] + r.head_max[1]) / 2 - feet) * r.height_scale;
    face_d = distance_for(head_h * 1.25f, (r.head_max[0] - r.head_min[0]) * 1.4f) + (r.head_max[2] - r.head_min[2]) / 2;
  }
  zoom = std::clamp(zoom, 0.0f, 1.0f);
  const float z = zoom * zoom * (3 - 2 * zoom);
  const float cy = body_cy + (face_cy - body_cy) * z, d = body_d + (face_d - body_d) * z;
  const XMMATRIX view = XMMatrixLookAtRH(XMVectorSet(0, cy + d * 0.03f, d, 1), XMVectorSet(0, cy, 0, 1), XMVectorSet(0, 1, 0, 0));
  const XMMATRIX proj = XMMatrixPerspectiveFovRH(fov, aspect, d * 0.05f, d * 4);
  FrameConstants frame{};
  XMStoreFloat4x4(&frame.view_proj, XMMatrixTranspose(view * proj));
  frame.light[0] = 0.45f, frame.light[1] = 0.6f, frame.light[2] = 0.75f;
  frame.shape[0] = r.height_scale;
  frame.shape[1] = r.weight_scale;
  frame.shape[2] = r.has_head ? r.head_min[1] + (r.head_max[1] - r.head_min[1]) * 0.05f : r.max[1];
  frame.shape[3] = feet;
  frame.turn[0] = std::cos(yaw), frame.turn[1] = std::sin(yaw);
  ctx->UpdateSubresource(r.frame.Get(), 0, nullptr, &frame, 0, 0);

  ctx->IASetInputLayout(r.layout.Get());
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->VSSetShader(r.vs.Get(), nullptr, 0);
  ctx->PSSetShader(r.ps.Get(), nullptr, 0);
  ctx->VSSetConstantBuffers(0, 1, r.frame.GetAddressOf());
  ID3D11SamplerState* samplers[2] = {r.wrap.Get(), r.clamp.Get()};
  ctx->PSSetSamplers(0, 2, samplers);
  ctx->RSSetState(r.raster.Get());
  // Opaque meshes first, then the lenses over them.
  for (int pass = 0; pass < 2; ++pass) {
    ctx->OMSetBlendState(pass ? r.blend.Get() : r.opaque.Get(), nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(pass ? r.depth_test_only.Get() : r.depth_on.Get(), 0);
    for (const Mesh& mesh : r.meshes) {
      if ((mesh.material == 3) != (pass == 1)) continue;
      const UINT stride = sizeof(Vertex), offset = 0;
      ctx->IASetVertexBuffers(0, 1, mesh.vertices.GetAddressOf(), &stride, &offset);
      ctx->IASetIndexBuffer(mesh.indices.Get(), DXGI_FORMAT_R16_UINT, 0);
      ID3D11Buffer* constants[2] = {r.frame.Get(), mesh.constants.Get()};
      ctx->VSSetConstantBuffers(0, 2, constants);
      ctx->PSSetConstantBuffers(0, 2, constants);
      ID3D11ShaderResourceView* views[kSlots];
      for (int k = 0; k < kSlots; ++k) views[k] = mesh.slots[k] ? mesh.slots[k].Get() : r.empty.Get();
      ctx->PSSetShaderResources(0, kSlots, views);
      ctx->DrawIndexed(mesh.index_count, 0, 0);
    }
  }

  if (r.samples > 1) ctx->ResolveSubresource(r.resolved.Get(), 0, r.color.Get(), 0, kColorFormat);
  ctx->CopyResource(r.staging.Get(), r.samples > 1 ? r.resolved.Get() : r.color.Get());
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (FAILED(ctx->Map(r.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
  out.width = width;
  out.height = height;
  out.bgra.resize(size_t(width) * height * 4);
  for (uint32_t y = 0; y < height; ++y)
    std::memcpy(out.bgra.data() + size_t(y) * width * 4, static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
                size_t(width) * 4);
  ctx->Unmap(r.staging.Get(), 0);
  return true;
}

}  // namespace anyx360::launcher::ui
