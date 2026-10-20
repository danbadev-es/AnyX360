// Kinect Sports' Kinect library (NUI 2.0.11775, the same launch-era build as Kinect Adventures'),
// replaced at its API boundary by the virtual sensor (x360/nui.h).
//
// The addresses are the entry points the game calls (tools/scripts/nui_recon.py, see
// games/ks/notes/nui_entry_points.md). Each was matched to Kinect Adventures' function of the same
// build by its instructions with addresses and offsets masked (the same code, linked elsewhere);
// names, signatures and frame layouts are in games/ka/notes/nui_api.md. The pure code linked
// around the library (829B0000-829BB000, 829E3000-829F0000) works on its results and stays.
#include <mutex>
#include <vector>

#include "x360/endian.h"
#include "x360/hle.h"
#include "x360/kernel.h"
#include "x360/nui.h"

PPC_EXTERN_FUNC(sub_82A93C88);  // XGSetTextureHeaderEx (the title's XG library)
PPC_EXTERN_FUNC(sub_82A93F70);  // XGOffsetResourceAddress

namespace ks {

namespace {

using x360::load_be;
using x360::store_be;
namespace nui = x360::nui;

uint32_t Load32(uint32_t address) { return load_be<uint32_t>(x360::hle::to_host(address)); }
void Store32(uint32_t address, uint32_t value) { store_be<uint32_t>(x360::hle::to_host(address), value); }

// ---- image stream formats ---------------------------------------------------------------------
// ImageStreamOpen (829D1AD8) is Kinect Adventures' (82D2AAB0): each NUI_IMAGE_TYPE accepts one
// resolution and gets a D3DFORMAT and frame size.
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

// The texture header of an image frame, made as the library makes its own (829D21D8):
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
  ctx.lr = 0x829D22A4;
  sub_82A93C88(ctx, base);
  ctx.r3.u64 = header;
  ctx.r4.u64 = pixels;
  ctx.lr = 0x829D22B0;
  sub_82A93F70(ctx, base);
  ctx = saved;
}

// ---- color camera settings: kept as the game sets them ---------------------------------------
// 829C90F0(camera, setting, value) sets them; 829C8658/829C8668(camera, setting, value*) read them
// back. 829C9110(camera, float[4]) sets a region. The camera is always 0.
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

// 829C5988(angle*, status*): the motor's elevation, from XamNuiCameraElevationGetAngle.
uint32_t CameraElevationGetAngle(uint32_t angle_ptr, uint32_t status_ptr) {
  if (status_ptr) Store32(status_ptr, 0);
  return nui::CameraElevationGetAngle(angle_ptr);
}

// ---- identity: nobody is enrolled -------------------------------------------------------------
// 829DA858(tracking ID, flags, callback, context): identify the player with that skeleton. As in
// Kinect Adventures, the answer comes with the next skeleton frame through the callback: done (1),
// S_OK, enrollment index -2 (nobody enrolled is this player), no flags.
struct IdentityRequest {
  uint32_t tracking_id, callback, context;
};
std::mutex g_identity_mutex;
std::vector<IdentityRequest> g_identity_requests;

uint32_t IdentityIdentify(uint32_t tracking_id, uint32_t flags, uint32_t callback, uint32_t context) {
  (void)flags;
  if (!callback) return nui::kInvalidArg;
  std::lock_guard lock(g_identity_mutex);
  g_identity_requests.push_back({tracking_id, callback, context});
  return nui::kOk;
}

// 829D2D28, on the game's Kinect thread: the identity answers first, then the frame.
uint32_t SkeletonGetNextFrame(uint32_t timeout_ms, uint32_t frame) {
  std::vector<IdentityRequest> requests;
  {
    std::lock_guard lock(g_identity_mutex);
    requests.swap(g_identity_requests);
  }
  if (!requests.empty()) {
    static const uint32_t message = x360::kernel::SystemAlloc(32);
    for (const IdentityRequest& r : requests) {
      Store32(message + 0, 1);  // done
      Store32(message + 4, r.tracking_id);
      Store32(message + 8, nui::kOk);
      Store32(message + 12, 0xFFFFFFFE);  // not enrolled
      Store32(message + 16, 0);
      x360::kernel::CallGuest(r.callback, {r.context, message});
    }
  }
  return nui::SkeletonGetNextFrame(timeout_ms, frame);
}

}  // namespace

void InstallNui() {
  nui::TitleSupport support;
  support.stream_format = StreamFormat;
  support.texture_header = TextureHeader;
  nui::SetTitleSupport(std::move(support));
}

}  // namespace ks

// Initialization (Kinect Adventures' 82D25C88).
X360_GUEST_FUNCTION(829CF908, x360::nui::Initialize)

// Skeleton tracking (82D26620, 82D25DE0, 82D26768, 82D25EF8, 82D26190, 82D28E28).
X360_GUEST_FUNCTION(829D2BE0, x360::nui::SkeletonTrackingEnable)
X360_GUEST_FUNCTION(829D23A0, x360::nui::SkeletonTrackingDisable)
X360_GUEST_FUNCTION(829D2D28, ks::SkeletonGetNextFrame)
X360_GUEST_FUNCTION(829D24B8, x360::nui::SkeletonSetTrackedSkeletons)
X360_GUEST_FUNCTION(829D2750, x360::nui::TransformSmooth)
X360_GUEST_TRACE(829CFE50, "NUI depth to color pixel coordinates (pure)")

// Image streams and camera (82D2AAB0, 82D28FB8, 82D292C8, 82D2B678, 82D2BF98, 82D28A08, 82D27F70,
// 82D27F80, 82D28A18, 82D28A28, 82D27608).
X360_GUEST_FUNCTION(829D1AD8, x360::nui::ImageStreamOpen)
X360_GUEST_FUNCTION(829CFFE0, x360::nui::ImageStreamGetNextFrame)
X360_GUEST_FUNCTION(829D02F0, x360::nui::ImageStreamReleaseFrame)
X360_GUEST_FUNCTION(829C5988, ks::CameraElevationGetAngle)
X360_GUEST_STUB(829C62A8, "NUI elevation adjustment", 0)
X360_GUEST_FUNCTION(829C90F0, ks::CameraSet)
X360_GUEST_FUNCTION(829C8658, ks::CameraGet)
X360_GUEST_FUNCTION(829C8668, ks::CameraGet)
X360_GUEST_STUB(829C9100, "NUI camera property 829C9100", 0)
X360_GUEST_FUNCTION(829C9110, ks::CameraSetRegion)
X360_GUEST_STUB(829C7D00, "NUI 829C7D00", x360::nui::kFail)

// Identity (82D2D6C0, 82D2D840, 82D2DB58) and the troubleshooter (82D2DB60).
X360_GUEST_STUB(829DA6D8, "NUI identity request (with callback)", x360::nui::kFail)
X360_GUEST_FUNCTION(829DA858, ks::IdentityIdentify)
X360_GUEST_STUB(829DAB70, "NUI identity 829DAB70", 0)
X360_GUEST_STUB(829C79C8, "NUI troubleshooter UI", 0)
