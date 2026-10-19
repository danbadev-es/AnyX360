// Virtual Kinect sensor (x360/nui.h): the NUI API on a 30 Hz clock fed by a source.
//
// Debugging: ANYX360_NUI_TRACE logs the API calls; ANYX360_NUI_SCRIPT drives the sensor with
// scripted poses (synthetic_source.cpp) whatever the settings say; ANYX360_NUI_SOURCE picks the
// source (webcam, mouse, synthetic, none) for this run.
#include "x360/nui.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "bridge.h"
#include "image_stream.h"
#include "source.h"
#include "x360/kernel.h"
#include "x360/settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace x360::kernel;

namespace x360::nui {

namespace {

constexpr double kFrameRate = 30.0;
constexpr uint32_t kSkeletonFrameSize = 0xAB0;
constexpr uint32_t kSkeletonDataSize = 0x1C0;
constexpr uint32_t kTitleSetsTracked = 2;  // NUI_SKELETON_TRACKING_FLAG_TITLE_SETS_TRACKED_SKELETONS
enum : uint32_t { kSkeletonNotTracked = 0, kSkeletonPositionOnly = 1, kSkeletonTracked = 2 };

std::mutex g_mutex;
std::condition_variable g_frame_cv;
TitleSupport g_title;

bool g_initialized = false;
// The source has its own lock: polling it (a 1.38 MB copy from the pose bridge, starting the
// bridge) doesn't hold up the title's SkeletonGetNextFrame or the settings panel, which wait on
// g_mutex. Taken before g_mutex when both are needed.
std::mutex g_source_mutex;
std::unique_ptr<Source> g_source;
std::string g_source_message;  // the source's message and numbers after the last poll (g_mutex)
Status g_source_stats;
std::string g_source_name;
std::thread g_clock;
bool g_stop = false;
float g_camera_height = 0.8f;
uint32_t g_players = 2;
settings::KinectSettings g_applied;  // the settings the source runs with

bool g_tracking = false;
uint32_t g_tracking_flags = 0;
uint32_t g_skeleton_event = 0;
uint32_t g_chosen[2] = {};
int32_t g_elevation = 0;

// The latest frame.
uint32_t g_frame_number = 0;
uint64_t g_frame_time_ms = 0;
Snapshot g_snapshot;
uint32_t g_slots[kSkeletonCount] = {};  // body ID in each of the frame's six slots (0: free)
uint32_t g_delivered = 0;               // frame number last returned by SkeletonGetNextFrame

const bool g_trace = std::getenv("ANYX360_NUI_TRACE") != nullptr;

uint64_t NowMs() {
  static const auto start = std::chrono::steady_clock::now();
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                      .count());
}

void StoreFloat(uint32_t address, float value) { Store<uint32_t>(address, std::bit_cast<uint32_t>(value)); }

void StoreVector(uint32_t address, const float v[3], float w) {
  StoreFloat(address + 0, v[0]);
  StoreFloat(address + 4, v[1]);
  StoreFloat(address + 8, v[2]);
  StoreFloat(address + 12, w);
}

// A body keeps its slot while it stays in view; newcomers take the first free one.
void AssignSlots() {
  for (uint32_t& slot : g_slots) {
    bool present = false;
    for (uint32_t i = 0; i < g_snapshot.body_count; ++i) present |= g_snapshot.bodies[i].id == slot;
    if (!present) slot = 0;
  }
  for (uint32_t i = 0; i < g_snapshot.body_count; ++i) {
    uint32_t id = g_snapshot.bodies[i].id;
    if (std::find(std::begin(g_slots), std::end(g_slots), id) != std::end(g_slots)) continue;
    if (uint32_t* free = std::find(std::begin(g_slots), std::end(g_slots), 0u); free != std::end(g_slots)) *free = id;
  }
}

bool IsChosen(uint32_t id) { return id && (id == g_chosen[0] || id == g_chosen[1]); }

// Fully tracked bodies: the title's choice, or the first `players` in view.
bool IsTracked(uint32_t id, uint32_t order) {
  if (g_tracking_flags & kTitleSetsTracked) return IsChosen(id);
  return order < g_players;
}

void WriteSkeletonFrame(uint32_t frame) {
  std::memset(Ptr(frame), 0, kSkeletonFrameSize);
  Store<uint64_t>(frame + 0x00, g_frame_time_ms);
  Store<uint32_t>(frame + 0x08, g_frame_number);
  // The floor is camera_height below the sensor; elevation tilts its normal (and gravity's).
  const float tilt = float(g_elevation) * 3.14159265f / 180.0f;
  const float up[3] = {0, std::cos(tilt), std::sin(tilt)};
  StoreVector(frame + 0x10, up, g_camera_height);
  StoreVector(frame + 0x20, up, 0);
  uint32_t order = 0;
  for (uint32_t slot = 0; slot < kSkeletonCount; ++slot) {
    if (!g_slots[slot]) continue;
    const Body* body = nullptr;
    for (uint32_t i = 0; i < g_snapshot.body_count; ++i)
      if (g_snapshot.bodies[i].id == g_slots[slot]) body = &g_snapshot.bodies[i];
    if (!body) continue;
    const uint32_t data = frame + 0x30 + slot * kSkeletonDataSize;
    const bool tracked = IsTracked(body->id, order++);
    Store<uint32_t>(data + 0x00, tracked ? kSkeletonTracked : kSkeletonPositionOnly);
    Store<uint32_t>(data + 0x04, body->id);
    StoreVector(data + 0x10, body->position, 1);
    if (!tracked) continue;
    for (uint32_t j = 0; j < kJointCount; ++j) {
      StoreVector(data + 0x20 + j * 16, body->joints[j], 1);
      Store<uint32_t>(data + 0x160 + j * 4, body->joint_state[j]);
    }
    Store<uint32_t>(data + 0x1B0, body->quality);
  }
}

std::unique_ptr<Source> MakeSource(const settings::KinectSettings& kinect, std::string& name) {
  if (const char* script = std::getenv("ANYX360_NUI_SCRIPT")) {
    name = "synthetic";
    return MakeSyntheticSource(script, kinect.camera_height);
  }
  name = kinect.source;
  if (const char* source = std::getenv("ANYX360_NUI_SOURCE")) name = source;  // debugging switch
  if (name == "synthetic") return MakeSyntheticSource("0:enter", kinect.camera_height);
  if (name == "mouse") return MakeMouseSource(kinect.camera_height);
  if (name == "webcam") return MakeBridgeSource(kinect);
  return nullptr;  // none
}

GuideGesture g_guide;

// The player whose gestures count: the first the title chose to track, else the first in view.
const Body* MainBody(const Snapshot& snapshot) {
  for (uint32_t i = 0; i < snapshot.body_count; ++i)
    if (snapshot.bodies[i].id && (snapshot.bodies[i].id == g_chosen[0] || snapshot.bodies[i].id == g_chosen[1]))
      return &snapshot.bodies[i];
  return snapshot.body_count ? &snapshot.bodies[0] : nullptr;
}

void ClockThread() {
  SetThreadDescription(GetCurrentThread(), L"NUI clock");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  const auto start = std::chrono::steady_clock::now();
  const auto period = std::chrono::duration<double>(1.0 / kFrameRate);
  for (uint64_t tick = 1;; ++tick) {
    auto due = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(period * double(tick));
    auto wait = due - std::chrono::steady_clock::now();
    if (wait.count() > 0) {
      if (timer) {
        LARGE_INTEGER relative;
        relative.QuadPart = -std::max<int64_t>(
            1, std::chrono::duration_cast<std::chrono::nanoseconds>(wait).count() / 100);
        SetWaitableTimer(timer, &relative, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(timer, INFINITE);
      } else {
        std::this_thread::sleep_until(due);
      }
    }
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    uint32_t skeleton_event = 0;
    Snapshot snapshot;
    uint32_t slots[kSkeletonCount];  // frame slot of each body in `snapshot`
    uint32_t number;
    uint64_t time_ms;
    GuideGesture::Change guide = GuideGesture::kNone;
    std::string message;
    Status stats;
    {
      std::lock_guard source_lock(g_source_mutex);
      if (g_source) {
        g_source->Poll(now, snapshot);
        message = g_source->Message();
        g_source->Stats(stats);
      }
    }
    {
      std::lock_guard lock(g_mutex);
      if (g_stop) break;
      g_source_message = std::move(message);
      g_source_stats = stats;
      g_snapshot = snapshot;
      number = ++g_frame_number;
      time_ms = g_frame_time_ms = NowMs();
      AssignSlots();
      for (uint32_t i = 0; i < kSkeletonCount; ++i) {
        slots[i] = kSkeletonCount;
        for (uint32_t s = 0; s < kSkeletonCount && i < snapshot.body_count; ++s)
          if (g_slots[s] == snapshot.bodies[i].id) slots[i] = s;
      }
      if (g_tracking) skeleton_event = g_skeleton_event;
      guide = g_guide.Update(g_tracking ? MainBody(snapshot) : nullptr, now);
    }
    if (guide != GuideGesture::kNone) SetKinectGuideOpen(guide == GuideGesture::kOpen);
    g_frame_cv.notify_all();
    if (skeleton_event)
      if (uint32_t body = ObjectFromHandle(skeleton_event)) SetEvent(body);
    for (uint32_t event : PublishImageFrames(snapshot, slots, number, time_ms))
      if (uint32_t body = ObjectFromHandle(event)) SetEvent(body);
  }
  if (timer) CloseHandle(timer);
}

}  // namespace

bool IsGuidePose(const Body& b) {
  struct V {
    float x, y, z;
    V operator-(const V& o) const { return {x - o.x, y - o.y, z - o.z}; }
    float dot(const V& o) const { return x * o.x + y * o.y + z * o.z; }
    float length() const { return std::sqrt(dot(*this)); }
    V unit() const {
      const float l = std::max(length(), 1e-6f);
      return {x / l, y / l, z / l};
    }
  };
  auto at = [&](Joint j) { return V{b.joints[j][0], b.joints[j][1], b.joints[j][2]}; };
  // Measured against the torso, not the room: leaning sideways with the arms down isn't the gesture.
  const V up = (at(kShoulderCenter) - at(kHipCenter)).unit();
  const V left = (at(kShoulderLeft) - at(kShoulderRight)).unit();  // the player's left
  const V arm = at(kHandLeft) - at(kShoulderLeft);
  if (arm.length() < 0.40f) return false;  // bent, or the hand on the body
  const V dir = arm.unit();
  const float down = -dir.dot(up), out = dir.dot(left);
  // Out to the side and down at 45 degrees from the body (30-60), in the body's plane.
  if (down < 0.50f || down > 0.87f || out < 0.50f) return false;
  // The right arm relaxed: the hand hanging at the side, below the chest.
  const V right = at(kHandRight) - at(kShoulderRight);
  return right.length() > 0.30f && -right.unit().dot(up) > 0.80f;
}

GuideGesture::Change GuideGesture::Update(const Body* body, double time) {
  if (!body || !IsGuidePose(*body)) {
    held_since_ = -1;
    if (!open_) return kNone;
    open_ = false;
    return kClose;
  }
  if (open_) return kNone;
  if (held_since_ < 0) held_since_ = time;
  if (time - held_since_ < kHoldSeconds) return kNone;
  open_ = true;
  return kOpen;
}

void SetTitleSupport(TitleSupport support) {
  std::lock_guard lock(g_mutex);
  g_title = std::move(support);
  SetImageTitleSupport(g_title);
}

uint32_t Initialize(uint32_t flags, uint32_t hardware_thread) {
  settings::KinectSettings kinect = settings::Get().kinect;
  static std::once_flag subscribed;  // the settings panel's changes reach the source
  std::call_once(subscribed, [] { settings::Subscribe([](const settings::Settings& s) { ApplySettings(s.kinect); }); });
  std::scoped_lock lock(g_source_mutex, g_mutex);
  if (g_initialized) return kOk;
  g_applied = kinect;
  g_camera_height = kinect.camera_height;
  g_players = kinect.players;
  g_source = MakeSource(kinect, g_source_name);
  g_initialized = true;
  g_stop = false;
  g_clock = std::thread(ClockThread);
  KLOG_I("nui: initialized (flags %08X, hardware thread %d), source %s%s", flags, int32_t(hardware_thread),
         g_source_name.c_str(), g_source ? "" : " (no player)");
  return kOk;
}

void Shutdown() {
  {
    std::lock_guard lock(g_mutex);
    if (!g_initialized) return;
    g_stop = true;
    g_initialized = false;
    g_tracking = false;
  }
  if (g_clock.joinable()) g_clock.join();
  std::scoped_lock lock(g_source_mutex, g_mutex);
  g_source.reset();
  g_source_message.clear();
  CloseImageStreams();
  KLOG_I("nui: shut down");
}

uint32_t SkeletonTrackingEnable(uint32_t event, uint32_t flags) {
  std::lock_guard lock(g_mutex);
  g_tracking = true;
  g_skeleton_event = event;
  g_tracking_flags = flags;
  if (g_trace) KLOG_I("nui: SkeletonTrackingEnable(event %08X, flags %X)", event, flags);
  return kOk;
}

uint32_t SkeletonTrackingDisable() {
  std::lock_guard lock(g_mutex);
  g_tracking = false;
  g_skeleton_event = 0;
  if (g_trace) KLOG_I("nui: SkeletonTrackingDisable");
  return kOk;
}

uint32_t SkeletonGetNextFrame(uint32_t timeout_ms, uint32_t frame) {
  std::unique_lock lock(g_mutex);
  if (!frame) return kInvalidArg;
  if (!g_initialized || !g_tracking) return kFail;
  if (g_frame_number == g_delivered && timeout_ms) {
    auto ready = [] { return g_frame_number != g_delivered || !g_initialized; };
    if (timeout_ms == 0xFFFFFFFF)
      g_frame_cv.wait(lock, ready);
    else
      g_frame_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready);
  }
  if (g_frame_number == g_delivered || !g_initialized) return kFrameNoData;
  g_delivered = g_frame_number;
  WriteSkeletonFrame(frame);
  // The title waits on the event and fetches the frame: like the library, reset the event.
  if (g_skeleton_event)
    if (uint32_t body = ObjectFromHandle(g_skeleton_event)) ResetEvent(body);
  if (g_trace && (g_frame_number % 300) == 1)
    KLOG_I("nui: skeleton frame %u, %u bodies", g_frame_number, g_snapshot.body_count);
  return kOk;
}

uint32_t SkeletonSetTrackedSkeletons(uint32_t ids) {
  if (!ids) return kInvalidArg;
  std::lock_guard lock(g_mutex);
  uint32_t chosen[2];
  for (int i = 0; i < 2; ++i) {
    uint32_t id = Load<uint32_t>(ids + i * 4);
    chosen[i] = id == 0xFFFFFFFF ? 0 : id;
  }
  if (chosen[0] != g_chosen[0] || chosen[1] != g_chosen[1]) {
    if (g_trace) KLOG_I("nui: SetTrackedSkeletons(%u, %u)", chosen[0], chosen[1]);
    g_chosen[0] = chosen[0];
    g_chosen[1] = chosen[1];
  }
  return kOk;
}

uint32_t CameraElevationGetAngle(uint32_t angle_ptr) {
  std::lock_guard lock(g_mutex);
  if (angle_ptr) Store<int32_t>(angle_ptr, g_elevation);
  return kOk;
}

uint32_t CameraElevationSetAngle(int32_t angle) {
  std::lock_guard lock(g_mutex);
  g_elevation = std::clamp(angle, -27, 27);
  if (g_trace) KLOG_I("nui: CameraElevationSetAngle(%d)", angle);
  return kOk;
}

Status GetStatus() {
  std::lock_guard lock(g_mutex);
  Status status;
  status.initialized = g_initialized;
  status.source = g_source_name;
  status.message = !g_source_message.empty() ? g_source_message : (g_initialized ? "no player" : "");
  status.frames = g_frame_number;
  status.bodies = g_snapshot.body_count;
  uint32_t order = 0;
  for (uint32_t slot : g_slots)
    if (slot && IsTracked(slot, order++)) ++status.tracked;
  status.camera_running = g_source_stats.camera_running;
  status.camera_width = g_source_stats.camera_width;
  status.camera_height = g_source_stats.camera_height;
  status.camera_fps = g_source_stats.camera_fps;
  status.inference_ms = g_source_stats.inference_ms;
  status.latency_ms = g_source_stats.latency_ms;
  status.restarts = g_source_stats.restarts;
  return status;
}

std::vector<PreviewBody> GetBodies() {
  std::lock_guard lock(g_mutex);
  std::vector<PreviewBody> bodies;
  uint32_t order = 0;
  for (uint32_t slot : g_slots) {
    if (!slot) continue;
    const bool tracked = IsTracked(slot, order++);
    for (uint32_t i = 0; i < g_snapshot.body_count; ++i) {
      if (g_snapshot.bodies[i].id != slot) continue;
      PreviewBody& b = bodies.emplace_back();
      b.id = slot;
      b.tracked = tracked;
      std::memcpy(b.joints, g_snapshot.bodies[i].joints, sizeof(b.joints));
    }
  }
  return bodies;
}

void ApplySettings(const settings::KinectSettings& kinect) {
  std::unique_ptr<Source> previous;
  {
    std::scoped_lock lock(g_source_mutex, g_mutex);
    if (kinect == g_applied) return;
    g_applied = kinect;
    g_camera_height = kinect.camera_height;
    g_players = std::clamp<uint32_t>(kinect.players, 1, 2);
    if (!g_initialized || std::getenv("ANYX360_NUI_SCRIPT") || std::getenv("ANYX360_NUI_SOURCE")) {
      if (g_source) g_source->Configure(kinect);
      return;
    }
    if (kinect.source != g_source_name) {
      previous = std::move(g_source);
      g_source = MakeSource(kinect, g_source_name);
      KLOG_I("nui: source %s%s", g_source_name.c_str(), g_source ? "" : " (no player)");
    } else if (g_source) {
      g_source->Configure(kinect);
    }
  }
  // The previous source ends on its own thread: the bridge takes up to a second to stop.
  if (previous) std::thread([source = std::move(previous)]() mutable { source.reset(); }).detach();
}

void Calibrate() {
  std::lock_guard lock(g_source_mutex);
  if (g_source) g_source->Calibrate();
}

void RestartSource() {
  std::lock_guard lock(g_source_mutex);
  if (g_source) g_source->Restart();
}

}  // namespace x360::nui
