// Dance Central 3's Kinect library (NUI, linked into the executable), replaced at its API boundary by
// the virtual sensor (x360/nui.h).
//
// The addresses are the entry points the game calls (tools/scripts/nui_recon.py, see
// games/dc3/notes/nui_entry_points.md); names and signatures come from Harmonix's profiling scopes
// around the calls, the arguments, how the results are used and what each function reaches
// (games/dc3/notes/nui_api.md).
#include "x360/endian.h"
#include "x360/hle.h"
#include "x360/nui.h"

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
constexpr uint32_t kStreamTypes = 0x820F0080;
constexpr uint32_t kStreamTypeCount = 9;
constexpr uint32_t kFormats = 0x820F0110;

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

// ---- color camera settings: kept as the game sets them ---------------------------------------
// 8280BF40(camera, setting, value) / 8280B258(camera, setting, value*): the game sets settings 0 and
// 2 and reads them back. 8280BF98(camera, float[4]) / 8280AA68(camera, float[4]*): a region it
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
// 82807B98(enrollment index 0-7, info*): {+0 value, +4 enrolled}.
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
  nui::SetTitleSupport(std::move(support));
}

}  // namespace dc3

// Initialization.
X360_GUEST_FUNCTION(828151F8, x360::nui::Initialize)
X360_GUEST_FUNCTION(82812D98, x360::nui::Shutdown)

// Skeleton tracking.
X360_GUEST_FUNCTION(828067F0, x360::nui::SkeletonTrackingEnable)
X360_GUEST_FUNCTION(82806018, x360::nui::SkeletonTrackingDisable)
X360_GUEST_FUNCTION(82806990, x360::nui::SkeletonGetNextFrame)
X360_GUEST_FUNCTION(82806190, x360::nui::SkeletonSetTrackedSkeletons)
X360_GUEST_TRACE(82807768, "NUI transform from the gravity vector (pure)")
X360_GUEST_TRACE(82807DB0, "NUI 82807DB0 (pure)")
X360_GUEST_TRACE(8280A350, "NUI gesture troubleshooter (pure)")

// Image streams and camera.
X360_GUEST_FUNCTION(8280D328, x360::nui::ImageStreamOpen)
X360_GUEST_FUNCTION(8280C6E8, x360::nui::ImageStreamGetNextFrame)
X360_GUEST_FUNCTION(8280CA10, x360::nui::ImageStreamReleaseFrame)
X360_GUEST_FUNCTION(828080F8, x360::nui::CameraElevationGetAngle)
X360_GUEST_FUNCTION(828081E0, x360::nui::CameraElevationSetAngle)
X360_GUEST_STUB(82808B40, "NUI elevation adjustment", 0)
X360_GUEST_FUNCTION(8280BF40, dc3::CameraSet)
X360_GUEST_FUNCTION(8280B258, dc3::CameraGet)
X360_GUEST_FUNCTION(8280BF98, dc3::CameraSetRegion)
X360_GUEST_FUNCTION(8280AA68, dc3::CameraGetRegion)

// Identity (face recognition): nobody enrolled, nothing recognised.
X360_GUEST_FUNCTION(82807B98, dc3::IdentityGetEnrollment)
X360_GUEST_STUB(828078B0, "NUI identity request (with callback)", x360::nui::kFail)
X360_GUEST_STUB(82807A70, "NUI identity request (with callback)", x360::nui::kFail)
X360_GUEST_STUB(82815660, "NUI identity 82815660", x360::nui::kFail)
X360_GUEST_STUB(82815750, "NUI identity enable", x360::nui::kOk)
X360_GUEST_STUB(828157F0, "NUI identity 828157F0", 0)
X360_GUEST_STUB(82815870, "NUI identity 82815870", 0)

// Audio (multichannel echo cancellation): no microphone array, so creation fails.
X360_GUEST_STUB(82852000, "NUI audio create", x360::nui::kFail)
X360_GUEST_STUB(82851900, "NUI audio callback", 0)
X360_GUEST_STUB(82851978, "NUI audio 82851978", 0)
X360_GUEST_STUB(82851418, "NUI audio destroy", 0)

// Speech: starting recognition fails, so the game turns voice commands off.
X360_GUEST_STUB(82868B98, "NUI speech start", x360::nui::kFail)
X360_GUEST_STUB(828650B8, "NUI speech 828650B8", 0)
X360_GUEST_STUB(828650C8, "NUI speech 828650C8", 0)
X360_GUEST_STUB(828650E0, "NUI speech 828650E0", 0)
X360_GUEST_STUB(828650F0, "NUI speech 828650F0", x360::nui::kFail)
X360_GUEST_STUB(82866988, "NUI speech 82866988", 0)
X360_GUEST_STUB(82866998, "NUI speech 82866998", 0)
X360_GUEST_STUB(828669A8, "NUI speech 828669A8", x360::nui::kFail)
X360_GUEST_STUB(828669C8, "NUI speech 828669C8", 0)
X360_GUEST_STUB(828669F0, "NUI speech 828669F0", 0)
X360_GUEST_STUB(82866A10, "NUI speech 82866A10", 0)
X360_GUEST_STUB(82866A58, "NUI speech 82866A58", 0)
X360_GUEST_STUB(82867B80, "NUI speech 82867B80", 0)
X360_GUEST_STUB(82867B90, "NUI speech 82867B90", 0)
X360_GUEST_STUB(82867BB0, "NUI speech 82867BB0", 0)
X360_GUEST_STUB(82867BC0, "NUI speech 82867BC0", 0)
X360_GUEST_STUB(82868AC0, "NUI speech 82868AC0", 0)
