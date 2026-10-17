// Virtual Kinect: the skeleton frame in the XDK layout (big-endian), slots and tracking IDs, the
// title choosing whom to track, and the scripted source's gestures.
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <process.h>
#include <string>

#include "../../runtime/src/nui/image_stream.h"
#include "../../runtime/src/nui/source.h"
#include "x360/hle.h"
#include "x360/kernel.h"
#include "x360/memory.h"
#include "x360/nui.h"

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

  nui::Shutdown();
  CHECK(!nui::GetStatus().initialized);

  std::printf("\n%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
