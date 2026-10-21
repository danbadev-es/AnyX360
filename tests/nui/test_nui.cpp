// Virtual Kinect: the skeleton frame in the XDK layout (big-endian), slots and tracking IDs, the
// title choosing whom to track, and the scripted source's gestures.
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <process.h>
#include <string>
#include <thread>

#include "../../runtime/src/nui/bridge.h"
#include "../../runtime/src/nui/image_stream.h"
#include "../../runtime/src/nui/source.h"
#include "x360/hle.h"
#include "x360/kernel.h"
#include "x360/memory.h"
#include "x360/nui.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

PPCFuncMapping PPCFuncMappings[] = {{0, nullptr}};

using namespace x360::kernel;
namespace nui = x360::nui;

static int failures = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    if (cond) {                                                     \
      std::printf("PASS %s\n", #cond);                              \
    } else {                                                        \
      std::printf("FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++failures;                                                   \
    }                                                               \
  } while (0)

namespace {

constexpr uint32_t kMemory = 0x40000000;
constexpr uint32_t kFrame = kMemory;           // NUI_SKELETON_FRAME (0xAB0)
constexpr uint32_t kIds = kMemory + 0x1000;    // two tracking IDs

float LoadFloat(uint32_t address) { return std::bit_cast<float>(Load<uint32_t>(address)); }
uint32_t Skeleton(uint32_t slot) { return kFrame + 0x30 + slot * 0x1C0; }
float JointY(uint32_t slot, uint32_t joint) { return LoadFloat(Skeleton(slot) + 0x20 + joint * 16 + 4); }

// Waits for the next skeleton frame (the sensor runs at 30 Hz).
uint32_t NextFrame() { return nui::SkeletonGetNextFrame(500, kFrame); }

}  // namespace

int main() {
  x360::GuestMemory mem;
  CHECK(mem.valid());
  CHECK(mem.commit(kMemory, 0x10000));
  x360::hle::set_guest_base(mem.base());
  g_base = mem.base();

  {  // The scripted source: a standing player, then a swipe that crosses in front of the body.
    auto source = nui::MakeSyntheticSource("0:enter,1:menu_right,2:swipe_left/0.9", 0.8f);
    nui::Snapshot s;
    source->Poll(0.5, s);
    CHECK(s.body_count == 1);
    CHECK(s.bodies[0].id == 1001);
    const auto& b = s.bodies[0];
    CHECK(b.joints[nui::kHead][1] > b.joints[nui::kShoulderCenter][1]);
    CHECK(b.joints[nui::kShoulderRight][0] > b.joints[nui::kShoulderLeft][0]);  // +x is the player's right
    CHECK(std::abs(b.joints[nui::kFootLeft][1] - (-0.8f)) < 0.1f);              // feet near the floor
    source->Poll(1.5, s);
    const float raised = s.bodies[0].joints[nui::kHandRight][1];
    CHECK(raised > s.bodies[0].joints[nui::kElbowRight][1] - 0.05f);
    CHECK(s.bodies[0].joints[nui::kHandRight][0] > s.bodies[0].joints[nui::kShoulderRight][0] + 0.4f);
    source->Poll(2.45, s);  // halfway: the hand is across the body, in front of it
    CHECK(s.bodies[0].joints[nui::kHandRight][0] < s.bodies[0].joints[nui::kShoulderCenter][0]);
    CHECK(s.bodies[0].joints[nui::kHandRight][2] < s.bodies[0].joints[nui::kSpine][2] - 0.2f);
    source->Poll(3.5, s);  // back to the side
    CHECK(s.bodies[0].joints[nui::kHandRight][0] > s.bodies[0].joints[nui::kShoulderRight][0] + 0.4f);
  }

  {  // An arm at a given angle, raised from the side over the duration.
    auto source = nui::MakeSyntheticSource("0:enter,1:arm_right_0_45/1", 0.8f);
    nui::Snapshot s;
    source->Poll(1.0, s);
    const float* shoulder = s.bodies[0].joints[nui::kShoulderRight];
    CHECK(s.bodies[0].joints[nui::kHandRight][1] < shoulder[1] - 0.5f);  // hanging at first
    source->Poll(2.5, s);
    const float* hand = s.bodies[0].joints[nui::kHandRight];
    CHECK(std::abs((hand[1] - shoulder[1]) - 0.445f) < 0.02f);  // 45 degrees up, 0.63 m away
    CHECK(std::abs((hand[0] - shoulder[0]) - 0.445f) < 0.02f);  // out to the player's right
  }

  {  // Whole-body moves (Kinect Adventures): jump, crouch, lean, steps, walking, both arms.
    auto source = nui::MakeSyntheticSource(
        "0:enter,1:jump/0.6,2:crouch/1,4:lean_left/1,6:step_right,7:step_right,8:walk_in,9:hands_up,10:arms_out,"
        "11:lower,12:flap/2",
        0.8f);
    nui::Snapshot s;
    source->Poll(0.5, s);
    const auto standing = s.bodies[0];
    auto joint = [&](nui::Joint j, int axis) { return s.bodies[0].joints[j][axis]; };
    source->Poll(1.3, s);  // the top of the jump: everything 30 cm up, feet too
    CHECK(std::abs(joint(nui::kHead, 1) - standing.joints[nui::kHead][1] - 0.30f) < 0.01f);
    CHECK(std::abs(joint(nui::kFootLeft, 1) - standing.joints[nui::kFootLeft][1] - 0.30f) < 0.01f);
    source->Poll(2.5, s);  // crouched: the head 35 cm lower, the feet where they were
    CHECK(std::abs(standing.joints[nui::kHead][1] - joint(nui::kHead, 1) - 0.35f) < 0.01f);
    CHECK(std::abs(joint(nui::kFootLeft, 1) - standing.joints[nui::kFootLeft][1]) < 0.001f);
    CHECK(joint(nui::kKneeLeft, 2) < standing.joints[nui::kKneeLeft][2] - 0.1f);  // knees forward
    source->Poll(3.5, s);  // standing again
    CHECK(std::abs(joint(nui::kHead, 1) - standing.joints[nui::kHead][1]) < 0.001f);
    source->Poll(4.75, s);  // leaning left: the head over the player's left, the hips still
    CHECK(joint(nui::kHead, 0) < standing.joints[nui::kHead][0] - 0.2f);
    CHECK(std::abs(joint(nui::kHipCenter, 0) - standing.joints[nui::kHipCenter][0]) < 0.001f);
    source->Poll(7.5, s);  // two steps right: 0.9 m over, and they stay
    CHECK(std::abs(joint(nui::kHipCenter, 0) - standing.joints[nui::kHipCenter][0] - 0.9f) < 0.01f);
    source->Poll(8.9, s);  // half a metre closer to the sensor
    CHECK(std::abs(standing.joints[nui::kHipCenter][2] - joint(nui::kHipCenter, 2) - 0.5f) < 0.01f);
    source->Poll(9.5, s);  // both hands over the head
    CHECK(joint(nui::kHandRight, 1) > joint(nui::kHead, 1) && joint(nui::kHandLeft, 1) > joint(nui::kHead, 1));
    source->Poll(10.5, s);  // arms out at shoulder height
    CHECK(std::abs(joint(nui::kHandLeft, 1) - joint(nui::kShoulderLeft, 1)) < 0.01f);
    CHECK(joint(nui::kHandLeft, 0) < joint(nui::kShoulderLeft, 0) - 0.6f);
    source->Poll(12.4, s);  // flapping (0.8 s a beat): the top, both hands above the shoulders
    CHECK(joint(nui::kHandLeft, 1) > joint(nui::kShoulderLeft, 1) && joint(nui::kHandRight, 1) > joint(nui::kShoulderRight, 1));
    source->Poll(12.8, s);  // and down at the sides again
    CHECK(joint(nui::kHandLeft, 1) < joint(nui::kHipLeft, 1) && joint(nui::kHandRight, 1) < joint(nui::kHipRight, 1));
  }

  {  // Driving (Kinect Joy Ride): the steering wheel, a boost, leaning forward and back.
    auto source = nui::MakeSyntheticSource(
        "0:enter,1:wheel_0,2:wheel_30/0.5,3:wheel_-30,4:wheel_0,5:boost/1,7:lean_forward/1,9:lean_back/1,11:lower",
        0.8f);
    nui::Snapshot s;
    source->Poll(0.5, s);
    const auto standing = s.bodies[0];
    auto joint = [&](nui::Joint j, int axis) { return s.bodies[0].joints[j][axis]; };
    source->Poll(1.5, s);  // the wheel held level: both hands forward at chest height, together
    const float chest_z = joint(nui::kShoulderCenter, 2);
    CHECK(std::abs(joint(nui::kHandLeft, 1) - joint(nui::kHandRight, 1)) < 0.001f);
    CHECK(std::abs(chest_z - joint(nui::kHandRight, 2) - 0.40f) < 0.01f);
    CHECK(std::abs(chest_z - joint(nui::kHandLeft, 2) - 0.40f) < 0.01f);
    CHECK(std::abs(joint(nui::kHandRight, 0) - joint(nui::kHandLeft, 0) - 0.34f) < 0.01f);
    CHECK(joint(nui::kHandRight, 1) < joint(nui::kShoulderRight, 1) && joint(nui::kHandRight, 1) > joint(nui::kHipRight, 1));
    CHECK(joint(nui::kElbowRight, 1) < joint(nui::kHandRight, 1));  // elbows bent, below the hands
    const float level = joint(nui::kHandRight, 1);
    source->Poll(2.25, s);  // turning right, halfway (15 degrees)
    CHECK(joint(nui::kHandRight, 1) < level - 0.02f && joint(nui::kHandRight, 1) > level - 0.08f);
    source->Poll(2.7, s);  // turned 30 degrees right: the right hand 8.5 cm down, the left up
    CHECK(std::abs(level - joint(nui::kHandRight, 1) - 0.085f) < 0.005f);
    CHECK(std::abs(joint(nui::kHandLeft, 1) - level - 0.085f) < 0.005f);
    source->Poll(3.5, s);  // 30 degrees left: the other way round
    CHECK(joint(nui::kHandLeft, 1) < level - 0.08f && joint(nui::kHandRight, 1) > level + 0.08f);
    CHECK(!nui::IsGuidePose(s.bodies[0]));
    source->Poll(5.45, s);  // boosting: the hands pulled back to the chest
    CHECK(chest_z - joint(nui::kHandRight, 2) < 0.15f && chest_z - joint(nui::kHandLeft, 2) < 0.15f);
    source->Poll(5.8, s);  // then pushed out further than the wheel
    CHECK(chest_z - joint(nui::kHandRight, 2) > 0.45f);
    source->Poll(6.5, s);  // and back on the wheel
    CHECK(std::abs(chest_z - joint(nui::kHandRight, 2) - 0.40f) < 0.01f);
    source->Poll(7.5, s);  // leaning forward: the head towards the sensor and lower, the hips still
    CHECK(joint(nui::kHead, 2) < standing.joints[nui::kHead][2] - 0.2f);
    CHECK(joint(nui::kHead, 1) < standing.joints[nui::kHead][1] - 0.02f);
    CHECK(std::abs(joint(nui::kHipCenter, 2) - standing.joints[nui::kHipCenter][2]) < 0.001f);
    CHECK(std::abs(joint(nui::kHead, 0) - standing.joints[nui::kHead][0]) < 0.001f);
    source->Poll(9.5, s);  // leaning back: away from it
    CHECK(joint(nui::kHead, 2) > standing.joints[nui::kHead][2] + 0.2f);
    source->Poll(10.5, s);  // upright again, still on the wheel
    CHECK(std::abs(joint(nui::kHead, 2) - standing.joints[nui::kHead][2]) < 0.001f);
    CHECK(std::abs(chest_z - joint(nui::kHandRight, 2) - 0.40f) < 0.01f);
    source->Poll(11.5, s);  // lower: the arms hang again
    CHECK(std::abs(joint(nui::kHandRight, 1) - standing.joints[nui::kHandRight][1]) < 0.001f);
    CHECK(std::abs(joint(nui::kHandLeft, 2) - standing.joints[nui::kHandLeft][2]) < 0.001f);
  }

  {  // The Kinect guide gesture: the scripted pose is it, other poses aren't; it fires once per hold.
    auto source = nui::MakeSyntheticSource("0:enter,1:guide,5:lower,6:arm_left_0_0,7:arm_left_0_-80,8:guide", 0.8f);
    nui::Snapshot s;
    source->Poll(0.5, s);
    CHECK(!nui::IsGuidePose(s.bodies[0]));  // standing, arms down
    source->Poll(1.5, s);
    CHECK(nui::IsGuidePose(s.bodies[0]));
    source->Poll(6.5, s);
    CHECK(!nui::IsGuidePose(s.bodies[0]));  // arm out at shoulder height
    source->Poll(7.5, s);
    CHECK(!nui::IsGuidePose(s.bodies[0]));  // arm hanging down
    auto leaning = nui::MakeSyntheticSource("0:enter,1:lean_right/2,4:lean_left/2,7:crouch,9:jump", 0.8f);
    for (double t : {1.5, 2.0, 4.5, 5.0, 7.6, 9.3}) {  // leaning, crouching, jumping with the arms down
      leaning->Poll(t, s);
      CHECK(!nui::IsGuidePose(s.bodies[0]));
    }
    nui::GuideGesture gesture;
    int opened = 0, closed = 0;
    source = nui::MakeSyntheticSource("0:enter,1:guide,5:lower,6:arm_left_0_0,7:arm_left_0_-80,8:guide", 0.8f);
    for (double t = 0; t < 10; t += 1.0 / 30) {
      source->Poll(t, s);
      auto change = gesture.Update(&s.bodies[0], t);
      opened += change == nui::GuideGesture::kOpen;
      closed += change == nui::GuideGesture::kClose;
      if (t > 2.3 && t < 2.45) CHECK(opened == 0);  // not yet: held for less than 1.5 s
      if (t > 2.6 && t < 2.8) CHECK(opened == 1 && gesture.open());  // 1.5 s after the pose began
      // The arm came down at 5 s, but the guide stays open its 3 s minimum (opened at 2.5 s).
      if (t > 5.2 && t < 5.4) CHECK(closed == 0 && gesture.open());
      if (t > 5.6 && t < 5.8) CHECK(closed == 1 && !gesture.open());
    }
    CHECK(opened == 2 && closed == 1);  // opened for each hold (1-5 s, 8 s on), still open at the end
  }

  {  // The guide pose as a webcam sees it: wrong depths and any body size; not reaching towards the
     // screen, not with both arms out, not while the right hand points at a menu.
    auto pose = [](float left_azimuth, float left_elevation, float right_azimuth, float right_elevation) {
      nui::Body b;
      nui::StandingPose(b, 0, 2.5f, 0.8f);
      nui::ArmPose(b, -1, left_azimuth, left_elevation);
      nui::ArmPose(b, +1, right_azimuth, right_elevation);
      return b;
    };
    auto move_hand = [](nui::Body b, float dz) {  // the left hand and wrist nearer the sensor (dz < 0)
      b.joints[nui::kHandLeft][2] += dz;
      b.joints[nui::kWristLeft][2] += dz;
      return b;
    };
    auto scale = [](nui::Body b, float s) {  // a smaller person: every joint towards the hips
      const float hip[3] = {b.joints[nui::kHipCenter][0], b.joints[nui::kHipCenter][1], b.joints[nui::kHipCenter][2]};
      for (auto& joint : b.joints)
        for (int i = 0; i < 3; ++i) joint[i] = hip[i] + (joint[i] - hip[i]) * s;
      return b;
    };
    const nui::Body guide = pose(0, -45, 0, -88);
    CHECK(nui::IsGuidePose(guide));
    CHECK(nui::IsGuidePose(pose(0, -32, 0, -88)) && nui::IsGuidePose(pose(0, -60, 0, -88)));  // 30-60 degrees
    CHECK(nui::IsGuidePose(move_hand(guide, -0.25f)) && nui::IsGuidePose(move_hand(guide, 0.2f)));  // depth errors
    CHECK(nui::IsGuidePose(scale(guide, 0.6f)));  // a child (arms of ~35 cm)
    // Down and out 35 degrees across the body, but as far again towards the screen.
    const nui::GuideMeasure reaching = nui::MeasureGuidePose(pose(60, -35, 0, -88));
    CHECK(reaching.rejected && std::string(reaching.rejected) == "the left arm reaches forward");
    CHECK(!nui::IsGuidePose(pose(0, -45, 0, -45)));  // both arms out
    CHECK(!nui::IsGuidePose(pose(0, -45, 90, -10)));  // the right hand pointing at the screen
    CHECK(!nui::IsGuidePose(pose(0, -15, 0, -88)) && !nui::IsGuidePose(pose(0, -75, 0, -88)));  // too high, too low
    const nui::GuideMeasure m = nui::MeasureGuidePose(guide);
    CHECK(std::abs(m.left_angle - 45) < 3 && m.left_extension > 0.95f && m.right_angle < 5);
  }

  {  // A live script file: what is appended while the game runs happens when it's read.
    const auto path = std::filesystem::temp_directory_path() / ("anyx360_nui_live_" + std::to_string(_getpid()) + ".txt");
    std::FILE* f = _wfopen(path.c_str(), L"wb");
    std::fputs("0:enter\n", f);
    std::fclose(f);
    auto source = nui::MakeSyntheticSource("@" + path.string(), 0.8f);
    nui::Snapshot s;
    source->Poll(0.1, s);
    CHECK(s.body_count == 1);
    const float down = s.bodies[0].joints[nui::kHandRight][1];
    f = _wfopen(path.c_str(), L"ab");
    std::fputs("menu_right\nhand_u", f);  // the second line isn't complete yet
    std::fclose(f);
    source->Poll(0.5, s);
    CHECK(s.bodies[0].joints[nui::kHandRight][1] > down + 0.3f);
    const float raised = s.bodies[0].joints[nui::kHandRight][1];
    f = _wfopen(path.c_str(), L"ab");
    std::fputs("p\n", f);
    std::fclose(f);
    source->Poll(0.8, s);
    source->Poll(1.5, s);
    CHECK(s.bodies[0].joints[nui::kHandRight][1] > raised + 0.05f);  // hand_up
    std::filesystem::remove(path);
  }

  {  // Images drawn from a body: its pixels carry its slot and its depth; its arm is on the mirrored side.
    auto source = nui::MakeSyntheticSource("0:enter,0:menu_right", 0.8f);
    nui::Snapshot s;
    source->Poll(0.5, s);
    const uint32_t slots[nui::kSkeletonCount] = {2, 6, 6, 6, 6, 6};
    std::vector<uint16_t> depth;
    nui::DrawDepth(s, slots, depth);
    CHECK(depth.size() == nui::kDepthWidth * nui::kDepthHeight);
    const float* spine = s.bodies[0].joints[nui::kSpine];
    const uint32_t cx = uint32_t(160 + spine[0] * nui::kDepthFocal / spine[2]);
    const uint32_t cy = uint32_t(120 - spine[1] * nui::kDepthFocal / spine[2]);
    const uint16_t center = depth[cy * nui::kDepthWidth + cx];
    CHECK((center & 7) == 3);                                        // slot 2 -> player index 3
    CHECK(std::abs(int((center >> 3)) - int(spine[2] * 1000)) < 50);  // millimetres
    CHECK((depth[5 * nui::kDepthWidth + 5] & 7) == 0);              // the room has no player
    const float* hand = s.bodies[0].joints[nui::kHandRight];
    const uint32_t hx = uint32_t(160 + hand[0] * nui::kDepthFocal / hand[2]);
    CHECK(hx > cx + 20);  // the raised right hand is on the image's right
    std::vector<uint8_t> color;
    nui::DrawColor(s, color);
    CHECK(color.size() == nui::kColorWidth * nui::kColorHeight * 4);
  }

  // The API, driven by a scripted player who walks in at once.
  _putenv_s("ANYX360_NUI_SCRIPT", "0:enter,0:menu_right");
  CHECK(nui::Initialize(0x4049, 0xFFFFFFFF) == nui::kOk);
  CHECK(nui::SkeletonGetNextFrame(0, kFrame) == nui::kFail);  // tracking not enabled yet
  CHECK(nui::SkeletonTrackingEnable(0, 0) == nui::kOk);

  {  // The frame: header, then the body in slot 0, fully tracked.
    CHECK(NextFrame() == nui::kOk);
    CHECK(Load<uint32_t>(kFrame + 0x08) != 0);                     // frame number
    CHECK(std::abs(LoadFloat(kFrame + 0x14) - 1.0f) < 1e-6f);     // floor normal y
    CHECK(std::abs(LoadFloat(kFrame + 0x1C) - 0.8f) < 1e-6f);     // floor distance: camera height
    CHECK(Load<uint32_t>(Skeleton(0) + 0x00) == 2);               // NUI_SKELETON_TRACKED
    CHECK(Load<uint32_t>(Skeleton(0) + 0x04) == 1001);            // tracking ID
    CHECK(std::abs(LoadFloat(Skeleton(0) + 0x1C) - 1.0f) < 1e-6f);  // position w
    CHECK(Load<uint32_t>(Skeleton(0) + 0x160 + nui::kHead * 4) == 2);
    CHECK(JointY(0, nui::kHead) > JointY(0, nui::kHipCenter));
    CHECK(Load<uint32_t>(Skeleton(1) + 0x00) == 0);               // nobody else
    // No new frame since: an immediate call has no data.
    CHECK(nui::SkeletonGetNextFrame(0, kFrame) == nui::kFrameNoData);
  }

  {  // The title chooses whom to track: until it does, bodies are position only.
    CHECK(nui::SkeletonTrackingEnable(0, 2) == nui::kOk);
    Store<uint32_t>(kIds + 0, 0xFFFFFFFF);
    Store<uint32_t>(kIds + 4, 0xFFFFFFFF);
    CHECK(nui::SkeletonSetTrackedSkeletons(kIds) == nui::kOk);
    CHECK(NextFrame() == nui::kOk);
    CHECK(Load<uint32_t>(Skeleton(0) + 0x00) == 1);     // NUI_SKELETON_POSITION_ONLY
    CHECK(Load<uint32_t>(Skeleton(0) + 0x04) == 1001);
    CHECK(JointY(0, nui::kHead) == 0.0f);               // no joints without tracking
    Store<uint32_t>(kIds + 0, 1001);
    CHECK(nui::SkeletonSetTrackedSkeletons(kIds) == nui::kOk);
    CHECK(NextFrame() == nui::kOk);
    CHECK(Load<uint32_t>(Skeleton(0) + 0x00) == 2);
    nui::Status status = nui::GetStatus();
    CHECK(status.source == "synthetic");
    CHECK(status.bodies == 1 && status.tracked == 1);
  }

  {  // Elevation is clamped to the motor's range.
    CHECK(nui::CameraElevationSetAngle(40) == nui::kOk);
    CHECK(nui::CameraElevationGetAngle(kIds) == nui::kOk);
    CHECK(Load<int32_t>(kIds) == 27);
  }

  {  // A depth pixel on the color image: the cameras share their optical centre, the color camera's
     // view is wider (the depth image's edge is 23 pixels in on a 640x480 color image).
    const uint32_t x = kIds, y = kIds + 4;
    CHECK(nui::ColorPixelFromDepthPixel(2, 0, 159, 119, 2000 << 3, x, y) == nui::kOk);
    CHECK(Load<int32_t>(x) == 319 && Load<int32_t>(y) == 239);
    CHECK(nui::ColorPixelFromDepthPixel(2, 0, 0, 0, 2000 << 3, x, y) == nui::kOk);
    CHECK(Load<int32_t>(x) == 23 && Load<int32_t>(y) == 17);
    CHECK(nui::ColorPixelFromDepthPixel(2, 0, 319, 239, 800 << 3, x, y) == nui::kOk);
    CHECK(Load<int32_t>(x) == 616 && Load<int32_t>(y) == 462);
    CHECK(nui::ColorPixelFromDepthPixel(1, 0, 0, 0, 2000 << 3, x, y) == nui::kOk);  // 320x240 color
    CHECK(Load<int32_t>(x) == 11 && Load<int32_t>(y) == 8);
    CHECK(nui::ColorPixelFromDepthPixel(4, 0, 0, 0, 0, x, y) == nui::kInvalidArg);
  }

  nui::Shutdown();
  CHECK(!nui::GetStatus().initialized);

  {  // A webcam source sets the pace: each of the bridge's frames becomes the title's at once (not at
     // the next tick of a 30 Hz clock), stamped with the time the camera captured it.
    _putenv_s("ANYX360_NUI_SCRIPT", "");
    _putenv_s("ANYX360_NUI_SOURCE", "webcam");
    const std::string channel = "Local\\AnyX360-Nui-test-" + std::to_string(_getpid()) + "-clock";
    _putenv_s("ANYX360_NUI_BRIDGE", channel.c_str());  // the game creates it, a producer attaches
    CHECK(nui::Initialize(0x4049, 0xFFFFFFFF) == nui::kOk);
    CHECK(nui::SkeletonTrackingEnable(0, 0) == nui::kOk);
    nui::BridgeChannel producer;
    CHECK(producer.Open(std::wstring(channel.begin(), channel.end())));
    namespace bridge = nui::bridge;
    bridge::Header* h = producer.header();
    h->producer_version_major = bridge::kVersionMajor;
    h->producer_version_minor = bridge::kVersionMinor;
    h->state = bridge::kRunning;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    auto qpc = [] {
      LARGE_INTEGER t;
      QueryPerformanceCounter(&t);
      return t.QuadPart;
    };
    double worst_ms = 0;
    uint64_t previous_stamp = 0;
    int64_t previous_published = 0;
    bool stamps_follow_capture = true;
    for (uint32_t frame = 1; frame <= 12; ++frame) {
      // Slower than the 30 Hz cap, and about the clock's own 50 ms (Windows sleeps 40-50 ms).
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
      h->heartbeat_qpc = qpc();
      const int64_t published = qpc();
      // Captured 10 ms (frame 1) to 21 ms (frame 12) before: each stamp 1 ms less after the last
      // than the frame was published after the last.
      const int64_t age = int64_t(frequency.QuadPart * (0.009 + 0.001 * frame));
      producer.Publish([&](bridge::Slot& s) {
        s.frame_number = frame;
        s.capture_qpc = published - age;
        s.publish_qpc = published;
        s.body_count = 1;
        s.bodies[0].id = 5;
        s.bodies[0].state = bridge::kBodyTracked;
        for (uint32_t j = 0; j < bridge::kJoints; ++j) {
          s.bodies[0].joints[j][0] = float(frame);
          s.bodies[0].joints[j][2] = 2.0f;
          s.bodies[0].joint_state[j] = 2;
        }
      });
      // The title waits for the frame with this bridge frame's skeleton in it.
      bool seen = false;
      while (!seen && double(qpc() - published) / double(frequency.QuadPart) < 0.2)
        if (nui::SkeletonGetNextFrame(100, kFrame) == nui::kOk)
          seen = LoadFloat(Skeleton(0) + 0x20) == float(frame);
      const double ms = double(qpc() - published) * 1000 / double(frequency.QuadPart);
      CHECK(seen);
      if (frame > 2) {
        worst_ms = std::max(worst_ms, ms);
        const double step = double(Load<uint64_t>(kFrame) - previous_stamp);
        const double expected = double(published - previous_published) * 1000 / double(frequency.QuadPart) - 1;
        stamps_follow_capture &= std::abs(step - expected) <= 2.5;
      }
      previous_stamp = Load<uint64_t>(kFrame);
      previous_published = published;
    }
    std::printf("slowest bridge frame to the title: %.1f ms\n", worst_ms);
    CHECK(worst_ms < 12.0);  // a 30 Hz clock: up to 33 ms
    CHECK(stamps_follow_capture);
    nui::Shutdown();
    _putenv_s("ANYX360_NUI_SOURCE", "");
    _putenv_s("ANYX360_NUI_BRIDGE", "");
  }

  std::printf("\n%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
