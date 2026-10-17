// Image streams of the virtual Kinect (color and depth), used by nui_core.cpp.
#pragma once

#include <cstdint>
#include <vector>

#include "x360/nui.h"

namespace x360::nui {

void SetImageTitleSupport(const TitleSupport& support);
// Makes a new frame available on every open stream; returns the events to signal. Called by the
// sensor clock at 30 Hz.
std::vector<uint32_t> PublishImageFrames(const Snapshot& snapshot, uint32_t frame_number, uint64_t time_ms);
void CloseImageStreams();

}  // namespace x360::nui
