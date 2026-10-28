// Fruit Ninja Kinect's Kinect library (NUI 2.0.20500, the February 2011 XDK), replaced at its API
// boundary by the virtual sensor (x360/nui.h).
//
// The addresses are the library's functions the game calls (tools/scripts/nui_recon.py, see
// games/fnk/notes/nui_api.md); names and signatures come from the arguments, how the results are
// used, what each function reaches and the callers' code, compared with Dance Central 3's and
// Kinect Adventures' libraries. The frame layouts are theirs; the image formats come from the
// library's tables (82001C18 per image type, 82001CA8 per stream).
//
// They are the title update's (0.0.1.3), which the game is recompiled from: the same library, moved
// by +0x27F78 (the XG functions by +0x1D40, the game's own code by -0x21170 to -0x31040, the tables
// by +8). Each address of the base executable (0.0.0.3, docs/FRUITNINJAKINECT.md) was matched again
// by instruction shape, one match each, and checked over 16 instructions at the new address.
#include "x360/endian.h"
#include "x360/hle.h"
#include "x360/kernel.h"
#include "x360/nui.h"

PPC_EXTERN_FUNC(sub_823D5F60);  // XGSetTextureHeaderEx (the title's XG library)
PPC_EXTERN_FUNC(sub_823D6118);  // XGOffsetResourceAddress

namespace fnk {

namespace {

using x360::store_be;
namespace nui = x360::nui;

void Store32(uint32_t address, uint32_t value) { store_be<uint32_t>(x360::hle::to_host(address), value); }

// ---- image stream formats ---------------------------------------------------------------------
// 82001C18: each NUI_IMAGE_TYPE has one resolution and a stream entry of 82001CA8 (frame bytes,
// width, height, bytes per pixel, D3DFORMATs). The game opens types 1 (color, 640x480, 32 bits)
// and 0 (depth and player index, 320x240).
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

// The texture header of an image frame, made as the library makes its own (821EB510):
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
  ctx.lr = 0x821EB6E8;
  sub_823D5F60(ctx, base);
  ctx.r3.u64 = header;
  ctx.r4.u64 = pixels;
  ctx.lr = 0x821EB6FC;
  sub_823D6118(ctx, base);
  ctx = saved;
}

// ---- initialization -------------------------------------------------------------------------
// NuiInitialize(flags, hardware thread). The console then binds the signed-in player to the body
// in view and says so (XN_SYS_NUIBINDINGCHANGED); the game's notification handler (82664AF8) asks
// for the enrollments again on it (826641D8) and nothing else does.
uint32_t Initialize(uint32_t flags, uint32_t hardware_thread) {
  const uint32_t result = nui::Initialize(flags, hardware_thread);
  if (result == nui::kOk) x360::kernel::Notify(x360::kernel::kNotifyNuiBindingChanged, 0);
  return result;
}

// ---- identity: the player in view is player 1 ----------------------------------------------------
// 821DAA68(enrollment slot 0-7, info*): {+0 the user signed in with that enrollment (254: none),
// +4 flags (1: identified)}. The game (826641D8) asks for all eight and pairs each signed-in user
// with the skeleton carrying that enrollment index (+8 of NUI_SKELETON_DATA, 0 in the virtual
// sensor's frames); the menus then work for that user's profile. Slot 0 is player 1, who is always
// signed in. With no user anywhere, the main menu read the profile of user 4 (none) and crashed.
uint32_t IdentityGetEnrollment(uint32_t slot, uint32_t info) {
  if (slot >= 8 || !info) return nui::kInvalidArg;
  Store32(info + 0, slot == 0 ? 0 : 254);
  Store32(info + 4, slot == 0 ? 1 : 0);
  return nui::kOk;
}

// ---- the motor: the automatic tilt ------------------------------------------------------------
// 821F9548(flags, ..., f1-f3, players*, XOVERLAPPED*): aims the camera at the players (flags 0x50
// from the calibration screen, 8 to go back to the default angle). On the console it returns
// ERROR_IO_PENDING (997) and completes the overlapped when the motor stops; the game (8265CC68)
// polls the overlapped and then calls its calibration's completion, which leaves "MOVING". Here
// there's no motor: the overlapped is completed at once. Answered "done" (0) instead, the
// completion never ran and the title screen said "CALIBRATING" for good.
constexpr uint32_t kErrorIoPending = 997;

uint32_t AutoTilt(uint32_t flags, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t players, uint32_t overlapped) {
  (void)flags, (void)r4, (void)r5, (void)r6, (void)players;
  if (!overlapped) return nui::kOk;
  Store32(overlapped + 0x00, 0);  // result: success
  Store32(overlapped + 0x04, 0);
  Store32(overlapped + 0x18, 0);
  if (uint32_t event = x360::load_be<uint32_t>(x360::hle::to_host(overlapped + 0x0C))) {
    if (uint32_t body = x360::kernel::ObjectFromHandle(event)) x360::kernel::SetEvent(body);
  }
  return kErrorIoPending;
}

// 826A0A70, the game's calibration: is the player framed? From the floor plane's pitch and the
// camera's height, the top of the view 1.8 m away must be 0.5 m (+-0.2) above the player's head;
// if not, the game tilts the camera (AutoTilt) and checks again, three times, then starts over. The
// virtual camera has no motor and its view is the webcam's: the player is framed as they are.
uint32_t PlayerFramed() { return 1; }

}  // namespace

void InstallNui() {
  nui::TitleSupport support;
  support.stream_format = StreamFormat;
  support.texture_header = TextureHeader;
  nui::SetTitleSupport(std::move(support));
}

}  // namespace fnk

// Initialization: NuiInitialize(0xC0B: depth and player index, color, skeleton, ...; hardware
// thread -1), from 826458E0.
X360_GUEST_FUNCTION(821EA890, fnk::Initialize)
X360_GUEST_FUNCTION(821E8460, x360::nui::Shutdown)

// Skeleton tracking: enabled with flags 2 (the title chooses whom to track, 826448D0).
X360_GUEST_FUNCTION(821DB698, x360::nui::SkeletonTrackingEnable)
X360_GUEST_FUNCTION(821DB7E8, x360::nui::SkeletonGetNextFrame)
X360_GUEST_FUNCTION(821DB070, x360::nui::SkeletonSetTrackedSkeletons)

// Image streams.
X360_GUEST_FUNCTION(821EBC38, x360::nui::ImageStreamOpen)
X360_GUEST_FUNCTION(821EB098, x360::nui::ImageStreamGetNextFrame)
X360_GUEST_FUNCTION(821EB3C0, x360::nui::ImageStreamReleaseFrame)

// The motor: the automatic tilt finishes at once, and there's no movement to stop.
X360_GUEST_FUNCTION(821F9548, fnk::AutoTilt)
X360_GUEST_STUB(821F8C00, "NUI elevation stop movement", 0)
X360_GUEST_FUNCTION(826A0A70, fnk::PlayerFramed)

// Identity.
X360_GUEST_FUNCTION(821DAA68, fnk::IdentityGetEnrollment)
