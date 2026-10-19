// The pose bridge protocol: shared memory between a Kinect game (consumer) and the pose bridge
// process (producer, tools/pose_bridge), which turns a webcam into skeletons and images.
//
// The game creates a file mapping "Local\AnyX360-Nui-<pid>-<n>" (sizeof(Header) + kSlotCount slots)
// and an auto-reset event of the same name plus "-frame", then starts the bridge with
//   --shm <mapping name> --event <event name> --parent <pid>
// The bridge fills the header's producer fields, writes each new frame into the slot after
// `latest_slot` and publishes it:
//   slot.seq odd (writing) -> slot contents -> slot.seq even -> header.latest_slot -> SetEvent.
// The game copies the latest slot and checks that seq is even and unchanged across the copy
// (a seqlock; three slots so the writer never touches the slot just published). Both sides run on
// x86-64, whose stores are seen in order; the layout is little-endian with natural alignment.
//
// The game writes the layout fields (magic to max_bodies) and its settings before starting the bridge;
// the bridge checks the layout, writes its own version and, if it can't speak the game's, sets state
// kError and exits with code 3. Later setting changes bump control_seq; quit = 1 asks the bridge to
// exit. Without a camera the bridge stays in kNoCamera and keeps looking for one.
//
// Joints are in Kinect camera space (metres; x towards the sensor's left = the player's right, y
// up, z away from the camera), in the order of x360::nui::Joint. Depth is 320x240 millimetres << 3 |
// (1 + body index) or 0 for no body; color is 640x480 BGRA8; both mirrored like the skeleton.
// tools/pose_bridge/protocol.py mirrors this file; its tests check every offset against the
// static_asserts below.
#pragma once

#include <cstddef>
#include <cstdint>

namespace x360::nui::bridge {

constexpr uint32_t kMagic = 0x49554E41;  // "ANUI" in memory
constexpr uint16_t kVersionMajor = 1;    // a different major version is incompatible
constexpr uint16_t kVersionMinor = 1;  // 1.1: tilt, timings and the streams in use
constexpr uint32_t kSlotCount = 3;
constexpr uint32_t kMaxBodies = 6;
constexpr uint32_t kJoints = 20;
constexpr uint32_t kColorWidth = 640, kColorHeight = 480;
constexpr uint32_t kDepthWidth = 320, kDepthHeight = 240;

enum State : uint32_t {
  kStarting = 0,      // loading the model, opening the camera
  kRunning = 1,
  kNoCamera = 2,      // no webcam (or the chosen index doesn't exist)
  kCameraDenied = 3,  // Windows' camera privacy setting blocks desktop apps
  kError = 4,         // see status
  kStopped = 5,
};

enum BodyState : uint32_t { kBodyNone = 0, kBodyPositionOnly = 1, kBodyTracked = 2 };

struct alignas(8) Header {
  uint32_t magic;
  uint16_t version_major;
  uint16_t version_minor;
  uint32_t header_size;  // sizeof(Header)
  uint32_t slot_size;    // sizeof(Slot)
  uint32_t slot_count;
  uint32_t max_bodies;
  // Producer.
  volatile uint32_t state;
  uint32_t producer_pid;
  volatile int64_t heartbeat_qpc;  // QueryPerformanceCounter, at least every 500 ms
  volatile uint32_t latest_slot;   // the newest complete slot (kSlotCount: none yet)
  float camera_fps;
  float inference_ms;
  uint32_t camera_width, camera_height;
  char status[128];  // UTF-8, NUL-terminated: what the bridge is doing or what went wrong
  // Consumer (the game's settings).
  volatile uint32_t control_seq;
  uint32_t camera_index;
  uint32_t mirror;   // the camera already mirrors its image
  uint32_t players;  // bodies to track (1-2)
  uint32_t model;    // 0 lite, 1 full, 2 heavy
  float fov_deg;     // the webcam's horizontal field of view
  float smoothing;   // 0 raw - 1 heavy
  float camera_height_m;
  volatile uint32_t calibrate;  // bumped to recentre the play space on the current position
  volatile uint32_t quit;
  // Producer: the protocol version it speaks (it refuses a layout it doesn't know).
  uint16_t producer_version_major;
  uint16_t producer_version_minor;
  // 1.1, producer: the webcam's tilt the bridge corrects (degrees; pitch > 0 looking down, roll >
  // 0 turned clockwise as seen from behind it), where the estimate comes from, and the camera's
  // height above the floor it measured (0: not yet).
  float tilt_pitch_deg;
  float tilt_roll_deg;
  uint32_t tilt_source;  // TiltSource
  float floor_height_m;
  // 1.1, consumer: the tilt to correct, measured (0) or set (1: the manual angles).
  uint32_t tilt_mode;
  float tilt_pitch_manual_deg;
  float tilt_roll_manual_deg;
  // 1.1, consumer: the images the title uses (1 color, 2 depth; 0: both, as 1.0 consumers).
  uint32_t stream_mask;
  // 1.1, producer: where a frame's time goes (milliseconds, smoothed) and the frames it skipped.
  float capture_ms;  // from the camera's exposure to the bridge having the image
  float detect_ms;   // pose estimation
  float process_ms;  // mapping, filtering
  float render_ms;   // the images
  uint32_t dropped_frames;
  uint8_t reserved[4096 - 284];
};

enum TiltSource : uint32_t { kTiltNone = 0, kTiltBody = 1, kTiltFloor = 2, kTiltManual = 3 };

struct Body {
  uint32_t id;     // stable while the person stays in view, never 0
  uint32_t state;  // BodyState
  float position[4];
  float joints[kJoints][4];
  uint8_t joint_state[kJoints];  // 0 not tracked, 1 inferred, 2 tracked
  uint32_t quality;              // clipped edges: 1 right, 2 left, 4 top, 8 bottom
};

struct alignas(64) Slot {
  volatile uint32_t seq;  // odd while being written
  uint32_t frame_number;
  int64_t capture_qpc;  // when the camera captured the image
  int64_t publish_qpc;
  uint32_t flags;  // 1: color and depth are valid
  uint32_t body_count;
  float floor_plane[4];
  Body bodies[kMaxBodies];
  alignas(64) uint8_t color[kColorWidth * kColorHeight * 4];
  uint16_t depth[kDepthWidth * kDepthHeight];
};

constexpr uint32_t kSlotHasImages = 1;

static_assert(sizeof(Header) == 4096);
static_assert(offsetof(Header, version_major) == 4);
static_assert(offsetof(Header, header_size) == 8);
static_assert(offsetof(Header, slot_size) == 12);
static_assert(offsetof(Header, slot_count) == 16);
static_assert(offsetof(Header, max_bodies) == 20);
static_assert(offsetof(Header, state) == 24);
static_assert(offsetof(Header, producer_pid) == 28);
static_assert(offsetof(Header, heartbeat_qpc) == 32);
static_assert(offsetof(Header, latest_slot) == 40);
static_assert(offsetof(Header, camera_fps) == 44);
static_assert(offsetof(Header, inference_ms) == 48);
static_assert(offsetof(Header, camera_width) == 52);
static_assert(offsetof(Header, camera_height) == 56);
static_assert(offsetof(Header, status) == 60);
static_assert(offsetof(Header, control_seq) == 188);
static_assert(offsetof(Header, camera_index) == 192);
static_assert(offsetof(Header, mirror) == 196);
static_assert(offsetof(Header, players) == 200);
static_assert(offsetof(Header, model) == 204);
static_assert(offsetof(Header, fov_deg) == 208);
static_assert(offsetof(Header, smoothing) == 212);
static_assert(offsetof(Header, camera_height_m) == 216);
static_assert(offsetof(Header, calibrate) == 220);
static_assert(offsetof(Header, quit) == 224);
static_assert(offsetof(Header, producer_version_major) == 228);
static_assert(offsetof(Header, producer_version_minor) == 230);
static_assert(offsetof(Header, tilt_pitch_deg) == 232);
static_assert(offsetof(Header, tilt_mode) == 248);
static_assert(offsetof(Header, stream_mask) == 260);
static_assert(offsetof(Header, capture_ms) == 264);
static_assert(offsetof(Header, dropped_frames) == 280);
static_assert(offsetof(Header, reserved) == 284);
static_assert(sizeof(Body) == 368);
static_assert(offsetof(Body, position) == 8);
static_assert(offsetof(Body, joints) == 24);
static_assert(offsetof(Body, joint_state) == 344);
static_assert(offsetof(Body, quality) == 364);
static_assert(offsetof(Slot, frame_number) == 4);
static_assert(offsetof(Slot, capture_qpc) == 8);
static_assert(offsetof(Slot, publish_qpc) == 16);
static_assert(offsetof(Slot, flags) == 24);
static_assert(offsetof(Slot, body_count) == 28);
static_assert(offsetof(Slot, floor_plane) == 32);
static_assert(offsetof(Slot, bodies) == 48);
static_assert(offsetof(Slot, color) == 2304);
static_assert(offsetof(Slot, depth) == 2304 + kColorWidth * kColorHeight * 4);
static_assert(sizeof(Slot) == 1384704);

}  // namespace x360::nui::bridge
