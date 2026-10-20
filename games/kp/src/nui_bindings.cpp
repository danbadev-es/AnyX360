// Kinect Party's Kinect library (NUI 2.0.21119, the February 2012 XDK), replaced at its API boundary
// by the virtual sensor (x360/nui.h).
//
// The addresses are the library's functions the game calls (tools/scripts/nui_recon.py, see
// games/kp/notes/nui_api.md). Most were matched to Fruit Ninja Kinect's library (2.0.20500) by their
// instructions with addresses and offsets masked; the others are named from their arguments, what
// they reach and the callers' code. The image formats come from the library's tables (8208C3A8 per
// image type, 8208C438 per stream).
#include <atomic>

#include "x360/endian.h"
#include "x360/hle.h"
#include "x360/kernel.h"
#include "x360/nui.h"

PPC_EXTERN_FUNC(sub_82620DF0);  // XGSetTextureHeaderEx (the title's XG library)
PPC_EXTERN_FUNC(sub_82621150);  // XGOffsetResourceAddress

namespace kp {

namespace {

using x360::load_be;
using x360::store_be;
namespace nui = x360::nui;

uint32_t Load32(uint32_t address) { return load_be<uint32_t>(x360::hle::to_host(address)); }
void Store32(uint32_t address, uint32_t value) { store_be<uint32_t>(x360::hle::to_host(address), value); }

// ---- image stream formats ---------------------------------------------------------------------
// 8208C3A8: each NUI_IMAGE_TYPE (0-8) has one resolution and one of six streams; 8208C438: each
// stream's frame bytes, width, height, bytes per pixel and D3DFORMATs. This library also offers the
// depth image in the color camera's view (4, 5) and the color image in the depth camera's (6). The
// game opens types 1 (color, 640x480), 4 (depth and player index in the color camera's view,
// 320x240), 0 (depth and player index, 320x240) and 7 (the same, 80x60).
bool StreamFormat(uint32_t type, uint32_t resolution, nui::StreamFormat& format) {
  struct Entry {
    uint32_t type, resolution, width, height, bytes_per_pixel, d3d_format;
    nui::StreamFormat::Kind kind;
  };
  static constexpr Entry kEntries[] = {
      {0, 1, 320, 240, 2, 0x1A220058, nui::StreamFormat::kDepthPlayer},
      {1, 2, 640, 480, 4, 0x28287EB2, nui::StreamFormat::kColorRgb},
      {2, 2, 640, 480, 2, 0x1A20000C, nui::StreamFormat::kColorYuv},
      {3, 1, 320, 240, 2, 0x1A220058, nui::StreamFormat::kDepth},
      {4, 1, 320, 240, 2, 0x1A220058, nui::StreamFormat::kDepthPlayerInColor},
      {5, 1, 320, 240, 2, 0x1A220058, nui::StreamFormat::kDepthInColor},
      {6, 2, 640, 480, 4, 0x28287EB2, nui::StreamFormat::kColorInDepth},
      {7, 0, 80, 60, 2, 0x1A220058, nui::StreamFormat::kDepthPlayer},
      {8, 0, 80, 60, 2, 0x1A220058, nui::StreamFormat::kDepth},
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

// The texture header of an image frame, made as the library makes its own (826ACB5C):
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
  ctx.lr = 0x826ACB60;
  sub_82620DF0(ctx, base);
  ctx.r3.u64 = header;
  ctx.r4.u64 = pixels;
  ctx.lr = 0x826ACB74;
  sub_82621150(ctx, base);
  ctx = saved;
}

// ---- initialization -------------------------------------------------------------------------
// NuiInitialize(0x400B (depth and player index, color, skeleton, depth in color space) | 0x40000000
// | 0x800 when asked; hardware thread 5), from the game's Kinect manager (82A501C8), which is only
// created if it succeeds. The console then binds the signed-in player to the body in view and says
// so (XN_SYS_NUIBINDINGCHANGED).
uint32_t Initialize(uint32_t flags, uint32_t hardware_thread) {
  const uint32_t result = nui::Initialize(flags, hardware_thread);
  if (result == nui::kOk) x360::kernel::Notify(x360::kernel::kNotifyNuiBindingChanged, 0);
  return result;
}

// ---- identity: the player in view is player 1 ----------------------------------------------------
// 826A3FB0(enrollment slot 0-7, info*), Fruit Ninja Kinect's 821B2AF0: {+0 the user signed in with
// that enrollment (254: none), +4 flags (1: identified)}. The game (82A49C78) asks for the slot of a
// skeleton's enrollment index (+8 of NUI_SKELETON_DATA, 0 in the virtual sensor's frames).
uint32_t IdentityGetEnrollment(uint32_t slot, uint32_t info) {
  if (slot >= 8 || !info) return nui::kInvalidArg;
  Store32(info + 0, slot == 0 ? 0 : 254);
  Store32(info + 4, slot == 0 ? 1 : 0);
  return nui::kOk;
}

// ---- the motor: the automatic tilt ------------------------------------------------------------
// 826B0138(flags, ..., f1-f3, players*, XOVERLAPPED*), Fruit Ninja Kinect's 821D15D0: aims the camera
// at the players (flags 8 from 82A4B340). On the console it returns ERROR_IO_PENDING (997) and
// completes the overlapped when the motor stops; here there's no motor: it completes at once.
constexpr uint32_t kErrorIoPending = 997;

uint32_t AutoTilt(uint32_t flags, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t players, uint32_t overlapped) {
  (void)flags, (void)r4, (void)r5, (void)r6, (void)players;
  if (!overlapped) return nui::kOk;
  Store32(overlapped + 0x00, 0);  // result: success
  Store32(overlapped + 0x04, 0);
  Store32(overlapped + 0x18, 0);
  if (uint32_t event = Load32(overlapped + 0x0C)) {
    if (uint32_t body = x360::kernel::ObjectFromHandle(event)) x360::kernel::SetEvent(body);
  }
  return kErrorIoPending;
}

// 826AF6F0(LONG* angle, status*): the motor's elevation (reaches XamNuiCameraElevationGetAngle).
uint32_t CameraElevationGetAngle(uint32_t angle_ptr, uint32_t status_ptr) {
  if (status_ptr) Store32(status_ptr, 0);
  return nui::CameraElevationGetAngle(angle_ptr);
}

// ---- color camera settings and view area: kept as the game sets them -----------------------------
// 826B34D0(camera 0, property, value): activities switch the camera's automatic exposure and white
// balance (properties 0 and 1) on and off. 826B3470(NUI_IMAGE_VIEW_AREA*): {digital zoom 0 or 1, pan x
// (-320-320), pan y (-240-240)}, set from the game's scripts (82A4B930). The virtual camera has neither:
// the webcam's own settings apply, and the picture is the whole view.
std::atomic<uint32_t> g_camera_settings[16] = {};

uint32_t CameraSetProperty(uint32_t camera, uint32_t property, uint32_t value) {
  (void)camera;
  g_camera_settings[property & 15] = value;
  return nui::kOk;
}

uint32_t SetViewArea(uint32_t view_area) {
  if (!view_area) return nui::kInvalidArg;
  static std::atomic<bool> logged{false};
  if (Load32(view_area) != 0 && !logged.exchange(true))
    KLOG_I("kp: the game asked for a digital zoom (view area %u, %d, %d); the picture stays whole", Load32(view_area),
           int32_t(Load32(view_area + 4)), int32_t(Load32(view_area + 8)));
  return nui::kOk;
}

}  // namespace

void InstallNui() {
  nui::TitleSupport support;
  support.stream_format = StreamFormat;
  support.texture_header = TextureHeader;
  nui::SetTitleSupport(std::move(support));
}

}  // namespace kp

// Initialization (82A501C8).
X360_GUEST_FUNCTION(826ABB48, kp::Initialize)

// Skeleton tracking: enabled with flags 0 at start-up, then each activity enables it again (flags 0
// or 2: the title chooses whom to track) or disables it, and picks the tracked skeletons.
X360_GUEST_FUNCTION(826ADA00, x360::nui::SkeletonTrackingEnable)
X360_GUEST_FUNCTION(826AD380, x360::nui::SkeletonTrackingDisable)
X360_GUEST_FUNCTION(826ADBA0, x360::nui::SkeletonGetNextFrame)
X360_GUEST_FUNCTION(826AD4F8, x360::nui::SkeletonSetTrackedSkeletons)

// Image streams, and the depth image's pixels on the color image.
X360_GUEST_FUNCTION(826AD160, x360::nui::ImageStreamOpen)
X360_GUEST_FUNCTION(826AC520, x360::nui::ImageStreamGetNextFrame)
X360_GUEST_FUNCTION(826AC848, x360::nui::ImageStreamReleaseFrame)
X360_GUEST_FUNCTION(826ACFF8, x360::nui::ColorPixelFromDepthPixel)

// The motor and the camera.
X360_GUEST_FUNCTION(826AF6F0, kp::CameraElevationGetAngle)
X360_GUEST_FUNCTION(826AF7D8, x360::nui::CameraElevationSetAngle)
X360_GUEST_FUNCTION(826B0138, kp::AutoTilt)
X360_GUEST_FUNCTION(826B34D0, kp::CameraSetProperty)
X360_GUEST_FUNCTION(826B3470, kp::SetViewArea)

// Identity.
X360_GUEST_FUNCTION(826A3FB0, kp::IdentityGetEnrollment)
