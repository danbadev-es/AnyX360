// Dance Central 3's Kinect library (NUI, linked into the executable) at its API boundary.
//
// The addresses are the entry points the game calls (tools/scripts/nui_recon.py, see
// games/dc3/notes/nui_entry_points.md); names come from Harmonix's profiling scopes around the calls,
// the argument shapes and what each function reaches (docs/DANCECENTRAL3.md).
//
// For now there is no sensor: NuiInitialize fails with "device not connected", the code the game
// checks for, and every other entry point runs the original code with its first call logged.
#include "x360/hle.h"

namespace {

constexpr uint32_t kErrorDeviceNotConnected = 0x8007048F;  // HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED)

}  // namespace

// Initialization.
X360_GUEST_STUB(828151F8, "NuiInitialize", kErrorDeviceNotConnected)
X360_GUEST_TRACE(82812D98, "NuiShutdown")

// Skeleton tracking.
X360_GUEST_TRACE(828067F0, "NuiSkeletonTrackingEnable")
X360_GUEST_TRACE(82806018, "NuiSkeletonTrackingDisable")
X360_GUEST_TRACE(82806990, "NuiSkeletonGetNextFrame")
X360_GUEST_TRACE(82806190, "NuiSkeletonSetTrackedSkeletons")
X360_GUEST_TRACE(82807768, "NUI 82807768")
X360_GUEST_TRACE(828078B0, "NUI 828078B0 (with callback)")
X360_GUEST_TRACE(82807A70, "NUI 82807A70 (with callback)")
X360_GUEST_TRACE(82807B98, "NUI 82807B98 (per index 0-7)")
X360_GUEST_TRACE(82807DB0, "NUI 82807DB0")
X360_GUEST_TRACE(8280A350, "NUI 8280A350 (gesture troubleshooter)")

// Image streams and camera.
X360_GUEST_TRACE(8280D328, "NuiImageStreamOpen")
X360_GUEST_TRACE(8280C6E8, "NuiImageStreamGetNextFrame")
X360_GUEST_TRACE(8280CA10, "NuiImageStreamReleaseFrame")
X360_GUEST_TRACE(828080F8, "NuiCameraElevationGetAngle")
X360_GUEST_TRACE(828081E0, "NuiCameraElevationSetAngle")
X360_GUEST_TRACE(82808B40, "NUI 82808B40 (elevation)")
X360_GUEST_TRACE(8280BF40, "NUI 8280BF40 (camera mode set)")
X360_GUEST_TRACE(8280B258, "NUI 8280B258 (camera mode get)")
X360_GUEST_TRACE(8280BF98, "NUI 8280BF98 (camera region set)")
X360_GUEST_TRACE(8280AA68, "NUI 8280AA68 (camera region get)")

// Identity.
X360_GUEST_TRACE(82815660, "NUI 82815660")
X360_GUEST_TRACE(82815750, "NUI 82815750")
X360_GUEST_TRACE(828157F0, "NUI 828157F0")
X360_GUEST_TRACE(82815870, "NUI 82815870")

// Audio (multichannel echo cancellation).
X360_GUEST_TRACE(82851418, "NUI audio 82851418")
X360_GUEST_TRACE(82851900, "NUI audio 82851900")
X360_GUEST_TRACE(82851978, "NUI audio 82851978")
X360_GUEST_TRACE(82852000, "NUI audio 82852000")

// Speech.
X360_GUEST_TRACE(828650B8, "NUI speech 828650B8")
X360_GUEST_TRACE(828650C8, "NUI speech 828650C8")
X360_GUEST_TRACE(828650E0, "NUI speech 828650E0")
X360_GUEST_TRACE(828650F0, "NUI speech 828650F0")
X360_GUEST_TRACE(82866988, "NUI speech 82866988")
X360_GUEST_TRACE(82866998, "NUI speech 82866998")
X360_GUEST_TRACE(828669A8, "NUI speech 828669A8")
X360_GUEST_TRACE(828669C8, "NUI speech 828669C8")
X360_GUEST_TRACE(828669F0, "NUI speech 828669F0")
X360_GUEST_TRACE(82866A10, "NUI speech 82866A10")
X360_GUEST_TRACE(82866A58, "NUI speech 82866A58")
X360_GUEST_TRACE(82867B80, "NUI speech 82867B80")
X360_GUEST_TRACE(82867B90, "NUI speech 82867B90")
X360_GUEST_TRACE(82867BB0, "NUI speech 82867BB0")
X360_GUEST_TRACE(82867BC0, "NUI speech 82867BC0")
X360_GUEST_TRACE(82868AC0, "NUI speech 82868AC0")
X360_GUEST_TRACE(82868B98, "NUI speech 82868B98")
