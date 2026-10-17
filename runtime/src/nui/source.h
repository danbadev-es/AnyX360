// Sources of the virtual Kinect's skeletons (and images).
#pragma once

#include <memory>
#include <string>

#include "x360/nui.h"

namespace x360::nui {

class Source {
 public:
  virtual ~Source() = default;
  // The people in view at `time` (seconds since the sensor started). Called at 30 Hz.
  virtual void Poll(double time, Snapshot& out) = 0;
  // The source's state for the settings panel ("ok", "no camera found"...).
  virtual std::string Message() const { return "ok"; }
};

// Scripted test poses: "seconds:action[/duration][@player],..." (see synthetic_source.cpp).
std::unique_ptr<Source> MakeSyntheticSource(const std::string& script, float camera_height);

// A standing person, joints in camera space, `x`/`z` their position on the floor.
void StandingPose(Body& body, float x, float z, float camera_height);

}  // namespace x360::nui
