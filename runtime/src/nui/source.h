// Sources of the virtual Kinect's skeletons (and images).
#pragma once

#include <memory>
#include <string>

#include "x360/nui.h"
#include "x360/settings.h"

namespace x360::nui {

class Source {
 public:
  virtual ~Source() = default;
  // The people in view at `time` (seconds since the sensor started). Called at 30 Hz.
  virtual void Poll(double time, Snapshot& out) = 0;
  // The source's state for the settings panel ("ok", "no camera found"...).
  virtual std::string Message() const { return "ok"; }
  // The [kinect] settings changed (camera, mirror, players...).
  virtual void Configure(const settings::KinectSettings&) {}
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

}  // namespace x360::nui
