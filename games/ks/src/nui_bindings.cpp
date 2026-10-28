// Kinect Sports' Kinect library, replaced at its API boundary by the virtual sensor (x360/nui.h).
//
// The game is recompiled from its title update (0.0.4.2), which was built with a later XDK: its
// library is NUI 2.0.20353, close to Fruit Ninja Kinect's 2.0.20500, where the disc's was 2.0.11775
// (Kinect Adventures' launch-era build). The addresses are the update's. Each was found from the
// disc's entry points (tools/scripts/nui_recon.py, games/ks/notes/nui_entry_points.md) by
// instruction shape (addresses and offsets masked): the function of the same shape in the update or
// in Fruit Ninja Kinect's library, or the call at the same place in the game's callers; the leaf
// functions (camera settings, troubleshooter) are the same instructions. Names, signatures and
// frame layouts are in games/ka/notes/nui_api.md. The pure code linked around the library works on
// its results and stays.
#include <mutex>
#include <vector>

#include "x360/endian.h"
#include "x360/hle.h"
#include "x360/kernel.h"
#include "x360/nui.h"

PPC_EXTERN_FUNC(sub_82AE8748);  // XGSetTextureHeaderEx (the title's XG library)
PPC_EXTERN_FUNC(sub_82AE8A30);  // XGOffsetResourceAddress

namespace ks {

namespace {

using x360::load_be;
using x360::store_be;
namespace nui = x360::nui;

uint32_t Load32(uint32_t address) { return load_be<uint32_t>(x360::hle::to_host(address)); }
void Store32(uint32_t address, uint32_t value) { store_be<uint32_t>(x360::hle::to_host(address), value); }

// ---- image stream formats ---------------------------------------------------------------------
// ImageStreamOpen (82A1E268) is Fruit Ninja Kinect's (2.0.20500) and reads the same tables
// (820598B0 per image type, 82059940 per stream, word for word Fruit Ninja Kinect's 82001C18 and
// 82001CA8): each NUI_IMAGE_TYPE has one resolution and a D3DFORMAT and frame size. The disc's
// 2.0.11775 numbered the plain depth image 4; this build numbers it 3, and adds the small depth 8.
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

// The texture header of an image frame, made as the library makes its own (82A1DA78):
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
  ctx.lr = 0x82A1DC50;
  sub_82AE8748(ctx, base);
  ctx.r3.u64 = header;
  ctx.r4.u64 = pixels;
  ctx.lr = 0x82A1DC64;
  sub_82AE8A30(ctx, base);
  ctx = saved;
}

// ---- color camera settings: kept as the game sets them ---------------------------------------
// 82A13A78(camera, setting, value) sets them; 82A131E0/82A131F0(camera, setting, value*) read them
// back. 82A13A98(camera, float[4]) sets a region. The camera is always 0.
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

// 82A13DE0(angle*, status*): the motor's elevation, from XamNuiCameraElevationGetAngle.
uint32_t CameraElevationGetAngle(uint32_t angle_ptr, uint32_t status_ptr) {
  if (status_ptr) Store32(status_ptr, 0);
  return nui::CameraElevationGetAngle(angle_ptr);
}

// ---- identity: nobody is enrolled -------------------------------------------------------------
// 82A28F08(tracking ID, flags, callback, context): identify the player with that skeleton. As in
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

// 82A1EEB8, on the game's Kinect thread: the identity answers first, then the frame.
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

// The disc's (0.0.0.2) addresses of each are in docs/KINECTSPORTS.md; those matched in Fruit Ninja
// Kinect's library are its 821C2918 (initialization), 821B3720, 821B3870, 821B30F8 (skeletons),
// 821C3CC0, 821C3120, 821C3448 (image streams) and 821D15D0 (elevation adjustment).

// Initialization.
X360_GUEST_FUNCTION(82A1CDF8, x360::nui::Initialize)

// Skeleton tracking.
X360_GUEST_FUNCTION(82A1ED68, x360::nui::SkeletonTrackingEnable)
X360_GUEST_FUNCTION(82A1E488, x360::nui::SkeletonTrackingDisable)
X360_GUEST_FUNCTION(82A1EEB8, ks::SkeletonGetNextFrame)
X360_GUEST_FUNCTION(82A1E600, x360::nui::SkeletonSetTrackedSkeletons)
X360_GUEST_FUNCTION(82A1E8C8, x360::nui::TransformSmooth)
X360_GUEST_TRACE(82A1E100, "NUI depth to color pixel coordinates (pure)")

// Image streams and camera.
X360_GUEST_FUNCTION(82A1E268, x360::nui::ImageStreamOpen)
X360_GUEST_FUNCTION(82A1D600, x360::nui::ImageStreamGetNextFrame)
X360_GUEST_FUNCTION(82A1D928, x360::nui::ImageStreamReleaseFrame)
X360_GUEST_FUNCTION(82A13DE0, ks::CameraElevationGetAngle)
X360_GUEST_STUB(82A14780, "NUI elevation adjustment", 0)
X360_GUEST_FUNCTION(82A13A78, ks::CameraSet)
X360_GUEST_FUNCTION(82A131E0, ks::CameraGet)
X360_GUEST_FUNCTION(82A131F0, ks::CameraGet)
X360_GUEST_STUB(82A13A88, "NUI camera property 82A13A88", 0)
X360_GUEST_FUNCTION(82A13A98, ks::CameraSetRegion)
X360_GUEST_STUB(82A12A50, "NUI 82A12A50", x360::nui::kFail)

// Identity and the troubleshooter (which calls XamShowNuiTroubleshooterUI).
X360_GUEST_STUB(82A28D48, "NUI identity request (with callback)", x360::nui::kFail)
X360_GUEST_FUNCTION(82A28F08, ks::IdentityIdentify)
X360_GUEST_STUB(82A29280, "NUI identity 82A29280", 0)
X360_GUEST_STUB(82A15ED8, "NUI troubleshooter UI", 0)
