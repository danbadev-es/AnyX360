// Sources of the virtual Kinect's skeletons (and images).
#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include "x360/nui.h"
#include "x360/settings.h"

namespace x360::nui {

class Source {
 public:
  virtual ~Source() = default;
  // The people in view at `time` (seconds since the sensor started). Called for each sensor frame.
  virtual void Poll(double time, Snapshot& out) = 0;
  // The source's state for the settings panel ("ok", "no camera found"...).
  virtual std::string Message() const { return "ok"; }
  // The [kinect] settings changed (camera, mirror, players...).
  virtual void Configure(const settings::KinectSettings&) {}
  // Recentre the play space on the player in view.
  virtual void Calibrate() {}
  // Start again after failures (the pose bridge).
  virtual void Restart() {}
  // The camera's numbers for the status (webcam).
  virtual void Stats(Status&) const {}
  // An event (a Windows HANDLE) signalled when the source has a new frame, so the sensor's frame
  // follows it at once; nullptr: the sensor polls on its 30 Hz clock.
  virtual void* FrameEvent() const { return nullptr; }
  // The images the title opened streams for (image_stream.h, OpenStreamMask).
  virtual void SetStreams(uint32_t) {}
};

// Scripted test poses: "seconds:action[/duration][@player],..." (see synthetic_source.cpp).
std::unique_ptr<Source> MakeSyntheticSource(const std::string& script, float camera_height);

// The mouse over the game window: the right hand follows the cursor (see mouse_source.cpp).
std::unique_ptr<Source> MakeMouseSource(float camera_height);

// A standing person, joints in camera space, `x`/`z` their position on the floor.
void StandingPose(Body& body, float x, float z, float camera_height);

// An arm held straight from its shoulder. side: +1 the player's right arm, -1 their left.
// azimuth: 0 pointing out to the side, 90 forward at the sensor, more across the body (degrees);
// elevation: up from horizontal (degrees).
void ArmPose(Body& body, float side, float azimuth, float elevation);

// The Kinect Guide gesture: the left arm held straight out and down at about 45 degrees, away from
// the body, the right arm relaxed at the side. On the console the system recognises it (the title
// only sees its UI open); here the sensor does (nui_core.cpp).
bool IsGuidePose(const Body& body);

// Holding the pose for `kHoldSeconds` opens the Kinect Guide; it closes when the pose ends (the
// arm comes down, or the player leaves).
class GuideGesture {
 public:
  static constexpr double kHoldSeconds = 1.5;
  enum Change { kNone, kOpen, kClose };
  // The main player's body this frame (nullptr: nobody).
  Change Update(const Body* body, double time);
  bool open() const { return open_; }
  // How long the pose has been held, as a fraction of kHoldSeconds (0 when not held).
  double Progress(double time) const {
    return held_since_ < 0 ? 0.0 : std::min(1.0, (time - held_since_) / kHoldSeconds);
  }

 private:
  double held_since_ = -1;
  bool open_ = false;
};

}  // namespace x360::nui
