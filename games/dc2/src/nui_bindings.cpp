// Dance Central 2's Kinect library (NUI, linked into the executable), replaced at its API boundary by
// the virtual sensor (x360/nui.h).
//
// The addresses are the entry points the game calls (tools/scripts/nui_recon.py, see
// games/dc2/notes/nui_entry_points.md). Dance Central 3 links a later build of the same library
// (2.0.21173; this one is 2.0.20764) and calls it from the same Harmonix layer, so each entry point
// was matched to Dance Central 3's (games/dc3/notes/nui_api.md) by its size, the kernel functions it
// reaches, the arguments it is called with and its caller, and the thin wrappers by their code
// (games/dc2/notes/nui_api.md).
#include "x360/endian.h"
#include "x360/hle.h"
#include "x360/nui.h"

PPC_EXTERN_FUNC(sub_8296F688);  // XGSetTextureHeaderEx (the title's D3D library)
PPC_EXTERN_FUNC(sub_8296F840);  // XGOffsetResourceAddress

namespace dc2 {

namespace {

using x360::load_be;
using x360::store_be;
namespace nui = x360::nui;

uint32_t Load32(uint32_t address) { return load_be<uint32_t>(x360::hle::to_host(address)); }
void Store32(uint32_t address, uint32_t value) { store_be<uint32_t>(x360::hle::to_host(address), value); }

// ---- image stream formats: the library's own tables -------------------------------------------
// Per stream type (16 bytes): index into the format table, initialization flags it needs,
// resolution. Per format (28 bytes): frame size, width, height, bytes per pixel, D3DFORMAT. The same
// tables as Dance Central 3's library, at other addresses.
constexpr uint32_t kStreamTypes = 0x820C5220;
constexpr uint32_t kStreamTypeCount = 9;
constexpr uint32_t kFormats = 0x820C52B0;

bool StreamFormat(uint32_t type, uint32_t resolution, nui::StreamFormat& format) {
  if (type >= kStreamTypeCount) return false;
  const uint32_t entry = kStreamTypes + type * 16;
  if (Load32(entry + 8) != resolution) return false;
  const uint32_t f = kFormats + Load32(entry) * 28;
  format.width = Load32(f + 4);
  format.height = Load32(f + 8);
  format.bytes_per_pixel = Load32(f + 12);
  format.d3d_format = Load32(f + 16);
  switch (type) {  // NUI_IMAGE_TYPE
    case 0: format.kind = nui::StreamFormat::kDepthPlayer; return true;
    case 1: format.kind = nui::StreamFormat::kColorRgb; return true;
    case 2: case 3: format.kind = nui::StreamFormat::kColorYuv; return true;
    case 4: format.kind = nui::StreamFormat::kDepth; return true;
    default: return false;  // images mapped into the other camera's space
  }
}

// The texture header of an image frame, made as the library makes its own (826E8720):
// XGSetTextureHeaderEx(width, height, 1 level, usage 4, format, exp bias 0, flags 1, base offset 0,
// mip offset "contiguous", pitch 0 (default), header, NULL, NULL), then
// XGOffsetResourceAddress(header, pixels). Arguments 9-13 go on the stack at r1+0x54, 8 bytes apart.
void TextureHeader(PPCContext& ctx, uint32_t header, const nui::StreamFormat& format, uint32_t pixels) {
  uint8_t* base = x360::hle::guest_base();
  const PPCContext saved = ctx;
  ctx.r1.u64 -= 0x200;  // a frame for the calls, clear of the caller's
  const uint32_t sp = ctx.r1.u32;
  const uint32_t stack_args[] = {0xFFFFFFFF, 0, header, 0, 0};
  for (uint32_t i = 0; i < 5; ++i) Store32(sp + 0x54 + i * 8, stack_args[i]);
  ctx.r3.u64 = format.width;
  ctx.r4.u64 = format.height;
  ctx.r5.u64 = 1;
  ctx.r6.u64 = 4;
  ctx.r7.u64 = format.d3d_format;
  ctx.r8.u64 = 0;
  ctx.r9.u64 = 1;
  ctx.r10.u64 = 0;
  ctx.lr = 0x826E88E8;
  sub_8296F688(ctx, base);
  ctx.r3.u64 = header;
  ctx.r4.u64 = pixels;
  ctx.lr = 0x826E88FC;
  sub_8296F840(ctx, base);
  ctx = saved;
}

// ---- color camera settings: kept as the game sets them ---------------------------------------
// 826EA950(camera, setting, value) / 826E9EC8(camera, setting, value*): the game sets settings and
// reads them back. 826EA960(camera, float[4]) / 826E96D8(camera, float[4]*): a region it sets and
// compares.
uint32_t g_camera_settings[4] = {};
uint32_t g_camera_region[4] = {};

uint32_t CameraSet(uint32_t camera, uint32_t setting, uint32_t value) {
  (void)camera;
  g_camera_settings[setting & 3] = value;
  return nui::kOk;
}
uint32_t CameraGet(uint32_t camera, uint32_t setting, uint32_t value_ptr) {
  (void)camera;
  if (value_ptr) Store32(value_ptr, g_camera_settings[setting & 3]);
  return nui::kOk;
}
uint32_t CameraSetRegion(uint32_t camera, uint32_t region) {
  (void)camera;
  for (int i = 0; i < 4; ++i) g_camera_region[i] = Load32(region + i * 4);
  return nui::kOk;
}
uint32_t CameraGetRegion(uint32_t camera, uint32_t region) {
  (void)camera;
  for (int i = 0; i < 4; ++i) Store32(region + i * 4, g_camera_region[i]);
  return nui::kOk;
}

// ---- identity: nobody is enrolled -------------------------------------------------------------
// 826E5788(enrollment index 0-7, info*): {+0 value, +4 enrolled}.
uint32_t IdentityGetEnrollment(uint32_t index, uint32_t info) {
  (void)index;
  Store32(info + 0, 0xFFFFFFFF);
  Store32(info + 4, 0);
  return nui::kOk;
}

}  // namespace

void InstallNui() {
  nui::TitleSupport support;
  support.stream_format = StreamFormat;
  support.texture_header = TextureHeader;
  nui::SetTitleSupport(std::move(support));
}

}  // namespace dc2

// Initialization.
X360_GUEST_FUNCTION(826F2150, x360::nui::Initialize)
X360_GUEST_FUNCTION(826EFCA0, x360::nui::Shutdown)

// Skeleton tracking.
X360_GUEST_FUNCTION(826E43E0, x360::nui::SkeletonTrackingEnable)
X360_GUEST_FUNCTION(826E3C08, x360::nui::SkeletonTrackingDisable)
X360_GUEST_FUNCTION(826E4580, x360::nui::SkeletonGetNextFrame)
X360_GUEST_FUNCTION(826E3D80, x360::nui::SkeletonSetTrackedSkeletons)
X360_GUEST_TRACE(826E5358, "NUI transform from the gravity vector (pure)")
X360_GUEST_TRACE(826E59A0, "NUI 826E59A0 (pure)")
X360_GUEST_TRACE(826E7C50, "NUI gesture troubleshooter (pure)")

// Image streams and camera.
X360_GUEST_FUNCTION(826E8E38, x360::nui::ImageStreamOpen)
X360_GUEST_FUNCTION(826E82A8, x360::nui::ImageStreamGetNextFrame)
X360_GUEST_FUNCTION(826E85D0, x360::nui::ImageStreamReleaseFrame)
X360_GUEST_FUNCTION(826EA950, dc2::CameraSet)
X360_GUEST_FUNCTION(826E9EC8, dc2::CameraGet)
X360_GUEST_FUNCTION(826EA960, dc2::CameraSetRegion)
X360_GUEST_FUNCTION(826E96D8, dc2::CameraGetRegion)

// Identity (face recognition): nobody enrolled, nothing recognised.
X360_GUEST_FUNCTION(826E5788, dc2::IdentityGetEnrollment)
X360_GUEST_STUB(826E54A0, "NUI identity request (with callback)", x360::nui::kFail)
X360_GUEST_STUB(826E5660, "NUI identity request (with callback)", x360::nui::kFail)

// Speech: starting recognition fails, so the game turns voice commands off. The thin wrappers of
// the speech library (each Dance Central 3's, at the same place in the same order).
X360_GUEST_STUB(8272BC18, "NUI speech start", x360::nui::kFail)
X360_GUEST_STUB(82727F40, "NUI speech 82727F40", 0)
X360_GUEST_STUB(82727F50, "NUI speech 82727F50", 0)
X360_GUEST_STUB(82727F60, "NUI speech 82727F60", x360::nui::kFail)
X360_GUEST_STUB(82729828, "NUI speech 82729828", 0)
X360_GUEST_STUB(82729838, "NUI speech 82729838", 0)
X360_GUEST_STUB(82729848, "NUI speech 82729848", x360::nui::kFail)
X360_GUEST_STUB(82729868, "NUI speech 82729868", 0)
X360_GUEST_STUB(82729890, "NUI speech 82729890", 0)
X360_GUEST_STUB(827298B0, "NUI speech 827298B0", 0)
X360_GUEST_STUB(827298F8, "NUI speech 827298F8", 0)
X360_GUEST_STUB(8272ABF8, "NUI speech 8272ABF8", 0)
X360_GUEST_STUB(8272AC08, "NUI speech 8272AC08", 0)
X360_GUEST_STUB(8272AC30, "NUI speech 8272AC30", 0)
X360_GUEST_STUB(8272AC40, "NUI speech 8272AC40", 0)
X360_GUEST_STUB(8272BB40, "NUI speech 8272BB40", 0)
