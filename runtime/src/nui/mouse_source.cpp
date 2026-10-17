// The mouse as a virtual Kinect player, for menus without a camera: a person stands 2.3 m from the
// sensor with the right arm raised; the hand follows the cursor over the game window (up and down,
// and from out to the side on the right edge to across the body on the left edge).
//   left click   the right hand swipes across the body (select)
//   right click  the left arm swipes across from its side (back)
//   Space        the left arm held at 45 degrees (the Kinect guide gesture)
// While the window isn't in the foreground (or the settings panel is open) the arms rest.
#include <algorithm>
#include <cmath>

#include "source.h"
#include "x360/input.h"

namespace x360::nui {

namespace {

constexpr double kSwipeSeconds = 0.6;

// 0 -> 1 -> 0 over a swipe: across in the first third, held, back in the last third.
float Sweep(double t) {
  constexpr float kPi = 3.14159265f;
  const float k = float(std::clamp(t / kSwipeSeconds, 0.0, 1.0));
  if (k < 1.0f / 3) return 0.5f - 0.5f * std::cos(k * 3 * kPi);
  if (k < 2.0f / 3) return 1.0f;
  return 0.5f + 0.5f * std::cos((k - 2.0f / 3) * 3 * kPi);
}

class MouseSource : public Source {
 public:
  explicit MouseSource(float camera_height) : camera_height_(camera_height) {}

  void Poll(double time, Snapshot& out) override {
    input::MouseHand mouse;
    active_ = input::GetMouseHand(mouse);
    out.body_count = 1;
    Body& body = out.bodies[0];
    StandingPose(body, 0, 2.3f, camera_height_);
    body.id = 1001;
    if (!active_) return;

    // A click starts a swipe on its press.
    if (mouse.left && !left_was_down_) right_swipe_ = time;
    if (mouse.right && !right_was_down_) left_swipe_ = time;
    left_was_down_ = mouse.left;
    right_was_down_ = mouse.right;

    // The right hand: the right edge is out to the side, the left edge across the body; the top of
    // the window 50 degrees up, the bottom 35 degrees down.
    float azimuth = 10.0f + (1.0f - mouse.x) * 120.0f;
    const float elevation = 50.0f - mouse.y * 85.0f;
    if (time - right_swipe_ < kSwipeSeconds) azimuth = azimuth + (155.0f - azimuth) * Sweep(time - right_swipe_);
    ArmPose(body, 1.0f, azimuth, elevation);

    if (time - left_swipe_ < kSwipeSeconds) {
      ArmPose(body, -1.0f, 25.0f + 130.0f * Sweep(time - left_swipe_), 0.0f);
    } else if (mouse.space) {
      ArmPose(body, -1.0f, 0.0f, -45.0f);
    }
  }

  std::string Message() const override { return active_ ? "mouse" : "mouse (window inactive)"; }

 private:
  float camera_height_;
  bool active_ = false;
  bool left_was_down_ = false, right_was_down_ = false;
  double right_swipe_ = -10, left_swipe_ = -10;
};

}  // namespace

std::unique_ptr<Source> MakeMouseSource(float camera_height) { return std::make_unique<MouseSource>(camera_height); }

}  // namespace x360::nui
