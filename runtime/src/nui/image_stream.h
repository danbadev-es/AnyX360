// Image streams of the virtual Kinect (color and depth), used by nui_core.cpp.
#pragma once

#include <cstdint>
#include <vector>

#include "x360/nui.h"

namespace x360::nui {

void SetImageTitleSupport(const TitleSupport& support);
// Makes a new frame available on every open stream; returns the events to signal. Called by the
// sensor clock for each frame, without its lock. `slots[i]`: the frame slot of snapshot.bodies[i].
// A camera's images that haven't changed since the last call (the source had nothing new) aren't
// converted and handed out again.
std::vector<uint32_t> PublishImageFrames(const Snapshot& snapshot, const uint32_t slots[kSkeletonCount],
                                         uint32_t frame_number, uint64_t time_ms);
void CloseImageStreams();
// The images the title has streams open for, as the bridge's stream_mask: 1 color, 2 depth, 4 none.
uint32_t OpenStreamMask();

// Images drawn from the bodies, for sources without a camera: each body as capsules between its
// joints, at its depth (and in a color per player), over a room. Exposed for tests.
void DrawDepth(const Snapshot& snapshot, const uint32_t slots[kSkeletonCount], std::vector<uint16_t>& depth);
void DrawColor(const Snapshot& snapshot, std::vector<uint8_t>& bgra);

}  // namespace x360::nui
