// Kinect Joy Ride's Kinect library (NUI 2.0.11775, an earlier build of the one in Kinect Adventures
// and on the Kinect Sports disc), replaced at its API boundary by the virtual sensor (x360/nui.h).
//
// The addresses are the library's functions the game calls (every bl or b into 82CEB760-82D00FA0
// from outside it; see games/kjr/notes/nui_api.md). Each was matched to the Kinect Sports disc's or
// Kinect Adventures' function by its instructions with addresses and offsets masked: the same code,
// with only the addresses of the library's globals different (Kinect Sports' addresses here are its
// disc's, 0.0.0.2). Names, signatures and frame layouts are in games/ka/notes/nui_api.md. The
// skeleton tracking after the library (pure floating point) and the game's own Kinect layer
// (827E5D38-827F5D58) stay recompiled.
//
// They are Title Update #3's (0.0.3.1), which the game is recompiled from: the update has the same
// library, moved by +0x6F78 (the game's Kinect layer by +0x5D38, the XG functions by +0x5FB8). Each
// disc address (docs/KINECTJOYRIDE.md) was matched again by instruction shape, one match each, and
// checked over 16 instructions at the new address.
#include <mutex>
#include <vector>

#include "x360/endian.h"
#include "x360/hle.h"
#include "x360/kernel.h"
#include "x360/nui.h"

PPC_EXTERN_FUNC(sub_8270A948);  // XGSetTextureHeaderEx (the title's XG library)
PPC_EXTERN_FUNC(sub_8270ACF8);  // XGOffsetResourceAddress

namespace kjr {

namespace {

using x360::store_be;
namespace nui = x360::nui;

void Store32(uint32_t address, uint32_t value) { store_be<uint32_t>(x360::hle::to_host(address), value); }

// ---- image stream formats ---------------------------------------------------------------------
// ImageStreamOpen (82CF5B10) is the Kinect Sports disc's (829D1AD8) word for word, apart from the
// addresses of the library's globals: each NUI_IMAGE_TYPE accepts one resolution and gets a
// D3DFORMAT and frame size.
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

// The texture header of an image frame, made as the library makes its own (in ImageStreamOpen, at
// 82CF62D8): XGSetTextureHeaderEx(width, height, 1 level, usage 4, format, exp bias 0, flags 1, base
// offset 0, mip offset "contiguous", pitch 0 (default), header, NULL, NULL), then
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
  ctx.lr = 0x82CF62DC;
  sub_8270A948(ctx, base);
  ctx.r3.u64 = header;
  ctx.r4.u64 = pixels;
  ctx.lr = 0x82CF62E8;
  sub_8270ACF8(ctx, base);
  ctx = saved;
}

// ---- color camera settings: kept as the game sets them ---------------------------------------
// 82CF7A48(camera, setting, value) sets them (Kinect Sports' 829C90F0); 82CF7028(camera, setting,
// value*) reads them back (829C8668). The camera is always 0.
uint32_t g_camera_settings[16] = {};

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

// ---- identity: nobody is enrolled -------------------------------------------------------------
// 82CF23D8(enrollment index, info*) (Kinect Adventures' 82D2D948): {+0 value, +4 enrolled}.
uint32_t IdentityGetEnrollment(uint32_t index, uint32_t info) {
  (void)index;
  if (info) {
    Store32(info + 0, 0xFFFFFFFF);
    Store32(info + 4, 0);
  }
  return nui::kOk;
}

// 82CF22D0(tracking ID, flags, callback, context) (Kinect Sports' 829DA858): identify the player
// with that skeleton. As in Kinect Adventures, the answer comes with the next skeleton frame
// through the callback (827EA640): done (1), S_OK, enrollment index -2 (nobody enrolled is this
// player), no flags.
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

// 82CF2FB0, on the game's Kinect thread: the identity answers first, then the frame.
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

}  // namespace kjr

// Initialization and shutdown (Kinect Sports' 829CF908, Kinect Adventures' 82D23E30).
X360_GUEST_FUNCTION(82CF1FF0, x360::nui::Initialize)
X360_GUEST_FUNCTION(82CF0198, x360::nui::Shutdown)

// Skeleton tracking (829D2BE0, 829D23A0, 829D2D28, 829D24B8, 829D2750, 829CFE50). The game enables
// and disables it with tail calls (827ED030, 827ED038).
X360_GUEST_FUNCTION(82CF2E68, x360::nui::SkeletonTrackingEnable)
X360_GUEST_FUNCTION(82CF25F0, x360::nui::SkeletonTrackingDisable)
X360_GUEST_FUNCTION(82CF2FB0, kjr::SkeletonGetNextFrame)
X360_GUEST_FUNCTION(82CF2708, x360::nui::SkeletonSetTrackedSkeletons)
X360_GUEST_FUNCTION(82CF29A0, x360::nui::TransformSmooth)
X360_GUEST_TRACE(82CF3E88, "NUI depth to color pixel coordinates (pure)")

// Image streams (829D1AD8, 829CFFE0, 829D02F0).
X360_GUEST_FUNCTION(82CF5B10, x360::nui::ImageStreamOpen)
X360_GUEST_FUNCTION(82CF4018, x360::nui::ImageStreamGetNextFrame)
X360_GUEST_FUNCTION(82CF4328, x360::nui::ImageStreamReleaseFrame)

// The color camera's settings. The game brackets its changes with 82CF6700 and 82CF73F8 (which
// apply them to the camera when the count of open brackets returns to 0), sets integers (82CF7A48)
// and floats (82CF7A58, Kinect Sports' 829C9100) and a three-word setting (82CF7A38, 829C8A38 with
// camera 0), and reads them back (82CF7028).
X360_GUEST_STUB(82CF6700, "NUI camera settings begin", 0)
X360_GUEST_STUB(82CF73F8, "NUI camera settings apply", 0)
X360_GUEST_FUNCTION(82CF7A48, kjr::CameraSet)
X360_GUEST_STUB(82CF7A58, "NUI camera float setting", 0)
X360_GUEST_STUB(82CF7A38, "NUI camera setting 82CF7A38", 0)
X360_GUEST_FUNCTION(82CF7028, kjr::CameraGet)

// Identity (Kinect Sports' 829DA6D8, 829DA858, 829DAB70; Kinect Adventures' 82D2D948) and the
// troubleshooter (829C79C8). 82CF2148 (Kinect Adventures' 82D2F118 with a second argument 0) works
// on the library's own tables, which are empty: it stays.
X360_GUEST_STUB(82CF2150, "NUI identity request (with callback)", x360::nui::kFail)
X360_GUEST_FUNCTION(82CF22D0, kjr::IdentityIdentify)
X360_GUEST_FUNCTION(82CF23D8, kjr::IdentityGetEnrollment)
X360_GUEST_STUB(82CF25E8, "NUI identity 82CF25E8", 0)
X360_GUEST_STUB(82CEB760, "NUI troubleshooter UI", 0)
