// Image streams of the virtual Kinect (color and depth), used by nui_core.cpp.
#pragma once

#include <cstdint>
#include <vector>

#include "x360/nui.h"

namespace x360::nui {

void SetImageTitleSupport(const TitleSupport& support);
// Makes a new frame available on every open stream; returns the events to signal. Called by the
// sensor clock at 30 Hz, without its lock. `slots[i]`: the frame slot of snapshot.bodies[i].
std::vector<uint32_t> PublishImageFrames(const Snapshot& snapshot, const uint32_t slots[kSkeletonCount],
                                         uint32_t frame_number, uint64_t time_ms);
void CloseImageStreams();

// Images drawn from the bodies, for sources without a camera: each body as capsules between its
// joints, at its depth (and in a color per player), over a room. Exposed for tests.
void DrawDepth(const Snapshot& snapshot, const uint32_t slots[kSkeletonCount], std::vector<uint16_t>& depth);
void DrawColor(const Snapshot& snapshot, std::vector<uint8_t>& bgra);

}  // namespace x360::nui
