// Kinect Adventures' Kinect library (NUI 2.0.11775, a launch-era build linked into the executable),
// replaced at its API boundary by the virtual sensor (x360/nui.h).
//
// The addresses are the entry points the game calls (tools/scripts/nui_recon.py, see
// games/ka/notes/nui_entry_points.md); names and signatures come from the arguments, how the results
// are used, what each function reaches and the callers' code (games/ka/notes/nui_api.md). The
// skeleton and image frame layouts are the same as in Dance Central 3's later library; the image
// formats are chosen in ImageStreamOpen's code (82D2AAB0) rather than read from a table.
#include "x360/endian.h"
#include "x360/hle.h"
#include "x360/nui.h"

PPC_EXTERN_FUNC(sub_82D713A0);  // XGSetTextureHeaderEx (the title's XG library)
PPC_EXTERN_FUNC(sub_82D71690);  // XGOffsetResourceAddress

namespace ka {

namespace {

using x360::load_be;
using x360::store_be;
namespace nui = x360::nui;

uint32_t Load32(uint32_t address) { return load_be<uint32_t>(x360::hle::to_host(address)); }
void Store32(uint32_t address, uint32_t value) { store_be<uint32_t>(x360::hle::to_host(address), value); }

// ---- image stream formats ---------------------------------------------------------------------
// From 82D2AAB0: each NUI_IMAGE_TYPE accepts one resolution and gets a D3DFORMAT and frame size.
// The game opens types 0 (depth and player index, 320x240), 1 (color, 640x480, 32 bits) and 7
// (80x60, the depth format: a small player map).
bool StreamFormat(uint32_t type, uint32_t resolution, nui::StreamFormat& format) {
  struct Entry {
    uint32_t type, resolution, width, height, bytes_per_pixel, d3d_format;
    nui::StreamFormat::Kind kind;
  };
  static constexpr Entry kEntries[] = {
      {0, 1, 320, 240, 2, 0x1A220058, nui::StreamFormat::kDepthPlayer},
      {1, 2, 640, 480, 4, 0x28287EB2, nui::StreamFormat::kColorRgb},
      {2, 2, 640, 480, 2, 0x1A20000C, nui::StreamFormat::kColorYuv},
      {4, 1, 320, 240, 2, 0x1A220058, nui::StreamFormat::kDepth},
      {7, 0, 80, 60, 2, 0x1A220058, nui::StreamFormat::kDepthPlayer},
  };
  for (const Entry& e : kEntries) {
    if (e.type != type || e.resolution != resolution) continue;
    format.kind = e.kind;
    format.width = e.width;
    format.height = e.height;
    format.bytes_per_pixel = e.bytes_per_pixel;
    format.d3d_format = e.d3d_format;
    return true;
  }
  return false;
}

// The texture header of an image frame, made as the library makes its own (82D2B1B0):
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
  ctx.lr = 0x82D2B27C;
  sub_82D713A0(ctx, base);
  ctx.r3.u64 = header;
  ctx.r4.u64 = pixels;
  ctx.lr = 0x82D2B288;
  sub_82D71690(ctx, base);
  ctx = saved;
}

// ---- color camera settings: kept as the game sets them ---------------------------------------
// 82D28A08(camera, setting, value) sets settings 0 and 2; 82D27F70/82D27F80(camera, setting,
// value*) read them (and setting 5) back. 82D28A28(camera, float[4]) sets a region that
// 82D27688(camera, float[4]*) reads. The camera is always 0.
uint32_t g_camera_settings[16] = {};
uint32_t g_camera_region[4] = {};

uint32_t CameraSet(uint32_t camera, uint32_t setting, uint32_t value) {
  (void)camera;
  g_camera_settings[setting & 15] = value;
  return nui::kOk;
}
uint32_t CameraGet(uint32_t camera, uint32_t setting, uint32_t value_ptr) {
  (void)camera;
  if (value_ptr) Store32(value_ptr, g_camera_settings[setting & 15]);
  return nui::kOk;
}
uint32_t CameraSetRegion(uint32_t camera, uint32_t region) {
  (void)camera;
  if (region)
    for (int i = 0; i < 4; ++i) g_camera_region[i] = Load32(region + i * 4);
  return nui::kOk;
}
uint32_t CameraGetRegion(uint32_t camera, uint32_t region) {
  (void)camera;
  if (region)
    for (int i = 0; i < 4; ++i) Store32(region + i * 4, g_camera_region[i]);
  return nui::kOk;
}

// 82D2B678(angle*, status*): the motor's elevation, from XamNuiCameraElevationGetAngle.
uint32_t CameraElevationGetAngle(uint32_t angle_ptr, uint32_t status_ptr) {
  if (status_ptr) Store32(status_ptr, 0);
  return nui::CameraElevationGetAngle(angle_ptr);
}

// ---- identity: nobody is enrolled -------------------------------------------------------------
// 82D2D948(enrollment index, info*): {+0 value, +4 enrolled}.
uint32_t IdentityGetEnrollment(uint32_t index, uint32_t info) {
  (void)index;
  if (info) {
    Store32(info + 0, 0xFFFFFFFF);
    Store32(info + 4, 0);
  }
  return nui::kOk;
}

}  // namespace

void InstallNui() {
  nui::TitleSupport support;
  support.stream_format = StreamFormat;
  support.texture_header = TextureHeader;
  nui::SetTitleSupport(std::move(support));
}

}  // namespace ka

// Initialization: NuiInitialize(0x849: depth and player index, color, skeleton; hardware thread 5).
X360_GUEST_FUNCTION(82D25C88, x360::nui::Initialize)
X360_GUEST_FUNCTION(82D23E30, x360::nui::Shutdown)

// Skeleton tracking.
X360_GUEST_FUNCTION(82D26620, x360::nui::SkeletonTrackingEnable)
X360_GUEST_FUNCTION(82D25DE0, x360::nui::SkeletonTrackingDisable)
X360_GUEST_FUNCTION(82D26768, x360::nui::SkeletonGetNextFrame)
X360_GUEST_FUNCTION(82D25EF8, x360::nui::SkeletonSetTrackedSkeletons)
X360_GUEST_TRACE(82D26190, "NuiTransformSmooth (pure)")
X360_GUEST_TRACE(82D28E28, "NUI depth to color pixel coordinates (pure)")

// Image streams and camera.
X360_GUEST_FUNCTION(82D2AAB0, x360::nui::ImageStreamOpen)
X360_GUEST_FUNCTION(82D28FB8, x360::nui::ImageStreamGetNextFrame)
X360_GUEST_FUNCTION(82D292C8, x360::nui::ImageStreamReleaseFrame)
X360_GUEST_FUNCTION(82D2B678, ka::CameraElevationGetAngle)
X360_GUEST_STUB(82D2BF98, "NUI elevation adjustment", 0)
X360_GUEST_FUNCTION(82D28A08, ka::CameraSet)
X360_GUEST_FUNCTION(82D27F70, ka::CameraGet)
X360_GUEST_FUNCTION(82D27F80, ka::CameraGet)
X360_GUEST_STUB(82D28A18, "NUI camera property 82D28A18", 0)
X360_GUEST_FUNCTION(82D28A28, ka::CameraSetRegion)
X360_GUEST_FUNCTION(82D27688, ka::CameraGetRegion)
X360_GUEST_STUB(82D27608, "NUI 82D27608", x360::nui::kFail)

// Identity (face recognition): nobody enrolled, nothing recognised.
X360_GUEST_FUNCTION(82D2D948, ka::IdentityGetEnrollment)
X360_GUEST_STUB(82D2D6C0, "NUI identity request (with callback)", x360::nui::kFail)
X360_GUEST_STUB(82D2D840, "NUI identity request (with callback)", x360::nui::kFail)
X360_GUEST_STUB(82D2D6B8, "NUI identity 82D2D6B8", x360::nui::kFail)
X360_GUEST_STUB(82D2DB58, "NUI identity 82D2DB58", 0)
X360_GUEST_STUB(82D2DB60, "NUI troubleshooter UI", 0)
