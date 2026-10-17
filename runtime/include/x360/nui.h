// Virtual Kinect sensor: the Xbox 360 NUI library's API, implemented on the host.
//
// Kinect titles link the NUI library into their executable; its skeleton tracking reads depth images
// from the camera driver. Instead of emulating the camera, a game's bindings replace the library's
// entry points with these functions (X360_GUEST_FUNCTION in x360/hle.h), and the skeletons and
// images come from a source: the webcam pose bridge, the mouse or scripted test poses.
//
// The guest structures follow the XDK layout (big-endian): NUI_SKELETON_FRAME is 0xAB0 bytes (time
// stamp in ms, frame number, flags, floor clip plane, normal to gravity, then 6 NUI_SKELETON_DATA of
// 0x1C0: tracking state, tracking ID, enrollment index, user index, position, 20 joints as
// 16-byte vectors, 20 joint states, quality flags).
//
// Camera space is in metres: x positive towards the sensor's left (the player's right as they face
// it), y up, z away from the sensor. The sensor sits camera_height above the floor.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "x360/settings.h"

struct PPCContext;

namespace x360::nui {

constexpr uint32_t kSkeletonCount = 6;
constexpr uint32_t kJointCount = 20;

// HRESULTs of the NUI API.
constexpr uint32_t kOk = 0;
constexpr uint32_t kFail = 0x80004005;
constexpr uint32_t kInvalidArg = 0x80070057;
constexpr uint32_t kDeviceNotConnected = 0x8007048F;
constexpr uint32_t kFrameNoData = 0x83010001;

enum Joint : uint32_t {
  kHipCenter, kSpine, kShoulderCenter, kHead,
  kShoulderLeft, kElbowLeft, kWristLeft, kHandLeft,
  kShoulderRight, kElbowRight, kWristRight, kHandRight,
  kHipLeft, kKneeLeft, kAnkleLeft, kFootLeft,
  kHipRight, kKneeRight, kAnkleRight, kFootRight,
};

enum JointState : uint8_t { kJointNotTracked = 0, kJointInferred = 1, kJointTracked = 2 };

// Edges of the view a body is cut by (NUI_SKELETON_QUALITY_CLIPPED_*).
enum Clipped : uint32_t { kClippedRight = 1, kClippedLeft = 2, kClippedTop = 4, kClippedBottom = 8 };

// One person as the sensor sees them.
struct Body {
  uint32_t id = 0;  // stable while the person stays in view, never 0
  float position[3] = {};
  float joints[kJointCount][3] = {};
  uint8_t joint_state[kJointCount] = {};
  uint32_t quality = 0;
};

// Camera images (as the sensor sees them: mirrored, like the skeleton's x).
constexpr uint32_t kColorWidth = 640, kColorHeight = 480;
constexpr uint32_t kDepthWidth = 320, kDepthHeight = 240;
// Nominal focal lengths in pixels (NUI_CAMERA_*_NOMINAL_FOCAL_LENGTH_IN_PIXELS); both cameras share
// their optical centre here. A point (x, y, z) projects to (w/2 + x f/z, h/2 - y f/z).
constexpr float kColorFocal = 531.15f;
constexpr float kDepthFocal = 285.63f;

// What a source sees at one moment.
struct Snapshot {
  uint32_t body_count = 0;
  Body bodies[kSkeletonCount];
  // When the source has a camera: 640x480 BGRA8, and 320x240 depth (millimetres << 3 | 1 + the
  // index of the body in `bodies`, 0 for none). Without them the images are drawn from the bodies.
  std::shared_ptr<const std::vector<uint8_t>> color;
  std::shared_ptr<const std::vector<uint16_t>> depth;
};

// The format of an image stream, from the title's NUI tables.
struct StreamFormat {
  enum Kind : uint32_t { kColorYuv, kColorRgb, kDepth, kDepthPlayer };
  Kind kind = kDepth;
  uint32_t width = 0, height = 0, bytes_per_pixel = 0;
  uint32_t d3d_format = 0;  // the D3DFORMAT the title's library uses for the texture
};

// What depends on the title's own code.
struct TitleSupport {
  // Format of an image stream type at a resolution; false if the title's library doesn't offer it.
  std::function<bool(uint32_t type, uint32_t resolution, StreamFormat& format)> stream_format;
  // Writes a D3DTexture header at `header` for `format` with its pixels at guest address `pixels`,
  // with the title's D3D library. Called on the guest thread opening the stream.
  std::function<void(PPCContext& ctx, uint32_t header, const StreamFormat& format, uint32_t pixels)> texture_header;
};
void SetTitleSupport(TitleSupport support);

// ---- the NUI API (guest addresses as uint32_t) ----

uint32_t Initialize(uint32_t flags, uint32_t hardware_thread);
void Shutdown();

uint32_t SkeletonTrackingEnable(uint32_t event, uint32_t flags);
uint32_t SkeletonTrackingDisable();
uint32_t SkeletonGetNextFrame(uint32_t timeout_ms, uint32_t frame);
// ids: two tracking IDs (0 or 0xFFFFFFFF: none) when the title chooses whom to track.
uint32_t SkeletonSetTrackedSkeletons(uint32_t ids);

uint32_t ImageStreamOpen(PPCContext* ctx, uint32_t type, uint32_t resolution, uint32_t flags, uint32_t frame_limit,
                         uint32_t event, uint32_t stream_ptr);
uint32_t ImageStreamGetNextFrame(uint32_t stream, uint32_t timeout_ms, uint32_t frame_ptr);
uint32_t ImageStreamReleaseFrame(uint32_t stream, uint32_t frame);

uint32_t CameraElevationGetAngle(uint32_t angle_ptr);
uint32_t CameraElevationSetAngle(int32_t angle);

// ---- status and settings (settings panel, overlay) ----

struct Status {
  bool initialized = false;
  std::string source;   // webcam, mouse, synthetic, none
  std::string message;  // the source's state, e.g. "no camera found"
  uint32_t frames = 0;  // skeleton frames produced
  uint32_t bodies = 0;  // people in view
  uint32_t tracked = 0;  // of them, fully tracked
  // The webcam source (the pose bridge).
  bool camera_running = false;
  uint32_t camera_width = 0, camera_height = 0;
  float camera_fps = 0;
  float inference_ms = 0;  // pose estimation per frame
  float latency_ms = 0;    // from the camera's capture to the sensor's frame
  uint32_t restarts = 0;   // times the bridge was started again
};
Status GetStatus();

// The latest frame's people, for a preview.
struct PreviewBody {
  uint32_t id = 0;
  bool tracked = false;
  float joints[kJointCount][3] = {};
};
std::vector<PreviewBody> GetBodies();

// The [kinect] settings changed: another source starts if it changed, else the source applies them.
void ApplySettings(const settings::KinectSettings& kinect);
// Recentres the play space on the player in view (webcam).
void Calibrate();
// Starts the source again (the webcam's pose bridge), clearing its failures.
void RestartSource();

// The webcams Media Foundation lists, in the order [kinect] camera counts. Enumerated once (again
// with refresh); listing doesn't open them.
std::vector<std::string> ListCameras(bool refresh = false);

// Joint pairs to draw a skeleton with.
constexpr Joint kSkeletonBones[][2] = {
    {kHipCenter, kSpine},         {kSpine, kShoulderCenter},      {kShoulderCenter, kHead},
    {kShoulderCenter, kShoulderLeft}, {kShoulderLeft, kElbowLeft}, {kElbowLeft, kWristLeft},
    {kWristLeft, kHandLeft},      {kShoulderCenter, kShoulderRight}, {kShoulderRight, kElbowRight},
    {kElbowRight, kWristRight},   {kWristRight, kHandRight},      {kHipCenter, kHipLeft},
    {kHipLeft, kKneeLeft},        {kKneeLeft, kAnkleLeft},        {kAnkleLeft, kFootLeft},
    {kHipCenter, kHipRight},      {kHipRight, kKneeRight},        {kKneeRight, kAnkleRight},
    {kAnkleRight, kFootRight},
};

}  // namespace x360::nui
