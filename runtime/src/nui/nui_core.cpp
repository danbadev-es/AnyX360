// Virtual Kinect sensor (x360/nui.h): the NUI API, its frames made as the source has them (30 a
// second at most, and at least 20) or on a 30 Hz clock.
//
// Debugging: ANYX360_NUI_TRACE logs the API calls; ANYX360_NUI_SCRIPT drives the sensor with
// scripted poses (synthetic_source.cpp) whatever the settings say; ANYX360_NUI_SOURCE picks the
// source (webcam, mouse, synthetic, none) for this run; ANYX360_NUI_BRIDGE=<name> makes the webcam
// source create its channel under that name and not start the bridge (start one by hand with
// --shm <name> --event <name>-frame, e.g. in a debugger).
#include "x360/nui.h"

#include <algorithm>
#include <atomic>
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
constexpr uint32_t kUserIndexNone = 0xFE;  // XUSER_INDEX_NONE

std::mutex g_mutex;
std::condition_variable g_frame_cv;
TitleSupport g_title;

bool g_initialized = false;
// The source has its own lock: polling it (a 1.38 MB copy from the pose bridge, starting the
// bridge) doesn't hold up the title's SkeletonGetNextFrame or the settings panel, which wait on
// g_mutex. Taken before g_mutex when both are needed.
std::mutex g_source_mutex;
std::unique_ptr<Source> g_source;
uint32_t g_source_generation = 0;  // counts the sources (g_source_mutex): the clock watches its event
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

// For the debug window: the guide gesture's progress, the last 10 s of frame timings, and whether
// it wants the camera's images.
float g_guide_progress = 0;
constexpr size_t kTimingHistory = 400;  // 10 s at up to 35 frames a second, and some
TimingSample g_timings[kTimingHistory];
size_t g_timing_next = 0, g_timing_count = 0;
std::atomic<bool> g_debug_images{false};

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
  // The profile each body belongs to (+0x0C): XUSER_INDEX_NONE unless the system bound it, as the
  // XDK fills it. Fully tracked bodies are bound in order to the signed-in players (the first to
  // player 1). Fruit Ninja Kinect pairs its players with their bodies by this field alone; left 0,
  // the empty slots claimed player 1 and the main menu found no player.
  for (uint32_t slot = 0; slot < kSkeletonCount; ++slot)
    Store<uint32_t>(frame + 0x30 + slot * kSkeletonDataSize + 0x0C, kUserIndexNone);
  uint32_t order = 0, bound = 0;
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
    if (const uint32_t user = bound++; user < 4 && kernel::IsUserSignedIn(user)) Store<uint32_t>(data + 0x0C, user);
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
  if (name == "webcam") {
    BridgeOptions options;
    if (const char* channel = std::getenv("ANYX360_NUI_BRIDGE")) {  // a bridge started by hand (or a test's)
      options.name = std::wstring(channel, channel + std::strlen(channel));
      options.launch = false;
    }
    return MakeBridgeSource(kinect, options);
  }
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

// When the player seems to be trying the guide gesture (the left arm raised out to the side for a
// second) and it isn't recognised, the log says why, every 5 s at most: a tester's log then tells
// which measure their pose misses.
void ExplainGuidePose(const Body& body, double now) {
  static double near_since = -1, logged = -1e9;
  const GuideMeasure m = MeasureGuidePose(body);
  if (!m.rejected || m.left_angle < 15 || m.left_angle > 80 || m.left_extension < 0.4f) {
    near_since = -1;
    return;
  }
  if (near_since < 0) near_since = now;
  if (now - near_since < 1.0 || now - logged < 5.0) return;
  logged = now;
  KLOG_I("nui: not the Kinect guide gesture: %s (left arm %.0f deg out, wanted 28-65; %.0f%% extended, wanted 60; "
         "forward %.2f, at most 1; right arm %.0f deg from down, at most 30, %.0f%% down, wanted 50)",
         m.rejected, m.left_angle, m.left_extension * 100, m.left_forward, m.right_angle, m.right_drop * 100);
}

using Clock = std::chrono::steady_clock;

Clock::duration Seconds(double seconds) {
  return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
}

// Waits until `due`, or until `event` is signalled (if given). True if the event was.
bool WaitUntil(HANDLE timer, Clock::time_point due, HANDLE event = nullptr) {
  const auto wait = due - Clock::now();
  if (wait.count() <= 0) return event && WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
  if (!timer) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(wait).count() + 1;
    if (event) return WaitForSingleObject(event, DWORD(ms)) == WAIT_OBJECT_0;
    std::this_thread::sleep_until(due);
    return false;
  }
  LARGE_INTEGER relative;
  relative.QuadPart = -std::max<int64_t>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(wait).count() / 100);
  SetWaitableTimer(timer, &relative, 0, nullptr, nullptr, FALSE);
  if (!event) return WaitForSingleObject(timer, INFINITE), false;
  const HANDLE both[2] = {event, timer};
  return WaitForMultipleObjects(2, both, FALSE, INFINITE) == WAIT_OBJECT_0;
}

// The frame's time stamp (ms on NowMs' clock): when the camera captured it, if the source knows.
uint64_t FrameTimeMs(int64_t capture_qpc) {
  const uint64_t now = NowMs();
  if (!capture_qpc) return now;
  LARGE_INTEGER qpc, frequency;
  QueryPerformanceCounter(&qpc);
  QueryPerformanceFrequency(&frequency);
  const double age_ms = double(qpc.QuadPart - capture_qpc) * 1000.0 / double(frequency.QuadPart);
  return age_ms >= 0 && age_ms < 1000 && uint64_t(age_ms) < now ? now - uint64_t(age_ms) : now;
}

// A source with a frame event (the pose bridge publishing one) sets the pace: a frame as soon as
// its new one is there, rather than at the next tick of a clock of its own (up to a frame's time
// later). New frames not sooner than kMinPeriod after the last new one, though (a Kinect gives 30 a
// second, and titles count on about that), and a frame anyway kMaxPeriod after the last, so the
// title's frames and the guide gesture go on while the source has nothing new. Other sources: a
// frame every 1/30 s.
constexpr double kMinPeriod = 0.028, kMaxPeriod = 0.050;

void ClockThread() {
  SetThreadDescription(GetCurrentThread(), L"NUI clock");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  const auto start = Clock::now();
  const auto period = Seconds(1.0 / kFrameRate);
  auto last = start, last_new = start, due = start;
  HANDLE frame_event = nullptr;  // a duplicate of the source's: it outlives a source being replaced
  uint32_t generation = ~0u;
  for (;;) {
    {
      std::lock_guard source_lock(g_source_mutex);
      if (generation != g_source_generation) {
        generation = g_source_generation;
        if (frame_event) CloseHandle(frame_event);
        frame_event = nullptr;
        if (HANDLE event = g_source ? HANDLE(g_source->FrameEvent()) : nullptr)
          DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &frame_event,
                          SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, 0);
      }
    }
    bool new_frame = true;  // the source had something new (sources without an event: always)
    if (frame_event) {
      WaitUntil(timer, last_new + Seconds(kMinPeriod));
      const bool fresh = WaitUntil(timer, last + Seconds(kMaxPeriod), frame_event);
      ResetEvent(frame_event);  // the poll below reads the newest frame, whichever signalled
      due = Clock::now();
      if (fresh) last_new = due;
      new_frame = fresh;
    } else {
      due = std::max(due + period, Clock::now() - period);  // after a source with an event: no catching up
      WaitUntil(timer, due);
    }
    last = Clock::now();
    const double now = std::chrono::duration<double>(last - start).count();

    uint32_t skeleton_event = 0;
    Snapshot snapshot;
    uint32_t slots[kSkeletonCount];  // frame slot of each body in `snapshot`
    uint32_t number;
    uint64_t time_ms;
    GuideGesture::Change guide = GuideGesture::kNone;
    std::string message;
    Status stats;
    // The debug window wants the camera's images (1 color, 2 depth) even if the title opened none.
    const uint32_t streams = g_debug_images ? 3u : OpenStreamMask();
    {
      std::lock_guard source_lock(g_source_mutex);
      if (g_source) {
        g_source->SetStreams(streams);
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
      time_ms = g_frame_time_ms = std::max(FrameTimeMs(snapshot.capture_qpc), g_frame_time_ms + 1);
      AssignSlots();
      for (uint32_t i = 0; i < kSkeletonCount; ++i) {
        slots[i] = kSkeletonCount;
        for (uint32_t s = 0; s < kSkeletonCount && i < snapshot.body_count; ++s)
          if (g_slots[s] == snapshot.bodies[i].id) slots[i] = s;
      }
      if (g_tracking) skeleton_event = g_skeleton_event;
      const Body* main = g_tracking ? MainBody(snapshot) : nullptr;
      guide = g_guide.Update(main, now);
      g_guide_progress = float(g_guide.Progress(now));
      if (main) ExplainGuidePose(*main, now);
      const size_t previous = (g_timing_next + kTimingHistory - 1) % kTimingHistory;
      const float period_ms = g_timing_count ? (float(now) - g_timings[previous].time) * 1000.0f : 0.0f;
      g_timings[g_timing_next] = {float(now), stats.capture_ms, stats.detect_ms, stats.process_ms,
                                  stats.render_ms, stats.transfer_ms, period_ms, new_frame};
      g_timing_next = (g_timing_next + 1) % kTimingHistory;
      g_timing_count = std::min(g_timing_count + 1, kTimingHistory);
    }
    if (guide != GuideGesture::kNone) SetKinectGuideOpen(guide == GuideGesture::kOpen);
    g_frame_cv.notify_all();
    if (skeleton_event)
      if (uint32_t body = ObjectFromHandle(skeleton_event)) SetEvent(body);
    for (uint32_t event : PublishImageFrames(snapshot, slots, number, time_ms))
      if (uint32_t body = ObjectFromHandle(event)) SetEvent(body);
  }
  if (frame_event) CloseHandle(frame_event);
  if (timer) CloseHandle(timer);
}

}  // namespace

GuideMeasure MeasureGuidePose(const Body& b) {
  struct V {
    float x, y, z;
    V operator-(const V& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const V& o) const { return x * o.x + y * o.y + z * o.z; }
    float length() const { return std::sqrt(dot(*this)); }
    V unit() const {
      const float l = std::max(length(), 1e-6f);
      return {x / l, y / l, z / l};
    }
  };
  constexpr float kDegrees = 57.29578f;
  auto at = [&](Joint j) { return V{b.joints[j][0], b.joints[j][1], b.joints[j][2]}; };
  // The torso's frame, not the room's: leaning sideways with the arms down isn't the gesture.
  const V up = (at(kShoulderCenter) - at(kHipCenter)).unit();
  const V across = at(kShoulderLeft) - at(kShoulderRight);
  const V left = (across - up * across.dot(up)).unit();  // the player's left, square to `up`
  struct Arm {
    float down, out, forward, length;  // the hand from its shoulder; length: shoulder-elbow-hand
  };
  auto arm = [&](Joint shoulder, Joint elbow, Joint hand) {
    const V v = at(hand) - at(shoulder);
    const float down = -v.dot(up), out = v.dot(left);
    const float forward = (v - up * v.dot(up) - left * out).length();
    return Arm{down, out, forward, std::max((at(elbow) - at(shoulder)).length() + (at(hand) - at(elbow)).length(), 1e-3f)};
  };
  const Arm l = arm(kShoulderLeft, kElbowLeft, kHandLeft), r = arm(kShoulderRight, kElbowRight, kHandRight);
  GuideMeasure m;
  const float l_plane = std::hypot(l.down, l.out);
  m.left_angle = std::atan2(l.out, l.down) * kDegrees;
  m.left_extension = l_plane / l.length;
  m.left_forward = l.forward / std::max(l_plane, 1e-3f);
  m.right_angle = std::atan2(std::abs(r.out), r.down) * kDegrees;
  m.right_drop = r.down / r.length;
  // The left arm out to the side and down, 45 degrees from the body give or take ~20, nearly straight,
  // and across the body's plane rather than towards the screen (where menus are pointed at).
  if (m.left_angle < 28 || m.left_angle > 65) m.rejected = "the left arm isn't at 45 degrees";
  else if (m.left_extension < 0.6f) m.rejected = "the left arm is bent";
  else if (m.left_forward > 1.0f) m.rejected = "the left arm reaches forward";
  // The right arm relaxed: the hand hanging at the side, below the shoulder.
  else if (m.right_angle > 30 || m.right_drop < 0.5f) m.rejected = "the right arm isn't down";
  return m;
}

bool IsGuidePose(const Body& b) { return MeasureGuidePose(b).rejected == nullptr; }

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
  ++g_source_generation;
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
  ++g_source_generation;
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

uint32_t TransformSmooth(uint32_t frame, uint32_t params) {
  (void)params;
  return frame ? kOk : kInvalidArg;
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
  status.tilt_pitch = g_source_stats.tilt_pitch;
  status.tilt_roll = g_source_stats.tilt_roll;
  status.tilt_source = g_source_stats.tilt_source;
  status.floor_height = g_source_stats.floor_height;
  status.capture_ms = g_source_stats.capture_ms;
  status.detect_ms = g_source_stats.detect_ms;
  status.process_ms = g_source_stats.process_ms;
  status.render_ms = g_source_stats.render_ms;
  status.transfer_ms = g_source_stats.transfer_ms;
  status.dropped_frames = g_source_stats.dropped_frames;
  return status;
}

std::shared_ptr<const std::vector<uint8_t>> GetPreviewColor() {
  std::lock_guard lock(g_mutex);
  return g_snapshot.color;
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
      std::memcpy(b.joint_state, g_snapshot.bodies[i].joint_state, sizeof(b.joint_state));
    }
  }
  return bodies;
}

GuideState GetGuideState() {
  std::lock_guard lock(g_mutex);
  return {g_guide_progress, g_guide.open()};
}

DebugView GetDebugView(bool draw_images) {
  DebugView view;
  view.status = GetStatus();
  Snapshot snapshot;
  uint32_t slots[kSkeletonCount];  // frame slot of each of the snapshot's bodies
  {
    std::lock_guard lock(g_mutex);
    view.frame = g_frame_number;
    view.tracking = g_tracking;
    view.title_chooses = (g_tracking_flags & kTitleSetsTracked) != 0;
    view.chosen[0] = g_chosen[0], view.chosen[1] = g_chosen[1];
    view.elevation = g_elevation;
    view.camera_height = g_camera_height;
    view.guide_progress = g_guide_progress;
    view.guide_open = g_guide.open();
    snapshot = g_snapshot;
    uint32_t order = 0;
    for (uint32_t slot = 0; slot < kSkeletonCount; ++slot) {
      if (!g_slots[slot]) continue;
      const bool tracked = IsTracked(g_slots[slot], order++);
      for (uint32_t i = 0; i < g_snapshot.body_count; ++i) {
        const Body& body = g_snapshot.bodies[i];
        if (body.id != g_slots[slot]) continue;
        DebugBody& b = view.bodies.emplace_back();
        b.id = body.id;
        b.slot = slot;
        b.tracked = tracked;
        b.chosen = IsChosen(body.id);
        std::memcpy(b.position, body.position, sizeof(b.position));
        std::memcpy(b.joints, body.joints, sizeof(b.joints));
        std::memcpy(b.joint_state, body.joint_state, sizeof(b.joint_state));
        b.quality = body.quality;
      }
    }
    for (uint32_t i = 0; i < kSkeletonCount; ++i) {
      slots[i] = kSkeletonCount;
      for (uint32_t s = 0; s < kSkeletonCount && i < g_snapshot.body_count; ++s)
        if (g_slots[s] == g_snapshot.bodies[i].id) slots[i] = s;
    }
  }
  view.streams = OpenStreamMask();
  view.color = snapshot.color;
  view.depth = snapshot.depth;
  view.camera_images = snapshot.color || snapshot.depth;
  if (draw_images) {  // a source without a camera: the bodies drawn, as the image streams do
    if (!view.depth) {
      auto depth = std::make_shared<std::vector<uint16_t>>(size_t(kDepthWidth) * kDepthHeight);
      DrawDepth(snapshot, slots, *depth);
      view.depth = depth;
    }
    if (!view.color) {
      auto color = std::make_shared<std::vector<uint8_t>>(size_t(kColorWidth) * kColorHeight * 4);
      DrawColor(snapshot, *color);
      view.color = color;
    }
  }
  return view;
}

std::vector<TimingSample> GetTimingHistory() {
  std::lock_guard lock(g_mutex);
  std::vector<TimingSample> samples;
  samples.reserve(g_timing_count);
  for (size_t i = 0; i < g_timing_count; ++i)
    samples.push_back(g_timings[(g_timing_next + kTimingHistory - g_timing_count + i) % kTimingHistory]);
  size_t first = 0;
  while (first < samples.size() && samples.back().time - samples[first].time > 10.0f) ++first;
  samples.erase(samples.begin(), samples.begin() + first);
  return samples;
}

void SetDebugImages(bool want) { g_debug_images = want; }

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
      ++g_source_generation;
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
