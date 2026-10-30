// Dance Central 3's Kinect library (NUI, linked into the executable), replaced at its API boundary by
// the virtual sensor (x360/nui.h).
//
// The addresses are the entry points the game calls (tools/scripts/nui_recon.py, see
// games/dc3/notes/nui_entry_points.md); names and signatures come from Harmonix's profiling scopes
// around the calls, the arguments, how the results are used and what each function reaches
// (games/dc3/notes/nui_api.md).
//
// The addresses are the title update's (0.0.1.5), the executable the game is recompiled from. The
// notes give the disc's (0.0.0.5); games/dc3/notes/tu_addresses.md maps one to the other.
#include "x360/endian.h"
#include "x360/hle.h"
#include "x360/nui.h"

PPC_EXTERN_FUNC(sub_82AC8E08);  // XGSetTextureHeaderEx (the title's D3D library)
PPC_EXTERN_FUNC(sub_82AC9028);  // XGOffsetResourceAddress

namespace dc3 {

namespace {

using x360::load_be;
using x360::store_be;
namespace nui = x360::nui;

uint32_t Load32(uint32_t address) { return load_be<uint32_t>(x360::hle::to_host(address)); }
void Store32(uint32_t address, uint32_t value) { store_be<uint32_t>(x360::hle::to_host(address), value); }

// ---- image stream formats: the library's own tables -------------------------------------------
// Per stream type (16 bytes): index into the format table, initialization flags it needs,
// resolution. Per format (28 bytes): frame size, width, height, bytes per pixel, D3DFORMAT.
constexpr uint32_t kStreamTypes = 0x820F0600;
constexpr uint32_t kStreamTypeCount = 9;
constexpr uint32_t kFormats = 0x820F0690;

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

// The texture header of an image frame, made as the library makes its own (8280E110):
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
  ctx.lr = 0x8280E2BC;
  sub_82AC8E08(ctx, base);
  ctx.r3.u64 = header;
  ctx.r4.u64 = pixels;
  ctx.lr = 0x8280E2CC;
  sub_82AC9028(ctx, base);
  ctx = saved;
}

// ---- color camera settings: kept as the game sets them ---------------------------------------
// 8280D4F0(camera, setting, value) / 8280C808(camera, setting, value*): the game sets settings 0 and
// 2 and reads them back. 8280D548(camera, float[4]) / 8280C018(camera, float[4]*): a region it
// sets and compares.
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
// 82809148(enrollment index 0-7, info*): {+0 value, +4 enrolled}.
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

}  // namespace dc3

// Initialization.
X360_GUEST_FUNCTION(828167A8, x360::nui::Initialize)
X360_GUEST_FUNCTION(82814348, x360::nui::Shutdown)

// Skeleton tracking.
X360_GUEST_FUNCTION(82807DA0, x360::nui::SkeletonTrackingEnable)
X360_GUEST_FUNCTION(828075C8, x360::nui::SkeletonTrackingDisable)
X360_GUEST_FUNCTION(82807F40, x360::nui::SkeletonGetNextFrame)
X360_GUEST_FUNCTION(82807740, x360::nui::SkeletonSetTrackedSkeletons)
X360_GUEST_TRACE(82808D18, "NUI transform from the gravity vector (pure)")
X360_GUEST_TRACE(82809360, "NUI 82809360 (pure)")
X360_GUEST_TRACE(8280B900, "NUI gesture troubleshooter (pure)")

// Image streams and camera.
X360_GUEST_FUNCTION(8280E8D8, x360::nui::ImageStreamOpen)
X360_GUEST_FUNCTION(8280DC98, x360::nui::ImageStreamGetNextFrame)
X360_GUEST_FUNCTION(8280DFC0, x360::nui::ImageStreamReleaseFrame)
X360_GUEST_FUNCTION(828096A8, x360::nui::CameraElevationGetAngle)
X360_GUEST_FUNCTION(82809790, x360::nui::CameraElevationSetAngle)
X360_GUEST_STUB(8280A0F0, "NUI elevation adjustment", 0)
X360_GUEST_FUNCTION(8280D4F0, dc3::CameraSet)
X360_GUEST_FUNCTION(8280C808, dc3::CameraGet)
X360_GUEST_FUNCTION(8280D548, dc3::CameraSetRegion)
X360_GUEST_FUNCTION(8280C018, dc3::CameraGetRegion)

// Identity (face recognition): nobody enrolled, nothing recognised.
X360_GUEST_FUNCTION(82809148, dc3::IdentityGetEnrollment)
X360_GUEST_STUB(82808E60, "NUI identity request (with callback)", x360::nui::kFail)
X360_GUEST_STUB(82809020, "NUI identity request (with callback)", x360::nui::kFail)
X360_GUEST_STUB(82816C10, "NUI identity 82816C10", x360::nui::kFail)
X360_GUEST_STUB(82816D00, "NUI identity enable", x360::nui::kOk)
X360_GUEST_STUB(82816DA0, "NUI identity 82816DA0", 0)
X360_GUEST_STUB(82816E20, "NUI identity 82816E20", 0)

// Audio (multichannel echo cancellation): no microphone array, so creation fails.
X360_GUEST_STUB(828535B8, "NUI audio create", x360::nui::kFail)
X360_GUEST_STUB(82852EB8, "NUI audio callback", 0)
X360_GUEST_STUB(82852F30, "NUI audio 82852F30", 0)
X360_GUEST_STUB(828529D0, "NUI audio destroy", 0)

// Speech: starting recognition fails, so the game turns voice commands off.
X360_GUEST_STUB(8286A6F8, "NUI speech start", x360::nui::kFail)
X360_GUEST_STUB(82866708, "NUI speech 82866708", 0)
X360_GUEST_STUB(82866718, "NUI speech 82866718", 0)
X360_GUEST_STUB(82866730, "NUI speech 82866730", 0)
X360_GUEST_STUB(82866740, "NUI speech 82866740", x360::nui::kFail)
X360_GUEST_STUB(82868290, "NUI speech 82868290", 0)
X360_GUEST_STUB(828682A0, "NUI speech 828682A0", 0)
X360_GUEST_STUB(828682B0, "NUI speech 828682B0", x360::nui::kFail)
X360_GUEST_STUB(828682D0, "NUI speech 828682D0", 0)
X360_GUEST_STUB(828682F8, "NUI speech 828682F8", 0)
X360_GUEST_STUB(82868318, "NUI speech 82868318", 0)
X360_GUEST_STUB(82868360, "NUI speech 82868360", 0)
X360_GUEST_STUB(828696E0, "NUI speech 828696E0", 0)
X360_GUEST_STUB(828696F0, "NUI speech 828696F0", 0)
X360_GUEST_STUB(82869710, "NUI speech 82869710", 0)
X360_GUEST_STUB(82869720, "NUI speech 82869720", 0)
X360_GUEST_STUB(8286A620, "NUI speech 8286A620", 0)
